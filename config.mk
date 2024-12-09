# ===========================================================================
# Generic configuration options for building the SMA-X client library (both 
# static and shared).
#
# You can include this snipplet in your Makefile also.
# ============================================================================

# Location under which the Smithsonian/xchange library is installed.
# (I.e., the root directory under which there is an include/ directory
# that contains xchange.h, and a lib/ or lib64/ directory that contains
# libxchange.so
XCHANGE ?= /usr

# Location under which the Smithsonian/redisx library is installed.
# (I.e., the root directory under which there is an include/ directory
# that contains redisx.h, and a lib/ or lib64/ directory that contains
# libredisx.so
REDISX ?= /usr

# Folders in which sources and header files are located, respectively
SRC ?= src
INC ?= include

# Folders for compiled objects, libraries, and binaries, respectively 
OBJ ?= obj
LIB ?= lib
BIN ?= bin

# Compiler: use gcc by default
CC ?= gcc

# Add include/ directory
CPPFLAGS += -I$(INC)

# Base compiler options (if not defined externally...)
CFLAGS ?= -g -Os -Wall -std=c99

# Extra warnings (not supported on all compilers)
#CFLAGS += -Wextra

# Extra link flags (if any)
#LDFLAGS =

# Link flags to use for threading with pthread
THREADS ?= -pthread

# Link flags required for network functions (if any) to include in LDFLAGS
#NETFLAGS = -lnsl

# Link flags required for OS calls (if any) to include in LDFLAGS
#OSFLAGS =

# cppcheck options for 'check' target
CHECKOPTS ?= --enable=performance,warning,portability,style --language=c \
            --error-exitcode=1 --std=c99

# Add-on ccpcheck options
CHECKOPTS += --inline-suppr $(CHECKEXTRA)

# Exhaustive checking for newer cppcheck
#CHECKOPTS += --check-level=exhaustive

# Specific Doxygen to use if not the default one
#DOXYGEN ?= /opt/bin/doxygen

# ============================================================================
# END of user config section. 
#
# Below are some generated constants based on the one that were set above
# ============================================================================

ifdef OSFLAGS
  LDFLAGS += $(OSFLAGS)
endif

ifdef NETFLAGS
  LDFLAGS += $(NETFLAGS)
endif

# Links against pthread and dependencies
LDFLAGS += $(THREADS) -lredisx -lxchange 

# Search for libraries under LIB
ifneq ($(findstring $(LIB),$(LD_LIBRARY_PATH)),$LIB)
  LDFLAGS += -L$(LIB)
  LD_LIBRARY_PATH := $(LIB):$(LD_LIBRARY_PATH)
endif

# Compile and link against a specific redisx library (if defined)
ifdef REDISX
  CPPFLAGS += -I$(REDISX)/include
  LDFLAGS += -L$(REDISX)/lib
  LD_LIBRARY_PATH := $(REDISX)/lib:$(LD_LIBRARY_PATH)
endif

# Compile and link against a specific xchange library (if defined)
ifdef XCHANGE
  CPPFLAGS += -I$(XCHANGE)/include
  LDFLAGS += -L$(XCHANGE)/lib
  LD_LIBRARY_PATH := $(XCHANGE)/lib:$(LD_LIBRARY_PATH)
endif


# Search for files in the designated locations
vpath %.h $(INC)
vpath %.c $(SRC)
vpath %.o $(OBJ)
vpath %.d dep 

