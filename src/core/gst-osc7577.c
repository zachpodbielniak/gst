/*
 * gst-osc7577.c - Program Resume Protocol (OSC 7577)
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * One terminal keeps one record. A report is ignored when the mac or the
 * time cannot be trusted. A report that passes those checks and then fails
 * a rule removes that owner's record, because the old command is no longer
 * the one the program asked for.
 *
 * Records are mode 0600 under the state directory (mode 0700). The resume
 * command is executed by argument vector. Shell text is only the fixed
 * helper that reaches the first prompt, never the program's arguments.
 */

#include "gst-osc7577.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define GST_OSC7577_TIME_WINDOW ((gint64)60)
#define GST_OSC7577_MAX_ARGV    (256)
#define GST_OSC7577_TOKEN_LEN   (32)

typedef struct {
	gchar    *state;
	gchar    *policy;
	gint64    pid;
	guint64   starttime;
	gint64    report_time;
	uid_t     uid;
	gchar    *cwd;
	gboolean  explicit_cwd;
	gchar   **argv;
} OscRecord;

typedef struct {
	gboolean   active;
	gchar     *token;
	gchar     *dir;
	gchar     *record_path;
	gchar     *pts;
	gchar     *policy;
	gchar    **allow;
	gboolean   allow_root;
	gboolean   proc_checks;
	gint64     clock;
	gint64     owner_pid;
	guint64    owner_start;
	gboolean   shutting_down;
} OscSession;

static OscSession session = {
	FALSE, NULL, NULL, NULL, NULL, NULL, NULL,
	FALSE, TRUE, 0, 0, 0, FALSE
};

static void record_clear(OscRecord *rec);
static gboolean record_load(const gchar *path, OscRecord *rec);
static gboolean record_store(const gchar *path, const OscRecord *rec);

/* ===== Small helpers ===== */

static void
wipe_string(gchar *text)
{
	volatile gchar *cursor;

	if (text == NULL) {
		return;
	}
	for (cursor = text; *cursor != '\0'; cursor++) {
		*cursor = '\0';
	}
}

static gint64
session_now(void)
{
	if (session.clock != 0) {
		return session.clock;
	}
	return g_get_real_time() / G_USEC_PER_SEC;
}

static gboolean
is_private_owner(const struct stat *st, gboolean directory)
{
	mode_t blocked;

	if (st->st_uid != getuid()) {
		return FALSE;
	}
	/*
	 * A record is as sensitive as a shell startup file. Group and other
	 * access is refused. Directories also refuse group/other write, and
	 * we require them to be mode 0700 so a swap cannot be planted beside
	 * the file.
	 */
	blocked = directory ? (mode_t)077 : (mode_t)077;
	if ((st->st_mode & blocked) != 0) {
		return FALSE;
	}
	if (directory) {
		return S_ISDIR(st->st_mode) ? TRUE : FALSE;
	}
	return S_ISREG(st->st_mode) ? TRUE : FALSE;
}

static gboolean
path_is_private(const gchar *path, gboolean directory)
{
	struct stat st;

	if (path == NULL || g_lstat(path, &st) != 0) {
		return FALSE;
	}
	if (S_ISLNK(st.st_mode)) {
		return FALSE;
	}
	return is_private_owner(&st, directory);
}

static gchar *
default_state_dir(void)
{
	const gchar *env;
	const gchar *state;
	gchar *dir;

	env = g_getenv("GST_OSC7577_STATE_DIR");
	if (env != NULL && env[0] != '\0') {
		return g_strdup(env);
	}
	state = g_get_user_state_dir();
	dir = g_build_filename(state, "gst", "osc7577", NULL);
	return dir;
}

static gboolean
ensure_state_dir(void)
{
	if (session.dir == NULL) {
		session.dir = default_state_dir();
	}
	if (g_mkdir_with_parents(session.dir, 0700) != 0) {
		g_warning("osc7577: cannot create %s: %s",
			session.dir, g_strerror(errno));
		return FALSE;
	}
	if (chmod(session.dir, 0700) != 0) {
		g_warning("osc7577: cannot restrict %s: %s",
			session.dir, g_strerror(errno));
		return FALSE;
	}
	return path_is_private(session.dir, TRUE);
}

static gchar *
make_token(void)
{
	static const gchar alphabet[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";
	guchar raw[GST_OSC7577_TOKEN_LEN];
	gchar *token;
	gint fd;
	gssize got;
	guint i;

	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0) {
		return NULL;
	}
	got = read(fd, raw, sizeof(raw));
	close(fd);
	if (got != (gssize)sizeof(raw)) {
		return NULL;
	}
	token = g_new(gchar, GST_OSC7577_TOKEN_LEN + 1);
	for (i = 0; i < GST_OSC7577_TOKEN_LEN; i++) {
		/* 256 divides evenly by 64, so masking keeps the draw uniform. */
		token[i] = alphabet[raw[i] & 63];
	}
	token[GST_OSC7577_TOKEN_LEN] = '\0';
	return token;
}

static gboolean
ascii_eq(const gchar *got, const gchar *expect)
{
	guchar diff;
	gsize i;
	gsize n;

	n = strlen(expect);
	if (got == NULL || strlen(got) != n) {
		return FALSE;
	}
	diff = 0;
	for (i = 0; i < n; i++) {
		diff |= (guchar)(got[i] ^ expect[i]);
	}
	return diff == 0;
}

static gboolean
mac_matches(
	const gchar *prefix,
	gsize        prefix_len,
	const gchar *token,
	const gchar *mac_hex
){
	GChecksum *sum;
	guint8 dig[32];
	gsize dig_len;
	gchar expect[65];
	guint i;
	gchar sep;
	gboolean ok;

	sum = g_checksum_new(G_CHECKSUM_SHA256);
	if (prefix_len > 0) {
		g_checksum_update(sum, (const guchar *)prefix, prefix_len);
	}
	sep = ':';
	g_checksum_update(sum, (const guchar *)&sep, 1);
	g_checksum_update(sum, (const guchar *)token, strlen(token));
	dig_len = sizeof(dig);
	g_checksum_get_digest(sum, dig, &dig_len);
	g_checksum_free(sum);
	for (i = 0; i < 32; i++) {
		g_snprintf(expect + (i * 2), 3, "%02x", dig[i]);
	}
	expect[64] = '\0';
	ok = ascii_eq(mac_hex, expect);
	return ok;
}

