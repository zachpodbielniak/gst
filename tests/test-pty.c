/*
 * test-pty.c - Tests for GstPty
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Regression coverage for the large-paste truncation bug: a write that
 * exceeds the slave tty's input buffer must be fully delivered (queued
 * and drained via the G_IO_OUT watch), not silently dropped on EAGAIN.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <poll.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include "core/gst-pty.h"

typedef struct {
	GByteArray  *received;
	gboolean    child_exited;
	gint        child_status;
} PtyFixture;

static void
on_data_received(
	GstPty      *pty,
	gpointer    data,
	gulong      len,
	gpointer    user_data
){
	PtyFixture *fix = user_data;

	(void)pty;
	g_byte_array_append(fix->received, (const guint8 *)data, (guint)len);
}

/*
 * Spawn `cat` over the PTY and switch the slave line discipline to raw
 * mode so bytes round-trip verbatim (no canonical buffering/echo and no
 * MAX_CANON line-length cap that would mask the behaviour under test).
 * Returns the running GstPty, or NULL (with g_test_skip already called)
 * when no pty/cat is available in the environment.
 */
static GstPty *
spawn_raw_cat(PtyFixture *fix)
{
	GstPty *pty;
	gchar *cat_path;
	GError *err;
	gint fd;
	struct termios tio;

	cat_path = g_find_program_in_path("cat");
	if (cat_path == NULL) {
		g_test_skip("cat not available");
		return NULL;
	}

	pty = gst_pty_new();

	err = NULL;
	if (!gst_pty_spawn(pty, cat_path, NULL, &err)) {
		g_test_skip(err != NULL ? err->message : "pty spawn failed");
		g_clear_error(&err);
		g_free(cat_path);
		g_object_unref(pty);
		return NULL;
	}
	g_free(cat_path);

	/* Raw mode on the slave via the master fd. */
	fd = gst_pty_get_fd(pty);
	if (tcgetattr(fd, &tio) == 0) {
		cfmakeraw(&tio);
		tcsetattr(fd, TCSANOW, &tio);
	}

	g_signal_connect(pty, "data-received",
		G_CALLBACK(on_data_received), fix);

	return pty;
}

/*
 * Pump the main loop until @want bytes have echoed back or a generous
 * deadline elapses (so a regression fails fast rather than hanging CI).
 */
static void
pump_until(PtyFixture *fix, guint want)
{
	gint64 deadline;

	deadline = g_get_monotonic_time() + 15 * G_USEC_PER_SEC;
	while (fix->received->len < want &&
	       g_get_monotonic_time() < deadline) {
		/* Non-blocking iteration; spin with a tiny yield so the
		 * deadline is honoured even if no source is ready. */
		if (!g_main_context_iteration(NULL, FALSE)) {
			g_usleep(1000);
		}
	}
}

/*
 * A write far larger than the ~4 KB tty input buffer must arrive in full
 * and byte-for-byte. Before the fix this truncated at the first EAGAIN.
 */
static void
test_pty_large_write_not_truncated(void)
{
	PtyFixture fix;
	GstPty *pty;
	gsize n;
	guint i;
	gchar *payload;

	fix.received = g_byte_array_new();

	pty = spawn_raw_cat(&fix);
	if (pty == NULL) {
		g_byte_array_unref(fix.received);
		return;
	}

	n = 256 * 1024;
	payload = g_malloc(n);
	for (i = 0; i < n; i++) {
		/* Printable, deterministic, position-dependent pattern. */
		payload[i] = (gchar)('!' + (i % 90));
	}

	gst_pty_write(pty, payload, (gssize)n);

	pump_until(&fix, (guint)n);

	g_assert_cmpuint(fix.received->len, ==, (guint)n);
	g_assert_cmpint(memcmp(fix.received->data, payload, n), ==, 0);

	g_free(payload);
	g_object_unref(pty);
	g_byte_array_unref(fix.received);
}

/*
 * A second write issued while the first is still draining must be
 * appended behind it, preserving byte order.
 */
static void
test_pty_write_order_preserved(void)
{
	PtyFixture fix;
	GstPty *pty;
	gsize n1;
	gsize n2;
	guint i;
	gchar *part1;
	gchar *part2;

	fix.received = g_byte_array_new();

	pty = spawn_raw_cat(&fix);
	if (pty == NULL) {
		g_byte_array_unref(fix.received);
		return;
	}

	n1 = 64 * 1024;  /* large enough to force queueing */
	n2 = 4 * 1024;
	part1 = g_malloc(n1);
	part2 = g_malloc(n2);
	memset(part1, 'A', n1);
	memset(part2, 'Z', n2);

	/* Back-to-back with no loop iteration between: part2 must queue
	 * behind part1's unwritten tail. */
	gst_pty_write(pty, part1, (gssize)n1);
	gst_pty_write(pty, part2, (gssize)n2);

	pump_until(&fix, (guint)(n1 + n2));

	g_assert_cmpuint(fix.received->len, ==, (guint)(n1 + n2));
	for (i = 0; i < n1; i++) {
		g_assert_cmpint(fix.received->data[i], ==, 'A');
	}
	for (i = 0; i < n2; i++) {
		g_assert_cmpint(fix.received->data[n1 + i], ==, 'Z');
	}

	g_free(part1);
	g_free(part2);
	g_object_unref(pty);
	g_byte_array_unref(fix.received);
}

