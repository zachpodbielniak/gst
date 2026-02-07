# config.mk - GST Configuration
# GObject Simple Terminal
#
# This file contains all configurable build options.
# Override any variable on the command line:
#   make DEBUG=1
#   make PREFIX=/usr/local

# Version
VERSION_MAJOR := 0
VERSION_MINOR := 1
VERSION_MICRO := 0
VERSION := $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_MICRO)

# Installation directories
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
DATADIR ?= $(PREFIX)/share
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig
GIRDIR ?= $(DATADIR)/gir-1.0
TYPELIBDIR ?= $(LIBDIR)/girepository-1.0
MODULEDIR ?= $(LIBDIR)/gst/modules
SYSCONFDIR ?= /etc

# Build directories
BUILDDIR := build
OBJDIR_DEBUG := $(BUILDDIR)/debug/obj
OBJDIR_RELEASE := $(BUILDDIR)/release/obj
BINDIR_DEBUG := $(BUILDDIR)/debug
BINDIR_RELEASE := $(BUILDDIR)/release

# Build options (0 or 1)
DEBUG ?= 0
ASAN ?= 0
BUILD_GIR ?= 1
BUILD_TESTS ?= 1
BUILD_MODULES ?= 1
BUILD_WAYLAND ?= 1

# Select build directories based on DEBUG
ifeq ($(DEBUG),1)
    OBJDIR := $(OBJDIR_DEBUG)
    OUTDIR := $(BINDIR_DEBUG)
    BUILD_TYPE := debug
else
    OBJDIR := $(OBJDIR_RELEASE)
    OUTDIR := $(BINDIR_RELEASE)
    BUILD_TYPE := release
endif

# Compiler and tools
CC := gcc
AR := ar
PKG_CONFIG ?= pkg-config
GIR_SCANNER ?= g-ir-scanner
GIR_COMPILER ?= g-ir-compiler
INSTALL := install
INSTALL_PROGRAM := $(INSTALL) -m 755
INSTALL_DATA := $(INSTALL) -m 644
MKDIR_P := mkdir -p

# C standard and dialect
CSTD := -std=gnu89

# Base compiler flags
CFLAGS_BASE := $(CSTD) -Wall -Wextra -Wno-unused-parameter
CFLAGS_BASE += -fPIC
CFLAGS_BASE += -DGST_VERSION=\"$(VERSION)\"
CFLAGS_BASE += -DGST_VERSION_MAJOR=$(VERSION_MAJOR)
CFLAGS_BASE += -DGST_VERSION_MINOR=$(VERSION_MINOR)
CFLAGS_BASE += -DGST_VERSION_MICRO=$(VERSION_MICRO)
CFLAGS_BASE += -DGST_MODULEDIR=\"$(MODULEDIR)\"
CFLAGS_BASE += -DGST_SYSCONFDIR=\"$(SYSCONFDIR)\"
CFLAGS_BASE += -DGST_DATADIR=\"$(DATADIR)\"

# Debug/Release flags
ifeq ($(DEBUG),1)
    CFLAGS_BUILD := -g -O0 -DDEBUG
    ifeq ($(ASAN),1)
        CFLAGS_BUILD += -fsanitize=address -fsanitize=undefined
        LDFLAGS_ASAN := -fsanitize=address -fsanitize=undefined
    endif
else
    CFLAGS_BUILD := -O2 -DNDEBUG
endif

# Required dependencies
DEPS_REQUIRED := glib-2.0 gobject-2.0 gio-2.0 gmodule-2.0
DEPS_REQUIRED += x11 xft fontconfig
DEPS_REQUIRED += yaml-0.1 json-glib-1.0

# Optional Wayland dependencies
ifeq ($(BUILD_WAYLAND),1)
DEPS_WAYLAND := wayland-client wayland-cursor xkbcommon cairo cairo-ft
WAYLAND_AVAILABLE := $(shell $(PKG_CONFIG) --exists $(DEPS_WAYLAND) 2>/dev/null && echo 1 || echo 0)
else
WAYLAND_AVAILABLE := 0
endif

