/*!
 * \file swasp_scrape.c
 * \brief Implementations for functions used to scrape weather/environment data from SuperWASP website.
 * \author Pierre van Heerden
 *
 * One of the main design goals for the functions contained within this file is that they should be relatively
 * impervious to changes made to the format of the HTML code of the SuperWASP website - the format of the SuperWASP
 * website is luckily not changed often. The data on the SuperWASP website we're interested in is displayed in a 
 * tabular form near the top of the page, where the name of the quantities are given in the first column and the
 * values are given in the second row and corresponding column. The success so far has been largely due to the
 * find_colnum and find_colval functions. find_colnum extracts the column number containing the datum requested
 * and find_colnum extracts the value in the same column, second row.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <act_ipc.h>
#include <act_positastro.h>
#include <act_log.h>
#include "swasp_scrape.h"

/// Used to indicate invalid/unreadable temperature (lower than 0 K).
#define ABS_ZERO_TEMP_C  -275

void string_to_upper(char *str);
char find_colnum(const char *swasp_dat, const char *valname);
char *find_colval(const char *swasp_dat, unsigned char colnum);
double cardinal_to_deg(char *dir);
char scrape_unidt(const char *swasp_dat, int len, struct datestruct *unid, struct timestruct *unit);
char scrape_rel_hum(const char *swasp_dat, int len);
char scrape_is_dry(const char *swasp_dat, int len);
short scrape_wind_speed(const char *swasp_dat, int len);
float scrape_wind_dir(const char *swasp_dat, int len);
float scrape_ext_temp(const char *swasp_dat, int len);
float scrape_ext_dew_temp(const char *swasp_dat, int len);
float scrape_cloud(const char *swasp_dat, int len);

/** \brief Convert string to all UPPERCASE characters.
 * \param str String to convert. Conversion is done in-place.
 * \return (void)
 */
void string_to_upper(char *str)
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
char find_rowcol(const char *swasp_dat, unsigned int len, const char *valname, unsigned int *rownum, unsigned int *colnum)
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
  
/*  char tmpline[256], *tmpchar;
  char colcount = 0;
  unsigned char coladd = 0;
  
  while (fgets(tmpline, sizeof(tmpline), fp) != NULL)
  {
    string_to_upper(tmpline);
    tmpchar = tmpline;
    while (tmpchar != '\0')
    {
      if ((tmpchar = strstr(tmpchar, "<TH")) == NULL)
        break;
      tmpchar += 3;
      if (strstr(tmpchar, "COLSPAN") != NULL)
      {
        tmpchar = strstr(tmpchar, "COLSPAN") + 7;
        while ((*tmpchar < '0') || (*tmpchar > '9'))
          tmpchar++;
        if (sscanf(tmpchar, "%hhu", &coladd) != 1)
          return -1;
      }
      else
        coladd = 1;
      if ((tmpchar = strchr(tmpchar, '>')) == NULL) // Split tag
        return -1;
      colcount += coladd;
      tmpchar++;
      if ((tmpchar = strstr(tmpchar, valname)) == NULL)
        continue;
      return colcount;
    }
  }
  return -1;*/
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
char *find_rowcolval(const char *swasp_dat, int len, unsigned int rownum, unsigned int colnum)
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
//   const char *tmp;
//   for (tmp = curchar; tmp<=next_char+1; tmp++)
//     act_log_debug(act_log_msg("%c", *tmp));
//   act_log_debug(act_log
  
  char *ret = malloc(next_char - curchar + 2);
  if (ret == NULL)
  {
    act_log_error(act_log_msg("Could not allocate memory for string to return."));
    return NULL;
  }
  snprintf(ret, next_char-curchar+1, "%s", curchar);
  return ret;
  
  
  
