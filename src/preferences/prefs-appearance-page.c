/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 *  Copyright © 2000-2003 Marco Pesenti Gritti
 *  Copyright © 2003, 2004, 2005 Christian Persch
 *  Copyright © 2010, 2017 Igalia S.L.
 *
 *  This file is part of Epiphany.
 *
 *  Epiphany is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Epiphany is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Epiphany.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "prefs-appearance-page.h"

#include "ephy-embed-prefs.h"
#include "ephy-file-helpers.h"
#include "ephy-flatpak-utils.h"
#include "ephy-lib-type-builtins.h"
#include "ephy-settings.h"
#include "ephy-shell.h"

#include <math.h>

struct _PrefsAppearancePage {
  AdwPreferencesPage parent_instance;

  /* Fonts */
  GtkWidget *use_gnome_fonts_row;
  GtkWidget *sans_fontbutton;
  GtkWidget *serif_fontbutton;
  GtkWidget *mono_fontbutton;

  /* Reader Mode */
  GtkWidget *reader_mode_box;
  GtkWidget *reader_mode_font_style;
  GtkWidget *reader_mode_color_scheme;

  /* Style */
  GtkWidget *css_row;
  GtkWidget *css_edit_button;
  GtkWidget *js_row;
  GtkWidget *js_edit_button;
  GtkWidget *default_zoom_row;
};

G_DEFINE_FINAL_TYPE (PrefsAppearancePage, prefs_appearance_page, ADW_TYPE_PREFERENCES_PAGE)

static gchar *
reader_font_style_get_name (gpointer                 user_data,
                            EphyPrefsReaderFontStyle style)
{
  switch (style) {
    case EPHY_PREFS_READER_FONT_STYLE_SANS:
      return g_strdup (_("Sans"));
    case EPHY_PREFS_READER_FONT_STYLE_SERIF:
      return g_strdup (_("Serif"));
    default:
      g_assert_not_reached ();
  }
}

static gboolean
reader_font_style_get_mapping (GValue   *value,
                               GVariant *variant,
                               gpointer  user_data)
{
  const char *reader_colors = g_variant_get_string (variant, NULL);

  if (g_strcmp0 (reader_colors, "sans") == 0)
    g_value_set_uint (value, EPHY_PREFS_READER_FONT_STYLE_SANS);
  else if (g_strcmp0 (reader_colors, "serif") == 0)
    g_value_set_uint (value, EPHY_PREFS_READER_FONT_STYLE_SERIF);

  return TRUE;
}

static GVariant *
reader_font_style_set_mapping (const GValue       *value,
                               const GVariantType *expected_type,
                               gpointer            user_data)
{
  switch (g_value_get_uint (value)) {
    case EPHY_PREFS_READER_FONT_STYLE_SANS:
      return g_variant_new_string ("sans");
    case EPHY_PREFS_READER_FONT_STYLE_SERIF:
      return g_variant_new_string ("serif");
    default:
      return g_variant_new_string ("crashed");
  }
}

static gchar *
reader_color_scheme_get_name (gpointer                   user_data,
                              EphyPrefsReaderColorScheme scheme)
{
  switch (scheme) {
    case EPHY_PREFS_READER_COLORS_LIGHT:
      return g_strdup (_("Light"));
    case EPHY_PREFS_READER_COLORS_DARK:
      return g_strdup (_("Dark"));
    default:
      g_assert_not_reached ();
  }
}

static gboolean
reader_color_scheme_get_mapping (GValue   *value,
                                 GVariant *variant,
                                 gpointer  user_data)
{
  const char *reader_colors = g_variant_get_string (variant, NULL);

  if (g_strcmp0 (reader_colors, "light") == 0)
    g_value_set_uint (value, EPHY_PREFS_READER_COLORS_LIGHT);
  else if (g_strcmp0 (reader_colors, "dark") == 0)
    g_value_set_uint (value, EPHY_PREFS_READER_COLORS_DARK);

  return TRUE;
}

static GVariant *
reader_color_scheme_set_mapping (const GValue       *value,
                                 const GVariantType *expected_type,
                                 gpointer            user_data)
{
  switch (g_value_get_uint (value)) {
    case EPHY_PREFS_READER_COLORS_LIGHT:
      return g_variant_new_string ("light");
    case EPHY_PREFS_READER_COLORS_DARK:
      return g_variant_new_string ("dark");
    default:
      return g_variant_new_string ("crashed");
  }
}

