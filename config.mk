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
CFLAGS ?= -g -Os -Wall

# Compile for specific C standard
ifdef CSTANDARD
  CFLAGS += -std=$(CSTANDARD)
endif

# Extra warnings (not supported on all compilers)
ifeq ($(WEXTRA), 1) 
  CFLAGS += -Wextra
endif

# Add source code fortification checks
ifdef FORTIFY 
  CFLAGS += -D_FORTIFY_SOURCE=$(FORTIFY)
endif

# On some old platforms __progname is not provided by libc. We have a 
# workaround in place for LynxOS/PowerPCs. For other platforms without
# __progname, uncomment the line below to use a default program name
# instead.
#NO_PROCNAME = 1

# Extra link flags (if any)
#LDFLAGS =

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

# Whether to build with TLS support (via OpenSSL). If not defined, we'll
# enable it automatically if libssl is available
#WITH_TLS = 1

# ============================================================================
# END of user config section. 
#
# Below are some generated constants based on the one that were set above
# ============================================================================

ifneq ($(shell which ldconfig), )
  # Detect OpenSSL automatically, and enable TLS support if present
  ifndef WITH_TLS 
    ifneq ($(shell ldconfig -p | grep libssl), )
      $(info INFO: TLS support is enabled automatically.)
      WITH_TLS = 1
    else
      $(info INFO: optional TLS support is not enabled.)
      WITH_TLS = 0
    endif
  endif
endif

ifeq ($(NO_PROCNAME),1)
  CPPFLAGS += -DNO_PROCNAME=1
endif

ifdef OSFLAGS
  LDFLAGS += $(OSFLAGS)
endif

ifdef NETFLAGS
  LDFLAGS += $(NETFLAGS)
endif

ifeq ($(WITH_TLS),1)
  CPPFLAGS += -DWITH_TLS=1
  LDFLAGS += -lssl
endif

# Link against pthread and dependencies
LDFLAGS += -lpthread -lredisx -lxchange 

# Search for libraries under LIB
ifneq ($(findstring $(LIB),$(LD_LIBRARY_PATH)),$LIB)
  LDFLAGS += -L$(LIB)
endif

# Compile and link against a specific redisx library (if defined)
ifdef REDISX
  CPPFLAGS += -I$(REDISX)/include
  LDFLAGS += -L$(REDISX)/lib
endif

# Compile and link against a specific xchange library (if defined)
ifdef XCHANGE
  CPPFLAGS += -I$(XCHANGE)/include
  LDFLAGS += -L$(XCHANGE)/lib
endif

# Build static or shared libs
ifeq ($(STATICLINK),1)
  LIBSMAX = $(LIB)/libsmax.a
else
  LIBSMAX = $(LIB)/libsmax.so
endif


# Search for files in the designated locations
vpath %.h $(INC)
vpath %.c $(SRC)
vpath %.o $(OBJ)
vpath %.d dep 

