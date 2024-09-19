include config.mk

# Load the common Makefile definitions...
CFLAGS += -g
LDFLAGS += -L$(LIB) -lsmax

LD_LIBRARY_PATH := $(LIB):$(LD_LIBRARY_PATH)


TESTS = $(BIN)/simpleIntTest $(BIN)/simpleIntsTest $(BIN)/structTest $(BIN)/queueTest $(BIN)/lazyTest \
		$(BIN)/waitTest $(BIN)/resilientTest

# Top level make targets...
all: $(TESTS)

include build.mk