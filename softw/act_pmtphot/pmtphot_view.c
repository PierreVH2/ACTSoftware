#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <mysql/mysql.h>
#include <act_timecoord.h>
#include <act_log.h>
#include "pmtphot_view.h"
#include "pmtfuncs.h"

enum
{
  LINETYPE_COMMENT = 0,
  LINETYPE_IDLE,
  LINETYPE_PROBE,
  LINETYPE_TARGNAME,
  LINETYPE_INTEG
};

enum
{
  PHOTSTORE_LINETYPE = 0,
  PHOTSTORE_COMMENT,
  PHOTSTORE_TARGNAME,
  PHOTSTORE_STARTDATE,
  PHOTSTORE_STARTTIME,
  PHOTSTORE_INTEGT_S,
  PHOTSTORE_FILTNAME,
  PHOTSTORE_APERNAME,
  PHOTSTORE_SKY,
  PHOTSTORE_COUNTS,
  PHOTSTORE_WARN,
  PHOTSTORE_ERR,
  PHOTSTORE_NUMCOLS
};

void render_line (GtkTreeViewColumn *tree_column, GtkCellRenderer *cell, GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data);
void view_set_idle_countrate(struct viewobjects *objs, unsigned long countrate);
void view_set_high_counts(struct viewobjects *objs, char high_counts_warn);
void view_set_time_sync(struct viewobjects *objs, char time_sync_warn);
void view_set_zero_counts(struct viewobjects *objs, char zero_counts_err);
void view_set_overflow(struct viewobjects *objs, char overflow_err);
void view_set_overillum(struct viewobjects *objs, char overillum_err);
void view_row_inserted(GtkTreeModel *phot_store, GtkTreePath *path, GtkTreeIter *iter, gpointer user_data);

struct viewobjects *create_view_objects(GtkWidget *container)
{
  if (container == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return NULL;
  }
  struct viewobjects *objs = malloc(sizeof(struct viewobjects));
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return NULL;
  }
  objs->targ_id = 0;
  objs->high_counts_warn = objs->time_sync_warn = 0;
  objs->overflow_err = 0;
  objs->zero_counts_err = objs->overillum_err = 0;

  time_t systime_sec = time(NULL);
  struct tm *timedate = gmtime(&systime_sec);
  objs->unidate.year = timedate->tm_year+1900;
  objs->unidate.month = timedate->tm_mon;
  objs->unidate.day = timedate->tm_mday-1;

  objs->phot_store = GTK_TREE_MODEL(gtk_tree_store_new(PHOTSTORE_NUMCOLS, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_FLOAT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT));
  g_object_ref(objs->phot_store);
  gtk_tree_store_append(GTK_TREE_STORE(objs->phot_store),&objs->targ_iter,NULL);
  gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), &objs->targ_iter, PHOTSTORE_LINETYPE, LINETYPE_COMMENT, PHOTSTORE_COMMENT, "ACT Photometry", -1);
  gtk_tree_store_append(GTK_TREE_STORE(objs->phot_store),&objs->targ_iter,NULL);
  gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), &objs->targ_iter, PHOTSTORE_LINETYPE, LINETYPE_IDLE, PHOTSTORE_COUNTS, 0, -1);
  objs->cur_iter = &objs->targ_iter;

  GtkWidget *scr_photview = gtk_scrolled_window_new(NULL,NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scr_photview), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gtk_container_add(GTK_CONTAINER(container),scr_photview);
  objs->trv_photview = gtk_tree_view_new_with_model(objs->phot_store);
  g_object_ref(objs->trv_photview);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(objs->trv_photview), FALSE);
  gtk_tree_view_set_fixed_height_mode(GTK_TREE_VIEW(objs->trv_photview), TRUE);
  gtk_container_add(GTK_CONTAINER(scr_photview),objs->trv_photview);
  GtkTreeViewColumn * col_phot_line =  gtk_tree_view_column_new ();
  gtk_tree_view_column_set_sizing(col_phot_line, GTK_TREE_VIEW_COLUMN_FIXED);
  gtk_tree_view_append_column(GTK_TREE_VIEW(objs->trv_photview), col_phot_line);
  GtkCellRenderer *rnd_phot_line = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (col_phot_line, rnd_phot_line, TRUE);
  gtk_tree_view_column_set_cell_data_func (col_phot_line, rnd_phot_line, render_line, objs, NULL);
  
  g_signal_connect(G_OBJECT(objs->phot_store), "row-inserted", G_CALLBACK(view_row_inserted), objs->trv_photview);
  
  return objs;
}