/* ===== Process identity ===== */

static gboolean
parse_proc_stat(
	const gchar *text,
	guint64     *tty_nr,
	guint64     *starttime
){
	const gchar *cursor;
	guint field;
	gchar *end;

	cursor = strrchr(text, ')');
	if (cursor == NULL || cursor[1] != ' ') {
		return FALSE;
	}
	cursor += 2;
	for (field = 1; field <= 20; field++) {
		while (*cursor == ' ') {
			cursor++;
		}
		if (*cursor == '\0') {
			return FALSE;
		}
		if (field == 5) {
			*tty_nr = g_ascii_strtoull(cursor, &end, 10);
		} else if (field == 20) {
			*starttime = g_ascii_strtoull(cursor, &end, 10);
			return end != cursor;
		} else {
			end = (gchar *)cursor;
			while (*end != '\0' && *end != ' ') {
				end++;
			}
		}
		cursor = end;
	}
	return FALSE;
}

/*
 * query_process:
 *
 * Reads the owner of a pid. A missing process, a different user, or a
 * different controlling terminal is a failed check. starttime distinguishes
 * a reused pid from the process that sent the report.
 */
static gboolean
query_process(
	gint64        pid,
	guint64      *starttime,
	gboolean     *alive,
	gboolean     *same_tty
){
	gchar *path;
	gchar *text;
	struct stat proc_st;
	struct stat pts_st;
	guint64 tty_nr;
	guint64 start;
	gboolean ok;

	*alive = FALSE;
	*same_tty = FALSE;
	*starttime = 0;
	if (pid <= 0 || pid > (gint64)G_MAXINT) {
		return FALSE;
	}
	path = g_strdup_printf("/proc/%" G_GINT64_FORMAT, pid);
	ok = (g_lstat(path, &proc_st) == 0);
	g_free(path);
	if (!ok || !S_ISDIR(proc_st.st_mode)) {
		return TRUE;
	}
	if (proc_st.st_uid != getuid()) {
		return FALSE;
	}
	*alive = TRUE;
	path = g_strdup_printf("/proc/%" G_GINT64_FORMAT "/stat", pid);
	ok = g_file_get_contents(path, &text, NULL, NULL);
	g_free(path);
	if (!ok) {
		return FALSE;
	}
	tty_nr = 0;
	start = 0;
	ok = parse_proc_stat(text, &tty_nr, &start);
	g_free(text);
	if (!ok) {
		return FALSE;
	}
	*starttime = start;
	if (session.pts == NULL) {
		return TRUE;
	}
	if (g_lstat(session.pts, &pts_st) != 0 || !S_ISCHR(pts_st.st_mode)) {
		return FALSE;
	}
	*same_tty = ((dev_t)tty_nr == pts_st.st_rdev);
	return TRUE;
}

/* ===== Records ===== */

static void
record_clear(OscRecord *rec)
{
	g_clear_pointer(&rec->state, g_free);
	g_clear_pointer(&rec->policy, g_free);
	g_clear_pointer(&rec->cwd, g_free);
	g_clear_pointer(&rec->argv, g_strfreev);
	rec->pid = 0;
	rec->starttime = 0;
	rec->report_time = 0;
	rec->uid = 0;
	rec->explicit_cwd = FALSE;
}

static gboolean
record_load(const gchar *path, OscRecord *rec)
{
	g_autoptr(YamlParser) parser = NULL;
	YamlNode *root;
	YamlMapping *map;
	YamlSequence *argv;
	const gchar *text;
	guint i;
	guint n;
	gchar *end;

	memset(rec, 0, sizeof(*rec));
	if (!path_is_private(path, FALSE)) {
		return FALSE;
	}
	parser = yaml_parser_new();
	if (!yaml_parser_load_from_file(parser, path, NULL)) {
		return FALSE;
	}
	root = yaml_parser_get_root(parser);
	map = (root != NULL) ? yaml_node_get_mapping(root) : NULL;
	if (map == NULL) {
		return FALSE;
	}
	text = yaml_mapping_get_string_member(map, "state");
	rec->state = g_strdup(text != NULL ? text : "");
	text = yaml_mapping_get_string_member(map, "policy");
	rec->policy = g_strdup(text != NULL ? text : "confirm");
	text = yaml_mapping_get_string_member(map, "pid");
	rec->pid = (text != NULL) ? g_ascii_strtoll(text, &end, 10) : 0;
	text = yaml_mapping_get_string_member(map, "starttime");
	rec->starttime = (text != NULL) ? g_ascii_strtoull(text, &end, 10) : 0;
	text = yaml_mapping_get_string_member(map, "report_time");
	rec->report_time = (text != NULL) ? g_ascii_strtoll(text, &end, 10) : 0;
	text = yaml_mapping_get_string_member(map, "uid");
	rec->uid = (text != NULL) ? (uid_t)g_ascii_strtoull(text, &end, 10) : 0;
	text = yaml_mapping_get_string_member(map, "cwd");
	rec->cwd = g_strdup(text != NULL ? text : "");
	if (yaml_mapping_has_member(map, "explicit_cwd")) {
		rec->explicit_cwd = yaml_mapping_get_boolean_member(
			map, "explicit_cwd");
	}
	argv = yaml_mapping_get_sequence_member(map, "argv");
	if (argv != NULL) {
		n = yaml_sequence_get_length(argv);
		rec->argv = g_new0(gchar *, n + 1);
		for (i = 0; i < n; i++) {
			text = yaml_sequence_get_string_element(argv, i);
			rec->argv[i] = g_strdup(text != NULL ? text : "");
		}
	}
	return rec->state[0] != '\0';
}