static void
css_file_created_cb (GObject      *source,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  g_autoptr (GFile) file = G_FILE (source);
  g_autoptr (GFileOutputStream) stream = NULL;
  g_autoptr (GError) error = NULL;

  stream = g_file_create_finish (file, result, &error);
  if (stream == NULL && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS))
    g_warning ("Failed to create %s: %s", g_file_get_path (file), error->message);
  else {
    if (ephy_is_running_inside_sandbox ()) {
      g_autofree char *uri = g_file_get_uri (file);
      ephy_open_uri_via_flatpak_portal (uri);
    } else {
      ephy_file_launch_uri_handler (file, "text/css");
    }
  }
}

static void
css_edit_button_clicked_cb (GtkWidget           *button,
                            PrefsAppearancePage *appearance_page)
{
  GFile *css_file;

  css_file = g_file_new_for_path (g_build_filename (ephy_profile_dir (),
                                                    USER_STYLESHEET_FILENAME,
                                                    NULL));

  g_file_create_async (css_file, G_FILE_CREATE_NONE, G_PRIORITY_DEFAULT, NULL, css_file_created_cb, NULL);
}

static void
js_file_created_cb (GObject      *source,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  g_autoptr (GFile) file = G_FILE (source);
  g_autoptr (GFileOutputStream) stream = NULL;
  g_autoptr (GError) error = NULL;

  stream = g_file_create_finish (file, result, &error);
  if (stream == NULL && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS))
    g_warning ("Failed to create %s: %s", g_file_get_path (file), error->message);
  else {
    if (ephy_is_running_inside_sandbox ()) {
      g_autofree char *uri = g_file_get_uri (file);
      ephy_open_uri_via_flatpak_portal (uri);
    } else {
      ephy_file_launch_uri_handler (file, "text/javascript");
    }
  }
}

static void
js_edit_button_clicked_cb (GtkWidget           *button,
                           PrefsAppearancePage *appearance_page)
{
  GFile *js_file;

  js_file = g_file_new_for_path (g_build_filename (ephy_profile_dir (),
                                                   USER_JAVASCRIPT_FILENAME,
                                                   NULL));

  g_file_create_async (g_steal_pointer (&js_file), G_FILE_CREATE_NONE, G_PRIORITY_DEFAULT, NULL, js_file_created_cb, NULL);
}

static gboolean
on_default_zoom_row_output (AdwSpinRow *spin,
                            gpointer    user_data)
{
  g_autofree gchar *text = NULL;
  gdouble value;

  value = (int)adw_spin_row_get_value (spin);
  text = g_strdup_printf ("%.f%%", value);
  gtk_editable_set_text (GTK_EDITABLE (spin), text);

  return TRUE;
}

static void
on_default_zoom_row_changed (AdwSpinRow *spin,
                             gpointer    user_data)
{
  gdouble value;

  value = adw_spin_row_get_value (spin);
  value = round (value) / 100;
  g_settings_set_double (EPHY_SETTINGS_WEB, EPHY_PREFS_WEB_DEFAULT_ZOOM_LEVEL, value);
}

