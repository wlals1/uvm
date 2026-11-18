CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -g
TESTS   = test_m1 test_m2 test_m3

all: $(TESTS)

test_m1: test_m1.o uvm.o
	$(CC) $(CFLAGS) -o $@ $^

test_m2: test_m2.o uvm.o
	$(CC) $(CFLAGS) -o $@ $^

test_m3: test_m3.o uvm.o
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c uvm.h
	$(CC) $(CFLAGS) -c $<

check: $(TESTS)
	./test_m1
	./test_m1 segv; test $$? -eq 139 && echo "segv test passed"
	./test_m2
	./test_m3

clean:
	rm -f *.o $(TESTS)

.PHONY: all check clean