SRC := tests

include config.mk

# Load the common Makefile definitions...
CFLAGS += -g
LDFLAGS += -L$(LIB) -lsmax

LD_LIBRARY_PATH := $(LIB):$(LD_LIBRARY_PATH)

TESTS = $(BIN)/simpleIntTest $(BIN)/simpleIntsTest $(BIN)/structTest $(BIN)/queueTest $(BIN)/lazyTest \
		$(BIN)/waitTest $(BIN)/resilientTest

# Top level make targets...
.PHONY: build
build: $(TESTS)

.PHONY: run
run: build
	$(BIN)/simpleIntTest
	$(BIN)/simpleIntsTest
	$(BIN)/structTest
	$(BIN)/queueTest
	$(BIN)/lazyTest
	$(BIN)/waitTest
	$(BIN)/resilientTest

.PHONY: benchmark
benchmark: $(BIN)/benchmark

$(BIN)/benchmark: LDFLAGS += -lpopt -lbsd

include build.mk