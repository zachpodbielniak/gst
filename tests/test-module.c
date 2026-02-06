/*
 * test-module.c - Tests for the GST module system
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests module registration, activation, priority, hook auto-detection,
 * hook dispatch (bell, key events), and priority ordering.
 * Uses in-process test module classes (no .so loading needed).
 */

#include <glib.h>
#include <glib-object.h>
#include "module/gst-module.h"
#include "module/gst-module-manager.h"
#include "interfaces/gst-bell-handler.h"
#include "interfaces/gst-input-handler.h"
#include "gst-enums.h"

/* ===================================================================
 * TestBellModule - a minimal GstModule that implements GstBellHandler.
 * Sets a flag when handle_bell is called, tracks call order via a
 * shared counter pointer.
 * =================================================================== */

typedef struct
{
	GstModule parent_instance;
	gboolean  bell_called;
	gint     *order_counter;   /* shared counter for priority ordering tests */
	gint      call_order;      /* captured value of *order_counter at call time */
} TestBellModule;

typedef struct
{
	GstModuleClass parent_class;
} TestBellModuleClass;

static GType test_bell_module_get_type(void);

#define TEST_TYPE_BELL_MODULE (test_bell_module_get_type())
#define TEST_BELL_MODULE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), TEST_TYPE_BELL_MODULE, TestBellModule))

/* GstBellHandler interface implementation */
static void
test_bell_module_handle_bell(GstBellHandler *handler)
{
	TestBellModule *self;

	self = TEST_BELL_MODULE(handler);
	self->bell_called = TRUE;

	/* Record call order if counter is set */
	if (self->order_counter != NULL)
	{
		self->call_order = *self->order_counter;
		(*self->order_counter)++;
	}
}

static void
test_bell_handler_iface_init(GstBellHandlerInterface *iface)
{
	iface->handle_bell = test_bell_module_handle_bell;
}

/* GstModule vfuncs */
static const gchar *
test_bell_module_get_name(GstModule *module)
{
	(void)module;
	return "test-bell";
}

static const gchar *
test_bell_module_get_description(GstModule *module)
{
	(void)module;
	return "Test bell module";
}

static gboolean
test_bell_module_activate(GstModule *module)
{
	(void)module;
	return TRUE;
}

static void
test_bell_module_deactivate(GstModule *module)
{
	(void)module;
}

static void
test_bell_module_class_init(TestBellModuleClass *klass)
{
	GstModuleClass *mod_class;

	mod_class = GST_MODULE_CLASS(klass);
	mod_class->get_name = test_bell_module_get_name;
	mod_class->get_description = test_bell_module_get_description;
	mod_class->activate = test_bell_module_activate;
	mod_class->deactivate = test_bell_module_deactivate;
}

static void
test_bell_module_init(TestBellModule *self)
{
	self->bell_called = FALSE;
	self->order_counter = NULL;
	self->call_order = -1;
}

G_DEFINE_TYPE_WITH_CODE(TestBellModule, test_bell_module, GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_BELL_HANDLER, test_bell_handler_iface_init))

/* ===================================================================
 * TestBellModule2 - a second bell module for priority ordering tests.
 * Identical to TestBellModule but with a different type and name.
 * =================================================================== */

typedef struct
{
	GstModule parent_instance;
	gboolean  bell_called;
	gint     *order_counter;
	gint      call_order;
} TestBellModule2;

typedef struct
{
	GstModuleClass parent_class;
} TestBellModule2Class;

static GType test_bell_module2_get_type(void);

#define TEST_TYPE_BELL_MODULE2 (test_bell_module2_get_type())
#define TEST_BELL_MODULE2(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), TEST_TYPE_BELL_MODULE2, TestBellModule2))

static void
test_bell_module2_handle_bell(GstBellHandler *handler)
{
	TestBellModule2 *self;

	self = TEST_BELL_MODULE2(handler);
	self->bell_called = TRUE;

	if (self->order_counter != NULL)
	{
		self->call_order = *self->order_counter;
		(*self->order_counter)++;
	}
}

static void
test_bell_handler2_iface_init(GstBellHandlerInterface *iface)
{
	iface->handle_bell = test_bell_module2_handle_bell;
}

static const gchar *
test_bell_module2_get_name(GstModule *module)
{
	(void)module;
	return "test-bell-2";
}

static const gchar *
test_bell_module2_get_description(GstModule *module)
{
	(void)module;
	return "Test bell module 2";
}

static gboolean
test_bell_module2_activate(GstModule *module)
{
	(void)module;
	return TRUE;
}

static void
test_bell_module2_deactivate(GstModule *module)
{
	(void)module;
}

