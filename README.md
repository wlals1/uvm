# uvm: 유저 공간 demand paging

## 개요
- `PROT_NONE`으로 예약만 해둔 영역에 처음 접근하면 SIGSEGV가 오고, 시그널 핸들러가
  그 페이지의 권한을 열어준 뒤 돌아간다. 재실행된 접근은 커널이 처리해서 성공한다.
  커널 page fault handler의 "판단(정책)" 부분을 유저 공간 시그널 핸들러로 흉내낸 라이브러리다.
- CSAPP 9장의 demand paging과 page fault 처리 흐름을 직접 구현하며 확인하려고 만들었다.
- 단계별로 확장한다.

| 단계 | 내용 | 대응하는 커널 개념 |
|---|---|---|
| M1 | 지연 할당: 처음 닿은 페이지만 연다 | demand paging, demand-zero |
| M2 | 읽기/쓰기 구분, dirty 추적 | PTE의 R/W 비트, D 비트 |
| M3 (예정) | 상주 페이지 수 제한과 교체 | 페이지 교체, 참조 비트, swap |
| M4 (선택) | COW 스냅샷 | copy-on-write |

## 빌드와 실행
- `make` : 빌드
- `make check` : 모든 테스트 실행
- `./test_m1` : M1 테스트 (성공 시 `all tests passed`)
- `./test_m1 segv` : 영역 밖 접근 테스트 (`Segmentation fault`, `echo $?` → 139)
- `./test_m2` : M2 테스트
- `make clean` : 빌드 결과물 삭제 (CFLAGS를 바꿨을 때는 `make clean && make`)
# uvm: 유저 공간 demand paging

## 개요
- `PROT_NONE`으로 예약만 해둔 영역에 처음 접근하면 SIGSEGV가 오고, 시그널 핸들러가
  그 페이지의 권한을 열어준 뒤 돌아간다. 재실행된 접근은 커널이 처리해서 성공한다.
  커널 page fault handler의 "판단(정책)" 부분을 유저 공간 시그널 핸들러로 흉내낸 라이브러리다.
- CSAPP 9장의 demand paging, page fault 처리, 페이지 교체를 직접 구현하며 확인하려고 만들었다.
- 단계별로 확장한다.

| 단계 | 내용 | 대응하는 커널 개념 |
|---|---|---|
| M1 | 지연 할당: 처음 닿은 페이지만 연다 | demand paging, demand-zero |
| M2 | 읽기/쓰기 구분, dirty 추적 | PTE의 R/W 비트, D 비트 |
| M3a | 상주 페이지 수 제한, FIFO 교체, 사본 저장/복원 | swap-out / swap-in, 프레임 반납 |
| M3b (예정) | clock 교체, 참조 비트 흉내 | A 비트, second-chance |
| M4 (선택) | COW 스냅샷 | copy-on-write |

## 빌드와 실행
- `make` : 빌드
- `make check` : 모든 테스트 실행 (하나라도 실패하면 종료 코드 1)
- `./test_m1` : M1 테스트 (성공 시 `all tests passed`)
- `./test_m1 segv` : 영역 밖 접근 테스트 (`Segmentation fault`, `echo $?` → 139)
- `./test_m2` : M2 테스트
- `./test_m3` : M3a 테스트 (테스트마다 `PASS` / `FAIL` 출력)
- `make clean` : 빌드 결과물 삭제 (CFLAGS를 바꿨을 때는 `make clean && make`)

## 공개 API
```c
int    uvm_init(void);                  // SIGSEGV 핸들러 설치. 실패 시 -1
void  *uvm_alloc(size_t npages);        // PROT_NONE으로 npages 예약. 실패 시 NULL
size_t uvm_pagesize(void);              // uvm이 쓰는 페이지 크기
size_t uvm_faults(void);                // 핸들러가 처리한 폴트 횟수
int    uvm_page_state(void *addr);      // (M2) 페이지 상태. 영역 밖이면 -1
size_t uvm_dirty_count(void);           // (M2) 모든 영역의 DIRTY 페이지 수
int    uvm_set_max_resident(size_t n);  // (M3a) 상주 한도. 1 ≤ n ≤ UVM_MAX_RESIDENT,
                                        //        상주 페이지가 0개일 때만 가능. 실패 시 -1
size_t uvm_resident(void);              // (M3a) 지금 상주 중인 페이지 수
size_t uvm_evictions(void);             // (M3a) 내보낸 횟수
size_t uvm_writebacks(void);            // (M3a) 그중 사본에 저장한 횟수 (DIRTY였던 것)
```

## 커널과의 대응
| 커널 | uvm |
|---|---|
| vm_area 권한이 접근 금지 | `PROT_NONE`으로 mmap한 영역 |
| page fault → 커널 page fault handler | 커널이 SIGSEGV → `segv_handler` |
| CR2 / stval | `info->si_addr` |
| vm_area 탐색 | `find_region` |
| 권한 허용 | `mprotect` |
| PTE의 R/W 비트 (M2) | `PROT_READ` / `PROT_READ \| PROT_WRITE` |
| PTE의 D 비트 (M2) | 페이지 상태 `PG_DIRTY` |
| swap 영역 (M3a) | 영역마다 calloc한 사본 버퍼, 페이지 i ↔ 칸 i |
| swap-out: dirty면 swap에 쓰기 (M3a) | DIRTY면 사본 칸에 memcpy |
| PTE 무효화 (M3a) | `mprotect(PROT_NONE)` |
| 프레임 반납 (M3a) | `madvise(MADV_DONTNEED)` |
| swap-in (M3a) | 사본 칸 → 페이지 memcpy |
| 같은 명령어 재실행 | 핸들러 return → `sigreturn` → 재실행 |
| 프레임 할당, PTE 기록 | uvm은 안 함. 진짜 커널이 함 |

## 공통 설계 결정
- **영역 표를 왜 static으로 숨겼나?**
  사용자가 표를 고치면 핸들러가 엉뚱한 주소를 내 영역으로 믿고 열어줄 수 있다.
  또 내부 구조를 바꿔도 사용자 코드는 영향을 받지 않는다. 대신 공개 함수의 반환값
  (페이지 상태 enum)은 사용자가 알아야 하므로 헤더에 둔다.
- **왜 [start, end) 반개구간인가?**
  `end - start`가 바로 크기가 되고, 두 영역이 맞닿았는지를 `a.end == b.start`
  한 번으로 판단할 수 있어서 합치기/분할이 쉽다. off-by-one 실수도 줄어든다.
  `find_region`이 `start <= addr < end`를 확인했으면 페이지 번호
  `(addr - start) / pagesize`는 항상 `0 ~ npages-1`이라 배열 범위를 넘지 않는다.
  Linux vm_area도 같은 방식이다.
- **왜 uintptr_t로 저장하나?**
  주소에 정수 연산(페이지 마스킹, 범위 비교, 바이트 단위 덧셈)이 필요하다.
  포인터에는 `&` 연산을 할 수 없고, 정수를 더하면 가리키는 타입의 크기
  (`sizeof(*p) * n`)만큼 더해진다. `void *`는 표준 C에서 덧셈도 안 된다.
  `uint64_t`가 아니라 `uintptr_t`인 이유는 포인터 폭이 플랫폼마다 달라서다.
- **왜 "채우고 → fence → 개수 증가" 순서인가?**
  개수가 먼저 늘면, 그 사이에 끼어든 핸들러가 아직 채워지지 않은 칸을 영역으로 믿는다.
  `regions[nregions++] = ...`는 한 문장 안에서 두 저장의 순서를 보장하지 않고,
  두 줄로 나눠도 컴파일러는 핸들러가 이 변수들을 읽는다는 걸 모르니 순서를 바꿀 수 있다.
  `atomic_signal_fence`로 컴파일러의 재배치를 막는다.
