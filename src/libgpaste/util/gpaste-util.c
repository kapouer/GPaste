/*
 *      This file is part of GPaste.
 *
 *      Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gpaste-macros.h>
#include <gpaste-util.h>

/**
 * g_paste_util_confirm_dialog:
 * @parent: (nullable): the parent #GtkWindow
 * @msg: the message to display
 *
 * Show GPaste about dialog
 *
 * Returns:
 */
G_PASTE_VISIBLE gboolean
g_paste_util_confirm_dialog (GtkWindow   *parent,
                             const gchar *msg)
{
    g_return_val_if_fail (!parent || GTK_IS_WINDOW (parent), FALSE);
    g_return_val_if_fail (g_utf8_validate (msg, -1, NULL), FALSE);

    GtkWidget *dialog = gtk_message_dialog_new (parent,
                                                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_USE_HEADER_BAR,
                                                GTK_MESSAGE_QUESTION,
                                                GTK_BUTTONS_OK_CANCEL,
                                                "%s", msg);
    gboolean ret = gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK;

    gtk_widget_destroy (dialog);

    return ret;
}

/* Copied from glib's gio/gapplication-tool.c */
static GVariant *
app_get_platform_data (void)
{
    g_auto (GVariantBuilder) builder;
    const gchar *startup_id;

    g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);

    if ((startup_id = g_getenv ("DESKTOP_STARTUP_ID")))
        g_variant_builder_add (&builder, "{sv}", "desktop-startup-id", g_variant_new_string (startup_id));

    return g_variant_builder_end (&builder);
}

static void
g_paste_util_spawn_on_proxy_ready (GObject      *source_object G_GNUC_UNUSED,
                                   GAsyncResult *res,
                                   gpointer      user_data G_GNUC_UNUSED)
{
    g_autoptr (GDBusProxy) proxy = g_dbus_proxy_new_for_bus_finish (res, NULL /* error */);

    if (proxy)
    {
        g_dbus_proxy_call (proxy,
                           "Activate",
                           g_variant_new ("(@a{sv})", app_get_platform_data ()),
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,
                           NULL, /* cancellable */
                           NULL, /* callback */
                           NULL); /* user_data */
    }
}

/**
 * g_paste_util_spawn:
 * @app: the GPaste app to spawn
 *
 * spawn a GPaste app
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_util_spawn (const gchar *app)
{
    g_return_if_fail (g_utf8_validate (app, -1, NULL));

    g_autofree gchar *name = g_strdup_printf ("org.gnome.GPaste.%s", app);
    g_autofree gchar *object = g_strdup_printf ("/org/gnome/GPaste/%s", app);

    g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                              G_DBUS_PROXY_FLAGS_NONE,
                              NULL,
                              name,
                              object,
                              "org.freedesktop.Application",
                              NULL,
                              g_paste_util_spawn_on_proxy_ready,
                              NULL);
}

static GDBusProxy *
_bus_proxy_new_sync (const gchar *app,
                     GError     **error)
{
    g_autofree gchar *name = g_strdup_printf ("org.gnome.GPaste.%s", app);
    g_autofree gchar *object = g_strdup_printf ("/org/gnome/GPaste/%s", app);

    return g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                          G_DBUS_PROXY_FLAGS_NONE,
                                          NULL,
                                          name,
                                          object,
                                          "org.freedesktop.Application",
                                          NULL,
                                          error);
}

static gboolean
_spawn_sync (GDBusProxy *proxy,
             GError    **error)
{
    g_autoptr (GVariant) res = g_dbus_proxy_call_sync (proxy,
                                                      "Activate",
                                                      g_variant_new ("(@a{sv})", app_get_platform_data ()),
                                                      G_DBUS_CALL_FLAGS_NONE,
                                                      -1,
                                                      NULL,
                                                      error);

    return !error || !(*error);
}

/**
 * g_paste_util_spawn_sync:
 * @app: the GPaste app to spawn
 * @error: a #GError or %NULL
 *
 * spawn a GPaste app
 *
 * Returns: whether the spawn was successful
 */
G_PASTE_VISIBLE gboolean
g_paste_util_spawn_sync (const gchar *app,
                         GError     **error)
{
    g_return_val_if_fail (g_utf8_validate (app, -1, NULL), FALSE);
    g_return_val_if_fail (!error || !(*error), FALSE);

    g_autoptr (GDBusProxy) proxy = _bus_proxy_new_sync (app, error);

    if (!proxy)
        return FALSE;

    return _spawn_sync (proxy, error);
}

static void
g_paste_util_activate_ui_on_proxy_ready (GObject      *source_object G_GNUC_UNUSED,
                                         GAsyncResult *res,
                                         gpointer      user_data)
{
    g_autofree gpointer *data = (gpointer *) user_data;
    g_autofree gchar *action = data[0];
    GVariant *arg = data[1];
    g_autoptr (GDBusProxy) proxy = g_dbus_proxy_new_for_bus_finish (res, NULL /* error */);

    if (proxy)
    {
        g_auto (GVariantBuilder) params;

        g_variant_builder_init (&params, G_VARIANT_TYPE ("av"));

        if (arg)
            g_variant_builder_add (&params, "v", arg);

        g_dbus_proxy_call (proxy,
                           "ActivateAction",
                           g_variant_new ("(sav@a{sv})", action, &params, app_get_platform_data ()),
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,
                           NULL, /* cancellable */
                           NULL, /* callback */
                           NULL); /* user_data */
    }
}

