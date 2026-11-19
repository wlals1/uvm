#ifndef UVM_H
#define UVM_H

#include <stddef.h>

/* uvm_page_state()의 반환값 */
enum { PG_UNTOUCHED = 0, PG_READ, PG_DIRTY, PG_EVICTED };
enum { UVM_FIFO, UVM_CLOCK };

int uvm_init(void);
void *uvm_alloc(size_t npages);
size_t uvm_pagesize(void);
size_t uvm_faults(void);
int uvm_page_state(void *addr); /* 영역 밖이면 -1 */
size_t uvm_dirty_count(void);
int uvm_set_max_resident(
    size_t n); // 1 ≤ n ≤ UVM_MAX_RESIDENT. 영역 할당 전에 호출. 실패 시 -1
size_t uvm_resident(void);  // 지금 상주 중인 페이지 수
size_t uvm_evictions(void); // 내보낸 횟수
size_t uvm_writebacks(void); // 그중 사본에 저장한 횟수 (DIRTY였던 것)
int uvm_set_policy(int policy);
size_t uvm_ref_faults(void);

#endif /* UVM_H */