#include "uvm.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static char rd(char *p) { return *(volatile char *)p; }
static void wr(char *p, char v) { *(volatile char *)p = v; }

static int in_ram(char *p) {
  unsigned char vec;
  int rc = mincore(p, uvm_pagesize(), &vec);
  assert(rc == 0);
  return vec & 1; /* 1이면 RAM에 있음, 0이면 없음 */
}

static int run(const char *name, void (*fn)(void)) {
  fflush(stdout); /* 자식이 버퍼를 물려받아 중복 출력하지 않게 */

  pid_t pid = fork();
  if (pid == -1) {
    perror("fork");
    exit(1);
  }
  if (pid == 0) {
    fn();
    exit(0);
  }
  int status;
  if (waitpid(pid, &status, 0) == -1) {
    perror("waitpid");
    exit(1);
  }
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    printf("PASS %s\n", name);
    return 0;
  }
  if (WIFEXITED(status))
    printf("FAIL %s: exit %d\n", name, WEXITSTATUS(status));
  else if (WIFSIGNALED(status))
    printf("FAIL %s: killed by signal %d\n", name, WTERMSIG(status));
  return 1;
}

static char *page(void *base, size_t i) {
  return (char *)base + uvm_pagesize() * i;
}

static void *setup(size_t limit, size_t npages) {
  // uvm_init → set_max_resident → uvm_alloc
  if (uvm_init() == -1) {
    perror("uvm_init");
    exit(1);
  }
  void *res = uvm_alloc(npages);
  if (res == NULL) {
    perror("uvm_alloc");
    exit(1);
  }
  if (uvm_set_max_resident(limit) == -1) {
    perror("uvm_set_max_resident");
    exit(1);
  }
  return res;
}

static void test_t1(void) {
  void *addr = setup(4, 10);
  for (int i = 0; i < 10; i++) {
    wr(page(addr, i), (char)i);
    assert(uvm_resident() <= 4);
  }
  assert(uvm_faults() == 20);
  assert(uvm_evictions() == 6);
  assert(uvm_writebacks() == 6);
  assert(uvm_resident() == 4);
  for (int i = 0; i < 6; i++) {
    assert(uvm_page_state(page(addr, i)) == PG_EVICTED);
    assert(!in_ram(page(addr, i)));
  }
  for (int i = 6; i < 10; i++) {
    assert(uvm_page_state(page(addr, i)) == PG_DIRTY);
    assert(in_ram(page(addr, i)));
  }
}

static void test_t2(void) {
  void *addr = setup(4, 10);
  for (int i = 0; i < 10; i++) {
    wr(page(addr, i), (char)i);
    assert(uvm_resident() <= 4);
  }
  assert(uvm_faults() == 20);
  assert(uvm_evictions() == 6);
  assert(uvm_writebacks() == 6);
  assert(uvm_resident() == 4);
  for (int i = 0; i < 6; i++) {
    assert(uvm_page_state(page(addr, i)) == PG_EVICTED);
    assert(!in_ram(page(addr, i)));
  }
  for (int i = 6; i < 10; i++) {
    assert(uvm_page_state(page(addr, i)) == PG_DIRTY);
    assert(in_ram(page(addr, i)));
  }
  size_t before_fault = uvm_faults();
  size_t before_eviction = uvm_evictions();
  size_t before_writebacks = uvm_writebacks();

  for (int i = 0; i < 10; i++) {
    char v = rd(page(addr, i));
    assert(v == (char)i);
    assert(uvm_resident() <= 4);
  }

  for (int i = 0; i < 6; i++) {
    assert(uvm_page_state(page(addr, i)) == PG_EVICTED);
    assert(!in_ram(page(addr, i)));
  }
  for (int i = 6; i < 10; i++) {
    assert(uvm_page_state(page(addr, i)) == PG_READ);
    assert(in_ram(page(addr, i)));
  }

  assert(uvm_faults() - before_fault == 10);
  assert(uvm_evictions() - before_eviction == 10);
  assert(uvm_writebacks() - before_writebacks == 4);
}

