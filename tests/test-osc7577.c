/*
 * test-osc7577.c - Program Resume Protocol (OSC 7577)
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Spec revision 0.5 vectors use the token "dummy-token-replace-me" and
 * the injected clock 1795543200. Confirm is never read from this process's
 * controlling terminal: a declined launch runs in its own session.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "config/gst-config.h"
#include "core/gst-osc7577.h"
#include "core/gst-pty.h"

#define SPEC_NOW    ((gint64)1795543200)
#define SPEC_TOKEN  "dummy-token-replace-me"
#define SPEC_ARGV   "bnZpbQ==,LVM=,L2hvbWUvdXNlci8ubG9jYWwvc3RhdGUvbnZpbS9zZXNzaW9ucy84ZjJjMWQudmlt"
#define SPEC_CLEAR  "pid=74877:time=1795543200:clear=1"
#define SPEC_NVIM   "pid=74877:time=1795543200:argv=" SPEC_ARGV
#define MAC_CLEAR   "c30603369f98fef581a4098be889175b0c74f0641e49f8b25f4326cb38f97c93"
#define MAC_NVIM    "9dbb99bfb856749a87563839d7ce9062818d78cf25909db93f309e0cd3b1909b"

static gchar *
fresh_dir(void)
{
	gchar *tmpl;

	tmpl = g_strdup("/tmp/gst-osc7577-XXXXXX");
	g_assert_nonnull(g_mkdtemp(tmpl));
	g_assert_cmpint(chmod(tmpl, 0700), ==, 0);
	return tmpl;
}

static void
wipe_dir(const gchar *dir)
{
	GDir *handle;
	const gchar *name;

	handle = g_dir_open(dir, 0, NULL);
	if (handle == NULL) {
		return;
	}
	while ((name = g_dir_read_name(handle)) != NULL) {
		gchar *path;

		path = g_build_filename(dir, name, NULL);
		if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
			wipe_dir(path);
		} else {
			unlink(path);
		}
		g_free(path);
	}
	g_dir_close(handle);
	g_rmdir(dir);
}

static GstConfig *
load_cfg(
	gboolean     enabled,
	const gchar *policy,
	const gchar *allow_block
){
	gchar *yaml;
	gchar *path;
	GstConfig *config;
	GError *error;
	gint fd;

	yaml = g_strdup_printf(
		"modules:\n"
		"  program_resume:\n"
		"    enabled: %s\n"
		"    policy: %s\n"
		"    allow_root: false\n"
		"    allow: %s\n",
		enabled ? "true" : "false",
		policy,
		allow_block);
	error = NULL;
	fd = g_file_open_tmp("gst-osc7577-cfg-XXXXXX.yaml", &path, &error);
	g_assert_no_error(error);
	g_assert_cmpint(fd, >=, 0);
	close(fd);
	g_assert_true(g_file_set_contents(path, yaml, -1, &error));
	g_assert_no_error(error);
	config = gst_config_new();
	g_assert_true(gst_config_load_from_path(config, path, &error));
	g_assert_no_error(error);
	unlink(path);
	g_free(path);
	g_free(yaml);
	return config;
}

static void
arm(
	const gchar *dir,
	const gchar *policy,
	const gchar *allow_block,
	gboolean     proc_checks
){
	GstConfig *config;
	GstOsc7577Spawn spawn;

	gst_osc7577_reset();
	gst_osc7577_test_configure(dir, SPEC_NOW, proc_checks);
	config = load_cfg(TRUE, policy, allow_block);
	memset(&spawn, 0, sizeof(spawn));
	gst_osc7577_prepare(config, NULL, FALSE, &spawn);
	g_assert_null(g_getenv("OSC7577_TOKEN"));
	gst_osc7577_test_set_token(SPEC_TOKEN);
	gst_osc7577_spawn_clear(&spawn);
	g_object_unref(config);
}

static gchar *
mac_body(const gchar *prefix)
{
	GChecksum *sum;
	guint8 dig[32];
	gsize dig_len;
	gchar hex[65];
	guint i;

	sum = g_checksum_new(G_CHECKSUM_SHA256);
	g_checksum_update(sum, (const guchar *)prefix, (gssize)strlen(prefix));
	g_checksum_update(sum, (const guchar *)":", 1);
	g_checksum_update(sum, (const guchar *)SPEC_TOKEN,
		(gssize)strlen(SPEC_TOKEN));
	dig_len = sizeof(dig);
	g_checksum_get_digest(sum, dig, &dig_len);
	g_checksum_free(sum);
	for (i = 0; i < 32; i++) {
		g_snprintf(hex + (i * 2), 3, "%02x", dig[i]);
	}
	hex[64] = '\0';
	return g_strdup_printf("%s:mac=%s", prefix, hex);
}

static gboolean
report(const gchar *body)
{
	gchar *payload;
	gboolean ok;

	payload = g_strdup_printf("7577;%s", body);
	ok = gst_osc7577_on_report(payload, strlen(payload));
	g_free(payload);
	return ok;
}

static gchar *
find_rec(const gchar *dir)
{
	GDir *handle;
	const gchar *name;
	gchar *found;

	found = NULL;
	handle = g_dir_open(dir, 0, NULL);
	g_assert_nonnull(handle);
	while ((name = g_dir_read_name(handle)) != NULL) {
		if (!g_str_has_prefix(name, "rec-") ||
		    !g_str_has_suffix(name, ".yaml"))
		{
			continue;
		}
		g_assert_null(found);
		found = g_build_filename(dir, name, NULL);
	}
	g_dir_close(handle);
	return found;
}

static gchar *
b64(const gchar *text)
{
	return g_base64_encode((const guchar *)text, strlen(text));
}

static void
test_config_files(void)
{
	GstConfig *config;
	GError *error;

	error = NULL;
	config = gst_config_new();
	g_assert_true(gst_config_load_from_path(config,
		"data/default-config.yaml", &error));
	g_assert_no_error(error);
	g_assert_false(config->modules.program_resume.enabled);
	g_assert_cmpstr(config->modules.program_resume.policy, ==, "confirm");
	g_assert_false(config->modules.program_resume.allow_root);
	g_object_unref(config);

	config = gst_config_new();
	g_assert_true(gst_config_load_from_path(config,
		"data/all-modules.yaml", &error));
	g_assert_no_error(error);
	g_assert_true(config->modules.program_resume.enabled);
	g_assert_cmpstr(config->modules.program_resume.policy, ==, "confirm");
	g_object_unref(config);

	config = load_cfg(TRUE, "allow", "\n      - nvim\n      - /usr/bin/vim");
	g_assert_true(config->modules.program_resume.enabled);
	g_assert_cmpstr(config->modules.program_resume.policy, ==, "allow");
	g_assert_nonnull(config->modules.program_resume.allow);
	g_assert_cmpstr(config->modules.program_resume.allow[0], ==, "nvim");
	g_assert_cmpstr(config->modules.program_resume.allow[1], ==, "/usr/bin/vim");
	g_assert_null(config->modules.program_resume.allow[2]);
	g_object_unref(config);
}

static void
test_token_not_in_parent(void)
{
	gchar *dir;
	GstConfig *config;
	GstOsc7577Spawn spawn;
	guint i;
	static const gchar alphabet[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";

	dir = fresh_dir();
	gst_osc7577_reset();
	gst_osc7577_test_configure(dir, SPEC_NOW, FALSE);
	config = load_cfg(TRUE, "confirm", "[]");
	memset(&spawn, 0, sizeof(spawn));
	gst_osc7577_prepare(config, NULL, FALSE, &spawn);
	g_assert_nonnull(spawn.token);
	g_assert_cmpuint(strlen(spawn.token), ==, 32);
	for (i = 0; i < 32; i++) {
		g_assert_nonnull(strchr(alphabet, spawn.token[i]));
	}
	g_assert_null(g_getenv("OSC7577_TOKEN"));
	gst_osc7577_spawn_clear(&spawn);
	g_assert_null(spawn.token);
	g_object_unref(config);

	config = load_cfg(FALSE, "confirm", "[]");
	gst_osc7577_reset();
	gst_osc7577_test_configure(dir, SPEC_NOW, FALSE);
	memset(&spawn, 0, sizeof(spawn));
	gst_osc7577_prepare(config, "/bin/bash", TRUE, &spawn);
	g_assert_false(spawn.resume);
	g_assert_null(spawn.token);
	gst_osc7577_spawn_clear(&spawn);
	g_object_unref(config);
	wipe_dir(dir);
	g_free(dir);
}

static void
test_spec_vectors(void)
{
	gchar *dir;
	gchar *body;
	gchar *path;
	gchar *text;
	gint64 pid;
	struct stat st;

	body = mac_body(SPEC_CLEAR);
	g_assert_cmpstr(body, ==, SPEC_CLEAR ":mac=" MAC_CLEAR);
	g_free(body);
	body = mac_body(SPEC_NVIM);
	g_assert_cmpstr(body, ==, SPEC_NVIM ":mac=" MAC_NVIM);
	g_free(body);

	dir = fresh_dir();
	arm(dir, "confirm", "[]", FALSE);
	g_assert_false(gst_osc7577_on_report("0;title", 7));
	g_assert_true(report(SPEC_CLEAR ":mac=" MAC_CLEAR));
	g_assert_null(find_rec(dir));
	g_assert_true(report(SPEC_NVIM ":mac=" MAC_NVIM));
	path = find_rec(dir);
	g_assert_nonnull(path);
	g_assert_cmpint(g_lstat(path, &st), ==, 0);
	g_assert_cmpuint((guint)(st.st_mode & 0777), ==, 0600);
	g_assert_cmpint(g_lstat(dir, &st), ==, 0);
	g_assert_cmpuint((guint)(st.st_mode & 0777), ==, 0700);
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	g_assert_nonnull(strstr(text, "nvim"));
	g_assert_nonnull(strstr(text, "-S"));
	g_assert_nonnull(strstr(text,
		"/home/user/.local/state/nvim/sessions/8f2c1d.vim"));
	g_assert_nonnull(strstr(text, "pending"));
	g_free(text);
	g_assert_true(gst_osc7577_get_owner(&pid, NULL));
	g_assert_cmpint(pid, ==, 74877);

	g_assert_true(report(SPEC_CLEAR ":mac=" MAC_CLEAR));
	g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
	g_assert_false(gst_osc7577_get_owner(&pid, NULL));
	g_free(path);
	wipe_dir(dir);
	g_free(dir);
}

static void
test_ignore_does_not_store(void)
{
	gchar *dir;
	gchar *body;
	gchar *upper;

	dir = fresh_dir();
	arm(dir, "confirm", "[]", FALSE);
	g_assert_true(report("?"));
	body = g_strdup(SPEC_NVIM ":mac=" MAC_NVIM);
	body[strlen(body) - 1] = (body[strlen(body) - 1] == 'b') ? 'c' : 'b';
	g_assert_true(report(body));
	g_free(body);
	g_assert_true(report(SPEC_NVIM));
	upper = g_ascii_strup(MAC_NVIM, -1);
	body = g_strdup_printf(SPEC_NVIM ":mac=%s", upper);
	g_assert_true(report(body));
	g_free(body);
	g_free(upper);
	body = mac_body("pid=74877:time=1795543261:argv=" SPEC_ARGV);
	g_assert_true(report(body));
	g_free(body);
	g_assert_null(find_rec(dir));
	wipe_dir(dir);
	g_free(dir);
}

static void
test_reject_removes_record(void)
{
	gchar *dir;
	gchar *path;
	gchar *body;
	gchar *rel;
	gchar *bad;

	dir = fresh_dir();
	arm(dir, "confirm", "[]", FALSE);
	g_assert_true(report(SPEC_NVIM ":mac=" MAC_NVIM));
	path = find_rec(dir);
	g_assert_nonnull(path);

	body = mac_body("pid=74877:time=1795543199:argv=" SPEC_ARGV);
	g_assert_true(report(body));
	g_free(body);
	g_assert_true(g_file_test(path, G_FILE_TEST_EXISTS));

	rel = b64("./nvim");
	body = g_strdup_printf(
		"pid=74877:time=1795543200:argv=%s", rel);
	bad = mac_body(body);
	g_assert_true(report(bad));
	g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
	g_free(bad);
	g_free(body);
	g_free(rel);
	g_free(path);

	g_assert_true(report(SPEC_NVIM ":mac=" MAC_NVIM));
	path = find_rec(dir);
	g_assert_nonnull(path);
	bad = b64("nv\nim");
	body = g_strdup_printf("pid=74877:time=1795543200:argv=%s", bad);
	g_free(bad);
	bad = mac_body(body);
	g_assert_true(report(bad));
	g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
	g_free(bad);
	g_free(body);
	g_free(path);

	g_assert_true(report(SPEC_NVIM ":mac=" MAC_NVIM));
	path = find_rec(dir);
	body = mac_body(
		"pid=74877:time=1795543200:pid=74877:argv=" SPEC_ARGV);
	g_assert_true(report(body));
	g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
	g_free(body);
	g_free(path);

	g_assert_true(report(SPEC_NVIM ":mac=" MAC_NVIM));
	path = find_rec(dir);
	body = mac_body(
		"pid=74877:time=1795543200:argv=" SPEC_ARGV ":need=later");
	g_assert_true(report(body));
	g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
	g_free(body);
	g_free(path);
	wipe_dir(dir);
	g_free(dir);
}

static void
test_shell_jobs(void)
{
	gchar *dir;
	gchar *text;
	GstConfig *config;
	GstOsc7577Spawn spawn;

	dir = fresh_dir();
	arm(dir, "auto", "[]", FALSE);
	g_assert_true(report(SPEC_NVIM ":mac=" MAC_NVIM));

	gst_osc7577_reset();
	gst_osc7577_test_configure(dir, SPEC_NOW, FALSE);
	config = load_cfg(TRUE, "auto", "[]");
	memset(&spawn, 0, sizeof(spawn));
	gst_osc7577_prepare(config, "/bin/bash", TRUE, &spawn);
	g_assert_true(spawn.resume);
	g_assert_cmpstr(spawn.argv[1], ==, "--rcfile");
	g_assert_true(g_file_get_contents(spawn.argv[2], &text, NULL, NULL));
	g_assert_nonnull(strstr(text, "--osc7577-launch"));
	g_assert_null(strstr(text, "nvim"));
	g_assert_nonnull(spawn.cleanup);
	{
		gchar *base;

		base = g_path_get_basename(spawn.cleanup);
		g_assert_true(g_str_has_prefix(base, "bash-rc-"));
		g_free(base);
	}
	g_free(text);
	gst_osc7577_spawn_clear(&spawn);
	g_object_unref(config);

	gst_osc7577_reset();
	gst_osc7577_test_configure(dir, SPEC_NOW, FALSE);
	config = load_cfg(TRUE, "auto", "[]");
	memset(&spawn, 0, sizeof(spawn));
	gst_osc7577_prepare(config, "/bin/zsh", TRUE, &spawn);
	/* The bash claim already launched the only record. */
	g_assert_false(spawn.resume);
	gst_osc7577_spawn_clear(&spawn);
	g_object_unref(config);
	wipe_dir(dir);
	g_free(dir);

	dir = fresh_dir();
	arm(dir, "auto", "[]", FALSE);
	g_assert_true(report(SPEC_NVIM ":mac=" MAC_NVIM));
	gst_osc7577_reset();
	gst_osc7577_test_configure(dir, SPEC_NOW, FALSE);
	config = load_cfg(TRUE, "auto", "[]");
	memset(&spawn, 0, sizeof(spawn));
	gst_osc7577_prepare(config, "/bin/zsh", TRUE, &spawn);
	g_assert_true(spawn.resume);
	g_assert_true(g_str_has_prefix(spawn.env_extra[0], "ZDOTDIR="));
	g_assert_true(g_file_test(spawn.cleanup, G_FILE_TEST_IS_DIR));
	{
		gchar *rc;
		gchar *script;

		rc = g_build_filename(spawn.cleanup, ".zshrc", NULL);
		g_assert_true(g_file_get_contents(rc, &script, NULL, NULL));
		g_assert_nonnull(strstr(script, "unset ZDOTDIR"));
		g_assert_nonnull(strstr(script, "--osc7577-launch"));
		g_assert_null(strstr(script, "nvim"));
		g_free(script);
		g_free(rc);
	}
	gst_osc7577_spawn_clear(&spawn);
	g_object_unref(config);
	wipe_dir(dir);
	g_free(dir);
}

