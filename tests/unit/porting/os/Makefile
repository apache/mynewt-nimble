# Makefile

PROJ_ROOT = ../../../..

### ===== Toolchain =====

CROSS_COMPILE =
CC  = ccache $(CROSS_COMPILE)gcc
CPP = ccache $(CROSS_COMPILE)g++
LD  = $(CROSS_COMPILE)gcc
AR  = $(CROSS_COMPILE)ar

### ===== Compiler Flags =====

INCLUDES = \
    -I.    \
    -I$(PROJ_ROOT)/porting/linux          \
    -I$(PROJ_ROOT)/porting/linux/include  \
    -I$(PROJ_ROOT)/porting/common/include \
    $(NULL)

DEFINES =

CFLAGS =                   \
    $(INCLUDES) $(DEFINES) \
    -g                     \
    -D_GNU_SOURCE          \
    $(NULL)

#    -D_XOPEN_SOURCE=700    \

LIBS = -lrt -lpthread -lstdc++

LDFLAGS =

### ===== Sources =====

OSAL_PATH = $(PROJ_ROOT)/porting/linux/os

SRCS  = $(shell find $(OSAL_PATH) -maxdepth 1 -name '*.c')
SRCS += $(shell find $(OSAL_PATH) -maxdepth 1 -name '*.cc')
SRCS += $(PROJ_ROOT)/porting/common/src/os/os_mempool.c

OBJS  = $(patsubst %.c, %.o,$(filter %.c,  $(SRCS)))
OBJS += $(patsubst %.cc,%.o,$(filter %.cc, $(SRCS)))

TEST_SRCS  = $(shell find . -maxdepth 1 -name '*.c')
TEST_SRCS += $(shell find . -maxdepth 1 -name '*.cc')

TEST_OBJS  = $(patsubst %.c, %.o,$(filter %.c,  $(SRCS)))
TEST_OBJS += $(patsubst %.cc,%.o,$(filter %.cc, $(SRCS)))

### ===== Rules =====

all: depend              \
     test_os_mempool     \
     test_os_task        \
     test_os_eventq      \
     test_os_callout     \
     test_os_sem         \
     $(NULL)

test_os_mempool: test_os_mempool.o $(OBJS)
	$(LD) -o $@ $^ $(LDFLAGS) $(LIBS)

test_os_task: test_os_task.o $(OBJS)
	$(LD) -o $@ $^ $(LDFLAGS) $(LIBS)

test_os_eventq: test_os_eventq.o $(OBJS)
	$(LD) -o $@ $^ $(LDFLAGS) $(LIBS)

test_os_callout: test_os_callout.o $(OBJS)
	$(LD) -o $@ $^ $(LDFLAGS) $(LIBS)

test_os_sem: test_os_sem.o $(OBJS)
	$(LD) -o $@ $^ $(LDFLAGS) $(LIBS)

test_sem: test_sem.o
	$(LD) -o $@ $^ $(LDFLAGS) $(LIBS)

test: all
	./test_os_mempool
	./test_os_task
	./test_os_eventq
	./test_os_callout
	./test_os_sem

show_objs:
	@echo $(OBJS)

### ===== Clean =====
clean:
	@echo "Cleaning artifacts."
	rm *~ .depend $(OBJS)

### ===== Dependencies =====
### Rebuild if headers change
depend: .depend

.depend: $(SRCS) $(TEST_SRCS)
	@echo "Building dependencies."
	rm -f ./.depend
	$(CC) $(CFLAGS) -MM $^ > ./.depend;

include .depend

### Generic rules based on extension
%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

%.o: %.cc
	$(CPP) -c $(CFLAGS) $< -o $@
