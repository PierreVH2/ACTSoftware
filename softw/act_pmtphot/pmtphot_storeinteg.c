#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <mysql/mysql.h>
#include <act_ipc.h>
#include <act_log.h>
#include <act_timecoord.h>
#include <act_positastro.h>
#include "pmtphot_storeinteg.h"
#include "pmtfuncs.h"

#define PMTPHOT_LINE_LENGTH  180

char *get_bak_store_dir(MYSQL *conn);

struct storeinteg_objects *create_storeinteg(GtkWidget *container, MYSQL *conn)
{
  if ((container == NULL) || (conn == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameter."));
    return NULL;
  }
  
  char *phot_bak_dir = get_bak_store_dir(conn);
  if (phot_bak_dir == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve photometry fallback storage directory path from SQL server."));
    return NULL;
  }
  
  struct storeinteg_objects *objs = malloc(sizeof(struct storeinteg_objects));
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Failed to allocate memory for integration storage object."));
    free(phot_bak_dir);
    return NULL;
  }
  objs->evb_store_stat = gtk_event_box_new();
  g_object_ref(objs->evb_store_stat);
  GdkColor stat_col;
  gdk_color_parse("#0000AA", &stat_col);
  gtk_widget_modify_bg(objs->evb_store_stat, GTK_STATE_NORMAL, &stat_col);
  gtk_container_add(GTK_CONTAINER(container), objs->evb_store_stat);
  objs->lbl_store_stat = gtk_label_new("STORE N/A");
  g_object_ref(objs->lbl_store_stat);
  gtk_container_add(GTK_CONTAINER(objs->evb_store_stat),objs->lbl_store_stat);
  objs->mysql_conn = conn;
  
  time_t systime_sec = time(NULL);
  struct tm *timedate = gmtime(&systime_sec);
  struct datestruct unidate;
  struct timestruct unitime;
  unidate.year = timedate->tm_year+1900;
  unidate.month = timedate->tm_mon;
  unidate.day = timedate->tm_mday-1;
  unitime.hours = timedate->tm_hour;
  unitime.minutes = timedate->tm_min;
  unitime.seconds = timedate->tm_sec;
  unitime.milliseconds = 0;
  
  int filename_len = strlen(phot_bak_dir);
  if (convert_HMSMS_H_time(&unitime) < 12.0)
  {
    unidate.day--;
    if (unidate.day > 30)
    {
      unidate.month--;
      if (unidate.month > 11)
      {
        unidate.year--;
        unidate.month = 11;
      }
      unidate.day = daysInMonth(&unidate)-1;
    }
  }
  char tmp_phot_filename[filename_len+30];
  sprintf(tmp_phot_filename,"%s%04hd_%02hd_%02hd.dat", phot_bak_dir, unidate.year, unidate.month+1, unidate.day+1);
  objs->bak_phot_fd = fopen(tmp_phot_filename,"a");
  if (objs->bak_phot_fd == NULL)
  {
    act_log_error(act_log_msg("Could not open backup photometry storage file %s.", tmp_phot_filename));
    free(phot_bak_dir);
    return NULL;
  }
  act_log_debug(act_log_msg("Backup photometry storage file: %s", tmp_phot_filename));
  return objs;
}

void finalise_storeinteg(struct storeinteg_objects *objs)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  objs->mysql_conn = NULL;
  fclose(objs->bak_phot_fd);
  objs->bak_phot_fd = NULL;
  g_object_unref(objs->evb_store_stat);
  g_object_unref(objs->lbl_store_stat);
}

