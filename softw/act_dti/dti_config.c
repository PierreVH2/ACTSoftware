#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mysql/mysql.h>
#include <act_log.h>
#include <act_timecoord.h>
#include "dti_config.h"

#define TRUE  1
#define FALSE 0

enum
{
  SOFT_LIM_NUM_W = 1,
  SOFT_LIM_NUM_E,
  SOFT_LIM_NUM_N,
  SOFT_LIM_NUM_S,
  SOFT_LIM_NUM_ALT
};

char parse_tel_limits(MYSQL *conn, struct act_msg_targcap *targcap_msg);
char parse_pmtfilters(MYSQL *conn, struct act_msg_pmtcap *pmtcap_msg);
char parse_pmtapertures(MYSQL *conn, struct act_msg_pmtcap *pmtcap_msg);
char parse_ccdfilters(MYSQL *conn, struct act_msg_ccdcap *ccdcap_msg);

char parse_config(const char *sqlhost, struct act_msg_targcap *targcap_msg, struct act_msg_pmtcap *pmtcap_msg, struct act_msg_ccdcap *ccdcap_msg)
{
  MYSQL *sql_conn = mysql_init(NULL);
  if (sql_conn == NULL)
    act_log_error(act_log_msg("Error initialising MySQL connection handler."));
  else if (mysql_real_connect(sql_conn, sqlhost, "act_dti", NULL, "actnew", 0, NULL, 0) == NULL)
  {
    act_log_error(act_log_msg("Error connecting to MySQL database - %s.", mysql_error(sql_conn)));
    sql_conn = NULL;
  }
  if (sql_conn == NULL)
    return FALSE;
  
  struct act_msg_targcap tmp_targcap;
  memcpy(&tmp_targcap, targcap_msg, sizeof(struct act_msg_targcap));
  if (!parse_tel_limits(sql_conn, &tmp_targcap))
  {
    act_log_error(act_log_msg("Failed to load telescope capabilities."));
    return FALSE;
  }
  
  struct act_msg_pmtcap tmp_pmtcap;
  memcpy(&tmp_pmtcap, pmtcap_msg, sizeof(struct act_msg_pmtcap));
  if (!parse_pmtfilters(sql_conn, &tmp_pmtcap))
  {
    act_log_error(act_log_msg("Failed to load PMT capabilities."));
    return FALSE;
  }
  if (!parse_pmtapertures(sql_conn, &tmp_pmtcap))
  {
    act_log_error(act_log_msg("Failed to load PMT capabilities."));
    return FALSE;
  }
  
  struct act_msg_ccdcap tmp_ccdcap;
  memcpy(&tmp_ccdcap, ccdcap_msg, sizeof(struct act_msg_ccdcap));
  if (!parse_ccdfilters(sql_conn, &tmp_ccdcap))
  {
    act_log_error(act_log_msg("Failed to load CCD capabilities."));
    return FALSE;
  }

  memcpy(targcap_msg, &tmp_targcap, sizeof(struct act_msg_targcap));
  memcpy(pmtcap_msg, &tmp_pmtcap, sizeof(struct act_msg_pmtcap));
  memcpy(ccdcap_msg, &tmp_ccdcap, sizeof(struct act_msg_ccdcap));
  return TRUE;
}

char parse_pmtfilters(MYSQL *conn, struct act_msg_pmtcap *pmtcap_msg)
{
  MYSQL_RES *result;
  MYSQL_ROW row;
  mysql_query(conn,"SELECT id,slot,name FROM pmt_filters WHERE slot >= 0 ORDER BY slot;");
  result = mysql_store_result(conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve PMT filters configuration information - %s.", mysql_error(conn)));
    return FALSE;
  }
  
  int rowcount = mysql_num_rows(result);
  if ((rowcount <= 0) || (mysql_num_fields(result) != 3))
  {
    act_log_error(act_log_msg("Could not retrieve PMT filters configuration information - Invalid number of rows/columns returned (%d rows, %d columns).", rowcount, mysql_num_fields(result)));
    return FALSE;
  }
  if (rowcount > IPC_MAX_NUM_FILTAPERS)
  {
    act_log_error(act_log_msg("Configuration database reports %d installed PMT filters, but only the first %d can be handled. Ignoring the last %d filters.", rowcount, IPC_MAX_NUM_FILTAPERS, IPC_MAX_NUM_FILTAPERS - rowcount));
    rowcount = IPC_MAX_NUM_FILTAPERS;
  }
  
  int i;
  int tmp_slot, tmp_id, num_filts = 0;
  for (i=0; i<rowcount; i++)
  {
    row = mysql_fetch_row(result);
    if (sscanf(row[0], "%d", &tmp_id) != 1)
    {
      act_log_error(act_log_msg("Error parsing PMT filter database id number (%s).", row[0]));
      continue;
    }
    if (sscanf(row[1], "%d", &tmp_slot) != 1)
    {
      act_log_error(act_log_msg("Error parsing PMT filter slot number (%s).", row[1]));
      continue;
    }
    pmtcap_msg->filters[num_filts].db_id = tmp_id;
    pmtcap_msg->filters[num_filts].slot = tmp_slot;
    if (strlen(row[2]) >= IPC_MAX_FILTAPER_NAME_LEN-1)
      act_log_error(act_log_msg("PMT filter %d's identifier string (%s) is too long (%d characters). Trimming to %d characters.", tmp_slot, row[1], strlen(row[1]), IPC_MAX_FILTAPER_NAME_LEN));
    snprintf(pmtcap_msg->filters[num_filts].name, IPC_MAX_FILTAPER_NAME_LEN, "%s", row[2]);
    num_filts++;
  }
  for (i=num_filts; i<IPC_MAX_NUM_FILTAPERS; i++)
    pmtcap_msg->filters[i].db_id = -1;
  mysql_free_result(result);
  return TRUE;
}