- **핸들러 안에서 왜 write + _exit만 쓰나?**
  핸들러는 main이 `printf`를 실행하는 도중에도 끼어들 수 있다. 이때 핸들러가 또
  `printf`를 부르면 stdio 버퍼와 락이 반쯤 갱신된 상태라 깨진다. `write`는 유저 공간에
  공유 상태가 없는 시스템콜이라 이런 문제가 없다(async-signal-safe). `_exit`은 `exit`과
  달리 stdio flush나 atexit 함수 없이 바로 커널에 종료를 요청한다.
  반복을 줄이려고 `DIE("...")` 매크로를 쓴다. 매크로라서 `sizeof`가 문자열 리터럴(배열)에
  적용되어 정확한 길이가 나온다. 함수 인자로 받으면 포인터라 `sizeof`가 8이 된다.
- **오버플로 검사를 왜 곱하기 전에 하나?**
  곱한 결과는 mod 2^64로 작은 수가 될 수 있어서, 그 값만 보고는 넘쳤는지 알 수 없다.
  곱하기 전에 `npages > SIZE_MAX / pagesize`로 검사하면 잘못된 값을 아예 만들지 않는다.
  이 방식은 넘치는 순간 UB인 signed 타입에도 그대로 쓸 수 있다.
- **영역 밖 폴트는 왜 SIG_DFL로 넘기나?**
  그냥 return하면 같은 명령어가 재실행되어 무한 루프에 빠진다. `_exit`으로 끝내면
  부모는 정상 종료로 보고 코어 덤프도 없다. SIG_DFL로 되돌리고 return하면 재실행 때
  커널이 평소처럼 SIGSEGV로 죽여서(139, 코어 덤프) 진짜 버그를 숨기지 않는다.
- **여러 자원을 얻다가 실패하면 역순으로 되돌린다.**
  uvm_alloc은 상태 배열 → 사본 버퍼 → mmap 순으로 얻고, 실패하면 goto 레이블
  (`err_swap`, `err_state`)로 이미 얻은 것만 역순으로 푼다. 영역 표에는 전부 성공한 뒤에만 넣는다.

## 한계와 주의점
- `mprotect`, `madvise`는 POSIX async-signal-safe 목록에 없다 (Linux에서는 시스템콜을
  바로 부르는 얇은 래퍼라 실제로 동작). `memcpy`는 POSIX 2016 개정판부터 목록에 있다.
- 폴트 카운터는 하드웨어 폴트 수가 아니다. uvm 영역 안에서 SIGSEGV가 와서 핸들러가
  처리한 횟수만 센다. 같은 페이지의 재실행 폴트(커널이 프레임 할당)와
  uvm 밖의 폴트(스택, 힙 등)는 커널이 시그널 없이 처리하므로 세지 않는다.
  실제 minor fault 수는 getrusage의 ru_minflt로 확인할 수 있다.
- (M3a) 사본 버퍼도 RAM에 있으므로 실제로 메모리를 아끼지는 못한다. 유저 공간에서는
  디스크의 swap 영역을 직접 쓸 수 없어서 메모리 버퍼로 흉내냈다. 목적은 메모리 절약이
  아니라 내보내기/되돌리기 흐름과 교체 정책의 구현이다.
- (M3a) 상주 한도는 상주 페이지가 0개일 때만 바꿀 수 있다. 원형 큐의 칸 위치를
  `% max_resident`로 계산하므로, 도중에 한도를 바꾸면 칸의 순서 해석이 어긋난다.
- 스택 오버플로로 인한 SIGSEGV는 처리 못 한다 (SA_ONSTACK)
- 멀티스레드 미고려
- 영역 해제(`uvm_free`) 미구현

---

## M1: 지연 할당 (demand paging)

### 동작 흐름
1. `uvm_alloc`이 `PROT_NONE`으로 영역을 예약한다. 이 시점에는 프레임도 PTE도 없다.
2. 영역 안의 주소에 처음 접근하면 page fault가 난다. **커널의 page fault handler**가
   vm_area 권한을 확인하는데, 접근 금지라서 직접 처리하지 못하고 SIGSEGV를 보낸다.
3. 커널은 유저 모드로 돌아가기 직전에 PC를 `segv_handler`로 바꾼다.
   핸들러는 user mode에서 실행된다.
4. 핸들러는 `si_addr`이 uvm 영역 안인지 확인한다.
   - 안이면: 페이지 시작 주소로 내리고 `mprotect`로 읽기/쓰기를 허용한 뒤 return한다.
     (M2에서는 이 부분이 상태 기계로 바뀐다)
   - 밖이면: SIGSEGV를 `SIG_DFL`로 되돌리고 return한다.
5. return하면 `sigreturn`을 거쳐 폴트가 났던 **같은 명령어**가 다시 실행된다.
   - 영역 안이었다면: 다시 page fault가 나지만, 이번엔 허용된 접근이라 커널이
     **시그널 없이** 0으로 채운 프레임을 할당하고 PTE를 채운다. 명령어가 성공한다.
   - 영역 밖이었다면: 다시 폴트가 나고, 기본 동작이라 커널이 프로세스를 종료한다.

### 테스트 (`test_m1.c`)
| test | 내용 | 검증 |
|---|---|---|
| test1 | 1000페이지를 예약하고 0, 10, 20, ..., 990번 페이지(총 100개)에 처음으로 씀 | `uvm_faults()` == 100. 처음 닿은 페이지마다 SIGSEGV가 한 번씩 온다 |
| test2 | test1이 건드리지 않은 3번 페이지에 100번 씀 | 변화량 1. 첫 쓰기에서만 폴트가 나고, 이후 99번은 이미 열린 페이지라 폴트가 없다 |
| test3 | 처음 닿는 13번 페이지를 **읽음** | 읽은 값 0, 변화량 1. 읽기도 첫 접근이면 SIGSEGV가 오고, 재실행 때 커널이 프레임을 0으로 채워 할당한다(demand-zero) |
| test4 | `./test_m1 segv`: 영역 밖 0번 주소에 씀 | `Segmentation fault`, `echo $?` → 139. 핸들러가 SIG_DFL로 넘기고, 재실행 때 커널이 다시 SIGSEGV를 보내 기본 동작으로 종료한다. uvm이 진짜 버그를 숨기거나 무한 루프에 빠지지 않는지 확인 |
| test5 | test1이 이미 연 10번 페이지에 다시 씀 | 변화량 0. 이미 허용된 페이지에서는 핸들러가 불리지 않는다 |
| test6 | 영역의 마지막 바이트(`addr + 1000 * ps - 1`)에 씀 | 변화량 1. `[start, end)` 경계 검사가 마지막 바이트를 영역 안으로 판정하는지 확인 |

> M2 이후 쓰기 테스트의 폴트 수는 달라진다(처음 쓰는 페이지는 SIGSEGV 2번).
> 현재 test_m1의 기대값은 M2 기준이다: test1 200, test2 2, test6 2.

---

## M2: 읽기/쓰기 구분과 dirty 추적

### 상태 기계
| 현재 상태 | 폴트의 의미 | 권한 | 다음 상태 |
|---|---|---|---|
| UNTOUCHED | 첫 접근 (읽기인지 쓰기인지 모름) | `PROT_READ` | READ |
| READ | 쓰기 접근 | `PROT_READ \| PROT_WRITE` | DIRTY |
| DIRTY | uvm이 처리할 수 없는 폴트 (실행 접근, 상태 불일치) | 메시지 후 `SIG_DFL` | 종료 |
| 그 외 값 | 상태 오염 | `SIG_DFL` | 종료 |

- `siginfo_t`에는 읽기/쓰기를 구분하는 이식성 있는 필드가 없다. 그래서 처음엔 읽기만
  열어두고, READ 페이지에서 다시 폴트가 나면 쓰기였다고 판단한다.
