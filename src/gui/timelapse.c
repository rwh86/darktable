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

// Values for the imagelist treeview
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
//static void init_tab_imagelist(GtkWidget *book);
static void init_imagelist(GtkWidget *dialog);
static void tree_insert_images(GtkListStore *store);
int dt_exposure_get_params(float *black, float *deflicker_percentile, float *deflicker_target_level, const int imgid);
int dt_colisa_get_params(float *contrast, float *brightness, float *saturation, const int imgid);
int dt_temperature_get_params(float *temp_out, float *coeffs, const int imgid);

static GtkWidget *_timelapse_dialog;

void dt_gui_timelapse_show()
{
  GtkWidget *win = dt_ui_main_window(darktable.gui->ui);
  _timelapse_dialog = gtk_dialog_new_with_buttons(_("darktable timelapse tool"), GTK_WINDOW(win),
                                                    GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
                                                    _("_Cancel"), GTK_RESPONSE_REJECT,
                                                    _("_OK"), GTK_RESPONSE_ACCEPT, NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(_timelapse_dialog));
  gtk_widget_set_size_request (_timelapse_dialog, DT_PIXEL_APPLY_DPI(500),DT_PIXEL_APPLY_DPI(300));

  init_imagelist(content);

  gtk_widget_show_all(_timelapse_dialog);
  (void)gtk_dialog_run(GTK_DIALOG(_timelapse_dialog));
  gtk_widget_destroy(_timelapse_dialog);
}

static void init_imagelist(GtkWidget *dialog)
{
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
  //gtk_notebook_append_page(GTK_NOTEBOOK(book), scroll, gtk_label_new(_("image list")));
  gtk_container_add(GTK_CONTAINER(dialog), scroll);

  //tree_insert_presets(model);
  tree_insert_images(model);

  // Setting up the cell renderers
  renderer = gtk_cell_renderer_toggle_new();
  column = gtk_tree_view_column_new_with_attributes(_("key"), renderer, "toggle", I_KEY_COLUMN, NULL);
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
  column = gtk_tree_view_column_new_with_attributes(_("percentile"), renderer, "text", I_PERCENTILE_COLUMN, NULL);
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

/** build a scrollable table containing image names with their corresponding metadata */
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

    float black, deflicker_percentile, deflicker_target_level = 0.0;
    dt_exposure_get_params(&black, &deflicker_percentile, &deflicker_target_level, imgid); 

    char expo_str[16] = "";
    if(exposure <= 0.5)
            snprintf(expo_str, sizeof(expo_str), "1/%.0f", 1.0 / exposure);
    else
            snprintf(expo_str, sizeof(expo_str), "%.1f''", exposure);
    char info[255] = "";
    snprintf(info,sizeof(info),"f/%.1f, %ss, iso %.0f",aperture,expo_str,iso);

    float contrast, brightness, saturation = 0.0;
    dt_colisa_get_params(&contrast, &brightness, &saturation, imgid); 

    float temp_out = 0.0, coeffs[3] = {0.0, 0.0, 0.0};
    dt_temperature_get_params(&temp_out, coeffs, imgid);

    gtk_list_store_set (store, &iter,
      I_KEY_COLUMN, FALSE,
      I_FILENAME_COLUMN, filename,
      I_IMAGE_INFO_COLUMN, info,
      I_TEMPERATURE_COLUMN, 5000.0,
      I_TINT_COLUMN, 10.0,
      I_PERCENTILE_COLUMN, deflicker_percentile,
      I_TARGET_LEVEL_COLUMN, deflicker_target_level,
      I_BLACK_COLUMN, black,
      I_SATURATION_COLUMN, saturation,
      -1);
    gtk_list_store_append (store, &iter);
  }
  // remove empty last row
  gtk_list_store_remove (store, &iter);
}

