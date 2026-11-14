#include "uvm.h"
#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#define MAXREGIONS 16

typedef struct region {
  uintptr_t start, end;
  unsigned char *state;
} region;

static size_t pagesize;
static volatile sig_atomic_t fault_success = 0;
static struct region regions[MAXREGIONS];
static size_t nregions = 0; // 지금까지 채운 개수

int uvm_init(void);
static region *find_region(uintptr_t addr);
void *uvm_alloc(size_t npages);
size_t uvm_faults(void);
size_t uvm_pagesize(void);
int uvm_page_state(void *addr); // addr가 속한 페이지의 상태. 영역 밖이면 -1
size_t uvm_dirty_count(void); // 모든 영역의 DIRTY 페이지 수

int uvm_page_state(void *addr) {
  region *r = find_region((uintptr_t)addr);
  if (r == NULL)
    return -1;
  return r->state[((uintptr_t)addr - r->start) / pagesize];
}

size_t uvm_dirty_count(void) {
  size_t count = 0;
  for (size_t i = 0; i < nregions; i++) {
    size_t pnum = (regions[i].end - regions[i].start) / pagesize;
    for (size_t j = 0; j < pnum; j++) {
      if (regions[i].state[j] == PG_DIRTY)
        count++;
    }
  }
  return count;
}

size_t uvm_pagesize(void) { return pagesize; }

size_t uvm_faults(void) { return (size_t)fault_success; }

static region *find_region(uintptr_t addr) {
  for (size_t i = 0; i < nregions; i++) {
    if (regions[i].start <= addr && addr < regions[i].end) {
      return &regions[i];
    }
  }
  return NULL;
}

static void segv_handler(int sig, siginfo_t *info, void *ucontext) {
  int olderrno = errno;
  (void)ucontext;
  uintptr_t addr = (uintptr_t)info->si_addr;
  region *r = find_region(addr);
  struct sigaction dfl = {0};
  dfl.sa_handler = SIG_DFL;
  sigemptyset(&dfl.sa_mask);
  if (!r) {
    goto fallback;
  }
  addr = addr & ~(pagesize - 1);
  size_t idx = (addr - r->start) / pagesize;
  int prot;
  unsigned char pg_state;
  switch (r->state[idx]) {
  case PG_UNTOUCHED:
    prot = PROT_READ;
    pg_state = PG_READ;
    break;

  case PG_READ:
    prot = PROT_READ | PROT_WRITE;
    pg_state = PG_DIRTY;
    break;
  case PG_DIRTY: {
    static const char msg[] = "uvm: unexpected fault on dirty page\n";
    ssize_t s = write(STDERR_FILENO, msg, sizeof msg - 1);
    (void)s;
    goto fallback;
  }
  default:
    goto fallback;
  }
  if (mprotect((void *)addr, pagesize, prot) == -1) {
    static const char msg[] = "uvm: mprotect failed\n";
    ssize_t s = write(STDERR_FILENO, msg, sizeof msg - 1);
    (void)s;
    _exit(1);
  }
  r->state[idx] = pg_state;
  fault_success++;
  errno = olderrno;
  return;
fallback:
  if (sigaction(sig, &dfl, NULL) == -1) {
    static const char msg[] = "uvm: sigaction failed\n";
    ssize_t s = write(STDERR_FILENO, msg, sizeof msg - 1);
    (void)s;
    _exit(1);
  }
  errno = olderrno;
  return;
}

int uvm_init(void) {
  long ps = sysconf(_SC_PAGESIZE);
  if (ps <= 0) {
    return -1;
  }
  pagesize = (size_t)ps;
  struct sigaction sa = {0};
  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = segv_handler;
  if (sigaction(SIGSEGV, &sa, NULL) == -1)
    return -1;
  return 0;
}

void *uvm_alloc(size_t npages) {
  if (npages == 0 || pagesize == 0 || nregions == MAXREGIONS)
    return NULL;
  if (npages > SIZE_MAX / pagesize)
    return NULL;
  size_t size = npages * pagesize;
  unsigned char *state = calloc(npages, sizeof(char));
  if (state == NULL)
    return NULL;
  void *addr = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (addr == MAP_FAILED) {
    free(state);
    return NULL;
  }
  regions[nregions] = (region){
      .start = (uintptr_t)addr, .end = (uintptr_t)addr + size, .state = state};
  atomic_signal_fence(memory_order_seq_cst);
  nregions++;
  return addr;
}