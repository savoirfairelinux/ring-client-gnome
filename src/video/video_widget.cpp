/*
 *  Copyright (C) 2004-2015 Savoir-Faire Linux Inc.
 *  Author: Sebastien Bourdelin <sebastien.bourdelin@savoirfairelinux.com>
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
 *
 *  Additional permission under GNU GPL version 3 section 7:
 *
 *  If you modify this program, or any covered work, by linking or
 *  combining it with the OpenSSL project's OpenSSL library (or a
 *  modified version of that library), containing parts covered by the
 *  terms of the OpenSSL or SSLeay licenses, Savoir-Faire Linux Inc.
 *  grants you additional permission to convey the resulting work.
 *  Corresponding Source for a non-source form of such a combination
 *  shall include the source code for the parts of OpenSSL used as well
 *  as that of the covered work.
 */

#include "video_widget.h"

#include "video_aspect_frame.h"
#include <clutter/clutter.h>
#include <clutter-gtk/clutter-gtk.h>
#include <video/renderer.h>

struct _VideoWidgetClass {
    GtkBinClass parent_class;
};

struct _VideoWidget {
    GtkBin parent;
};

typedef struct _VideoWidgetPrivate VideoWidgetPrivate;

struct _VideoWidgetPrivate {
    GtkWidget               *clutter_widget;
    ClutterActor            *stage;
    ClutterActor            *video_container;

    /* remote peer data */
    ClutterActor            *actor_remote;
    Video::Renderer         *renderer_remote;
    QMetaObject::Connection  remote_frame_update;

    /* local peer data */
    ClutterActor            *actor_local;
    Video::Renderer         *renderer_local;
    QMetaObject::Connection  local_frame_update;

    gboolean first_draw;
};

G_DEFINE_TYPE_WITH_PRIVATE(VideoWidget, video_widget, GTK_TYPE_BIN);

#define VIDEO_WIDGET_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), VIDEO_WIDGET_TYPE, VideoWidgetPrivate))

typedef struct _FrameInfo
{
    ClutterActor   *image_actor;
    // ClutterContent *image;
    guchar         *data;
    gint            data_size;
    gint            width;
    gint            height;
} FrameInfo;

/*
 * video_widget_dispose()
 *
 * The dispose function for the video_widget class.
 */
static void
video_widget_dispose(GObject *gobject)
{
    VideoWidget *self = VIDEO_WIDGET(gobject);
    VideoWidgetPrivate *priv = VIDEO_WIDGET_GET_PRIVATE(self);

    QObject::disconnect(priv->remote_frame_update);

    G_OBJECT_CLASS(video_widget_parent_class)->dispose(gobject);
}


/*
 * video_widget_finalize()
 *
 * The finalize function for the video_widget class.
 */
static void
video_widget_finalize(GObject *object)
{
    G_OBJECT_CLASS(video_widget_parent_class)->finalize(object);
}


/*
 * video_widget_class_init()
 *
 * This function init the video_widget_class.
 */
static void
video_widget_class_init(VideoWidgetClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    /* override method */
    object_class->dispose = video_widget_dispose;
    object_class->finalize = video_widget_finalize;
}


/*
 * video_widget_init()
 *
 * This function init the video_widget.
 * - init clutter
 * - init all the widget members
 */
static void
video_widget_init(VideoWidget *self)
{
    VideoWidgetPrivate *priv = VIDEO_WIDGET_GET_PRIVATE(self);

    /* init clutter widget */
    priv->clutter_widget = gtk_clutter_embed_new();
    /* make it act like a normal widget for resizing */
    // gtk_clutter_embed_set_use_layout_size(GTK_CLUTTER_EMBED(priv->clutter_widget), TRUE);
    /* add it to the video_widget */
    gtk_container_add(GTK_CONTAINER(self), priv->clutter_widget);
    /* get the stage */
    priv->stage = gtk_clutter_embed_get_stage(GTK_CLUTTER_EMBED(priv->clutter_widget));

    /* layout manager is used to arrange children in space, here we ask clutter
     * to align children to fill the space when resizing the window */
    clutter_actor_set_layout_manager(priv->stage,
        clutter_bin_layout_new(CLUTTER_BIN_ALIGNMENT_FILL, CLUTTER_BIN_ALIGNMENT_FILL));

    /* add a scene container where we can add and remove our actors */
    priv->video_container = clutter_actor_new();
    clutter_actor_set_background_color(priv->video_container, CLUTTER_COLOR_Black);
    /* make sure it keeps the aspect ratio of the content */
    // clutter_actor_set_content_gravity(priv->video_container, CLUTTER_CONTENT_GRAVITY_RESIZE_ASPECT);
    clutter_actor_add_child(priv->stage, priv->video_container);

    /* TODO: handle button event in screen */
    // g_signal_connect(screen, "button-press-event",
    //         G_CALLBACK(on_button_press_in_screen_event_cb),
    //         self);

    /* TODO: init drag & drop images */
    // gtk_drag_dest_set(GTK_WIDGET(self), GTK_DEST_DEFAULT_ALL, NULL, 0, (GdkDragAction)(GDK_ACTION_COPY | GDK_ACTION_PRIVATE));
    // gtk_drag_dest_add_uri_targets(GTK_WIDGET(self));
    // g_signal_connect(GTK_WIDGET(self), "drag-data-received",
    //                  G_CALLBACK(on_drag_data_received_cb), NULL);
}

