/*
 *  Copyright (C) 2016-2018 Savoir-faire Linux Inc.
 *  Author: Stepan Salenikovich <stepan.salenikovich@savoirfairelinux.com>
 *  Author: Alexandre Viau <alexandre.viau@savoirfairelinux.com>
 *  Author: Nicolas Jäger <nicolas.jager@savoirfairelinux.com>
 *  Author: Sébastien Blin <sebastien.blin@savoirfairelinux.com>
 *  Author: Hugo Lefeuvre <hugo.lefeuvre@savoirfairelinux.com>
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

#include "chatview.h"

// std
#include <algorithm>

// GTK
#include <glib/gi18n.h>

// LRC
#include <api/contactmodel.h>
#include <api/conversationmodel.h>
#include <api/contact.h>

// Client
#include "utils/files.h"

struct _ChatView
{
    GtkBox parent;
};

struct _ChatViewClass
{
    GtkBoxClass parent_class;
};

typedef struct _ChatViewPrivate ChatViewPrivate;

struct _ChatViewPrivate
{
    GtkWidget *box_webkit_chat_container;
    GtkWidget *webkit_chat_container;
    GtkWidget *hbox_chat_info;
    GtkWidget *label_peer;
    GtkWidget *label_cm;
    GtkWidget *button_close_chatview;
    GtkWidget *button_placecall;
    GtkWidget *button_add_to_conversations;
    GtkWidget *button_place_audio_call;

    GSettings *settings;

    lrc::api::conversation::Info* conversation_;
    AccountInfoPointer const * accountInfo_;
    bool isTemporary_;

    QMetaObject::Connection new_interaction_connection;
    QMetaObject::Connection update_interaction_connection;
    QMetaObject::Connection update_add_to_conversations;

    gulong webkit_ready;
    gulong webkit_send_text;
};

G_DEFINE_TYPE_WITH_PRIVATE(ChatView, chat_view, GTK_TYPE_BOX);

#define CHAT_VIEW_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), CHAT_VIEW_TYPE, ChatViewPrivate))

enum {
    NEW_MESSAGES_DISPLAYED,
    HIDE_VIEW_CLICKED,
    LAST_SIGNAL
};

static guint chat_view_signals[LAST_SIGNAL] = { 0 };

static void
chat_view_dispose(GObject *object)
{
    ChatView *view;
    ChatViewPrivate *priv;

    view = CHAT_VIEW(object);
    priv = CHAT_VIEW_GET_PRIVATE(view);

    QObject::disconnect(priv->new_interaction_connection);
    QObject::disconnect(priv->update_interaction_connection);
    QObject::disconnect(priv->update_add_to_conversations);

    /* Destroying the box will also destroy its children, and we wouldn't
     * want that. So we remove the webkit_chat_container from the box. */
    if (priv->webkit_chat_container) {
        /* disconnect for webkit signals */
        g_signal_handler_disconnect(priv->webkit_chat_container, priv->webkit_ready);
        priv->webkit_ready = 0;
        g_signal_handler_disconnect(priv->webkit_chat_container, priv->webkit_send_text);
        priv->webkit_send_text = 0;

        gtk_container_remove(
            GTK_CONTAINER(priv->box_webkit_chat_container),
            GTK_WIDGET(priv->webkit_chat_container)
        );
        priv->webkit_chat_container = nullptr;
    }

    G_OBJECT_CLASS(chat_view_parent_class)->dispose(object);
}

static void
hide_chat_view(G_GNUC_UNUSED GtkWidget *widget, ChatView *self)
{
    g_signal_emit(G_OBJECT(self), chat_view_signals[HIDE_VIEW_CLICKED], 0);
}

static void
display_links_toggled(ChatView *self)
{
    auto priv = CHAT_VIEW_GET_PRIVATE(self);
    if (priv->webkit_chat_container) {
        webkit_chat_container_set_display_links(
            WEBKIT_CHAT_CONTAINER(priv->webkit_chat_container),
            g_settings_get_boolean(priv->settings, "enable-display-links")
        );
    }
}

