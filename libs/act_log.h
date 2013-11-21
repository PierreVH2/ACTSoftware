#include <stdio.h>
#include <syslog.h>

#define act_log_debug(msg) act_log_full(LOG_DEBUG, __progname, __FUNCTION__, msg)
#define act_log_normal(msg) act_log_full(LOG_INFO, __progname, __FUNCTION__, msg)
#define act_log_error(msg)  act_log_full(LOG_ERR, __progname, __FUNCTION__, msg)
#define act_log_level(level,msg), act_log_full(lvel, __progname, __FUNCTION__, msg)

extern const char *__progname;

void act_log_open();
void act_log_close();
char *act_log_msg(const char *fmt, ...);
void act_log_full(int log_level, const char *progname_msg, const char *funcname_msg, char *log_msg);