static void
test_fish_and_direct(void)
{
	gchar *dir;
	GstConfig *config;
	GstOsc7577Spawn spawn;

	dir = fresh_dir();
	arm(dir, "auto", "[]", FALSE);
	g_assert_true(report(SPEC_NVIM ":mac=" MAC_NVIM));
	gst_osc7577_reset();
	gst_osc7577_test_configure(dir, SPEC_NOW, FALSE);
	config = load_cfg(TRUE, "auto", "[]");
	memset(&spawn, 0, sizeof(spawn));
	gst_osc7577_prepare(config, "/usr/bin/fish", TRUE, &spawn);
	g_assert_true(spawn.resume);
	g_assert_cmpstr(spawn.argv[1], ==, "-C");
	g_assert_nonnull(strstr(spawn.argv[2], "--osc7577-launch"));
	g_assert_null(strstr(spawn.argv[2], "nvim"));
	g_assert_null(spawn.cleanup);
	gst_osc7577_spawn_clear(&spawn);
	g_object_unref(config);
	wipe_dir(dir);
	g_free(dir);

	dir = fresh_dir();
	arm(dir, "auto", "[]", FALSE);
	g_assert_true(report(SPEC_NVIM ":mac=" MAC_NVIM));
	gst_osc7577_reset();
	gst_osc7577_test_configure(dir, SPEC_NOW, FALSE);
	config = load_cfg(TRUE, "auto", "[]");
	memset(&spawn, 0, sizeof(spawn));
	gst_osc7577_prepare(config, "/bin/sh", TRUE, &spawn);
	g_assert_true(spawn.resume);
	g_assert_cmpstr(spawn.argv[0], ==, "nvim");
	g_assert_cmpstr(spawn.argv[1], ==, "-S");
	g_assert_null(spawn.cleanup);
	gst_osc7577_spawn_clear(&spawn);
	g_object_unref(config);
	wipe_dir(dir);
	g_free(dir);
}

