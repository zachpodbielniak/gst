/*
 * gst-pty.c - GST PTY Management Implementation
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Manages pseudo-terminal allocation, fork/exec of shell,
 * and I/O with the child process via GIOChannel and GMainLoop.
 */

#include "gst-pty.h"
#include <gio/gio.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

/* Read buffer size for PTY data */
#define PTY_READ_BUF_SIZ (8192)

typedef struct {
	gint master_fd;
	GPid child_pid;
	gboolean running;
	GIOChannel *io_channel;
	guint io_watch_id;
	guint child_watch_id;
	gint cols;
	gint rows;
} GstPtyPrivate;

enum {
	SIGNAL_DATA_RECEIVED,
	SIGNAL_CHILD_EXITED,
	N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE(GstPty, gst_pty, G_TYPE_OBJECT)

/* Forward declarations */
static gboolean pty_io_callback(GIOChannel *source, GIOCondition condition,
                                gpointer user_data);
static void pty_child_watch(GPid pid, gint status, gpointer user_data);

static void
gst_pty_finalize(GObject *object)
{
	GstPty *pty = GST_PTY(object);
	GstPtyPrivate *priv = gst_pty_get_instance_private(pty);

	/* Remove watches first, checking they still exist in the context */
	if (priv->io_watch_id > 0) {
		if (g_main_context_find_source_by_id(NULL, priv->io_watch_id) != NULL) {
			g_source_remove(priv->io_watch_id);
		}
		priv->io_watch_id = 0;
	}

	if (priv->child_watch_id > 0) {
		if (g_main_context_find_source_by_id(NULL, priv->child_watch_id) != NULL) {
			g_source_remove(priv->child_watch_id);
		}
		priv->child_watch_id = 0;
	}

	if (priv->io_channel != NULL) {
		g_io_channel_unref(priv->io_channel);
		priv->io_channel = NULL;
	}

	if (priv->master_fd >= 0) {
		close(priv->master_fd);
		priv->master_fd = -1;
	}

	G_OBJECT_CLASS(gst_pty_parent_class)->finalize(object);
}

static void
gst_pty_class_init(GstPtyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = gst_pty_finalize;

	/**
	 * GstPty::data-received:
	 * @pty: the PTY
	 * @data: the data received
	 * @len: length of the data
	 *
	 * Emitted when data is received from the child process.
	 */
	signals[SIGNAL_DATA_RECEIVED] = g_signal_new(
	    "data-received",
	    G_TYPE_FROM_CLASS(klass),
	    G_SIGNAL_RUN_LAST,
	    G_STRUCT_OFFSET(GstPtyClass, data_received),
	    NULL, NULL, NULL,
	    G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_ULONG);

	/**
	 * GstPty::child-exited:
	 * @pty: the PTY
	 * @status: exit status of the child
	 *
	 * Emitted when the child process exits.
	 */
	signals[SIGNAL_CHILD_EXITED] = g_signal_new(
	    "child-exited",
	    G_TYPE_FROM_CLASS(klass),
	    G_SIGNAL_RUN_LAST,
	    G_STRUCT_OFFSET(GstPtyClass, child_exited),
	    NULL, NULL, NULL,
	    G_TYPE_NONE, 1, G_TYPE_INT);
}

static void
gst_pty_init(GstPty *pty)
{
	GstPtyPrivate *priv = gst_pty_get_instance_private(pty);

	priv->master_fd = -1;
	priv->child_pid = 0;
	priv->running = FALSE;
	priv->io_channel = NULL;
	priv->io_watch_id = 0;
	priv->child_watch_id = 0;
	priv->cols = 80;
	priv->rows = 24;
}

/*
 * pty_io_callback:
 *
 * GIOChannel watch callback. Reads data from the PTY master fd
 * and emits the "data-received" signal.
 */
static gboolean
pty_io_callback(
    GIOChannel      *source,
    GIOCondition    condition,
    gpointer        user_data
){
	GstPty *pty = GST_PTY(user_data);
	GstPtyPrivate *priv = gst_pty_get_instance_private(pty);
	gchar buf[PTY_READ_BUF_SIZ];
	gssize n;

	if (condition & (G_IO_HUP | G_IO_ERR)) {
		priv->io_watch_id = 0;
		return FALSE;
	}

	if (condition & G_IO_IN) {
		n = read(priv->master_fd, buf, sizeof(buf));
		if (n > 0) {
			g_signal_emit(pty, signals[SIGNAL_DATA_RECEIVED], 0,
			              (gpointer)buf, (gulong)n);
		} else if (n <= 0) {
			priv->io_watch_id = 0;
			return FALSE;
		}
	}

	return TRUE;
}

/*
 * pty_child_watch:
 *
 * Called when the child process exits. Emits the "child-exited" signal.
 */
static void
pty_child_watch(
    GPid        pid,
    gint        status,
    gpointer    user_data
){
	GstPty *pty = GST_PTY(user_data);
	GstPtyPrivate *priv = gst_pty_get_instance_private(pty);

	priv->running = FALSE;
	priv->child_watch_id = 0;

	g_spawn_close_pid(pid);
	g_signal_emit(pty, signals[SIGNAL_CHILD_EXITED], 0, status);
}

/**
 * gst_pty_new:
 *
 * Creates a new PTY instance.
 *
 * Returns: (transfer full): a new GstPty
 */
GstPty *
gst_pty_new(void)
{
	return g_object_new(GST_TYPE_PTY, NULL);
}

/**
 * gst_pty_spawn:
 * @pty: a #GstPty
 * @shell: (nullable): shell program to run, or NULL for $SHELL
 * @envp: (nullable): environment variables, or NULL for inherited
 * @error: location for error
 *
 * Forks a child process connected via a pseudo-terminal.
 * Sets up a GIOChannel watch to read data from the child.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_pty_spawn(
    GstPty      *pty,
    const gchar *shell,
    gchar       **envp,
    GError      **error
){
	GstPtyPrivate *priv;
	struct winsize ws;
	pid_t pid;

	g_return_val_if_fail(GST_IS_PTY(pty), FALSE);

	priv = gst_pty_get_instance_private(pty);

	if (priv->running) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
		            "PTY already has a running child");
		return FALSE;
	}

	ws.ws_col = (unsigned short)priv->cols;
	ws.ws_row = (unsigned short)priv->rows;
	ws.ws_xpixel = 0;
	ws.ws_ypixel = 0;

	pid = forkpty(&priv->master_fd, NULL, NULL, &ws);

	if (pid < 0) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
		            "Failed to fork PTY: %s", g_strerror(errno));
		return FALSE;
	}

	if (pid == 0) {
		/* Child process */
		const gchar *sh;