static gboolean
record_store(const gchar *path, const OscRecord *rec)
{
	g_autoptr(YamlBuilder) builder = NULL;
	g_autoptr(YamlGenerator) generator = NULL;
	g_autofree gchar *data = NULL;
	g_autofree gchar *pid_text = NULL;
	g_autofree gchar *start_text = NULL;
	g_autofree gchar *time_text = NULL;
	g_autofree gchar *uid_text = NULL;
	YamlNode *root;
	gint fd;
	gsize length;
	gssize wrote;
	guint i;

	builder = yaml_builder_new();
	yaml_builder_begin_mapping(builder);
	yaml_builder_set_member_name(builder, "state");
	yaml_builder_add_string_value(builder,
		rec->state != NULL ? rec->state : "pending");
	yaml_builder_set_member_name(builder, "policy");
	yaml_builder_add_string_value(builder,
		rec->policy != NULL ? rec->policy : "confirm");
	pid_text = g_strdup_printf("%" G_GINT64_FORMAT, rec->pid);
	start_text = g_strdup_printf("%" G_GUINT64_FORMAT, rec->starttime);
	time_text = g_strdup_printf("%" G_GINT64_FORMAT, rec->report_time);
	uid_text = g_strdup_printf("%" G_GUINT64_FORMAT, (guint64)rec->uid);
	yaml_builder_set_member_name(builder, "pid");
	yaml_builder_add_string_value(builder, pid_text);
	yaml_builder_set_member_name(builder, "starttime");
	yaml_builder_add_string_value(builder, start_text);
	yaml_builder_set_member_name(builder, "report_time");
	yaml_builder_add_string_value(builder, time_text);
	yaml_builder_set_member_name(builder, "uid");
	yaml_builder_add_string_value(builder, uid_text);
	yaml_builder_set_member_name(builder, "cwd");
	yaml_builder_add_string_value(builder, rec->cwd != NULL ? rec->cwd : "");
	yaml_builder_set_member_name(builder, "explicit_cwd");
	yaml_builder_add_boolean_value(builder, rec->explicit_cwd);
	yaml_builder_set_member_name(builder, "argv");
	yaml_builder_begin_sequence(builder);
	for (i = 0; rec->argv != NULL && rec->argv[i] != NULL; i++) {
		yaml_builder_add_string_value(builder, rec->argv[i]);
	}
	yaml_builder_end_sequence(builder);
	yaml_builder_end_mapping(builder);

	root = yaml_builder_get_root(builder);
	if (root == NULL) {
		return FALSE;
	}
	generator = yaml_generator_new();
	yaml_generator_set_root(generator, root);
	data = yaml_generator_to_data(generator, &length, NULL);
	if (data == NULL) {
		return FALSE;
	}

	fd = open(path, O_RDWR | O_CREAT, 0600);
	if (fd < 0) {
		return FALSE;
	}
	if (flock(fd, LOCK_EX) != 0 || fchmod(fd, 0600) != 0 ||
	    ftruncate(fd, 0) != 0 || lseek(fd, 0, SEEK_SET) < 0)
	{
		close(fd);
		return FALSE;
	}
	wrote = write(fd, data, length);
	if (wrote != (gssize)length || fsync(fd) != 0) {
		close(fd);
		return FALSE;
	}
	if (flock(fd, LOCK_UN) != 0) {
		close(fd);
		return FALSE;
	}
	close(fd);
	return path_is_private(path, FALSE);
}

static gboolean
owner_still_alive(const OscRecord *rec)
{
	guint64 start;
	gboolean alive;
	gboolean same_tty;

	if (!session.proc_checks || rec->pid <= 0) {
		return FALSE;
	}
	if (!query_process(rec->pid, &start, &alive, &same_tty)) {
		return FALSE;
	}
	if (!alive) {
		return FALSE;
	}
	/* A reused pid is a different process, so the record's owner is gone. */
	if (rec->starttime != 0 && start != rec->starttime) {
		return FALSE;
	}
	return TRUE;
}

static void
forget_record_file(void)
{
	if (session.record_path != NULL) {
		unlink(session.record_path);
	}
	g_clear_pointer(&session.record_path, g_free);
	session.owner_pid = 0;
	session.owner_start = 0;
}

static gboolean
policy_allows(const OscRecord *rec)
{
	gchar *base;
	guint i;
	gboolean ok;

	if (g_strcmp0(session.policy, "allow") != 0) {
		return TRUE;
	}
	if (rec->argv == NULL || rec->argv[0] == NULL || session.allow == NULL) {
		return FALSE;
	}
	base = g_path_get_basename(rec->argv[0]);
	ok = FALSE;
	for (i = 0; session.allow[i] != NULL; i++) {
		if (g_strcmp0(session.allow[i], rec->argv[0]) == 0 ||
		    g_strcmp0(session.allow[i], base) == 0)
		{
			ok = TRUE;
			break;
		}
	}
	g_free(base);
	return ok;
}

static gboolean
claim_record(const gchar *path, OscRecord *rec)
{
	gint fd;
	gboolean ok;

	fd = open(path, O_RDWR);
	memset(rec, 0, sizeof(*rec));
	if (fd < 0) {
		return FALSE;
	}
	if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
		close(fd);
		return FALSE;
	}
	close(fd);
	/*
	 * The lock ends with the fd. record_store() takes it again. Two
	 * terminals can race here; the in-place write still ends as
	 * `launched`, and a second claim sees that state.
	 */
	if (!record_load(path, rec) || g_strcmp0(rec->state, "pending") != 0) {
		return FALSE;
	}
	if (owner_still_alive(rec) || !policy_allows(rec)) {
		return FALSE;
	}
	if (rec->explicit_cwd &&
	    (rec->cwd == NULL || !g_file_test(rec->cwd, G_FILE_TEST_IS_DIR)))
	{
		g_debug("osc7577: dropping record, cwd is gone");
		unlink(path);
		return FALSE;
	}
	g_free(rec->state);
	rec->state = g_strdup("launched");
	g_free(rec->policy);
	rec->policy = g_strdup(session.policy != NULL ? session.policy : "confirm");
	ok = record_store(path, rec);
	return ok;
}

/* ===== Report grammar ===== */

