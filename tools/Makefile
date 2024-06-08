# Load the common Makefile definitions...
include $(GLOBALINC)/setup.mk

ifeq ($(OSNAME),Linux)
	LDFLAGS += -lrt
else
	LDFLAGS += -llynx
endif

LDFLAGS += $(NETFLAGS) $(THREADS)

# Top level make targets...
all: $(BIN)/smaxValue  $(BIN)/smaxWrite

# Additional dependencies...
$(BIN)/smaxValue: $(OBJ)/smaxValue.o $(SMAX) 

$(BIN)/smaxWrite: $(OBJ)/smaxWrite.o $(SMAX) 

install: install-platform

install-platform: $(BIN)/smaxValue $(BIN)/smaxWrite

# Standard generic rules and targets...
include $(GLOBALINC)/recipes.mk




