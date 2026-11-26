/*
 * gst-program-resume-module.h - OSC 7577 program resume module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Receives OSC 7577 reports and watches the owning process. The resume
 * command itself is started by the terminal before this module activates.
 */

#ifndef GST_PROGRAM_RESUME_MODULE_H
#define GST_PROGRAM_RESUME_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-escape-handler.h"

G_BEGIN_DECLS

#define GST_TYPE_PROGRAM_RESUME_MODULE (gst_program_resume_module_get_type())

G_DECLARE_FINAL_TYPE(GstProgramResumeModule, gst_program_resume_module,
	GST, PROGRAM_RESUME_MODULE, GstModule)

G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_PROGRAM_RESUME_MODULE_H */