static gboolean
decode_b64(const gchar *text, gchar **out)
{
	g_autofree gchar *padded = NULL;
	gsize in_len;
	gsize out_len;
	guchar *raw;
	guint pad;
	guint i;
	const gchar *cursor;

	if (text == NULL) {
		return FALSE;
	}
	if (text[0] == '\0') {
		*out = g_strdup("");
		return TRUE;
	}
	in_len = strlen(text);
	pad = 0;
	for (cursor = text; *cursor != '\0'; cursor++) {
		if (*cursor == '=') {
			pad++;
			if (pad > 2 || (cursor[1] != '\0' && cursor[1] != '=')) {
				return FALSE;
			}
			continue;
		}
		if (pad != 0) {
			return FALSE;
		}
		if (!g_ascii_isalnum(*cursor) && *cursor != '+' && *cursor != '/') {
			return FALSE;
		}
	}
	if (pad == 0) {
		if ((in_len % 4) == 1) {
			return FALSE;
		}
		pad = (4 - (in_len % 4)) % 4;
		padded = g_strdup_printf("%s%s", text, pad == 2 ? "==" : (pad == 1 ? "=" : ""));
	} else {
		if ((in_len % 4) != 0) {
			return FALSE;
		}
		padded = g_strdup(text);
	}
	raw = g_base64_decode(padded, &out_len);
	if (raw == NULL) {
		return FALSE;
	}
	if (!g_utf8_validate((const gchar *)raw, (gssize)out_len, NULL)) {
		g_free(raw);
		return FALSE;
	}
	for (i = 0; i < out_len; ) {
		gunichar ch;

		ch = g_utf8_get_char_validated((const gchar *)raw + i, out_len - i);
		if (ch == (gunichar)-1 || ch == (gunichar)-2) {
			g_free(raw);
			return FALSE;
		}
		if (ch <= 0x1F || ch == 0x7F || (ch >= 0x80 && ch <= 0x9F)) {
			g_free(raw);
			return FALSE;
		}
		i = (guint)(g_utf8_next_char((const gchar *)raw + i) - (const gchar *)raw);
	}
	*out = g_strndup((const gchar *)raw, out_len);
	g_free(raw);
	return TRUE;
}

static gboolean
valid_command_name(const gchar *name)
{
	if (name == NULL || name[0] == '\0' || name[0] == '-') {
		return FALSE;
	}
	if (g_path_is_absolute(name)) {
		return TRUE;
	}
	if (strchr(name, '/') != NULL) {
		return FALSE;
	}
	return TRUE;
}

static gboolean
parse_u64_token(const gchar *text, gboolean nonzero, guint64 *out)
{
	gchar *end;
	guint64 value;

	if (text == NULL || text[0] < '1' || text[0] > '9') {
		return FALSE;
	}
	value = g_ascii_strtoull(text, &end, 10);
	if (end == text || *end != '\0') {
		return FALSE;
	}
	if (!nonzero && value == 0) {
		return FALSE;
	}
	*out = value;
	return TRUE;
}

typedef struct {
	gint64    pid;
	gint64    report_time;
	gboolean  has_argv;
	gboolean  has_clear;
	gboolean  explicit_cwd;
	gchar    *cwd;
	gchar   **argv;
	gboolean  reject;
} ParsedReport;

static void
parsed_clear(ParsedReport *parsed)
{
	g_clear_pointer(&parsed->cwd, g_free);
	g_clear_pointer(&parsed->argv, g_strfreev);
}

/*
 * parse_prefix:
 *
 * The mac has already been checked. A malformed pair, a repeated key, or a
 * value that does not fit its key rejects the report.
 */
static gboolean
parse_prefix(
	gchar        *prefix,
	ParsedReport *parsed
){
	gchar **pairs;
	GHashTable *seen;
	guint i;
	gboolean ok;

	memset(parsed, 0, sizeof(*parsed));
	seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	pairs = g_strsplit(prefix, ":", -1);
	ok = TRUE;
	for (i = 0; pairs[i] != NULL && ok; i++) {
		gchar *eq;
		const gchar *key;
		const gchar *value;
		guint64 number;

		if (pairs[i][0] == '\0') {
			ok = FALSE;
			break;
		}
		eq = strchr(pairs[i], '=');
		if (eq == NULL || eq == pairs[i]) {
			ok = FALSE;
			break;
		}
		*eq = '\0';
		key = pairs[i];
		value = eq + 1;
		for (eq = (gchar *)key; *eq != '\0'; eq++) {
			if (*eq < 'a' || *eq > 'z') {
				ok = FALSE;
			}
		}
		for (eq = (gchar *)value; *eq != '\0'; eq++) {
			if (!g_ascii_isalnum(*eq) && *eq != '_' && *eq != '.' &&
			    *eq != ',' && *eq != '+' && *eq != '/' &&
			    *eq != '=' && *eq != '-')
			{
				ok = FALSE;
			}
		}
		if (!ok) {
			break;
		}
		if (g_hash_table_contains(seen, key)) {
			ok = FALSE;
			break;
		}
		g_hash_table_add(seen, g_strdup(key));
		if (g_strcmp0(key, "pid") == 0) {
			if (!parse_u64_token(value, TRUE, &number)) {
				ok = FALSE;
			} else {
				parsed->pid = (gint64)number;
			}
		} else if (g_strcmp0(key, "time") == 0) {
			if (!parse_u64_token(value, TRUE, &number)) {
				ok = FALSE;
			} else {
				parsed->report_time = (gint64)number;
			}
		} else if (g_strcmp0(key, "argv") == 0) {
			gchar **parts;
			guint n;
			guint p;

			parts = g_strsplit(value, ",", -1);
			n = g_strv_length(parts);
			if (n == 0 || n > GST_OSC7577_MAX_ARGV ||
			    (value[0] == '\0' && n == 1))
			{
				ok = FALSE;
			} else {
				parsed->argv = g_new0(gchar *, n + 1);
				for (p = 0; parts[p] != NULL && ok; p++) {
					if (!decode_b64(parts[p], &parsed->argv[p])) {
						ok = FALSE;
					}
				}
				parsed->has_argv = ok;
				if (ok && !valid_command_name(parsed->argv[0])) {
					ok = FALSE;
				}
			}
			g_strfreev(parts);
		} else if (g_strcmp0(key, "cwd") == 0) {
			if (!decode_b64(value, &parsed->cwd) ||
			    !g_path_is_absolute(parsed->cwd))
			{
				ok = FALSE;
			} else {
				parsed->explicit_cwd = TRUE;
			}
		} else if (g_strcmp0(key, "clear") == 0) {
			if (g_strcmp0(value, "1") != 0) {
				ok = FALSE;
			} else {
				parsed->has_clear = TRUE;
			}
		} else if (g_strcmp0(key, "need") == 0) {
			gchar **need;
			guint n;

			need = g_strsplit(value, ",", -1);
			for (n = 0; need[n] != NULL && ok; n++) {
				if (g_strcmp0(need[n], "pid") != 0 &&
				    g_strcmp0(need[n], "time") != 0 &&
				    g_strcmp0(need[n], "mac") != 0 &&
				    g_strcmp0(need[n], "argv") != 0 &&
				    g_strcmp0(need[n], "cwd") != 0 &&
				    g_strcmp0(need[n], "clear") != 0 &&
				    g_strcmp0(need[n], "need") != 0)
				{
					ok = FALSE;
				}
			}
			g_strfreev(need);
		} else if (key[0] < 'a' || key[0] > 'z') {
			ok = FALSE;
		}
		/* Unknown keys are ignored. Extension keys use `need`. */
	}
	g_strfreev(pairs);
	g_hash_table_destroy(seen);
	if (!ok) {
		parsed->reject = TRUE;
		return FALSE;
	}
	if (parsed->pid <= 0 || parsed->report_time <= 0) {
		/* Missing time is an ignore. Missing pid cannot name an owner. */
		parsed->reject = (parsed->pid <= 0 && parsed->report_time > 0);
		return FALSE;
	}
	if (parsed->has_argv == parsed->has_clear) {
		parsed->reject = TRUE;
		return FALSE;
	}
	return TRUE;
}

