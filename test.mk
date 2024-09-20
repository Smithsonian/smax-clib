include config.mk

# Load the common Makefile definitions...
CFLAGS += -g
LDFLAGS += -L$(LIB) -lsmax

LD_LIBRARY_PATH := $(LIB):$(LD_LIBRARY_PATH)

TESTS = $(BIN)/simpleIntTest $(BIN)/simpleIntsTest $(BIN)/structTest $(BIN)/queueTest $(BIN)/lazyTest \
		$(BIN)/waitTest $(BIN)/resilientTest

# Top level make targets...
build: $(TESTS)

run: build
	$(BIN)/simpleIntTest
	$(BIN)/simpleIntsTest
	$(BIN)/structTest
	$(BIN)/queueTest
	$(BIN)/lazyTest
	$(BIN)/waitTest
	$(BIN)/resilientTest

include build.mk