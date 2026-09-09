/*
 * test-keyboard-protocol.c - Negotiated input and graphics invalidation
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <glib.h>
#include <X11/keysym.h>
#include "../src/core/gst-terminal.h"
#include "../src/core/gst-escape-parser.h"

typedef struct {
	GstTerminal *term;
	GString *bytes;
	GString *events;
} Fixture;

/* Capture the explicit length, not strlen: response is a byte transport. */
static void
on_response(GstTerminal *term, const gchar *data, glong len, Fixture *f)
{
	(void)term;
	g_string_append_len(f->bytes, data, len);
}

/* A shared log makes move-before-erase and exact inclusive bounds observable. */
static void
on_erased(GstTerminal *term, gint x1, gint y1, gint x2, gint y2, Fixture *f)
{
	(void)term;
	g_assert_cmpint(x1, >=, 0);
	g_assert_cmpint(y1, >=, 0);
	g_assert_cmpint(x2, >=, x1);
	g_assert_cmpint(y2, >=, y1);
	g_string_append_printf(f->events, "E%d,%d,%d,%d;", x1, y1, x2, y2);
}

static void
on_scrolled(GstTerminal *term, gint top, gint bottom, gint amount, Fixture *f)
{
	(void)term;
	g_string_append_printf(f->events, "S%d,%d,%d;", top, bottom, amount);
}

static void
on_mode(GstTerminal *term, GstTermMode mode, gboolean enabled, Fixture *f)
{
	/* Only screen transitions belong in the graphics log. */
	g_assert_cmpint(gst_terminal_is_altscreen(term), ==,
	    (gst_terminal_get_mode(term) & GST_MODE_ALTSCREEN) != 0);
	if (mode & GST_MODE_ALTSCREEN) {
		g_assert_cmpint(gst_terminal_is_altscreen(term), ==, enabled);
		g_string_append_printf(f->events, "A%d;", enabled);
	}
}

static void
setup(Fixture *f, gconstpointer data)
{
	(void)data;
	f->term = gst_terminal_new(8, 4);
	f->bytes = g_string_new(NULL);
	f->events = g_string_new(NULL);
	g_signal_connect(f->term, "response", G_CALLBACK(on_response), f);
	g_signal_connect(f->term, "region-erased", G_CALLBACK(on_erased), f);
	g_signal_connect(f->term, "region-scrolled", G_CALLBACK(on_scrolled), f);
	g_signal_connect(f->term, "mode-changed", G_CALLBACK(on_mode), f);
}

static void
teardown(Fixture *f, gconstpointer data)
{
	(void)data;
	g_object_unref(f->term);
	g_string_free(f->bytes, TRUE);
	g_string_free(f->events, TRUE);
}

/* Query through the real PTY parser rather than exposing internal state. */
static void
assert_flags(Fixture *f, guint flags)
{
	gchar expected[32];

	g_string_truncate(f->bytes, 0);
	gst_terminal_write(f->term, "\033[?u", -1);
	g_snprintf(expected, sizeof(expected), "\033[?%uu", flags);
	g_assert_cmpstr(f->bytes->str, ==, expected);
	g_string_truncate(f->bytes, 0);
}

