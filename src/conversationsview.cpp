/****************************************************************************
 *    Copyright (C) 2017-2020 Savoir-faire Linux Inc.                             *
 *   Author: Nicolas Jäger <nicolas.jager@savoirfairelinux.com>             *
 *   Author: Sébastien Blin <sebastien.blin@savoirfairelinux.com>           *
 *   Author: Hugo Lefeuvre <hugo.lefeuvre@savoirfairelinux.com>             *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Lesser General Public             *
 *   License as published by the Free Software Foundation; either           *
 *   version 2.1 of the License, or (at your option) any later version.     *
 *                                                                          *
 *   This library is distributed in the hope that it will be useful,        *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU      *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU General Public License      *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.  *
 ***************************************************************************/

#include "conversationsview.h"

// std
#include <algorithm>
#include <chrono>
#include <iomanip> // for std::put_time
#include <string>
#include <sstream>

// GTK+ related
#include <QSize>

// LRC
#include <globalinstances.h>
#include <api/conversationmodel.h>
#include <api/contactmodel.h>
#include <api/call.h>
#include <api/contact.h>
#include <api/newcallmodel.h>

// Gnome client
#include "native/pixbufmanipulator.h"
#include "conversationpopupmenu.h"
#include "utils/files.h"

static constexpr const char* TEXT_URI_LIST_TARGET    = "text/uri-list";
static constexpr int         TEXT_URI_LIST_TARGET_ID = 0;

struct _ConversationsView
{
    GtkTreeView parent;
};

struct _ConversationsViewClass
{
    GtkTreeViewClass parent_class;
};

typedef struct _ConversationsViewPrivate ConversationsViewPrivate;

namespace { namespace details {
class CppImpl;
}}

struct _ConversationsViewPrivate
{
    AccountInfoPointer const *accountInfo_;

    GtkWidget* popupMenu_;

    bool useDarkTheme {false};
    details::CppImpl* cpp {nullptr};

    QMetaObject::Connection selection_updated;
    QMetaObject::Connection layout_changed;
    QMetaObject::Connection modelSortedConnection_;
    QMetaObject::Connection searchChangedConnection_;
    QMetaObject::Connection searchStatusChangedConnection_;
    QMetaObject::Connection conversationUpdatedConnection_;
    QMetaObject::Connection filterChangedConnection_;
    QMetaObject::Connection callChangedConnection_;
};

G_DEFINE_TYPE_WITH_PRIVATE(ConversationsView, conversations_view, GTK_TYPE_TREE_VIEW);

#define CONVERSATIONS_VIEW_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), CONVERSATIONS_VIEW_TYPE, ConversationsViewPrivate))

namespace { namespace details {

class CppImpl
{
public:
    explicit CppImpl();

    QString status;
};

CppImpl::CppImpl()
    : status("")
{}

}}

static void
render_contact_photo(G_GNUC_UNUSED GtkTreeViewColumn *tree_column,
                     GtkCellRenderer *cell,
                     GtkTreeModel *model,
                     GtkTreeIter *iter,
                     gpointer self)
{
    // Get active conversation
    auto path = gtk_tree_model_get_path(model, iter);
    auto priv = CONVERSATIONS_VIEW_GET_PRIVATE(self);
    if (!priv) return;
    gchar *uid;

    gtk_tree_model_get (model, iter,
                        0 /* col# */, &uid /* data */,
                        -1);


    auto convOpt = (*priv->accountInfo_)->conversationModel->getConversationForUid(uid);
    if (!convOpt) {
        g_warning("Can't get conversation %s", uid);
        g_free(uid);
        return;
    }

    // Draw first contact.
    // NOTE: We just draw the first contact, must change this for conferences when they will have their own object
    auto isPresent = false;
    auto isBanned = false;
    if (not convOpt->get().participants.empty()) {
        auto contactInfo = (*priv->accountInfo_)->contactModel->getContact(convOpt->get().participants.front());
        isPresent = contactInfo.isPresent;
        isBanned = contactInfo.isBanned;
    }
    std::shared_ptr<GdkPixbuf> image;
    auto var_photo = GlobalInstances::pixmapManipulator().conversationPhoto(
        convOpt->get(),
        **(priv->accountInfo_),
        QSize(50, 50),
        isPresent
    );
    image = var_photo.value<std::shared_ptr<GdkPixbuf>>();

    // set the width of the cell rendered to the width of the photo
    // so that the other renderers are shifted to the right
    g_object_set(G_OBJECT(cell), "width", 50, NULL);
    g_object_set(G_OBJECT(cell), "pixbuf", image.get(), NULL);

    // Banned contacts should be displayed with grey bg
    g_object_set(G_OBJECT(cell), "cell-background", isBanned ? "#BDBDBD" : NULL, NULL);

    g_free(uid);
}

