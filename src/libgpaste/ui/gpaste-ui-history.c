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

#include <gpaste-ui-empty-item.h>
#include <gpaste-ui-history.h>
#include <gpaste-ui-item.h>
#include <gpaste-update-enums.h>

struct _GPasteUiHistory
{
    GtkListBox parent_instance;
};

typedef struct
{
    GPasteClient   *client;
    GPasteSettings *settings;
    GtkWidget      *dummy_item;

    GSList         *items;
    gsize           size;

    gchar          *search;
    guint32        *search_results;
    gsize           search_results_size;

    gulong          activated_id;
    gulong          update_id;
} GPasteUiHistoryPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiHistory, g_paste_ui_history, GTK_TYPE_LIST_BOX)

static void
on_row_activated (GtkListBox    *history G_GNUC_UNUSED,
                  GtkListBoxRow *row,
                  gpointer       user_data G_GNUC_UNUSED)
{
    g_paste_ui_item_activate (G_PASTE_UI_ITEM (row));
}

static void
g_paste_ui_history_add_item (gpointer data,
                             gpointer user_data)
{
    GtkContainer *self = user_data;
    GtkWidget *item = data;
    g_object_ref (item);
    gtk_container_add (self, item);
    gtk_widget_show_all (item);
}

static void
g_paste_ui_history_add_list (GtkContainer *self,
                             GSList       *list)
{
    g_slist_foreach (list, g_paste_ui_history_add_item, self);
}

static void
g_paste_ui_history_remove (gpointer data,
                           gpointer user_data)
{
    GtkWidget *item = data;
    GtkContainer *self = user_data;

    g_object_unref (item);
    gtk_container_remove (self, item);
}

static void
g_paste_applet_history_drop_list (GtkContainer *self,
                                  GSList       *list)
{
    g_slist_foreach (list, g_paste_ui_history_remove, self);
    g_slist_free (list);
}

typedef struct {
    GPasteUiHistory *self;
    guint            from_index;
} OnUpdateCallbackData;

static void
g_paste_ui_history_refresh_history (GObject      *source_object G_GNUC_UNUSED,
                                    GAsyncResult *res,
                                    gpointer      user_data)
{
    g_autofree OnUpdateCallbackData *data = user_data;
    GPasteUiHistory *self = data->self;
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);

    gsize old_size = priv->size;
    guint refreshTextBound = old_size;
    priv->size = MIN (g_paste_client_get_history_size_finish (priv->client, res, NULL),
                      g_paste_settings_get_max_displayed_history_size (priv->settings));
    if (priv->size)
        gtk_widget_hide (priv->dummy_item);
    else
        gtk_widget_show (priv->dummy_item);

    if (old_size < priv->size)
    {
        for (gsize i = old_size; i < priv->size; ++i)
        {
            GtkWidget *item = g_paste_ui_item_new (priv->client, priv->settings, i);
            priv->items = g_slist_append (priv->items, item);
        }
        g_paste_ui_history_add_list (GTK_CONTAINER (self), g_slist_nth (priv->items, old_size));
        refreshTextBound = old_size;
    }
    else if (old_size > priv->size)
    {
        if (priv->size)
        {
            GSList *last = g_slist_nth (priv->items, priv->size - 1);
            g_return_if_fail (last);
            g_paste_applet_history_drop_list (GTK_CONTAINER (self), g_slist_next (last));
            last->next = NULL;
        }
        else
        {
            g_paste_applet_history_drop_list (GTK_CONTAINER (self), priv->items);
            priv->items = NULL;
        }
        refreshTextBound = priv->size;
    }

    GSList *item = priv->items;

    for (gsize i = 0; i < data->from_index; ++i)
        item = g_slist_next (item);
    for (gsize i = data->from_index; i < refreshTextBound; ++i, item = g_slist_next (item))
        g_paste_ui_item_set_index (item->data, i);
}

static void
g_paste_ui_history_refresh (GPasteUiHistory *self,
                            guint            from_index)
{
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);

    if (priv->search)
    {
        g_paste_ui_history_search (self, priv->search);
    }
    else
    {
        OnUpdateCallbackData *data = g_new (OnUpdateCallbackData, 1);
        data->self = self;
        data->from_index = from_index;

        g_paste_client_get_history_size (priv->client, g_paste_ui_history_refresh_history, data);
    }
}

