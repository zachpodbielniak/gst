# rules.mk - GST Build Rules
# Pattern rules and common build recipes

# All source objects depend on the generated version header
$(LIB_OBJS) $(MAIN_OBJ): src/gst-version.h

# Object file compilation
$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/core/%.o: src/core/%.c | $(OBJDIR)
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/rendering/%.o: src/rendering/%.c | $(OBJDIR)
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/window/%.o: src/window/%.c | $(OBJDIR)
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/config/%.o: src/config/%.c | $(OBJDIR)
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/module/%.o: src/module/%.c | $(OBJDIR)
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/selection/%.o: src/selection/%.c | $(OBJDIR)
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/boxed/%.o: src/boxed/%.c | $(OBJDIR)
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/interfaces/%.o: src/interfaces/%.c | $(OBJDIR)
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/util/%.o: src/util/%.c | $(OBJDIR)
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Test compilation
$(OBJDIR)/tests/%.o: tests/%.c | $(OBJDIR)
	@$(MKDIR_P) $(dir $@)
	$(CC) $(TEST_CFLAGS) -c $< -o $@

# Module compilation (generic rule)
$(OUTDIR)/modules/%.so: modules/%/*.c | $(OUTDIR)/modules
	@$(MKDIR_P) $(dir $@)
	$(CC) $(MODULE_CFLAGS) $(MODULE_LDFLAGS) -o $@ $^ $(LDFLAGS) -L$(OUTDIR) -lgst

# yaml-glib dependency compilation
$(OBJDIR)/deps/yaml-glib/src/%.o: deps/yaml-glib/src/%.c | $(OBJDIR)
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Static library creation
$(OUTDIR)/$(LIB_STATIC): $(LIB_OBJS) $(YAMLGLIB_OBJS)
	@$(MKDIR_P) $(dir $@)
	$(AR) rcs $@ $^

# Shared library creation
$(OUTDIR)/$(LIB_SHARED_FULL): $(LIB_OBJS) $(YAMLGLIB_OBJS)
	@$(MKDIR_P) $(dir $@)
	$(CC) $(LDFLAGS_SHARED) -o $@ $^ $(LDFLAGS)
	cd $(OUTDIR) && ln -sf $(LIB_SHARED_FULL) $(LIB_SHARED_MAJOR)
	cd $(OUTDIR) && ln -sf $(LIB_SHARED_MAJOR) $(LIB_SHARED)

# Executable linking
$(OUTDIR)/gst: $(OBJDIR)/main.o $(OUTDIR)/$(LIB_SHARED_FULL)
	$(CC) -o $@ $(OBJDIR)/main.o -L$(OUTDIR) -lgst $(LDFLAGS) -Wl,-rpath,'$$ORIGIN'

# GIR generation
$(OUTDIR)/$(GIR_FILE): $(LIB_SRCS) $(LIB_HDRS) | $(OUTDIR)/$(LIB_SHARED_FULL)
	$(GIR_SCANNER) \
		--namespace=$(GIR_NAMESPACE) \
		--nsversion=$(GIR_VERSION) \
		--library=gst \
		--library-path=$(OUTDIR) \
		--include=GLib-2.0 \
		--include=GObject-2.0 \
		--include=Gio-2.0 \
		--pkg=glib-2.0 \
		--pkg=gobject-2.0 \
		--pkg=gio-2.0 \
		--output=$@ \
		--warn-all \
		-Isrc \
		$(LIB_HDRS) $(LIB_SRCS)

# Typelib compilation
$(OUTDIR)/$(TYPELIB_FILE): $(OUTDIR)/$(GIR_FILE)
	$(GIR_COMPILER) --output=$@ $<

# Directory creation
$(OBJDIR):
	@$(MKDIR_P) $(OBJDIR)
	@$(MKDIR_P) $(OBJDIR)/core
	@$(MKDIR_P) $(OBJDIR)/rendering
	@$(MKDIR_P) $(OBJDIR)/window
	@$(MKDIR_P) $(OBJDIR)/config
	@$(MKDIR_P) $(OBJDIR)/module
	@$(MKDIR_P) $(OBJDIR)/selection
	@$(MKDIR_P) $(OBJDIR)/boxed
	@$(MKDIR_P) $(OBJDIR)/interfaces
	@$(MKDIR_P) $(OBJDIR)/util
	@$(MKDIR_P) $(OBJDIR)/tests
	@$(MKDIR_P) $(OBJDIR)/deps/yaml-glib/src

$(OUTDIR):
	@$(MKDIR_P) $(OUTDIR)

$(OUTDIR)/modules:
	@$(MKDIR_P) $(OUTDIR)/modules

# pkg-config file generation
$(OUTDIR)/gst.pc: gst.pc.in | $(OUTDIR)
	sed \
		-e 's|@PREFIX@|$(PREFIX)|g' \
		-e 's|@LIBDIR@|$(LIBDIR)|g' \
		-e 's|@INCLUDEDIR@|$(INCLUDEDIR)|g' \
		-e 's|@VERSION@|$(VERSION)|g' \
		$< > $@

# Version header generation
src/gst-version.h: src/gst-version.h.in
	sed \
		-e 's|@GST_VERSION_MAJOR@|$(VERSION_MAJOR)|g' \
		-e 's|@GST_VERSION_MINOR@|$(VERSION_MINOR)|g' \
		-e 's|@GST_VERSION_MICRO@|$(VERSION_MICRO)|g' \
		-e 's|@GST_VERSION@|$(VERSION)|g' \
		$< > $@

# Header dependency generation
$(OBJDIR)/%.d: src/%.c | $(OBJDIR)
	@$(MKDIR_P) $(dir $@)
	@$(CC) $(CFLAGS) -MM -MT '$(@:.d=.o)' $< > $@

# Clean rules
.PHONY: clean clean-all
clean:
	rm -rf $(BUILDDIR)/$(BUILD_TYPE)
	rm -f src/gst-version.h

clean-all:
	rm -rf $(BUILDDIR)
	rm -f src/gst-version.h

# Installation rules
.PHONY: install install-lib install-bin install-headers install-pc install-gir install-modules

install: install-lib install-bin install-headers install-pc
ifeq ($(BUILD_GIR),1)
install: install-gir
endif
ifeq ($(BUILD_MODULES),1)
install: install-modules
endif

install-bin: $(OUTDIR)/gst
	$(MKDIR_P) $(DESTDIR)$(BINDIR)
	$(INSTALL_PROGRAM) $(OUTDIR)/gst $(DESTDIR)$(BINDIR)/gst

install-lib: $(OUTDIR)/$(LIB_STATIC) $(OUTDIR)/$(LIB_SHARED_FULL)
	$(MKDIR_P) $(DESTDIR)$(LIBDIR)
	$(INSTALL_DATA) $(OUTDIR)/$(LIB_STATIC) $(DESTDIR)$(LIBDIR)/
	$(INSTALL_DATA) $(OUTDIR)/$(LIB_SHARED_FULL) $(DESTDIR)$(LIBDIR)/
	cd $(DESTDIR)$(LIBDIR) && ln -sf $(LIB_SHARED_FULL) $(LIB_SHARED_MAJOR)
	cd $(DESTDIR)$(LIBDIR) && ln -sf $(LIB_SHARED_MAJOR) $(LIB_SHARED)

install-headers:
	$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/gst
	$(INSTALL_DATA) src/gst.h $(DESTDIR)$(INCLUDEDIR)/gst/
	$(INSTALL_DATA) src/gst-types.h $(DESTDIR)$(INCLUDEDIR)/gst/
	$(INSTALL_DATA) src/gst-enums.h $(DESTDIR)$(INCLUDEDIR)/gst/
	$(INSTALL_DATA) src/gst-version.h $(DESTDIR)$(INCLUDEDIR)/gst/
	$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/gst/core
	$(INSTALL_DATA) src/core/*.h $(DESTDIR)$(INCLUDEDIR)/gst/core/
	$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/gst/boxed
	$(INSTALL_DATA) src/boxed/*.h $(DESTDIR)$(INCLUDEDIR)/gst/boxed/
	$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/gst/config
	$(INSTALL_DATA) src/config/*.h $(DESTDIR)$(INCLUDEDIR)/gst/config/
	$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/gst/module
	$(INSTALL_DATA) src/module/*.h $(DESTDIR)$(INCLUDEDIR)/gst/module/
	$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/gst/rendering
	$(INSTALL_DATA) src/rendering/*.h $(DESTDIR)$(INCLUDEDIR)/gst/rendering/
	$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/gst/window
	$(INSTALL_DATA) src/window/*.h $(DESTDIR)$(INCLUDEDIR)/gst/window/
	$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/gst/selection
	$(INSTALL_DATA) src/selection/*.h $(DESTDIR)$(INCLUDEDIR)/gst/selection/
	$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/gst/interfaces
	$(INSTALL_DATA) src/interfaces/*.h $(DESTDIR)$(INCLUDEDIR)/gst/interfaces/
	$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/gst/util
	$(INSTALL_DATA) src/util/*.h $(DESTDIR)$(INCLUDEDIR)/gst/util/

install-pc: $(OUTDIR)/gst.pc
	$(MKDIR_P) $(DESTDIR)$(PKGCONFIGDIR)
	$(INSTALL_DATA) $(OUTDIR)/gst.pc $(DESTDIR)$(PKGCONFIGDIR)/

install-gir: $(OUTDIR)/$(GIR_FILE) $(OUTDIR)/$(TYPELIB_FILE)
	$(MKDIR_P) $(DESTDIR)$(GIRDIR)
	$(MKDIR_P) $(DESTDIR)$(TYPELIBDIR)
	$(INSTALL_DATA) $(OUTDIR)/$(GIR_FILE) $(DESTDIR)$(GIRDIR)/
	$(INSTALL_DATA) $(OUTDIR)/$(TYPELIB_FILE) $(DESTDIR)$(TYPELIBDIR)/

install-modules:
	$(MKDIR_P) $(DESTDIR)$(MODULEDIR)
	@for mod in $(OUTDIR)/modules/*.so; do \
		if [ -f "$$mod" ]; then \
			$(INSTALL_DATA) "$$mod" $(DESTDIR)$(MODULEDIR)/; \
		fi \
	done

# Uninstall
.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(BINDIR)/gst
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_STATIC)
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_SHARED_FULL)
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_SHARED_MAJOR)
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_SHARED)
	rm -rf $(DESTDIR)$(INCLUDEDIR)/gst
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/gst.pc
	rm -f $(DESTDIR)$(GIRDIR)/$(GIR_FILE)
	rm -f $(DESTDIR)$(TYPELIBDIR)/$(TYPELIB_FILE)
	rm -rf $(DESTDIR)$(MODULEDIR)