static void
render_name_and_last_interaction(G_GNUC_UNUSED GtkTreeViewColumn *tree_column,
                                 GtkCellRenderer *cell,
                                 GtkTreeModel *model,
                                 GtkTreeIter *iter,
                                 GtkTreeView *treeview)
{
    gchar *alias;
    gchar *registeredName;
    gchar *lastInteraction;
    gchar *text;
    gchar *uid;
    gchar *uri;

    // Get active conversation
    auto path = gtk_tree_model_get_path(model, iter);
    auto row = std::atoi(gtk_tree_path_to_string(path));
    if (row == -1) return;

    auto priv = CONVERSATIONS_VIEW_GET_PRIVATE(treeview);
    if (!priv) return;

    auto grey = priv->useDarkTheme? "#bbb" : "#666";

    gtk_tree_model_get (model, iter,
                        0 /* col# */, &uid /* data */,
                        1 /* col# */, &alias /* data */,
                        2 /* col# */, &uri /* data */,
                        3 /* col# */, &registeredName /* data */,
                        5 /* col# */, &lastInteraction /* data */,
                        -1);

    auto bestId = std::string(registeredName).empty() ? uri: registeredName;

    auto convOpt = (*priv->accountInfo_)->conversationModel->getConversationForUid(uid);
    auto isBanned = false;
    if (not convOpt->get().participants.empty()) {
        auto contactUri = convOpt->get().participants.front();
        auto contactInfo = (*priv->accountInfo_)->contactModel->getContact(contactUri);
        isBanned = contactInfo.isBanned;
    }

    if (isBanned) {
        // Contact is banned, display it clearly
        text = g_markup_printf_escaped(
            "<span font_weight=\"bold\">%s</span>\n<span size=\"smaller\" font_weight=\"bold\">Banned contact</span>",
            bestId
        );
    } else if (std::string(alias).empty()) {
        // If no alias to show, use the best id
        text = g_markup_printf_escaped(
            "<span font_weight=\"bold\">%s</span>\n<span size=\"smaller\" color=\"%s\">%s</span>",
            bestId,
            grey,
            lastInteraction
        );
    } else if (std::string(alias) == std::string(bestId)) {
        // If the alias and the best id are identical, show only the alias
        text = g_markup_printf_escaped(
            "<span font_weight=\"bold\">%s</span>\n<span size=\"smaller\" color=\"%s\">%s</span>",
            alias,
            grey,
            lastInteraction
        );
    } else {
        // If the alias is not empty and not equals to the best id, show both the alias and the best id
        text = g_markup_printf_escaped(
            "<span font_weight=\"bold\">%s</span>\n<span size=\"smaller\" color=\"%s\">%s</span>\n<span size=\"smaller\" color=\"%s\">%s</span>",
            alias,
            grey,
            bestId,
            grey,
            lastInteraction
        );
    }

    // Banned contacts should be displayed with grey bg
    g_object_set(G_OBJECT(cell), "cell-background", isBanned ? "#BDBDBD" : NULL, NULL);

    g_object_set(G_OBJECT(cell), "markup", text, NULL);
    g_free(text);
    g_free(uid);
    g_free(uri);
    g_free(alias);
    g_free(registeredName);
}

