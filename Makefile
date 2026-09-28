# #############################################################################
# 
# 	dmsdio - SD memory card driver (SDSC/SDHC/SDXC).
#
# 	The CMake build is the reference build: it also fetches the headers of
# 	dmdrvi, dmini, dmosi, dmgpio and libsystemd (dmod_link_modules) and builds
# 	dmsdio_port, the dmsdiod service and the tests. Pass their include directories through
# 	DMOD_INC_DIRS when building with make.
#
# #############################################################################
DMOD_DIR=@DMOD_DIR@

# -----------------------------------------------------------------------------
#  Paths initialization
# -----------------------------------------------------------------------------
include $(DMOD_DIR)/paths.mk

# -----------------------------------------------------------------------------
#   Module configuration
# -----------------------------------------------------------------------------

# The name of the module
DMOD_MODULE_NAME=dmsdio

# The version of the module
DMOD_MODULE_VERSION=0.1

# The name of the author
DMOD_AUTHOR_NAME=Patryk Kubiak

# The list of C sources
DMOD_CSOURCES=src/dmsdio.c \
              src/dmsdio_card.c \
              src/dmsdio_cmd.c \
              src/dmsdio_config.c \
              src/dmsdio_decode.c \
              src/dmsdio_detect.c \
              src/dmsdio_ident.c \
              src/dmsdio_io.c \
              src/dmsdio_xfer.c

# The list of C++ sources
DMOD_CXXSOURCES=

# The list of include directories
DMOD_INC_DIRS=include

# The list of libraries to link
DMOD_LIBS=

# The list of definitions
DMOD_DEFINITIONS=

# -----------------------------------------------------------------------------
#   List of MAL interfaces implemented by the module
# -----------------------------------------------------------------------------
DMOD_MAL_IMPLS=

# -----------------------------------------------------------------------------
#   List of DIF interfaces implemented by the module
# -----------------------------------------------------------------------------
DMOD_DIF_IMPLS=

# -----------------------------------------------------------------------------
#   Include the dmod app makefile
# -----------------------------------------------------------------------------
include $(DMOD_DMF_LIB_FILE_PATH)