static void
test_allowlist_and_explicit_cwd(void)
{
	gchar *dir;
	gchar *cwd_b64;
	gchar *prefix;
	gchar *body;
	GstConfig *config;
	GstOsc7577Spawn spawn;

	dir = fresh_dir();
	arm(dir, "confirm", "[]", FALSE);
	g_assert_true(report(SPEC_NVIM ":mac=" MAC_NVIM));
	gst_osc7577_reset();
	gst_osc7577_test_configure(dir, SPEC_NOW, FALSE);
	config = load_cfg(TRUE, "allow", "[]");
	memset(&spawn, 0, sizeof(spawn));
	gst_osc7577_prepare(config, "/bin/sh", TRUE, &spawn);
	g_assert_false(spawn.resume);
	gst_osc7577_spawn_clear(&spawn);
	g_object_unref(config);
	g_assert_nonnull(find_rec(dir));
	wipe_dir(dir);
	g_free(dir);

	dir = fresh_dir();
	arm(dir, "allow", "\n      - nvim", FALSE);
	g_assert_true(report(SPEC_NVIM ":mac=" MAC_NVIM));
	gst_osc7577_reset();
	gst_osc7577_test_configure(dir, SPEC_NOW, FALSE);
	config = load_cfg(TRUE, "allow", "\n      - nvim");
	memset(&spawn, 0, sizeof(spawn));
	gst_osc7577_prepare(config, "/bin/sh", TRUE, &spawn);
	g_assert_true(spawn.resume);
	g_assert_cmpstr(spawn.argv[0], ==, "nvim");
	gst_osc7577_spawn_clear(&spawn);
	g_object_unref(config);
	wipe_dir(dir);
	g_free(dir);

	dir = fresh_dir();
	arm(dir, "auto", "[]", FALSE);
	cwd_b64 = b64("/no/such/osc7577-cwd");
	prefix = g_strdup_printf(
		"pid=74877:time=1795543200:cwd=%s:argv=" SPEC_ARGV, cwd_b64);
	body = mac_body(prefix);
	g_assert_true(report(body));
	gst_osc7577_reset();
	gst_osc7577_test_configure(dir, SPEC_NOW, FALSE);
	config = load_cfg(TRUE, "auto", "[]");
	memset(&spawn, 0, sizeof(spawn));
	gst_osc7577_prepare(config, "/bin/sh", TRUE, &spawn);
	g_assert_false(spawn.resume);
	g_assert_null(find_rec(dir));
	gst_osc7577_spawn_clear(&spawn);
	g_object_unref(config);
	g_free(body);
	g_free(prefix);
	g_free(cwd_b64);
	wipe_dir(dir);
	g_free(dir);
}