static void
render_time(G_GNUC_UNUSED GtkTreeViewColumn *tree_column,
            GtkCellRenderer *cell,
            GtkTreeModel *model,
            GtkTreeIter *iter,
            G_GNUC_UNUSED GtkTreeView *treeview)
{
    g_return_if_fail(IS_CONVERSATIONS_VIEW(treeview));
    auto priv = CONVERSATIONS_VIEW_GET_PRIVATE(treeview);
    g_return_if_fail(priv);

    // Get active conversation
    auto path = gtk_tree_model_get_path(model, iter);
    auto row = std::atoi(gtk_tree_path_to_string(path));
    g_return_if_fail(row != -1);
    gchar *uid;

    gtk_tree_model_get (model, iter,
                        0 /* col# */, &uid /* data */,
                        -1);
    if (g_strcmp0(uid, "") == 0) {
        g_object_set(G_OBJECT(cell), "markup", "", NULL);
        g_free(uid);
        return;
    }

    try
    {
        // Draw first contact.
        // NOTE: We just draw the first contact, must change this for conferences when they will have their own object
        auto convOpt = (*priv->accountInfo_)->conversationModel->getConversationForUid(uid);
        if (convOpt->get().participants.empty()) return;
        auto contactUri = convOpt->get().participants.front();
        auto& contactInfo = (*priv->accountInfo_)->contactModel->getContact(contactUri);

        // Banned contacts should be displayed with grey bg
        g_object_set(G_OBJECT(cell), "cell-background", contactInfo.isBanned ? "#BDBDBD" : NULL, NULL);

        auto callId = convOpt->get().confId.isEmpty() ? convOpt->get().callId : convOpt->get().confId;
        if (!callId.isEmpty()) {
            auto call = (*priv->accountInfo_)->callModel->getCall(callId);
            if (call.status != lrc::api::call::Status::ENDED) {
                g_object_set(G_OBJECT(cell), "markup", qUtf8Printable(lrc::api::call::to_string(call.status)), NULL);
                g_free(uid);
                return;
            }
        }

        auto& interactions = convOpt->get().interactions;
        auto lastUid = convOpt->get().lastMessageUid;

        if (!interactions.empty() && interactions.find(lastUid) != interactions.end()) {
            std::time_t lastTimestamp = interactions[lastUid].timestamp;
            std::chrono::time_point<std::chrono::system_clock> lastTs = std::chrono::system_clock::from_time_t(lastTimestamp);
            std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();
            std::chrono::hours diff = std::chrono::duration_cast<std::chrono::hours>(now - lastTs);

            std::stringstream timestamp;
            timestamp << std::put_time(std::localtime(&lastTimestamp), diff.count() < 24 ? "%R" : "%x");
            gchar* text = g_markup_printf_escaped("<span size=\"smaller\" color=\"#666\">%s</span>", timestamp.str().c_str());
            g_object_set(G_OBJECT(cell), "markup", text, NULL);

            g_free(text);
            g_free(uid);
            return;
        }
    } catch (const std::exception&) {
        g_warning("Can't get conversation at row %i", row);
    }

    g_free(uid);
    g_object_set(G_OBJECT(cell), "markup", "", NULL);
}

void
update_conversation(ConversationsView *self, const std::string& uid) {
    auto priv = CONVERSATIONS_VIEW_GET_PRIVATE(self);
    auto model = gtk_tree_view_get_model (GTK_TREE_VIEW(self));

    auto idx = 0;
    auto iterIsCorrect = true;
    GtkTreeIter iter;

    while(iterIsCorrect) {
        iterIsCorrect = gtk_tree_model_iter_nth_child (model, &iter, nullptr, idx);
        if (!iterIsCorrect)
            break;
        gchar *uidModel;
        gtk_tree_model_get (model, &iter,
                            0 /* col# */, &uidModel /* data */,
                            -1);
        if(std::string(uid) == uidModel) {
            // Get informations
            auto convOpt = (*priv->accountInfo_)->conversationModel->getConversationForUid(uidModel);
            if (convOpt->get().participants.empty()) {
                g_free(uidModel);
                return;
            }
            auto contactUri = convOpt->get().participants.front();
            auto contactInfo = (*priv->accountInfo_)->contactModel->getContact(contactUri);
            auto lastMessage = convOpt->get().interactions.empty() ? "" :
                convOpt->get().interactions.at(convOpt->get().lastMessageUid).body;
            std::replace(lastMessage.begin(), lastMessage.end(), '\n', ' ');
            auto alias = contactInfo.profileInfo.alias;
            alias.remove('\r');
            // Update iter

            gtk_list_store_set (GTK_LIST_STORE(model), &iter,
                                0 /* col # */ , qUtf8Printable(convOpt->get().uid) /* celldata */,
                                1 /* col # */ , qUtf8Printable(alias) /* celldata */,
                                2 /* col # */ , qUtf8Printable(contactInfo.profileInfo.uri) /* celldata */,
                                3 /* col # */ , qUtf8Printable(contactInfo.registeredName) /* celldata */,
                                4 /* col # */ , qUtf8Printable(contactInfo.profileInfo.avatar) /* celldata */,
                                5 /* col # */ , qUtf8Printable(lastMessage) /* celldata */,
                                -1 /* end */);
            g_free(uidModel);
            return;
        }
        g_free(uidModel);
        idx++;
    }
}

