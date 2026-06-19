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
#include <termios.h>
#include "core/gst-pty.h"

typedef struct {
	GByteArray  *received;
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

	return g_test_run();
}
