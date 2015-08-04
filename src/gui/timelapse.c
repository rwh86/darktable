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

#include "common/darktable.h"
#include "develop/imageop.h"
#include "common/debug.h"
#include "gui/gtk.h"
#include "gui/timelapse.h"

#define BUF_SIZE 255

// Values for the imagelist treeview
typedef struct
{
  gboolean key;
  gchar filename[BUF_SIZE];
  gchar image_info[BUF_SIZE];
  gfloat temperature;
  gfloat tint;
  gfloat percentile;
  gfloat target_level;
  gfloat black;
  gfloat saturation;
}
image_row_t;

enum
{
  I_KEY_COLUMN,
  I_FILENAME_COLUMN,
  I_IMAGE_INFO_COLUMN,
  I_TEMPERATURE_COLUMN,
  I_TINT_COLUMN,
  I_PERCENTILE_COLUMN,
  I_TARGET_LEVEL_COLUMN,
  I_BLACK_COLUMN,
  I_SATURATION_COLUMN,
  I_N_COLUMNS
};

void dt_gui_timelapse_show();
static void init_data(GArray *image_table);
static void init_imagelist(GArray *image_table, GtkWidget *dialog);
static void cell_edited (GtkCellRendererText *cell, const gchar *path_string, const gchar *new_text, gpointer data);
static void cell_toggled (GtkCellRendererToggle *cell, const gchar *path_string, gpointer data);

static GtkWidget *_timelapse_dialog;

GArray *image_table;

void dt_gui_timelapse_show()
{
  image_table = g_array_sized_new (FALSE, FALSE, sizeof (image_row_t), 1);
  GtkWidget *win = dt_ui_main_window(darktable.gui->ui);
  _timelapse_dialog = gtk_dialog_new_with_buttons(_("darktable timelapse tool"), GTK_WINDOW(win),
                                                    GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
                                                    _("_Cancel"), GTK_RESPONSE_REJECT,
                                                    _("_OK"), GTK_RESPONSE_ACCEPT, NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(_timelapse_dialog));
  gtk_widget_set_size_request (_timelapse_dialog, DT_PIXEL_APPLY_DPI(800),DT_PIXEL_APPLY_DPI(600));

  init_data(image_table);
  init_imagelist(image_table, content);

  gtk_widget_show_all(_timelapse_dialog);
  (void)gtk_dialog_run(GTK_DIALOG(_timelapse_dialog));
  gtk_widget_destroy(_timelapse_dialog);
}