static gboolean
split_mac(
	const gchar  *body,
	gchar       **prefix,
	gchar       **mac
){
	const gchar *mark;
	gsize mac_len;
	guint i;

	if (g_strcmp0(body, "?") == 0) {
		return FALSE;
	}
	mark = g_strrstr(body, ":mac=");
	if (mark == NULL) {
		if (g_str_has_prefix(body, "mac=")) {
			mark = body + 4;
			*prefix = g_strdup("");
		} else {
			return FALSE;
		}
	} else {
		*prefix = g_strndup(body, (gsize)(mark - body));
		mark += 5;
	}
	mac_len = strlen(mark);
	if (mac_len != 64) {
		g_free(*prefix);
		*prefix = NULL;
		return FALSE;
	}
	for (i = 0; i < 64; i++) {
		if (!g_ascii_isxdigit(mark[i]) || g_ascii_isupper(mark[i])) {
			g_free(*prefix);
			*prefix = NULL;
			return FALSE;
		}
	}
	*mac = g_strdup(mark);
	return TRUE;
}

static void
reject_owner(gint64 pid)
{
	OscRecord rec;

	if (pid <= 0 || session.record_path == NULL) {
		return;
	}
	if (!record_load(session.record_path, &rec)) {
		return;
	}
	if (rec.pid == pid) {
		g_debug("osc7577: rejected report for pid %" G_GINT64_FORMAT, pid);
		forget_record_file();
	}
	record_clear(&rec);
}

static gboolean
accept_report(ParsedReport *parsed)
{
	OscRecord rec;
	guint64 start;
	gboolean alive;
	gboolean same_tty;
	gboolean ok;

	if (session.proc_checks) {
		if (session.pts == NULL) {
			g_debug("osc7577: report ignored, pty is not known yet");
			return TRUE;
		}
		if (!query_process(parsed->pid, &start, &alive, &same_tty) ||
		    !alive || !same_tty)
		{
			reject_owner(parsed->pid);
			return TRUE;
		}
	} else {
		start = 0;
	}

	if (!ensure_state_dir()) {
		return TRUE;
	}
	if (session.record_path != NULL && record_load(session.record_path, &rec)) {
		if (rec.pid == parsed->pid && parsed->report_time < rec.report_time) {
			g_debug("osc7577: stale report ignored");
			record_clear(&rec);
			return TRUE;
		}
		record_clear(&rec);
	}

	if (parsed->has_clear) {
		reject_owner(parsed->pid);
		session.owner_pid = 0;
		session.owner_start = 0;
		return TRUE;
	}

	memset(&rec, 0, sizeof(rec));
	rec.state = g_strdup("pending");
	rec.policy = g_strdup(session.policy != NULL ? session.policy : "confirm");
	rec.pid = parsed->pid;
	rec.starttime = start;
	rec.report_time = parsed->report_time;
	rec.uid = getuid();
	rec.explicit_cwd = parsed->explicit_cwd;
	if (parsed->explicit_cwd) {
		rec.cwd = g_steal_pointer(&parsed->cwd);
	} else if (session.proc_checks) {
		gchar *link;
		gchar *target;

		link = g_strdup_printf("/proc/%" G_GINT64_FORMAT "/cwd", parsed->pid);
		target = g_file_read_link(link, NULL);
		g_free(link);
		rec.cwd = target;
	}
	rec.argv = g_steal_pointer(&parsed->argv);

	if (session.record_path == NULL) {
		gchar *name;
		gchar *id;

		id = g_uuid_string_random();
		name = g_strdup_printf("rec-%s.yaml", id);
		g_free(id);
		session.record_path = g_build_filename(session.dir, name, NULL);
		g_free(name);
	}
	ok = record_store(session.record_path, &rec);
	if (ok) {
		session.owner_pid = rec.pid;
		session.owner_start = rec.starttime;
	}
	record_clear(&rec);
	return TRUE;
}

static gboolean
handle_body(const gchar *body)
{
	g_autofree gchar *prefix = NULL;
	g_autofree gchar *mac = NULL;
	ParsedReport parsed;
	gint64 now;
	gint64 skew;

	if (session.token == NULL) {
		return TRUE;
	}
	if (g_strcmp0(body, "?") == 0) {
		return TRUE;
	}
	if (!split_mac(body, &prefix, &mac)) {
		return TRUE;
	}
	if (!mac_matches(prefix, strlen(prefix), session.token, mac)) {
		g_debug("osc7577: mac mismatch, report ignored");
		return TRUE;
	}
	if (!parse_prefix(prefix, &parsed)) {
		if (!parsed.reject) {
			parsed_clear(&parsed);
			return TRUE;
		}
		/* Time is checked below only when the prefix parsed. */
		if (parsed.report_time <= 0) {
			parsed_clear(&parsed);
			return TRUE;
		}
	}
	now = session_now();
	skew = parsed.report_time - now;
	if (skew < 0) {
		skew = -skew;
	}
	if (parsed.report_time <= 0 || skew > GST_OSC7577_TIME_WINDOW) {
		parsed_clear(&parsed);
		return TRUE;
	}
	if (parsed.reject) {
		reject_owner(parsed.pid);
		parsed_clear(&parsed);
		return TRUE;
	}
	accept_report(&parsed);
	parsed_clear(&parsed);
	return TRUE;
}

