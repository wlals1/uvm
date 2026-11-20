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

static int run(const char *name, void (*fn)(void)) {
  fflush(stdout);

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

static void test1(void) {
  void *addr = setup(5, 5);
  wr(page(addr, 0), 'A');
  int s = uvm_snapshot(addr);
  assert(s == 0);
  wr(page(addr, 0), 'B');
  s = uvm_restore(addr);
  assert(s == 0);
  char c = rd(page(addr, 0));
  assert(c == 'A');
  assert(uvm_cow_copies() == 1);
}

static void test2(void) {
  void *addr = setup(4, 4);
  for (int i = 0; i < 4; i++) {
    wr(page(addr, i), 'A' + i);
  }
  size_t before_faults = uvm_faults();
  assert(before_faults == 8);
  int s = uvm_snapshot(addr);
  assert(s == 0);

  for (int i = 0; i < 2; i++) {
    // i=0일때 PROT_READ라서 페이지 폴트, 이 때 snap에 저장, 그다음 PROT_WRITE
    // 재실행 때 PTE에 WRITE가 있어 폴트 없이 성공
    // i=1일때 RW 라서 폴트 없이 써짐.
    wr(page(addr, 1), 'Z');
  }
  assert(uvm_cow_copies() == 1);
  s = uvm_restore(addr);
  assert(s == 0);
  for (int i = 0; i < 4; i++) {
    char c = rd(page(addr, i));
    assert(c == 'A' + i);
  }
  assert(uvm_faults() - before_faults == 1);
}

static void test3(void) {
  void *addr = setup(10, 10);
  size_t before = uvm_faults();
  int s = uvm_snapshot(addr);
  assert(s == 0);
  for (size_t i = 0; i < 10; i++) {
    wr(page(addr, i), 'x');
    assert(uvm_faults() - before == 2);
    before = uvm_faults();
    assert(uvm_cow_copies() == (i + 1));
  }
  before = uvm_faults();
  s = uvm_restore(addr);
  assert(s == 0);
  for (size_t i = 0; i < 10; i++) {
    char c = rd(page(addr, i));
    assert(c == 0);
  }
  assert(uvm_faults() == before);
}

static void test4(void) {
  char *addr = (char *)setup(10, 10);
  assert(addr);
  size_t psz = uvm_pagesize();
  int local;

  assert(uvm_restore(addr) == -1);

  assert(uvm_snapshot(addr + 1000) == -1);
  assert(uvm_restore(addr + 1000) == -1);

  assert(uvm_snapshot(addr + psz) == -1);
  assert(uvm_restore(addr + psz) == -1);

  assert(uvm_snapshot(&local) == -1);
  assert(uvm_restore(&local) == -1);

  assert(uvm_restore(addr) == -1);

  assert(uvm_snapshot(addr) == 0);
  assert(uvm_restore(addr) == 0);
}

static void test5(void) {
  void *addr = setup(10, 10);
  wr(page(addr, 0), 'A');
  assert(uvm_snapshot(addr) == 0);

  wr(page(addr, 0), 'B');
  assert(uvm_cow_copies() == 1);
  assert(uvm_restore(addr) == 0);
  assert(rd(page(addr, 0)) == 'A');

  wr(page(addr, 0), 'C');
  assert(uvm_cow_copies() == 2);
  assert(uvm_restore(addr) == 0);
  assert(rd(page(addr, 0)) == 'A');
}

static void test6(int policy) {
  void *addr = setup(2, 8);
  int s = uvm_set_policy(policy);
  assert(s == 0);

  for (int i = 0; i < 8; i++) {
    wr(page(addr, i), 'A' + i);
    assert(uvm_resident() <= 2);
  }
  assert(uvm_evictions() == 6);
  s = uvm_snapshot(addr);
  assert(s == 0);

  for (int i = 0; i < 8; i++) {
    wr(page(addr, i), 'a' + i);
    assert(uvm_resident() <= 2);
  }
  assert(uvm_cow_copies() == 8);
  s = uvm_restore(addr);
  assert(s == 0);

  for (int i = 0; i < 8; i++) {
    char c = rd(page(addr, i));
    assert(c == 'A' + i);
  }
}

static void test6_FIFO(void) { test6(UVM_FIFO); }
static void test6_CLOCK(void) { test6(UVM_CLOCK); }
int main() {
  run("T1", test1);
  run("T2", test2);
  run("T3", test3);
  run("T4", test4);
  run("T5", test5);
  run("T6_FIFO", test6_FIFO);
  run("T6_CLOCK", test6_CLOCK);
  return 0;
}