static void
placecall_clicked(ChatView *self)
{
    auto priv = CHAT_VIEW_GET_PRIVATE(self);
    if (!priv->conversation_) return;
    (*priv->accountInfo_)->conversationModel->placeCall(priv->conversation_->uid);
}

static void
place_audio_call_clicked(ChatView *self)
{
    auto priv = CHAT_VIEW_GET_PRIVATE(self);

    if (!priv->conversation_)
        return;

    (*priv->accountInfo_)->conversationModel->placeAudioOnlyCall(priv->conversation_->uid);
}

static void
button_add_to_conversations_clicked(ChatView *self)
{
    auto priv = CHAT_VIEW_GET_PRIVATE(self);
    if (!priv->conversation_) return;
    (*priv->accountInfo_)->conversationModel->makePermanent(priv->conversation_->uid);
}

static gchar*
file_to_manipulate(GtkWindow* top_window, bool send)
{
    GtkWidget* dialog;
    GtkFileChooserAction action = send? GTK_FILE_CHOOSER_ACTION_OPEN : GTK_FILE_CHOOSER_ACTION_SAVE;
    gint res;
    gchar* filename = nullptr;

    dialog = gtk_file_chooser_dialog_new(send? _("Send File") : _("Save File"),
                                         top_window,
                                         action,
                                         _("_Cancel"),
                                         GTK_RESPONSE_CANCEL,
                                         send? _("_Open"): _("_Save"),
                                         GTK_RESPONSE_ACCEPT,
                                         nullptr);

    res = gtk_dialog_run (GTK_DIALOG(dialog));

    if (res == GTK_RESPONSE_ACCEPT) {
        auto chooser = GTK_FILE_CHOOSER(dialog);
        filename = gtk_file_chooser_get_filename(chooser);
    }
    gtk_widget_destroy (dialog);

    return filename;
}

static void
webkit_chat_container_script_dialog(GtkWidget* webview, gchar *interaction, ChatView* self)
{
    auto priv = CHAT_VIEW_GET_PRIVATE(self);
    auto order = std::string(interaction);
    if (!priv->conversation_) return;
    if (order == "ACCEPT") {
        (*priv->accountInfo_)->conversationModel->makePermanent(priv->conversation_->uid);
    } else if (order == "REFUSE") {
        (*priv->accountInfo_)->conversationModel->removeConversation(priv->conversation_->uid);
    } else if (order == "BLOCK") {
        (*priv->accountInfo_)->conversationModel->removeConversation(priv->conversation_->uid, true);
    } else if (order.find("PLACE_CALL") == 0) {
        placecall_clicked(self);
    } else if (order.find("PLACE_AUDIO_CALL") == 0) {
        place_audio_call_clicked(self);
    } else if (order.find("CLOSE_CHATVIEW") == 0) {
        hide_chat_view(webview, self);
    } else if (order.find("SEND:") == 0) {
        // Get text body
        auto toSend = order.substr(std::string("SEND:").size());
        (*priv->accountInfo_)->conversationModel->sendMessage(priv->conversation_->uid, toSend);
    } else if (order.find("SEND_FILE") == 0) {
        if (auto model = (*priv->accountInfo_)->conversationModel.get()) {
            if (auto filename = file_to_manipulate(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(self))), true))
                model->sendFile(priv->conversation_->uid, filename, g_path_get_basename(filename));
        }
    } else if (order.find("ACCEPT_FILE:") == 0) {
        if (auto model = (*priv->accountInfo_)->conversationModel.get()) {
            try {
                auto interactionId = std::stoull(order.substr(std::string("ACCEPT_FILE:").size()));

                lrc::api::datatransfer::Info info = {};
                (*priv->accountInfo_)->conversationModel->getTransferInfo(interactionId, info);

                // get prefered directory destination.
                auto* download_directory_variant = g_settings_get_value(priv->settings, "download-folder");
                char* download_directory_value;
                g_variant_get(download_directory_variant, "&s", &download_directory_value);
                std::string default_download_dir = g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD);
                auto current_value = std::string(download_directory_value);
                if (current_value.empty()) {
                    g_settings_set_value(priv->settings, "download-folder", g_variant_new("s", default_download_dir.c_str()));
                }
                // get full path
                std::string filename = current_value.empty()? default_download_dir.c_str() : download_directory_value;
                if (!filename.empty() && filename.back() != '/') filename += "/";
                filename += info.displayName;

                model->acceptTransfer(priv->conversation_->uid, interactionId, filename);
            } catch (...) {
                // ignore
            }
        }
    } else if (order.find("REFUSE_FILE:") == 0) {
        if (auto model = (*priv->accountInfo_)->conversationModel.get()) {
            try {
                auto interactionId = std::stoull(order.substr(std::string("REFUSE_FILE:").size()));
                model->cancelTransfer(priv->conversation_->uid, interactionId);
            } catch (...) {
                // ignore
            }
        }
    } else if (order.find("OPEN_FILE:") == 0) {
        // Get text body
        auto filename {"file://" + order.substr(std::string("OPEN_FILE:").size())};
        filename.erase(std::find_if(filename.rbegin(), filename.rend(),
            std::not1(std::ptr_fun<int, int>(std::isspace))).base(), filename.end());
        GError* error = nullptr;
        if (!gtk_show_uri(nullptr, filename.c_str(), GDK_CURRENT_TIME, &error)) {
            g_debug("Could not open file: %s", error->message);
            g_error_free(error);
        }
    } else if (order.find("ADD_TO_CONVERSATIONS") == 0) {
        button_add_to_conversations_clicked(self);
    }
}

