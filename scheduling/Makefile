
EXTRA_CFLAGS +=
KERNEL_SRC:= /lib/modules/$(shell uname -r)/build
SUBDIR= $(PWD)
GCC:=gcc
RM:=rm

.PHONY : clean

all: clean modules test

obj-m:= mp2_group1.o


modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(SUBDIR) modules

test: test.c
	$(GCC) test.c -o test

clean:
	$(RM) -f *.ko *.o *.mod.c Module.symvers test modules.order
