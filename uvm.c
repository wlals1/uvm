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
#define UVM_MAX_RESIDENT 4096

typedef struct region {
  uintptr_t start, end;
  unsigned char *state, *ref;
  char *swap;
} region;

static size_t pagesize;
static volatile sig_atomic_t fault_success;
static struct region regions[MAXREGIONS];
static size_t nregions;
static uintptr_t resident[UVM_MAX_RESIDENT];
static volatile size_t hand, nresident;
static size_t max_resident = UVM_MAX_RESIDENT;
static volatile sig_atomic_t nevictions;
static volatile sig_atomic_t nwritebacks;
static volatile sig_atomic_t nref_faults;
static int policy;

static size_t make_room(void);
int uvm_set_policy(int policy);
int uvm_init(void);
static region *find_region(uintptr_t addr);
void *uvm_alloc(size_t npages);
size_t uvm_faults(void);
size_t uvm_pagesize(void);
int uvm_page_state(void *addr);
size_t uvm_dirty_count(void);
static void evict(uintptr_t page);

int uvm_set_policy(int p) {
  if (p != UVM_FIFO && p != UVM_CLOCK)
    return -1;
  if (nresident != 0) /* 상주 페이지가 있으면 바꾸지 않음 */
    return -1;
  policy = p;
  hand = 0;
  return 0;
}

static size_t page_index(region *r, uintptr_t page) {
  return (page - r->start) / pagesize;
}

static _Noreturn void die_(const char *msg, size_t len) {
  ssize_t s = write(STDERR_FILENO, msg, len);
  (void)s;
  _exit(1);
}
#define DIE(s) die_((s), sizeof(s) - 1)

static void protect(uintptr_t page, int prot) {
  if (mprotect((void *)page, pagesize, prot) == -1)
    DIE("uvm: mprotect failed\n");
}

static char *swap_slot(region *r, size_t idx) {
  return r->swap + idx * pagesize;
}

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
size_t uvm_resident(void) { return nresident; }
size_t uvm_evictions(void) { return nevictions; }
size_t uvm_writebacks(void) { return nwritebacks; }
size_t uvm_ref_faults(void) { return nref_faults; }

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
  if (r == NULL)
    goto fallback;

  uintptr_t page = addr & ~((uintptr_t)pagesize - 1);
  size_t idx = (page - r->start) / pagesize;
  size_t slot;
  switch (r->state[idx]) {
  case PG_UNTOUCHED:
    slot = make_room();
    protect(page, PROT_READ);
    resident[slot] = page;
    r->state[idx] = PG_READ;
    break;

  case PG_EVICTED:
    slot = make_room();
    protect(page, PROT_READ | PROT_WRITE);
    memcpy((void *)page, swap_slot(r, idx), pagesize);
    protect(page, PROT_READ);
    resident[slot] = page;
    r->state[idx] = PG_READ;
    break;

  case PG_READ:
    if (r->ref[idx] == 0) {
      protect(page, PROT_READ);
      nref_faults++;
    } else {
      protect(page, PROT_READ | PROT_WRITE);
      r->state[idx] = PG_DIRTY;
    }
    break;

  case PG_DIRTY: {
    static const char msg[] = "uvm: unexpected fault on dirty page\n";
    ssize_t s = write(STDERR_FILENO, msg, sizeof msg - 1);
    (void)s;
  }
    goto fallback;

  default:
    goto fallback;
  }

  r->ref[idx] = 1;
  fault_success++;
  errno = olderrno;
  return;

fallback: {
  struct sigaction dfl = {0};
  dfl.sa_handler = SIG_DFL;
  sigemptyset(&dfl.sa_mask);
  if (sigaction(sig, &dfl, NULL) == -1)
    DIE("uvm: sigaction failed\n");
}
  errno = olderrno;
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
  if (!state)
    return NULL;
  char *swap = calloc(size, sizeof(char));
  if (!swap)
    goto err_state;
  unsigned char *ref = calloc(npages, sizeof *ref);
  if (!ref)
    goto err_swap;
  void *addr = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (addr == MAP_FAILED)
    goto err_ref;
  regions[nregions] = (region){.start = (uintptr_t)addr,
                               .end = (uintptr_t)addr + size,
                               .state = state,
                               .ref = ref,
                               .swap = swap};
  atomic_signal_fence(memory_order_seq_cst);
  nregions++;
  return addr;
err_ref:
  free(ref);
err_swap:
  free(swap);
err_state:
  free(state);
  return NULL;
}

static void clean(region *r, size_t idx, uintptr_t page) {
  if (r->state[idx] == PG_DIRTY) {
    // 사본 저장
    memcpy(swap_slot(r, idx), (void *)page, pagesize);
    nwritebacks++;
    protect(page, PROT_READ);
    r->state[idx] = PG_READ;
  }
}

static void evict(uintptr_t page) {
  region *r = find_region(page);
  if (r == NULL) {
    DIE("uvm: internal error: evict page not in region\n");
  }
  size_t pn = page_index(r, page);
  if (r->state[pn] == PG_DIRTY) {
    DIE("uvm: internal error: evict called on dirty page\n");
  }
  protect(page, PROT_NONE);
  if (madvise((void *)page, pagesize, MADV_DONTNEED) == -1) {
    // madvise 실패
    DIE("uvm: madvise failed\n");
  }
  r->state[pn] = PG_EVICTED;
  nevictions++;
}

static size_t make_room(void) {
  if (nresident < max_resident)
    return nresident++;

  if (policy == UVM_FIFO) {
    uintptr_t page = resident[hand];
    region *r = find_region(page);
    size_t idx = page_index(r, page);
    clean(r, idx, page);
    evict(page);
    size_t slot = hand;
    hand = (hand + 1) % max_resident;
    return slot;
  }

  for (size_t step = 0; step <= max_resident; step++) {
    uintptr_t page = resident[hand];
    region *r = find_region(page);
    size_t idx = page_index(r, page);

    if (r->ref[idx] == 1) {
      clean(r, idx, page);
      r->ref[idx] = 0;
      protect(page, PROT_NONE);
      hand = (hand + 1) % max_resident;
    } else {
      evict(page);
      size_t slot = hand;
      hand = (hand + 1) % max_resident;
      return slot;
    }
  }
  DIE("uvm: clock found no victim\n");
}

int uvm_set_max_resident(size_t n) {
  if (n == 0 || n > UVM_MAX_RESIDENT)
    return -1;
  if (nresident != 0)
    return -1;
  max_resident = n;
  hand = 0;
  return 0;
}