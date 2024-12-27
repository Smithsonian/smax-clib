SRC := tools

include config.mk

LDFLAGS := -lm -lpopt -L$(LIB) -lsmax $(LDFLAGS)
LD_LIBRARY_PATH := $(LIB):$(LD_LIBRARY_PATH)

include build.mk

# Top level make targets...
all: $(BIN)/smaxValue $(BIN)/smaxWrite