# Check for required dependencies
define check_dep
$(if $(shell $(PKG_CONFIG) --exists $(1) && echo yes),,$(error Missing dependency: $(1)))
endef

# Get flags from pkg-config
CFLAGS_DEPS := $(shell $(PKG_CONFIG) --cflags $(DEPS_REQUIRED) 2>/dev/null)
LDFLAGS_DEPS := $(shell $(PKG_CONFIG) --libs $(DEPS_REQUIRED) 2>/dev/null)

# Add Wayland flags if available
ifeq ($(WAYLAND_AVAILABLE),1)
    CFLAGS_BASE += -DGST_HAVE_WAYLAND=1
    CFLAGS_DEPS += $(shell $(PKG_CONFIG) --cflags wayland-client wayland-cursor xkbcommon cairo cairo-ft 2>/dev/null)
    LDFLAGS_DEPS += $(shell $(PKG_CONFIG) --libs wayland-client wayland-cursor xkbcommon cairo cairo-ft 2>/dev/null)
    LDFLAGS_DEPS += -lrt
endif

# Include paths
CFLAGS_INC := -I. -Isrc -Ideps/yaml-glib/src

# Combine all CFLAGS
CFLAGS := $(CFLAGS_BASE) $(CFLAGS_BUILD) $(CFLAGS_INC) $(CFLAGS_DEPS)

# Linker flags
LDFLAGS := $(LDFLAGS_DEPS) $(LDFLAGS_ASAN)
LDFLAGS_SHARED := -shared -Wl,-soname,libgst.so.$(VERSION_MAJOR)

# Library names
LIB_NAME := gst
LIB_STATIC := lib$(LIB_NAME).a
LIB_SHARED := lib$(LIB_NAME).so
LIB_SHARED_FULL := lib$(LIB_NAME).so.$(VERSION)
LIB_SHARED_MAJOR := lib$(LIB_NAME).so.$(VERSION_MAJOR)

# GIR settings
GIR_NAMESPACE := GstTerm
GIR_VERSION := $(VERSION_MAJOR).$(VERSION_MINOR)
GIR_FILE := $(GIR_NAMESPACE)-$(GIR_VERSION).gir
TYPELIB_FILE := $(GIR_NAMESPACE)-$(GIR_VERSION).typelib

# Test framework
TEST_CFLAGS := $(CFLAGS) $(shell $(PKG_CONFIG) --cflags glib-2.0)
TEST_LDFLAGS := $(LDFLAGS) -L$(OUTDIR) -lgst -Wl,-rpath,$(OUTDIR)

# Module flags (absolute include paths for out-of-tree compilation)
MODULE_CFLAGS_INC := -I$(CURDIR) -I$(CURDIR)/src -I$(CURDIR)/deps/yaml-glib/src
MODULE_CFLAGS := $(CFLAGS_BASE) $(CFLAGS_BUILD) $(MODULE_CFLAGS_INC) $(CFLAGS_DEPS)
MODULE_LDFLAGS := -shared -fPIC

# Print configuration (for debugging)
.PHONY: show-config
show-config:
	@echo "GST Build Configuration"
	@echo "========================"
	@echo "Version:      $(VERSION)"
	@echo "Build type:   $(BUILD_TYPE)"
	@echo "Compiler:     $(CC)"
	@echo "CFLAGS:       $(CFLAGS)"
	@echo "LDFLAGS:      $(LDFLAGS)"
	@echo "PREFIX:       $(PREFIX)"
	@echo "MODULEDIR:    $(MODULEDIR)"
	@echo "DEBUG:        $(DEBUG)"
	@echo "ASAN:         $(ASAN)"
	@echo "BUILD_GIR:    $(BUILD_GIR)"
	@echo "BUILD_TESTS:  $(BUILD_TESTS)"
	@echo "BUILD_MODULES:$(BUILD_MODULES)"
	@echo "BUILD_WAYLAND:$(BUILD_WAYLAND)"
	@echo "WAYLAND_AVAILABLE:$(WAYLAND_AVAILABLE)"