static void
test_negotiation(Fixture *f, gconstpointer data)
{
	GstEscapeParser *parser;
	guint i;
	static const gchar *invalid[] = {
		"\033[=1;4u", "\033[=1;2;3u", "\033[>1;2u",
		"\033[<1;2u", "\033[=4294967296u", "\033[=-1u",
		"\033[>1:2u", "\033[?1u", "\033[=1 u"
	};

	(void)data;
	assert_flags(f, 0);
	/* Chunk boundaries must not affect negotiation; the facade owns a ref. */
	parser = gst_escape_parser_new(f->term);
	gst_escape_parser_feed(parser, "\033[>", -1);
	gst_escape_parser_feed(parser, "3u", -1);
	g_object_unref(parser);
	assert_flags(f, 3);
	gst_terminal_write(f->term, "\033[=8;2u", -1);
	assert_flags(f, 11);
	gst_terminal_write(f->term, "\033[=2;3u", -1);
	assert_flags(f, 9);
	gst_terminal_write(f->term, "\033[=31u", -1);
	assert_flags(f, 27);
	for (i = 0; i < G_N_ELEMENTS(invalid); i++) {
		gst_terminal_write(f->term, invalid[i], -1);
		g_assert_cmpuint(f->bytes->len, ==, 0);
		assert_flags(f, 27);
	}
	gst_terminal_write(f->term, "\033[=;u", -1);
	assert_flags(f, 0);
	/* Private CSI u cannot accidentally restore the saved cursor. */
	gst_terminal_write(f->term, "\033[2;3H\033[s\033[4;7H\033[?u", -1);
	g_assert_cmpint(gst_terminal_get_cursor(f->term)->x, ==, 6);
	g_assert_cmpint(gst_terminal_get_cursor(f->term)->y, ==, 3);
	gst_terminal_write(f->term, "\033[u", -1);
	g_assert_cmpint(gst_terminal_get_cursor(f->term)->x, ==, 2);
	g_assert_cmpint(gst_terminal_get_cursor(f->term)->y, ==, 1);
}

static void
test_stacks(Fixture *f, gconstpointer data)
{
	guint i;

	(void)data;
	gst_terminal_write(f->term, "\033[=1u\033[>3u\033[>8u", -1);
	assert_flags(f, 8);
	gst_terminal_write(f->term, "\033[<u", -1);
	assert_flags(f, 3);
	gst_terminal_write(f->term, "\033[?1049h", -1);
	assert_flags(f, 0);
	gst_terminal_write(f->term, "\033[>24u\033[>2u", -1);
	assert_flags(f, 2);
	gst_terminal_write(f->term, "\033[?1049l", -1);
	assert_flags(f, 3);
	gst_terminal_write(f->term, "\033[<u", -1);
	assert_flags(f, 1);
	gst_terminal_swap_screen(f->term);
	gst_terminal_write(f->term, "\033[<u", -1);
	assert_flags(f, 24);
	gst_terminal_swap_screen(f->term);
	assert_flags(f, 1);
	gst_terminal_write(f->term, "\033[<4294967295u", -1);
	assert_flags(f, 0);
	/* Overflow evicts oldest states, retaining the most recent 32. */
	for (i = 0; i < 40; i++) {
		gst_terminal_write(f->term, "\033[>1u", -1);
	}
	gst_terminal_write(f->term, "\033[>8u\033[<32u", -1);
	assert_flags(f, 1);
	gst_terminal_write(f->term, "\033[<u", -1);
	assert_flags(f, 0);
	gst_terminal_write(f->term, "\033[>3u\033[>u", -1);
	assert_flags(f, 0);
	gst_terminal_write(f->term, "\033[<0u", -1);
	assert_flags(f, 3);
	gst_terminal_reset(f->term, FALSE);
	assert_flags(f, 0);
	gst_terminal_swap_screen(f->term);
	assert_flags(f, 0);
}