static GtkTreeModel*
create_and_fill_model(ConversationsView *self)
{
    auto priv = CONVERSATIONS_VIEW_GET_PRIVATE(self);
    auto store = gtk_list_store_new (6 /* # of cols */ ,
                                     G_TYPE_STRING,
                                     G_TYPE_STRING,
                                     G_TYPE_STRING,
                                     G_TYPE_STRING,
                                     G_TYPE_STRING,
                                     G_TYPE_STRING,
                                     G_TYPE_UINT);
    if(!priv) GTK_TREE_MODEL (store);
    GtkTreeIter iter;

    if (priv->cpp && !priv->cpp->status.isEmpty()) {
        gtk_list_store_append (store, &iter);
        gtk_list_store_set (store, &iter,
                            0 /* col # */ , "" /* celldata */,
                            1 /* col # */ , qUtf8Printable(priv->cpp->status) /* celldata */,
                            2 /* col # */ , "" /* celldata */,
                            3 /* col # */ , "" /* celldata */,
                            4 /* col # */ , "" /* celldata */,
                            5 /* col # */ , "" /* celldata */,
                            -1 /* end */);
    }


    auto fillModelLambda = [&](const auto& queue) {
        for (const lrc::api::conversation::Info& conversation : queue) {
            if (conversation.participants.empty()) {
                g_debug("Found conversation with empty list of participants - most likely the result of earlier bug.");
                break;
            }

            auto contactUri = conversation.participants.front();
            try {
                auto contactInfo = (*priv->accountInfo_)->contactModel->getContact(contactUri);
                auto lastMessage = conversation.interactions.empty() ? "" :
                    conversation.interactions.at(conversation.lastMessageUid).body;
                std::replace(lastMessage.begin(), lastMessage.end(), '\n', ' ');
                gtk_list_store_append (store, &iter);
                auto alias = contactInfo.profileInfo.alias;
                alias.remove('\r');
                gtk_list_store_set (store, &iter,
                                    0 /* col # */ , qUtf8Printable(conversation.uid) /* celldata */,
                                    1 /* col # */ , qUtf8Printable(alias) /* celldata */,
                                    2 /* col # */ , qUtf8Printable(contactInfo.profileInfo.uri) /* celldata */,
                                    3 /* col # */ , qUtf8Printable(contactInfo.registeredName) /* celldata */,
                                    4 /* col # */ , qUtf8Printable(contactInfo.profileInfo.avatar) /* celldata */,
                                    5 /* col # */ , qUtf8Printable(lastMessage) /* celldata */,
                                    -1 /* end */);
            } catch (const std::out_of_range&) {
                // ContactModel::getContact() exception
            }
        }
    };

    fillModelLambda((*priv->accountInfo_)->conversationModel->getAllSearchResults());
    fillModelLambda((*priv->accountInfo_)->conversationModel->allFilteredConversations().get());

    return GTK_TREE_MODEL (store);
}

static void
call_conversation(GtkTreeView *self,
                  G_GNUC_UNUSED GtkTreePath *path,
                  G_GNUC_UNUSED GtkTreeViewColumn *column,
                  G_GNUC_UNUSED gpointer user_data)
{
    auto priv = CONVERSATIONS_VIEW_GET_PRIVATE(self);
    if (!priv) return;
    GtkTreeIter iter;
    GtkTreeModel *model = nullptr;
    gchar *uid = nullptr;

    auto selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(self));
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;

    gtk_tree_model_get(model, &iter,
                       0, &uid,
                       -1);

    (*priv->accountInfo_)->conversationModel->placeCall(uid);
    g_free(uid);
}

