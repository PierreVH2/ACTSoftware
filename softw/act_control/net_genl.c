#include <gtk/gtk.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <act_log.h>
#include <act_ipc.h>
#include "net_genl.h"
#include "net_basic.h"
#include "net_dataccd.h"
#include "net_datapmt.h"
#include "net_targset.h"
#include "subprogrammes.h"

static void process_msg(struct act_prog *prog, struct act_msg *msg, struct act_prog *prog_array, int num_progs);
static void process_quit(struct act_prog *prog);
static void process_cap(struct act_prog *prog, struct act_msg *msg);
static void process_stat(struct act_prog *prog, struct act_msg *msg, struct act_prog *prog_array, int num_progs);
static void process_guisock(struct act_prog *prog);
static void process_coord(struct act_prog *prog_array, int num_progs, struct act_msg *msg);
static void process_time(struct act_prog *prog_array, int num_progs, struct act_msg *msg);
static void process_environ(struct act_prog *prog_array, int num_progs, struct act_msg *msg);
// static void active_time_change(unsigned char new_active_time, struct act_prog *prog_array, int num_progs);

/** \brief Check for an incoming message from subprogramme.
 *
 * \param prog_array Array of programmes managed by act_control
 * \param num_progs Number of elements in prog_array
 *
 * \return Number of messages received
 */
int check_prog_messages(struct act_prog *prog_array, int num_progs)
{
  if (prog_array == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return 0;
  }
  int i, num_msgs=0;
  struct act_msg msgbuf;
  for (i=0; i<num_progs; i++)
  {
    if (prog_array[i].sockfd <= 0)
      continue;
    while (act_recv(&(prog_array[i]), &msgbuf))
    {
      process_msg(&(prog_array[i]), &msgbuf, prog_array, num_progs);
      num_msgs++;
    }
  }
  return num_msgs;
}

char send_statreq(struct act_prog *prog)
{
  struct act_msg msgbuf;
  msgbuf.mtype = MT_STAT;
  msgbuf.content.msg_stat.status = 0;
  if (!act_send(prog, &msgbuf))
  {
    act_log_error(act_log_msg("Error sending status request to %s.", prog->name));
    return FALSE;
  }
  return TRUE;
}

void send_allstop(struct act_prog *prog_array, int num_progs)
{
  int i;
  struct act_msg msgbuf;
  msgbuf.mtype = MT_STAT;
  msgbuf.content.msg_stat.status = PROGSTAT_ERR_CRIT;
  for (i=0; i<num_progs; i++)
  {
    if (!act_send(&prog_array[i], &msgbuf))
      act_log_error(act_log_msg("Error sending all-stop command to %s.", prog_array[i].name));
  }
}