/**
 * g_paste_util_activate_ui:
 * @action: the action to activate
 * @arg: (nullable): the action argument
 *
 * Activate an action on a GPaste app
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_util_activate_ui (const gchar *action,
                          GVariant    *arg)
{
    gpointer *data = g_new (gpointer, 2);
    data[0] = g_strdup (action);
    data[1] = arg;

    g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                              G_DBUS_PROXY_FLAGS_NONE,
                              NULL,
                              "org.gnome.GPaste.Ui",
                              "/org/gnome/GPaste/Ui",
                              "org.freedesktop.Application",
                              NULL,
                              g_paste_util_activate_ui_on_proxy_ready,
                              data);
}

/**
 * g_paste_util_activate_ui_sync:
 * @action: the action to activate
 * @arg: (nullable): the action argument
 * @error: a #GError or %NULL
 *
 * activate an action from GPaste Ui
 *
 * Returns: whether the action was successful
 */
G_PASTE_VISIBLE gboolean
g_paste_util_activate_ui_sync (const gchar *action,
                               GVariant    *arg,
                               GError     **error)
{
    g_return_val_if_fail (g_utf8_validate (action, -1, NULL), FALSE);
    g_return_val_if_fail (!error || !(*error), FALSE);

    g_autoptr (GDBusProxy) proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                                  G_DBUS_PROXY_FLAGS_NONE,
                                                                  NULL,
                                                                  "org.gnome.GPaste.Ui",
                                                                  "/org/gnome/GPaste/Ui",
                                                                  "org.freedesktop.Application",
                                                                  NULL,
                                                                  error);

    if (!proxy)
        return FALSE;

    g_auto (GVariantBuilder) params;

    g_variant_builder_init (&params, G_VARIANT_TYPE ("av"));

    if (arg)
        g_variant_builder_add (&params, "v", arg);

    /* We only consume it */
    G_GNUC_UNUSED g_autoptr (GVariant) res = g_dbus_proxy_call_sync (proxy,
                                                                     "ActivateAction",
                                                                     g_variant_new ("(sav@a{sv})",
                                                                                    action,
                                                                                    &params,
                                                                                    app_get_platform_data ()),
                                                                     G_DBUS_CALL_FLAGS_NONE,
                                                                     -1,
                                                                     NULL, /* cancellable */
                                                                     error);

    return TRUE;
}

/**
 * g_paste_util_relace:
 * @text: the initial text
 * @pattern: the pattern to replace
 * @substitution: the replacement text
 *
 * Replace some text
 *
 * Returns: the newly allocated string
 */
G_PASTE_VISIBLE gchar *
g_paste_util_replace (const gchar *text,
                      const gchar *pattern,
                      const gchar *substitution)
{
    g_return_val_if_fail (g_utf8_validate (text, -1, NULL), NULL);
    g_return_val_if_fail (g_utf8_validate (pattern, -1, NULL), NULL);
    g_return_val_if_fail (g_utf8_validate (substitution, -1, NULL), NULL);

    g_autofree gchar *regex_string = g_regex_escape_string (pattern, -1);
    g_autoptr (GRegex) regex = g_regex_new (regex_string,
                                            0, /* Compile options */
                                            0, /* Match options */
                                            NULL); /* Error */
    return g_regex_replace_literal (regex,
                                    text,
                                    (gssize) -1,
                                    0, /* Start position */
                                    substitution,
                                    0, /* Match options */
                                    NULL); /* Error */
}

/**
 * g_paste_util_compute_checksum:
 * @image: the #GdkPixbuf to checksum
 *
 * Compute the checksum of an image
 *
 * Returns: the newly allocated checksum
 */
G_PASTE_VISIBLE gchar *
g_paste_util_compute_checksum (GdkPixbuf *image)
{
    if (!image)
        return NULL;

    guint length;
    const guchar *data = gdk_pixbuf_get_pixels_with_length (image,
                                                            &length);
    return g_compute_checksum_for_data (G_CHECKSUM_SHA256,
                                        data,
                                        length);
}

/**
 * g_paste_util_has_applet:
 *
 * Check whether gpaste-applet is installed or not
 *
 * Returns: %TRUE if gpaste-applet is installed
 */
G_PASTE_VISIBLE gboolean
g_paste_util_has_applet (void)
{
    return g_file_test (PKGLIBEXECDIR "/gpaste-applet", G_FILE_TEST_IS_EXECUTABLE);
}

/**
 * g_paste_util_has_unity:
 *
 * Check whether gpaste-app-indicator is installed or not
 *
 * Returns: %TRUE if gpaste-app-indicator is installed
 */
G_PASTE_VISIBLE gboolean
g_paste_util_has_unity (void)
{
    return g_file_test (PKGLIBEXECDIR "/gpaste-app-indicator", G_FILE_TEST_IS_EXECUTABLE);
}

/**
 * g_paste_util_show_win:
 * @application: a #GtkApplication
 *
 * Present the application's window to user
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_util_show_win (GApplication *application)
{
    g_return_if_fail (GTK_IS_APPLICATION (application));

    for (GList *wins = gtk_application_get_windows (GTK_APPLICATION (application)); wins; wins = g_list_next (wins))
    {
        if (gtk_widget_get_realized (wins->data))
            gtk_window_present (wins->data);
    }
}