- 처음부터 쓰는 페이지는 SIGSEGV가 2번 온다(UNTOUCHED → READ → DIRTY). 이식성을 얻는
  대신 폴트가 하나 더 든다. x86에서는 `ucontext`의 에러 코드로 한 번에 구분할 수 있지만
  아키텍처 전용이다.
- `mprotect`는 권한을 더하는 게 아니라 **교체**한다. 쓰기를 열 때 `PROT_WRITE`만 주면
  읽기 권한이 사라지므로 `PROT_READ | PROT_WRITE`를 준다.

### dirty의 의미
- "로드된 뒤로 한 번이라도 쓰였나"를 나타낸다. 내보낼 때(M3) 저장이 필요한지 판단하는 데 쓴다.
  write-back 캐시의 dirty 비트와 같은 개념이다(캐시 블록 ↔ 메모리 관계가 페이지 ↔ swap 관계로 바뀐 것).
- 0 → 1로 바뀌는 순간(첫 쓰기)만 잡으면 되므로, 쓰기 권한을 준 뒤 폴트가 안 나는 건 의도된 설계다.
- 하드웨어 D 비트가 없는 CPU에서 커널이 dirty를 추적하는 방법과 같다.

### M2 설계 결정
- **상태는 페이지 단위로 둔다.** 영역 단위로 두면 한 페이지만 써도 영역 전체가 DIRTY가 되어
  내보낼 때 전부 저장해야 한다. 하드웨어도 PTE마다 D 비트가 있다.
- **상태는 직접 정한 enum으로, 권한과 분리한다.** M3의 EVICTED는 UNTOUCHED와 권한이
  같지만(PROT_NONE) 처리가 다르다. 권한 값만 저장하면 둘을 구분할 수 없다.
- **상태 배열은 uvm_alloc에서 calloc으로 확보한다.** 핸들러에서는 malloc을 쓸 수 없다.
  UNTOUCHED를 0으로 정해서 calloc만으로 초기화된다. calloc은 개수 × 크기 오버플로도 검사한다.
- **상태는 mprotect가 성공한 후에 바꾼다.** 상태는 항상 실제 권한과 일치해야 한다.
  먼저 바꾸면 실패 처리를 바꿨을 때 다음 폴트를 잘못 해석한다.
- **switch의 default는 fallback으로 보낸다.** 모르는 상태 값이면 권한을 열지 않고 안전하게 죽는다.
- **dirty 수는 매번 스캔한다.** 카운터를 따로 두면 O(1)이지만, M3에서 clean으로 바뀔 때
  감소를 빠뜨리면 상태와 어긋난다. 스캔은 O(전체 페이지 수)지만 항상 정확하다.
- **실패 처리는 함수 끝의 fallback 레이블로 모은다.** 정상 경로는 그 앞에서 return한다.

### 테스트 (`test_m2.c`)
모든 메모리 접근은 `volatile`로 해서 `-O2`에서도 읽기/쓰기가 지워지지 않게 한다.

| test | 내용 | 검증 |
|---|---|---|
| test1 | 처음 닿는 1번 페이지를 읽기만 | 값 0, 폴트 +1, dirty +0, 상태 READ |
| test2 | 처음 닿는 2번 페이지에 쓰기 | 폴트 +2, dirty +1, 상태 DIRTY |
| test3 | READ 상태인 1번 페이지에 쓰기 | 폴트 +1, dirty +1, 상태 DIRTY |
| test4 | DIRTY 상태인 1번 페이지에 다시 쓰기 | 폴트 +0, dirty +0, 쓴 값이 남아 있음 |
| test5 | 상태 조회 경계 | 안 닿은 500번 페이지는 UNTOUCHED, 영역 밖(NULL)은 -1 |
| test6 | 새 영역 100페이지, 홀수는 쓰기 / 짝수는 읽기만 | 폴트 +150 (쓰기 50 × 2 + 읽기 50 × 1), dirty +50, 페이지마다 상태 확인 |

---

## M3a: 상주 페이지 제한과 FIFO 교체

### 상태 기계
| 현재 상태 | 할 일 | 다음 상태 |
|---|---|---|
| UNTOUCHED | 자리 확보 → `PROT_READ` → 큐에 넣기 | READ |
| **EVICTED** | 자리 확보 → `PROT_READ \| PROT_WRITE` → 사본 칸을 페이지로 복사 → `PROT_READ` → 큐에 넣기 | READ |
| READ | `PROT_READ \| PROT_WRITE` (이미 상주 중이라 큐는 그대로) | DIRTY |
| DIRTY / 그 외 | fallback | 종료 |

### 내보내기 (`evict`)
1. DIRTY면 페이지 내용을 사본 칸에 저장 (writeback). READ면 건너뜀
2. `mprotect(PROT_NONE)`: 다음 접근에서 폴트가 나게 함 (PTE 무효화에 해당)
3. `madvise(MADV_DONTNEED)`: 물리 프레임을 커널에 반납
4. 상태를 EVICTED로

### 동작 예 (한도 4, 10페이지에 차례로 쓰기)
```
0~3번: 열림 (큐: 0 1 2 3)
4번: 큐가 꽉 참 → 0번 내보냄(DIRTY → 저장) → 큐: 1 2 3 4
...
9번: 5번 내보냄 → 큐: 6 7 8 9
결과: evictions 6, writebacks 6, 0~5번 EVICTED(RAM에 없음), 6~9번 DIRTY(RAM에 있음)
```

### M3a 설계 결정
- **권한만 빼앗는 것으로는 내보낸 게 아니다.** `mprotect(PROT_NONE)`은 접근만 막고 프레임은
  RAM에 그대로 둔다. `madvise(MADV_DONTNEED)`까지 해야 프레임이 반납된다.
  빠뜨려도 복원 때 사본으로 덮어쓰니 값은 맞게 나와서, `mincore`로 직접 확인해야 잡힌다.
  - `MADV_NORMAL`: 아무것도 버리지 않음. `MADV_REMOVE`: 공유 매핑 전용, private anonymous에서 EINVAL.
  - `MADV_FREE`: 커널이 언제 버릴지 몰라 다시 읽으면 0일 수도 옛 데이터일 수도 있다.
- **사본 저장은 dirty일 때만.** READ(clean)는 사본과 내용이 같으므로 프레임만 버린다.
  writebacks와 evictions의 차이가 dirty 추적으로 아낀 저장 횟수다.
- **사본 버퍼는 calloc으로.** 한 번도 안 쓰고 읽기만 한 페이지는 저장 없이 내보내진다.
  이 페이지를 되돌리면 사본 칸을 복사하므로, 칸이 0이어야 원래 내용(demand-zero)과 같다.
  malloc이면 쓰레기 값이 복원된다.
- **사본 칸은 페이지 번호로 직접 정한다.** 페이지 i ↔ 칸 i(`swap_slot(r, i)`). 탐색이 필요 없고,
  칸에 유효한 사본이 있는지는 상태(EVICTED)가 알려준다. 진짜 커널은 swap의 빈 칸을 할당하고
  그 번호를 무효 PTE에 적어둔다. 저장과 복원이 같은 함수로 위치를 계산하게 해서 어긋남을 막는다.
- **복원할 때 잠깐 쓰기 권한을 연다.** memcpy가 페이지에 써야 하기 때문이다. 핸들러 실행 중에는
  SIGSEGV가 막혀 있어서, 닫힌 페이지를 건드리면 커널이 프로세스를 바로 죽인다.
- **복원 후에는 READ로 낮춘다.** 복원 직후는 사본과 같으니 clean이다. 쓰기 권한을 남기면
  다음 쓰기를 핸들러가 몰라서, 다음 내보내기 때 저장을 건너뛰어 데이터가 사라진다.