static gint
run_launch(const gchar *record, const gchar *cleanup)
{
	pid_t pid;
	int status;

	pid = fork();
	g_assert_cmpint(pid, >=, 0);
	if (pid == 0) {
		if (setsid() < 0) {
			_exit(99);
		}
		_exit(gst_osc7577_launch(record, cleanup));
	}
	g_assert_cmpint(waitpid(pid, &status, 0), ==, (int)pid);
	g_assert_true(WIFEXITED(status));
	return WEXITSTATUS(status);
}

static void
test_launch_confirm_and_exec(void)
{
	gchar *dir;
	gchar *path;
	gchar *marker;
	gchar *touch;
	gchar *prefix;
	gchar *body;
	gchar *b64_touch;
	gchar *b64_marker;
	gchar *helper;
	gchar *text;
	gint status;

	dir = fresh_dir();
	arm(dir, "confirm", "[]", FALSE);
	g_assert_true(report(SPEC_NVIM ":mac=" MAC_NVIM));
	path = find_rec(dir);
	helper = g_build_filename(dir, "bash-rc-keep", NULL);
	g_assert_true(g_file_set_contents(helper, "keep\n", -1, NULL));
	status = run_launch(path, helper);
	g_assert_cmpint(status, ==, 0);
	g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
	g_assert_false(g_file_test(helper, G_FILE_TEST_EXISTS));
	g_free(helper);
	g_free(path);
	wipe_dir(dir);
	g_free(dir);

	touch = g_find_program_in_path("touch");
	g_assert_nonnull(touch);
	dir = fresh_dir();
	marker = g_build_filename(dir, "marker", NULL);
	b64_touch = b64(touch);
	b64_marker = b64(marker);
	prefix = g_strdup_printf(
		"pid=74877:time=1795543200:argv=%s,%s", b64_touch, b64_marker);
	body = mac_body(prefix);
	arm(dir, "auto", "[]", FALSE);
	g_assert_true(report(body));
	path = find_rec(dir);
	status = run_launch(path, NULL);
	g_assert_cmpint(status, ==, 0);
	g_assert_true(g_file_test(marker, G_FILE_TEST_EXISTS));
	g_free(body);
	g_free(prefix);
	g_free(b64_marker);
	g_free(b64_touch);
	g_free(path);
	g_free(marker);

	b64_touch = b64("/no/such/osc7577-bin");
	prefix = g_strdup_printf(
		"pid=74878:time=1795543200:argv=%s", b64_touch);
	body = mac_body(prefix);
	g_free(b64_touch);
	/*
	 * The previous launch did not replace this process. A second report
	 * overwrites the record, then a claim marks it launched before exec.
	 */
	g_assert_true(report(body));
	g_free(body);
	g_free(prefix);
	{
		GstConfig *config;
		GstOsc7577Spawn spawn;

		gst_osc7577_reset();
		gst_osc7577_test_configure(dir, SPEC_NOW, FALSE);
		config = load_cfg(TRUE, "auto", "[]");
		memset(&spawn, 0, sizeof(spawn));
		gst_osc7577_prepare(config, "/bin/sh", TRUE, &spawn);
		g_assert_true(spawn.resume);
		g_assert_true(g_str_has_prefix(spawn.argv[0], "/no/such/osc7577-bin"));
		gst_osc7577_spawn_clear(&spawn);
		g_object_unref(config);
	}
	path = find_rec(dir);
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	g_assert_nonnull(strstr(text, "launched"));
	g_free(text);
	{
		gchar *outsider;

		outsider = g_strdup("/tmp/bash-rc-gst-osc7577-outsider");
		g_assert_true(g_file_set_contents(outsider, "no\n", -1, NULL));
		status = run_launch(path, outsider);
		g_assert_cmpint(status, ==, 127);
		g_assert_true(g_file_test(path, G_FILE_TEST_EXISTS));
		g_assert_true(g_file_test(outsider, G_FILE_TEST_EXISTS));
		unlink(outsider);
		g_free(outsider);
	}
	g_free(path);
	g_free(touch);
	wipe_dir(dir);
	g_free(dir);
}