void finalise_view_objects(struct viewobjects *objs)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  objs->targ_id = -1;
  g_object_unref(objs->phot_store);
  g_object_unref(objs->trv_photview);
  objs->cur_iter = NULL;
}

void view_set_pmtdetail(struct viewobjects *objs, struct pmtdetailstruct *pmtdetail)
{
  if ((objs == NULL) || (pmtdetail == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  view_set_idle_countrate(objs, pmtdetail->est_counts_s);
  view_set_high_counts(objs, pmt_high_counts(pmtdetail->error));
  view_set_time_sync(objs, pmt_time_desync(pmtdetail->error));
  view_set_zero_counts(objs, pmt_zero_counts(pmtdetail->error));
  view_set_overflow(objs, pmt_overflow(pmtdetail->error));
  view_set_overillum(objs, pmt_overillum(pmtdetail->error));
}

void view_set_targ_id(struct viewobjects *objs, int targ_id, const char *targ_name)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  if (targ_id < 0)
  {
    act_log_error(act_log_msg("Invalid target number specified."));
    return;
  }
  if (targ_id == objs->targ_id)
    return;
  objs->targ_id = targ_id;
  if (targ_id == 0)
  {
    act_log_debug(act_log_msg("targ_id == 0"));
    gtk_tree_store_append(GTK_TREE_STORE(objs->phot_store),&objs->targ_iter,NULL);
    gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), &objs->targ_iter, PHOTSTORE_LINETYPE, LINETYPE_IDLE, PHOTSTORE_COUNTS, 0, -1);
    objs->cur_iter = &objs->targ_iter;
    return;
  }
  
  char *datestr = date_to_str(&objs->unidate);
  if (datestr == NULL)
  {
    datestr = malloc(10);
    sprintf(datestr, "N/A");
  }
  gint cur_linetype;
  gtk_tree_model_get (objs->phot_store, objs->cur_iter, PHOTSTORE_LINETYPE, &cur_linetype, -1);
  if (cur_linetype != LINETYPE_IDLE)
  {
    GtkTreePath *targ_path = gtk_tree_model_get_path(GTK_TREE_MODEL(objs->phot_store), &objs->targ_iter);
    gtk_tree_view_collapse_row (GTK_TREE_VIEW(objs->trv_photview), targ_path);
    gtk_tree_path_free(targ_path);
    gtk_tree_store_append (GTK_TREE_STORE(objs->phot_store), &objs->targ_iter, NULL);
  }
  if (targ_name != NULL)
    gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), &objs->targ_iter, PHOTSTORE_LINETYPE, LINETYPE_TARGNAME, PHOTSTORE_TARGNAME, targ_name, PHOTSTORE_STARTDATE, datestr, -1);
  else
    gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), &objs->targ_iter, PHOTSTORE_LINETYPE, LINETYPE_TARGNAME, PHOTSTORE_TARGNAME, "Unspecified", PHOTSTORE_STARTDATE, datestr, -1);
  free(datestr);
  
  GtkTreePath *targ_path = gtk_tree_model_get_path(GTK_TREE_MODEL(objs->phot_store), &objs->targ_iter);
  gtk_tree_view_expand_row (GTK_TREE_VIEW(objs->trv_photview), targ_path, TRUE);
  gtk_tree_path_free(targ_path);
  gtk_tree_store_append(GTK_TREE_STORE(objs->phot_store),&objs->integ_iter,&objs->targ_iter);
  gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), &objs->integ_iter, PHOTSTORE_LINETYPE, LINETYPE_IDLE, PHOTSTORE_COUNTS, 0, -1);
  objs->cur_iter = &objs->integ_iter;
}

