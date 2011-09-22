/*
    This file is part of darktable,
    copyright (c) 2009--2011 johannes hanika.

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
#include "common/image.h"
#include "common/image_cache.h"
#include "common/imageio.h"
#include "common/exif.h"
#include "common/debug.h"
#include "common/tags.h"
#include "control/control.h"
#include "control/conf.h"
#include "control/jobs.h"
#include <math.h>
#include <sqlite3.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <assert.h>
#include <glob.h>
#include <glib/gstdio.h>

int dt_image_is_ldr(const dt_image_t *img)
{
  const char *c = img->filename + strlen(img->filename);
  while(*c != '.' && c > img->filename) c--;
  if(!strcasecmp(c, ".jpg") || !strcasecmp(c, ".png") || !strcasecmp(c, ".ppm") || (img->flags & DT_IMAGE_LDR)) return 1;
  else return 0;
}

const char *
dt_image_film_roll_name(const char *path)
{
  const char *folder = path + strlen(path);
  int numparts = CLAMPS(dt_conf_get_int("show_folder_levels"), 1, 5);
  int count = 0;
  if (numparts < 1)
    numparts = 1;
  while (folder > path)
  {
    if (*folder == '/')
      if (++count >= numparts)
      {
        ++folder;
        break;
      }
    --folder;
  }
  return folder;
}

void dt_image_film_roll(dt_image_t *img, char *pathname, int len)
{
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select folder from film_rolls where id = ?1", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, img->film_id);
  if(sqlite3_step(stmt) == SQLITE_ROW)
  {
    char *f = (char *)sqlite3_column_text(stmt, 0);
    const char *c = dt_image_film_roll_name(f);
    snprintf(pathname, len, "%s", c);
  }
  else
  {
    snprintf(pathname, len, "%s", _("orphaned image"));
  }
  sqlite3_finalize(stmt);
  pathname[len-1] = '\0';
}

void dt_image_full_path(const int imgid, char *pathname, int len)
{
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select folder || '/' || filename from images, film_rolls where images.film_id = film_rolls.id and images.id = ?1", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
  if(sqlite3_step(stmt) == SQLITE_ROW)
  {
    g_strlcpy(pathname, (char *)sqlite3_column_text(stmt, 0), len);
  }
  sqlite3_finalize(stmt);
}

void dt_image_path_append_version(int imgid, char *pathname, const int len)
{
  // get duplicate suffix
  int version = 0;
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select count(id) from images where filename in (select filename from images where id = ?1) and film_id in (select film_id from images where id = ?1) and id < ?1", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
  if(sqlite3_step(stmt) == SQLITE_ROW)
    version = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  if(version != 0)
  {
    // add version information:
    char *filename = g_strdup(pathname);

    char *c = pathname + strlen(pathname);
    while(*c != '.' && c > pathname) c--;
    snprintf(c, pathname + len - c, "_%02d", version);
    char *c2 = filename + strlen(filename);
    while(*c2 != '.' && c2 > filename) c2--;
    snprintf(c+3, pathname + len - c - 3, "%s", c2);
  }
}

void dt_image_print_exif(dt_image_t *img, char *line, int len)
{
  if(img->exif_exposure >= 0.1f)
    snprintf(line, len, "%.1f'' f/%.1f %dmm iso %d", img->exif_exposure, img->exif_aperture, (int)img->exif_focal_length, (int)img->exif_iso);
  else
    snprintf(line, len, "1/%.0f f/%.1f %dmm iso %d", 1.0/img->exif_exposure, img->exif_aperture, (int)img->exif_focal_length, (int)img->exif_iso);
}

// FIXME: use a history stack item for this.
#if 0
void dt_image_flip(const int32_t imgid, const int32_t cw)
{
  dt_image_t *img = dt_image_cache_get (imgid, 'r');
  int8_t orientation = dt_image_orientation(img);

  if(cw == 1)
  {
    if(orientation & 4) orientation ^= 1;
    else                orientation ^= 2; // flip x
  }
  else
  {
    if(orientation & 4) orientation ^= 2;
    else                orientation ^= 1; // flip y
  }
  orientation ^= 4;             // flip axes

  if(cw == 2) orientation = -1; // reset
  img->raw_params.user_flip = orientation;
  img->force_reimport = 1;
  img->dirty = 1;
  dt_image_invalidate(img, DT_IMAGE_MIPF);
  dt_image_invalidate(img, DT_IMAGE_FULL);
  dt_image_cache_flush(img);
  dt_image_cache_release(img, 'r');
}
#endif

int32_t dt_image_duplicate(const int32_t imgid)
{
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "insert into images "
                              "(id, film_id, width, height, filename, maker, model, lens, exposure, aperture, iso, "
                              "focal_length, focus_distance, datetime_taken, flags, output_width, output_height, crop, "
                              "raw_parameters, raw_denoise_threshold, raw_auto_bright_threshold, raw_black, raw_maximum, orientation) "
                              "select null, film_id, width, height, filename, maker, model, lens, exposure, aperture, iso, "
                              "focal_length, focus_distance, datetime_taken, flags, width, height, crop, "
                              "raw_parameters, raw_denoise_threshold, raw_auto_bright_threshold, raw_black, raw_maximum, orientation "
                              "from images where id = ?1", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select a.id from images as a join images as b where a.film_id = b.film_id and a.filename = b.filename and b.id = ?1 order by a.id desc", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
  int32_t newid = -1;
  if(sqlite3_step(stmt) == SQLITE_ROW) newid = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  if(newid != -1)
  {
    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "insert into color_labels (imgid, color) select ?1, color from color_labels where imgid = ?2", -1, &stmt, NULL);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, newid);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, imgid);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "insert into meta_data (id, key, value) select ?1, key, value from meta_data where id = ?2", -1, &stmt, NULL);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, newid);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, imgid);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "insert into tagged_images (imgid, tagid) select ?1, tagid from tagged_images where imgid = ?2", -1, &stmt, NULL);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, newid);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, imgid);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "update tagxtag set count = count + 1 where "
                                "(id1 in (select tagid from tagged_images where imgid = ?1)) or "
                                "(id2 in (select tagid from tagged_images where imgid = ?1))", -1, &stmt, NULL);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, newid);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  return newid;
}

// TODO: move to image cache api, leave function here.
void dt_image_remove(const int32_t imgid)
{
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "delete from images where id = ?1", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "update tagxtag set count = count - 1 where "
                              "(id2 in (select tagid from tagged_images where imgid = ?1)) or "
                              "(id1 in (select tagid from tagged_images where imgid = ?1))", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "delete from tagged_images where imgid = ?1", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "delete from history where imgid = ?1", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "delete from color_labels where imgid = ?1", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "delete from meta_data where id = ?1", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "delete from selected_images where imgid = ?1", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  dt_image_cache_clear(imgid);
  // TODO: also clear all thumbnails in mipmap_cache? done inside the above?
  // TODO: should also be called dt_image_cache_remove().
}

int dt_image_altered(const dt_image_t *img)
{
  int altered = 0;
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select num from history where imgid = ?1", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, img->id);
  if(sqlite3_step(stmt) == SQLITE_ROW) altered = 1;
  sqlite3_finalize(stmt);
  return altered;
}


// TODO: move to mipmap_cache.c (at least the guts of it)
int dt_image_import(const int32_t film_id, const char *filename, gboolean override_ignore_jpegs)
{
  if(!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) return 0;
  const char *cc = filename + strlen(filename);
  for(; *cc!='.'&&cc>filename; cc--);
  if(!strcmp(cc, ".dt")) return 0;
  if(!strcmp(cc, ".dttags")) return 0;
  if(!strcmp(cc, ".xmp")) return 0;
  char *ext = g_ascii_strdown(cc+1, -1);
  if(override_ignore_jpegs == FALSE && (!strcmp(ext, "jpg") || !strcmp(ext, "jpeg")) && dt_conf_get_bool("ui_last/import_ignore_jpegs"))
    return 0;
  int supported = 0;
  char **extensions = g_strsplit(dt_supported_extensions, ",", 100);
  for(char **i=extensions; *i!=NULL; i++)
    if(!strcmp(ext, *i))
    {
      supported = 1;
      break;
    }
  g_strfreev(extensions);
  if(!supported)
  {
    g_free(ext);
    return 0;
  }
  int rc;
  int ret = 0, id = -1;
  // select from images; if found => return
  gchar *imgfname;
  imgfname = g_path_get_basename((const gchar*)filename);
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select id from images where film_id = ?1 and filename = ?2", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, film_id);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 2, imgfname, strlen(imgfname), SQLITE_STATIC);
  if(sqlite3_step(stmt) == SQLITE_ROW)
  {
    id = sqlite3_column_int(stmt, 0);
    g_free(imgfname);
    sqlite3_finalize(stmt);
    ret = dt_image_open(id); // image already in db, open this.
    g_free(ext);
    if(ret) return 0;
    else return id;
  }
  sqlite3_finalize(stmt);

  // insert dummy image entry in database
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "insert into images (id, film_id, filename, caption, description, license, sha1sum) values (null, ?1, ?2, '', '', '', '')", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, film_id);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 2, imgfname, strlen(imgfname), SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) fprintf(stderr, "sqlite3 error %d\n", rc);
  sqlite3_finalize(stmt);

  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select id from images where film_id = ?1 and filename = ?2", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, film_id);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 2, imgfname, strlen(imgfname), SQLITE_STATIC);
  if(sqlite3_step(stmt) == SQLITE_ROW) id = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  // printf("[image_import] importing `%s' to img id %d\n", imgfname, id);
  // FIXME: this has to go away!
  dt_image_t *img = dt_image_cache_get_uninited(id, 'w');
  g_strlcpy(img->filename, imgfname, DT_MAX_PATH);
  img->id = id;
  img->film_id = film_id;
  img->dirty = 1;

  // read dttags and exif for database queries!
  (void) dt_exif_read(img, filename);
  char dtfilename[DT_MAX_PATH];
  g_strlcpy(dtfilename, filename, DT_MAX_PATH);
  dt_image_path_append_version(img->id, dtfilename, DT_MAX_PATH);
  char *c = dtfilename + strlen(dtfilename);
  sprintf(c, ".xmp");
  (void)dt_exif_xmp_read(img, dtfilename, 0);

  // add a tag with the file extension
  guint tagid = 0;
  char tagname[512];
  snprintf(tagname, 512, "darktable|format|%s", ext);
  g_free(ext);
  dt_tag_new(tagname, &tagid);
  dt_tag_attach(tagid,id);

  dt_image_cache_flush_no_sidecars(img);

  dt_image_cache_release(img, 'w');

  // Search for sidecar files and import them if found.
  glob_t *globbuf = malloc(sizeof(glob_t));

  // Add version wildcard
  gchar *fname = g_strdup(filename);
  gchar pattern[DT_MAX_PATH];
  g_snprintf(pattern, DT_MAX_PATH, "%s", filename);
  char *c1 = pattern + strlen(pattern);
  while(*c1 != '.' && c1 > pattern) c1--;
  snprintf(c1, pattern + DT_MAX_PATH - c1, "_*");
  char *c2 = fname + strlen(fname);
  while(*c2 != '.' && c2 > fname) c2--;
  snprintf(c1+2, pattern + DT_MAX_PATH - c1 - 2, "%s.xmp", c2);

  if (!glob(pattern, 0, NULL, globbuf))
  {
    for (int i=0; i < globbuf->gl_pathc; i++)
    {
      int newid = -1;
      newid = dt_image_duplicate(id);

      dt_image_t *newimg = dt_image_cache_get(newid, 'w');
      (void)dt_exif_xmp_read(newimg, globbuf->gl_pathv[i], 0);

      dt_image_cache_flush_no_sidecars(newimg);
      dt_image_cache_release(newimg, 'w');
    }
    globfree(globbuf);
  }

  g_free(imgfname);
  g_free(fname);

  return id;
}

void dt_image_init(dt_image_t *img)
{
  for(int k=0; (int)k<(int)DT_IMAGE_MIPF; k++) img->mip[k] = NULL;
  memset(img->lock,0, sizeof(dt_image_lock_t)*DT_IMAGE_NONE);
  img->output_width = img->output_height = img->width = img->height = 0;
  img->orientation = -1;

  img->filters = 0;
  img->bpp = 0;
  img->film_id = -1;
  img->flags = dt_conf_get_int("ui_last/import_initial_rating");
  if(img->flags < 0 || img->flags > 4)
  {
    img->flags = 1;
    dt_conf_set_int("ui_last/import_initial_rating", 1);
  }
  img->id = -1;
  img->dirty = 0;
  img->exif_inited = 0;
  memset(img->exif_maker, 0, sizeof(img->exif_maker));
  memset(img->exif_model, 0, sizeof(img->exif_model));
  memset(img->exif_lens, 0, sizeof(img->exif_lens));
  memset(img->filename, 0, sizeof(img->filename));
  g_strlcpy(img->filename, "(unknown)", 10);
  img->exif_model[0] = img->exif_maker[0] = img->exif_lens[0] = '\0';
  g_strlcpy(img->exif_datetime_taken, "0000:00:00 00:00:00", sizeof(img->exif_datetime_taken));
  img->exif_crop = 1.0;
  img->exif_exposure = img->exif_aperture = img->exif_iso = img->exif_focal_length = img->exif_focus_distance = 0;
}

// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