static void
on_search_ready (GObject      *source_object G_GNUC_UNUSED,
                 GAsyncResult *res,
                 gpointer      user_data)
{
    GPasteUiHistory *self = user_data;
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);

    priv->search_results = g_paste_client_search_finish (priv->client, res, &priv->search_results_size, NULL /* error */);

    if (!priv->search_results)
        priv->search_results_size = 0;
    else if (priv->search_results_size > priv->size)
        priv->search_results_size = priv->size;

    GSList *item = priv->items;

    for (gsize i = 0; i < priv->search_results_size; ++i, item = g_slist_next (item))
        g_paste_ui_item_set_index (item->data, priv->search_results[i]);
    for (gsize i = priv->search_results_size; i < priv->size; ++i, item = g_slist_next (item))
        g_paste_ui_item_set_index (item->data, -1);
}

/**
 * g_paste_ui_history_search:
 * @self: a #GPasteUiHistory instance
 * @search: the search
 *
 * Apply a search to a #GPasteUiHistory instance
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_history_search (GPasteUiHistory *self,
                           const gchar     *search)
{
    g_return_if_fail (G_PASTE_IS_UI_HISTORY (self));

    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);

    if (!g_strcmp0 (search, ""))
    {
        g_clear_pointer (&priv->search, g_free);
        g_clear_pointer (&priv->search_results, g_free);
        priv->search_results_size = 0;
        g_paste_ui_history_refresh (self, 0);
    }
    else
    {
        g_free (priv->search);
        priv->search = g_strdup (search);
        g_paste_client_search (priv->client, search, on_search_ready, self);
    }
}

static void
g_paste_ui_history_on_update (GPasteClient      *client G_GNUC_UNUSED,
                              GPasteUpdateAction action,
                              GPasteUpdateTarget target,
                              guint              position,
                              gpointer           user_data)
{
    GPasteUiHistory *self = user_data;
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);
    gboolean refresh = FALSE;

    switch (target)
    {
    case G_PASTE_UPDATE_TARGET_ALL:
        refresh = TRUE;
        break;
    case G_PASTE_UPDATE_TARGET_POSITION:
        switch (action)
        {
        case G_PASTE_UPDATE_ACTION_REPLACE:
            g_paste_ui_item_refresh (g_slist_nth_data (priv->items, position));
            break;
        case G_PASTE_UPDATE_ACTION_REMOVE:
            refresh = TRUE;
            break;
        default:
            g_assert_not_reached ();
        }
        break;
    default:
        g_assert_not_reached ();
    }

    if (refresh)
        g_paste_ui_history_refresh (self, position);
}

static void
g_paste_ui_history_dispose (GObject *object)
{
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (G_PASTE_UI_HISTORY (object));

    if (priv->activated_id)
    {
        g_signal_handler_disconnect (object, priv->activated_id);
        priv->activated_id = 0;
    }

    if (priv->update_id)
    {
        g_signal_handler_disconnect (priv->client, priv->update_id);
        priv->update_id = 0;
    }

    g_clear_object (&priv->settings);
    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_ui_history_parent_class)->dispose (object);
}

static void
g_paste_ui_history_finalize (GObject *object)
{
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (G_PASTE_UI_HISTORY (object));

    g_free (priv->search);
    g_free (priv->search_results);

    G_OBJECT_CLASS (g_paste_ui_history_parent_class)->finalize (object);
}

static void
g_paste_ui_history_class_init (GPasteUiHistoryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_ui_history_dispose;
    object_class->finalize = g_paste_ui_history_finalize;
}

static void
g_paste_ui_history_init (GPasteUiHistory *self)
{
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);

    priv->settings = g_paste_settings_new ();
    priv->dummy_item = g_paste_ui_empty_item_new ();

    gtk_container_add (GTK_CONTAINER (self), priv->dummy_item);

    priv->activated_id = g_signal_connect (G_OBJECT (self),
                                           "row-activated",
                                           G_CALLBACK (on_row_activated),
                                           NULL);
}

/**
 * g_paste_ui_history_new:
 * @client: a #GPasteClient instance
 *
 * Create a new instance of #GPasteUiHistory
 *
 * Returns: a newly allocated #GPasteUiHistory
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_history_new (GPasteClient *client)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_HISTORY, NULL);
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (G_PASTE_UI_HISTORY (self));

    priv->client = g_object_ref (client);

    priv->update_id = g_signal_connect (client,
                                        "update",
                                        G_CALLBACK (g_paste_ui_history_on_update),
                                        self);

    g_paste_ui_history_on_update (client, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, 0, self);

    return self;
}