static void
test_events(Fixture *f, gconstpointer data)
{
	static const struct {
		guint flags, key, state, event;
		const gchar *text;
		gboolean handled;
		const gchar *expected;
	} cases[] = {
		{ 0, XK_Escape, 0, 1, NULL, FALSE, "" },
		{ 0, XK_Up, 0, 2, NULL, FALSE, "" },
		{ 0, XK_a, 0, 3, "a", TRUE, "" },
		{ 1, XK_Escape, 0, 1, NULL, TRUE, "\033[27u" },
		{ 1, XK_a, 0, 1, "a", FALSE, "" },
		{ 1, XK_A, 1, 1, "A", FALSE, "" },
		{ 1, XK_c, 4, 1, "\003", TRUE, "\033[99;5u" },
		{ 1, XK_A, 5, 1, "\001", TRUE, "\033[97;6u" },
		{ 1, XK_bracketleft, 8, 1, "[", TRUE, "\033[91;3u" },
		{ 1, XK_equal, 5, 1, "+", TRUE, "\033[61;6u" },
		{ 1, XK_Return, 0, 1, "\r", FALSE, "" },
		{ 1, XK_Return, 4, 1, "\r", TRUE, "\033[13;5u" },
		{ 3, XK_Return, 0, 3, NULL, TRUE, "" },
		{ 3, XK_Tab, 0, 2, "\t", FALSE, "" },
		{ 3, XK_BackSpace, 2, 3, NULL, TRUE, "" },
		{ 1, XK_ISO_Left_Tab, 0, 1, NULL, TRUE, "\033[9;2u" },
		{ 1, XK_Up, 0, 1, NULL, TRUE, "\033[A" },
		{ 3, XK_Up, 0, 2, NULL, TRUE, "\033[1;1:2A" },
		{ 3, XK_Down, 4, 3, NULL, TRUE, "\033[1;5:3B" },
		{ 1, XK_F1, 0, 1, NULL, TRUE, "\033[P" },
		{ 1, XK_F3, 0, 1, NULL, TRUE, "\033[13~" },
		{ 3, XK_Delete, 1, 3, NULL, TRUE, "\033[3;2:3~" },
		{ 3, XK_F12, 0, 2, NULL, TRUE, "\033[24;1:2~" },
		{ 1, XK_F35, 0, 1, NULL, TRUE, "\033[57398u" },
		{ 1, XK_KP_Enter, 0, 1, NULL, TRUE, "\033[57414u" },
		{ 1, XK_KP_Left, 0, 1, NULL, TRUE, "\033[57417u" },
		{ 1, XK_KP_1, 16, 1, "1", FALSE, "" },
		{ 3, XK_KP_1, 16, 3, NULL, TRUE, "" },
		{ 24, XK_KP_1, 16, 1, "1", TRUE, "\033[57400;129;49u" },
		{ 3, XK_a, 0, 3, NULL, TRUE, "" },
		{ 3, XK_A, 1, 3, NULL, TRUE, "" },
		{ 2, XK_c, 4, 1, "\003", FALSE, "" },
		{ 2, XK_c, 4, 2, "\003", TRUE, "\033[99;5:2u" },
		{ 2, XK_c, 4, 3, NULL, TRUE, "\033[99;5:3u" },
		{ 8, XK_A, 1, 1, "A", TRUE, "\033[97;2u" },
		{ 8, XK_a, 0, 2, "a", TRUE, "\033[97u" },
		{ 8, XK_a, 0, 3, NULL, TRUE, "" },
		{ 10, XK_a, 0, 3, NULL, TRUE, "\033[97;1:3u" },
		{ 10, XK_Return, 0, 3, NULL, TRUE, "\033[13;1:3u" },
		{ 3, XK_Shift_L, 1, 1, NULL, TRUE, "" },
		{ 10, XK_Shift_L, 1, 1, NULL, TRUE, "\033[57441;2u" },
		{ 10, XK_Shift_L, 0, 3, NULL, TRUE, "\033[57441;1:3u" },
		{ 8, XK_a, 255, 1, NULL, TRUE, "\033[97;256u" },
		{ 24, XK_a, 1, 1, "A", TRUE, "\033[97;2;65u" },
		{ 26, XK_a, 0, 2, "a\314\201", TRUE, "\033[97;1:2;97:769u" },
		{ 26, XK_a, 0, 3, "a", TRUE, "\033[97;1:3u" },
		{ 24, 0x0101f600, 0, 1, "\360\237\230\200", TRUE, "\033[128512;1;128512u" },
		{ 24, 0x01000416, 4, 1, NULL, TRUE, "\033[1078;5u" },
		{ 24, 0, 0, 1, "\346\227\245\346\234\254", TRUE, "\033[0;1;26085:26412u" },
		{ 26, 0, 0, 3, "a", TRUE, "" },
		{ 24, XK_a, 4, 1, "\001", TRUE, "\033[97;5u" },
		{ 24, XK_a, 0, 1, "\302\200", TRUE, "\033[97u" },
		{ 24, XK_a, 0, 1, "\xff", TRUE, "\033[97u" },
		{ 16, XK_a, 0, 1, "a", FALSE, "" },
		{ 4, XK_Escape, 0, 1, NULL, FALSE, "" },
		{ 8, 0xffffffffu, 0, 1, NULL, TRUE, "" }
	};
	guint i;
	gchar sequence[32];

	(void)data;
	for (i = 0; i < G_N_ELEMENTS(cases); i++) {
		g_test_message("keyboard case %u", i);
		g_snprintf(sequence, sizeof(sequence), "\033[=%uu", cases[i].flags);
		gst_terminal_write(f->term, sequence, -1);
		g_string_truncate(f->bytes, 0);
		g_assert_cmpint(gst_terminal_key_event(f->term, cases[i].key, 0,
		    cases[i].state, cases[i].event, cases[i].text), ==, cases[i].handled);
		g_assert_cmpstr(f->bytes->str, ==, cases[i].expected);
	}
	/* Nonzero physical codes are reserved, not accidentally sent as Unicode. */
	gst_terminal_write(f->term, "\033[=8u", -1);
	g_string_truncate(f->bytes, 0);
	g_assert_true(gst_terminal_key_event(f->term, XK_a, 38, 0, 1, "a"));
	g_assert_cmpstr(f->bytes->str, ==, "\033[97u");
}