FrameInfo *
prepare_framedata(Video::Renderer *renderer, ClutterActor* image_actor)
{
    const QByteArray& data = renderer->currentFrame();
    QSize res = renderer->size();

    /* copy frame data */
    gpointer frame_data = g_memdup((gconstpointer)data.constData(), data.size());

    FrameInfo *frame = g_new0(FrameInfo, 1);
    
    frame->image_actor = image_actor;
    frame->data = (guchar *)frame_data;
    frame->data_size = data.size();
    frame->width = res.width();
    frame->height = res.height();

    return frame;
}

static void
free_framedata(gpointer data)
{
    FrameInfo *frame = (FrameInfo *)data;
    g_free(frame->data);
    g_free(frame);
}

static gboolean
clutter_render_image(gpointer data)
{    
    FrameInfo *frame = (FrameInfo *)data;

    g_return_val_if_fail(CLUTTER_IS_ACTOR(frame->image_actor), FALSE);

    ClutterContent *image_current = clutter_actor_get_content(frame->image_actor);
    ClutterContent *image_new = image_current;

    if (image_new == NULL)
        image_new = clutter_image_new();

    const gint BPP = 4;
    const gint ROW_STRIDE = BPP * frame->width;

    /* update the clutter image */
    GError *error = NULL;
    clutter_image_set_data(
            CLUTTER_IMAGE(image_new),
            frame->data,
            COGL_PIXEL_FORMAT_BGRA_8888,
            frame->width,
            frame->height,
            ROW_STRIDE,
            &error);
    if (error) {
        g_warning("error rendering image to clutter: %s", error->message);
        g_error_free(error);
    }

    if (image_current == NULL) {
        clutter_actor_set_content(frame->image_actor, image_new);
        clutter_actor_set_content_gravity(frame->image_actor, CLUTTER_CONTENT_GRAVITY_RESIZE_ASPECT);
    }

    return FALSE; /* we do not want this function to be called again */
}

/*
 * video_widget_new()
 *
 * The function use to create a new video_widget
 */
GtkWidget*
video_widget_new(void)
{
    GtkWidget *self = (GtkWidget *)g_object_new(VIDEO_WIDGET_TYPE, NULL);
    return self;
}

void
video_widget_set_remote_renderer(VideoWidget *self, Video::Renderer *renderer_remote_new)
{
    g_return_if_fail(IS_VIDEO_WIDGET(self));
    if (renderer_remote_new == NULL) return;

    VideoWidgetPrivate *priv = VIDEO_WIDGET_GET_PRIVATE(self);

    if (priv->renderer_remote)
        QObject::disconnect(priv->remote_frame_update);

    priv->renderer_remote = renderer_remote_new;

    priv->remote_frame_update = QObject::connect(
        renderer_remote_new,
        &Video::Renderer::frameUpdated,
        [=]() {

            /* this callback comes from another thread;
             * rendering must be done in the main loop;
             * copy the frame and add it as a task on the main loop
             */

            /* for now use the video container for the remote image */
            FrameInfo *frame = prepare_framedata(priv->renderer_remote, priv->video_container);

            g_idle_add_full(G_PRIORITY_HIGH_IDLE + 20,
                            (GSourceFunc)clutter_render_image,
                            frame,
                            (GDestroyNotify)free_framedata);
        }
    );
}
