# Load the common Makefile definitions...
include config.mk 

CFLAGS += -g
LDFLAGS += -L$(LIB) -lsmax

LD_LIBRARY_PATH := $(LIB):$(LD_LIBRARY_PATH)


TESTS = $(BIN)/simpleIntTest $(BIN)/simpleIntsTest $(BIN)/structTest $(BIN)/queueTest $(BIN)/lazyTest \
		$(BIN)/waitTest $(BIN)/resilientTest

# Top level make targets...
all: $(TESTS)


$(BIN)/%: $(OBJ)/%.o | $(BIN)
	$(CC) -o $@ $^ $(LDFLAGS)

# Standard generic rules and targets...
include build.mk