/* ===== Resume spawn ===== */

static void
sweep_helpers(void)
{
	GDir *dir;
	const gchar *name;
	gint64 now;

	if (session.dir == NULL) {
		return;
	}
	dir = g_dir_open(session.dir, 0, NULL);
	if (dir == NULL) {
		return;
	}
	now = g_get_real_time() / G_USEC_PER_SEC;
	while ((name = g_dir_read_name(dir)) != NULL) {
		gchar *path;
		struct stat st;

		if (!g_str_has_prefix(name, "bash-rc-") &&
		    !g_str_has_prefix(name, "zdot-"))
		{
			continue;
		}
		path = g_build_filename(session.dir, name, NULL);
		if (g_lstat(path, &st) == 0 &&
		    st.st_mtime + (24 * 60 * 60) < now)
		{
			if (S_ISDIR(st.st_mode)) {
				gchar *env;
				gchar *rc;

				env = g_build_filename(path, ".zshenv", NULL);
				rc = g_build_filename(path, ".zshrc", NULL);
				unlink(env);
				unlink(rc);
				g_rmdir(path);
				g_free(env);
				g_free(rc);
			} else {
				unlink(path);
			}
		}
		g_free(path);
	}
	g_dir_close(dir);
}

static gchar *
self_exe(void)
{
	return g_file_read_link("/proc/self/exe", NULL);
}

static gboolean
write_private_file(const gchar *path, const gchar *text)
{
	gint fd;
	gssize wrote;
	gsize length;

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		return FALSE;
	}
	length = strlen(text);
	wrote = write(fd, text, length);
	if (wrote != (gssize)length || fchmod(fd, 0600) != 0) {
		close(fd);
		unlink(path);
		return FALSE;
	}
	close(fd);
	return TRUE;
}

static gboolean
build_shell_job(
	const gchar      *shell,
	const gchar      *record_path,
	GstOsc7577Spawn  *out
){
	g_autofree gchar *base = NULL;
	g_autofree gchar *exe = NULL;
	g_autofree gchar *quoted_exe = NULL;
	g_autofree gchar *quoted_rec = NULL;
	g_autofree gchar *quoted_clean = NULL;
	g_autofree gchar *id = NULL;
	gchar *script;
	gchar *path;

	base = g_path_get_basename(shell);
	exe = self_exe();
	if (exe == NULL) {
		return FALSE;
	}
	quoted_exe = g_shell_quote(exe);
	quoted_rec = g_shell_quote(record_path);
	id = g_uuid_string_random();

	if (g_strcmp0(base, "bash") == 0) {
		path = g_strdup_printf("%s/bash-rc-%s", session.dir, id);
		quoted_clean = g_shell_quote(path);
		script = g_strdup_printf(
			"if [ -r /etc/bash.bashrc ]; then . /etc/bash.bashrc; fi\n"
			"if [ -r \"$HOME/.bashrc\" ]; then . \"$HOME/.bashrc\"; fi\n"
			"__gst_saved_prompt=${PROMPT_COMMAND-}\n"
			"PROMPT_COMMAND=\"command %s --osc7577-launch %s %s; "
			"PROMPT_COMMAND=\\$__gst_saved_prompt; unset __gst_saved_prompt\"\n",
			quoted_exe, quoted_rec, quoted_clean);
		if (!write_private_file(path, script)) {
			g_free(script);
			g_free(path);
			return FALSE;
		}
		g_free(script);
		out->cleanup = g_strdup(path);
		out->argv = g_new0(gchar *, 4);
		out->argv[0] = g_strdup(shell);
		out->argv[1] = g_strdup("--rcfile");
		out->argv[2] = path;
		out->resume = TRUE;
		return TRUE;
	}
	if (g_strcmp0(base, "zsh") == 0) {
		gchar *env_path;
		gchar *rc_path;

		path = g_strdup_printf("%s/zdot-%s", session.dir, id);
		if (g_mkdir_with_parents(path, 0700) != 0 || chmod(path, 0700) != 0) {
			g_free(path);
			return FALSE;
		}
		quoted_clean = g_shell_quote(path);
		env_path = g_build_filename(path, ".zshenv", NULL);
		rc_path = g_build_filename(path, ".zshrc", NULL);
		script = g_strdup(
			"if [ -r \"$HOME/.zshenv\" ]; then . \"$HOME/.zshenv\"; fi\n");
		if (!write_private_file(env_path, script)) {
			g_free(script);
			g_free(env_path);
			g_free(rc_path);
			g_free(path);
			return FALSE;
		}
		g_free(script);
		script = g_strdup_printf(
			"if [ -r \"$HOME/.zshrc\" ]; then . \"$HOME/.zshrc\"; fi\n"
			"autoload -Uz add-zsh-hook\n"
			"function gst-osc7577-precmd {\n"
			"\tadd-zsh-hook -d precmd gst-osc7577-precmd\n"
			"\tunset ZDOTDIR\n"
			"\tcommand %s --osc7577-launch %s %s\n"
			"}\n"
			"add-zsh-hook precmd gst-osc7577-precmd\n",
			quoted_exe, quoted_rec, quoted_clean);
		if (!write_private_file(rc_path, script)) {
			g_free(script);
			g_free(env_path);
			g_free(rc_path);
			g_free(path);
			return FALSE;
		}
		g_free(script);
		g_free(env_path);
		g_free(rc_path);
		out->argv = g_new0(gchar *, 2);
		out->argv[0] = g_strdup(shell);
		out->env_extra = g_new0(gchar *, 2);
		out->env_extra[0] = g_strdup_printf("ZDOTDIR=%s", path);
		out->cleanup = path;
		out->resume = TRUE;
		return TRUE;
	}
	if (g_strcmp0(base, "fish") == 0) {
		gchar *init;

		init = g_strdup_printf(
			"command %s --osc7577-launch %s",
			quoted_exe, quoted_rec);
		out->argv = g_new0(gchar *, 4);
		out->argv[0] = g_strdup(shell);
		out->argv[1] = g_strdup("-C");
		out->argv[2] = init;
		out->resume = TRUE;
		return TRUE;
	}
	return FALSE;
}