char parse_tel_limits(MYSQL *conn, struct act_msg_targcap *targcap_msg)
{
  MYSQL_RES *result;
  MYSQL_ROW row;
  mysql_query(conn,"SELECT IFNULL(gn.conf_value,\"100.0\"), IFNULL(gs.conf_value,\"-200.0\"), IFNULL(ge.conf_value, \"-20.0\"), IFNULL(gw.conf_value, \"20.0\") FROM (SELECT 0 AS t) AS s LEFT JOIN (SELECT 0 AS t, conf_value FROM global_config WHERE conf_key=\"soft_lim_N\") AS gn ON gn.t=s.t LEFT JOIN (SELECT 0 AS t, conf_value FROM global_config WHERE conf_key=\"soft_lim_S\") AS gs ON gs.t=s.t LEFT JOIN (SELECT 0 AS t, conf_value FROM global_config WHERE conf_key=\"soft_lim_E\") AS ge ON ge.t=s.t LEFT JOIN (SELECT 0 AS t, conf_value FROM global_config WHERE conf_key=\"soft_lim_W\") AS gw ON gw.t=s.t;");
  result = mysql_store_result(conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve soft limits - %s.", mysql_error(conn)));
    return FALSE;
  }
  
  int rowcount = mysql_num_rows(result);
  if ((mysql_num_rows(result) != 1) || (mysql_num_fields(result) != 4))
  {
    act_log_error(act_log_msg("Could not retrieve soft limits - Invalid number of rows/columns returned (%d rows, %d columns).", mysql_num_rows(result), mysql_num_fields(result)));
    return FALSE;
  }
  
  row = mysql_fetch_row(result);
  double tmp_lim_N, tmp_lim_S, tmp_lim_E, tmp_lim_W;
  char num_parsed = 0;
  if (sscanf(row[0], "%lf", &tmp_lim_N) != 1)
    act_log_error(act_log_msg("Error parsing Northern limit (%s).", row[0]));
  else if (tmp_lim_N > 90.0)
    act_log_error(act_log_msg("Invalid Northern limit: %lf", tmp_lim_N));
  else
    num_parsed++;
  if (sscanf(row[1], "%lf", &tmp_lim_S) != 1)
    act_log_error(act_log_msg("Error parsing Southern limit (%s).", row[1]));
  else if (tmp_lim_S < 180.0)
    act_log_error(act_log_msg("Invalid Southern limit: %lf", tmp_lim_S));
  else
    num_parsed++;
  if (sscanf(row[2], "%lf", &tmp_lim_E) != 1)
    act_log_error(act_log_msg("Error parsing Eastern limit (%s).", row[2]));
  else if (tmp_lim_E < -12.0)
    act_log_error(act_log_msg("Invalid Eastern limit: %lf", tmp_lim_E));
  else
    num_parsed++;
  if (sscanf(row[3], "%lf", &tmp_lim_W) != 1)
    act_log_error(act_log_msg("Error parsing Western limit (%s).", row[3]));
  else if (tmp_lim_W > 12.0)
    act_log_error(act_log_msg("Invalid Western limit: %lf", tmp_lim_W));
  else
    num_parsed++;
  
  mysql_free_result(result);
  if (num_parsed != 4)
  {
    act_log_error(act_log_msg("Not all software limits parsed."));
    return FALSE;
  }
  convert_H_HMSMS_ha(tmp_lim_W, &targcap_msg->ha_lim_W);
  convert_H_HMSMS_ha(tmp_lim_E, &targcap_msg->ha_lim_E);
  convert_D_DMS_dec(tmp_lim_N, &targcap_msg->dec_lim_N);
  convert_D_DMS_dec(tmp_lim_S, &targcap_msg->dec_lim_S);
}