- **자리 확보는 열기 전에, 큐에 넣기는 성공한 후에.** 잠깐이라도 한도를 넘지 않게 하고,
  장부(큐)는 실제로 일어난 일만 기록한다.
- **원형 큐는 head / count 방식.** head / tail 방식은 꽉 참과 빔을 구분하려고 한 칸을 비워둬야 해서
  실제 용량이 한도 - 1이 된다. 넣을 자리는 `(head + nresident) % max_resident`.
  모듈로는 배열 크기가 아니라 실제 한도로 한다.
- **`make_room`은 "자리를 확보한다".** 자리가 있으면 그냥 return한다. 호출하는 쪽이 꽉 찼는지
  검사할 필요가 없다. evict는 페이지만 다루고 큐 정리는 make_room이 한다(M3b에서 고르는 방법만 바꾸기 쉽게).
- **기본 한도는 크게(4096), 내보내기는 opt-in.** 처음에 기본값을 64로 두었더니 한도를 설정하지 않은
  test_m1의 동작이 바뀌었다. 새 기능은 원하는 쪽이 `uvm_set_max_resident`로 켜게 하고,
  기존 사용자의 동작은 그대로 유지한다.
- **핸들러의 각 case가 자기 일을 처음부터 끝까지 한다.** EVICTED는 mprotect가 두 번이라
  "권한만 정하고 mprotect는 한 번" 구조와 맞지 않았다. `protect()` 도우미로 중복을 줄였다.

### 테스트 (`test_m3.c`)
- 상주 큐와 한도가 전역 상태라서 **테스트마다 fork해서 자식 프로세스에서 실행**한다.
  assert가 실패해도 그 테스트만 FAIL(`killed by signal 6`)로 끝나고 다음 테스트가 계속 돈다.
- fork 전에 `fflush(stdout)`: 출력을 파일이나 파이프로 보낼 때 자식이 버퍼를 물려받아 중복 출력하는 것을 막는다.
- `run`은 PASS면 0, FAIL이면 1을 반환하고, main은 합계로 종료 코드를 정한다.
- 모든 접근은 volatile(`rd`, `wr`)로, 프레임 반납은 `mincore`(`in_ram`)로 확인한다.

| test | 설정 | 동작 | 검증 |
|---|---|---|---|
| T1 | 한도 4, 10페이지 | 페이지 i에 i를 0~9번 순서로 씀 | faults 20, evictions 6, writebacks 6, resident 4. 0~5번 EVICTED + RAM에 없음, 6~9번 DIRTY + RAM에 있음 |
| T2 | T1과 같은 상태에서 | 0~9번을 순서대로 읽음 | 값 i. faults +10, evictions +10, **writebacks +4** (6~9번만 DIRTY였고, 복원한 0~3번은 clean). 최종 0~5 EVICTED, 6~9 READ |
| T3 | 한도 4, 10페이지 | 읽기만 | 값 0, faults 10, evictions 6, **writebacks 0** |
| T4 | 한도 3, 4페이지 | 0~3번 읽기 | 0번만 EVICTED. FIFO 순서 확인(LIFO 버그면 2번이 나감) |
| T5 | 한도 2, 8페이지 | 페이지 전체를 i로 채움 → 확인 후 i+10으로 덮어씀 → 다시 확인 | memcmp로 페이지 전체 검사. writebacks 6 → 14 → 16, evictions 6 → 14 → 22. 저장 → 복원 → 수정 → 재저장 왕복과 오래된 사본 버그 확인 |
| 공통 | 모든 테스트 | 접근할 때마다 | `uvm_resident() <= 한도` |

T5의 3바퀴째는 한도 2로 8페이지를 순서대로 반복 접근하므로, 필요한 페이지가 항상 방금 밀려난
페이지라 모든 접근이 폴트가 난다. 작업 집합이 메모리보다 큰 경우의 thrashing이다.

---

## 개선 아이디어
- oldact 백업으로 원래 SIGSEGV 핸들러 복원 (라이브러리는 자기가 바꾼 것만 되돌린다)
- `uvm_free`: munmap과 상태 배열, 사본 버퍼 해제, 영역 표에서 제거
- M3b: clock 교체. 상주 페이지의 권한을 빼앗아 참조 비트를 흉내내고(프레임은 유지),
  같은 접근 패턴에서 FIFO와 폴트 수 비교
- 사본 저장소를 파일로(`pwrite` / `pread`). 부분 쓰기/읽기를 처리해야 한다
- M4: COW 스냅샷

---

## 배운 점 / 실수 기록

### M1

#### 포인터와 주소
- 값이 필요한 곳에 `&변수`를 넘김: `mprotect(&addr, ..)`, `.start = &addr`.
  `&addr`는 mmap이 준 주소가 아니라 지역 변수 addr가 스택에 놓인 위치다.
  → 받는 쪽이 원하는 게 변수의 "값"인지 "위치"인지 먼저 묻기.
- 주소를 정수로 바꾸고 다시 포인터로 돌리는 단계를 빠뜨림 (`mprotect`에 uintptr_t 전달).
- `void *`에 정수 덧셈 (`addr + i`). 표준 C에서 불가. `char *`로 받아서 계산.
- `addr & ~(pagesize - 1);`처럼 계산 결과를 저장하지 않음 (statement with no effect).
- 마스크 폭: `~`는 피연산자 타입 폭에서 계산된다. 32비트에서 `~` 후 넓히면 상위 32비트가
  날아간다. → 먼저 uintptr_t로 넓힌 다음 `~`.
- `uint64_t` 대신 `uintptr_t`: 32비트 플랫폼에서 포인터 변환 경고(-Werror면 빌드 실패).

#### 단위와 조건
- 단위 혼동: `npages / pagesize` (페이지 ÷ 바이트/페이지). 바이트는 `npages * pagesize`.
- 지수와 값 혼동: `1 << PAGESIZE` (PAGESIZE는 이미 4096). 시프트 칸 수 ≥ 타입 폭이면 UB.
- 성공 조건을 반대로 씀: `assert(!addr)`, `strcmp(...)`를 참/거짓으로 사용(같으면 0).
- 테스트 루프 범위: 끝과 증가량을 바꿔 써서 1번만 돎, 루프 변수를 주소 계산에 안 써서
  매번 같은 페이지에 씀. → 시작/끝/증가에 단위를 붙여서 확인.

#### 시그널 API
- `sa_handler`(인자 1개)와 `sa_sigaction`(인자 3개)을 혼동. 3인자 핸들러는
  `sa_sigaction` + `SA_SIGINFO` 짝으로 써야 한다. 플래그가 없으면 info가 쓰레기 값이다.
- `sigaction(sig, SIG_DFL, NULL)`: 두 번째 인자는 설정서(struct sigaction *)다.
  SIG_DFL은 그 구조체의 sa_handler 필드에 넣는 값이다.
- `info->si_handler = SIG_DFL`: siginfo_t는 커널이 준 "보고서"이고 고쳐도 아무 효과가 없다.
  처리 방식은 sigaction()으로만 바뀐다.
- `sigreturn;`을 직접 부르려 함. 핸들러는 그냥 return하면 trampoline이 sigreturn을 호출한다.
- 핸들러 안에서 `perror`, `exit` 사용. async-signal-safe가 아니다 → `write` + `_exit`.
- 라이브러리 함수(uvm_init)에서 `_exit` 사용. main 쪽 함수는 실패를 -1로 알리고
  호출자가 결정하게 해야 한다.

#### mmap과 영역 표
- mmap 실패를 `!addr`로 검사. 실패 값은 NULL이 아니라 `MAP_FAILED`((void *)-1).
- `regions[nregions++] = ...`: 채우기와 개수 증가의 순서가 보장되지 않는다.
  → 채우기, `atomic_signal_fence`, 증가 순으로 분리.