static void
test_bell_module2_class_init(TestBellModule2Class *klass)
{
	GstModuleClass *mod_class;

	mod_class = GST_MODULE_CLASS(klass);
	mod_class->get_name = test_bell_module2_get_name;
	mod_class->get_description = test_bell_module2_get_description;
	mod_class->activate = test_bell_module2_activate;
	mod_class->deactivate = test_bell_module2_deactivate;
}

static void
test_bell_module2_init(TestBellModule2 *self)
{
	self->bell_called = FALSE;
	self->order_counter = NULL;
	self->call_order = -1;
}

G_DEFINE_TYPE_WITH_CODE(TestBellModule2, test_bell_module2, GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_BELL_HANDLER, test_bell_handler2_iface_init))

/* ===================================================================
 * TestInputModule - a GstModule that implements GstInputHandler.
 * Can be configured to consume or pass through key events.
 * =================================================================== */

typedef struct
{
	GstModule parent_instance;
	gboolean  consume;         /* whether to return TRUE from handle_key_event */
	gboolean  key_event_called;
} TestInputModule;

typedef struct
{
	GstModuleClass parent_class;
} TestInputModuleClass;

static GType test_input_module_get_type(void);

#define TEST_TYPE_INPUT_MODULE (test_input_module_get_type())
#define TEST_INPUT_MODULE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), TEST_TYPE_INPUT_MODULE, TestInputModule))

static gboolean
test_input_module_handle_key_event(
	GstInputHandler *handler,
	guint            keyval,
	guint            keycode,
	guint            state
){
	TestInputModule *self;

	self = TEST_INPUT_MODULE(handler);
	self->key_event_called = TRUE;
	return self->consume;
}

static void
test_input_handler_iface_init(GstInputHandlerInterface *iface)
{
	iface->handle_key_event = test_input_module_handle_key_event;
}

static const gchar *
test_input_module_get_name(GstModule *module)
{
	(void)module;
	return "test-input";
}

static const gchar *
test_input_module_get_description(GstModule *module)
{
	(void)module;
	return "Test input module";
}

static gboolean
test_input_module_activate(GstModule *module)
{
	(void)module;
	return TRUE;
}

static void
test_input_module_deactivate(GstModule *module)
{
	(void)module;
}

static void
test_input_module_class_init(TestInputModuleClass *klass)
{
	GstModuleClass *mod_class;

	mod_class = GST_MODULE_CLASS(klass);
	mod_class->get_name = test_input_module_get_name;
	mod_class->get_description = test_input_module_get_description;
	mod_class->activate = test_input_module_activate;
	mod_class->deactivate = test_input_module_deactivate;
}

static void
test_input_module_init(TestInputModule *self)
{
	self->consume = FALSE;
	self->key_event_called = FALSE;
}

G_DEFINE_TYPE_WITH_CODE(TestInputModule, test_input_module, GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_INPUT_HANDLER, test_input_handler_iface_init))

/* ===================================================================
 * Test cases
 * =================================================================== */

/*
 * test_module_manager_new:
 * Verify that creating a module manager returns a non-NULL object.
 */
static void
test_module_manager_new(void)
{
	GstModuleManager *mgr;

	mgr = gst_module_manager_new();
	g_assert_nonnull(mgr);
	g_assert_true(GST_IS_MODULE_MANAGER(mgr));
	g_object_unref(mgr);
}

/*
 * test_module_register:
 * Register a test module, verify it's found by name.
 */
static void
test_module_register(void)
{
	GstModuleManager *mgr;
	TestBellModule *mod;
	GstModule *found;

	mgr = gst_module_manager_new();
	mod = (TestBellModule *)g_object_new(TEST_TYPE_BELL_MODULE, NULL);

	g_assert_true(gst_module_manager_register(mgr, GST_MODULE(mod)));

	found = gst_module_manager_get_module(mgr, "test-bell");
	g_assert_nonnull(found);
	g_assert_true(found == GST_MODULE(mod));

	g_object_unref(mod);
	g_object_unref(mgr);
}

/*
 * test_module_register_duplicate:
 * Registering a module with the same name twice should fail.
 */
static void
test_module_register_duplicate(void)
{
	GstModuleManager *mgr;
	TestBellModule *mod1;
	TestBellModule *mod2;

	mgr = gst_module_manager_new();
	mod1 = (TestBellModule *)g_object_new(TEST_TYPE_BELL_MODULE, NULL);
	mod2 = (TestBellModule *)g_object_new(TEST_TYPE_BELL_MODULE, NULL);

	g_assert_true(gst_module_manager_register(mgr, GST_MODULE(mod1)));

	/* Expect the g_warning from the duplicate registration attempt */
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING,
		"*already registered*");
	g_assert_false(gst_module_manager_register(mgr, GST_MODULE(mod2)));
	g_test_assert_expected_messages();

	g_object_unref(mod1);
	g_object_unref(mod2);
	g_object_unref(mgr);
}

/*
 * test_module_unregister:
 * Unregister a module, verify lookup returns NULL.
 */