static gchar *
pick_pending_record(void)
{
	GDir *dir;
	const gchar *name;
	gchar *best_path;
	gint64 best_time;

	best_path = NULL;
	best_time = 0;
	dir = g_dir_open(session.dir, 0, NULL);
	if (dir == NULL) {
		return NULL;
	}
	while ((name = g_dir_read_name(dir)) != NULL) {
		gchar *path;
		OscRecord rec;

		if (!g_str_has_prefix(name, "rec-") ||
		    !g_str_has_suffix(name, ".yaml"))
		{
			continue;
		}
		path = g_build_filename(session.dir, name, NULL);
		if (!record_load(path, &rec)) {
			g_free(path);
			continue;
		}
		if (g_strcmp0(rec.state, "pending") != 0 || rec.uid != getuid()) {
			record_clear(&rec);
			g_free(path);
			continue;
		}
		if (rec.report_time >= best_time) {
			best_time = rec.report_time;
			g_free(best_path);
			best_path = path;
		} else {
			g_free(path);
		}
		record_clear(&rec);
	}
	g_dir_close(dir);
	return best_path;
}

/* ===== Launch ===== */

static gboolean
cleanup_is_ours(const gchar *record_path, const gchar *cleanup)
{
	g_autofree gchar *dir = NULL;
	g_autofree gchar *base = NULL;

	if (cleanup == NULL) {
		return FALSE;
	}
	dir = g_path_get_dirname(record_path);
	base = g_path_get_basename(cleanup);
	if (!g_str_has_prefix(base, "bash-rc-") &&
	    !g_str_has_prefix(base, "zdot-"))
	{
		return FALSE;
	}
	return g_str_has_prefix(cleanup, dir);
}

static void
remove_cleanup(const gchar *record_path, const gchar *cleanup)
{
	g_autofree gchar *env = NULL;
	g_autofree gchar *rc = NULL;

	if (!cleanup_is_ours(record_path, cleanup)) {
		return;
	}
	if (g_file_test(cleanup, G_FILE_TEST_IS_DIR)) {
		env = g_build_filename(cleanup, ".zshenv", NULL);
		rc = g_build_filename(cleanup, ".zshrc", NULL);
		unlink(env);
		unlink(rc);
		g_rmdir(cleanup);
	} else {
		unlink(cleanup);
	}
}

/*
 * strip_bidi:
 *
 * Replaces bidirectional overrides so a confirm prompt cannot draw the
 * command backwards or hide the question. Other characters stay put.
 */
static gchar *
strip_bidi(const gchar *text)
{
	GString *out;
	const gchar *cursor;

	out = g_string_new(NULL);
	cursor = text;
	while (cursor != NULL && *cursor != '\0') {
		gunichar ch;

		ch = g_utf8_get_char_validated(cursor, -1);
		if (ch == (gunichar)-1 || ch == (gunichar)-2) {
			g_string_append_c(out, '?');
			cursor++;
			continue;
		}
		if ((ch >= 0x202A && ch <= 0x202E) ||
		    (ch >= 0x2066 && ch <= 0x2069) ||
		    ch == 0x200E || ch == 0x200F)
		{
			g_string_append_c(out, '?');
		} else {
			g_string_append_unichar(out, ch);
		}
		cursor = g_utf8_next_char(cursor);
	}
	return g_string_free(out, FALSE);
}

static gchar *
quote_display(const gchar *text)
{
	g_autofree gchar *clean = NULL;

	if (text == NULL) {
		return g_strdup("''");
	}
	clean = strip_bidi(text);
	return g_shell_quote(clean);
}

static gchar *
display_command(const OscRecord *rec)
{
	GString *line;
	guint i;

	line = g_string_new("gst: resume");
	for (i = 0; rec->argv != NULL && rec->argv[i] != NULL && i < 8; i++) {
		g_autofree gchar *quoted = NULL;

		quoted = quote_display(rec->argv[i]);
		g_string_append_c(line, ' ');
		g_string_append(line, quoted);
	}
	if (rec->argv != NULL && rec->argv[8] != NULL) {
		g_string_append(line, " ...");
	}
	if (rec->cwd != NULL && rec->cwd[0] != '\0') {
		g_autofree gchar *quoted = NULL;

		quoted = quote_display(rec->cwd);
		g_string_append(line, " in ");
		g_string_append(line, quoted);
	}
	/*
	 * Keep the question on the line. A very long argv is cut on a UTF-8
	 * boundary before the prompt so the total stays near 500 bytes.
	 */
	if (line->len > 460) {
		g_string_truncate(line, 460);
		while (line->len > 0 &&
		       !g_utf8_validate(line->str, (gssize)line->len, NULL))
		{
			g_string_truncate(line, line->len - 1);
		}
		g_string_append(line, " ...");
	}
	g_string_append(line, "? [y/N] ");
	return g_string_free(line, FALSE);
}

gchar *
gst_osc7577_test_prompt(
	const gchar *const *argv,
	const gchar        *cwd
){
	OscRecord rec;

	memset(&rec, 0, sizeof(rec));
	rec.argv = (gchar **)argv;
	rec.cwd = (gchar *)cwd;
	return display_command(&rec);
}

static gboolean
confirm_on_tty(const OscRecord *rec)
{
	g_autofree gchar *prompt = NULL;
	gchar answer[8];
	gssize got;
	gint fd;

	fd = open("/dev/tty", O_RDWR | O_NOCTTY);
	if (fd < 0) {
		return FALSE;
	}
	prompt = display_command(rec);
	if (write(fd, prompt, strlen(prompt)) < 0) {
		close(fd);
		return FALSE;
	}
	got = read(fd, answer, sizeof(answer) - 1);
	close(fd);
	if (got <= 0) {
		return FALSE;
	}
	answer[got] = '\0';
	return answer[0] == 'y' || answer[0] == 'Y';
}

/* ===== Public API ===== */