static void
setup_appearance_page (PrefsAppearancePage *appearance_page)
{
  GSettings *web_settings = ephy_settings_get (EPHY_PREFS_WEB_SCHEMA);
  GSettings *reader_settings = ephy_settings_get (EPHY_PREFS_READER_SCHEMA);

  /* ======================================================================== */
  /* ========================== Fonts ======================================= */
  /* ======================================================================== */
  g_settings_bind (web_settings,
                   EPHY_PREFS_WEB_USE_GNOME_FONTS,
                   appearance_page->use_gnome_fonts_row,
                   "enable-expansion",
                   G_SETTINGS_BIND_INVERT_BOOLEAN);

  g_settings_bind (web_settings,
                   EPHY_PREFS_WEB_SANS_SERIF_FONT,
                   appearance_page->sans_fontbutton,
                   "font",
                   G_SETTINGS_BIND_DEFAULT);

  g_settings_bind (web_settings,
                   EPHY_PREFS_WEB_SERIF_FONT,
                   appearance_page->serif_fontbutton,
                   "font",
                   G_SETTINGS_BIND_DEFAULT);

  g_settings_bind (web_settings,
                   EPHY_PREFS_WEB_MONOSPACE_FONT,
                   appearance_page->mono_fontbutton,
                   "font",
                   G_SETTINGS_BIND_DEFAULT);

  /* ======================================================================== */
  /* ========================== Reader Mode ================================= */
  /* ======================================================================== */
  g_settings_bind_with_mapping (reader_settings,
                                EPHY_PREFS_READER_FONT_STYLE,
                                appearance_page->reader_mode_font_style,
                                "selected",
                                G_SETTINGS_BIND_DEFAULT,
                                reader_font_style_get_mapping,
                                reader_font_style_set_mapping,
                                NULL, NULL);

  g_object_bind_property (adw_style_manager_get_default (), "system-supports-color-schemes",
                          appearance_page->reader_mode_color_scheme, "visible",
                          G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);

  g_settings_bind_with_mapping (reader_settings,
                                EPHY_PREFS_READER_COLOR_SCHEME,
                                appearance_page->reader_mode_color_scheme,
                                "selected",
                                G_SETTINGS_BIND_DEFAULT,
                                reader_color_scheme_get_mapping,
                                reader_color_scheme_set_mapping,
                                NULL, NULL);

  /* ======================================================================== */
  /* ========================== Style ======================================= */
  /* ======================================================================== */
  g_settings_bind (web_settings,
                   EPHY_PREFS_WEB_ENABLE_USER_CSS,
                   appearance_page->css_row,
                   "active",
                   G_SETTINGS_BIND_DEFAULT);

  g_settings_bind (web_settings,
                   EPHY_PREFS_WEB_ENABLE_USER_CSS,
                   appearance_page->css_edit_button,
                   "sensitive",
                   G_SETTINGS_BIND_GET);

  g_signal_connect (appearance_page->css_edit_button,
                    "clicked",
                    G_CALLBACK (css_edit_button_clicked_cb),
                    appearance_page);

  g_settings_bind (web_settings,
                   EPHY_PREFS_WEB_ENABLE_USER_JS,
                   appearance_page->js_row,
                   "active",
                   G_SETTINGS_BIND_DEFAULT);

  g_settings_bind (web_settings,
                   EPHY_PREFS_WEB_ENABLE_USER_JS,
                   appearance_page->js_edit_button,
                   "sensitive",
                   G_SETTINGS_BIND_GET);

  g_signal_connect (appearance_page->js_edit_button,
                    "clicked",
                    G_CALLBACK (js_edit_button_clicked_cb),
                    appearance_page);

  adw_spin_row_set_value (ADW_SPIN_ROW (appearance_page->default_zoom_row),
                          g_settings_get_double (EPHY_SETTINGS_WEB, EPHY_PREFS_WEB_DEFAULT_ZOOM_LEVEL) * 100);
}

static void
prefs_appearance_page_class_init (PrefsAppearancePageClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/epiphany/gtk/prefs-appearance-page.ui");

  /* Fonts */
  gtk_widget_class_bind_template_child (widget_class, PrefsAppearancePage, use_gnome_fonts_row);
  gtk_widget_class_bind_template_child (widget_class, PrefsAppearancePage, sans_fontbutton);
  gtk_widget_class_bind_template_child (widget_class, PrefsAppearancePage, serif_fontbutton);
  gtk_widget_class_bind_template_child (widget_class, PrefsAppearancePage, mono_fontbutton);

  /* Reader Mode */
  gtk_widget_class_bind_template_child (widget_class, PrefsAppearancePage, reader_mode_box);
  gtk_widget_class_bind_template_child (widget_class, PrefsAppearancePage, reader_mode_font_style);
  gtk_widget_class_bind_template_child (widget_class, PrefsAppearancePage, reader_mode_color_scheme);

  /* Style */
  gtk_widget_class_bind_template_child (widget_class, PrefsAppearancePage, css_row);
  gtk_widget_class_bind_template_child (widget_class, PrefsAppearancePage, css_edit_button);
  gtk_widget_class_bind_template_child (widget_class, PrefsAppearancePage, js_row);
  gtk_widget_class_bind_template_child (widget_class, PrefsAppearancePage, js_edit_button);
  gtk_widget_class_bind_template_child (widget_class, PrefsAppearancePage, default_zoom_row);

  /* Signals */
  gtk_widget_class_bind_template_callback (widget_class, reader_font_style_get_name);
  gtk_widget_class_bind_template_callback (widget_class, reader_color_scheme_get_name);
  gtk_widget_class_bind_template_callback (widget_class, on_default_zoom_row_output);
  gtk_widget_class_bind_template_callback (widget_class, on_default_zoom_row_changed);
}

static void
prefs_appearance_page_init (PrefsAppearancePage *appearance_page)
{
  EphyEmbedShellMode mode = ephy_embed_shell_get_mode (ephy_embed_shell_get_default ());

  gtk_widget_init_template (GTK_WIDGET (appearance_page));

  gtk_widget_set_visible (appearance_page->reader_mode_box,
                          mode != EPHY_EMBED_SHELL_MODE_APPLICATION);

  setup_appearance_page (appearance_page);
}