char parse_pmtapertures(MYSQL *conn, struct act_msg_pmtcap *pmtcap_msg)
{
  MYSQL_RES *result;
  MYSQL_ROW row;
  mysql_query(conn,"SELECT id,slot,name FROM pmt_apertures WHERE slot >= 0 ORDER BY slot;");
  result = mysql_store_result(conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve PMT apertures configuration information - %s.", mysql_error(conn)));
    return FALSE;
  }
  
  int rowcount = mysql_num_rows(result);
  if ((rowcount <= 0) || (mysql_num_fields(result) != 3))
  {
    act_log_error(act_log_msg("Could not retrieve PMT apertures configuration information - Invalid number of rows/columns returned (%d rows, %d columns).", rowcount, mysql_num_fields(result)));
    return FALSE;
  }
  if (rowcount > IPC_MAX_NUM_FILTAPERS)
  {
    act_log_error(act_log_msg("Configuration database reports %d installed PMT apertures, but only the first %d can be handled. Ignoring the last %d apertures.", rowcount, IPC_MAX_NUM_FILTAPERS, IPC_MAX_NUM_FILTAPERS - rowcount));
    rowcount = IPC_MAX_NUM_FILTAPERS;
  }

  int i;
  int tmp_slot, tmp_id, num_apers = 0;
  for (i=0; i<rowcount; i++)
  {
    row = mysql_fetch_row(result);
    if (sscanf(row[0], "%d", &tmp_id) != 1)
    {
      act_log_error(act_log_msg("Error parsing PMT aperture database id number (%s).", row[0]));
      continue;
    }
    if (sscanf(row[1], "%d", &tmp_slot) != 1)
    {
      act_log_error(act_log_msg("Error parsing PMT aperture slot number (%s).", row[1]));
      continue;
    }
    pmtcap_msg->apertures[num_apers].db_id = tmp_id;
    pmtcap_msg->apertures[num_apers].slot = tmp_slot;
    if (strlen(row[2]) >= IPC_MAX_FILTAPER_NAME_LEN-1)
      act_log_error(act_log_msg("PMT aperture %d's identifier string (%s) is too long (%d characters). Trimming to %d characters.", tmp_slot, row[1], strlen(row[1]), IPC_MAX_FILTAPER_NAME_LEN));
    snprintf(pmtcap_msg->apertures[num_apers].name, IPC_MAX_FILTAPER_NAME_LEN, "%s", row[2]);
    num_apers++;
  }
  for (i=num_apers; i<IPC_MAX_NUM_FILTAPERS; i++)
    pmtcap_msg->apertures[i].db_id = -1;
  mysql_free_result(result);
  return TRUE;
}

char parse_ccdfilters(MYSQL *conn, struct act_msg_ccdcap *ccdcap_msg)
{
  MYSQL_RES *result;
  MYSQL_ROW row;
  mysql_query(conn,"SELECT id,slot,name FROM ccd_filters WHERE slot >= 0 ORDER BY slot;");
  result = mysql_store_result(conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve CCD filters configuration information - %s.", mysql_error(conn)));
    return FALSE;
  }
  
  int rowcount = mysql_num_rows(result);
  if ((rowcount <= 0) || (mysql_num_fields(result) != 3))
  {
    act_log_error(act_log_msg("Could not retrieve CCD filters configuration information - Invalid number of rows/columns returned (%d rows, %d columns).", rowcount, mysql_num_fields(result)));
    return FALSE;
  }
  if (rowcount > IPC_MAX_NUM_FILTAPERS)
  {
    act_log_error(act_log_msg("Configuration database reports %d installed CCD filters, but only the first %d can be handled. Ignoring the last %d filters.", rowcount, IPC_MAX_NUM_FILTAPERS, IPC_MAX_NUM_FILTAPERS - rowcount));
    rowcount = IPC_MAX_NUM_FILTAPERS;
  }
  
  int i;
  int tmp_slot, tmp_id, num_filts = 0;
  for (i=0; i<rowcount; i++)
  {
    row = mysql_fetch_row(result);
    if (sscanf(row[0], "%d", &tmp_id) != 1)
    {
      act_log_error(act_log_msg("Error parsing CCD filter database id number (%s).", row[0]));
      continue;
    }
    if (sscanf(row[1], "%d", &tmp_slot) != 1)
    {
      act_log_error(act_log_msg("Error parsing CCD filter slot number (%s).", row[1]));
      continue;
    }
    ccdcap_msg->filters[num_filts].db_id = tmp_id;
    ccdcap_msg->filters[num_filts].slot = tmp_slot;
    if (strlen(row[2]) >= IPC_MAX_FILTAPER_NAME_LEN-1)
      act_log_error(act_log_msg("CCD filter %d's identifier string (%s) is too long (%d characters). Trimming to %d characters.", tmp_slot, row[1], strlen(row[1]), IPC_MAX_FILTAPER_NAME_LEN));
    snprintf(ccdcap_msg->filters[num_filts].name, IPC_MAX_FILTAPER_NAME_LEN, "%s", row[2]);
    num_filts++;
  }
  for (i=num_filts; i<IPC_MAX_NUM_FILTAPERS; i++)
    ccdcap_msg->filters[i].db_id = -1;
  mysql_free_result(result);
  return TRUE;
}