static void
refresh_popup_menu(ConversationsView *self)
{
    auto priv = CONVERSATIONS_VIEW_GET_PRIVATE(self);
    if (priv->popupMenu_) {
        // Because popup menu is not up to date, we need to update it.
        auto isVisible = gtk_widget_get_visible(priv->popupMenu_);
        // Destroy the not up to date menu.
        gtk_widget_hide(priv->popupMenu_);
        gtk_widget_destroy(priv->popupMenu_);
        priv->popupMenu_ = conversation_popup_menu_new(GTK_TREE_VIEW(self), *priv->accountInfo_);
        auto children = gtk_container_get_children (GTK_CONTAINER(priv->popupMenu_));
        auto nbItems = g_list_length(children);
        // Show the new popupMenu_ should be visible
        if (isVisible && nbItems > 0)
            gtk_menu_popup(GTK_MENU(priv->popupMenu_), nullptr, nullptr, nullptr, nullptr, 0, gtk_get_current_event_time());
    }
}

static void
select_conversation(GtkTreeSelection *selection, ConversationsView *self)
{
    auto priv = CONVERSATIONS_VIEW_GET_PRIVATE(self);
    refresh_popup_menu(self);
    GtkTreeIter iter;
    GtkTreeModel *model = nullptr;
    gchar *conversationUid = nullptr;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;

    gtk_tree_model_get(model, &iter,
                       0, &conversationUid,
                       -1);
    (*priv->accountInfo_)->conversationModel->selectConversation(QString(conversationUid));
}

static void
conversations_view_init(G_GNUC_UNUSED ConversationsView *self)
{
    // Nothing to do
}

static void
show_popup_menu(ConversationsView *self, GdkEventButton *event)
{
    auto priv = CONVERSATIONS_VIEW_GET_PRIVATE(self);
    auto children = gtk_container_get_children (GTK_CONTAINER(priv->popupMenu_));
    auto nbItems = g_list_length(children);
    // Show the new popupMenu_ should be visible
    if (nbItems > 0)
        conversation_popup_menu_show(CONVERSATION_POPUP_MENU(priv->popupMenu_), event);
}

static gboolean
on_drag_motion(GtkWidget      *treeview,
               GdkDragContext *context,
               gint            x,
               gint            y,
               guint           time,
               G_GNUC_UNUSED gpointer user_data)
{
    g_return_val_if_fail(IS_CONVERSATIONS_VIEW(treeview), FALSE);

    GtkTreePath *path = NULL;
    GtkTreeViewDropPosition drop_pos;

    if (gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(treeview),
                                          x, y, &path, &drop_pos)) {
        // we only want to drop on a row, not before or after
        if (drop_pos == GTK_TREE_VIEW_DROP_BEFORE) {
            gtk_tree_view_set_drag_dest_row(GTK_TREE_VIEW(treeview), path, GTK_TREE_VIEW_DROP_INTO_OR_BEFORE);
        } else if (drop_pos == GTK_TREE_VIEW_DROP_AFTER) {
            gtk_tree_view_set_drag_dest_row(GTK_TREE_VIEW(treeview), path, GTK_TREE_VIEW_DROP_INTO_OR_AFTER);
        }
        gdk_drag_status(context, gdk_drag_context_get_suggested_action(context), time);
        return TRUE;
    } else {
        // not a row in the treeview, so we cannot drop
        return FALSE;
    }
}