		sh = shell;
		if (sh == NULL) {
			sh = g_getenv("SHELL");
		}
		if (sh == NULL) {
			sh = "/bin/sh";
		}

		/* Set TERM */
		setenv("TERM", "st-256color", 1);

		unsetenv("COLUMNS");
		unsetenv("LINES");
		unsetenv("TERMCAP");

		if (envp != NULL) {
			execve(sh, (char *const[]){(char *)sh, NULL}, envp);
		} else {
			execlp(sh, sh, NULL);
		}
		_exit(127);
	}

	/* Parent process */
	priv->child_pid = pid;
	priv->running = TRUE;

	/* Set up non-blocking I/O channel for reading */
	priv->io_channel = g_io_channel_unix_new(priv->master_fd);
	g_io_channel_set_encoding(priv->io_channel, NULL, NULL);
	g_io_channel_set_buffered(priv->io_channel, FALSE);
	g_io_channel_set_flags(priv->io_channel,
	    g_io_channel_get_flags(priv->io_channel) | G_IO_FLAG_NONBLOCK,
	    NULL);

	/* Add I/O watch to GMainLoop */
	priv->io_watch_id = g_io_add_watch(priv->io_channel,
	    G_IO_IN | G_IO_ERR | G_IO_HUP,
	    pty_io_callback, pty);

	/* Add child watch for SIGCHLD */
	priv->child_watch_id = g_child_watch_add(pid, pty_child_watch, pty);

	return TRUE;
}

/**
 * gst_pty_write:
 * @pty: a #GstPty
 * @data: data to write to the child
 * @len: length of data, or -1 if NUL-terminated
 *
 * Writes data to the child process via the PTY master fd.
 */
