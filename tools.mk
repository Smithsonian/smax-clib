include config.mk

LDFLAGS += -L$(LIB) -lsmax
LD_LIBRARY_PATH := $(LIB):$(LD_LIBRARY_PATH)

# Top level make targets...
all: $(BIN)/smaxValue $(BIN)/smaxWrite

include build.mk