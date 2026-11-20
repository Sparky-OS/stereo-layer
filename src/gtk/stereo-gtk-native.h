/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <gtk/gtk.h>
typedef struct StereoGtkNative StereoGtkNative;
StereoGtkNative *stereo_gtk_native_new(GtkWidget *area, GError **error);
gboolean stereo_gtk_native_present(StereoGtkNative *native, const graphene_rect_t *rect,
                                  GdkTexture *left, GdkTexture *right, GError **error);
void stereo_gtk_native_free(StereoGtkNative *native);
