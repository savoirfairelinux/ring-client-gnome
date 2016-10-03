/*
 *  Copyright (C) 2015-2016 Savoir-faire Linux Inc.
 *  Author: Stepan Salenikovich <stepan.salenikovich@savoirfairelinux.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA.
 */

#include "accountaudiotab.h"

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <account.h>
#include <codecmodel.h>
#include "models/gtkqtreemodel.h"
#include <QtCore/QSortFilterProxyModel>
#include "utils/models.h"

struct _AccountAudioTab
{
    GtkBox parent;
};

struct _AccountAudioTabClass
{
    GtkBoxClass parent_class;
};

typedef struct _AccountAudioTabPrivate AccountAudioTabPrivate;

struct _AccountAudioTabPrivate
{
    Account   *account;
    GtkWidget *treeview_codecs;
    GtkWidget *button_moveup;
    GtkWidget *button_movedown;
};

G_DEFINE_TYPE_WITH_PRIVATE(AccountAudioTab, account_audio_tab, GTK_TYPE_BOX);

#define ACCOUNT_AUDIO_TAB_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), ACCOUNT_AUDIO_TAB_TYPE, AccountAudioTabPrivate))

static void
account_audio_tab_dispose(GObject *object)
{
    G_OBJECT_CLASS(account_audio_tab_parent_class)->dispose(object);
}

static void
account_audio_tab_init(AccountAudioTab *view)
{
    gtk_widget_init_template(GTK_WIDGET(view));
}

static void
account_audio_tab_class_init(AccountAudioTabClass *klass)
{
    G_OBJECT_CLASS(klass)->dispose = account_audio_tab_dispose;

    gtk_widget_class_set_template_from_resource(GTK_WIDGET_CLASS (klass),
                                                "/cx/ring/RingGnome/accountaudiotab.ui");

    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), AccountAudioTab, treeview_codecs);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), AccountAudioTab, button_moveup);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), AccountAudioTab, button_movedown);
}

static void
codec_active_toggled(GtkCellRendererToggle *renderer, gchar *path, AccountAudioTab *view)
{
    g_return_if_fail(IS_ACCOUNT_AUDIO_TAB(view));
    AccountAudioTabPrivate *priv = ACCOUNT_AUDIO_TAB_GET_PRIVATE(view);

    /* we want to set it to the opposite of the current value */
    gboolean toggle = !gtk_cell_renderer_toggle_get_active(renderer);

    /* get iter which was clicked */
    GtkTreePath *tree_path = gtk_tree_path_new_from_string(path);
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(priv->treeview_codecs));
    GtkTreeIter iter;
    gtk_tree_model_get_iter(model, &iter, tree_path);

    /* get qmodelindex from iter and set the model data */
    QModelIndex idx = gtk_q_tree_model_get_source_idx(GTK_Q_TREE_MODEL(model), &iter);
    if (idx.isValid()) {
        priv->account->codecModel()->audioCodecs()->setData(idx, QVariant(toggle), Qt::CheckStateRole);
    }
}

static void
move_selected_codec(AccountAudioTab *view, int position_diff)
{
    g_return_if_fail(IS_ACCOUNT_AUDIO_TAB(view));
    AccountAudioTabPrivate *priv = ACCOUNT_AUDIO_TAB_GET_PRIVATE(view);

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->treeview_codecs));
    QModelIndex idx = get_index_from_selection(selection);

    if(!idx.isValid())
        return;

    QMimeData* mime = priv->account->codecModel()->audioCodecs()->mimeData(QModelIndexList() << idx);
    priv->account->codecModel()->audioCodecs()->dropMimeData(
                                                    mime,
                                                    Qt::MoveAction,
                                                    idx.row() + position_diff,
                                                    0,
                                                    QModelIndex());

    /* now make sure to select the same codec which was moved
     * TODO: UGLY! this should be somehow done in the qt modle bindings,
     *       or via a selection model, not here
     */
    int new_row = idx.row() + position_diff;
    int row_count = priv->account->codecModel()->audioCodecs()->rowCount(idx.parent());
    if (new_row < 0)
        new_row = row_count - 1;
    else if (new_row >= row_count)
        new_row = 0;

    idx = idx.sibling(new_row, idx.column());
    GtkTreeIter iter;
    if (gtk_q_tree_model_source_index_to_iter(
            GTK_Q_TREE_MODEL(gtk_tree_view_get_model(GTK_TREE_VIEW(priv->treeview_codecs))),
            idx,
            &iter)) {
        gtk_tree_selection_select_iter(selection, &iter);
    }
}

static void
move_codec_up(G_GNUC_UNUSED GtkButton *button, AccountAudioTab *view)
{
    move_selected_codec(view, -1);
}

static void
move_codec_down(G_GNUC_UNUSED GtkButton *button, AccountAudioTab *view)
{
    move_selected_codec(view, +1);
}

static void
build_tab_view(AccountAudioTab *view)
{
    g_return_if_fail(IS_ACCOUNT_AUDIO_TAB(view));
    AccountAudioTabPrivate *priv = ACCOUNT_AUDIO_TAB_GET_PRIVATE(view);

    /* codec model */
    GtkQTreeModel *codec_model;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    codec_model = gtk_q_tree_model_new(
        priv->account->codecModel()->audioCodecs(),
        4,
        0, Qt::CheckStateRole, G_TYPE_BOOLEAN,
        0, CodecModel::Role::NAME, G_TYPE_STRING,
        0, CodecModel::Role::BITRATE, G_TYPE_STRING,
        0, CodecModel::Role::SAMPLERATE, G_TYPE_STRING);
    gtk_tree_view_set_model(GTK_TREE_VIEW(priv->treeview_codecs), GTK_TREE_MODEL(codec_model));

    renderer = gtk_cell_renderer_toggle_new();
    column = gtk_tree_view_column_new_with_attributes(_("Enabled"), renderer, "active", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(priv->treeview_codecs), column);

    g_signal_connect(renderer, "toggled", G_CALLBACK(codec_active_toggled), view);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(C_("Name of the codec", "Name"), renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(priv->treeview_codecs), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(_("Bitrate"), renderer, "text", 2, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(priv->treeview_codecs), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(_("Samplerate"), renderer, "text", 3, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(priv->treeview_codecs), column);

    /* connect move codecs up/down signals */
    g_signal_connect(priv->button_moveup, "clicked", G_CALLBACK(move_codec_up), view);
    g_signal_connect(priv->button_movedown, "clicked", G_CALLBACK(move_codec_down), view);
}

GtkWidget *
account_audio_tab_new(Account *account)
{
    g_return_val_if_fail(account != NULL, NULL);

    gpointer view = g_object_new(ACCOUNT_AUDIO_TAB_TYPE, NULL);

    AccountAudioTabPrivate *priv = ACCOUNT_AUDIO_TAB_GET_PRIVATE(view);
    priv->account = account;

    build_tab_view(ACCOUNT_AUDIO_TAB(view));

    return (GtkWidget *)view;
}