- 영역 밖 테스트에 영역 바로 뒤 주소(`addr + size`)를 씀. 그 자리에 다른 매핑이 있을 수
  있어 결과가 흔들린다. → 확실히 비어 있는 0번 주소 사용.

#### 개념
- 핸들러가 두 개다: 커널 page fault handler(예외, kernel mode)와 segv_handler
  (시그널, user mode). 커널이 해결을 포기한 폴트만 시그널로 넘어온다.
- segv_handler는 프레임을 할당하지 않는다. vm_area 권한만 바꾸고, 실제 할당은
  재실행 때 커널이 한다.
- 시그널은 프로세스가 "보는" 게 아니라, 커널이 유저로 돌아가기 직전에 PC를 바꿔서 전달한다.
- demand-zero는 예약 시점이 아니라 프레임을 할당하는 시점에 0으로 채운다.
- fault는 같은 명령어로 돌아가므로, 원인을 고치지 않고 return하면 무한 루프다.

### M2

#### 포인터와 구조체
- `.state = &state`: 지역 변수의 주소를 전역에 저장(댕글링 포인터). 지역 변수 안의 값
  (힙 주소)인 `state`를 저장해야 한다. (`&변수` 실수 세 번째)
- 복합 리터럴로 구조체를 다시 채우면서 state 필드를 빠뜨려 NULL로 덮어씀.
  지정하지 않은 필드는 0으로 초기화된다.
- 상태 배열을 권한 값(PROT_*)으로 저장하려 함. M3에서 UNTOUCHED와 EVICTED를 구분할 수 없다.
- `char *`와 `unsigned char *` 혼용 → `-Wall`의 signedness 경고.
- 크기를 담는 변수를 `int`로 선언(`int pnum`). `size_t`여야 한다. `-Wconversion`이어야 잡힌다.

#### 제어 흐름
- goto 구조에서 정상 경로 끝의 `return` 누락 → 첫 폴트 처리 후 SIGSEGV가 SIG_DFL로 바뀌어
  다음 폴트에서 프로세스가 죽는다.
- 레이블(`fallback:`, `case`) 바로 뒤에 선언. C17까지는 에러이고 gcc는 C23 기능으로 허용한다.
  case 안의 선언은 중괄호로 감싼다(초기화를 건너뛰는 점프도 막는다).
- READ를 `default:`로 처리 → 오염된 상태 값에 쓰기 권한을 열어줄 수 있었다.
  알려진 상태는 case로 명시하고 default는 fallback으로.
- 메시지를 포인터 변수로 고르면 `sizeof msg`가 포인터 크기(8)가 된다.
- 진단 메시지를 stdout(fd 1)으로, 길이를 손으로 세서(`'\0'`까지 출력) 씀.

#### 테스트
- `(void)c`는 메모리 접근이 아니다. 읽기를 테스트하려면 실제로 읽어야 하고,
  값을 안 쓰면 `-O2`에서 지워질 수 있으므로 volatile로 읽는다.
- 여러 조건을 `&&`로 묶은 assert → 실패해도 어느 조건인지 모른다. 하나씩 나눈다.
- 루프 밖에서 마지막 값만 검사 → 루프 안에서 매번 검사한다.
- 동작을 의도적으로 바꾸면 이전 단계의 회귀 테스트가 실패한다(M2에서 test_m1의 폴트 수).
  의도하지 않은 변경이면 코드를, 의도한 변경이면 테스트의 기대값을 고친다.

#### 개념
- dirty는 "쓰였는가"를 나타내는 1비트라서 첫 쓰기만 잡으면 된다. 이후 쓰기에 폴트가
  안 나는 건 의도된 설계다.
- `mprotect`는 권한을 교체한다.
- 여러 자원을 차례로 얻다가 실패하면 이미 얻은 것을 역순으로 되돌린다.
- 상태는 작업이 성공한 후에 기록한다.

### M3a

#### 상태와 권한, 값과 위치
- `mprotect(page, ps, PG_EVICTED)`: 상태 값을 권한 자리에 넘김. PG_EVICTED(3)는
  `PROT_READ | PROT_WRITE`와 같은 값이라, 내보내려던 페이지를 오히려 활짝 연다.
  둘 다 정수라 컴파일러가 경고하지 않는다.
- `memcpy(r->swap[k], ...)`: 위치가 필요한데 값(char 하나)을 넘김. 이번엔 `&`가 **필요한** 쪽이다.
  → `&r->swap[k]` 또는 `r->swap + k`. 결국 `swap_slot()` 하나로 통일.
- 복원할 때 사본 칸을 `&r->swap[addr]`(가상 주소로 인덱싱), `pagesize * idx`(오프셋만, 기준 주소 없음)로 씀.
- 큐의 값을 넘길 때 `evict(resident[head])` (값). `&resident[head]`(칸의 위치)가 아니다.
- uvm_alloc 복합 리터럴에서 `.swap`을 빠뜨림 → NULL. M2의 `.state`와 같은 실수(두 번째).
- 방어 코드의 NULL 검사를 `page`(큐에서 꺼낸 값)에 함. 검사할 대상은 `find_region`의 결과였다.

#### 큐와 상태 기계
- enqueue 인덱스에 모듈로를 빠뜨림(`resident[head + nresident]`). 배열 안이라 segv 없이
  원형 범위 밖에 조용히 써지고, 그 페이지는 영원히 내보내지지 않는다. 최종 숫자보다
  "매 접근마다 resident ≤ 한도" 검사가 이런 버그를 잡는다.
- make_room이 꽉 차지 않았을 때 죽게 함 → 핸들러가 조건 없이 부르면 첫 폴트에서 죽는다.
- head / tail 방식을 쓰면 한 칸이 비어서 한도가 1 줄어든다(off-by-one).
- 핸들러 EVICTED case에 `break` 누락 → default로 떨어져 복원 후 fallback으로 죽음.
- enqueue를 switch 뒤 공통 위치, 다음엔 READ case에 둠 → 이미 상주 중인 페이지를 또 넣는다.
  큐에 넣는 건 새로 상주하는 경우(UNTOUCHED, EVICTED)뿐이다.
- 기능을 계속 덧대다 보니 구조가 꼬였다. 상태 기계 표를 기준으로 핸들러를 처음부터 다시 짰다.

#### 시스템콜
- `MADV_NORMAL`(아무것도 안 함), `MADV_REMOVE`(공유 매핑 전용)를 씀. private anonymous의
  프레임 반납은 `MADV_DONTNEED`.
- 복원 시 mprotect 전에 memcpy하면, 막혀 있는 SIGSEGV 때문에 커널이 프로세스를 즉시 죽인다(무한 루프가 아님).
- 기본 한도를 64로 둬서 test_m1의 동작이 바뀜 → 기본은 크게, 새 기능은 opt-in.

#### 테스트 코드
- `if (fork == 0)`: 반환값 변수가 아니라 함수 이름(주소)과 비교 → 항상 거짓.
- 자식이 테스트 후 `exit`하지 않으면 부모 코드까지 실행한다(8장 실습 1단계와 같은 실수).
- 테스트 결과를 기다릴 때 `WNOHANG` 루프 + pause → 옵션 0으로 한 번 `waitpid(pid, ...)`.
- status를 그대로 비교/출력 → `WIFEXITED` / `WEXITSTATUS` / `WIFSIGNALED` / `WTERMSIG`로 해석.
- run이 결과를 반환하지 않음 → main이 실패를 몰라서 `make check`가 계속 진행.
  main에서 assert로 검사하면 첫 실패에서 나머지 테스트가 안 돈다 → 합산 후 종료 코드로.
