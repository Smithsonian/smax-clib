INC = ../include

# Load the common Makefile definitions...
include $(GLOBALINC)/setup.mk

ifeq ($(OSNAME),Linux)
	LDFLAGS += -lrt
endif

CFLAGS0 += -g

LDFLAGS += -lm $(NETFLAGS) $(THREADS)

TESTS = $(BIN)/simpleIntTest $(BIN)/simpleIntsTest $(BIN)/structTest $(BIN)/queueTest $(BIN)/lazyTest \
		$(BIN)/waitTest $(BIN)/resilientTest

# Top level make targets...
all: $(TESTS)

# Additional dependencies...
$(BIN)/benchmarkRM: $(RMDSM) 

$(BIN)/lazyTest: $(SMAX)

$(BIN)/queueTest: $(SMAX)

$(BIN)/simpleIntTest: $(SMAX)

$(BIN)/simpleIntsTest: $(SMAX)

$(BIN)/structTest: $(SMAX)

$(BIN)/waitTest: $(SMAX)

$(BIN)/resilientTest: $(SMAX)




# Standard generic rules and targets...
include $(GLOBALINC)/recipes.mk
