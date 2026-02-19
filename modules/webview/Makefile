# Makefile for webview module
#
# Requires libsoup-3.0 and json-glib-1.0 for the HTTP/WebSocket server.
# Build with WEBVIEW=1 from the top-level Makefile.
#
# Invoked by the top-level Makefile with:
#   OUTDIR, LIBDIR, CFLAGS, LDFLAGS, WEBVIEW_CFLAGS, WEBVIEW_LDFLAGS

MODULE_NAME := webview
MODULE_SRCS := gst-webview-module.c gst-webview-server.c

CC      ?= gcc
override CFLAGS  += $(WEBVIEW_CFLAGS)
override LDFLAGS += $(WEBVIEW_LDFLAGS)

.PHONY: all clean

all: $(OUTDIR)/$(MODULE_NAME).so

$(OUTDIR)/$(MODULE_NAME).so: $(MODULE_SRCS)
	$(CC) $(CFLAGS) -shared -fPIC -o $@ $^ -L$(LIBDIR) -lgst $(LDFLAGS)

clean:
	rm -f $(OUTDIR)/$(MODULE_NAME).so