static void
test_legacy_and_text(Fixture *f, gconstpointer data)
{
	gchar buffer[32];
	gint len;
	guint i;
	g_autoptr(GString) text = g_string_new(NULL);
	g_autoptr(GString) expected = g_string_new("\033[0;1;");

	(void)data;
	/* The legacy table does not silently acquire Kitty mode side effects. */
	gst_terminal_set_mode(f->term, GST_MODE_APPCURSOR, TRUE);
	len = gst_terminal_key_to_escape(f->term, XK_Up, 0, buffer, sizeof(buffer));
	buffer[len] = '\0';
	g_assert_cmpstr(buffer, ==, "\033OA");
	gst_terminal_write(f->term, "\033[>1u\033[<u", -1);
	g_assert_false(gst_terminal_key_event(f->term, XK_Up, 0, 0, 1, NULL));
	len = gst_terminal_key_to_escape(f->term, XK_Up, 0, buffer, sizeof(buffer));
	buffer[len] = '\0';
	g_assert_cmpstr(buffer, ==, "\033OA");
	/* Associated text must not be truncated at a fixed escape buffer size. */
	for (i = 0; i < 1024; i++) {
		g_string_append_c(text, 'a');
		g_string_append(expected, i == 0 ? "97" : ":97");
	}
	g_string_append_c(expected, 'u');
	gst_terminal_write(f->term, "\033[=24u", -1);
	g_assert_true(gst_terminal_key_event(f->term, 0, 0, 0, 1, text->str));
	g_assert_cmpstr(f->bytes->str, ==, expected->str);
	g_string_truncate(f->bytes, 0);
	g_string_truncate(f->events, 0);
	gst_terminal_set_mode(f->term, GST_MODE_KBDLOCK, TRUE);
	g_assert_true(gst_terminal_key_event(f->term, XK_a, 0, 0, 1, "a"));
	g_assert_cmpstr(f->bytes->str, ==, "");
	g_assert_cmpstr(f->events->str, ==, "");
}