void view_set_date(struct viewobjects *objs, struct datestruct *unidate)
{
  if ((objs == NULL) || (unidate == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  memcpy(&objs->unidate, unidate, sizeof(struct datestruct));
}

void view_probe_start(struct viewobjects *objs)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), objs->cur_iter, PHOTSTORE_LINETYPE, LINETYPE_PROBE, -1);
}

void view_integ_start(struct viewobjects *objs, struct pmtintegstruct *pmtinteg)
{
  if ((objs == NULL) || (pmtinteg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  GtkTreeIter tmp_iter;
  if (!gtk_tree_model_iter_parent(GTK_TREE_MODEL(objs->phot_store), &tmp_iter, objs->cur_iter))
    view_set_targ_id(objs, 1, "ACT_ANY");
  gint cur_linetype;
  gtk_tree_model_get (objs->phot_store, objs->cur_iter, PHOTSTORE_LINETYPE, &cur_linetype, -1);
  if ((cur_linetype != LINETYPE_IDLE) && (cur_linetype != LINETYPE_PROBE))
  {
    gtk_tree_store_append (GTK_TREE_STORE(objs->phot_store), &objs->integ_iter, &objs->targ_iter);
    objs->cur_iter = &objs->integ_iter;
  }
  
  char *timestr = time_to_str(&pmtinteg->start_unitime);
  if (timestr == NULL)
  {
    timestr = malloc(5);
    sprintf(timestr, "N/A");
  }
  gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), objs->cur_iter, PHOTSTORE_LINETYPE, LINETYPE_INTEG, PHOTSTORE_STARTTIME, timestr, PHOTSTORE_FILTNAME, pmtinteg->filter.name, PHOTSTORE_APERNAME, pmtinteg->aperture.name, PHOTSTORE_COUNTS, 0, -1);
}

void view_integ_cancelled(struct viewobjects *objs)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), objs->cur_iter, PHOTSTORE_LINETYPE, LINETYPE_IDLE, PHOTSTORE_COUNTS, 0, -1);
}

void view_integ_complete(struct viewobjects *objs)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), objs->cur_iter, PHOTSTORE_LINETYPE, LINETYPE_IDLE, PHOTSTORE_COUNTS, 0,  -1);
}

void view_integ_error(struct viewobjects *objs, const char* msg)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  GtkTreeIter iter;
  if (gtk_tree_model_iter_has_child (objs->phot_store,&objs->targ_iter))
    gtk_tree_store_append (GTK_TREE_STORE(objs->phot_store), &iter, &objs->targ_iter);
  else
    gtk_tree_store_append (GTK_TREE_STORE(objs->phot_store), &iter, NULL);
  gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), &iter, PHOTSTORE_LINETYPE, LINETYPE_COMMENT, PHOTSTORE_ERR, 1, PHOTSTORE_COMMENT, msg, -1);
}

void view_dispinteg(struct viewobjects *objs, struct pmtintegstruct *pmtinteg)
{
  if ((objs == NULL) || (pmtinteg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  
  gint cur_linetype;
  gtk_tree_model_get (objs->phot_store, objs->cur_iter, PHOTSTORE_LINETYPE, &cur_linetype, -1);
  if (cur_linetype != LINETYPE_INTEG)
    view_integ_start(objs, pmtinteg);
  struct pmtintegstruct *lastinteg = pmtinteg;
  char done = lastinteg->done, *timestr;
  while (done > 0)
  {
    timestr = time_to_str(&pmtinteg->start_unitime);
    if (timestr == NULL)
    {
      timestr = malloc(5);
      sprintf(timestr, "N/A");
    }
    gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), objs->cur_iter, PHOTSTORE_LINETYPE, LINETYPE_INTEG, PHOTSTORE_STARTTIME, timestr, PHOTSTORE_INTEGT_S, lastinteg->integt_s, PHOTSTORE_FILTNAME, lastinteg->filter.name, PHOTSTORE_APERNAME, lastinteg->aperture.name, PHOTSTORE_SKY, lastinteg->sky, PHOTSTORE_COUNTS, lastinteg->counts, PHOTSTORE_ERR, pmt_crit_err(lastinteg->error), PHOTSTORE_WARN, pmt_noncrit_err(lastinteg->error), -1);
    free(timestr);
    gtk_tree_store_append (GTK_TREE_STORE(objs->phot_store), &objs->integ_iter, &objs->targ_iter);
    objs->cur_iter = &objs->integ_iter;
    if (lastinteg->next != NULL)
    {
      lastinteg = (struct pmtintegstruct *)lastinteg->next;
      done = lastinteg->done;
    }
    else
      done = 0;
  }

  timestr = time_to_str(&pmtinteg->start_unitime);
  if (timestr == NULL)
  {
    timestr = malloc(5);
    sprintf(timestr, "N/A");
  }
  gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), objs->cur_iter, PHOTSTORE_LINETYPE, LINETYPE_INTEG, PHOTSTORE_STARTTIME, timestr, PHOTSTORE_FILTNAME, pmtinteg->filter.name, PHOTSTORE_APERNAME, pmtinteg->aperture.name, PHOTSTORE_COUNTS, 0, -1);
  free(timestr);
}