static void
test_module_unregister(void)
{
	GstModuleManager *mgr;
	TestBellModule *mod;

	mgr = gst_module_manager_new();
	mod = (TestBellModule *)g_object_new(TEST_TYPE_BELL_MODULE, NULL);

	gst_module_manager_register(mgr, GST_MODULE(mod));
	g_assert_true(gst_module_manager_unregister(mgr, "test-bell"));
	g_assert_null(gst_module_manager_get_module(mgr, "test-bell"));

	g_object_unref(mod);
	g_object_unref(mgr);
}

/*
 * test_module_activate_deactivate:
 * Verify activate sets active and deactivate clears it.
 */
static void
test_module_activate_deactivate(void)
{
	TestBellModule *mod;

	mod = (TestBellModule *)g_object_new(TEST_TYPE_BELL_MODULE, NULL);

	g_assert_false(gst_module_is_active(GST_MODULE(mod)));
	g_assert_true(gst_module_activate(GST_MODULE(mod)));
	g_assert_true(gst_module_is_active(GST_MODULE(mod)));

	gst_module_deactivate(GST_MODULE(mod));
	g_assert_false(gst_module_is_active(GST_MODULE(mod)));

	g_object_unref(mod);
}

/*
 * test_module_priority:
 * Verify set/get priority.
 */
static void
test_module_priority(void)
{
	TestBellModule *mod;

	mod = (TestBellModule *)g_object_new(TEST_TYPE_BELL_MODULE, NULL);

	/* Default priority is NORMAL (0) */
	g_assert_cmpint(gst_module_get_priority(GST_MODULE(mod)), ==,
		GST_MODULE_PRIORITY_NORMAL);

	gst_module_set_priority(GST_MODULE(mod), GST_MODULE_PRIORITY_HIGH);
	g_assert_cmpint(gst_module_get_priority(GST_MODULE(mod)), ==,
		GST_MODULE_PRIORITY_HIGH);

	gst_module_set_priority(GST_MODULE(mod), GST_MODULE_PRIORITY_LOW);
	g_assert_cmpint(gst_module_get_priority(GST_MODULE(mod)), ==,
		GST_MODULE_PRIORITY_LOW);

	g_object_unref(mod);
}

/*
 * test_module_is_active:
 * Verify is_active reflects activation state correctly.
 */
static void
test_module_is_active(void)
{
	TestBellModule *mod;

	mod = (TestBellModule *)g_object_new(TEST_TYPE_BELL_MODULE, NULL);

	g_assert_false(gst_module_is_active(GST_MODULE(mod)));

	gst_module_activate(GST_MODULE(mod));
	g_assert_true(gst_module_is_active(GST_MODULE(mod)));

	/* Activating again should still be true */
	g_assert_true(gst_module_activate(GST_MODULE(mod)));
	g_assert_true(gst_module_is_active(GST_MODULE(mod)));

	gst_module_deactivate(GST_MODULE(mod));
	g_assert_false(gst_module_is_active(GST_MODULE(mod)));

	/* Deactivating again is a no-op */
	gst_module_deactivate(GST_MODULE(mod));
	g_assert_false(gst_module_is_active(GST_MODULE(mod)));

	g_object_unref(mod);
}

/*
 * test_hook_registration:
 * Register a bell handler module, verify hooks auto-detected.
 * The bell module should be registered at GST_HOOK_BELL.
 * Dispatch bell to verify the hook is wired correctly.
 */
static void
test_hook_registration(void)
{
	GstModuleManager *mgr;
	TestBellModule *mod;

	mgr = gst_module_manager_new();
	mod = (TestBellModule *)g_object_new(TEST_TYPE_BELL_MODULE, NULL);

	gst_module_manager_register(mgr, GST_MODULE(mod));
	gst_module_activate(GST_MODULE(mod));

	/* Dispatch bell - should call handle_bell via auto-registered hook */
	g_assert_false(mod->bell_called);
	gst_module_manager_dispatch_bell(mgr);
	g_assert_true(mod->bell_called);

	g_object_unref(mod);
	g_object_unref(mgr);
}

/*
 * test_hook_dispatch_bell:
 * Register a bell handler, activate it, dispatch bell, verify called.
 * Also verify inactive modules are NOT called.
 */
static void
test_hook_dispatch_bell(void)
{
	GstModuleManager *mgr;
	TestBellModule *mod;

	mgr = gst_module_manager_new();
	mod = (TestBellModule *)g_object_new(TEST_TYPE_BELL_MODULE, NULL);

	gst_module_manager_register(mgr, GST_MODULE(mod));

	/* Module is not active - bell should NOT be called */
	gst_module_manager_dispatch_bell(mgr);
	g_assert_false(mod->bell_called);

	/* Activate and dispatch again */
	gst_module_activate(GST_MODULE(mod));
	gst_module_manager_dispatch_bell(mgr);
	g_assert_true(mod->bell_called);

	g_object_unref(mod);
	g_object_unref(mgr);
}