static void
test_graphics_writes(Fixture *f, gconstpointer data)
{
	(void)data;
	gst_terminal_write(f->term, "a\ra", -1);
	g_assert_cmpstr(f->events->str, ==, "E0,0,0,0;E0,0,0,0;");
	g_string_truncate(f->events, 0);
	gst_terminal_mark_dirty(f->term, -1);
	gst_terminal_clear_dirty(f->term);
	gst_terminal_write(f->term, "\033[2;2H\033[31m\033[s\033[u", -1);
	g_assert_cmpstr(f->events->str, ==, "");
	gst_terminal_write(f->term, "e\314\201", -1);
	g_assert_cmpstr(f->events->str, ==, "E1,1,1,1;E1,1,1,1;");
	gst_terminal_write(f->term, "\033[3;1H\344\270\255", -1);
	g_string_truncate(f->events, 0);
	gst_terminal_write(f->term, "\033[3;2HX", -1);
	g_assert_cmpstr(f->events->str, ==, "E0,2,1,2;");
	gst_terminal_write(f->term, "\033[3;1H\344\270\255", -1);
	g_string_truncate(f->events, 0);
	gst_terminal_clear_region(f->term, 1, 2, 1, 2);
	g_assert_cmpstr(f->events->str, ==, "E0,2,1,2;");
	/* Promotion overwrites the new dummy cell, not just the original scalar. */
	gst_terminal_write(f->term, "\033[4;1H\342\235\244", -1);
	g_string_truncate(f->events, 0);
	gst_terminal_write(f->term, "\357\270\217", -1);
	g_assert_cmpstr(f->events->str, ==, "E0,3,0,3;E0,3,1,3;");
	g_string_truncate(f->events, 0);
	gst_terminal_write(f->term, "\033[4;3H\033[2147483647X", -1);
	g_assert_cmpstr(f->events->str, ==, "E2,3,7,3;");
}

static void
test_graphics_scroll(Fixture *f, gconstpointer data)
{
	(void)data;
	gst_terminal_scroll_up(f->term, 0, 1);
	g_assert_cmpstr(f->events->str, ==, "S0,3,1;E0,3,7,3;");
	g_string_truncate(f->events, 0);
	gst_terminal_scroll_down(f->term, 0, 2);
	g_assert_cmpstr(f->events->str, ==, "S0,3,-2;E0,0,7,0;E0,1,7,1;");
	gst_terminal_set_scroll_region(f->term, 1, 2);
	g_string_truncate(f->events, 0);
	gst_terminal_scroll_up(f->term, 1, G_MAXINT);
	g_assert_cmpstr(f->events->str, ==, "S1,2,2;E0,1,7,1;E0,2,7,2;");
	g_string_truncate(f->events, 0);
	gst_terminal_write(f->term, "\033[2;1H\033[L", -1);
	g_assert_cmpstr(f->events->str, ==, "S1,2,-1;E0,1,7,1;");
	g_string_truncate(f->events, 0);
	gst_terminal_write(f->term, "\033[M", -1);
	g_assert_cmpstr(f->events->str, ==, "S1,2,1;E0,2,7,2;");
	g_string_truncate(f->events, 0);
	gst_terminal_write(f->term, "\033[2;3H\033[@", -1);
	g_assert_cmpstr(f->events->str, ==, "E2,1,7,1;E2,1,2,1;");
	g_string_truncate(f->events, 0);
	gst_terminal_write(f->term, "\033[P", -1);
	g_assert_cmpstr(f->events->str, ==, "E2,1,7,1;E7,1,7,1;");
}

static void
test_screen_signals(Fixture *f, gconstpointer data)
{
	(void)data;
	gst_terminal_swap_screen(f->term);
	gst_terminal_set_mode(f->term, GST_MODE_ALTSCREEN, TRUE);
	gst_terminal_set_mode(f->term, GST_MODE_ALTSCREEN, FALSE);
	gst_terminal_set_mode(f->term, GST_MODE_ALTSCREEN | GST_MODE_INSERT, TRUE);
	g_assert_cmpstr(f->events->str, ==, "A1;A0;A1;");
	g_string_truncate(f->events, 0);
	gst_terminal_reset(f->term, TRUE);
	g_assert_cmpstr(f->events->str, ==, "A0;E0,0,7,3;");
	g_string_truncate(f->events, 0);
	gst_terminal_write(f->term, "\033[?1049h\033[?1049h", -1);
	g_assert_cmpstr(f->events->str, ==,
	    "A1;E0,0,7,0;E0,1,7,1;E0,2,7,2;E0,3,7,3;");
	g_string_truncate(f->events, 0);
	gst_terminal_write(f->term, "\033[?1049l\033[?1049l", -1);
	g_assert_cmpstr(f->events->str, ==,
	    "E0,0,7,0;E0,1,7,1;E0,2,7,2;E0,3,7,3;A0;");
}