static void
chat_view_init(ChatView *view)
{
    gtk_widget_init_template(GTK_WIDGET(view));

    ChatViewPrivate *priv = CHAT_VIEW_GET_PRIVATE(view);
    priv->settings = g_settings_new_full(get_ring_schema(), NULL, NULL);

    g_signal_connect(priv->button_close_chatview, "clicked", G_CALLBACK(hide_chat_view), view);
    g_signal_connect_swapped(priv->button_placecall, "clicked", G_CALLBACK(placecall_clicked), view);
    g_signal_connect_swapped(priv->button_add_to_conversations, "clicked", G_CALLBACK(button_add_to_conversations_clicked), view);
    g_signal_connect_swapped(priv->button_place_audio_call, "clicked", G_CALLBACK(place_audio_call_clicked), view);
}

static void
chat_view_class_init(ChatViewClass *klass)
{
    G_OBJECT_CLASS(klass)->dispose = chat_view_dispose;

    gtk_widget_class_set_template_from_resource(GTK_WIDGET_CLASS (klass),
                                                "/cx/ring/RingGnome/chatview.ui");

    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), ChatView, box_webkit_chat_container);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), ChatView, hbox_chat_info);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), ChatView, label_peer);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), ChatView, label_cm);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), ChatView, button_close_chatview);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), ChatView, button_placecall);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), ChatView, button_add_to_conversations);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), ChatView, button_place_audio_call);

    chat_view_signals[NEW_MESSAGES_DISPLAYED] = g_signal_new (
        "new-interactions-displayed",
        G_TYPE_FROM_CLASS(klass),
        (GSignalFlags) (G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION),
        0,
        nullptr,
        nullptr,
        g_cclosure_marshal_VOID__VOID,
        G_TYPE_NONE, 0);

    chat_view_signals[HIDE_VIEW_CLICKED] = g_signal_new (
        "hide-view-clicked",
        G_TYPE_FROM_CLASS(klass),
        (GSignalFlags) (G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION),
        0,
        nullptr,
        nullptr,
        g_cclosure_marshal_VOID__VOID,
        G_TYPE_NONE, 0);
}

static void
print_interaction_to_buffer(ChatView* self, uint64_t interactionId, const lrc::api::interaction::Info& interaction)
{
    ChatViewPrivate *priv = CHAT_VIEW_GET_PRIVATE(self);

    if (!priv->conversation_) return;
    if (interaction.status == lrc::api::interaction::Status::UNREAD)
        (*priv->accountInfo_)->conversationModel->setInteractionRead(priv->conversation_->uid, interactionId);

    webkit_chat_container_print_new_interaction(
        WEBKIT_CHAT_CONTAINER(priv->webkit_chat_container),
        *(*priv->accountInfo_)->conversationModel,
        interactionId,
        interaction
    );
}

