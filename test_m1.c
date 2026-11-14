#include "uvm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void test1(void *addr, size_t ps) {
  for (char *i = (char *)addr; i < (char *)addr + 1000 * ps; i += (10 * ps)) {
    *i = 'A';
  }
}

void test2(void *addr, size_t ps) {
  for (int i = 0; i < 100; i++) {
    char *c = (char *)addr + ps * 3;
    *c = i;
  }
}

char test3(void *addr, size_t ps) {
  char *c = (char *)addr + 13 * ps;
  return *c;
}

void test5(void *addr, size_t ps) {
  char *c = (char *)addr + ps * 10;
  *c = 'B';
}

void test6(void *addr, size_t ps) {
  char *c = (char *)addr + 1000 * ps - 1;
  *c = 'Z';
}

int main(int argc, char *argv[]) {
  int rc = uvm_init();
  assert(!rc);
  void *addr = uvm_alloc(1000);
  assert(addr != NULL);
  size_t ps = uvm_pagesize();

  if (argc > 1 && strcmp(argv[1], "segv") == 0) {
    // addr 영역 밖이라 segfault 이후 SIGSEGV 핸들러어에서 핸들러 함수 SIG_DFL
    // 로 바뀌고 다음 fault handler에서 코어 덤프, 프로세스 종료. echo $? 확인시
    // 139
    *(volatile int *)0 = 1;
  }

  test1(addr, ps);
  assert(uvm_faults() == 200);

  size_t before = uvm_faults();
  test2(addr, ps);
  assert(uvm_faults() - before == 2);

  before = uvm_faults();
  char res = test3(addr, ps);
  assert(res == 0 && uvm_faults() - before == 1);

  before = uvm_faults();
  test5(addr, ps);
  assert(uvm_faults() - before == 0);

  before = uvm_faults();
  test6(addr, ps);
  assert(uvm_faults() - before == 2);

  printf("all tests passed\n");
  return 0;
}