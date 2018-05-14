# Configure NimBLE variables
NIMBLE_ROOT := ../../..
NIMBLE_CFG_TINYCRYPT := 1
include $(NIMBLE_ROOT)/porting/nimble/Makefile.defs

# Add dummy NPL, dummy HCI transport and all NimBLE sources to build
SRC = \
	$(NIMBLE_ROOT)/porting/npl/dummy/src/npl_os_dummy.c \
	$(NIMBLE_ROOT)/porting/npl/dummy/src/hci_dummy.c \
	$(NIMBLE_SRC) \
	main.c \

# Add dummy NPL and all NimBLE directories to include paths
INC = \
	$(NIMBLE_ROOT)/porting/npl/dummy/include \
	$(NIMBLE_INCLUDE) \

OBJ := $(SRC:.c=.o)

CFLAGS := $(NIMBLE_CFLAGS)

.PHONY: all clean
.DEFAULT: all

all: dummy

clean:
	rm $(OBJ) -f
	rm dummy -f

%.o: %.c
	$(CC) -m32 -c $(addprefix -I, $(INC)) $(CFLAGS) -o $@ $<

dummy: $(OBJ)
	$(CC) -m32 -o $@ $^