static void init_data(GArray *image_table)
{
  // find the modules -- the pointers stay valid until darktable shuts down
  static dt_iop_module_so_t *exposure_mod    = NULL;
  static dt_iop_module_so_t *colisa_mod      = NULL;
  static dt_iop_module_so_t *temperature_mod = NULL;
  GList *modules = g_list_first(darktable.iop);
  while(modules)
  {
    dt_iop_module_so_t *module = (dt_iop_module_so_t *)(modules->data);
    if(!strcmp(module->op, "exposure"))
    {
      exposure_mod = module;
    }
    else if(!strcmp(module->op, "colisa"))
    {
      colisa_mod = module;
    }
    else if(!strcmp(module->op, "temperature"))
    {
      temperature_mod = module;
    }
    
    if( exposure_mod != NULL && colisa_mod != NULL && temperature_mod != NULL )
    {
      break;
    }
    modules = g_list_next(modules);
  }

  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
                              "select imgid, filename, aperture, exposure, iso from selected_images s left join images i on s.imgid = i.id",
                              -1, &stmt, NULL);
  while(sqlite3_step(stmt) == SQLITE_ROW)
  {
    image_row_t image_row;

    int imgid = (int)sqlite3_column_int(stmt,0);
    gchar *filename = (gchar *)sqlite3_column_text(stmt, 1);
    float aperture = sqlite3_column_double(stmt, 2);
    float exposure = sqlite3_column_double(stmt, 3);
    float iso = sqlite3_column_double(stmt, 4);

    float black = 0.0, deflicker_percentile = 0.0, deflicker_target_level = 0.0; //exposure
    float contrast, brightness, saturation = 0.0; //colisa
    float temp_out = 0.0, coeffs[3] = {0.0, 0.0, 0.0}; //temperature

    char expo_str[16] = "";
    if(exposure <= 0.5)
            snprintf(expo_str, sizeof(expo_str), "1/%.0f", 1.0 / exposure);
    else
            snprintf(expo_str, sizeof(expo_str), "%.1f''", exposure);
    char info[255] = "";
    snprintf(info,sizeof(info),"f/%.1f, %ss, iso %.0f",aperture,expo_str,iso);

    image_row.key = FALSE;
    strncpy(image_row.filename, filename, BUF_SIZE);
    image_row.filename[BUF_SIZE-1]='\0';
    strncpy(image_row.image_info, info, BUF_SIZE);
    image_row.image_info[BUF_SIZE-1]='\0';

    // params available in the exposure module:
    // dt_iop_exposure_mode_t mode;
    // float black;
    // float exposure;
    // float deflicker_percentile, deflicker_target_level;
    // dt_iop_exposure_deflicker_histogram_source_t deflicker_histogram_source;

    // params available in the colisa module:
    // float contrast;
    // float brightness;
    // float saturation;

    // params available in the temperature module:
    // float temp_out;
    // float coeffs[3];

    // db lookup params
    if(exposure_mod && exposure_mod->get_p)
    {
      sqlite3_stmt *stmt;
      DT_DEBUG_SQLITE3_PREPARE_V2(
          dt_database_get(darktable.db),
          "SELECT op_params FROM history WHERE imgid=?1 AND operation='exposure' ORDER BY num DESC LIMIT 1", -1,
          &stmt, NULL);
      DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
      if(sqlite3_step(stmt) == SQLITE_ROW)
      {
        // use introspection to get the values from the binary params blob
        const void *params = sqlite3_column_blob(stmt, 0);
        //*dt_iop_exposure_mode_t = *(float *)exposure_mod->get_p(params, "black");
        black                  = *(float *)exposure_mod->get_p(params, "black");
        //*exposure               = *(float *)exposure_mod->get_p(params, "exposure");
        deflicker_percentile   = *(float *)exposure_mod->get_p(params, "deflicker_percentile");
        deflicker_target_level = *(float *)exposure_mod->get_p(params, "deflicker_target_level");
        //dt_print(DT_DEBUG_LIGHTTABLE, "[lighttable] black: %f, deflicker_percentile: %f, deflicker_target_level: %f\n", black, deflicker_percentile, deflicker_target_level);
      }
      sqlite3_finalize(stmt);
    }
    if(colisa_mod && colisa_mod->get_p)
    {
      sqlite3_stmt *stmt;
      DT_DEBUG_SQLITE3_PREPARE_V2(
          dt_database_get(darktable.db),
          "SELECT op_params FROM history WHERE imgid=?1 AND operation='colisa' ORDER BY num DESC LIMIT 1", -1,
          &stmt, NULL);
      DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
      if(sqlite3_step(stmt) == SQLITE_ROW)
      {
        // use introspection to get the values from the binary params blob
        const void *params = sqlite3_column_blob(stmt, 0);
        contrast   = *(float *)colisa_mod->get_p(params, "contrast");
        brightness = *(float *)colisa_mod->get_p(params, "brightness");
        saturation = *(float *)colisa_mod->get_p(params, "saturation");
        //dt_print(DT_DEBUG_LIGHTTABLE, "row found\n");
        dt_print(DT_DEBUG_LIGHTTABLE, "[lighttable] contrast: %f, brightness: %f, saturation: %f\n", contrast, brightness, saturation);
      }
      sqlite3_finalize(stmt);
    }

    if(temperature_mod && temperature_mod->get_p)
    {
      sqlite3_stmt *stmt;
      DT_DEBUG_SQLITE3_PREPARE_V2(
          dt_database_get(darktable.db),
          "SELECT op_params FROM history WHERE imgid=?1 AND operation='temperature' ORDER BY num DESC LIMIT 1", -1,
          &stmt, NULL);
      DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
      if(sqlite3_step(stmt) == SQLITE_ROW)
      {
        // use introspection to get the values from the binary params blob
        const void *params = sqlite3_column_blob(stmt, 0);
        temp_out = *(float *)temperature_mod->get_p(params, "temp_out");
        coeffs[0]   = ((float *)temperature_mod->get_p(params, "coeffs"))[0];
        coeffs[1]   = ((float *)temperature_mod->get_p(params, "coeffs"))[1];
        coeffs[2]   = ((float *)temperature_mod->get_p(params, "coeffs"))[2];
        //dt_print(DT_DEBUG_LIGHTTABLE, "row found\n");
        dt_print(DT_DEBUG_LIGHTTABLE, "[lighttable] temp_out: %f, coeffs[0]: %f, coeffs[1]: %f coeffs[2]: %f\n", temp_out, coeffs[0], coeffs[1], coeffs[2]);
      }
      sqlite3_finalize(stmt);
    }

    image_row.temperature = 5000.0;
    image_row.tint = 10.0;
    image_row.percentile = deflicker_percentile;
    image_row.target_level = deflicker_target_level;
    image_row.black = black;
    image_row.saturation = saturation;

    g_array_append_vals(image_table, &image_row, 1);
    //dt_print(DT_DEBUG_LIGHTTABLE, "[init_data]: key: %d, filename: %s, image_info: %s, temperature: %f, tint: %f, percentile: %f, target_level: %f, black: %f, saturation: %f\n",
    //    image_row.key, image_row.filename, image_row.image_info, image_row.temperature, image_row.tint, image_row.percentile, image_row.target_level, image_row.black, image_row.saturation);
  }
  //dt_print(DT_DEBUG_LIGHTTABLE, "[lighttable]: number of rows: %d\n", image_table->len);
}