void view_update_integ(struct viewobjects *objs, struct pmtintegstruct *pmtinteg)
{
  if ((objs == NULL) || (pmtinteg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  
//   gint linetype = PHOTSTORE_LINETYPE;
//   gtk_tree_model_get (objs->phot_store, objs->cur_iter, PHOTSTORE_LINETYPE, &cur_linetype, -1);
/*  if (cur_linetype != LINETYPE_INTEG)
  {
    act_log_debug(act_log_msg("No integration is in progress."));
    gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), objs->cur_iter, PHOTSTORE_INTEGT_S, pmtinteg->integt_s, PHOTSTORE_COUNTS, pmtinteg->counts, PHOTSTORE_ERR, pmt_crit_err(pmtinteg->error), PHOTSTORE_WARN, pmt_noncrit_err(pmtinteg->error), -1);
  }*/
  
//   gint counts;
//   gtk_tree_model_get (objs->phot_store, objs->cur_iter, PHOTSTORE_COUNTS, &counts, -1);
//   if (counts == 0)
//   {
//     gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), objs->cur_iter, PHOTSTORE_INTEGT_S, integinfo->integt_elapsed_ms/1000.0, PHOTSTORE_COUNTS, integinfo->counts, -1);
//     return;
//   }

  gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), objs->cur_iter, PHOTSTORE_LINETYPE, LINETYPE_INTEG, PHOTSTORE_INTEGT_S, pmtinteg->integt_s, PHOTSTORE_COUNTS, pmtinteg->counts, PHOTSTORE_ERR, pmt_crit_err(pmtinteg->error), PHOTSTORE_WARN, pmt_noncrit_err(pmtinteg->error), -1);
}

