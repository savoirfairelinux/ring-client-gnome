/*
 *  Copyright (C) 2018 Savoir-faire Linux Inc.
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

#include "selfiewidget.h"
#include "selfieview.h"

struct _SelfieWidget
{
    GtkDialog parent;
};

struct _SelfieWidgetClass
{
    GtkDialogClass parent_class;
};

typedef struct _SelfieWidgetPrivate SelfieWidgetPrivate;

struct _SelfieWidgetPrivate
{
    GtkWidget *selfie_view;
};

G_DEFINE_TYPE_WITH_PRIVATE(SelfieWidget, selfie_widget, GTK_TYPE_DIALOG);
#define SELFIE_WIDGET_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SELFIE_WIDGET_TYPE, SelfieWidgetPrivate))

static void
selfie_widget_dispose(GObject *object)
{
    SelfieWidgetPrivate *priv = SELFIE_WIDGET_GET_PRIVATE(object);
    G_OBJECT_CLASS(selfie_widget_parent_class)->dispose(object);
}

static void
selfie_widget_finalize(GObject *object)
{
    G_OBJECT_CLASS(selfie_widget_parent_class)->finalize(object);
}

GtkWidget*
selfie_widget_new(void)
{
    return (GtkWidget *) g_object_new(SELFIE_WIDGET_TYPE, NULL);
}

static void
selfie_widget_class_init(SelfieWidgetClass *klass)
{
    G_OBJECT_CLASS(klass)->finalize = selfie_widget_finalize;
    G_OBJECT_CLASS(klass)->dispose = selfie_widget_dispose;

    gtk_widget_class_set_template_from_resource(GTK_WIDGET_CLASS (klass), "/cx/ring/RingGnome/selfiewidget.ui");

    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), SelfieWidget, selfie_view);
}

static void
selfie_widget_init(SelfieWidget *self)
{
    SelfieWidgetPrivate *priv = SELFIE_WIDGET_GET_PRIVATE(self);
    gtk_widget_init_template(GTK_WIDGET(self));
}

char *
selfie_widget_get_filename(SelfieWidget *self)
{
    SelfieWidgetPrivate *priv = SELFIE_WIDGET_GET_PRIVATE(self);
    return selfie_view_get_save_file(SELFIE_VIEW(priv->selfie_view));
}
