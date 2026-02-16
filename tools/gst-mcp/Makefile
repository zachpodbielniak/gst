# Makefile for gst-mcp relay binary
#
# Builds a stdio-to-unix-socket relay that bridges MCP clients
# (like Claude Code) to GST's MCP module socket.
#
# Invoked by the top-level Makefile with OUTDIR set.

CC      ?= gcc
CSTD     = -std=gnu89

PKG_DEPS   = glib-2.0 gio-2.0 gio-unix-2.0
PKG_CFLAGS = $(shell pkg-config --cflags $(PKG_DEPS))
PKG_LIBS   = $(shell pkg-config --libs $(PKG_DEPS))

OUTDIR ?= .

SRC = gst-mcp.c
OUT = $(OUTDIR)/gst-mcp

.PHONY: all clean

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CSTD) -Wall -Wextra -Wno-unused-parameter $(PKG_CFLAGS) \
		-o $@ $(SRC) $(PKG_LIBS)

clean:
	rm -f $(OUT)