/*  char tmpline[256], *tmpchar, *tmp_val;
  char colcount = 0;
  unsigned char coladd, val_len = 0;
  
  unsigned long curchar = 0;
  while (fgets(tmpline, sizeof(tmpline), fp) != NULL)
  {
    tmpchar = &swasp_dat[curchar];
    while (*tmpchar != '\0')
    {
      if ((tmpchar = strstr(tmpchar, "<TD")) == NULL)
        break;
      tmpchar += 3;
      if (strstr(tmpchar, "COLSPAN") != NULL)
      {
        tmpchar = strstr(tmpchar, "COLSPAN") + 7;
        while ((*tmpchar < '0') || (*tmpchar > '9'))
          tmpchar++;
        if (sscanf(tmpchar, "%hhu", &coladd) != 1)
          return NULL;
      }
      else
        coladd = 1;
      if ((tmpchar = strchr(tmpchar, '>')) == NULL)  // Split tag
        return NULL;
      colcount += coladd;
      if (colcount > colnum) // Just in case, shouldn't be necessary
        return NULL;
      if (colcount < colnum)
        continue;
      tmpchar++;
      val_len = strcspn(tmpchar, "<");
      if (val_len == strlen(tmpchar))
        return NULL;
      tmp_val = malloc(val_len+1);
      strncpy(tmp_val, tmpchar, val_len);
      tmp_val[val_len] = '\0';
      return tmp_val;
    }
  }
  return NULL;*/
}

/** \brief Convert cardinal direction to azimuthal direction.
 * \param dir Cardinal direction (wind direction) as a C string.
 * N is 0 deg, E is 90 deg etc.
 * Only processes the first 3 characters in the cardinal direction string, error is 22.5 degrees, which is large
 * but accurate enough for wind direction, which is what this function was designed to interpret.
 */
double cardinal_to_deg(char *dir)
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
char scrape_unidt(const char *swasp_dat, int len, struct datestruct *unid, struct timestruct *unit)
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
/*  char tmpline[256], *tmpchar;
  unsigned char found_tcstime = 0;
  while (fgets(tmpline, sizeof(tmpline), swasp_html) != NULL)
  {
    string_to_upper(tmpline);
    if ((tmpchar = strstr(tmpline,"TCS STATUS")) != NULL)
    {
      found_tcstime = 1;
      if ((tmpchar = strstr(tmpchar, "<TD")) != NULL)
      {
        tmpchar = strchr(tmpchar, '>') + 1;
        break;
      }
      else
        continue;
    }
    if (found_tcstime)
    {
      if (strstr(tmpline, "<TD") == NULL)
        continue;
      tmpchar = strchr(tmpline, '>') + 1;
      break;
    }
  }
  if (!found_tcstime)
  {
    if (feof(swasp_html))
      act_log_normal(act_log_msg("End of file reached"));
    if (ferror(swasp_html))
      act_log_error(act_log_msg("An error occurred while attempting to read the file - %d %s", ferror(swasp_html), strerror(ferror(swasp_html))));
    act_log_error(act_log_msg("Could not find TCS status block"));
    return 0;
  }
  unsigned char swasp_min, swasp_hrs, swasp_day, swasp_month;
  short swasp_year;
  if (sscanf(tmpchar, "%02hhu%02hhuUT %02hhu/%02hhu/%02hd", &swasp_hrs, &swasp_min, &swasp_month, &swasp_day, &swasp_year) != 5)
  {
    act_log_error(act_log_msg("Could not extract time and date information from ""%s""", tmpline));
    return 0;
  }
  unid->year = swasp_year + 2000;
  unid->month = swasp_month - 1;
  unid->day = swasp_day - 1;
  unit->hours = swasp_hrs;
  unit->minutes = swasp_min;
  unit->seconds = 0;
  unit->milliseconds = 0;
  return 1;*/
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
char scrape_rel_hum(const char *swasp_dat, int len)
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

/*  if (swasp_html == NULL)
    return -1;
  rewind(swasp_html);
  char colnum = find_colnum(swasp_html, "RH%");
  if (colnum == -1)
    return -1;
  char *rh_val = find_colval(swasp_html, colnum);
  if (rh_val == NULL)
    return -1;
  unsigned char rel_hum;
  if (sscanf(rh_val, "%hhu", &rel_hum) != 1)
    rel_hum = -1;
  free(rh_val);
  return rel_hum;*/
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
char scrape_is_dry(const char *swasp_dat, int len)
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
  