static void
on_drag_data_received(GtkWidget        *treeview,
                      GdkDragContext   *context,
                      gint              x,
                      gint              y,
                      GtkSelectionData *data,
                      guint             target_type,
                      guint             time,
                      G_GNUC_UNUSED gpointer user_data)
{
    g_return_if_fail(IS_CONVERSATIONS_VIEW(treeview));
    auto priv = CONVERSATIONS_VIEW_GET_PRIVATE(treeview);

    gboolean success = FALSE;

    /* get the source and destination calls */
    auto path_str_source = (gchar *)gtk_selection_data_get_data(data);
    auto type = gtk_selection_data_get_data_type(data);
    g_debug("data type: %s", gdk_atom_name(type));

    if (path_str_source && strlen(path_str_source) > 0) {
        g_debug("source path: %s", path_str_source);

        /* get the destination path */
        GtkTreePath *dest_path = nullptr;
        if (gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(treeview), x, y, &dest_path, NULL)) {
            auto model = gtk_tree_view_get_model(GTK_TREE_VIEW(treeview));

            switch (target_type) {
                case TEXT_URI_LIST_TARGET_ID: {
                    GtkTreeIter dest;
                    gtk_tree_model_get_iter(model, &dest, dest_path);
                    gchar *conversationUid = nullptr;
                    gtk_tree_model_get(model, &dest, 0, &conversationUid, -1);

                    foreach_file(path_str_source, [&](const char* file) {
                        (*priv->accountInfo_)->conversationModel->sendFile(
                            conversationUid, file, g_path_get_basename(file));
                    });

                    gtk_tree_path_free(dest_path);
                    g_free(conversationUid);
                    success = TRUE;
                    break;
                }
                default:
                    g_warning("unhandled drag and drop target type");
                    success = FALSE;
            }
        }
    }

    gtk_drag_finish(context, success, FALSE, time);
}