static void
test_shutdown_keeps_record(void)
{
	gchar *dir;
	gchar *path;

	dir = fresh_dir();
	arm(dir, "confirm", "[]", FALSE);
	g_assert_true(report(SPEC_NVIM ":mac=" MAC_NVIM));
	path = find_rec(dir);
	gst_osc7577_note_shutdown();
	g_assert_true(gst_osc7577_is_shutting_down());
	gst_osc7577_drop_owner();
	g_assert_true(g_file_test(path, G_FILE_TEST_EXISTS));
	g_free(path);
	wipe_dir(dir);
	g_free(dir);

	dir = fresh_dir();
	arm(dir, "confirm", "[]", FALSE);
	g_assert_true(report(SPEC_NVIM ":mac=" MAC_NVIM));
	path = find_rec(dir);
	gst_osc7577_drop_owner();
	g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
	g_free(path);
	wipe_dir(dir);
	g_free(dir);
}

static void
test_prompt_strips_bidi(void)
{
	const gchar *argv[3];
	gchar *prompt;

	argv[0] = "nvim";
	argv[1] = "a\xe2\x80\xae" "b";
	argv[2] = NULL;
	prompt = gst_osc7577_test_prompt(argv, "cwd\xe2\x80\x8e");
	g_assert_null(strstr(prompt, "\xe2\x80\xae"));
	g_assert_null(strstr(prompt, "\xe2\x80\x8e"));
	g_assert_nonnull(strstr(prompt, "[y/N]"));
	g_assert_nonnull(strstr(prompt, "a?b"));
	g_free(prompt);
}

