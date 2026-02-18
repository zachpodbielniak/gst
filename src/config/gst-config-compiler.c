/*
 * gst-config-compiler.c - C configuration compiler
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Compiles a user-written C configuration file into a shared object
 * and loads it at runtime.  Uses the crispy library for gcc-based
 * compilation and SHA256 content-hash caching.  The config source
 * may define CRISPY_PARAMS to pass extra compiler flags (e.g.
 * additional pkg-config packages).  The compiled .so must export
 * a `gst_config_init` symbol that is called to apply the
 * configuration.
 */

#include "gst-config-compiler.h"

#define CRISPY_COMPILATION
#include <crispy.h>

#include <glib.h>
#include <gio/gio.h>
#include <gmodule.h>
#include <string.h>

/**
 * GstConfigCompiler:
 *
 * Compiles a user-written C configuration file into a shared object
 * and loads it at runtime.  Uses the crispy library for compilation
 * and SHA256 content-hash caching.  The config source may define
 * CRISPY_PARAMS to pass extra compiler flags.  The compiled .so
 * must export a `gst_config_init` symbol.
 */
struct _GstConfigCompiler {
	GObject              parent_instance;

	CrispyGccCompiler   *compiler;   /* crispy compiler backend */
	CrispyFileCache     *cache;      /* crispy file cache (SHA256) */
};

G_DEFINE_FINAL_TYPE(GstConfigCompiler, gst_config_compiler, G_TYPE_OBJECT)

/* --- GObject lifecycle --- */

static void
gst_config_compiler_finalize(GObject *object)
{
	GstConfigCompiler *self;

	self = GST_CONFIG_COMPILER(object);

	g_clear_object(&self->compiler);
	g_clear_object(&self->cache);

	G_OBJECT_CLASS(gst_config_compiler_parent_class)->finalize(object);
}

/* --- class / instance init --- */

static void
gst_config_compiler_class_init(GstConfigCompilerClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = gst_config_compiler_finalize;
}

static void
gst_config_compiler_init(GstConfigCompiler *self)
{
	/* fields set in _new() after gcc probe */
	self->compiler = NULL;
	self->cache = NULL;
}

/* --- Internal helpers --- */

/**
 * run_pkg_config:
 * @args: arguments to pass to pkg-config (e.g. "--cflags --libs x11")
 * @error: (nullable): return location for a #GError, or %NULL
 *
 * Runs pkg-config with the given arguments and captures its stdout.
 * Trailing whitespace is stripped from the output.
 *
 * Returns: (transfer full): the pkg-config output string, or %NULL on error
 */
static gchar *
run_pkg_config(
	const gchar  *args,
	GError      **error
){
	g_autofree gchar *cmd = NULL;
	g_autofree gchar *stdout_output = NULL;
	g_autofree gchar *stderr_output = NULL;
	gint exit_status;

	cmd = g_strdup_printf("pkg-config %s", args);

	if (!g_spawn_command_line_sync(cmd, &stdout_output, &stderr_output,
	                               &exit_status, error))
		return NULL;

	if (!g_spawn_check_wait_status(exit_status, NULL)) {
		g_set_error(error,
		            G_IO_ERROR,
		            G_IO_ERROR_FAILED,
		            "pkg-config %s failed: %s",
		            args,
		            stderr_output != NULL ? stderr_output : "(no output)");
		return NULL;
	}

	g_strstrip(stdout_output);
	return g_steal_pointer(&stdout_output);
}

/**
 * get_gst_include_flags:
 *
 * Attempts to get gst's include flags via pkg-config.  If gst
 * is not installed, falls back to the compile-time development
 * include path (GST_DEV_INCLUDE_DIR).
 *
 * Returns: (transfer full): include flags string; free with g_free()
 */
static gchar *
get_gst_include_flags(void)
{
	g_autoptr(GError) error = NULL;
	gchar *flags;

	/* try installed gst first */
	flags = run_pkg_config("--cflags gst", &error);
	if (flags != NULL)
		return flags;

#ifdef GST_DEV_INCLUDE_DIR
	/*
	 * fall back to development include paths:
	 * -I<build>/include      for #include <gst/gst.h>
	 * -I<build>/include/gst  for bare includes from sub-headers
	 */
	return g_strdup("-I" GST_DEV_INCLUDE_DIR
	                " -I" GST_DEV_INCLUDE_DIR "/gst");
#else
	return g_strdup("");
#endif
}

/**
 * get_yaml_glib_include_flags:
 *
 * Gets include flags for yaml-glib headers. In development mode,
 * points to deps/yaml-glib/src. When installed, these come via
 * the gst pkg-config flags.
 *
 * Returns: (transfer full): include flags string; free with g_free()
 */