- 읽기를 assert 안에서 함(`assert(rd(p) == 0)`) → NDEBUG면 읽기 자체가 사라진다.
- `page(addr, 0) == PG_EVICTED`: 포인터와 상태 값을 비교(uvm_page_state 누락).
  `-Werror` 없이 빌드해서 경고를 놓쳤다.
- 루프 안에서 malloc, 루프 밖에서 free 한 번 → 7번 누수. malloc과 free의 횟수를 맞춘다.
- `static` 함수를 선언만 하고 정의하지 않음 → `-Wall` 경고.
- Makefile 규칙 이름 오타(`test_me`) → make가 내장 규칙으로 test_m3.c만 링크해서 undefined reference.
- errno를 설정하지 않는 함수(uvm_set_max_resident)의 실패에 perror → 엉뚱한 메시지.
- 반환형이 `void *`인 함수에서 return 누락 → `control reaches end of non-void function`.
- 중괄호 위치 실수로 PASS/FAIL 출력이 `waitpid` 실패 블록 안 `exit(1)` 뒤에 들어가 실행되지 않음.

#### 개념
- 프레임(물리 RAM 조각), 페이지(가상 주소 조각), PTE(둘의 연결), 페이지 워크(PTE를 찾는 과정)는 다르다.
- 내보내기 = (dirty면) swap에 쓰기 + PTE 무효화 + 프레임 반납. 권한만 빼앗는 건 내보내기가 아니다.
- READ 페이지의 "원본"은 0 페이지이거나 이미 저장된 사본이다. 그래서 그냥 버려도 된다.
- 한 번도 안 쓴 anonymous 페이지를 읽기만 하면 커널은 공유 zero page를 연결한다(새 프레임 없음).
- 순차 반복 접근에서 작업 집합이 한도보다 크면 FIFO는 모든 접근이 폴트가 난다(thrashing).
- 열려 있는 페이지는 접근해도 폴트가 안 나서 핸들러가 볼 수 없다. 그래서 LRU는 직접 구현할 수 없고,
  하드웨어는 A 비트로, uvm(M3b)은 권한을 일부러 빼앗는 폴트로 참조 여부를 알아낸다.
## 공개 API
```c
int    uvm_init(void);               // SIGSEGV 핸들러 설치. 실패 시 -1
void  *uvm_alloc(size_t npages);     // PROT_NONE으로 npages 예약. 실패 시 NULL
size_t uvm_pagesize(void);           // uvm이 쓰는 페이지 크기
size_t uvm_faults(void);             // 핸들러가 권한을 열어준 횟수
int    uvm_page_state(void *addr);   // (M2) 페이지 상태. 영역 밖이면 -1
size_t uvm_dirty_count(void);        // (M2) 모든 영역의 DIRTY 페이지 수
```

## 커널과의 대응
| 커널 | uvm |
|---|---|
| vm_area 권한이 접근 금지 | `PROT_NONE`으로 mmap한 영역 |
| page fault → 커널 page fault handler | 커널이 SIGSEGV → `segv_handler` |
| CR2 / stval | `info->si_addr` |
| vm_area 탐색 | `find_region` |
| 권한 허용 | `mprotect` |
| PTE의 R/W 비트 (M2) | `PROT_READ` / `PROT_READ \| PROT_WRITE` |
| PTE의 D 비트 (M2) | 페이지 상태 `PG_DIRTY` |
| 같은 명령어 재실행 | 핸들러 return → `sigreturn` → 재실행 |
| 프레임 할당, PTE 기록 | uvm은 안 함. 진짜 커널이 함 |

## 공통 설계 결정
- **영역 표를 왜 static으로 숨겼나?**
  사용자가 표를 고치면 핸들러가 엉뚱한 주소를 내 영역으로 믿고 열어줄 수 있다.
  또 내부 구조를 바꿔도 사용자 코드는 영향을 받지 않는다. 대신 공개 함수의 반환값
  (페이지 상태 enum)은 사용자가 알아야 하므로 헤더에 둔다.
- **왜 [start, end) 반개구간인가?**
  `end - start`가 바로 크기가 되고, 두 영역이 맞닿았는지를 `a.end == b.start`
  한 번으로 판단할 수 있어서 합치기/분할이 쉽다. off-by-one 실수도 줄어든다.
  `find_region`이 `start <= addr < end`를 확인했으면 페이지 번호
  `(addr - start) / pagesize`는 항상 `0 ~ npages-1`이라 배열 범위를 넘지 않는다.
  Linux vm_area도 같은 방식이다.
- **왜 uintptr_t로 저장하나?**
  주소에 정수 연산(페이지 마스킹, 범위 비교, 바이트 단위 덧셈)이 필요하다.
  포인터에는 `&` 연산을 할 수 없고, 정수를 더하면 가리키는 타입의 크기
  (`sizeof(*p) * n`)만큼 더해진다. `void *`는 표준 C에서 덧셈도 안 된다.
  `uint64_t`가 아니라 `uintptr_t`인 이유는 포인터 폭이 플랫폼마다 달라서다.
- **왜 "채우고 → fence → 개수 증가" 순서인가?**
  개수가 먼저 늘면, 그 사이에 끼어든 핸들러가 아직 채워지지 않은 칸을 영역으로 믿는다.
  `regions[nregions++] = ...`는 한 문장 안에서 두 저장의 순서를 보장하지 않고,
  두 줄로 나눠도 컴파일러는 핸들러가 이 변수들을 읽는다는 걸 모르니 순서를 바꿀 수 있다.
  `atomic_signal_fence`로 컴파일러의 재배치를 막는다.
- **핸들러 안에서 왜 write + _exit만 쓰나?**
  핸들러는 main이 `printf`를 실행하는 도중에도 끼어들 수 있다. 이때 핸들러가 또
  `printf`를 부르면 stdio 버퍼와 락이 반쯤 갱신된 상태라 깨진다. `write`는 유저 공간에
  공유 상태가 없는 시스템콜이라 이런 문제가 없다(async-signal-safe). `_exit`은 `exit`과
  달리 stdio flush나 atexit 함수 없이 바로 커널에 종료를 요청한다.
- **오버플로 검사를 왜 곱하기 전에 하나?**
  곱한 결과는 mod 2^64로 작은 수가 될 수 있어서, 그 값만 보고는 넘쳤는지 알 수 없다.
  곱하기 전에 `npages > SIZE_MAX / pagesize`로 검사하면 잘못된 값을 아예 만들지 않는다.
  이 방식은 넘치는 순간 UB인 signed 타입에도 그대로 쓸 수 있다.
- **영역 밖 폴트는 왜 SIG_DFL로 넘기나?**
  그냥 return하면 같은 명령어가 재실행되어 무한 루프에 빠진다. `_exit`으로 끝내면
  부모는 정상 종료로 보고 코어 덤프도 없다. SIG_DFL로 되돌리고 return하면 재실행 때
  커널이 평소처럼 SIGSEGV로 죽여서(139, 코어 덤프) 진짜 버그를 숨기지 않는다.

## 한계와 주의점
- mprotect는 POSIX async-signal-safe 목록에 없다 (Linux에서는 실제로 동작)
- 폴트 카운터는 하드웨어 폴트 수가 아니다. uvm 영역 안에서 SIGSEGV가 와서 핸들러가
  권한을 열어준 횟수만 센다. 같은 페이지의 재실행 폴트(커널이 프레임 할당)와
  uvm 밖의 폴트(스택, 힙 등)는 커널이 시그널 없이 처리하므로 세지 않는다.
  실제 minor fault 수는 getrusage의 ru_minflt로 확인할 수 있다.
- 스택 오버플로로 인한 SIGSEGV는 처리 못 한다 (SA_ONSTACK)
- 멀티스레드 미고려
- 영역 해제(`uvm_free`) 미구현

---

