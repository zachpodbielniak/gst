/*
 * gst-program-resume-module.c - OSC 7577 program resume module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * OSC 7577 payloads are handed to the library session. The owning pid is
 * watched with pidfd. When that process exits and the terminal stays up,
 * the record is removed after a short grace period so a shutdown that
 * races the exit can still resume.
 */

#include "gst-program-resume-module.h"

#include <errno.h>
#include <glib-unix.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "../../src/core/gst-osc7577.h"
#include "../../src/core/gst-pty.h"
#include "../../src/module/gst-module-manager.h"

#define GST_OSC7577_GRACE_SEC (2)

struct _GstProgramResumeModule
{
	GstModule parent_instance;
	gint      pidfd;
	guint     watch_id;
	guint     grace_id;
	gint64    watched_pid;
};

static void
gst_program_resume_escape_init(GstEscapeHandlerInterface *iface);

G_DEFINE_TYPE_WITH_CODE(GstProgramResumeModule, gst_program_resume_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_ESCAPE_HANDLER,
		gst_program_resume_escape_init))

static void
disarm(GstProgramResumeModule *self)
{
	if (self->watch_id != 0) {
		g_source_remove(self->watch_id);
		self->watch_id = 0;
	}
	if (self->grace_id != 0) {
		g_source_remove(self->grace_id);
		self->grace_id = 0;
	}
	if (self->pidfd >= 0) {
		close(self->pidfd);
		self->pidfd = -1;
	}
	self->watched_pid = 0;
}

static gboolean
on_grace(gpointer user_data)
{
	GstProgramResumeModule *self;

	self = GST_PROGRAM_RESUME_MODULE(user_data);
	self->grace_id = 0;
	if (!gst_osc7577_is_shutting_down()) {
		gst_osc7577_drop_owner();
	}
	return G_SOURCE_REMOVE;
}

static void
schedule_grace(GstProgramResumeModule *self)
{
	if (self->grace_id != 0 || gst_osc7577_is_shutting_down()) {
		return;
	}
	self->grace_id = g_timeout_add_seconds(GST_OSC7577_GRACE_SEC, on_grace, self);
}

static gboolean
on_pid_exit(
	gint         fd,
	GIOCondition condition,
	gpointer     user_data
){
	GstProgramResumeModule *self;

	(void)fd;
	(void)condition;
	self = GST_PROGRAM_RESUME_MODULE(user_data);
	self->watch_id = 0;
	schedule_grace(self);
	return G_SOURCE_REMOVE;
}

/*
 * arm_owner:
 *
 * Watch the process named by the latest accepted report. A pid of zero
 * means a resume is in flight and the new program has not reported yet.
 */
static void
arm_owner(GstProgramResumeModule *self)
{
	gint64 pid;
	guint64 start;
	int fd;

	disarm(self);
	if (!gst_osc7577_get_owner(&pid, &start) || pid <= 0) {
		return;
	}
#ifdef SYS_pidfd_open
	fd = (int)syscall(SYS_pidfd_open, (pid_t)pid, 0);
#else
	fd = (int)syscall(434, (pid_t)pid, 0);
#endif
	if (fd < 0) {
		if (errno == ESRCH) {
			schedule_grace(self);
		}
		return;
	}
	self->pidfd = fd;
	self->watched_pid = pid;
	self->watch_id = g_unix_fd_add(fd, G_IO_IN | G_IO_HUP, on_pid_exit, self);
}

static const gchar *
gst_program_resume_get_name(GstModule *module)
{
	(void)module;
	return "program_resume";
}

static const gchar *
gst_program_resume_get_description(GstModule *module)
{
	(void)module;
	return "Resume reported programs after a restart (OSC 7577)";
}

static gboolean
gst_program_resume_activate(GstModule *module)
{
	GstProgramResumeModule *self;
	GstModuleManager *mgr;
	GstPty *pty;

	self = GST_PROGRAM_RESUME_MODULE(module);
	mgr = gst_module_manager_get_default();
	pty = gst_module_manager_get_pty(mgr);
	if (pty != NULL) {
		gst_osc7577_set_pts(gst_pty_get_slave_name(pty));
	}
	arm_owner(self);
	return TRUE;
}

static void
gst_program_resume_deactivate(GstModule *module)
{
	GstProgramResumeModule *self;

	self = GST_PROGRAM_RESUME_MODULE(module);
	/* Keep the record when the terminal itself is going away. */
	gst_osc7577_note_shutdown();
	disarm(self);
}

static gboolean
gst_program_resume_handle_escape(
	GstEscapeHandler *handler,
	gchar             str_type,
	const gchar      *buf,
	gsize             len,
	gpointer          terminal
){
	GstProgramResumeModule *self;

	(void)terminal;
	if (str_type != ']' || buf == NULL) {
		return FALSE;
	}
	self = GST_PROGRAM_RESUME_MODULE(handler);
	if (!gst_osc7577_on_report(buf, len)) {
		return FALSE;
	}
	arm_owner(self);
	return TRUE;
}

static void
gst_program_resume_escape_init(GstEscapeHandlerInterface *iface)
{
	iface->handle_escape_string = gst_program_resume_handle_escape;
}

static void
gst_program_resume_finalize(GObject *object)
{
	disarm(GST_PROGRAM_RESUME_MODULE(object));
	G_OBJECT_CLASS(gst_program_resume_module_parent_class)->finalize(object);
}

static void
gst_program_resume_module_class_init(GstProgramResumeModuleClass *klass)
{
	GstModuleClass *module_class;
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	module_class = GST_MODULE_CLASS(klass);
	object_class->finalize = gst_program_resume_finalize;
	module_class->get_name = gst_program_resume_get_name;
	module_class->get_description = gst_program_resume_get_description;
	module_class->activate = gst_program_resume_activate;
	module_class->deactivate = gst_program_resume_deactivate;
}

static void
gst_program_resume_module_init(GstProgramResumeModule *self)
{
	self->pidfd = -1;
	self->watch_id = 0;
	self->grace_id = 0;
	self->watched_pid = 0;
}

G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_PROGRAM_RESUME_MODULE;
}