void render_line (GtkTreeViewColumn *col_phot_line, GtkCellRenderer *rnd_phot_line, GtkTreeModel *phot_store, GtkTreeIter *iter, gpointer user_data)
{
  (void) col_phot_line;
  (void) user_data;
  gint linetype;
  gtk_tree_model_get(phot_store, iter, PHOTSTORE_LINETYPE, &linetype, -1);
  switch(linetype)
  {
    case LINETYPE_COMMENT:
    {
      gint warn, err;
      gchar *comment;
      gtk_tree_model_get(phot_store, iter, PHOTSTORE_COMMENT, &comment, PHOTSTORE_WARN, &warn, PHOTSTORE_ERR, &err, -1);
      if (err)
        g_object_set(rnd_phot_line, "foreground", "#AA0000", "weight", 600, "style", PANGO_STYLE_NORMAL, "text", comment, NULL);
      else if (warn)
        g_object_set(rnd_phot_line, "foreground", "#AAAA00", "weight", 600, "style", PANGO_STYLE_NORMAL, "text", comment, NULL);
      else
        g_object_set(rnd_phot_line, "foreground", "#000000", "weight", 600, "style", PANGO_STYLE_NORMAL, "text", comment, NULL);
      g_free(comment);
      break;
    }
    case LINETYPE_IDLE:
    {
      gint warn, err, counts;
      gtk_tree_model_get(phot_store, iter, PHOTSTORE_WARN, &warn, PHOTSTORE_ERR, &err, PHOTSTORE_COUNTS, &counts, -1);
      char line[60];
      snprintf(line, 59, "Idle (%d counts/sec)", counts);
      if (err)
        g_object_set(rnd_phot_line, "foreground", "#AA0000", "weight", 400, "style", PANGO_STYLE_ITALIC, "text", line, NULL);
      else if (warn)
        g_object_set(rnd_phot_line, "foreground", "#AAAA00", "weight", 400, "style", PANGO_STYLE_ITALIC, "text", line, NULL);
      else
        g_object_set(rnd_phot_line, "foreground", "#000000", "weight", 400, "style", PANGO_STYLE_ITALIC, "text", line, NULL);
      break;
    }
    case LINETYPE_PROBE:
    {
      gint warn, err, counts;
      gtk_tree_model_get(phot_store, iter, PHOTSTORE_WARN, &warn, PHOTSTORE_ERR, &err, PHOTSTORE_COUNTS, &counts, -1);
      char line[60];
      snprintf(line, 59, "Probing (%d counts/sec)", counts);
      if (err)
        g_object_set(rnd_phot_line, "foreground", "#AA0000", "weight", 400, "style", PANGO_STYLE_ITALIC, "text", line, NULL);
      else if (warn)
        g_object_set(rnd_phot_line, "foreground", "#AAAA00", "weight", 400, "style", PANGO_STYLE_ITALIC, "text", line, NULL);
      else
        g_object_set(rnd_phot_line, "foreground", "#000000", "weight", 400, "style", PANGO_STYLE_ITALIC, "text", line, NULL);
      break;
    }
    case LINETYPE_TARGNAME:
    {
      gchar *targname, *datestr;
      gtk_tree_model_get(phot_store, iter, PHOTSTORE_TARGNAME, &targname, PHOTSTORE_STARTDATE, &datestr, -1);
      char line[60];
      snprintf(line, 59, "%s (%s)", targname, datestr);
      g_free(targname);
      g_free(datestr);
      g_object_set(rnd_phot_line, "foreground", "#000000", "weight", 500, "style", PANGO_STYLE_NORMAL, "text", line, NULL);
      break;
    }
    case LINETYPE_INTEG:
    {
      gint counts, sky, err, warn;
      gfloat integt_s;
      gchar *starttime, *filtname, *apername;
      gtk_tree_model_get(phot_store, iter, PHOTSTORE_STARTTIME, &starttime, PHOTSTORE_INTEGT_S, &integt_s, PHOTSTORE_FILTNAME, &filtname, PHOTSTORE_APERNAME, &apername, PHOTSTORE_SKY, &sky, PHOTSTORE_COUNTS, &counts, PHOTSTORE_WARN, &warn, PHOTSTORE_ERR, &err, -1);
      char line[60];
      snprintf(line, 59, "%s %10.3f  %5s %5s  %c  %15d", starttime, integt_s, filtname, apername, sky ? ' ' : '*', counts);
      g_free(starttime);
      g_free(filtname);
      g_free(apername);
      if (err)
        g_object_set(rnd_phot_line, "foreground", "#AA0000", "weight", 400, "style", PANGO_STYLE_NORMAL, "text", line, NULL);
      else if (warn)
        g_object_set(rnd_phot_line, "foreground", "#AAAA00", "weight", 400, "style", PANGO_STYLE_NORMAL, "text", line, NULL);
      else
        g_object_set(rnd_phot_line, "foreground", "#000000", "weight", 400, "style", PANGO_STYLE_NORMAL, "text", line, NULL);
      break;
    }
    default:
      act_log_error(act_log_msg("Invalid line type."));
  }
}

void view_set_idle_countrate(struct viewobjects *objs, unsigned long countrate)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  gint linetype;
  gtk_tree_model_get(objs->phot_store, objs->cur_iter, PHOTSTORE_LINETYPE, &linetype, -1);
  if ((linetype != LINETYPE_IDLE) && (linetype != LINETYPE_PROBE))
    return;
  gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), objs->cur_iter, PHOTSTORE_COUNTS, countrate, -1);
}