## M1: 지연 할당 (demand paging)

### 동작 흐름
1. `uvm_alloc`이 `PROT_NONE`으로 영역을 예약한다. 이 시점에는 프레임도 PTE도 없다.
2. 영역 안의 주소에 처음 접근하면 page fault가 난다. **커널의 page fault handler**가
   vm_area 권한을 확인하는데, 접근 금지라서 직접 처리하지 못하고 SIGSEGV를 보낸다.
3. 커널은 유저 모드로 돌아가기 직전에 PC를 `segv_handler`로 바꾼다.
   핸들러는 user mode에서 실행된다.
4. 핸들러는 `si_addr`이 uvm 영역 안인지 확인한다.
   - 안이면: 페이지 시작 주소로 내리고 `mprotect`로 읽기/쓰기를 허용한 뒤 return한다.
     (M2에서는 이 부분이 상태 기계로 바뀐다)
   - 밖이면: SIGSEGV를 `SIG_DFL`로 되돌리고 return한다.
5. return하면 `sigreturn`을 거쳐 폴트가 났던 **같은 명령어**가 다시 실행된다.
   - 영역 안이었다면: 다시 page fault가 나지만, 이번엔 허용된 접근이라 커널이
     **시그널 없이** 0으로 채운 프레임을 할당하고 PTE를 채운다. 명령어가 성공한다.
   - 영역 밖이었다면: 다시 폴트가 나고, 기본 동작이라 커널이 프로세스를 종료한다.

### 테스트 (`test_m1.c`)
| test | 내용 | 검증 |
|---|---|---|
| test1 | 1000페이지를 예약하고 0, 10, 20, ..., 990번 페이지(총 100개)에 처음으로 씀 | `uvm_faults()` == 100. 처음 닿은 페이지마다 SIGSEGV가 한 번씩 온다 |
| test2 | test1이 건드리지 않은 3번 페이지에 100번 씀 | 변화량 1. 첫 쓰기에서만 폴트가 나고, 이후 99번은 이미 열린 페이지라 폴트가 없다 |
| test3 | 처음 닿는 13번 페이지를 **읽음** | 읽은 값 0, 변화량 1. 읽기도 첫 접근이면 SIGSEGV가 오고, 재실행 때 커널이 프레임을 0으로 채워 할당한다(demand-zero) |
| test4 | `./test_m1 segv`: 영역 밖 0번 주소에 씀 | `Segmentation fault`, `echo $?` → 139. 핸들러가 SIG_DFL로 넘기고, 재실행 때 커널이 다시 SIGSEGV를 보내 기본 동작으로 종료한다. uvm이 진짜 버그를 숨기거나 무한 루프에 빠지지 않는지 확인 |
| test5 | test1이 이미 연 10번 페이지에 다시 씀 | 변화량 0. 이미 허용된 페이지에서는 핸들러가 불리지 않는다 |
| test6 | 영역의 마지막 바이트(`addr + 1000 * ps - 1`)에 씀 | 변화량 1. `[start, end)` 경계 검사가 마지막 바이트를 영역 안으로 판정하는지 확인 |

> M2 이후 test_m1의 폴트 수는 달라진다. 처음부터 쓰는 페이지는 SIGSEGV가 2번 오기 때문이다.

---

## M2: 읽기/쓰기 구분과 dirty 추적

### 상태 기계
| 현재 상태 | 폴트의 의미 | 권한 | 다음 상태 |
|---|---|---|---|
| UNTOUCHED | 첫 접근 (읽기인지 쓰기인지 모름) | `PROT_READ` | READ |
| READ | 쓰기 접근 | `PROT_READ \| PROT_WRITE` | DIRTY |
| DIRTY | uvm이 처리할 수 없는 폴트 (실행 접근, 상태 불일치) | 메시지 후 `SIG_DFL` | 종료 |
| 그 외 값 | 상태 오염 | `SIG_DFL` | 종료 |

- `siginfo_t`에는 읽기/쓰기를 구분하는 이식성 있는 필드가 없다. 그래서 처음엔 읽기만
  열어두고, READ 페이지에서 다시 폴트가 나면 쓰기였다고 판단한다.
- 처음부터 쓰는 페이지는 SIGSEGV가 2번 온다(UNTOUCHED → READ → DIRTY). 이식성을 얻는
  대신 폴트가 하나 더 든다. x86에서는 `ucontext`의 에러 코드로 한 번에 구분할 수 있지만
  아키텍처 전용이다.
- `mprotect`는 권한을 더하는 게 아니라 **교체**한다. 쓰기를 열 때 `PROT_WRITE`만 주면
  읽기 권한이 사라지므로 `PROT_READ | PROT_WRITE`를 준다.

### dirty의 의미
- "로드된 뒤로 한 번이라도 쓰였나"를 나타낸다. 내보낼 때(M3) 저장이 필요한지 판단하는 데 쓴다.
  write-back 캐시의 dirty 비트와 같은 개념이다(캐시 블록 ↔ 메모리 관계가 페이지 ↔ swap 관계로 바뀐 것).
- 0 → 1로 바뀌는 순간(첫 쓰기)만 잡으면 되므로, 쓰기 권한을 준 뒤 폴트가 안 나는 건 의도된 설계다.
- 하드웨어 D 비트가 없는 CPU에서 커널이 dirty를 추적하는 방법과 같다.

### M2 설계 결정
- **상태는 페이지 단위로 둔다.** 영역 단위로 두면 한 페이지만 써도 영역 전체가 DIRTY가 되어
  내보낼 때 전부 저장해야 한다. 하드웨어도 PTE마다 D 비트가 있다.
- **상태는 직접 정한 enum으로, 권한과 분리한다.** M3의 EVICTED는 UNTOUCHED와 권한이
  같지만(PROT_NONE) 처리가 다르다. 권한 값만 저장하면 둘을 구분할 수 없다.
- **상태 배열은 uvm_alloc에서 calloc으로 확보한다.** 핸들러에서는 malloc을 쓸 수 없다.
  UNTOUCHED를 0으로 정해서 calloc만으로 초기화된다. calloc은 개수 × 크기 오버플로도 검사한다.
- **calloc → mmap 순서, 실패하면 역순으로 되돌린다.** 되돌리기가 free(실패하지 않음)로 단순해진다.
  영역 표에는 둘 다 성공한 뒤에만 넣는다.
- **상태는 mprotect가 성공한 후에 바꾼다.** 상태는 항상 실제 권한과 일치해야 한다.
  먼저 바꾸면 실패 처리를 바꿨을 때 다음 폴트를 잘못 해석한다.
- **switch의 default는 fallback으로 보낸다.** 모르는 상태 값이면 권한을 열지 않고 안전하게 죽는다.
- **dirty 수는 매번 스캔한다.** 카운터를 따로 두면 O(1)이지만, M3에서 clean으로 바뀔 때
  감소를 빠뜨리면 상태와 어긋난다. 스캔은 O(전체 페이지 수)지만 항상 정확하다.
- **실패 처리는 함수 끝의 fallback 레이블로 모은다.** 정상 경로는 그 앞에서 return한다.

### 테스트 (`test_m2.c`)
모든 메모리 접근은 `volatile`로 해서 `-O2`에서도 읽기/쓰기가 지워지지 않게 한다.

| test | 내용 | 검증 |
|---|---|---|
| test1 | 처음 닿는 1번 페이지를 읽기만 | 값 0, 폴트 +1, dirty +0, 상태 READ |
| test2 | 처음 닿는 2번 페이지에 쓰기 | 폴트 +2, dirty +1, 상태 DIRTY |
| test3 | READ 상태인 1번 페이지에 쓰기 | 폴트 +1, dirty +1, 상태 DIRTY |
| test4 | DIRTY 상태인 1번 페이지에 다시 쓰기 | 폴트 +0, dirty +0, 쓴 값이 남아 있음 |
| test5 | 상태 조회 경계 | 안 닿은 500번 페이지는 UNTOUCHED, 영역 밖(NULL)은 -1 |
| test6 | 새 영역 100페이지, 홀수는 쓰기 / 짝수는 읽기만 | 폴트 +150 (쓰기 50 × 2 + 읽기 50 × 1), dirty +50, 페이지마다 상태 확인 |