/*  if (swasp_html == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters"));
    return 0;
  }
  rewind(swasp_html);
  char colnum = find_colnum(swasp_html, "RAIN");
  if (colnum == -1)
  {
    act_log_error(act_log_msg("Could not find HTML element containing rain information"));
    return 0;
  }
  char *rain_val = find_colval(swasp_html, colnum);
  if (rain_val == NULL)
  {
    act_log_error(act_log_msg("Could not extract rain information"));
    return 0;
  }
  if (strncmp(rain_val, "DRY", 3) == 0)
  {
    free(rain_val);
    return 1;
  }
  free(rain_val);
  return 0;*/
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
short scrape_wind_speed(const char *swasp_dat, int len)
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
  
  
/*  if (swasp_html == NULL)
    return -1;
  rewind(swasp_html);
  char colnum = find_colnum(swasp_html, "WIND");
  if (colnum == -1)
    return -1;
  colnum--;
  char *speed_val = find_colval(swasp_html, colnum);
  if (speed_val == NULL)
    return -1;
  short wind_speed;
  if (sscanf(speed_val, "%hd", &wind_speed) != 1)
  {
    free(speed_val);
    return -1;
  }
  free(speed_val);
  return wind_speed;*/
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
float scrape_wind_dir(const char *swasp_dat, int len)
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
  
/*  if (swasp_html == NULL)
    return -1.0;
  rewind(swasp_html);
  char colnum = find_colnum(swasp_html, "WIND");
  if (colnum == -1)
    return -1.0;
  char *dir_val = find_colval(swasp_html, colnum);
  if (dir_val == NULL)
    return -1.0;
  float wind_dir = cardinal_to_deg(dir_val);
  free(dir_val);
  return wind_dir;*/
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
float scrape_ext_temp(const char *swasp_dat, int len)
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
    free(str);
    return ABS_ZERO_TEMP_C;
  }
  free(str);
  return ext_temp;
  
  
/*  if (swasp_html == NULL)
    return ABS_ZERO_TEMP_C;
  rewind(swasp_html);
  char colnum = find_colnum(swasp_html, "T<SUB>EXT</SUB>");
  if (colnum == -1)
    return ABS_ZERO_TEMP_C;
  char *temp_val = find_colval(swasp_html, colnum);
  if (temp_val == NULL)
    return ABS_ZERO_TEMP_C;
  float ext_temp = atof(temp_val);
  free(temp_val);
  return ext_temp;*/
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
float scrape_ext_dew_temp(const char *swasp_dat, int len)
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
    free(str);
    return ABS_ZERO_TEMP_C;
  }
  free(str);
  return ext_dew_temp;
  

/*  if (swasp_html == NULL)
    return ABS_ZERO_TEMP_C;
  rewind(swasp_html);
  char colnum = find_colnum(swasp_html, "T<SUB>EXT</SUB> - T<SUB>DEW</SUB>");
  if (colnum == -1)
    return ABS_ZERO_TEMP_C;
  char *temp_val = find_colval(swasp_html, colnum);
  if (temp_val == NULL)
    return ABS_ZERO_TEMP_C;
  float ext_dew_temp = atof(temp_val);
  free(temp_val);
  return ext_dew_temp;*/
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
float scrape_cloud(const char *swasp_dat, int len)
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
    free(str);
    return -1.0;
  }
  free(str);
  return cloud;
  
/*  if (swasp_html == NULL)
    return -1.0;
  rewind (swasp_html);
  char colnum = find_colnum(swasp_html, "CLOUD");
  if (colnum == -1)
    return -1.0;
  char *cloud_val = find_colval(swasp_html, colnum);
  if (cloud_val == NULL)
    return -1.0;
  char *tmpchar = strchr(cloud_val, '(');
  if (tmpchar == NULL)
  {
    free(cloud_val);
    return 100.0;
  }
  float cloud;
  if (sscanf(tmpchar,"(%f)\n", &cloud) != 1)
  {
    free(cloud_val);
    return 100.0;
  }
  return 100.0-cloud;*/
}

