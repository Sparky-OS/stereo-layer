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
G_END_DECLS
#endif
