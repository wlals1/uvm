#ifndef UVM_H
#define UVM_H

#include <stddef.h>

/* uvm_page_state()의 반환값 */
enum { PG_UNTOUCHED = 0, PG_READ, PG_DIRTY };

int uvm_init(void);
void *uvm_alloc(size_t npages);
size_t uvm_pagesize(void);
size_t uvm_faults(void);
int uvm_page_state(void *addr); /* 영역 밖이면 -1 */
size_t uvm_dirty_count(void);

#endif /* UVM_H */