static gchar *
get_yaml_glib_include_flags(void)
{
#ifdef GST_DEV_INCLUDE_DIR
	/*
	 * GST_DEV_INCLUDE_DIR is <builddir>/include.
	 * yaml-glib source is at <project>/deps/yaml-glib/src.
	 * Navigate: <builddir>/include -> <builddir> -> <project>
	 */
	g_autofree gchar *build_dir = NULL;
	g_autofree gchar *project_dir = NULL;
	g_autofree gchar *yaml_dir = NULL;

	build_dir = g_path_get_dirname(GST_DEV_INCLUDE_DIR);
	project_dir = g_path_get_dirname(build_dir);
	yaml_dir = g_build_filename(project_dir, "deps",
	                            "yaml-glib", "src", NULL);

	if (g_file_test(yaml_dir, G_FILE_TEST_IS_DIR)) {
		return g_strdup_printf("-I%s", yaml_dir);
	}
#endif

	return g_strdup("");
}

/**
 * get_crispy_include_flags:
 *
 * Gets include flags for crispy headers.  In development mode,
 * points to deps/crispy/src.  When installed, these come via
 * the gst pkg-config flags.
 *
 * Returns: (transfer full): include flags string; free with g_free()
 */
static gchar *
get_crispy_include_flags(void)
{
#ifdef GST_DEV_INCLUDE_DIR
	g_autofree gchar *build_dir = NULL;
	g_autofree gchar *project_dir = NULL;
	g_autofree gchar *crispy_dir = NULL;

	build_dir = g_path_get_dirname(GST_DEV_INCLUDE_DIR);
	project_dir = g_path_get_dirname(build_dir);
	crispy_dir = g_build_filename(project_dir, "deps",
	                              "crispy", "src", NULL);

	if (g_file_test(crispy_dir, G_FILE_TEST_IS_DIR)) {
		return g_strdup_printf("-I%s", crispy_dir);
	}
#endif

	return g_strdup("");
}

/**
 * get_gst_extra_pkg_flags:
 *
 * Gets the pkg-config flags for dependencies that gst.h needs
 * beyond the base glib/gobject/gio/gmodule that crispy provides.
 *
 * Returns: (transfer full): pkg-config flags string; free with g_free()
 */
static gchar *
get_gst_extra_pkg_flags(void)
{
	g_autoptr(GError) error = NULL;
	gchar *flags;

	flags = run_pkg_config(
		"--cflags --libs x11 xft fontconfig json-glib-1.0", &error);
	if (flags != NULL)
		return flags;

	g_warning("Failed to get GST extra pkg-config flags: %s",
	          error->message);
	return g_strdup("");
}

/**
 * extract_crispy_params:
 * @source_content: the full text of the config source file
 *
 * Scans the source for a line matching `#define CRISPY_PARAMS "..."`
 * and extracts the quoted value portion.
 *
 * Returns: (transfer full): the extracted params string, or an empty
 *          string if no CRISPY_PARAMS define was found
 */
static gchar *
extract_crispy_params(const gchar *source_content)
{
	const gchar *line_start;
	const gchar *pos;
	const gchar *value_start;
	const gchar *line_end;
	gchar *value;

	/* walk through the source looking for the define */
	pos = source_content;
	while (pos != NULL && *pos != '\0') {
		line_start = pos;

		/* find end of this line */
		line_end = strchr(pos, '\n');
		if (line_end == NULL)
			line_end = pos + strlen(pos);

		/* check if this line starts with #define CRISPY_PARAMS */
		if (g_str_has_prefix(line_start, "#define CRISPY_PARAMS")) {
			value_start = line_start + strlen("#define CRISPY_PARAMS");

			/* skip whitespace after the macro name */
			while (value_start < line_end && g_ascii_isspace(*value_start))
				value_start++;

			value = g_strndup(value_start, (gsize)(line_end - value_start));
			g_strstrip(value);

			/* strip surrounding quotes if present */
			if (strlen(value) >= 2 &&
			    value[0] == '"' &&
			    value[strlen(value) - 1] == '"') {
				gchar *unquoted;

				unquoted = g_strndup(value + 1, strlen(value) - 2);
				g_free(value);
				value = unquoted;
			}

			return value;
		}

		/* advance to next line */
		if (*line_end == '\n')
			pos = line_end + 1;
		else
			break;
	}

	return g_strdup("");
}

/**
 * shell_expand:
 * @params: the raw CRISPY_PARAMS value
 * @error: (nullable): return location for a #GError, or %NULL
 *
 * Shell-expands the CRISPY_PARAMS value.  This allows use of
 * $(pkg-config ...) and other shell substitutions.
 *
 * Returns: (transfer full): the expanded string, or %NULL on error
 */