void storeinteg(struct storeinteg_objects *objs, struct pmtintegstruct *pmtinteg, int num_buffered)
{
  if ((objs == NULL) || (pmtinteg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters. This should not have happened. Data will be lost."));
    return;
  }
  
  struct pmtintegstruct *lastinteg = pmtinteg;
  char done = lastinteg->done;
  char *qrystr = malloc(180 + num_buffered*PMTPHOT_LINE_LENGTH);
  int num_written = 0;
  unsigned long qrylen;
  qrylen = sprintf(qrystr, "INSERT INTO pmt_phot_raw (modnum, start_date, start_time_h, integt_s, pmt_filt_id, pmt_aper_id, counts, warn, err) VALUES ");
  while (done > 0)
  {
    if (qrylen + PMTPHOT_LINE_LENGTH < sizeof(qrystr))
    {
      act_log_debug(act_log_msg("Extending query string."));
      char *tmpstr = malloc(2*sizeof(qrystr));
      if (tmpstr != NULL)
      {
        sprintf(tmpstr, "%s", qrystr);
        free(qrystr);
        qrystr = tmpstr;
      }
      else
      {
        act_log_error(act_log_msg("Failed to allocate space for extended query string. Data will be lost."));
        break;
      }
    }
    qrylen += sprintf(&qrystr[qrylen], "(%11d, \"%04hd-%02hhd-%02hhd\", %15.10lf, %15.10lf, %11d, %11d, %11lu, %3hhu, %3hhu), ", 1, lastinteg->start_unidate.year, lastinteg->start_unidate.month+1, lastinteg->start_unidate.day+1, convert_HMSMS_H_time(&lastinteg->start_unitime), lastinteg->integt_s, lastinteg->filter.db_id, lastinteg->aperture.db_id, lastinteg->counts, pmt_noncrit_err(lastinteg->error), pmt_crit_err(lastinteg->error));
    num_written++;
    if (lastinteg->next != NULL)
    {
      lastinteg = lastinteg->next;
      done = lastinteg->done;
    }
    else
      done = 0;
  }
  if (num_written != num_buffered)
    act_log_error(act_log_msg("Error: Incorrect number of data written to query string (%d data, %d written).", num_buffered, num_written));
  qrylen -= 2;
  qrystr[qrylen] = '\0';
  if (mysql_query(objs->mysql_conn, qrystr) == 0)
  {
    GdkColor stat_col;
    gdk_color_parse("#00AA00", &stat_col);
    gtk_widget_modify_bg(objs->evb_store_stat, GTK_STATE_NORMAL, &stat_col);
    gtk_label_set_text(GTK_LABEL(objs->lbl_store_stat),"STORE OK");
    return;
  }
  
  act_log_error(act_log_msg("Failed to save photometry to SQL database - %s", mysql_error(objs->mysql_conn)));
  
  if (objs->bak_phot_fd != NULL)
  {
    act_log_error(act_log_msg("Attempting to save to backup file."));
    if (fprintf(objs->bak_phot_fd,"%s\n",qrystr) > 0)
    {
      fflush(objs->bak_phot_fd);
      GdkColor stat_col;
      gdk_color_parse("#AAAA00", &stat_col);
      gtk_widget_modify_bg(objs->evb_store_stat, GTK_STATE_NORMAL, &stat_col);
      gtk_label_set_text(GTK_LABEL(objs->lbl_store_stat),"STORE BACKUP");
      return;
    }
    act_log_error(act_log_msg("Failed to save photometry to backup file."));
  }
  
  act_log_error(act_log_msg("Saving PMT photometry to log - starting here."));
  act_log_error(act_log_msg(qrystr));
  act_log_error(act_log_msg("PMT photometry entries end here."));
  GdkColor stat_col;
  gdk_color_parse("#AA0000", &stat_col);
  gtk_widget_modify_bg(objs->evb_store_stat, GTK_STATE_NORMAL, &stat_col);
  gtk_label_set_text(GTK_LABEL(objs->lbl_store_stat),"STORE LOG");
}

char *get_bak_store_dir(MYSQL *conn)
{
  MYSQL_RES *result;
  MYSQL_ROW row;
  mysql_query(conn,"SELECT conf_value FROM global_config WHERE conf_key LIKE 'bak_pmt_store_dir';");
  result = mysql_store_result(conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve path of directory for fallback photometry storage - %s.", mysql_error(conn)));
    return NULL;
  }
  
  int rowcount = mysql_num_rows(result);
  if ((rowcount <= 0) || (mysql_num_fields(result) != 1))
  {
    act_log_error(act_log_msg("Could not retrieve path of directory for fallback photometry storage - Invalid number of rows/columns returned (%d rows, %d columns).", rowcount, mysql_num_fields(result)));
    mysql_free_result(result);
    return NULL;
  }
  if (rowcount > 1)
    act_log_error(act_log_msg("Configuration database reports %d directories for fallback photometry storage. Only the first will be used", rowcount));
  
  row = mysql_fetch_row(result);
  int tmp_len = strlen(row[0]);
  if (tmp_len < 1)
  {
    act_log_error(act_log_msg("Invalid path of directory for fallback photometry storage returned."));
    mysql_free_result(result);
    return NULL;
  }
  
  char *dir_path;
  if (row[0][tmp_len-1] == '/')
  {
    dir_path = malloc(tmp_len+1);
    sprintf(dir_path,"%s", row[0]);
  }
  else
  {
    dir_path = malloc(tmp_len+2);
    sprintf(dir_path,"%s/", row[0]);
  }
  mysql_free_result(result);
  return dir_path;
}