---

## 개선 아이디어
- oldact 백업으로 원래 SIGSEGV 핸들러 복원 (라이브러리는 자기가 바꾼 것만 되돌린다)
- `uvm_free`: munmap과 상태 배열 해제, 영역 표에서 제거
- M3: 상주 페이지 제한과 교체 (FIFO → clock), dirty 페이지만 저장
- M4: COW 스냅샷

---

## 배운 점 / 실수 기록

### M1

#### 포인터와 주소
- 값이 필요한 곳에 `&변수`를 넘김: `mprotect(&addr, ..)`, `.start = &addr`.
  `&addr`는 mmap이 준 주소가 아니라 지역 변수 addr가 스택에 놓인 위치다.
  → 받는 쪽이 원하는 게 변수의 "값"인지 "위치"인지 먼저 묻기.
- 주소를 정수로 바꾸고 다시 포인터로 돌리는 단계를 빠뜨림 (`mprotect`에 uintptr_t 전달).
- `void *`에 정수 덧셈 (`addr + i`). 표준 C에서 불가. `char *`로 받아서 계산.
- `addr & ~(pagesize - 1);`처럼 계산 결과를 저장하지 않음 (statement with no effect).
- 마스크 폭: `~`는 피연산자 타입 폭에서 계산된다. 32비트에서 `~` 후 넓히면 상위 32비트가
  날아간다. → 먼저 uintptr_t로 넓힌 다음 `~`.
- `uint64_t` 대신 `uintptr_t`: 32비트 플랫폼에서 포인터 변환 경고(-Werror면 빌드 실패).

#### 단위와 조건
- 단위 혼동: `npages / pagesize` (페이지 ÷ 바이트/페이지). 바이트는 `npages * pagesize`.
- 지수와 값 혼동: `1 << PAGESIZE` (PAGESIZE는 이미 4096). 시프트 칸 수 ≥ 타입 폭이면 UB.
- 성공 조건을 반대로 씀: `assert(!addr)`, `strcmp(...)`를 참/거짓으로 사용(같으면 0).
- 테스트 루프 범위: 끝과 증가량을 바꿔 써서 1번만 돎, 루프 변수를 주소 계산에 안 써서
  매번 같은 페이지에 씀. → 시작/끝/증가에 단위를 붙여서 확인.

#### 시그널 API
- `sa_handler`(인자 1개)와 `sa_sigaction`(인자 3개)을 혼동. 3인자 핸들러는
  `sa_sigaction` + `SA_SIGINFO` 짝으로 써야 한다. 플래그가 없으면 info가 쓰레기 값이다.
- `sigaction(sig, SIG_DFL, NULL)`: 두 번째 인자는 설정서(struct sigaction *)다.
  SIG_DFL은 그 구조체의 sa_handler 필드에 넣는 값이다.
- `info->si_handler = SIG_DFL`: siginfo_t는 커널이 준 "보고서"이고 고쳐도 아무 효과가 없다.
  처리 방식은 sigaction()으로만 바뀐다.
- `sigreturn;`을 직접 부르려 함. 핸들러는 그냥 return하면 trampoline이 sigreturn을 호출한다.
- 핸들러 안에서 `perror`, `exit` 사용. async-signal-safe가 아니다 → `write` + `_exit`.
- 라이브러리 함수(uvm_init)에서 `_exit` 사용. main 쪽 함수는 실패를 -1로 알리고
  호출자가 결정하게 해야 한다.

#### mmap과 영역 표
- mmap 실패를 `!addr`로 검사. 실패 값은 NULL이 아니라 `MAP_FAILED`((void *)-1).
- `regions[nregions++] = ...`: 채우기와 개수 증가의 순서가 보장되지 않는다.
  → 채우기, `atomic_signal_fence`, 증가 순으로 분리.
- 영역 밖 테스트에 영역 바로 뒤 주소(`addr + size`)를 씀. 그 자리에 다른 매핑이 있을 수
  있어 결과가 흔들린다. → 확실히 비어 있는 0번 주소 사용.

#### 개념
- 핸들러가 두 개다: 커널 page fault handler(예외, kernel mode)와 segv_handler
  (시그널, user mode). 커널이 해결을 포기한 폴트만 시그널로 넘어온다.
- segv_handler는 프레임을 할당하지 않는다. vm_area 권한만 바꾸고, 실제 할당은
  재실행 때 커널이 한다.
- 시그널은 프로세스가 "보는" 게 아니라, 커널이 유저로 돌아가기 직전에 PC를 바꿔서 전달한다.
- demand-zero는 예약 시점이 아니라 프레임을 할당하는 시점에 0으로 채운다.
- fault는 같은 명령어로 돌아가므로, 원인을 고치지 않고 return하면 무한 루프다.

### M2

#### 포인터와 구조체
- `.state = &state`: 지역 변수의 주소를 전역에 저장(댕글링 포인터). 지역 변수 안의 값
  (힙 주소)인 `state`를 저장해야 한다. (`&변수` 실수 세 번째)
- 복합 리터럴로 구조체를 다시 채우면서 state 필드를 빠뜨려 NULL로 덮어씀.
  지정하지 않은 필드는 0으로 초기화된다.
- 상태 배열을 권한 값(PROT_*)으로 저장하려 함. M3에서 UNTOUCHED와 EVICTED를 구분할 수 없다.
- `char *`와 `unsigned char *` 혼용 → `-Wall`의 signedness 경고.
- 크기를 담는 변수를 `int`로 선언(`int pnum`). `size_t`여야 한다. `-Wconversion`이어야 잡힌다.

#### 제어 흐름
- goto 구조에서 정상 경로 끝의 `return` 누락 → 첫 폴트 처리 후 SIGSEGV가 SIG_DFL로 바뀌어
  다음 폴트에서 프로세스가 죽는다.
- 레이블(`fallback:`, `case`) 바로 뒤에 선언. C17까지는 에러이고 gcc는 C23 기능으로 허용한다.
  case 안의 선언은 중괄호로 감싼다(초기화를 건너뛰는 점프도 막는다).
- READ를 `default:`로 처리 → 오염된 상태 값에 쓰기 권한을 열어줄 수 있었다.
  알려진 상태는 case로 명시하고 default는 fallback으로.
- 메시지를 포인터 변수로 고르면 `sizeof msg`가 포인터 크기(8)가 된다.
- 진단 메시지를 stdout(fd 1)으로, 길이를 손으로 세서(`'\0'`까지 출력) 씀.

#### 테스트
- `(void)c`는 메모리 접근이 아니다. 읽기를 테스트하려면 실제로 읽어야 하고,
  값을 안 쓰면 `-O2`에서 지워질 수 있으므로 volatile로 읽는다.
- 여러 조건을 `&&`로 묶은 assert → 실패해도 어느 조건인지 모른다. 하나씩 나눈다.
- 루프 밖에서 마지막 값만 검사 → 루프 안에서 매번 검사한다.

#### 개념
- dirty는 "쓰였는가"를 나타내는 1비트라서 첫 쓰기만 잡으면 된다. 이후 쓰기에 폴트가
  안 나는 건 의도된 설계다.
- `mprotect`는 권한을 교체한다.
- 여러 자원을 차례로 얻다가 실패하면 이미 얻은 것을 역순으로 되돌린다.
- 상태는 작업이 성공한 후에 기록한다.