/** build a scrollable table containing image names with their corresponding metadata */
static void init_imagelist(GArray *image_table, GtkWidget *dialog)
{
  //dt_print(DT_DEBUG_LIGHTTABLE, "[lighttable] init_imagelist\n");
  //dt_print(DT_DEBUG_LIGHTTABLE, "[lighttable]: number of rows: %d\n", image_table->len);
  GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_widget_set_vexpand(scroll,TRUE);
  GtkWidget *tree = gtk_tree_view_new();

  GtkListStore *model = gtk_list_store_new(
      I_N_COLUMNS,
      G_TYPE_BOOLEAN,     /* key */
      G_TYPE_STRING,  /* filename */
      G_TYPE_STRING,  /* image info */
      G_TYPE_FLOAT,   /* white balance: temperature */
      G_TYPE_FLOAT,   /* white balance: tint */
      G_TYPE_FLOAT,   /* exposure: percentile */
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
  gtk_container_add(GTK_CONTAINER(dialog), scroll);

  GtkTreeIter iter;
  gtk_list_store_append (model, &iter);  /* Acquire an iterator */

  for( int ii = 0 ; ii < image_table->len ; ii++ )
  {
    //dt_print(DT_DEBUG_LIGHTTABLE, "[lighttable] ii: %d\n", ii);
    image_row_t row = g_array_index(image_table, image_row_t, ii);
    //dt_print(DT_DEBUG_LIGHTTABLE, "key: %d, filename: %s, image_info: %s, temperature: %f, tint: %f, percentile: %f, target_level: %f, black: %f, saturation: %f\n",
    //    row.key, row.filename, row.image_info, row.temperature, row.tint, row.percentile, row.target_level, row.black, row.saturation);

    gtk_list_store_set (model, &iter,
      I_KEY_COLUMN, FALSE,
      I_FILENAME_COLUMN, row.filename,
      I_IMAGE_INFO_COLUMN, row.image_info,
      I_TEMPERATURE_COLUMN, row.temperature,
      I_TINT_COLUMN, row.tint,
      I_PERCENTILE_COLUMN, row.percentile,
      I_TARGET_LEVEL_COLUMN, row.target_level,
      I_BLACK_COLUMN, row.black,
      I_SATURATION_COLUMN, row.saturation,
      -1);
    gtk_list_store_append (model, &iter);
  }
  // remove empty last row
  gtk_list_store_remove (model, &iter);

  // Set up the cell renderers
  renderer = gtk_cell_renderer_toggle_new();
  //g_signal_connect (renderer, "edited", G_CALLBACK(cell_edited), model);
  g_signal_connect (renderer, "toggled", G_CALLBACK (cell_toggled), model);
  g_object_set_data (G_OBJECT (renderer), "column", GINT_TO_POINTER (I_KEY_COLUMN));
  column = gtk_tree_view_column_new_with_attributes(_("key"), renderer, "active", I_KEY_COLUMN, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(_("filename"), renderer, "text", I_FILENAME_COLUMN, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(_("info"), renderer, "text", I_IMAGE_INFO_COLUMN, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  renderer = gtk_cell_renderer_text_new();
  g_object_set(renderer, "editable", TRUE, NULL);
  g_signal_connect (renderer, "edited", G_CALLBACK(cell_edited), model);
  g_object_set_data (G_OBJECT (renderer), "column", GINT_TO_POINTER (I_TEMPERATURE_COLUMN));
  column = gtk_tree_view_column_new_with_attributes(_("temperature"), renderer, "text", I_TEMPERATURE_COLUMN, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  renderer = gtk_cell_renderer_text_new();
  g_object_set(renderer, "editable", TRUE, NULL);
  column = gtk_tree_view_column_new_with_attributes(_("tint"), renderer, "text", I_TINT_COLUMN, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  renderer = gtk_cell_renderer_text_new();
  g_object_set(renderer, "editable", TRUE, NULL);
  column = gtk_tree_view_column_new_with_attributes(_("percentile"), renderer, "text", I_PERCENTILE_COLUMN, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  renderer = gtk_cell_renderer_text_new();
  g_object_set(renderer, "editable", TRUE, NULL);
  g_signal_connect (renderer, "edited", G_CALLBACK(cell_edited), model);
  g_object_set_data (G_OBJECT (renderer), "column", GINT_TO_POINTER (I_TARGET_LEVEL_COLUMN));
  column = gtk_tree_view_column_new_with_attributes(_("target level"), renderer, "text", I_TARGET_LEVEL_COLUMN, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  renderer = gtk_cell_renderer_text_new();
  g_object_set(renderer, "editable", TRUE, NULL);
  column = gtk_tree_view_column_new_with_attributes(_("black point"), renderer, "text", I_BLACK_COLUMN, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  renderer = gtk_cell_renderer_text_new();
  g_object_set(renderer, "editable", TRUE, NULL);
  column = gtk_tree_view_column_new_with_attributes(_("saturation"), renderer, "text", I_SATURATION_COLUMN, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

  // Attaching the model to the treeview
  gtk_tree_view_set_model(GTK_TREE_VIEW(tree), GTK_TREE_MODEL(model));

  // Adding the treeview to its containers
  gtk_container_add(GTK_CONTAINER(scroll), tree);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

  g_object_unref(G_OBJECT(model));
}

static void cell_edited (GtkCellRendererText *cell, const gchar *path_string, const gchar *new_text, gpointer data)
{
  GtkTreeModel *model = (GtkTreeModel *)data;
  GtkTreePath *path   = gtk_tree_path_new_from_string (path_string);
  gint column         = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (cell), "column"));

  GtkTreeIter iter;
  gtk_tree_model_get_iter (model, &iter, path);

  gint ii = gtk_tree_path_get_indices (path)[0];

  switch (column) {
    case I_TARGET_LEVEL_COLUMN:
    {
      g_array_index (image_table, image_row_t, ii).target_level = atof(new_text);
      gtk_list_store_set (GTK_LIST_STORE (model), &iter, column,
                          g_array_index (image_table, image_row_t, ii).target_level, -1);
    }
    break;
  }

  gtk_tree_path_free (path);
}

static void cell_toggled (GtkCellRendererToggle *cell, const gchar *path_string, gpointer data) {
  GtkTreeModel *model = (GtkTreeModel *)data;
  GtkTreePath *path   = gtk_tree_path_new_from_string (path_string);
  gint column         = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (cell), "column"));

  GtkTreeIter iter;
  gtk_tree_model_get_iter (model, &iter, path);

  gint ii = gtk_tree_path_get_indices (path)[0];

  switch (column) {
    case I_KEY_COLUMN:
    {
      gboolean key;
      gtk_tree_model_get (model, &iter, I_KEY_COLUMN, &key, -1);
      key ^= 1;
      g_array_index (image_table, image_row_t, ii).key = key;
      gtk_list_store_set (GTK_LIST_STORE (model), &iter, I_KEY_COLUMN, key, -1);
      //dt_print(DT_DEBUG_LIGHTTABLE, "[lighttable] column: %i, row: %d, was set to: %d\n", column, ii, g_array_index(image_table, image_row_t, ii).key);
    }
    break;
  }

  gtk_tree_path_free (path);
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
