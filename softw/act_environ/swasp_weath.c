#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <act_log.h>
#include <act_ipc.h>
#include <act_positastro.h>
#include "swasp_weath.h"

#define FETCH_TIMEOUT_S     60
#define INVAL_TIMEOUT_D     0.013888888888888888
#define TIME_TIMEOUT_S      60
#define SWASP_WEATH_URL "http://swaspgateway.suth/index.php"
/// Used to indicate invalid/unreadable temperature (lower than 0 K).
#define ABS_ZERO_TEMP_C  -275


static void class_init(SwaspWeathClass *klass);
static void instance_init(GObject *swasp_weath);
static void instance_dispose(GObject *swasp_weath);
static gboolean time_timeout(gpointer swasp_weath);
static gboolean fetch_swasp(gpointer swasp_weath);
static void process_resp(SoupSession *soup_session, SoupMessage *msg, gpointer swasp_weath);
static void string_to_upper(char *str);
static char find_rowcol(const char *swasp_dat, unsigned int len, const char *valname, unsigned int *rownum, unsigned int *colnum);
static char *find_rowcolval(const char *swasp_dat, int len, unsigned int rownum, unsigned int colnum);
static double cardinal_to_deg(char *dir);
static char scrape_unidt(const char *swasp_dat, int len, struct datestruct *unid, struct timestruct *unit);
static char scrape_rel_hum(const char *swasp_dat, int len);
static char scrape_is_dry(const char *swasp_dat, int len);
static short scrape_wind_speed(const char *swasp_dat, int len);
static float scrape_wind_dir(const char *swasp_dat, int len);
static float scrape_ext_temp(const char *swasp_dat, int len);
static float scrape_ext_dew_temp(const char *swasp_dat, int len);
static float scrape_cloud(const char *swasp_dat, int len);

enum
{
  SWASP_WEATH_UPDATE,
  LAST_SIGNAL
};

static guint swasp_weath_signals[LAST_SIGNAL] = { 0 };

GType swasp_weath_get_type (void)
{
  static GType swasp_wath_type = 0;
  
  if (!swasp_wath_type)
  {
    const GTypeInfo swasp_wath_info =
    {
      sizeof (SwaspWeathClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) class_init, /* class init */
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (SwaspWeath),
      0,
      (GInstanceInitFunc) instance_init,
      NULL
    };
    
    swasp_wath_type = g_type_register_static (G_TYPE_OBJECT, "SwaspWeath", &swasp_wath_info, 0);
  }
  
  return swasp_wath_type;
}

SwaspWeath *swasp_weath_new (void)
{
  SwaspWeath *objs = SWASP_WEATH(g_object_new (swasp_weath_get_type(), NULL));
  objs->fetch_session = soup_session_async_new();
  fetch_swasp((void *)objs);
  objs->fetch_to_id = g_timeout_add_seconds(FETCH_TIMEOUT_S, fetch_swasp, objs);
  objs->time_to_id = g_timeout_add_seconds(TIME_TIMEOUT_S, time_timeout, objs);
  return objs;
}

void swasp_weath_set_time(SwaspWeath * objs, double jd)
{
  objs->cur_jd = jd;
  if (objs->time_to_id != 0)
  {
    g_source_remove(objs->time_to_id);
    objs->time_to_id = 0;
  }
  objs->time_to_id = g_timeout_add_seconds(TIME_TIMEOUT_S, time_timeout, objs);
}

gboolean swasp_weath_get_env_data (SwaspWeath * objs, struct act_msg_environ *env_data)
{
  if (fabs(objs->weath_jd - objs->cur_jd) > INVAL_TIMEOUT_D)
  {
    act_log_normal(act_log_msg("No recent weather data available."));
    return FALSE;
  }
  
  memset(env_data, 0, sizeof(struct act_msg_environ));
  env_data->humidity = objs->rel_hum;
  env_data->clouds = objs->cloud;
  env_data->wind_vel = objs->wind_speed;
  convert_D_DMS_azm(objs->wind_dir, &env_data->wind_azm);
  env_data->rain = objs->rain;
  return TRUE;
}

