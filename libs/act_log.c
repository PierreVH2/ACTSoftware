#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "act_log.h"

#define MAX_MSGBUF_LEN 256

void act_log_open()
{
  openlog(__progname, LOG_CONS, LOG_LOCAL0);
}

void act_log_close()
{
  closelog();
}

char *act_log_msg (const char *fmt, ...)
{
  va_list ap;
  char *msgbuf = malloc(MAX_MSGBUF_LEN*sizeof(char));
  va_start(ap, fmt);
  vsnprintf(msgbuf, (MAX_MSGBUF_LEN-1)*sizeof(char), fmt, ap);
  va_end(ap);
  return msgbuf;
}

void act_log_full(int pri, const char *progname_msg, const char *funcname_msg, char *log_msg)
{
  syslog(pri, "[%s %s] %s", progname_msg, funcname_msg, log_msg);
  free(log_msg);
}