static void process_msg(struct act_prog *prog, struct act_msg *msg, struct act_prog *prog_array, int num_progs)
{
  if ((prog == NULL) || (msg == NULL) || (prog_array == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  switch (msg->mtype)
  {
    case MT_QUIT:
      process_quit(prog);
      break;
    case MT_CAP:
      process_cap(prog, msg);
      if (prog->caps.targset_prov)
        request_targcap(prog);
      if (prog->caps.datapmt_prov)
        request_pmtcap(prog);
      if (prog->caps.dataccd_prov)
        request_ccdcap(prog);
      break;
    case MT_STAT:
      process_stat(prog, msg, prog_array, num_progs);
      break;
    case MT_GUISOCK:
      process_guisock(prog);
      break;
    case MT_TIME:
      process_time(prog_array, num_progs, msg);
      break;
    case MT_COORD:
      process_coord(prog_array, num_progs, msg);
      break;
    case MT_ENVIRON:
      process_environ(prog_array, num_progs, msg);
      break;
    case MT_TARG_CAP:
      process_targcap(prog_array, num_progs, msg);
      break;
    case MT_TARG_SET:
      process_targset(prog_array, num_progs, msg);
      break;
    case MT_PMT_CAP:
      process_pmtcap(prog_array, num_progs, msg);
      break;
    case MT_DATA_PMT:
      process_datapmt(prog_array, num_progs, msg);
      break;
    case MT_CCD_CAP:
      process_ccdcap(prog_array, num_progs, msg);
      break;
    case MT_DATA_CCD:
      process_dataccd(prog_array, num_progs, msg);
      break;
    default:
      if (msg->mtype >= MT_INVAL)
        act_log_error(act_log_msg("Invalid signal received from %s: %d", prog->name, msg->mtype));
      break;
  }
}

/** \brief Process a received MT_QUIT message type received from the subprogramme.
 * \param prog The subprogramme that sent the MT_QUIT message.
 * \return (void)
 *
 * This probably means that the subprogramme encountered an error from which it could not recover and
 * exited normally. As such, the subprogramme should probably not be restarted. However, it should
 * be checked that the subprogramme did not provide a necessary service. Therefore, only indicate
 * that the programme has exited by the prog struct's status flag.
 */
static void process_quit(struct act_prog *prog)
{
  if (prog == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  close(prog->sockfd);
  prog->sockfd = 0;
  prog_set_status(prog, PROGSTAT_STOPPED);
}

/** \brief Process an MT_CAP message received from a programme.
 * \param prog The subprogramme that sent the MT_CAP message.
 * \param msg_cap The received message structure.
 * \return (void)
 * 
 * Simply copy the MT_CAP message structure to the subprogramme's prog struct.
 */
static void process_cap(struct act_prog *prog, struct act_msg *msg)
{
  if ((prog == NULL) || (msg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  memcpy(&(prog->caps), &(msg->content.msg_cap), sizeof(struct act_msg_cap));
}

/** \brief Process an MT_STAT message received from a programme.
 * \param prog The programme that sent the MT_STAT message.
 * \param msg_stat The received message structure.
 * \return (void)
 * 
 * If new status is same as old status, return.
 * Update prog->status.
 * Update the indicator button to reflect new status.
 */
static void process_stat(struct act_prog *prog, struct act_msg *msg, struct act_prog *prog_array, int num_progs)
{
  if ((prog == NULL) || (msg == NULL) || (prog_array == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  prog->last_stat_timer = 0;
  if (prog_set_status(prog, msg->content.msg_stat.status) == 0)
    return;
    
  if (prog->status == PROGSTAT_ERR_CRIT)
  {
    act_log_normal(act_log_msg("Received Critical Error status message from %s. Sending allstop.", prog->name));
    send_allstop(prog_array, num_progs);
  }
  else if (prog->status == PROGSTAT_ERR_RESTART)
  {
    act_log_normal(act_log_msg("Received restart request from %s.", prog->name));
    prog_set_status(prog, PROGSTAT_KILLED);
    kill(prog->pid, SIGTERM);
    if (!start_prog(prog))
      act_log_error(act_log_msg("Programme %s encountered an error and needed to be restarted. It has been stopped, but cannot start up again.", prog->name));
  }
}

/** \brief Process an MT_GUISOCK message received from a programme.
 * \param prog The programme that sent the MT_GUISOCK message.
 * \param msg_stat The received message structure.
 * \return (void)
 * 
 * Respond with programme's allocated GtkSocket's X11 ID (if available - 0 otherwise).
 */
static void process_guisock(struct act_prog *prog)
{
  if (prog == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  struct act_msg msg;
  msg.mtype = MT_GUISOCK;
  if (prog->socket == NULL)
  {
    act_log_error(act_log_msg("%s requested a GUI socket, but no space has been allocated for it. %s will not be able to create a GUI.", prog->name, prog->name));
    msg.content.msg_guisock.gui_socket = 0;
  }
  else if (GTK_WIDGET_REALIZED(prog->socket))
    msg.content.msg_guisock.gui_socket = gtk_socket_get_id(GTK_SOCKET(prog->socket));
  else
  {
    act_log_error(act_log_msg("GUI socket for %s has not yet been realised, hopefully it will be soon.", prog->name));
    msg.content.msg_guisock.gui_socket = 0;
  }
  if (!act_send(prog, &msg))
    act_log_error(act_log_msg("Error sending GUI socket information to %s (fd %d).", prog->name, prog->sockfd));
}

/** \brief Process received MT_COORD message.
 * \param prog_array Array of all programmes.
 * \param num_progs Number of elements in prog_array.
 * \param msg_coord Received MT_COORD message.
 *
 * Forward MT_COORD message to all running programmes that requires it.
 */
static void process_coord(struct act_prog *prog_array, int num_progs, struct act_msg *msg)
{
  if ((prog_array == NULL) || (msg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  int i;
  for (i=0; i<num_progs; i++)
  {
    if ((prog_array[i].status != PROGSTAT_RUNNING) && (prog_array[i].status != PROGSTAT_STARTUP))
      continue;
    if ((prog_array[i].caps.service_needs & SERVICE_COORD) == 0)
      continue;
    if (!act_send(&(prog_array[i]), msg))
      act_log_error(act_log_msg("Error sending telescope coordinates to %s (fd %d).", prog_array[i].name, prog_array[i].sockfd));
  }
}

/** \brief Process received MT_TIME message.
 * \param prog_array Array of all programmes.
 * \param num_progs Number of elements in prog_array.
 * \param msg_coord Received MT_TIME message.
 *
 * Forward MT_TIME message to all running programmes that requires it.
 */
static void process_time(struct act_prog *prog_array, int num_progs, struct act_msg *msg)
{
  if ((prog_array == NULL) || (msg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  int i;
  for (i=0; i<num_progs; i++)
  {
    if ((prog_array[i].status != PROGSTAT_RUNNING) && (prog_array[i].status != PROGSTAT_STARTUP))
      continue;
    if ((prog_array[i].caps.service_needs & SERVICE_TIME) == 0)
      continue;
    if (!act_send(&(prog_array[i]), msg))
      act_log_error(act_log_msg("Error sending time to %s (fd %d).", prog_array[i].name, prog_array[i].sockfd));
  }
}

/** \brief Process received MT_ENVIRON message.
 * \param prog_array Array of all programmes.
 * \param num_progs Number of elements in prog_array.
 * \param msg_coord Received MT_ENVIRON message.
 *
 * Forward MT_ENVIRON message to all running programmes that require it.
 */
static void process_environ(struct act_prog *prog_array, int num_progs, struct act_msg *msg)
{
  if ((prog_array == NULL) || (msg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  int i;
  for (i=0; i<num_progs; i++)
  {
    if ((prog_array[i].caps.service_needs & SERVICE_ENVIRON) == 0)
      continue;
    if (!act_send(&(prog_array[i]), msg))
      act_log_error(act_log_msg("Error sending situational parameters to %s (fd %d).", prog_array[i].name, prog_array[i].sockfd));
  }
  if (*(prog_array[0].status_active) == msg->content.msg_environ.status_active)
    return;
  for (i=0; i<num_progs; i++)
    prog_active_change(&(prog_array[i]), msg->content.msg_environ.status_active);
  *(prog_array[0].status_active) = msg->content.msg_environ.status_active;
}
