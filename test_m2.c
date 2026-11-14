#include "uvm.h"
#include <assert.h>
#include <stdio.h>

static size_t ps;

static char *page(void *base, size_t i) { return (char *)base + ps * i; }

/* volatile로 접근해서 -O2에서도 읽기/쓰기가 지워지지 않게 한다 */
static char rd(char *p) { return *(volatile char *)p; }
static void wr(char *p, char v) { *(volatile char *)p = v; }

int main(void) {
  int rc = uvm_init();
  assert(rc == 0);
  ps = uvm_pagesize();

  void *addr = uvm_alloc(1000);
  assert(addr != NULL);

  size_t f, d;

  /* test1: 처음 닿는 페이지를 읽기만 → READ, 폴트 1 */
  f = uvm_faults();
  d = uvm_dirty_count();
  char v = rd(page(addr, 1));
  assert(v == 0);
  assert(uvm_faults() - f == 1);
  assert(uvm_dirty_count() - d == 0);
  assert(uvm_page_state(page(addr, 1)) == PG_READ);

  /* test2: 처음 닿는 페이지에 쓰기 → DIRTY, 폴트 2 */
  f = uvm_faults();
  d = uvm_dirty_count();
  wr(page(addr, 2), 'A');
  assert(uvm_faults() - f == 2);
  assert(uvm_dirty_count() - d == 1);
  assert(uvm_page_state(page(addr, 2)) == PG_DIRTY);

  /* test3: READ 페이지에 쓰기 → DIRTY, 폴트 1 */
  f = uvm_faults();
  d = uvm_dirty_count();
  wr(page(addr, 1), 'B');
  assert(uvm_faults() - f == 1);
  assert(uvm_dirty_count() - d == 1);
  assert(uvm_page_state(page(addr, 1)) == PG_DIRTY);

  /* test4: DIRTY 페이지에 다시 쓰기 → 폴트 0 */
  f = uvm_faults();
  d = uvm_dirty_count();
  wr(page(addr, 1), 'C');
  assert(uvm_faults() - f == 0);
  assert(uvm_dirty_count() - d == 0);
  assert(rd(page(addr, 1)) == 'C');

  /* test5: 상태 조회의 경계 */
  assert(uvm_page_state(page(addr, 500)) == PG_UNTOUCHED);
  assert(uvm_page_state(NULL) == -1);

  /* test6: 새 영역 100페이지, 홀수는 쓰기, 짝수는 읽기만 */
  void *b = uvm_alloc(100);
  assert(b != NULL);
  f = uvm_faults();
  d = uvm_dirty_count();
  for (size_t i = 0; i < 100; i++) {
    if (i % 2) {
      wr(page(b, i), 'A');
      assert(uvm_page_state(page(b, i)) == PG_DIRTY);
    } else {
      char w = rd(page(b, i));
      assert(w == 0);
      assert(uvm_page_state(page(b, i)) == PG_READ);
    }
  }
  assert(uvm_faults() - f == 150); /* 쓰기 50 × 2 + 읽기 50 × 1 */
  assert(uvm_dirty_count() - d == 50);

  printf("all tests passed\n");
  return 0;
}