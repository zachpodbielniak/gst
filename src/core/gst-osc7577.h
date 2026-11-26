/*
 * gst-osc7577.h - Program Resume Protocol (OSC 7577)
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A program reports a resume command with OSC 7577. The report is
 * authenticated with OSC7577_TOKEN, stored as one record per terminal,
 * and may be started again after this process is replaced.
 *
 * The token is given only to the first child of the pseudo-terminal.
 * It is not placed in this process's environment.
 */

#ifndef GST_OSC7577_H
#define GST_OSC7577_H

#include <glib.h>
#include "../config/gst-config.h"

G_BEGIN_DECLS

/**
 * GstOsc7577Spawn:
 * @resume: %TRUE when the child should be @argv instead of the login shell
 * @argv: (array zero-terminated=1) (nullable): child argument vector
 * @env_extra: (array zero-terminated=1) (nullable): extra `KEY=VALUE` pairs
 * @child_cwd: (nullable): directory the child starts in
 * @token: (nullable): OSC7577_TOKEN for the first child
 * @cleanup: (nullable): bash or zsh helper to delete if spawn fails
 *
 * Plan for one pseudo-terminal. The token is copied into the child only.
 */
typedef struct _GstOsc7577Spawn {
	gboolean  resume;
	gchar   **argv;
	gchar   **env_extra;
	gchar    *child_cwd;
	gchar    *token;
	gchar    *cleanup;
} GstOsc7577Spawn;

/**
 * gst_osc7577_prepare:
 * @config: configuration, including the program_resume module
 * @shell: (nullable): shell used when a resume runs under a shell
 * @allow_resume: %FALSE when the user asked for an explicit command
 * @out: (out): spawn plan; free with gst_osc7577_spawn_clear()
 *
 * Creates a token when the module is enabled, and may claim one pending
 * record. A record whose owner is still alive is left for that terminal.
 */
void
gst_osc7577_prepare(
	GstConfig        *config,
	const gchar      *shell,
	gboolean          allow_resume,
	GstOsc7577Spawn  *out
);

/**
 * gst_osc7577_spawn_clear:
 * @spawn: plan from gst_osc7577_prepare()
 *
 * Releases the plan and wipes the token bytes.
 */
void
gst_osc7577_spawn_clear(GstOsc7577Spawn *spawn);

/**
 * gst_osc7577_set_pts:
 * @pts: (nullable): slave path of this terminal, such as `/dev/pts/3`
 *
 * Later reports are checked against this device. A %NULL path skips the
 * device check and does not reject a report for that reason.
 */
void
gst_osc7577_set_pts(const gchar *pts);

/**
 * gst_osc7577_on_report:
 * @buf: raw OSC payload, not including the OSC introducer
 * @len: byte length of @buf
 *
 * Handles one OSC payload. OSC 7577 is consumed even when the report is
 * ignored or rejected. Any other payload returns %FALSE.
 *
 * Returns: %TRUE when the payload is OSC 7577
 */
gboolean
gst_osc7577_on_report(
	const gchar *buf,
	gsize        len
);

/**
 * gst_osc7577_get_owner:
 * @pid: (out): owning process id
 * @starttime: (out): `/proc` starttime captured when the report was accepted
 *
 * Returns: %FALSE when there is no owner to watch
 */
gboolean
gst_osc7577_get_owner(
	gint64   *pid,
	guint64  *starttime
);

/**
 * gst_osc7577_note_shutdown:
 *
 * The terminal is going away. An owner that exits around this call keeps
 * its record so a later start can resume it.
 */
void
gst_osc7577_note_shutdown(void);

/**
 * gst_osc7577_is_shutting_down:
 *
 * Returns: %TRUE after gst_osc7577_note_shutdown()
 */
gboolean
gst_osc7577_is_shutting_down(void);

/**
 * gst_osc7577_drop_owner:
 *
 * Deletes the current record because its owner exited while this terminal
 * stayed up. A shutdown in progress keeps the record.
 */
void
gst_osc7577_drop_owner(void);

/**
 * gst_osc7577_launch:
 * @record_path: record file claimed for this start
 * @cleanup_path: (nullable): shell helper file or directory to remove
 *
 * Confirms when the record asks for it, then replaces this process with
 * the recorded command. Returns only when the command is not started.
 *
 * Returns: 0 when the command was declined, 127 when it could not be started
 */
gint
gst_osc7577_launch(
	const gchar *record_path,
	const gchar *cleanup_path
);

#ifndef __GI_SCANNER__
/**
 * gst_osc7577_test_configure:
 * @state_dir: (nullable): directory that holds records for this test
 * @now: unix time used instead of the clock, or 0 for the real clock
 * @proc_checks: %FALSE skips `/proc` owner checks
 *
 * Test hook. Production callers leave the defaults from gst_osc7577_reset().
 */
void
gst_osc7577_test_configure(
	const gchar *state_dir,
	gint64       now,
	gboolean     proc_checks
);

/**
 * gst_osc7577_test_set_token:
 * @token: (nullable): token later reports must mac with
 *
 * Replaces the session token. Production callers use the token from
 * gst_osc7577_prepare() instead.
 */
void
gst_osc7577_test_set_token(const gchar *token);

/**
 * gst_osc7577_test_prompt:
 * @argv: (array zero-terminated=1) (nullable): command to show
 * @cwd: (nullable): directory to show
 *
 * Returns: (transfer full): confirm prompt, with bidi overrides replaced
 */
gchar *
gst_osc7577_test_prompt(
	const gchar *const *argv,
	const gchar        *cwd
);

/**
 * gst_osc7577_reset:
 *
 * Drops in-memory session state. Record files are left on disk.
 */
void
gst_osc7577_reset(void);
#endif

G_END_DECLS

#endif /* GST_OSC7577_H */
