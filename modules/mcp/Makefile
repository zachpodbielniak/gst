# Makefile for MCP server module
#
# Requires MCP=1 and mcp-glib built in deps/mcp-glib.
# Invoked by the top-level Makefile with:
#   OUTDIR, LIBDIR, CFLAGS, LDFLAGS, MCP_CFLAGS, MCP_LDFLAGS

MODULE_NAME := mcp
MODULE_SRCS := gst-mcp-module.c \
               gst-mcp-tools-screen.c \
               gst-mcp-tools-process.c \
               gst-mcp-tools-url.c \
               gst-mcp-tools-config.c \
               gst-mcp-tools-input.c \
               gst-mcp-tools-window.c

CC      ?= gcc
override CFLAGS  += $(MCP_CFLAGS)
override LDFLAGS += $(MCP_LDFLAGS)

.PHONY: all clean

all: $(OUTDIR)/$(MODULE_NAME).so

$(OUTDIR)/$(MODULE_NAME).so: $(MODULE_SRCS)
	$(CC) $(CFLAGS) -shared -fPIC -o $@ $^ -L$(LIBDIR) -lgst $(LDFLAGS) \
		-Wl,-rpath,'$$ORIGIN/../../../deps/mcp-glib/build'

clean:
	rm -f $(OUTDIR)/$(MODULE_NAME).so
