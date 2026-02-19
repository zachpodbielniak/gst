# Makefile for sixel module
#
# This is invoked by the top-level Makefile with:
#   OUTDIR, LIBDIR, CFLAGS, LDFLAGS set appropriately.

MODULE_NAME := sixel
MODULE_SRC  := gst-sixel-module.c

CC      ?= gcc
CFLAGS  ?= $(shell pkg-config --cflags glib-2.0 gobject-2.0 gmodule-2.0) -fPIC -I../../src
LDFLAGS ?= -shared

.PHONY: all clean

all: $(OUTDIR)/$(MODULE_NAME).so

$(OUTDIR)/$(MODULE_NAME).so: $(MODULE_SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -L$(LIBDIR) -lgst $(shell pkg-config --libs glib-2.0 gobject-2.0 gmodule-2.0) -lm

clean:
	rm -f $(OUTDIR)/$(MODULE_NAME).so