/*
 * test_hook_dispatch_key_consumed:
 * Register an input handler that consumes events (returns TRUE).
 * Dispatch a key event and verify dispatch returns TRUE.
 */
static void
test_hook_dispatch_key_consumed(void)
{
	GstModuleManager *mgr;
	TestInputModule *mod;
	gboolean consumed;

	mgr = gst_module_manager_new();
	mod = (TestInputModule *)g_object_new(TEST_TYPE_INPUT_MODULE, NULL);
	mod->consume = TRUE;

	gst_module_manager_register(mgr, GST_MODULE(mod));
	gst_module_activate(GST_MODULE(mod));

	consumed = gst_module_manager_dispatch_key_event(mgr, 'a', 0, 0);
	g_assert_true(consumed);
	g_assert_true(mod->key_event_called);

	g_object_unref(mod);
	g_object_unref(mgr);
}

/*
 * test_hook_dispatch_key_passthrough:
 * Register an input handler that passes through (returns FALSE).
 * Dispatch a key event and verify dispatch returns FALSE.
 */
static void
test_hook_dispatch_key_passthrough(void)
{
	GstModuleManager *mgr;
	TestInputModule *mod;
	gboolean consumed;

	mgr = gst_module_manager_new();
	mod = (TestInputModule *)g_object_new(TEST_TYPE_INPUT_MODULE, NULL);
	mod->consume = FALSE;

	gst_module_manager_register(mgr, GST_MODULE(mod));
	gst_module_activate(GST_MODULE(mod));

	consumed = gst_module_manager_dispatch_key_event(mgr, 'a', 0, 0);
	g_assert_false(consumed);
	g_assert_true(mod->key_event_called);

	g_object_unref(mod);
	g_object_unref(mgr);
}

/*
 * test_hook_priority_order:
 * Register two bell modules with different priorities.
 * Dispatch bell and verify the higher-priority (lower value)
 * module is called first.
 */
static void
test_hook_priority_order(void)
{
	GstModuleManager *mgr;
	TestBellModule *mod_low;
	TestBellModule2 *mod_high;
	gint counter;

	mgr = gst_module_manager_new();

	/* mod_low has NORMAL priority (0) */
	mod_low = (TestBellModule *)g_object_new(TEST_TYPE_BELL_MODULE, NULL);
	gst_module_set_priority(GST_MODULE(mod_low), GST_MODULE_PRIORITY_LOW);

	/* mod_high has HIGH priority (-100) - should run first */
	mod_high = (TestBellModule2 *)g_object_new(TEST_TYPE_BELL_MODULE2, NULL);
	gst_module_set_priority(GST_MODULE(mod_high), GST_MODULE_PRIORITY_HIGH);

	/* Register low first, high second - priority should override order */
	gst_module_manager_register(mgr, GST_MODULE(mod_low));
	gst_module_manager_register(mgr, GST_MODULE(mod_high));

	gst_module_activate(GST_MODULE(mod_low));
	gst_module_activate(GST_MODULE(mod_high));

	/* Set up shared counter to track call order */
	counter = 0;
	mod_low->order_counter = &counter;
	mod_high->order_counter = &counter;

	gst_module_manager_dispatch_bell(mgr);

	/* HIGH priority (-100) should have been called first (order 0) */
	g_assert_true(mod_high->bell_called);
	g_assert_cmpint(mod_high->call_order, ==, 0);

	/* LOW priority (100) should have been called second (order 1) */
	g_assert_true(mod_low->bell_called);
	g_assert_cmpint(mod_low->call_order, ==, 1);

	g_object_unref(mod_low);
	g_object_unref(mod_high);
	g_object_unref(mgr);
}

/* ===================================================================
 * Main
 * =================================================================== */

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/module/manager-new", test_module_manager_new);
	g_test_add_func("/module/register", test_module_register);
	g_test_add_func("/module/register-duplicate", test_module_register_duplicate);
	g_test_add_func("/module/unregister", test_module_unregister);
	g_test_add_func("/module/activate-deactivate", test_module_activate_deactivate);
	g_test_add_func("/module/priority", test_module_priority);
	g_test_add_func("/module/is-active", test_module_is_active);
	g_test_add_func("/module/hook-registration", test_hook_registration);
	g_test_add_func("/module/hook-dispatch-bell", test_hook_dispatch_bell);
	g_test_add_func("/module/hook-dispatch-key-consumed", test_hook_dispatch_key_consumed);
	g_test_add_func("/module/hook-dispatch-key-passthrough", test_hook_dispatch_key_passthrough);
	g_test_add_func("/module/hook-priority-order", test_hook_priority_order);

	return g_test_run();
}