static void
update_interaction(ChatView* self, uint64_t interactionId, const lrc::api::interaction::Info& interaction)
{
    ChatViewPrivate *priv = CHAT_VIEW_GET_PRIVATE(self);
    webkit_chat_container_update_interaction(
        WEBKIT_CHAT_CONTAINER(priv->webkit_chat_container),
        *(*priv->accountInfo_)->conversationModel,
        interactionId,
        interaction
    );
}

static void
load_participants_images(ChatView *self)
{
    g_return_if_fail(IS_CHAT_VIEW(self));
    ChatViewPrivate *priv = CHAT_VIEW_GET_PRIVATE(self);

    // Contact
    if (!priv->conversation_) return;
    auto contactUri = priv->conversation_->participants.front();
    try{
        auto& contact = (*priv->accountInfo_)->contactModel->getContact(contactUri);
        if (!contact.profileInfo.avatar.empty()) {
            webkit_chat_container_set_sender_image(
                WEBKIT_CHAT_CONTAINER(priv->webkit_chat_container),
                (*priv->accountInfo_)->contactModel->getContactProfileId(contactUri),
                contact.profileInfo.avatar
                );
        }
    } catch (const std::out_of_range&) {
        // ContactModel::getContact() exception
    }

    // For this account
    if (!(*priv->accountInfo_)->profileInfo.avatar.empty()) {
        webkit_chat_container_set_sender_image(
            WEBKIT_CHAT_CONTAINER(priv->webkit_chat_container),
            (*priv->accountInfo_)->contactModel->getContactProfileId((*priv->accountInfo_)->profileInfo.uri),
            (*priv->accountInfo_)->profileInfo.avatar
        );
    }
}

static void
print_text_recording(ChatView *self)
{
    g_return_if_fail(IS_CHAT_VIEW(self));
    ChatViewPrivate *priv = CHAT_VIEW_GET_PRIVATE(self);

    // Read interactions
    if (!priv->conversation_) return;
    for (const auto& it: priv->conversation_->interactions) {
        if (it.second.status == lrc::api::interaction::Status::UNREAD)
            (*priv->accountInfo_)->conversationModel->setInteractionRead(priv->conversation_->uid, it.first);
    }

    webkit_chat_container_print_history(
        WEBKIT_CHAT_CONTAINER(priv->webkit_chat_container),
        *(*priv->accountInfo_)->conversationModel,
        priv->conversation_->interactions
    );

    QObject::disconnect(priv->new_interaction_connection);
}

static void
update_if_banned(ChatView *self)
{
    ChatViewPrivate *priv = CHAT_VIEW_GET_PRIVATE(self);

    if (!priv->conversation_) return;
    auto participant = priv->conversation_->participants[0];
    try {
        auto contactInfo = (*priv->accountInfo_)->contactModel->getContact(participant);
        if (contactInfo.isBanned) {
            webkit_chat_enable_banned(WEBKIT_CHAT_CONTAINER(priv->webkit_chat_container), true);
        } else {
            webkit_chat_enable_banned(WEBKIT_CHAT_CONTAINER(priv->webkit_chat_container), false);
        }
    } catch (const std::out_of_range&) {
        // ContactModel::getContact() exception
    }
}

static void
update_add_to_conversations(ChatView *self)
{
    ChatViewPrivate *priv = CHAT_VIEW_GET_PRIVATE(self);

    if (!priv->conversation_) return;
    auto participant = priv->conversation_->participants[0];
    try {
        auto contactInfo = (*priv->accountInfo_)->contactModel->getContact(participant);
        bool notAContact = contactInfo.profileInfo.type == lrc::api::profile::Type::TEMPORARY || contactInfo.profileInfo.type == lrc::api::profile::Type::PENDING;
        webkit_chat_show_add_to_conversations(WEBKIT_CHAT_CONTAINER(priv->webkit_chat_container), notAContact);
    } catch (const std::out_of_range&) {
        // ContactModel::getContact() exception
    }
}