int dt_exposure_get_params(float *black, float *deflicker_percentile, float *deflicker_target_level, const int imgid)
{
  // find the exposure module -- the pointer stays valid until darktable shuts down
  static dt_iop_module_so_t *exposure_mod = NULL;
  if(exposure_mod == NULL)
  {
    GList *modules = g_list_first(darktable.iop);
    while(modules)
    {
      dt_iop_module_so_t *module = (dt_iop_module_so_t *)(modules->data);
      if(!strcmp(module->op, "exposure"))
      {
        exposure_mod = module;
        break;
      }
      modules = g_list_next(modules);
    }
  }

  /* params available in the exposure module:
  dt_iop_exposure_mode_t mode;
  float black;
  float exposure;
  float deflicker_percentile, deflicker_target_level;
  dt_iop_exposure_deflicker_histogram_source_t deflicker_histogram_source;*/

  // db lookup exposure params
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
      *black                  = *(float *)exposure_mod->get_p(params, "black");
      //*exposure               = *(float *)exposure_mod->get_p(params, "exposure");
      *deflicker_percentile   = *(float *)exposure_mod->get_p(params, "deflicker_percentile");
      *deflicker_target_level = *(float *)exposure_mod->get_p(params, "deflicker_target_level");
    }
    sqlite3_finalize(stmt);
  }
  dt_print(DT_DEBUG_LIGHTTABLE, "[lighttable] black: %f, deflicker_percentile: %f, deflicker_target_level: %f\n", *black, *deflicker_percentile, *deflicker_target_level);
  return 0; //success
}

int dt_colisa_get_params(float *contrast, float *brightness, float *saturation, const int imgid)
{
  // find the colisa module -- the pointer stays valid until darktable shuts down
  static dt_iop_module_so_t *colisa_mod = NULL;
  if(colisa_mod == NULL)
  {
    GList *modules = g_list_first(darktable.iop);
    while(modules)
    {
      dt_iop_module_so_t *module = (dt_iop_module_so_t *)(modules->data);
      if(!strcmp(module->op, "colisa"))
      {
        colisa_mod = module;
        dt_print(DT_DEBUG_LIGHTTABLE, "found module\n");
        break;
      }
      modules = g_list_next(modules);
    }
  }

  // params available in the colisa module:
  // float contrast;
  // float brightness;
  // float saturation;

  // db lookup colisa params
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
      *contrast   = *(float *)colisa_mod->get_p(params, "contrast");
      *brightness = *(float *)colisa_mod->get_p(params, "brightness");
      *saturation = *(float *)colisa_mod->get_p(params, "saturation");
      dt_print(DT_DEBUG_LIGHTTABLE, "row found\n");
    }
    sqlite3_finalize(stmt);
  }
  dt_print(DT_DEBUG_LIGHTTABLE, "[lighttable] contrast: %f, brightness: %f, saturation: %f\n", *contrast, *brightness, *saturation);
  return 0; //success
}

int dt_temperature_get_params(float *temp_out, float *coeffs, const int imgid)
{
  // find the temperature module -- the pointer stays valid until darktable shuts down
  static dt_iop_module_so_t *temperature_mod = NULL;
  if(temperature_mod == NULL)
  {
    GList *modules = g_list_first(darktable.iop);
    while(modules)
    {
      dt_iop_module_so_t *module = (dt_iop_module_so_t *)(modules->data);
      if(!strcmp(module->op, "temperature"))
      {
        temperature_mod = module;
        dt_print(DT_DEBUG_LIGHTTABLE, "found module\n");
        break;
      }
      modules = g_list_next(modules);
    }
  }

  // params available in the temperature module:
  // float temp_out;
  // float coeffs[3];

  // db lookup temperature params
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
      *temp_out = *(float *)temperature_mod->get_p(params, "temp_out");
      coeffs[0]   = ((float *)temperature_mod->get_p(params, "coeffs"))[0];
      coeffs[1]   = ((float *)temperature_mod->get_p(params, "coeffs"))[1];
      coeffs[2]   = ((float *)temperature_mod->get_p(params, "coeffs"))[2];
      dt_print(DT_DEBUG_LIGHTTABLE, "row found\n");
    }
    sqlite3_finalize(stmt);
  }
  dt_print(DT_DEBUG_LIGHTTABLE, "[lighttable] temp_out: %f, coeffs[0]: %f, coeffs[1]: %f coeffs[2]: %f\n", *temp_out, coeffs[0], coeffs[1], coeffs[2]);
  return 0; //success
}


// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
