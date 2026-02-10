/*
 * gst-pty.h - GST PTY Management
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Manages pseudo-terminal allocation, fork/exec of shell,
 * and I/O with the child process.
 */

#ifndef GST_PTY_H
#define GST_PTY_H

#include <glib-object.h>
#include "../gst-types.h"

G_BEGIN_DECLS

#define GST_TYPE_PTY             (gst_pty_get_type())
#define GST_PTY(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_PTY, GstPty))
#define GST_PTY_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_PTY, GstPtyClass))
#define GST_IS_PTY(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_PTY))
#define GST_IS_PTY_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_PTY))
#define GST_PTY_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_PTY, GstPtyClass))

struct _GstPty {
    GObject parent_instance;
};

struct _GstPtyClass {
    GObjectClass parent_class;

    void (*data_received)(GstPty *pty, const gchar *data, gsize len);
    void (*child_exited)(GstPty *pty, gint status);
};

GType gst_pty_get_type(void) G_GNUC_CONST;

GstPty *gst_pty_new(void);

gboolean gst_pty_spawn(GstPty *pty, const gchar *shell, gchar **envp, GError **error);

void gst_pty_write(GstPty *pty, const gchar *data, gssize len);

void gst_pty_write_no_echo(GstPty *pty, const gchar *data, gssize len);

void gst_pty_resize(GstPty *pty, gint cols, gint rows);

gint gst_pty_get_fd(GstPty *pty);

GPid gst_pty_get_child_pid(GstPty *pty);

gboolean gst_pty_is_running(GstPty *pty);

G_END_DECLS

#endif /* GST_PTY_H */