static void
build_conversations_view(ConversationsView *self)
{
    auto* priv = CONVERSATIONS_VIEW_GET_PRIVATE(self);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(self), FALSE);

    auto model = create_and_fill_model(self);
    gtk_tree_view_set_model(GTK_TREE_VIEW(self),
                            GTK_TREE_MODEL(model));
    g_object_unref(model);

    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(self), false);

    // ringId method column
    auto area = gtk_cell_area_box_new();
    auto column = gtk_tree_view_column_new_with_area(area);

    // render the photo
    auto renderer = gtk_cell_renderer_pixbuf_new();
    gtk_cell_area_box_pack_start(GTK_CELL_AREA_BOX(area), renderer, FALSE, FALSE, FALSE);

    gtk_tree_view_column_set_cell_data_func(
        column,
        renderer,
        (GtkTreeCellDataFunc)render_contact_photo,
        self,
        NULL);

    // render name and last interaction
    renderer = gtk_cell_renderer_text_new();
    g_object_set(G_OBJECT(renderer), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    gtk_cell_area_box_pack_start(GTK_CELL_AREA_BOX(area), renderer, FALSE, FALSE, FALSE);

    gtk_tree_view_column_set_cell_data_func(
        column,
        renderer,
        (GtkTreeCellDataFunc)render_name_and_last_interaction,
        self,
        NULL);

    // render time of last interaction and number of unread
    renderer = gtk_cell_renderer_text_new();
    g_object_set(G_OBJECT(renderer), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    g_object_set(G_OBJECT(renderer), "xalign", 1.0, NULL);
    gtk_cell_area_box_pack_start(GTK_CELL_AREA_BOX(area), renderer, FALSE, FALSE, FALSE);

    gtk_tree_view_column_set_cell_data_func(
        column,
        renderer,
        (GtkTreeCellDataFunc)render_time,
        self,
        NULL);

    gtk_tree_view_append_column(GTK_TREE_VIEW(self), column);

    // This view should be synchronized and redraw at each update.
    priv->modelSortedConnection_ = QObject::connect(
    &*(*priv->accountInfo_)->conversationModel,
    &lrc::api::ConversationModel::modelChanged,
    [self] () {
        auto model = create_and_fill_model(self);

        gtk_tree_view_set_model(GTK_TREE_VIEW(self),
                                GTK_TREE_MODEL(model));
        g_object_unref(model);
    });


    priv->searchChangedConnection_ = QObject::connect(
    &*(*priv->accountInfo_)->conversationModel,
    &lrc::api::ConversationModel::searchResultUpdated,
    [self] () {
        auto model = create_and_fill_model(self);

        gtk_tree_view_set_model(GTK_TREE_VIEW(self),
                                GTK_TREE_MODEL(model));
        g_object_unref(model);
    });
    priv->searchStatusChangedConnection_ = QObject::connect(
    &*(*priv->accountInfo_)->conversationModel,
    &lrc::api::ConversationModel::searchStatusChanged,
    [self] (const QString& status) {
        auto priv = CONVERSATIONS_VIEW_GET_PRIVATE(self);
        if (!priv or !priv->cpp) return;
        priv->cpp->status = status;
        if (status.isEmpty()) return;

        auto model = gtk_tree_view_get_model(GTK_TREE_VIEW(self));
        GtkTreeIter iter;

        auto addItem = true;
        if (gtk_tree_model_iter_children(model, &iter, nullptr)) {
            gchar *conversationUid = nullptr;
            gtk_tree_model_get(model, &iter, 0, &conversationUid, -1);
            if (g_strcmp0(conversationUid, "") == 0) {
                addItem = false;
                gtk_list_store_set (GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(self))), &iter,
                                    0 /* col # */ , "" /* celldata */,
                                    1 /* col # */ , qUtf8Printable(status) /* celldata */,
                                    2 /* col # */ , "" /* celldata */,
                                    3 /* col # */ , "" /* celldata */,
                                    4 /* col # */ , "" /* celldata */,
                                    5 /* col # */ , "" /* celldata */,
                                    -1 /* end */);
            }
            g_free(conversationUid);
        }

        if (addItem) {
            gtk_list_store_append (GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(self))), &iter);
            gtk_list_store_set (GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(self))), &iter,
                                0 /* col # */ , "" /* celldata */,
                                1 /* col # */ , qUtf8Printable(status) /* celldata */,
                                2 /* col # */ , "" /* celldata */,
                                3 /* col # */ , "" /* celldata */,
                                4 /* col # */ , "" /* celldata */,
                                5 /* col # */ , "" /* celldata */,
                                -1 /* end */);
        }

    });
    priv->conversationUpdatedConnection_ = QObject::connect(
    &*(*priv->accountInfo_)->conversationModel,
    &lrc::api::ConversationModel::conversationUpdated,
    [self] (const QString& uid) {
        update_conversation(self, uid.toStdString());
    });

    priv->filterChangedConnection_ = QObject::connect(
    &*(*priv->accountInfo_)->conversationModel,
    &lrc::api::ConversationModel::filterChanged,
    [self] () {
        auto model = create_and_fill_model(self);

        gtk_tree_view_set_model(GTK_TREE_VIEW(self),
                                GTK_TREE_MODEL(model));
        g_object_unref(model);
    });

    priv->callChangedConnection_ = QObject::connect(
    &*(*priv->accountInfo_)->callModel,
    &lrc::api::NewCallModel::callStatusChanged,
    [self, priv] (const QString&) {
        // retrieve currently selected conversation
        GtkTreeIter iter;
        GtkTreeModel *model = nullptr;
        gchar *conversationUid = nullptr;

        auto selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(self));
        if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;
        gtk_tree_model_get(model, &iter, 0, &conversationUid, -1);

        // create updated model
        auto new_model = create_and_fill_model(self);
        gtk_tree_view_set_model(GTK_TREE_VIEW(self), GTK_TREE_MODEL(new_model));
        g_object_unref(new_model);

        // make sure conversation remains selected
        conversations_view_select_conversation(self, conversationUid);

        g_free(conversationUid);
    });

    gtk_widget_show_all(GTK_WIDGET(self));

    auto selectionNew = gtk_tree_view_get_selection(GTK_TREE_VIEW(self));
    // One left click to select the conversation
    g_signal_connect(selectionNew, "changed", G_CALLBACK(select_conversation), self);
    // Two clicks to placeCall
    g_signal_connect(self, "row-activated", G_CALLBACK(call_conversation), NULL);

    priv->popupMenu_ = conversation_popup_menu_new(GTK_TREE_VIEW(self), *priv->accountInfo_);
    // Right click to show actions
    g_signal_connect_swapped(self, "button-press-event", G_CALLBACK(show_popup_menu), self);

    /* drag and drop */
    static GtkTargetEntry target_entries[] = {
        { (gchar *)TEXT_URI_LIST_TARGET, GTK_TARGET_OTHER_APP, TEXT_URI_LIST_TARGET_ID },
    };

    gtk_tree_view_enable_model_drag_dest(GTK_TREE_VIEW(self),
        target_entries, 1,
        (GdkDragAction)(GDK_ACTION_DEFAULT | GDK_ACTION_COPY));

    g_signal_connect(self, "drag-motion", G_CALLBACK(on_drag_motion), nullptr);
    g_signal_connect(self, "drag-data-received", G_CALLBACK(on_drag_data_received), nullptr);

    priv->cpp = new details::CppImpl();
}