static void test_t3(void) {
  void *addr = setup(4, 10);
  for (int i = 0; i < 10; i++) {
    char v = rd(page(addr, i));
    assert(v == 0);
    assert(uvm_resident() <= 4);
  }
  assert(uvm_faults() == 10);
  assert(uvm_evictions() == 6);
  assert(uvm_writebacks() == 0);
  assert(uvm_resident() == 4);
  for (int i = 0; i < 6; i++) {
    assert(uvm_page_state(page(addr, i)) == PG_EVICTED);
    assert(!in_ram(page(addr, i)));
  }
  for (int i = 6; i < 10; i++) {
    assert(uvm_page_state(page(addr, i)) == PG_READ);
    assert(in_ram(page(addr, i)));
  }
}
static void test_t4(void) {
  void *addr = setup(3, 4);
  for (int i = 0; i < 4; i++) {
    char v = rd(page(addr, i));
    assert(v == 0);
    assert(uvm_resident() <= 3);
  }
  assert(uvm_page_state(page(addr, 0)) == PG_EVICTED);
  for (int i = 1; i < 4; i++) {
    assert(uvm_page_state(page(addr, i)) == PG_READ);
  }
  assert(uvm_resident() == 3);
}

static void test_t5(void) {
  void *addr = setup(2, 8);
  size_t ps = uvm_pagesize();
  for (int i = 0; i < 8; i++) {
    memset(page(addr, i), i, ps);
    assert(uvm_resident() <= 2);
  }
  assert(uvm_writebacks() == 6); // 6,7은 지금 큐에 있음
  assert(uvm_evictions() == 6);
  char *expect = malloc(ps);
  assert(expect != NULL);
  for (int i = 0; i < 8; i++) {
    memset(expect, i, ps);
    int diff = memcmp(page(addr, i), expect, ps);
    assert(diff == 0);

    memset(page(addr, i), i + 10, ps);
    assert(uvm_resident() <= 2);
  }
  assert(uvm_writebacks() == 14);
  assert(uvm_evictions() == 14);
  for (int i = 0; i < 8; i++) {
    memset(expect, i + 10, ps);
    int diff = memcmp(page(addr, i), expect, ps);
    assert(diff == 0);
    assert(uvm_resident() <= 2);
  }
  free(expect);
  assert(uvm_writebacks() == 16);
  assert(uvm_evictions() == 22);
}

static void test6(int policy) {
  void *addr = setup(3, 21);
  int s = uvm_set_policy(policy);
  assert(s == 0);

  char *H = page(addr, 0);
  size_t h_evicted = 0;

  char v = rd(H);
  assert(v == 0);
  assert(uvm_resident() <= 3);

  for (size_t i = 1; i < 21; i++) {
    /* 차가운 페이지 i */
    v = rd(page(addr, i));
    assert(v == 0);
    assert(uvm_resident() <= 3);

    /* H를 읽기 직전: 쫓겨나 있었나? */
    if (uvm_page_state(H) == PG_EVICTED)
      h_evicted++;

    v = rd(H);
    assert(v == 0);
    assert(uvm_resident() <= 3);
  }

  printf("  [%s] h_evicted=%zu evictions=%zu faults=%zu ref_faults=%zu\n",
         policy == UVM_FIFO ? "FIFO " : "CLOCK", h_evicted, uvm_evictions(),
         uvm_faults(), uvm_ref_faults());

  if (policy == UVM_FIFO) {
    assert(h_evicted >= 5);        /* H를 주기적으로 쫓아냄 */
    assert(uvm_ref_faults() == 0); /* 관찰 폴트 없음 */
  } else {
    assert(h_evicted <= 1);       /* 처음 꽉 찼을 때 한 번뿐 */
    assert(uvm_ref_faults() > 0); /* 관찰 폴트로 H를 지킴 */
  }
}

static void test_t6_fifo(void) { test6(UVM_FIFO); }
static void test_t6_clock(void) { test6(UVM_CLOCK); }

int main(void) {
  int nfail = 0;

  nfail += run("T1", test_t1);
  nfail += run("T2", test_t2);
  nfail += run("T3", test_t3);
  nfail += run("T4", test_t4);
  nfail += run("T5", test_t5);
  nfail += run("T6 FIFO", test_t6_fifo);
  nfail += run("T6 CLOCK", test_t6_clock);

  printf("%d failed\n", nfail);
  return nfail ? 1 : 0;
}