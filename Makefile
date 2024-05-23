# Build SMAX libs with the local recipes
SMAX_MAKE = 1

# Load the common Makefile definitions...
include $(GLOBALINC)/setup.mk

# The SMA-X library core objects.
SMAX_CORE_OBJS = $(LIB)/smax.o $(LIB)/smax-util.o $(LIB)/smax-resilient.o \
  $(LIB)/smax-lazy.o

ifeq ($(PLATFORM),lynx-ppc)
  SMAX_CORE_OBJS += $(OBJ)/procname.o
endif

# The full SMA-X library objects
SMAX_EXTENSION_OBJS = $(LIB)/smax-queue.o $(LIB)/smax-easy.o $(LIB)/smax-meta.o \
  $(LIB)/smax-messages.o $(LIB)/smax-buffers.o

ifeq ($(OSNAME),Linux)
	LDFLAGS += -lrt
endif

LDFLAGS += -lm $(NETFLAGS) $(THREADS)

# Top level make targets
# Build deps for libraries
$(LIB)/smax.a : $(SMAX_CORE_OBJS) $(SMAX_EXTENSION_OBJS)

lib: $(LIB)/smax.a

all: lib tools

distclean: purge-smax

.PHONY: purge-smax
purge-smax: 
	@rm -f $(SMAX_LIB) $(LIB)/smax.o $(LIB)/smax-*.o

.PHONY: tools
tools: 
	@make -C tools

.PHONY: test
test: 
	@make -C tests

.PHONY: tools-clean
tools-clean: 
	@make -s -C tools clean

.PHONY: test-clean
test-clean: 
	@make -s -C tests clean

.PHONY: tools-distclean
tools-distclean: 
	@make -s -C tools distclean

.PHONY: test-distclean
test-distclean: 
	@make -s -C tests distclean

clean: tools-clean test-clean

# Finally, the standard generic rules and targets...
include $(GLOBALINC)/recipes.mk