static void
on_tail_child_exited(
	GstPty *pty,
	gint status,
	gpointer user_data
){
	PtyFixture *fix = user_data;

	(void)pty;
	fix->child_exited = TRUE;
	fix->child_status = status;
}

static gboolean
on_tail_timeout(gpointer user_data)
{
	gboolean *timed_out = user_data;

	*timed_out = TRUE;
	return G_SOURCE_CONTINUE;
}

static void
test_pty_exit_preserves_buffered_tail(void)
{
#ifdef SYS_pidfd_open
	const gchar payload[] = "final output before child exit";
	const gchar script[] =
		"#!/bin/sh\n"
		"read gate\n"
		"printf '%s' 'final output before child exit'\n";
	PtyFixture fix;
	GstPty *pty;
	GError *error;
	gchar *script_path;
	struct pollfd exit_poll;
	struct pollfd master_poll;
	gint script_fd;
	gint poll_result;
	gint64 deadline;
	struct termios tio;
	gint64 remaining;
	guint timeout_id;
	gboolean timed_out;

	error = NULL;
	script_path = NULL;
	script_fd = g_file_open_tmp("gst-pty-tail-XXXXXX", &script_path, &error);
	g_assert_no_error(error);
	g_assert_cmpint(script_fd, >=, 0);
	close(script_fd);
	g_assert_true(g_file_set_contents(script_path, script, -1, &error));
	g_assert_no_error(error);
	g_assert_cmpint(g_chmod(script_path, 0700), ==, 0);

	fix.received = g_byte_array_new();
	fix.child_exited = FALSE;
	fix.child_status = 0;
	pty = gst_pty_new();
	g_signal_connect(pty, "data-received",
		G_CALLBACK(on_data_received), &fix);
	g_signal_connect(pty, "child-exited",
		G_CALLBACK(on_tail_child_exited), &fix);
	g_assert_true(gst_pty_spawn(pty, script_path, NULL, &error));
	g_assert_no_error(error);

	/* Wait for exit without dispatching GLib or consuming its wait status.
	 * A pidfd remains readable even if GLib's SIGCHLD worker reaps first. */
	exit_poll.fd = (gint)syscall(SYS_pidfd_open,
		gst_pty_get_child_pid(pty), 0);
	g_assert_cmpint(exit_poll.fd, >=, 0);
	exit_poll.events = POLLIN;
	exit_poll.revents = 0;
	/* Hold the child in read until its pidfd is open, so even GLib
	 * versions that reap from a worker cannot win that race. */
	g_assert_cmpint(tcgetattr(gst_pty_get_fd(pty), &tio), ==, 0);
	tio.c_lflag &= ~(ECHO | ECHONL);
	g_assert_cmpint(tcsetattr(gst_pty_get_fd(pty), TCSANOW, &tio), ==, 0);
	gst_pty_write(pty, "go\n", -1);
	deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
	do {
		remaining = deadline - g_get_monotonic_time();
		g_assert_cmpint(remaining, >, 0);
		poll_result = poll(&exit_poll, 1,
			(gint)((remaining + 999) / 1000));
	} while (poll_result < 0 && errno == EINTR);
	g_assert_cmpint(poll_result, ==, 1);
	g_assert_true((exit_poll.revents & POLLIN) != 0);
	close(exit_poll.fd);
	g_assert_cmpint(g_unlink(script_path), ==, 0);
	g_free(script_path);

	/* Prove the first I/O dispatch will see data and hangup together. */
	master_poll.fd = gst_pty_get_fd(pty);
	master_poll.events = POLLIN;
	master_poll.revents = 0;
	g_assert_cmpint(poll(&master_poll, 1, 0), ==, 1);
	g_assert_true((master_poll.revents & POLLIN) != 0);
	g_assert_true((master_poll.revents & POLLHUP) != 0);

	timed_out = FALSE;
	timeout_id = g_timeout_add_seconds(5, on_tail_timeout, &timed_out);
	while (!timed_out &&
	       (!fix.child_exited || fix.received->len < sizeof(payload) - 1)) {
		g_main_context_iteration(NULL, TRUE);
	}
	g_source_remove(timeout_id);

	g_assert_true(fix.child_exited);
	g_assert_true(WIFEXITED(fix.child_status));
	g_assert_cmpint(WEXITSTATUS(fix.child_status), ==, 0);
	g_assert_cmpuint(fix.received->len, ==, sizeof(payload) - 1);
	g_assert_cmpmem(fix.received->data, fix.received->len,
		payload, sizeof(payload) - 1);

	g_object_unref(pty);
	g_byte_array_unref(fix.received);
#else
	g_test_skip("pidfd_open is required to observe exit without reaping");
#endif
}

int
main(
	int     argc,
	char    **argv
){
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/pty/large-write-not-truncated",
		test_pty_large_write_not_truncated);
	g_test_add_func("/pty/write-order-preserved",
		test_pty_write_order_preserved);
	g_test_add_func("/pty/exit-preserves-buffered-tail",
		test_pty_exit_preserves_buffered_tail);

	return g_test_run();
}
