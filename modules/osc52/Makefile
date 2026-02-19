# Makefile for osc52 module

MODULE_NAME := osc52
MODULE_SRC  := gst-osc52-module.c

CC      ?= gcc
CFLAGS  ?= $(shell pkg-config --cflags glib-2.0 gobject-2.0 gmodule-2.0) -fPIC -I../../src
LDFLAGS ?= -shared

.PHONY: all clean

all: $(OUTDIR)/$(MODULE_NAME).so

$(OUTDIR)/$(MODULE_NAME).so: $(MODULE_SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -L$(LIBDIR) -lgst $(shell pkg-config --libs glib-2.0 gobject-2.0 gmodule-2.0)

clean:
	rm -f $(OUTDIR)/$(MODULE_NAME).so
