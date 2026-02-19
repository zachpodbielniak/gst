/*
 * gst-shellint-module.h - Shell integration via OSC 133 semantic zones
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implements semantic prompt zones using the OSC 133 protocol.
 * Tracks prompt, command, output, and completion boundaries so
 * the user can navigate between prompts and see exit code status.
 *
 * Implements GstEscapeHandler (for OSC 133 parsing),
 * GstInputHandler (for Ctrl+Shift+Up/Down prompt navigation),
 * and GstRenderOverlay (for prompt markers and exit code indicators).
 */

#ifndef GST_SHELLINT_MODULE_H
#define GST_SHELLINT_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-escape-handler.h"
#include "../../src/interfaces/gst-input-handler.h"
#include "../../src/interfaces/gst-render-overlay.h"

G_BEGIN_DECLS

#define GST_TYPE_SHELLINT_MODULE (gst_shellint_module_get_type())

G_DECLARE_FINAL_TYPE(GstShellintModule, gst_shellint_module,
	GST, SHELLINT_MODULE, GstModule)

/**
 * gst_module_register:
 *
 * Module entry point. Returns the GType of the shell integration
 * module so the module manager can instantiate it.
 *
 * Returns: The #GType for #GstShellintModule
 */
G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_SHELLINT_MODULE_H */