static gchar *
shell_expand(
	const gchar  *params,
	GError      **error
){
	g_autofree gchar *cmd = NULL;
	g_autofree gchar *stdout_output = NULL;
	g_autofree gchar *stderr_output = NULL;
	gint exit_status;

	if (params == NULL || params[0] == '\0')
		return g_strdup("");

	cmd = g_strdup_printf("/bin/sh -c \"printf '%%s' %s\"", params);

	if (!g_spawn_command_line_sync(cmd, &stdout_output, &stderr_output,
	                               &exit_status, error))
		return NULL;

	if (!g_spawn_check_wait_status(exit_status, NULL)) {
		g_set_error(error,
		            G_IO_ERROR,
		            G_IO_ERROR_FAILED,
		            "CRISPY_PARAMS expansion failed: %s",
		            stderr_output != NULL ? stderr_output : "(no output)");
		return NULL;
	}

	g_strstrip(stdout_output);
	return g_steal_pointer(&stdout_output);
}

/* --- Public API --- */

/**
 * gst_config_compiler_new:
 * @error: (nullable): return location for a #GError, or %NULL
 *
 * Creates a new #GstConfigCompiler backed by the crispy library.
 * Probes gcc for its version and caches pkg-config output.
 * Sets up SHA256 content-hash caching in $XDG_CACHE_HOME/gst.
 *
 * Returns: (transfer full) (nullable): a new #GstConfigCompiler,
 *          or %NULL if gcc is not found
 */
GstConfigCompiler *
gst_config_compiler_new(GError **error)
{
	GstConfigCompiler *self;
	g_autofree gchar *cache_dir = NULL;

	self = (GstConfigCompiler *)g_object_new(
		GST_TYPE_CONFIG_COMPILER, NULL);

	/* create crispy gcc compiler (probes gcc, caches base flags) */
	self->compiler = crispy_gcc_compiler_new(error);
	if (self->compiler == NULL) {
		g_object_unref(self);
		return NULL;
	}

	/* create crispy file cache in ~/.cache/gst */
	cache_dir = g_build_filename(g_get_user_cache_dir(), "gst", NULL);
	self->cache = crispy_file_cache_new_with_dir(cache_dir);

	return self;
}

/**
 * gst_config_compiler_find_config:
 * @self: a #GstConfigCompiler
 *
 * Searches standard paths for a C config file.
 *
 * Search order:
 *  1. $XDG_CONFIG_HOME/gst/config.c
 *  2. SYSCONFDIR/gst/config.c
 *  3. DATADIR/gst/config.c
 *  4. ./data/config.c (development fallback)
 *
 * Returns: (transfer full) (nullable): Path to the config.c,
 *          or %NULL if none found
 */
gchar *
gst_config_compiler_find_config(GstConfigCompiler *self)
{
	gchar *path;

	g_return_val_if_fail(GST_IS_CONFIG_COMPILER(self), NULL);

	/* 1. XDG user config */
	path = g_build_filename(g_get_user_config_dir(),
	                        "gst", "config.c", NULL);
	if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
		return path;
	g_free(path);

	/* 2. System config (SYSCONFDIR) */
#ifdef GST_SYSCONFDIR
	path = g_build_filename(GST_SYSCONFDIR, "gst", "config.c", NULL);
	if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
		return path;
	g_free(path);
#endif

	/* 3. Shared data (DATADIR) */
#ifdef GST_DATADIR
	path = g_build_filename(GST_DATADIR, "gst", "config.c", NULL);
	if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
		return path;
	g_free(path);
#endif

	/* 4. Development fallback: ./data/config.c relative to exe */
	{
		g_autofree gchar *exe_path = NULL;
		g_autofree gchar *exe_dir = NULL;

		exe_path = g_file_read_link("/proc/self/exe", NULL);
		if (exe_path != NULL) {
			exe_dir = g_path_get_dirname(exe_path);
			path = g_build_filename(exe_dir, "..", "data",
			                        "config.c", NULL);
			if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
				return path;
			g_free(path);
		}
	}

	return NULL;
}

/**
 * gst_config_compiler_compile:
 * @self: a #GstConfigCompiler
 * @source_path: path to the C configuration source file
 * @force: if %TRUE, bypass cache and force recompilation
 * @error: (nullable): return location for a #GError, or %NULL
 *
 * Reads the source file, scans for an optional CRISPY_PARAMS
 * define, computes a SHA256 content hash, and compiles to a
 * shared object if no valid cached artifact exists (or if
 * @force is %TRUE).
 *
 * Returns: (transfer full) (nullable): path to the compiled .so,
 *          or %NULL on error.  Free with g_free().
 */