/** \brief Extract all useful, available data from SuperWASP website and populate weather data structure.
 * \param filename Name of file (preferably incl. global filesystem path) to file containing SuperWASP website code.
 * \param swasp_weath Pointer to swasp_weath_data struct, where all weather data will be stored.
 * \return \>0 on success, otherwise \=0
 *
 * Master function for extracting all weather data from SuperWASP website.
 * Calls:
 *  -# scrape_unidt
 *  -# scrape_rel_hum
 *  -# scrape_is_dry
 *  -# scrape_wind_speed
 *  -# scrape_wind_dir
 *  -# scrape_ext_temp
 *  -# scrape_ext_dew_temp
 *  -# scrape_cloud
 */
char swasp_scrape_all(const char *swasp_dat, int len, struct swasp_weath_data *swasp_weath)
{
  char swasp_copy[len+1];
  snprintf(swasp_copy, len, "%s", swasp_dat);
  string_to_upper(swasp_copy);
  char ret = 1;
  struct datestruct unid;
  struct timestruct unit;
  if (!scrape_unidt(swasp_copy, len, &unid, &unit))
  {
    act_log_debug(act_log_msg("UNIDT"));
    ret = 0;
  }
  double tmp_jd = calc_GJD(&unid, &unit);
  
  char tmp_rel_hum = scrape_rel_hum(swasp_copy, len);
  if (tmp_rel_hum < 0)
  {
    act_log_debug(act_log_msg("RELHUM (%hhd)", tmp_rel_hum));
    ret = 0;
  }
  
  char tmp_is_dry = scrape_is_dry(swasp_copy, len);
  if (tmp_is_dry < 0)
  {
    act_log_debug(act_log_msg("IS_DRY"));
    ret = 0;
  }
  
  short tmp_wind_speed = scrape_wind_speed(swasp_copy, len); 
  if (tmp_wind_speed < 0)
  {
    act_log_debug(act_log_msg("WIND SPEED (%hd)", tmp_wind_speed));
    ret = 0;
  }
  
  float tmp_wind_dir = scrape_wind_dir(swasp_copy, len);
  if (tmp_wind_dir < 0.0)
  {
    act_log_debug(act_log_msg("WIND DIR (%f)", tmp_wind_dir));
    ret = 0;
  }
  
  float tmp_ext_temp = scrape_ext_temp(swasp_copy, len);
  if (tmp_ext_temp < ABS_ZERO_TEMP_C + 1.0)
  {
    act_log_debug(act_log_msg("EXT_TEMP"));
    ret = 0;
  }
  
  float tmp_ext_dew_temp = scrape_ext_dew_temp(swasp_copy, len);
  if (tmp_ext_dew_temp < ABS_ZERO_TEMP_C + 1.0)
  {
    act_log_debug(act_log_msg("DEW_TEMP (%f)", tmp_ext_dew_temp));
    ret = 0;
  }
  
  float tmp_cloud = scrape_cloud(swasp_copy, len);
  if (tmp_cloud < 0)
  {
    act_log_debug(act_log_msg("CLOUD (%f)", tmp_cloud));
    ret = 0;
  }
  
  swasp_weath->jd = tmp_jd;
  swasp_weath->rel_hum = tmp_rel_hum;
  swasp_weath->rain = (tmp_is_dry == 0);
  swasp_weath->wind_speed = tmp_wind_speed;
  swasp_weath->wind_dir = tmp_wind_dir;
  swasp_weath->ext_temp = tmp_ext_temp;
  swasp_weath->ext_dew_temp = tmp_ext_dew_temp;
  swasp_weath->cloud = tmp_cloud;
  
  return ret;
}