static void class_init(SwaspWeathClass *klass)
{
  swasp_weath_signals[SWASP_WEATH_UPDATE] = g_signal_new("swasp-weath-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  G_OBJECT_CLASS(klass)->dispose = instance_dispose;
}

static void instance_init(GObject *swasp_weath)
{
  SwaspWeath *objs = SWASP_WEATH(swasp_weath);
  
  objs->cur_jd = 1.0;
  objs->fetch_to_id = 0;
  objs->fetch_session = NULL;
  objs->fetch_msg = NULL;
  
  objs->weath_jd = 0.0;
  objs->rel_hum = 0.0;
  objs->rain = FALSE;
  objs->wind_speed = 0.0;
  objs->wind_dir = 0.0;
  objs->ext_temp = 0.0;
  objs->ext_dew_temp = 0.0;
  objs->cloud = 0;
}

static void instance_dispose(GObject *swasp_weath)
{
  SwaspWeath *objs = SWASP_WEATH(swasp_weath);
  if (objs->fetch_to_id > 0)
  {
    g_source_remove(objs->fetch_to_id);
    objs->fetch_to_id = 0;
  }
  if (objs->time_to_id != 0)
  {
    g_source_remove(objs->time_to_id);
    objs->time_to_id = 0;
  }
  if ((objs->fetch_session != NULL) && (objs->fetch_msg != NULL))
  {
    soup_session_cancel_message (objs->fetch_session, objs->fetch_msg, SOUP_STATUS_CANCELLED);
    objs->fetch_msg = NULL;
  }
  G_OBJECT_CLASS(swasp_weath)->dispose(swasp_weath);
}

static gboolean time_timeout(gpointer swasp_weath)
{
  SwaspWeath *objs = SWASP_WEATH(swasp_weath);
  objs->cur_jd = 1.0;
  objs->time_to_id = 0;
  return FALSE;
}

static gboolean fetch_swasp(gpointer swasp_weath)
{
  SwaspWeath *objs = SWASP_WEATH(swasp_weath);
  objs->fetch_msg = soup_message_new ("GET", SWASP_WEATH_URL);
  soup_session_queue_message (objs->fetch_session, objs->fetch_msg, process_resp, swasp_weath);
  return TRUE;
}

static void process_resp(SoupSession *soup_session, SoupMessage *msg, gpointer swasp_weath)
{
  (void)soup_session;
  SwaspWeath *objs = SWASP_WEATH(swasp_weath);
  objs->fetch_msg = NULL;
  if (msg->status_code != SOUP_STATUS_OK)
  {
    act_log_error(act_log_msg("An error occurred while retrieving SuperWASP weather data. Reason: %s", msg->reason_phrase));
    g_signal_emit(G_OBJECT(swasp_weath), swasp_weath_signals[SWASP_WEATH_UPDATE], 0, FALSE);
    return;
  }
  
  int len = msg->response_body->length;
  struct datestruct unid;
  struct timestruct unit;
  char msg_copy[len+1];
  snprintf(msg_copy, len, "%s", msg->response_body->data);
  string_to_upper(msg_copy);
  gboolean valid = TRUE;

  if (!scrape_unidt(msg_copy, len, &unid, &unit))
  {
    act_log_debug(act_log_msg("UNIDT"));
    valid = FALSE;
  }
  double tmp_jd = calc_GJD(&unid, &unit);
  
  char tmp_rel_hum = scrape_rel_hum(msg_copy, len);
  if (tmp_rel_hum < 0)
  {
    act_log_debug(act_log_msg("REL_HUM"));
    valid = FALSE;
  }
  
  char tmp_is_dry = scrape_is_dry(msg_copy, len);
  if (tmp_is_dry < 0)
  {
    act_log_debug(act_log_msg("RAIN"));
    valid = FALSE;
  }
  
  short tmp_wind_speed = scrape_wind_speed(msg_copy, len); 
  if (tmp_wind_speed < 0)
  {
    act_log_debug(act_log_msg("WIND SPEED"));
    valid = FALSE;
  }
  
  float tmp_wind_dir = scrape_wind_dir(msg_copy, len);
  if (tmp_wind_dir < 0.0)
  {
    act_log_debug(act_log_msg("WIND DIR"));
    valid = FALSE;
  }
  
  float tmp_ext_temp = scrape_ext_temp(msg_copy, len);
  if (tmp_ext_temp < ABS_ZERO_TEMP_C + 1.0)
  {
    act_log_debug(act_log_msg("EXTT"));
    valid = FALSE;
  }
  
  float tmp_ext_dew_temp = scrape_ext_dew_temp(msg_copy, len);
  if (tmp_ext_dew_temp < ABS_ZERO_TEMP_C + 1.0)
  {
    act_log_debug(act_log_msg("DEWT"));
    valid = FALSE;
  }
  
  float tmp_cloud = scrape_cloud(msg_copy, len);
  if (tmp_cloud < 0)
  {
    act_log_debug(act_log_msg("CLOUD"));
    valid = FALSE;
  }
  
  if (!valid)
  {
    act_log_normal(act_log_msg("Failed to extract all SuperWASP weather data."));
    g_signal_emit(G_OBJECT(swasp_weath), swasp_weath_signals[SWASP_WEATH_UPDATE], 0, FALSE);
    return;
  }
  
  objs->weath_jd = tmp_jd;
  objs->rel_hum = tmp_rel_hum;
  objs->rain = (tmp_is_dry == 0);
  objs->wind_speed = tmp_wind_speed;
  objs->wind_dir = tmp_wind_dir;
  objs->ext_temp = tmp_ext_temp;
  objs->ext_dew_temp = tmp_ext_dew_temp;
  objs->cloud = 100.0 - 2*tmp_cloud;
  
  g_signal_emit(G_OBJECT(swasp_weath), swasp_weath_signals[SWASP_WEATH_UPDATE], 0, TRUE);
}

/** \brief Convert string to all UPPERCASE characters.
 * \param str String to convert. Conversion is done in-place.
 * \return (void)
 */
static void string_to_upper(char *str)
{
  int i;
  for (i=0; str[i] != '\0'; i++)
    str[i] = toupper(str[i]);
}

/** \brief Find the column number in the weather data table containing the given datum.
 * \param fp File descriptor for text file containing SuperWASP website HTML source code.
 * \param valname Name of datum to be extracted.
 * \return Column number containing requested datum or <0 upon error.
 * \todo This function can maybe be rewritten to increase robustness.
 *
 * In essence, the algorithm counts \<TH\> tags until the tag encapsulating the given valname is found.
 * Deals with COLSPANs in HTML pages correctly.
 *  - The highest column number among a number of spanned columns is returned. It is left to the calling function to
 *    determine by how much the returned column number should be reduced to refer to the correct column value.
 * Will fail if an HTML tag is split across lines (which is very bad practise).
 * Will fail if requested datum is not present in file.
 * Will fail if more than one table cell per line.
 *
 * Algorithm:
 *  -# Step line-by-line through given file.
 *    -# Convert line to UPPERCASE.
 *    -# Step through line char-by-char
 *      -# Search for table header tag \<TH... in line from current char.
 *        - If not found, go to next line.
 *      -# Search for COLSPAN in line from current char.
 *        - If found, read number of columns spanned.
 *      -# Check for split tags.
 *      -# Check if line (from current char) contains requested valname.
 *        - If yes, return column
 *        - If no, go to next line.
 */
static char find_rowcol(const char *swasp_dat, unsigned int len, const char *valname, unsigned int *rownum, unsigned int *colnum)
{
  unsigned int tmp_row = 0, tmp_col = 0;
  char *valname_char = strstr(swasp_dat, valname);
  if (valname_char == NULL)
  {
    act_log_error(act_log_msg("Could not find string %s in retrieve SuperWASP weather data."));
    return 0;
  }
  const char *curchar = swasp_dat, *next_char;
  const char *end = &swasp_dat[len-1];
  const char *tagend;
  char substring[256];
  while (curchar < end)
  {
    next_char = strstr(curchar, "<TR");
    if (next_char == NULL)
    {
      act_log_error(act_log_msg("Could not find any HTML table row tags."));
      return 0;
    }
    next_char += 3;
    if (next_char > valname_char)
      break;
    curchar = next_char;
    tagend = strchr(next_char, '>');
    if (tagend == NULL)
    {
      act_log_error(act_log_msg("Open HTML row tag."));
      return 0;
    }
    if (tagend - curchar > (int)sizeof(substring))
    {
      act_log_error(act_log_msg("VERY long HTML column tag."));
      return 0;
    }
    next_char = strstr(curchar, "ROWSPAN");
    if ((next_char == NULL) || (next_char > tagend))
    {
      tmp_row++;
      continue;
    }
    next_char += 7;
    int row_add;
    while ((*next_char < '0') || (*next_char > '9'))
      next_char++;
    if (sscanf(next_char, "%d", &row_add) != 1)
    {
      act_log_error(act_log_msg("Error while processing spanned row."));
      return 0;
    }
    tmp_row += row_add;
  }
  const char *colchar = NULL, *headchar = NULL;
  while (curchar < end)
  {
    colchar = strstr(curchar, "<TD");
    headchar = strstr(curchar, "<TH");
    if ((colchar == NULL) && (headchar == NULL))
    {
      act_log_error(act_log_msg("Could not find any HTML table column tags."));
      return 0;
    }
    if (headchar == NULL)
      next_char = colchar;
    else if (colchar == NULL)
      next_char = headchar;
    else if (colchar < headchar)
      next_char = colchar;
    else
      next_char = headchar;
    next_char += 3;
    if (next_char > valname_char)
      break;
    curchar = next_char;
    tagend = strchr(next_char, '>');
    if (tagend == NULL)
    {
      act_log_error(act_log_msg("Open HTML column tag."));
      return 0;
    }
    if (tagend - curchar > (int)sizeof(substring))
    {
      act_log_error(act_log_msg("VERY long HTML column tag."));
      return 0;
    }
    next_char = strstr(curchar, "COLSPAN");
    if ((next_char == NULL) || (next_char > tagend))
    {
      tmp_col++;
      continue;
    }
    next_char += 7;
    int col_add;
    while ((*next_char < '0') || (*next_char > '9'))
      next_char++;
    if (sscanf(next_char, "%d", &col_add) != 1)
    {
      act_log_error(act_log_msg("Error while processing spanned column."));
      return 0;
    }
    tmp_col += col_add;
  }
  *rownum = tmp_row;
  *colnum = tmp_col;
  return 1;
}

/** \brief Find the value in the specified column number.
 * \param fp File descriptor for text file containing SuperWASP website HTML source code.
 * \param colnum Number of column for which datum must be extracted.
 * \return C string of column value of requested column number or NULL upon error.
 * \todo This function can maybe be rewritten.
 *
 * In essence, the algorithm seeks up to the colnum-th \<TD\> tag and then extracts the value encapsulated by the tags.
 * Deals with COLSPANs in HTML pages correctly.
 * Will fail if an HTML tag is split across lines (which is very bad practise).
 * Will fail if requested datum is not present in file.
 * Will fail if more than one table cell per line.
 *
 * Algorithm:
 *  -# Step line-by-line through given file.
 *    -# Convert line to UPPERCASE.
 *    -# Step through line char-by-char
 *      -# Search for table header tag \<TD... in line from current char.
 *        - If not found, go to next line.
 *      -# Search for COLSPAN in line from current char.
 *        - If found, read number of columns spanned.
 *      -# Check for split tags.
 *      -# Check if we've passed the specified colnum (this should never happen)
 *      -# Check if we're at the specified colnum
 *        - If no, go to next line.
 *        - If yes, extract value encapsulated within \<TD\> tags.
 */
static char *find_rowcolval(const char *swasp_dat, int len, unsigned int rownum, unsigned int colnum)
{
  unsigned int cur_row = 0, cur_col = 0;
  const char *curchar = swasp_dat, *next_char = NULL, *end_char = swasp_dat+len-1, *tagend = NULL;
  char substring[256];
  while (curchar < end_char)
  {
    if (cur_row >= rownum)
      break;
    next_char = strstr(curchar, "<TR");
    if (next_char == NULL)
    {
      act_log_error(act_log_msg("Could not find any HTML table row tags."));
      return NULL;
    }
    curchar = next_char + 3;
    tagend = strchr(next_char, '>');
    if (tagend == NULL)
    {
      act_log_error(act_log_msg("Open HTML row tag."));
      return NULL;
    }
    if (tagend - curchar > (int)sizeof(substring))
    {
      act_log_error(act_log_msg("VERY long HTML row tag."));
      return NULL;
    }
    next_char = strstr(curchar, "ROWSPAN");
    if ((next_char == NULL) || (next_char > tagend))
    {
      cur_row++;
      continue;
    }
    next_char += 7;
    int row_add;
    while ((*next_char < '0') || (*next_char > '9'))
      next_char++;
    if (sscanf(next_char, "%d", &row_add) != 1)
    {
      act_log_error(act_log_msg("Error while processing spanned row."));
      return 0;
    }
    cur_row += row_add;
  }
  if (cur_row != rownum)
  {
    act_log_error(act_log_msg("Could not find row %d.", rownum));
    return NULL;
  }
  
  char *colchar = NULL, *headchar = NULL;
  while (curchar < end_char)
  {
    if (cur_col >= colnum)
      break;
    colchar = strstr(curchar, "<TD");
    headchar = strstr(curchar, "<TH");
    if ((colchar == NULL) && (headchar == NULL))
    {
      act_log_error(act_log_msg("Could not find any HTML table column tags."));
      return 0;
    }
    if (headchar == NULL)
      next_char = colchar;
    else if (colchar == NULL)
      next_char = headchar;
    else if (colchar < headchar)
      next_char = colchar;
    else
      next_char = headchar;
    curchar = next_char + 3;
    tagend = strchr(next_char, '>');
    if (tagend == NULL)
    {
      act_log_error(act_log_msg("Open HTML column tag."));
      return NULL;
    }
    if (tagend - curchar > (int)sizeof(substring))
    {
      act_log_error(act_log_msg("VERY long HTML column tag."));
      return NULL;
    }
    next_char = strstr(curchar, "COLSPAN");
    if ((next_char == NULL) || (next_char > tagend))
    {
      cur_col++;
      continue;
    }
    next_char += 7;
    int col_add;
    while ((*next_char < '0') || (*next_char > '9'))
      next_char++;
    if (sscanf(next_char, "%d", &col_add) != 1)
    {
      act_log_error(act_log_msg("Error while processing spanned row."));
      return 0;
    }
    cur_col += col_add;
  }
  if (cur_col != colnum)
  {
    act_log_error(act_log_msg("Could not find column %d.", colnum));
    return NULL;
  }
  curchar = strchr(curchar, '>');
  if (curchar == NULL)
  {
    act_log_error(act_log_msg("Found start tag for requested row/column, but can't find tag's end."));
    return NULL;
  }
  curchar++;
  next_char = strchr(curchar, '<');
  if (next_char == NULL)
  {
    act_log_error(act_log_msg("Found start tag for requested row/column, but can't find next tag's start."));
    return NULL;
  }
  if (next_char <= curchar)
  {
    act_log_error(act_log_msg("This row/column does not contain anything."));
    return NULL;
  }
  
  char *ret = malloc(next_char - curchar + 2);
  if (ret == NULL)
  {
    act_log_error(act_log_msg("Could not allocate memory for string to return."));
    return NULL;
  }
  snprintf(ret, next_char-curchar+1, "%s", curchar);
  return ret;
}

/** \brief Convert cardinal direction to azimuthal direction.
 * \param dir Cardinal direction (wind direction) as a C string.
 * N is 0 deg, E is 90 deg etc.
 * Only processes the first 3 characters in the cardinal direction string, error is 22.5 degrees, which is large
 * but accurate enough for wind direction, which is what this function was designed to interpret.
 */
static double cardinal_to_deg(char *dir)
{
  char tmp_dir[10];
  sscanf(dir,"%s",tmp_dir);
  string_to_upper(tmp_dir);
  if (strncmp(tmp_dir,"NNE",3) == 0)
    return 22.5;
  if (strncmp(tmp_dir,"ENE",3) == 0)
    return 67.5;
  if (strncmp(tmp_dir,"ESE",3) == 0)
    return 112.5;
  if (strncmp(tmp_dir,"SSE",3) == 0)
    return 157.5;
  if (strncmp(tmp_dir,"SSW",3) == 0)
    return 202.5;
  if (strncmp(tmp_dir,"WSW",3) == 0)
    return 247.5;
  if (strncmp(tmp_dir,"WNW",3) == 0)
    return 292.5;
  if (strncmp(tmp_dir,"NNW",3) == 0)
    return 337.5;
  if (strncmp(tmp_dir,"NE",2) == 0)
    return 45.0;
  if (strncmp(tmp_dir,"SE",2) == 0)
    return 135.0;
  if (strncmp(tmp_dir,"SW",2) == 0)
    return 225.0;
  if (strncmp(tmp_dir,"NW",2) == 0)
    return 315.0;
  if (strncmp(tmp_dir,"N",1) == 0)
    return 0.0;
  if (strncmp(tmp_dir,"E",1) == 0)
    return 90.0;
  if (strncmp(tmp_dir,"S",1) == 0)
    return 180.0;
  if (strncmp(tmp_dir,"W",1) == 0)
    return 270.0;
  act_log_error(act_log_msg("Failed to extract wind direction."));
  return -1.0;
}

/** \brief Extract universal date and time.
 * \param filename Name of file (preferably incl. global filesystem path) to file containing SuperWASP website code.
 * \param min Pointer to where UT minutes will be stored.
 * \param hrs Pointer to where UT hours will be stored.
 * \param day Pointer to where UT day of the month will be stored.
 * \param month Pointer to where UT month of the year will be stored.
 * \param year Pointer to where UT year will be stored.
 * \return 1 if successful, 0 otherwise.
 *
 * This function doesn't use find_colnum or find_colval as the UT date and time are within the TCS STATUS section 
 * (and not within the tabular weather data section) of the SuperWASP webpage.
 * The first \<TD\> cell within the TCS STATUS section of the page contains the UT date and time.
 * Algorithm:
 *  -# Open SuperWASP website source file.
 *  -# Step through file line by line.
 *    -# Convert line to upper case.
 *    -# If line contains the "TCS STATUS" keyword:
 *      -# Set internal flag to indicate that we're in the TCS status section of the page.
 *      -# If line does not contain \<TD\> tag as well, go to next line.
 *      -# Otherwise set pointer to the start of the universal date and time string and break out of this loop.
 *    -# If we're in the TCS STATUS section of the page and line contains the \<TD\> tag, set pointer to the
 *       start of the universal date and time string and break out of this loop.
 *  -# If TCS STATUS string was not found (i.e. page does not contain "TCS STATUS"), return error.
 *  -# Try to extract the universal date and time from the string.
 *    - Return 0 if unsuccessful.
 *  -# Interpret the values in the string.
 *  -# Return success.
 */
static char scrape_unidt(const char *swasp_dat, int len, struct datestruct *unid, struct timestruct *unit)
{
  unsigned int row, column;
  if (!find_rowcol(swasp_dat, len, "TCS STATUS", &row, &column))
  {
    act_log_error(act_log_msg("Failed to find row/column with TCS time."));
    return 0;
  }
  column++;
  char *str = find_rowcolval(swasp_dat, len, row, column);
  if (str == NULL)
  {
    act_log_error(act_log_msg("Failed to retrieve TCS time."));
    return 0;
  }
  unsigned char swasp_min, swasp_hrs, swasp_day, swasp_month;
  short swasp_year;
  if (sscanf(str, "%02hhu%02hhuUT %02hhu/%02hhu/%02hd", &swasp_hrs, &swasp_min, &swasp_month, &swasp_day, &swasp_year) != 5)
  {
    act_log_error(act_log_msg("Could not extract time and date information from ""%s""", str));
    free(str);
    return 0;
  }
  free(str);
  unid->year = swasp_year + 2000;
  unid->month = swasp_month - 1;
  unid->day = swasp_day - 1;
  unit->hours = swasp_hrs;
  unit->minutes = swasp_min;
  unit->seconds = 0;
  unit->milliseconds = 0;
  return 1;
}

/** \brief Extract relative humidity.
 * \param filename Name of file (preferably incl. global filesystem path) to file containing SuperWASP website code.
 * \return Relative humidity as integer percentage or \<0 upon error.
 *
 * This function uses find_colnum and find_colval to extract the relative humidity.
 * Algorithm:
 *  -# Open SuperWASP website source file.
 *  -# Find column number containing the "RH%" keyword (find_colnum).
 *  -# Extract value in corresponding row of next column as a string (find_colval).
 *  -# Extract relative humidity.
 */
static char scrape_rel_hum(const char *swasp_dat, int len)
{
  unsigned int row, column;
  if (!find_rowcol(swasp_dat, len, "RH%", &row, &column))
  {
    act_log_error(act_log_msg("Failed to find row/column with relative humidity."));
    return -1;
  }
  row++;
  char *str = find_rowcolval(swasp_dat, len, row, column);
  if (str == NULL)
  {
    act_log_error(act_log_msg("Failed to retrieve relative humidity."));
    return -1;
  }
  char rel_hum;
  if (sscanf(str, "%hhd", &rel_hum) != 1)
  {
    act_log_error(act_log_msg("Invalid line: %s", str));
    rel_hum = -1;
  }
  free(str);
  return rel_hum;
}


/** \brief Extract rain indication.
 * \param filename Name of file (preferably incl. global filesystem path) to file containing SuperWASP website code.
 * \return 1 if conditions are "DRY", 0 otherwise, \<0 upon error.
 *
 * This function uses find_colnum and find_colval to extract the RAIN indicator, which should indicate "DRY"
 * when it is safe to observe.
 * Algorithm:
 *  -# Open SuperWASP website source file.
 *  -# Find column number containing the "RAIN" keyword (find_colnum).
 *  -# Extract value in corresponding row of next column as a string (find_colval).
 *  -# Extract RAIN indicator and return 1 if indicator is "DRY"
 */
static char scrape_is_dry(const char *swasp_dat, int len)
{
  unsigned int row, column;
  if (!find_rowcol(swasp_dat, len, "RAIN", &row, &column))
  {
    act_log_error(act_log_msg("Failed to find row/column with rain."));
    return 0;
  }
  row++;
  char *str = find_rowcolval(swasp_dat, len, row, column);
  if (str == NULL)
  {
    act_log_error(act_log_msg("Failed to retrieve rain."));
    return 0;
  }
  if (strncmp(str, "DRY", 3) == 0)
  {
    free(str);
    return 1;
  }
  free(str);
  return 0;
}


/** \brief Extract wind speed.
 * \param filename Name of file (preferably incl. global filesystem path) to file containing SuperWASP website code.
 * \return Wind speed as integer km/h or \<0 upon error.
 *
 * This function uses find_colnum and find_colval to extract the wind speed.
 * Algorithm:
 *  -# Open SuperWASP website source file.
 *  -# Find column number containing the "WIND" keyword (find_colnum).
 *  -# Reduce column number by 1 to account for the fact that the "WIND" header tag is used for both the wind speed 
 *     and wind direction values (as a COLSPAN in the HTML code).
 *  -# Extract value in corresponding row of next column as a string (find_colval).
 *  -# Extract wind speed.
 */
static short scrape_wind_speed(const char *swasp_dat, int len)
{
  unsigned int row, column;
  if (!find_rowcol(swasp_dat, len, "WIND", &row, &column))
  {
    act_log_error(act_log_msg("Failed to find row/column with wind speed."));
    return 0;
  }
  row++;
  column++;
  char *str = find_rowcolval(swasp_dat, len, row, column);
  if (str == NULL)
  {
    act_log_error(act_log_msg("Failed to retrieve wind velocity."));
    return 0;
  }
  short wind_speed;
  if (sscanf(str, "%hd", &wind_speed) != 1)
  {
    free(str);
    return -1;
  }
  free(str);
  return wind_speed;
}

/** \brief Extract wind direction.
 * \param filename Name of file (preferably incl. global filesystem path) to file containing SuperWASP website code.
 * \return Wind direction as fractional degrees (0=North, 90=East, etc.) or \<0 upon error.
 *
 * This function uses find_colnum and find_colval to extract the wind direction.
 * Algorithm:
 *  -# Open SuperWASP website source file.
 *  -# Find column number containing the "WIND" keyword (find_colnum).
 *    - In this case, the column number isn't reduced by 1 as it is with the wind speed, because the wind direction
 *      is in the second column spanned under "WIND".
 *  -# Extract value in corresponding row of next column as a string (find_colval).
 *  -# Extract wind direction.
 */
static float scrape_wind_dir(const char *swasp_dat, int len)
{
  unsigned int row, column;
  if (!find_rowcol(swasp_dat, len, "WIND", &row, &column))
  {
    act_log_error(act_log_msg("Failed to find row/column with wind speed."));
    return 0;
  }
  row++;
  char *str = find_rowcolval(swasp_dat, len, row, column);
  if (str == NULL)
  {
    act_log_error(act_log_msg("Failed to retrieve wind velocity."));
    return 0;
  }
  float wind_dir = cardinal_to_deg(str);
  free(str);
  return wind_dir;
}

/** \brief Extract external temperature.
 * \param filename Name of file (preferably incl. global filesystem path) to file containing SuperWASP website code.
 * \return External temperature as fractional degrees Celcius.
 *
 * This function uses find_colnum and find_colval to extract the external temperature.
 * Algorithm:
 *  -# Open SuperWASP website source file.
 *  -# Find column number containing the "T\<SUB\>EXT\</SUB\>" keyword (find_colnum).
 *  -# Extract value in corresponding row of next column as a string (find_colval).
 *  -# Extract external temperature.
 */
static float scrape_ext_temp(const char *swasp_dat, int len)
{
  unsigned int row, column;
  if (!find_rowcol(swasp_dat, len, "T<SUB>EXT</SUB>", &row, &column))
  {
    act_log_error(act_log_msg("Failed to find row/column with external temperature."));
    return ABS_ZERO_TEMP_C;
  }
  row++;
  char *str = find_rowcolval(swasp_dat, len, row, column);
  if (str == NULL)
  {
    act_log_error(act_log_msg("Failed to retrieve external temperature."));
    return ABS_ZERO_TEMP_C;
  }
  float ext_temp;
  if (sscanf(str, "%f", &ext_temp) != 1)
  {
    act_log_error(act_log_msg("Failed to extract exterior temperature."));
    free(str);
    return ABS_ZERO_TEMP_C;
  }
  free(str);
  return ext_temp;
}

/** \brief Extract external temperature - dew point temperature.
 * \param filename Name of file (preferably incl. global filesystem path) to file containing SuperWASP website code.
 * \return External temperature - dew point temperature as fractional degrees Celcius.
 *
 * This function uses find_colnum and find_colval to extract (T_ext - T_dew), which is a measure of the relative
 * humidity.
 * Algorithm:
 *  -# Open SuperWASP website source file.
 *  -# Find column number containing the "T\<SUB\>EXT\</SUB\> - T\<SUB\>DEW\</SUB\>" keyword (find_colnum).
 *  -# Extract value in corresponding row of next column as a string (find_colval).
 *  -# Extract (T_ext - T_dew).
 */
static float scrape_ext_dew_temp(const char *swasp_dat, int len)
{
  unsigned int row, column;
  if (!find_rowcol(swasp_dat, len, "T<SUB>EXT</SUB> - T<SUB>DEW</SUB>", &row, &column))
  {
    act_log_error(act_log_msg("Failed to find row/column with dew point temperature."));
    return ABS_ZERO_TEMP_C;
  }
  row++;
  char *str = find_rowcolval(swasp_dat, len, row, column);
  if (str == NULL)
  {
    act_log_error(act_log_msg("Failed to retrieve dew point temperature."));
    return ABS_ZERO_TEMP_C;
  }
  float ext_dew_temp;
  if (sscanf(str, "%f", &ext_dew_temp) != 1)
  {
    act_log_error(act_log_msg("Failed to extract dew point temperature."));
    free(str);
    return ABS_ZERO_TEMP_C;
  }
  free(str);
  return ext_dew_temp;
}

/** \brief Extract cloud coverage
 * \param filename Name of file (preferably incl. global filesystem path) to file containing SuperWASP website code.
 * \return Cloud measure
 *
 * This function uses find_colnum and find_colval to extract the cloud coverage.
 * Algorithm:
 *  -# Open SuperWASP website source file.
 *  -# Find column number containing the "CLOUD" keyword (find_colnum).
 *  -# Extract value in corresponding row of next column as a string (find_colval).
 *  -# Extract cload coverage.
 */
static float scrape_cloud(const char *swasp_dat, int len)
{
  unsigned int row, column;
  if (!find_rowcol(swasp_dat, len, "CLOUD", &row, &column))
  {
    act_log_error(act_log_msg("Failed to find row/column with cloud."));
    return -1.0;
  }
  row++;
  char *str = find_rowcolval(swasp_dat, len, row, column);
  if (str == NULL)
  {
    act_log_error(act_log_msg("Failed to retrieve cloud."));
    return -1.0;
  }
  char *tmpchar = strchr(str, '(');
  float cloud;
  if (sscanf(tmpchar, "(%f)", &cloud) != 1)
  {
    act_log_error(act_log_msg("Failed to extract cloud level."));
    free(str);
    return -1.0;
  }
  free(str);
  return cloud;
}
