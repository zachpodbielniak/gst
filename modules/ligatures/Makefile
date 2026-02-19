# Makefile for ligatures module
#
# This is invoked by the top-level Makefile with:
#   OUTDIR, LIBDIR, CFLAGS, LDFLAGS set appropriately.
#
# Requires HarfBuzz (harfbuzz-devel on Fedora).
# If HarfBuzz is not installed the module is silently skipped.

MODULE_NAME := ligatures
MODULE_SRC  := gst-ligatures-module.c

CC      ?= gcc

# Check for HarfBuzz availability
HB_EXISTS := $(shell pkg-config --exists harfbuzz 2>/dev/null && echo yes || echo no)

CFLAGS  ?= $(shell pkg-config --cflags glib-2.0 gobject-2.0 gmodule-2.0 harfbuzz freetype2) -fPIC -I../../src
LDFLAGS ?= -shared

.PHONY: all clean

ifeq ($(HB_EXISTS),yes)
all: $(OUTDIR)/$(MODULE_NAME).so
else
all:
	@echo "ligatures: skipping build (harfbuzz not found)"
endif

$(OUTDIR)/$(MODULE_NAME).so: $(MODULE_SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -L$(LIBDIR) -lgst \
		$(shell pkg-config --libs glib-2.0 gobject-2.0 gmodule-2.0 harfbuzz freetype2)

clean:
	rm -f $(OUTDIR)/$(MODULE_NAME).so
