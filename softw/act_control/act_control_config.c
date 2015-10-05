#include <mysql/mysql.h>
#include <string.h>
#include <stdlib.h>
#include <act_ipc.h>
#include <act_log.h>
#include "act_control_config.h"

int control_config_programmes(const char *sqldb_host, struct act_prog **prog_array)
{
  if ((sqldb_host == NULL) || (prog_array == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return -1;
  }
  
  MYSQL *conn;
  conn = mysql_init(NULL);
  if (conn == NULL)
    act_log_error(act_log_msg("Error initialising MySQL connection handler - %d (%s).", mysql_errno(conn), mysql_error(conn)));
  else if (mysql_real_connect(conn, sqldb_host, "act_control", NULL, "act", 0, NULL, 0) == NULL)
  {
    act_log_error(act_log_msg("Error connecting to MySQL database - %d (%s).", mysql_errno(conn), mysql_error(conn)));
    conn = NULL;
  }
  
  if (conn == NULL)
    return -1;
  MYSQL_RES *result;
  MYSQL_ROW row;
  mysql_query(conn,"SELECT name,executable,host,active_time,guicoord_left,guicoord_right,guicoord_top,guicoord_bottom FROM softw_config;");
  result = mysql_store_result(conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Error: No configuration results retrieved from database - %d (%s).", mysql_errno(conn), mysql_error(conn)));
    return -1;
  }
  int rowcount = mysql_num_rows(result);
  if ((rowcount <= 0) || (mysql_num_fields(result) != 8))
  {
    act_log_error(act_log_msg("Error: Invalid configuration results retrieved from database - %d (%s).", mysql_errno(conn), mysql_error(conn)));
    return -1;
  }
  
  *prog_array = malloc(rowcount*sizeof(struct act_prog));
  if (*prog_array == NULL)
  {
    act_log_error(act_log_msg("Error: Could not allocate memory for programme configuration information."));
    return -1;
  }
  memset(*prog_array, 0, rowcount*sizeof(struct act_prog));
  
  int i;
  for (i=0; i<rowcount; i++)
  {
    row = mysql_fetch_row(result);
    strncpy((*prog_array)[i].name, row[0], sizeof((*prog_array)[i].name));
    strncpy((*prog_array)[i].executable, row[1], sizeof((*prog_array)[i].executable));
    strncpy((*prog_array)[i].host, row[2], sizeof((*prog_array)[i].host));
    if (sscanf(row[3], "%hhu", &(*prog_array)[i].active_time) != 1)
      fprintf(stderr, "Error parsing active time for programme %s (%s).", row[0], row[3]);
    if (sscanf(row[4], "%hhu", &(*prog_array)[i].guicoords[0]) != 1)
      fprintf(stderr, "Error parsing guicoord_left for programme %s (%s).", row[0], row[4]);
    if (sscanf(row[5], "%hhu", &(*prog_array)[i].guicoords[1]) != 1)
      fprintf(stderr, "Error parsing guicoord_right for programme %s (%s).", row[0], row[5]);
    if (sscanf(row[6], "%hhu", &(*prog_array)[i].guicoords[2]) != 1)
      fprintf(stderr, "Error parsing guicoord_top for programme %s (%s).", row[0], row[6]);
    if (sscanf(row[7], "%hhu", &(*prog_array)[i].guicoords[3]) != 1)
      fprintf(stderr, "Error parsing guicoord_bottom for programme %s (%s).", row[0], row[7]);
  }
  mysql_free_result(result);
  return rowcount;
}
