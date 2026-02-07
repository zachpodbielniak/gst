# Makefile - GST (GObject Simple Terminal)
# A GObject-based terminal emulator with modular extensibility
#
# Usage:
#   make           - Build all (lib, gst, modules)
#   make lib       - Build static and shared libraries
#   make gst       - Build the gst executable
#   make gir       - Generate GIR/typelib for introspection
#   make modules   - Build all modules
#   make test      - Run the test suite
#   make install   - Install to PREFIX
#   make clean     - Clean build artifacts
#   make DEBUG=1   - Build with debug symbols
#   make ASAN=1    - Build with AddressSanitizer

.DEFAULT_GOAL := all
.PHONY: all lib gst gir modules test deps check-deps

# Include configuration
include config.mk

# Check dependencies before anything else (skip for targets that don't need them)
SKIP_DEP_CHECK_TARGETS := install-deps help check-deps show-config clean clean-all
ifeq ($(filter $(SKIP_DEP_CHECK_TARGETS),$(MAKECMDGOALS)),)
$(foreach dep,$(DEPS_REQUIRED),$(call check_dep,$(dep)))
endif

# Source files - Library
LIB_SRCS := \
	src/gst-enums.c \
	src/boxed/gst-glyph.c \
	src/boxed/gst-cursor.c \
	src/core/gst-line.c \
	src/core/gst-terminal.c \
	src/core/gst-pty.c \
	src/core/gst-escape-parser.c \
	src/rendering/gst-renderer.c \
	src/rendering/gst-render-context.c \
	src/rendering/gst-x11-renderer.c \
	src/rendering/gst-x11-render-context.c \
	src/rendering/gst-font-cache.c \
	src/window/gst-window.c \
	src/window/gst-x11-window.c \
	src/config/gst-config.c \
	src/config/gst-color-scheme.c \
	src/config/gst-keybind.c \
	src/module/gst-module.c \
	src/module/gst-module-manager.c \
	src/module/gst-module-info.c \
	src/selection/gst-selection.c \
	src/selection/gst-clipboard.c \
	src/interfaces/gst-color-provider.c \
	src/interfaces/gst-input-handler.c \
	src/interfaces/gst-output-filter.c \
	src/interfaces/gst-render-overlay.c \
	src/interfaces/gst-font-provider.c \
	src/interfaces/gst-url-handler.c \
	src/interfaces/gst-glyph-transformer.c \
	src/interfaces/gst-bell-handler.c \
	src/interfaces/gst-external-pipe.c \
	src/interfaces/gst-escape-handler.c \
	src/util/gst-utf8.c \
	src/util/gst-base64.c

# Wayland/Cairo sources (conditional)
ifeq ($(WAYLAND_AVAILABLE),1)
LIB_SRCS += \
	src/rendering/gst-cairo-font-cache.c \
	src/rendering/gst-wayland-renderer.c \
	src/rendering/gst-wayland-render-context.c \
	src/window/gst-wayland-window.c
endif

# Header files (for GIR scanner and installation)
LIB_HDRS := \
	src/gst.h \
	src/gst-types.h \
	src/gst-enums.h \
	src/gst-version.h \
	src/boxed/gst-glyph.h \
	src/boxed/gst-cursor.h \
	src/core/gst-line.h \
	src/core/gst-terminal.h \
	src/core/gst-pty.h \
	src/core/gst-escape-parser.h \
	src/rendering/gst-renderer.h \
	src/rendering/gst-render-context.h \
	src/rendering/gst-x11-renderer.h \
	src/rendering/gst-x11-render-context.h \
	src/rendering/gst-font-cache.h \
	src/window/gst-window.h \
	src/window/gst-x11-window.h \
	src/config/gst-config.h \
	src/config/gst-color-scheme.h \
	src/config/gst-keybind.h \
	src/module/gst-module.h \
	src/module/gst-module-manager.h \
	src/module/gst-module-info.h \
	src/selection/gst-selection.h \
	src/selection/gst-clipboard.h \
	src/interfaces/gst-color-provider.h \
	src/interfaces/gst-input-handler.h \
	src/interfaces/gst-output-filter.h \
	src/interfaces/gst-render-overlay.h \
	src/interfaces/gst-font-provider.h \
	src/interfaces/gst-url-handler.h \
	src/interfaces/gst-glyph-transformer.h \
	src/interfaces/gst-bell-handler.h \
	src/interfaces/gst-external-pipe.h \
	src/interfaces/gst-escape-handler.h \
	src/util/gst-utf8.h \
	src/util/gst-base64.h

# Wayland/Cairo headers (conditional)
ifeq ($(WAYLAND_AVAILABLE),1)
LIB_HDRS += \
	src/rendering/gst-cairo-font-cache.h \
	src/rendering/gst-wayland-renderer.h \
	src/rendering/gst-wayland-render-context.h \
	src/window/gst-wayland-window.h
endif