static void
update_contact_methods(ChatView *self)
{
    g_return_if_fail(IS_CHAT_VIEW(self));
    ChatViewPrivate *priv = CHAT_VIEW_GET_PRIVATE(self);
    if (!priv->conversation_) return;
    auto contactUri = priv->conversation_->participants.front();
    try {
        auto contactInfo = (*priv->accountInfo_)->contactModel->getContact(contactUri);
        auto bestId = std::string(contactInfo.registeredName).empty() ? contactInfo.profileInfo.uri : contactInfo.registeredName;
        if (contactInfo.profileInfo.alias == bestId) {
            gtk_widget_hide(priv->label_cm);
        } else {
            gtk_label_set_text(GTK_LABEL(priv->label_cm), bestId.c_str());
            gtk_widget_show(priv->label_cm);
        }
    } catch (const std::out_of_range&) {
        // ContactModel::getContact() exception
    }
}

static void
update_name(ChatView *self)
{
    g_return_if_fail(IS_CHAT_VIEW(self));
    ChatViewPrivate *priv = CHAT_VIEW_GET_PRIVATE(self);
    if (!priv->conversation_) return;
    auto contactUri = priv->conversation_->participants.front();
    try {
        auto contactInfo = (*priv->accountInfo_)->contactModel->getContact(contactUri);
        auto alias = contactInfo.profileInfo.alias;
        auto bestName = contactInfo.registeredName;
        if (bestName.empty())
            bestName = contactInfo.profileInfo.uri;
        bestName.erase(std::remove(bestName.begin(), bestName.end(), '\r'), bestName.end());
        alias.erase(std::remove(alias.begin(), alias.end(), '\r'), alias.end());
        webkit_chat_container_update_name(WEBKIT_CHAT_CONTAINER(priv->webkit_chat_container), alias.c_str(), bestName.c_str());
    } catch (const std::out_of_range&) {
        // ContactModel::getContact() exception
    }
}

static void
webkit_chat_container_ready(ChatView* self)
{
    /* The webkit chat container has loaded the javascript libraries, we can
     * now use it. */

    ChatViewPrivate *priv = CHAT_VIEW_GET_PRIVATE(self);

    webkit_chat_container_clear(
        WEBKIT_CHAT_CONTAINER(priv->webkit_chat_container)
    );

    display_links_toggled(self);
    print_text_recording(self);
    load_participants_images(self);

    priv->new_interaction_connection = QObject::connect(
    &*(*priv->accountInfo_)->conversationModel, &lrc::api::ConversationModel::newInteraction,
    [self, priv](const std::string& uid, uint64_t interactionId, lrc::api::interaction::Info interaction) {
        if (!priv->conversation_) return;
        if(uid == priv->conversation_->uid) {
            print_interaction_to_buffer(self, interactionId, interaction);
        }
    });

    priv->update_interaction_connection = QObject::connect(
    &*(*priv->accountInfo_)->conversationModel, &lrc::api::ConversationModel::interactionStatusUpdated,
    [self, priv](const std::string& uid, uint64_t msgId, lrc::api::interaction::Info msg) {
        if (!priv->conversation_) return;
        if(uid == priv->conversation_->uid) {
            update_interaction(self, msgId, msg);
        }
    });

    if (!priv->conversation_) return;
    auto contactUri = priv->conversation_->participants.front();
    try {
        auto contactInfo = (*priv->accountInfo_)->contactModel->getContact(contactUri);
        priv->isTemporary_ = contactInfo.profileInfo.type == lrc::api::profile::Type::TEMPORARY
            || contactInfo.profileInfo.type == lrc::api::profile::Type::PENDING;
        webkit_chat_container_set_temporary(WEBKIT_CHAT_CONTAINER(priv->webkit_chat_container), priv->isTemporary_);
        auto bestName = contactInfo.profileInfo.alias;
        if (bestName.empty())
            bestName = contactInfo.registeredName;
        if (bestName.empty())
            bestName = contactInfo.profileInfo.uri;
        bestName.erase(std::remove(bestName.begin(), bestName.end(), '\r'), bestName.end());
        webkit_chat_container_set_invitation(WEBKIT_CHAT_CONTAINER(priv->webkit_chat_container),
                                             (contactInfo.profileInfo.type == lrc::api::profile::Type::PENDING),
                                             bestName);
        webkit_chat_disable_send_interaction(WEBKIT_CHAT_CONTAINER(priv->webkit_chat_container),
                                             (contactInfo.profileInfo.type == lrc::api::profile::Type::SIP)
                                             && priv->conversation_->callId.empty());
    } catch (const std::out_of_range&) {
        // ContactModel::getContact() exception
    }
}

