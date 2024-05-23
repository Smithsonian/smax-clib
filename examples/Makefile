# Load the common Makefile definitions...
include $(GLOBALINC)/setup.mk

LDFLAGS += $(THREADS) $(NETFLAGS)

ifeq ($(OSNAME), Linux)
  LDFLAGS += -lrt
endif

# Top level make targets...
all:  $(BIN)/scalar $(BIN)/array $(BIN)/multidim $(BIN)/simpleScalar $(BIN)/simpleArray

# Additional dependencies...
$(BIN)/scalar: $(OBJ)/scalar.o $(SMAX) 

$(BIN)/array: $(OBJ)/array.o $(SMAX) 

$(BIN)/multidim: $(OBJ)/multidim.o $(SMAX) 

$(BIN)/simpleScalar: $(OBJ)/simpleScalar.o $(SMAX) 

$(BIN)/simpleArray: $(OBJ)/simpleArray.o $(SMAX) 


# Standard generic rules and targets...
include $(GLOBALINC)/recipes.mk