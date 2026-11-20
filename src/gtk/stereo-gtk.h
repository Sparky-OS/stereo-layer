/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef STEREO_GTK_H
#define STEREO_GTK_H
#include <gtk/gtk.h>
#include <stereo-policy.h>
G_BEGIN_DECLS
#define STEREO_GTK_EXPORT __attribute__((visibility("default")))
typedef void (*StereoGtkFileChanged)(GtkWidget *button, int error_number, gpointer data);
typedef void (*StereoGtkEnableChanged)(GtkWidget *button, gboolean enabled, gpointer data);
/* Uses the shared C input list and state store. Never modifies the media file. The menu also holds the program's
 * one switch, "Enable 3D" (on when made), and "Display Settings…", which opens Display Configuration. */
STEREO_GTK_EXPORT GtkWidget *stereo_gtk_file_menu_new(StereoGtkFileChanged changed, StereoGtkEnableChanged enable_changed,
                                                      gpointer data);
STEREO_GTK_EXPORT gboolean stereo_gtk_file_menu_get_enabled(GtkWidget *button);
STEREO_GTK_EXPORT gboolean stereo_gtk_file_menu_get_choice(GtkWidget *button, struct stereo_policy_file_choice *choice);
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
/* The display aspect of one view (width over height); 0 takes the textures' own shape. */
STEREO_GTK_EXPORT void stereo_gtk_area_set_view_aspect(GtkWidget *area, double aspect);
/* A 2D widget drawn over the area (a player's controls) stays visible in the window, the same in both eyes. */
STEREO_GTK_EXPORT void stereo_gtk_area_keep_above(GtkWidget *area, GtkWidget *above);
/* The views of one packed picture, each with its own samples, nothing resampled (the rule of the Qt layer's
 * storedView() and Picture::fromAnaglyph()); a picture and its depth map in one frame are drawn by libstereo-dibr as
 * Picture::fromDepth() draws them, at the library's one default. Packings that need another picture (multiview, frames
 * in turn, block laced, a separate depth map) fail with G_IO_ERROR_NOT_SUPPORTED and a translated sentence to show the
 * person: give the two pictures to stereo_gtk_unpack_pair(). */
STEREO_GTK_EXPORT gboolean stereo_gtk_unpack(GdkTexture *frame, int layout, int order, int anaglyph,
                                             GdkTexture **left, GdkTexture **right, GError **error);
/* The views of a packing that needs two pictures: two decoded pictures in turn (frames in turn, both views in one
 * block, the two views of a multiview stream), the first the left eye unless order is right first, sizes equal; or a
 * picture and its separate depth map (STEREO_POLICY_LAYOUT_DEPTH_SEPARATE), drawn by libstereo-dibr. */
STEREO_GTK_EXPORT gboolean stereo_gtk_unpack_pair(GdkTexture *first, GdkTexture *second, int layout, int order,
                                                  GdkTexture **left, GdkTexture **right, GError **error);
/* TRUE for the packings stereo_gtk_unpack_pair() takes. */
STEREO_GTK_EXPORT gboolean stereo_gtk_layout_needs_two_pictures(int layout);
/* The display aspect of one view of a frame shown at frame_aspect, by stereo_policy_video_full_views(). */
STEREO_GTK_EXPORT double stereo_gtk_view_aspect(int width, int height, double frame_aspect, int layout);
STEREO_GTK_EXPORT gboolean stereo_gtk_area_get_shown(GtkWidget *area);
G_END_DECLS
#endif
