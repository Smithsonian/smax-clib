SRC := tests

include config.mk

# Load the common Makefile definitions...
CFLAGS += -g
LDFLAGS += -L$(LIB) -lsmax

LD_LIBRARY_PATH := $(LIB):$(LD_LIBRARY_PATH)

TESTS = $(BIN)/simpleIntTest $(BIN)/simpleIntsTest $(BIN)/structTest $(BIN)/queueTest $(BIN)/lazyTest \
		$(BIN)/lazyCacheTest $(BIN)/waitTest $(BIN)/resilientTest

.PHONY: run
run: build test-tools
	$(BIN)/simpleIntTest
	$(BIN)/simpleIntsTest
	$(BIN)/structTest
	$(BIN)/queueTest
	$(BIN)/lazyTest
	$(BIN)/lazyCacheTest
	$(BIN)/waitTest

.PHONY: run2
run2: run
	$(BIN)/resilientTest

.PHONY: test-tools
test-tools: test-int8 test-int16 test-int32 test-double test-string test-int-array

.PHONY: test-int8
test-int8:
	@echo $@
	@$(BIN)/smaxWrite -t int8 "_test_:tools:int8" 11
	@test `$(BIN)/smaxValue _test_:tools int8` -eq 11

.PHONY: test-int16
test-int16:
	@echo $@
	@$(BIN)/smaxWrite -t int16 "_test_:tools:int16" 2025
	@test `$(BIN)/smaxValue _test_:tools int16` -eq 2025

.PHONY: test-int32
test-int32:
	@echo $@
	@$(BIN)/smaxWrite -t int32 "_test_:tools:int32" 20250707
	@test `$(BIN)/smaxValue _test_:tools int32` -eq 20250707

.PHONY: test-float
test-double:
	@echo $@
	@$(BIN)/smaxWrite -t float "_test_:tools:float" 3.14
	@test `$(BIN)/smaxValue _test_:tools float` = 3.14

.PHONY: test-double
test-double:
	@echo $@
	@$(BIN)/smaxWrite -t double "_test_:tools:double" 3.14159265
	@test `$(BIN)/smaxValue _test_:tools double` = 3.14159265

.PHONY: test-string
test-string:
	@echo $@
	@$(BIN)/smaxWrite -t string "_test_:tools:string" abc
	@test `$(BIN)/smaxValue _test_:tools string` = abc

.PHONY: test-int-array
test-int-array:
	@echo $@
	@$(BIN)/smaxWrite -t int32 -d 4 "_test_:tools:numbers" "1,2,3,4"
	@test "`$(BIN)/smaxValue _test_:tools numbers | xargs`" = "1 2 3 4"

# Top level make targets...
.PHONY: build
build: $(TESTS)

.PHONY: benchmark
benchmark: $(BIN)/benchmark

$(BIN)/benchmark: LDFLAGS += -lpopt -lbsd

include build.mk