void view_set_high_counts(struct viewobjects *objs, char high_counts_warn)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  if (objs->high_counts_warn == high_counts_warn)
    return;
  gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), objs->cur_iter, PHOTSTORE_WARN, high_counts_warn > 0, -1);
  if (high_counts_warn > 0)
  {
    GtkTreeIter new_iter;
    gtk_tree_store_insert_before (GTK_TREE_STORE(objs->phot_store), &new_iter, NULL, objs->cur_iter);
    gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), &new_iter, PHOTSTORE_LINETYPE, LINETYPE_COMMENT, PHOTSTORE_COMMENT, "High count rate", PHOTSTORE_WARN, 1, -1);
  }
  objs->high_counts_warn = high_counts_warn;
}

void view_set_time_sync(struct viewobjects *objs, char time_sync_warn)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  if (objs->time_sync_warn == time_sync_warn)
    return;
  gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), objs->cur_iter, PHOTSTORE_WARN, time_sync_warn > 0, -1);
  if (time_sync_warn > 0)
  {
    GtkTreeIter new_iter;
    gtk_tree_store_insert_before (GTK_TREE_STORE(objs->phot_store), &new_iter, NULL, objs->cur_iter);
    gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), &new_iter, PHOTSTORE_LINETYPE, LINETYPE_COMMENT, PHOTSTORE_COMMENT, "Time sync loss", PHOTSTORE_WARN, 1, -1);
  }
  objs->time_sync_warn = time_sync_warn;
}

void view_set_zero_counts(struct viewobjects *objs, char zero_counts_err)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  if (objs->zero_counts_err == zero_counts_err)
    return;
  gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), objs->cur_iter, PHOTSTORE_ERR, zero_counts_err > 0, -1);
  if (zero_counts_err > 0)
  {
    GtkTreeIter new_iter;
    gtk_tree_store_insert_before (GTK_TREE_STORE(objs->phot_store), &new_iter, NULL, objs->cur_iter);
    gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), &new_iter, PHOTSTORE_LINETYPE, LINETYPE_COMMENT, PHOTSTORE_COMMENT, "Zero counts", PHOTSTORE_ERR, 1, -1);
  }
  objs->zero_counts_err = zero_counts_err;
}

void view_set_overflow(struct viewobjects *objs, char overflow_err)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  if (objs->overflow_err == overflow_err)
    return;
  gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), objs->cur_iter, PHOTSTORE_ERR, overflow_err > 0, -1);
  if (overflow_err > 0)
  {
    GtkTreeIter new_iter;
    gtk_tree_store_insert_before (GTK_TREE_STORE(objs->phot_store), &new_iter, NULL, objs->cur_iter);
    gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), &new_iter, PHOTSTORE_LINETYPE, LINETYPE_COMMENT, PHOTSTORE_COMMENT, "OVERFLOW", PHOTSTORE_ERR, 1, -1);
  }
  objs->overflow_err = overflow_err;
}

void view_set_overillum(struct viewobjects *objs, char overillum_err)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  if (objs->overillum_err == overillum_err)
    return;
  gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), objs->cur_iter, PHOTSTORE_ERR, overillum_err > 0, -1);
  if (overillum_err > 0)
  {
    GtkTreeIter new_iter;
    gtk_tree_store_insert_before (GTK_TREE_STORE(objs->phot_store), &new_iter, NULL, objs->cur_iter);
    gtk_tree_store_set(GTK_TREE_STORE(objs->phot_store), &new_iter, PHOTSTORE_LINETYPE, LINETYPE_COMMENT, PHOTSTORE_COMMENT, "OVERILLUMINATION", PHOTSTORE_ERR, 1, -1);
  }
  objs->overillum_err = overillum_err;
}

void view_row_inserted(GtkTreeModel *phot_store, GtkTreePath *path, GtkTreeIter *iter, gpointer user_data)
{
  (void) path;
  (void) iter;
  (void) phot_store;
  GtkAdjustment *adjustment = gtk_tree_view_get_vadjustment(GTK_TREE_VIEW(user_data));
  double value, upper, pagesize, stepsize;
  value = gtk_adjustment_get_value(adjustment);
  upper = gtk_adjustment_get_upper(adjustment);
  pagesize = gtk_adjustment_get_page_size(adjustment);
  stepsize = gtk_adjustment_get_step_increment(adjustment);
  if (value + pagesize + 2*stepsize >= upper)
    gtk_adjustment_set_value(adjustment, upper);
}