static gboolean
bytes_has(GByteArray *buf, const gchar *needle)
{
	gsize n;
	gsize i;

	n = strlen(needle);
	if (buf->len < n) {
		return FALSE;
	}
	for (i = 0; i + n <= buf->len; i++) {
		if (memcmp(buf->data + i, needle, n) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

static void
on_child_bytes(
	GstPty   *pty,
	gpointer  data,
	gulong    len,
	gpointer  user_data
){
	GByteArray *buf;

	(void)pty;
	buf = user_data;
	g_byte_array_append(buf, (const guint8 *)data, (guint)len);
}

static void
test_pty_child_sees_token(void)
{
	GstPty *pty;
	GByteArray *buf;
	const gchar *env[2];
	const gchar *argv[4];
	GError *error;
	gint64 deadline;
	gchar *text;

	g_assert_null(g_getenv("OSC7577_TOKEN"));
	env[0] = "OSC7577_TOKEN=from-child";
	env[1] = NULL;
	argv[0] = "/bin/sh";
	argv[1] = "-c";
	argv[2] = "printf %s \"$OSC7577_TOKEN\"";
	argv[3] = NULL;
	pty = gst_pty_new();
	gst_pty_set_child_env(pty, env);
	gst_pty_set_child_argv(pty, argv);
	error = NULL;
	if (!gst_pty_spawn(pty, "/bin/sh", NULL, &error)) {
		g_test_skip(error != NULL ? error->message : "pty spawn failed");
		g_clear_error(&error);
		g_object_unref(pty);
		return;
	}
	buf = g_byte_array_new();
	g_signal_connect(pty, "data-received", G_CALLBACK(on_child_bytes), buf);
	deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
	while (g_get_monotonic_time() < deadline) {
		if (bytes_has(buf, "from-child")) {
			break;
		}
		if (!g_main_context_iteration(NULL, FALSE)) {
			g_usleep(1000);
		}
	}
	g_byte_array_append(buf, (const guint8 *)"", 1);
	text = (gchar *)buf->data;
	g_assert_true(bytes_has(buf, "from-child"));
	(void)text;
	g_assert_null(g_getenv("OSC7577_TOKEN"));
	g_byte_array_free(buf, TRUE);
	g_object_unref(pty);
}

static void
test_alive_owner_is_skipped(void)
{
	gchar *dir;
	gchar *tty;
	gchar *prefix;
	gchar *body;
	gchar *path;
	GstConfig *config;
	GstOsc7577Spawn spawn;
	gint64 self_pid;

	tty = ttyname(STDIN_FILENO);
	if (tty == NULL) {
		g_test_skip("no controlling terminal");
		return;
	}
	dir = fresh_dir();
	arm(dir, "auto", "[]", TRUE);
	gst_osc7577_set_pts(tty);
	self_pid = (gint64)getpid();
	prefix = g_strdup_printf(
		"pid=%" G_GINT64_FORMAT ":time=1795543200:argv=" SPEC_ARGV,
		self_pid);
	body = mac_body(prefix);
	g_assert_true(report(body));
	path = find_rec(dir);
	if (path == NULL) {
		g_test_skip("stdin is not this process's controlling tty");
		g_free(body);
		g_free(prefix);
		wipe_dir(dir);
		g_free(dir);
		return;
	}
	gst_osc7577_reset();
	gst_osc7577_test_configure(dir, SPEC_NOW, TRUE);
	gst_osc7577_set_pts(tty);
	config = load_cfg(TRUE, "auto", "[]");
	memset(&spawn, 0, sizeof(spawn));
	gst_osc7577_prepare(config, "/bin/sh", TRUE, &spawn);
	g_assert_false(spawn.resume);
	g_assert_true(g_file_test(path, G_FILE_TEST_EXISTS));
	gst_osc7577_spawn_clear(&spawn);
	g_object_unref(config);
	g_free(path);
	g_free(body);
	g_free(prefix);
	wipe_dir(dir);
	g_free(dir);
}

int
main(int argc, char *argv[])
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/osc7577/config", test_config_files);
	g_test_add_func("/osc7577/token", test_token_not_in_parent);
	g_test_add_func("/osc7577/spec", test_spec_vectors);
	g_test_add_func("/osc7577/ignore", test_ignore_does_not_store);
	g_test_add_func("/osc7577/reject", test_reject_removes_record);
	g_test_add_func("/osc7577/shell", test_shell_jobs);
	g_test_add_func("/osc7577/fish-direct", test_fish_and_direct);
	g_test_add_func("/osc7577/allow-cwd", test_allowlist_and_explicit_cwd);
	g_test_add_func("/osc7577/launch", test_launch_confirm_and_exec);
	g_test_add_func("/osc7577/shutdown", test_shutdown_keeps_record);
	g_test_add_func("/osc7577/bidi", test_prompt_strips_bidi);
	g_test_add_func("/osc7577/pty-token", test_pty_child_sees_token);
	g_test_add_func("/osc7577/alive", test_alive_owner_is_skipped);
	return g_test_run();
}