void
gst_pty_write(
    GstPty      *pty,
    const gchar *data,
    gssize      len
){
	GstPtyPrivate *priv;
	gssize written;
	gssize total;

	g_return_if_fail(GST_IS_PTY(pty));
	g_return_if_fail(data != NULL);

	priv = gst_pty_get_instance_private(pty);

	if (priv->master_fd < 0 || !priv->running) {
		return;
	}

	if (len < 0) {
		len = (gssize)strlen(data);
	}

	total = 0;
	while (total < len) {
		written = write(priv->master_fd, data + total, len - total);
		if (written < 0) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		total += written;
	}
}

/**
 * gst_pty_write_no_echo:
 * @pty: a #GstPty
 * @data: data to write to the child
 * @len: length of data, or -1 if NUL-terminated
 *
 * Writes data to the child process via the PTY master fd with
 * ECHO temporarily disabled. This prevents the line discipline
 * from echoing the data back to the master's read buffer, which
 * would cause the terminal to re-parse its own responses.
 *
 * Use this for terminal responses (DA, kitty graphics replies, etc.)
 * where echo loopback would create a feedback loop.
 */
void
gst_pty_write_no_echo(
    GstPty      *pty,
    const gchar *data,
    gssize      len
){
	GstPtyPrivate *priv;
	struct termios tio;
	struct termios saved;
	gboolean restored;

	g_return_if_fail(GST_IS_PTY(pty));
	g_return_if_fail(data != NULL);

	priv = gst_pty_get_instance_private(pty);

	if (priv->master_fd < 0 || !priv->running) {
		return;
	}

	/* Suppress echo for this write */
	restored = FALSE;
	if (tcgetattr(priv->master_fd, &tio) == 0) {
		saved = tio;
		if (tio.c_lflag & ECHO) {
			tio.c_lflag &= ~(ECHO);
			tcsetattr(priv->master_fd, TCSANOW, &tio);
			restored = TRUE;
		}
	}

	gst_pty_write(pty, data, len);

	/* Restore echo */
	if (restored) {
		tcsetattr(priv->master_fd, TCSANOW, &saved);
	}
}

/**
 * gst_pty_resize:
 * @pty: a #GstPty
 * @cols: new number of columns
 * @rows: new number of rows
 *
 * Resizes the PTY window. Sends SIGWINCH to the child process.
 */
void
gst_pty_resize(
    GstPty  *pty,
    gint    cols,
    gint    rows
){
	GstPtyPrivate *priv;
	struct winsize ws;

	g_return_if_fail(GST_IS_PTY(pty));

	priv = gst_pty_get_instance_private(pty);
	priv->cols = cols;
	priv->rows = rows;

	if (priv->master_fd < 0) {
		return;
	}

	ws.ws_col = (unsigned short)cols;
	ws.ws_row = (unsigned short)rows;
	ws.ws_xpixel = 0;
	ws.ws_ypixel = 0;

	ioctl(priv->master_fd, TIOCSWINSZ, &ws);
}

/**
 * gst_pty_get_fd:
 * @pty: a #GstPty
 *
 * Gets the master file descriptor.
 *
 * Returns: the fd, or -1 if not connected
 */
gint
gst_pty_get_fd(GstPty *pty)
{
	GstPtyPrivate *priv;

	g_return_val_if_fail(GST_IS_PTY(pty), -1);

	priv = gst_pty_get_instance_private(pty);
	return priv->master_fd;
}

/**
 * gst_pty_get_child_pid:
 * @pty: a #GstPty
 *
 * Gets the PID of the child process.
 *
 * Returns: child PID, or 0 if no child
 */
GPid
gst_pty_get_child_pid(GstPty *pty)
{
	GstPtyPrivate *priv;

	g_return_val_if_fail(GST_IS_PTY(pty), 0);

	priv = gst_pty_get_instance_private(pty);
	return priv->child_pid;
}

/**
 * gst_pty_is_running:
 * @pty: a #GstPty
 *
 * Checks if the child process is still running.
 *
 * Returns: %TRUE if child is running
 */
gboolean
gst_pty_is_running(GstPty *pty)
{
	GstPtyPrivate *priv;

	g_return_val_if_fail(GST_IS_PTY(pty), FALSE);

	priv = gst_pty_get_instance_private(pty);
	return priv->running;
}