void
gst_osc7577_reset(void)
{
	wipe_string(session.token);
	g_clear_pointer(&session.token, g_free);
	g_clear_pointer(&session.dir, g_free);
	g_clear_pointer(&session.record_path, g_free);
	g_clear_pointer(&session.pts, g_free);
	g_clear_pointer(&session.policy, g_free);
	g_clear_pointer(&session.allow, g_strfreev);
	session.active = FALSE;
	session.allow_root = FALSE;
	session.proc_checks = TRUE;
	session.clock = 0;
	session.owner_pid = 0;
	session.owner_start = 0;
	session.shutting_down = FALSE;
}

void
gst_osc7577_test_configure(
	const gchar *state_dir,
	gint64       now,
	gboolean     proc_checks
){
	g_free(session.dir);
	session.dir = g_strdup(state_dir);
	session.clock = now;
	session.proc_checks = proc_checks;
}

void
gst_osc7577_spawn_clear(GstOsc7577Spawn *spawn)
{
	if (spawn == NULL) {
		return;
	}
	wipe_string(spawn->token);
	g_clear_pointer(&spawn->token, g_free);
	g_clear_pointer(&spawn->argv, g_strfreev);
	g_clear_pointer(&spawn->env_extra, g_strfreev);
	g_clear_pointer(&spawn->child_cwd, g_free);
	g_clear_pointer(&spawn->cleanup, g_free);
	spawn->resume = FALSE;
}

void
gst_osc7577_test_set_token(const gchar *token)
{
	wipe_string(session.token);
	g_free(session.token);
	session.token = g_strdup(token);
}

void
gst_osc7577_set_pts(const gchar *pts)
{
	g_free(session.pts);
	session.pts = g_strdup(pts);
}

gboolean
gst_osc7577_get_owner(
	gint64  *pid,
	guint64 *starttime
){
	if (session.owner_pid <= 0) {
		return FALSE;
	}
	if (pid != NULL) {
		*pid = session.owner_pid;
	}
	if (starttime != NULL) {
		*starttime = session.owner_start;
	}
	return TRUE;
}

void
gst_osc7577_note_shutdown(void)
{
	session.shutting_down = TRUE;
}

gboolean
gst_osc7577_is_shutting_down(void)
{
	return session.shutting_down;
}

void
gst_osc7577_drop_owner(void)
{
	if (session.shutting_down) {
		return;
	}
	g_debug("osc7577: owner exited, dropping record");
	forget_record_file();
}

void
gst_osc7577_prepare(
	GstConfig        *config,
	const gchar      *shell,
	gboolean          allow_resume,
	GstOsc7577Spawn  *out
){
	const GstProgramResumeConfig *cfg;
	g_autofree gchar *pending = NULL;
	OscRecord rec;

	g_return_if_fail(out != NULL);
	memset(out, 0, sizeof(*out));
	if (!GST_IS_CONFIG(config)) {
		return;
	}
	cfg = &config->modules.program_resume;
	if (!cfg->enabled) {
		return;
	}
	if (geteuid() == 0 && !cfg->allow_root) {
		g_warning("osc7577: refusing resume while running as root");
		return;
	}
	session.active = TRUE;
	session.allow_root = cfg->allow_root;
	g_free(session.policy);
	if (g_strcmp0(cfg->policy, "allow") == 0 ||
	    g_strcmp0(cfg->policy, "auto") == 0)
	{
		session.policy = g_strdup(cfg->policy);
	} else {
		session.policy = g_strdup("confirm");
	}
	g_strfreev(session.allow);
	session.allow = g_strdupv(cfg->allow);
	wipe_string(session.token);
	g_free(session.token);
	session.token = make_token();
	out->token = g_strdup(session.token);
	if (!ensure_state_dir()) {
		return;
	}
	sweep_helpers();
	if (!allow_resume || shell == NULL) {
		return;
	}
	pending = pick_pending_record();
	if (pending == NULL) {
		return;
	}
	if (!claim_record(pending, &rec)) {
		record_clear(&rec);
		return;
	}
	g_free(session.record_path);
	session.record_path = g_strdup(pending);
	session.owner_pid = 0;
	session.owner_start = 0;
	if (rec.cwd != NULL && rec.cwd[0] != '\0' &&
	    g_file_test(rec.cwd, G_FILE_TEST_IS_DIR))
	{
		out->child_cwd = g_strdup(rec.cwd);
	}
	if (!build_shell_job(shell, pending, out)) {
		/* No shell hook. The command is the first process on the pty. */
		out->argv = g_strdupv(rec.argv);
		out->resume = TRUE;
	}
	record_clear(&rec);
}

gboolean
gst_osc7577_on_report(
	const gchar *buf,
	gsize        len
){
	g_autofree gchar *body = NULL;

	if (buf == NULL || len < 5 || memcmp(buf, "7577;", 5) != 0) {
		return FALSE;
	}
	body = g_strndup(buf + 5, len - 5);
	return handle_body(body);
}

gint
gst_osc7577_launch(
	const gchar *record_path,
	const gchar *cleanup_path
){
	OscRecord rec;
	gchar **argv;
	const gchar *policy;

	if (record_path == NULL || !record_load(record_path, &rec)) {
		return 127;
	}
	remove_cleanup(record_path, cleanup_path);
	if (rec.uid != getuid() || rec.argv == NULL || rec.argv[0] == NULL) {
		record_clear(&rec);
		return 127;
	}
	policy = rec.policy != NULL ? rec.policy : "confirm";
	if (g_strcmp0(policy, "auto") != 0 && g_strcmp0(policy, "allow") != 0) {
		if (!confirm_on_tty(&rec)) {
			unlink(record_path);
			record_clear(&rec);
			return 0;
		}
	}
	if (rec.explicit_cwd &&
	    (rec.cwd == NULL || chdir(rec.cwd) != 0))
	{
		unlink(record_path);
		record_clear(&rec);
		return 0;
	}
	if (!rec.explicit_cwd && rec.cwd != NULL && rec.cwd[0] != '\0') {
		if (chdir(rec.cwd) != 0) {
			g_debug("osc7577: cwd %s is gone, keeping the shell directory",
				rec.cwd);
		}
	}
	argv = g_strdupv(rec.argv);
	record_clear(&rec);
	execvp(argv[0], argv);
	g_printerr("osc7577: cannot start %s: %s\n", argv[0], g_strerror(errno));
	g_strfreev(argv);
	return 127;
}