# yaml-glib sources (built-in dependency)
YAMLGLIB_SRCS := \
	deps/yaml-glib/src/yaml-builder.c \
	deps/yaml-glib/src/yaml-document.c \
	deps/yaml-glib/src/yaml-generator.c \
	deps/yaml-glib/src/yaml-gobject.c \
	deps/yaml-glib/src/yaml-mapping.c \
	deps/yaml-glib/src/yaml-node.c \
	deps/yaml-glib/src/yaml-parser.c \
	deps/yaml-glib/src/yaml-schema.c \
	deps/yaml-glib/src/yaml-sequence.c \
	deps/yaml-glib/src/yaml-serializable.c

# Test sources
TEST_SRCS := $(wildcard tests/test-*.c)

# Module directories
MODULE_DIRS := $(wildcard modules/*)

# Object files
LIB_OBJS := $(patsubst src/%.c,$(OBJDIR)/%.o,$(LIB_SRCS))
YAMLGLIB_OBJS := $(patsubst deps/%.c,$(OBJDIR)/deps/%.o,$(YAMLGLIB_SRCS))
MAIN_OBJ := $(OBJDIR)/main.o
TEST_OBJS := $(patsubst tests/%.c,$(OBJDIR)/tests/%.o,$(TEST_SRCS))
TEST_BINS := $(patsubst tests/%.c,$(OUTDIR)/%,$(TEST_SRCS))

# Include build rules
include rules.mk

# Default target
all: deps lib gst
ifeq ($(BUILD_MODULES),1)
all: modules
endif
ifeq ($(BUILD_GIR),1)
all: gir
endif

# Build dependencies (yaml-glib)
deps: $(YAMLGLIB_OBJS)

# Build the library
lib: src/gst-version.h $(OUTDIR)/$(LIB_STATIC) $(OUTDIR)/$(LIB_SHARED_FULL) $(OUTDIR)/gst.pc

# Build the executable
gst: lib $(OUTDIR)/gst

# Build GIR/typelib
gir: $(OUTDIR)/$(GIR_FILE) $(OUTDIR)/$(TYPELIB_FILE)

# Build all modules
modules: lib $(OUTDIR)/modules
	@for dir in $(MODULE_DIRS); do \
		if [ -d "$$dir" ] && [ -f "$$dir/Makefile" ]; then \
			echo "Building module: $$(basename $$dir)"; \
			$(MAKE) -C "$$dir" \
				OUTDIR=$(abspath $(OUTDIR)/modules) \
				LIBDIR=$(abspath $(OUTDIR)) \
				CFLAGS="$(MODULE_CFLAGS)" \
				LDFLAGS="$(MODULE_LDFLAGS)"; \
		fi \
	done

# Build and run tests
test: lib $(TEST_BINS)
	@echo "Running tests..."
	@failed=0; \
	for test in $(TEST_BINS); do \
		echo "  Running $$(basename $$test)..."; \
		if LD_LIBRARY_PATH=$(OUTDIR) $$test; then \
			echo "    PASS"; \
		else \
			echo "    FAIL"; \
			failed=$$((failed + 1)); \
		fi \
	done; \
	if [ $$failed -gt 0 ]; then \
		echo "$$failed test(s) failed"; \
		exit 1; \
	else \
		echo "All tests passed"; \
	fi

# Build individual test binaries
$(OUTDIR)/test-%: $(OBJDIR)/tests/test-%.o $(OUTDIR)/$(LIB_SHARED_FULL)
	$(CC) -o $@ $< $(TEST_LDFLAGS)

# Check dependencies
check-deps:
	@echo "Checking dependencies..."
	@for dep in $(DEPS_REQUIRED); do \
		if $(PKG_CONFIG) --exists $$dep; then \
			echo "  $$dep: OK ($(shell $(PKG_CONFIG) --modversion $$dep 2>/dev/null))"; \
		else \
			echo "  $$dep: MISSING"; \
		fi \
	done

# Help target
.PHONY: help
help:
	@echo "GST - GObject Simple Terminal"
	@echo ""
	@echo "Build targets:"
	@echo "  all        - Build library, executable, and modules (default)"
	@echo "  lib        - Build static and shared libraries"
	@echo "  gst        - Build the gst executable"
	@echo "  gir        - Generate GObject Introspection data"
	@echo "  modules    - Build all modules"
	@echo "  test       - Build and run the test suite"
	@echo "  install    - Install to PREFIX ($(PREFIX))"
	@echo "  uninstall  - Remove installed files"
	@echo "  clean      - Remove build artifacts"
	@echo "  clean-all  - Remove all build directories"
	@echo ""
	@echo "Build options (set on command line):"
	@echo "  DEBUG=1       - Enable debug build"
	@echo "  ASAN=1        - Enable AddressSanitizer"
	@echo "  PREFIX=path   - Set installation prefix"
	@echo "  BUILD_GIR=0   - Disable GIR generation"
	@echo "  BUILD_TESTS=0 - Disable test building"
	@echo ""
	@echo "Utility targets:"
	@echo "  install-deps - Install build dependencies (Fedora/dnf)"
	@echo "  check-deps   - Check for required dependencies"
	@echo "  show-config  - Show current build configuration"
	@echo "  help         - Show this help message"

# Dependency tracking (optional, for incremental builds)
-include $(LIB_OBJS:.o=.d)
-include $(MAIN_OBJ:.o=.d)