/* Exercise deep cluster ownership across REP, scrolling, and cursor saves. */
static void
test_cluster_lifetime(Fixture *f, gconstpointer data)
{
	guint i;
	gchar buffer[7];

	(void)data;
	for (i = 0; i < 64; i++) {
		gst_terminal_reset(f->term, TRUE);
		gst_terminal_write(f->term, "e\314\201\033[s\033[2b\033[u", -1);
		g_assert_cmpstr(gst_glyph_get_text(gst_terminal_get_glyph(f->term, 2, 0), buffer),
		    ==, "e\314\201");
		gst_terminal_scroll_up(f->term, 0, 1);
		gst_terminal_write(f->term, "\033[?1049h\033[?1049l", -1);
	}
}

/**
 * test_legacy_backtab:
 * @f: terminal fixture
 * @data: unused test data
 *
 * XKB reports Shift-Tab as ISO_Left_Tab with Shift still set. Backtab
 * already encodes Shift in its final Z; adding a modifier produces a
 * sequence that intermediate terminal parsers can discard. Exercise both
 * backend keysyms, lock modifiers, and the unmodified Tab control case.
 */
static void
test_legacy_backtab(Fixture *f, gconstpointer data)
{
	static const struct {
		guint keysym;
		guint state;
		const gchar *expected;
	} cases[] = {
		{ XK_ISO_Left_Tab, 0, "\033[Z" },
		{ XK_ISO_Left_Tab, 1, "\033[Z" },
		{ XK_ISO_Left_Tab, 1 | 2 | 16, "\033[Z" },
		{ XK_Tab, 1, "\033[Z" },
		{ XK_Tab, 1 | 2 | 16, "\033[Z" },
		{ XK_Tab, 0, "\t" },
		{ XK_Tab, 2 | 16, "\t" },
		{ XK_Up, 1, "\033[1;2A" }
	};
	guint i;

	(void)data;
	for (i = 0; i < G_N_ELEMENTS(cases); i++) {
		gchar bytes[32] = { 0 };
		gint length;

		length = gst_terminal_key_to_escape(f->term, cases[i].keysym,
		    cases[i].state, bytes, sizeof(bytes));
		g_assert_cmpint(length, ==, (gint)strlen(cases[i].expected));
		g_assert_cmpmem(bytes, (gsize)length, cases[i].expected,
		    strlen(cases[i].expected));
	}
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add("/keyboard/negotiation", Fixture, NULL, setup, test_negotiation, teardown);
	g_test_add("/keyboard/stacks", Fixture, NULL, setup, test_stacks, teardown);
	g_test_add("/keyboard/events", Fixture, NULL, setup, test_events, teardown);
	g_test_add("/keyboard/legacy-backtab", Fixture, NULL, setup, test_legacy_backtab, teardown);
	g_test_add("/keyboard/legacy-and-text", Fixture, NULL, setup, test_legacy_and_text, teardown);
	g_test_add("/graphics/writes", Fixture, NULL, setup, test_graphics_writes, teardown);
	g_test_add("/graphics/scroll", Fixture, NULL, setup, test_graphics_scroll, teardown);
	g_test_add("/graphics/screens", Fixture, NULL, setup, test_screen_signals, teardown);
	g_test_add("/graphics/cluster-lifetime", Fixture, NULL, setup, test_cluster_lifetime, teardown);
	return g_test_run();
}