static void
build_chat_view(ChatView* self)
{
    ChatViewPrivate *priv = CHAT_VIEW_GET_PRIVATE(self);

    gtk_container_add(GTK_CONTAINER(priv->box_webkit_chat_container), priv->webkit_chat_container);
    gtk_widget_show(priv->webkit_chat_container);

    update_name(self);
    update_add_to_conversations(self);
    update_contact_methods(self);
    update_if_banned(self);

    priv->webkit_ready = g_signal_connect_swapped(
        priv->webkit_chat_container,
        "ready",
        G_CALLBACK(webkit_chat_container_ready),
        self
    );

    priv->webkit_send_text = g_signal_connect(priv->webkit_chat_container,
        "script-dialog",
        G_CALLBACK(webkit_chat_container_script_dialog),
        self);

    if (webkit_chat_container_is_ready(WEBKIT_CHAT_CONTAINER(priv->webkit_chat_container)))
        webkit_chat_container_ready(self);

    //gtk_widget_set_visible(priv->hbox_chat_info, TRUE);
}

GtkWidget *
chat_view_new (WebKitChatContainer* webkit_chat_container,
               AccountInfoPointer const & accountInfo,
               lrc::api::conversation::Info* conversation)
{
    ChatView *self = CHAT_VIEW(g_object_new(CHAT_VIEW_TYPE, NULL));

    ChatViewPrivate *priv = CHAT_VIEW_GET_PRIVATE(self);
    priv->webkit_chat_container = GTK_WIDGET(webkit_chat_container);
    priv->conversation_ = conversation;
    priv->accountInfo_ = &accountInfo;

    build_chat_view(self);
    return (GtkWidget *)self;
}

void
chat_view_update_temporary(ChatView* self, bool showAddButton, bool showInvitation)
{
    g_return_if_fail(IS_CHAT_VIEW(self));
    auto priv = CHAT_VIEW_GET_PRIVATE(self);

    priv->isTemporary_ = showAddButton;
    if (!priv->isTemporary_) {
        webkit_chat_show_add_to_conversations(WEBKIT_CHAT_CONTAINER(priv->webkit_chat_container), false);
    }
    webkit_chat_container_set_temporary(WEBKIT_CHAT_CONTAINER(priv->webkit_chat_container), priv->isTemporary_);
    if (!priv->conversation_) return;
    auto contactUri = priv->conversation_->participants.front();
    try {
        auto contactInfo = (*priv->accountInfo_)->contactModel->getContact(contactUri);
        auto bestName = contactInfo.profileInfo.alias;
        if (bestName.empty())
            bestName = contactInfo.registeredName;
        if (bestName.empty())
            bestName = contactInfo.profileInfo.uri;
        webkit_chat_container_set_invitation(WEBKIT_CHAT_CONTAINER(priv->webkit_chat_container),
                                             showInvitation,
                                             bestName);
    } catch (const std::out_of_range&) {
        // ContactModel::getContact() exception
    }
}

bool
chat_view_get_temporary(ChatView *self)
{
    g_return_val_if_fail(IS_CHAT_VIEW(self), false);
    auto priv = CHAT_VIEW_GET_PRIVATE(self);
    return priv->isTemporary_;
}

lrc::api::conversation::Info
chat_view_get_conversation(ChatView *self)
{
    g_return_val_if_fail(IS_CHAT_VIEW(self), lrc::api::conversation::Info());
    auto priv = CHAT_VIEW_GET_PRIVATE(self);
    return *priv->conversation_;
}

void
chat_view_set_header_visible(ChatView *self, gboolean visible)
{
    auto priv = CHAT_VIEW_GET_PRIVATE(self);
    //gtk_widget_set_visible(priv->hbox_chat_info, visible);
}
