/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef STEREO_GTK_H
#define STEREO_GTK_H
#include <gtk/gtk.h>
#include <stereo-policy.h>
G_BEGIN_DECLS
#define STEREO_GTK_EXPORT __attribute__((visibility("default")))
typedef void (*StereoGtkFileChanged)(GtkWidget *button, int error_number, gpointer data);
/* Uses the shared C input list and state store. Never modifies the media file. */
STEREO_GTK_EXPORT GtkWidget *stereo_gtk_file_menu_new(StereoGtkFileChanged changed, gpointer data);
STEREO_GTK_EXPORT int stereo_gtk_file_menu_set_file(GtkWidget *button, const char *path, gboolean stereo_source);
/* Windows mask only their active media rectangles; no top-level declaration. */
STEREO_GTK_EXPORT GType stereo_gtk_window_get_type(void);
STEREO_GTK_EXPORT GType stereo_gtk_application_window_get_type(void);
STEREO_GTK_EXPORT GType stereo_gtk_area_get_type(void);
STEREO_GTK_EXPORT GtkWidget *stereo_gtk_area_new(void);
/* The two textures retain their native samples and must have identical sizes.
 * A null right texture is an ordinary 2D picture. The widget never resamples. */
STEREO_GTK_EXPORT void stereo_gtk_area_set_pair(GtkWidget *area, GdkTexture *left, GdkTexture *right);
STEREO_GTK_EXPORT void stereo_gtk_area_set_enabled(GtkWidget *area, gboolean enabled);
STEREO_GTK_EXPORT gboolean stereo_gtk_area_get_shown(GtkWidget *area);
G_END_DECLS
#endif