gchar *
gst_config_compiler_compile(
	GstConfigCompiler  *self,
	const gchar        *source_path,
	gboolean            force,
	GError            **error
){
	g_autofree gchar *source_content = NULL;
	g_autofree gchar *raw_params = NULL;
	g_autofree gchar *expanded_params = NULL;
	g_autofree gchar *gst_flags = NULL;
	g_autofree gchar *yaml_flags = NULL;
	g_autofree gchar *crispy_flags = NULL;
	g_autofree gchar *extra_pkg_flags = NULL;
	g_autofree gchar *extra_flags = NULL;
	g_autofree gchar *hash = NULL;
	gchar *so_path;
	const gchar *compiler_version;

	g_return_val_if_fail(GST_IS_CONFIG_COMPILER(self), NULL);
	g_return_val_if_fail(source_path != NULL, NULL);

	/* read the source file */
	if (!g_file_get_contents(source_path, &source_content, NULL, error))
		return NULL;

	/* extract optional CRISPY_PARAMS from the source */
	raw_params = extract_crispy_params(source_content);

	/* shell-expand CRISPY_PARAMS (supports $(pkg-config ...) etc.) */
	expanded_params = shell_expand(raw_params, error);
	if (expanded_params == NULL)
		return NULL;

	/* get GST-specific include flags */
	gst_flags = get_gst_include_flags();
	yaml_flags = get_yaml_glib_include_flags();
	crispy_flags = get_crispy_include_flags();
	extra_pkg_flags = get_gst_extra_pkg_flags();

	/* build the combined extra_flags string */
	extra_flags = g_strdup_printf("%s %s %s %s %s",
	                              gst_flags,
	                              yaml_flags,
	                              crispy_flags,
	                              extra_pkg_flags,
	                              expanded_params);

	/* compute content hash for caching */
	compiler_version = crispy_compiler_get_version(
		CRISPY_COMPILER(self->compiler));
	hash = crispy_cache_provider_compute_hash(
		CRISPY_CACHE_PROVIDER(self->cache),
		source_content, -1,
		extra_flags,
		compiler_version);

	/* get the cache path for this hash */
	so_path = crispy_cache_provider_get_path(
		CRISPY_CACHE_PROVIDER(self->cache), hash);

	/* check cache unless force recompilation */
	if (!force &&
	    crispy_cache_provider_has_valid(
	        CRISPY_CACHE_PROVIDER(self->cache),
	        hash, source_path)) {
		g_debug("C config cache hit: %s", so_path);
		return so_path;
	}

	/* compile the source to a shared object */
	g_debug("C config compile: %s -> %s", source_path, so_path);
	if (!crispy_compiler_compile_shared(
	        CRISPY_COMPILER(self->compiler),
	        source_path,
	        so_path,
	        extra_flags,
	        error)) {
		g_free(so_path);
		return NULL;
	}

	return so_path;
}

/**
 * gst_config_compiler_load_and_apply:
 * @self: a #GstConfigCompiler
 * @so_path: path to the compiled shared object
 * @error: (nullable): return location for a #GError, or %NULL
 *
 * Opens the shared object at @so_path, looks up the
 * `gst_config_init` symbol, and calls it.  The init function
 * is expected to have the signature `gboolean gst_config_init(void)`
 * and returns %TRUE on success.
 *
 * Returns: %TRUE if the config was loaded and applied successfully,
 *          %FALSE on error
 */
gboolean
gst_config_compiler_load_and_apply(
	GstConfigCompiler  *self,
	const gchar        *so_path,
	GError            **error
){
	GModule *module;
	gpointer symbol;
	gboolean (*config_init_fn)(void);
	gboolean result;

	g_return_val_if_fail(GST_IS_CONFIG_COMPILER(self), FALSE);
	g_return_val_if_fail(so_path != NULL, FALSE);

	module = g_module_open(so_path, G_MODULE_BIND_LAZY);
	if (module == NULL) {
		g_set_error(error,
		            G_IO_ERROR,
		            G_IO_ERROR_FAILED,
		            "Failed to open module '%s': %s",
		            so_path,
		            g_module_error());
		return FALSE;
	}

	if (!g_module_symbol(module, "gst_config_init", &symbol)) {
		g_set_error(error,
		            G_IO_ERROR,
		            G_IO_ERROR_NOT_FOUND,
		            "Symbol 'gst_config_init' not found in '%s': %s",
		            so_path,
		            g_module_error());
		g_module_close(module);
		return FALSE;
	}

	config_init_fn = (gboolean (*)(void))symbol;
	result = config_init_fn();

	if (!result) {
		g_set_error(error,
		            G_IO_ERROR,
		            G_IO_ERROR_FAILED,
		            "gst_config_init() returned FALSE in '%s'",
		            so_path);
		g_module_close(module);
		return FALSE;
	}

	/* keep the module open so symbols remain available */

	return TRUE;
}