static void
conversations_view_dispose(GObject *object)
{
    auto self = CONVERSATIONS_VIEW(object);
    auto priv = CONVERSATIONS_VIEW_GET_PRIVATE(self);

    QObject::disconnect(priv->selection_updated);
    QObject::disconnect(priv->layout_changed);
    QObject::disconnect(priv->modelSortedConnection_);
    QObject::disconnect(priv->searchChangedConnection_);
    QObject::disconnect(priv->searchStatusChangedConnection_);
    QObject::disconnect(priv->conversationUpdatedConnection_);
    QObject::disconnect(priv->filterChangedConnection_);
    QObject::disconnect(priv->callChangedConnection_);

    gtk_widget_destroy(priv->popupMenu_);
    delete priv->cpp;
    priv->cpp = nullptr;

    G_OBJECT_CLASS(conversations_view_parent_class)->dispose(object);
}

static void
conversations_view_finalize(GObject *object)
{
    G_OBJECT_CLASS(conversations_view_parent_class)->finalize(object);
}

static void
conversations_view_class_init(ConversationsViewClass *klass)
{
    G_OBJECT_CLASS(klass)->finalize = conversations_view_finalize;
    G_OBJECT_CLASS(klass)->dispose = conversations_view_dispose;
}

GtkWidget *
conversations_view_new(AccountInfoPointer const & accountInfo)
{
    auto self = CONVERSATIONS_VIEW(g_object_new(CONVERSATIONS_VIEW_TYPE, NULL));
    auto priv = CONVERSATIONS_VIEW_GET_PRIVATE(self);

    priv->accountInfo_ = &accountInfo;

    if (*priv->accountInfo_) {
        build_conversations_view(self);
    } else {
        g_debug("building conversationsview for inexistant account (just removed ?)");
    }

    return (GtkWidget *)self;
}

/**
 * Select a conversation by uid (used to synchronize the selection)
 * @param self
 * @param uid of the conversation
 */
void
conversations_view_select_conversation(ConversationsView *self, const std::string& uid)
{
    auto idx = 0;
    auto model = gtk_tree_view_get_model (GTK_TREE_VIEW(self));
    auto iterIsCorrect = true;
    GtkTreeIter iter;

    while(iterIsCorrect) {
        iterIsCorrect = gtk_tree_model_iter_nth_child (model, &iter, nullptr, idx);
        if (!iterIsCorrect)
            break;
        gchar *ringId;
        gtk_tree_model_get (model, &iter,
                            0 /* col# */, &ringId /* data */,
                            -1);
        if(std::string(ringId) == uid) {
            auto selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(self));
            gtk_tree_selection_select_iter(selection, &iter);
            refresh_popup_menu(self);
            g_free(ringId);
            return;
        }
        g_free(ringId);
        idx++;
    }
}

std::string
conversations_view_get_current_selected(ConversationsView *self)
{
    g_return_val_if_fail(IS_CONVERSATIONS_VIEW(self), "");

    /* we always drag the selected row */
    auto selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(self));
    GtkTreeModel *model = NULL;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar* uid;
        gtk_tree_model_get(model, &iter,
                        0, &uid,
                        -1);
        if (!uid) return {};
        std::string result = uid;
        g_free(uid);
        return result;
    }
    return {};
}

void
conversations_view_set_theme(ConversationsView *self, bool darkTheme) {
    g_return_if_fail(IS_CONVERSATIONS_VIEW(self));
    auto priv = CONVERSATIONS_VIEW_GET_PRIVATE(self);
    priv->useDarkTheme = darkTheme;
}

