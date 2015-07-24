/*
    This file is part of darktable,
    copyright (c) 2009-2015 johannes hanika, Tobias Ellinghaus,
    Robert Hutton

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "iop/exposure.h"
#include "common/darktable.h"
#include "common/debug.h"
#include "gui/gtk.h"
#include "gui/timelapse.h"

// Values for the imagelist treeview
enum
{
  I_KEY_COLUMN,
  I_FILENAME_COLUMN,
  I_IMAGE_INFO_COLUMN,
  I_TEMPERATURE_COLUMN,
  I_TINT_COLUMN,
  I_TARGET_LEVEL_COLUMN,
  I_BLACK_COLUMN,
  I_SATURATION_COLUMN,
  I_N_COLUMNS
};

void dt_gui_timelapse_show();
static void init_tab_imagelist(GtkWidget *book);
static void tree_insert_images(GtkListStore *store);

static GtkWidget *_timelapse_dialog;

void dt_gui_timelapse_show()
{
  GtkWidget *win = dt_ui_main_window(darktable.gui->ui);
  _timelapse_dialog = gtk_dialog_new_with_buttons(_("darktable timelapse tool"), GTK_WINDOW(win),
                                                    GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
                                                    _("close"), GTK_RESPONSE_ACCEPT, NULL);
  gtk_window_set_position(GTK_WINDOW(_timelapse_dialog), GTK_WIN_POS_CENTER_ALWAYS);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(_timelapse_dialog));
  GtkWidget *notebook = gtk_notebook_new();
  gtk_widget_set_size_request(notebook, -1, DT_PIXEL_APPLY_DPI(500));
  gtk_widget_set_name(notebook, "timelapse_notebook");
  gtk_box_pack_start(GTK_BOX(content), notebook, TRUE, TRUE, 0);

  init_tab_imagelist(notebook);

  gtk_widget_show_all(_timelapse_dialog);
  (void)gtk_dialog_run(GTK_DIALOG(_timelapse_dialog));
  gtk_widget_destroy(_timelapse_dialog);

}

static void init_tab_imagelist(GtkWidget *book)
{
  GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
  GtkWidget *tree = gtk_tree_view_new();
  GtkListStore *model = gtk_list_store_new(
      I_N_COLUMNS,
      G_TYPE_BOOLEAN,     /* key */
      G_TYPE_STRING,  /* filename */
      G_TYPE_STRING,  /* image info */
      G_TYPE_FLOAT,   /* white balance: temperature */
      G_TYPE_FLOAT,   /* white balance: tint */
      G_TYPE_FLOAT,   /* exposure: target level */
      G_TYPE_FLOAT,   /* expsoure: black */
      G_TYPE_FLOAT   /* color correction: saturation */
  );
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;

  // Adding the outer container
  gtk_widget_set_margin_top(scroll, DT_PIXEL_APPLY_DPI(20));
  gtk_widget_set_margin_bottom(scroll, DT_PIXEL_APPLY_DPI(20));
  gtk_widget_set_margin_start(scroll, DT_PIXEL_APPLY_DPI(20));
  gtk_widget_set_margin_end(scroll, DT_PIXEL_APPLY_DPI(20));
  gtk_notebook_append_page(GTK_NOTEBOOK(book), scroll, gtk_label_new(_("image list")));

  //tree_insert_presets(model);
  tree_insert_images(model);

  // Setting up the cell renderers
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(_("key"), renderer, "text", I_KEY_COLUMN, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(_("filename"), renderer, "text", I_FILENAME_COLUMN, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(_("info"), renderer, "text", I_IMAGE_INFO_COLUMN, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(_("temperature"), renderer, "text", I_TEMPERATURE_COLUMN, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(_("tint"), renderer, "text", I_TINT_COLUMN, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(_("target level"), renderer, "text", I_TARGET_LEVEL_COLUMN, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(_("black point"), renderer, "text", I_BLACK_COLUMN, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(_("saturation"), renderer, "text", I_SATURATION_COLUMN, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  // Attaching the model to the treeview
  gtk_tree_view_set_model(GTK_TREE_VIEW(tree), GTK_TREE_MODEL(model));

  // Adding the treeview to its containers
  gtk_container_add(GTK_CONTAINER(scroll), tree);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

  g_object_unref(G_OBJECT(model));
}

static void tree_insert_images(GtkListStore *store)
{
  GtkTreeIter iter;
  sqlite3_stmt *stmt;

  gtk_list_store_append (store, &iter);  /* Acquire an iterator */

  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
                              "select imgid, filename, aperture, exposure, iso from selected_images s left join images i on s.imgid = i.id",
                              -1, &stmt, NULL);
  while(sqlite3_step(stmt) == SQLITE_ROW)
  {
    int imgid = (int)sqlite3_column_int(stmt,0);
    gchar *filename = (gchar *)sqlite3_column_text(stmt, 1);
    float aperture = sqlite3_column_double(stmt, 2);
    float exposure = sqlite3_column_double(stmt, 3);
    float iso = sqlite3_column_double(stmt, 4);

    float black = dt_exposure_get_black(imgid);

    char info[255] = "";
    sprintf(info,"%f, %f, %f",aperture,exposure,iso);

    gtk_list_store_append (store, &iter);
    gtk_list_store_set (store, &iter,
      I_KEY_COLUMN, FALSE,
      I_FILENAME_COLUMN, filename,
      I_IMAGE_INFO_COLUMN, info,
      I_TEMPERATURE_COLUMN, 5000.0,
      I_TINT_COLUMN, 10.0,
      I_TARGET_LEVEL_COLUMN, 0.5,
      I_BLACK_COLUMN, black,
      I_SATURATION_COLUMN, 1.0,
      -1);
  }

  gtk_list_store_append (store, &iter);
  gtk_list_store_set (store, &iter,
    I_KEY_COLUMN, FALSE,
    I_FILENAME_COLUMN, "asdf.CR2",
    I_IMAGE_INFO_COLUMN, "f/4",
    I_TEMPERATURE_COLUMN, 5000.0,
    I_TINT_COLUMN, 10.0,
    I_TARGET_LEVEL_COLUMN, 0.5,
    I_BLACK_COLUMN, 0.0,
    I_SATURATION_COLUMN, 1.0,
    -1);

  gtk_list_store_append (store, &iter);
  gtk_list_store_set (store, &iter,
    I_KEY_COLUMN, FALSE,
    I_FILENAME_COLUMN, "bsdf.CR2",
    I_IMAGE_INFO_COLUMN, "f/4",
    I_TEMPERATURE_COLUMN, 6000.0,
    I_TINT_COLUMN, 11.0,
    I_TARGET_LEVEL_COLUMN, 0.6,
    I_BLACK_COLUMN, 0.1,
    I_SATURATION_COLUMN, 1.1,
    -1);
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
