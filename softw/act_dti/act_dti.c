#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <gtk/gtk.h>
#include <argtable2.h>
#include <math.h>
#include <act_site.h>
#include <act_ipc.h>
#include <act_log.h>

#include "dti_config.h"
#include "dti_plc.h"
#include "dti_net.h"
#include "dti_motor.h"

#include "acqmir.h"
#include "aperture.h"
#include "domemove.h"
#include "domeshutter.h"
#include "dropout.h"
#include "dti_config.h"
#include "dtimisc.h"
#include "dti_motor.h"
#include "dti_net.h"
#include "dti_plc.h"
#include "filter.h"
#include "instrshutt.h"
#include "telmove.h"

#define TABLE_PADDING                5       // pixels
#define WATCHDOG_TIMEOUT_S           30
#define NUM_WIDGETS                  9

enum
{
  STAGE_QUIT_INSTRSHUTT = 1,
  STAGE_QUIT_ACQMIR,
  STAGE_QUIT_FILTER,
  STAGE_QUIT_APERTURE,
  STAGE_QUIT_DROPOUT,
  STAGE_QUIT_DOMESHUTT,
  STAGE_QUIT_TELMOVE,
  STAGE_QUIT_DOMEMOVE,
  STAGE_QUIT_DONE
};

enum
{
  STAGE_TARGSET_DOMESHUTT = 1,
  STAGE_TARGSET_DROPOUT,
  STAGE_TARGSET_ACQMIR,
  STAGE_TARGSET_DOMEAUTO,
  STAGE_TARGSET_TELMOVE,
  STAGE_TARGSET_DONE
};

enum
{
  STAGE_DATAPMT_MISC = 1,
  STAGE_DATAPMT_FILTER,
  STAGE_DATAPMT_APERTURE,
  STAGE_DATAPMT_ACQMIR,
  STAGE_DATAPMT_INSTRHUTT,
  STAGE_DATAPMT_DONE
};

enum
{
  STAGE_DATACCD_ACQMIR = 1,
  STAGE_DATACCD_DONE
};

struct form_main
{
  guchar progstat;
  gint watchdog_reset_to_id;
  gpointer dti_net, dti_motor, dti_plc;
  GtkWidget *box_main;
  GtkWidget *domeshutter, *dropout, *domemove;
  GtkWidget *telmove, *dtimisc;
  GtkWidget *acqmir, *filter, *aperture, *instrshutt;
  struct act_msg_targcap targcap_msg;
  struct act_msg_ccdcap ccdcap_msg;
  struct act_msg_pmtcap pmtcap_msg;
};

gboolean watchdog_reset(gpointer user_data);
void request_guisock(gpointer form);
void main_receive_message(gpointer form, gpointer msg);
void main_process_message_resp(gpointer user_data, guchar ret, gpointer msg);
void interlock_domeshutter_open(gpointer form, gboolean is_open);
void interlock_dropout_closed(gpointer form, gboolean is_closed);
void main_send_coord(gpointer form, gpointer coord_msg);
void main_set_caps(gpointer form);
void process_quit(gpointer form, gpointer msg, guchar ret);
void process_cap(gpointer form, gpointer msg);
void process_guisock(gpointer form, gpointer msg);
void process_targset(gpointer form, gpointer msg, guchar ret);
void process_datapmt(gpointer form, gpointer msg, guchar ret);
void process_dataccd(gpointer form, gpointer msg, guchar ret);
void process_message_all(gpointer form, gpointer msg);
void process_stat_resp(gpointer form, gpointer msg, guchar ret);
void process_response_all(gpointer msg);
void destroy_gui_plug(GtkWidget *plug, gpointer box_main);

int main(int argc, char** argv)
{
  act_log_open();
  act_log_normal(act_log_msg("Starting up."));
  
  const char *host, *port, *sqlhost;
  gtk_init(&argc, &argv);
  struct arg_str *addrarg = arg_str1("a", "addr", "<str>", "The host to connect to. May be a hostname, IP4 address or IP6 address.");
  struct arg_str *portarg = arg_str1("p", "port", "<str>", "The port to connect to. Must be an unsigned short integer.");
  struct arg_str *sqlhostarg = arg_str1("s", "sqlhost", "<server ip/hostname>", "The hostname or IP address of the SQL server than contains act_control's configuration information");
  struct arg_end *endargs = arg_end(10);
  void* argtable[] = {addrarg, portarg, sqlhostarg, endargs};
  if (arg_nullcheck(argtable) != 0)
  {
    act_log_error(act_log_msg("Argument parsing error: insufficient memory."));
    act_log_error(act_log_msg("Exiting"));
    return 1;
  }
  int argparse_errors = arg_parse(argc,argv,argtable);
  if (argparse_errors != 0)
  {
    arg_print_errors(stderr,endargs,argv[0]);
    act_log_error(act_log_msg("Exiting"));
    return 1;
  }
  host = addrarg->sval[0];
  port = portarg->sval[0];
  sqlhost = sqlhostarg->sval[0];
  arg_freetable(argtable,sizeof(argtable)/sizeof(argtable[0]));
  
  struct form_main form;
  memset(&form, 0, sizeof(struct form_main));
  form.progstat = PROGSTAT_STARTUP;
  
  if (!parse_config(sqlhost, &form.targcap_msg, &form.pmtcap_msg, &form.ccdcap_msg))
  {
    act_log_crit(act_log_msg ("Failed to load configuration data."));
    return 1;
  }
  
  form.dti_net = G_OBJECT(dti_net_new(host, port));
  form.dti_motor = G_OBJECT(dti_motor_new());
  form.dti_plc = G_OBJECT(dti_plc_new());
  if ((form.dti_net == NULL) || (form.dti_motor == NULL) || (form.dti_plc == NULL))
  {
    if (form.dti_net == NULL)
      act_log_crit(act_log_msg("Could not establish network connection."));
    else
    {
      g_object_ref_sink(G_OBJECT(form.dti_net));
      g_object_unref(G_OBJECT(form.dti_net));
      form.dti_net = NULL;
    }
    if (form.dti_motor == NULL)
      act_log_crit(act_log_msg("Could not access motor driver."));
    else
    {
      g_object_ref_sink(G_OBJECT(form.dti_motor));
      g_object_unref(G_OBJECT(form.dti_motor));
      form.dti_net = NULL;
    }
    if (form.dti_plc == NULL)
      act_log_crit(act_log_msg("Could not access PLC driver."));
    else
    {
      g_object_ref_sink(G_OBJECT(form.dti_plc));
      g_object_unref(G_OBJECT(form.dti_plc));
      form.dti_plc = NULL;
    }
    return 1;
  }
  g_object_ref_sink(G_OBJECT(form.dti_net));
  g_object_ref_sink(G_OBJECT(form.dti_motor));
  g_object_ref_sink(G_OBJECT(form.dti_plc));
    
  form.box_main = gtk_table_new(0, 0, FALSE);
  g_object_ref (G_OBJECT(form.box_main));
  
  form.domeshutter = domeshutter_new(dti_plc_get_domeshutt_stat(DTI_PLC(form.dti_plc)));
//   g_object_ref(G_OBJECT(form.domeshutter));
  gtk_table_attach(GTK_TABLE(form.box_main), form.domeshutter, 0, 1, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(form.domeshutter), "is-open", G_CALLBACK(interlock_domeshutter_open), &form);
  g_signal_connect_swapped(G_OBJECT(form.domeshutter), "start-open", G_CALLBACK(dti_plc_send_domeshutter_open), form.dti_plc);
  g_signal_connect_swapped(G_OBJECT(form.domeshutter), "start-close", G_CALLBACK(dti_plc_send_domeshutter_close), form.dti_plc);
  g_signal_connect_swapped(G_OBJECT(form.domeshutter), "stop", G_CALLBACK(dti_plc_send_domeshutter_stop), form.dti_plc);
  g_signal_connect_swapped(G_OBJECT(form.domeshutter), "proc-complete", G_CALLBACK(main_process_message_resp), &form);
  g_signal_connect_swapped(G_OBJECT(form.dti_plc), "domeshutt-stat-update", G_CALLBACK(domeshutter_update), form.domeshutter);
    
  form.dropout = dropout_new(dti_plc_get_dropout_stat(DTI_PLC(form.dti_plc)));
//   g_object_ref(G_OBJECT(form.dropout));
  gtk_table_attach(GTK_TABLE(form.box_main), form.dropout, 0, 1, 1, 2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(form.dropout), "is-closed", G_CALLBACK(interlock_dropout_closed), &form);
  g_signal_connect_swapped(G_OBJECT(form.dropout), "start-open", G_CALLBACK(dti_plc_send_dropout_open), form.dti_plc);
  g_signal_connect_swapped(G_OBJECT(form.dropout), "start-close", G_CALLBACK(dti_plc_send_dropout_close), form.dti_plc);
  g_signal_connect_swapped(G_OBJECT(form.dropout), "stop", G_CALLBACK(dti_plc_send_dropout_stop), form.dti_plc);
  g_signal_connect_swapped(G_OBJECT(form.dropout), "proc-complete", G_CALLBACK(main_process_message_resp), &form);
  g_signal_connect_swapped(G_OBJECT(form.dti_plc), "dropout-stat-update", G_CALLBACK(dropout_update), form.dropout);
  
  form.domemove = domemove_new(dti_plc_get_dome_moving(DTI_PLC(form.dti_plc)), dti_plc_get_dome_azm(DTI_PLC(form.dti_plc)));
//   g_object_ref(G_OBJECT(form.domemove));
  gtk_table_attach(GTK_TABLE(form.box_main), form.domemove, 0, 1, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(form.domemove), "start-move-left", G_CALLBACK(dti_plc_send_domemove_start_left), form.dti_plc);
  g_signal_connect_swapped(G_OBJECT(form.domemove), "start-move-right", G_CALLBACK(dti_plc_send_domemove_start_right), form.dti_plc);
  g_signal_connect_swapped(G_OBJECT(form.domemove), "stop-move", G_CALLBACK(dti_plc_send_domemove_stop), form.dti_plc);
  g_signal_connect_swapped(G_OBJECT(form.domemove), "send-azm", G_CALLBACK(dti_plc_send_domemove_azm), form.dti_plc);
  g_signal_connect_swapped(G_OBJECT(form.domemove), "proc-complete", G_CALLBACK(main_process_message_resp), &form);
  g_signal_connect_swapped(G_OBJECT(form.dti_plc), "dome-azm-update", G_CALLBACK(domemove_update_azm), form.domemove);
  g_signal_connect_swapped(G_OBJECT(form.dti_plc), "dome-moving-update", G_CALLBACK(domemove_update_moving), form.domemove);

  form.dtimisc = dtimisc_new(TRUE, dti_plc_get_watchdog_tripped(DTI_PLC(form.dti_plc)), dti_plc_get_power_failed(DTI_PLC(form.dti_plc)), dti_plc_get_trapdoor_open(DTI_PLC(form.dti_plc)), dti_plc_get_eht_stat(DTI_PLC(form.dti_plc)), dti_plc_get_focus_stat(DTI_PLC(form.dti_plc)), dti_plc_get_focus_pos(DTI_PLC(form.dti_plc)));
//   g_object_ref(G_OBJECT(form.dtimisc));
  gtk_table_attach(GTK_TABLE(form.box_main), form.dtimisc, 0, 2, 3, 4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(form.dtimisc), "send-focus-pos", G_CALLBACK(dti_plc_send_focus_pos), form.dti_plc);
  g_signal_connect_swapped(G_OBJECT(form.dtimisc), "send-eht-high", G_CALLBACK(dti_plc_send_eht_high), form.dti_plc);
  g_signal_connect_swapped(G_OBJECT(form.dtimisc), "proc-complete", G_CALLBACK(main_process_message_resp), &form);
  g_signal_connect_swapped(G_OBJECT(form.dti_plc), "plc-comm-stat-update", G_CALLBACK(dtimisc_update_plccomm), form.dtimisc);
  g_signal_connect_swapped(G_OBJECT(form.dti_plc), "power-fail-update", G_CALLBACK(dtimisc_update_power), form.dtimisc);
  g_signal_connect_swapped(G_OBJECT(form.dti_plc), "watchdog-trip-update", G_CALLBACK(dtimisc_update_watchdog), form.dtimisc);
  g_signal_connect_swapped(G_OBJECT(form.dti_plc), "trapdoor-update", G_CALLBACK(dtimisc_update_trapdoor), form.dtimisc);
  g_signal_connect_swapped(G_OBJECT(form.dti_plc), "focus-pos-update", G_CALLBACK(dtimisc_update_focus_pos), form.dtimisc);
  g_signal_connect_swapped(G_OBJECT(form.dti_plc), "focus-stat-update", G_CALLBACK(dtimisc_update_focus_stat), form.dtimisc);
  g_signal_connect_swapped(G_OBJECT(form.dti_plc), "eht-stat-update", G_CALLBACK(dtimisc_update_eht), form.dtimisc);

  form.telmove = telmove_new();
//   g_object_ref(G_OBJECT(form.telmove));
  gtk_table_attach(GTK_TABLE(form.box_main), form.telmove, 1, 2, 0, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(form.telmove), "send-coord", G_CALLBACK(main_send_coord), &form);
  g_signal_connect_swapped(G_OBJECT(form.telmove), "proc-complete", G_CALLBACK(main_process_message_resp), &form);

  form.acqmir = acqmir_new(dti_plc_get_acqmir_stat(DTI_PLC(form.dti_plc)));
//   g_object_ref(G_OBJECT(form.acqmir));
  gtk_table_attach(GTK_TABLE(form.box_main), form.acqmir, 2, 3, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(form.acqmir), "send-acqmir-view", G_CALLBACK(dti_plc_send_acqmir_view), form.dti_plc);g_signal_connect_swapped(G_OBJECT(form.acqmir), "send-acqmir-meas", G_CALLBACK(dti_plc_send_acqmir_meas), form.dti_plc);
  g_signal_connect_swapped(G_OBJECT(form.acqmir), "send-acqmir-stop", G_CALLBACK(dti_plc_send_acqmir_stop), form.dti_plc);
  g_signal_connect_swapped(G_OBJECT(form.acqmir), "proc-complete", G_CALLBACK(main_process_message_resp), &form);
  g_signal_connect_swapped(G_OBJECT(form.dti_plc), "acqmir-stat-update", G_CALLBACK(acqmir_update), form.acqmir);
  
  form.filter = filter_new(dti_plc_get_filt_stat(DTI_PLC(form.dti_plc)), dti_plc_get_filt_slot(DTI_PLC(form.dti_plc)));
//   g_object_ref(G_OBJECT(form.filter));
  gtk_table_attach(GTK_TABLE(form.box_main), form.filter, 2, 3, 1, 2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(form.filter), "send-filter", G_CALLBACK(dti_plc_send_change_filter), form.dti_plc);
  g_signal_connect_swapped(G_OBJECT(form.filter), "proc-complete", G_CALLBACK(main_process_message_resp), &form);
  g_signal_connect_swapped(G_OBJECT(form.dti_plc), "filt-pos-update", G_CALLBACK(filter_update_slot), form.filter);
  g_signal_connect_swapped(G_OBJECT(form.dti_plc), "filt-stat-update", G_CALLBACK(filter_update_stat), form.filter);
  
  form.aperture = aperture_new(dti_plc_get_aper_stat(DTI_PLC(form.dti_plc)), dti_plc_get_aper_slot(DTI_PLC(form.dti_plc)));
//   g_object_ref(G_OBJECT(form.aperture));
  gtk_table_attach(GTK_TABLE(form.box_main), form.aperture, 2, 3, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(form.aperture), "send-aperture", G_CALLBACK(dti_plc_send_change_aperture), form.dti_plc);
  g_signal_connect_swapped(G_OBJECT(form.aperture), "proc-complete", G_CALLBACK(main_process_message_resp), &form);
  g_signal_connect_swapped(G_OBJECT(form.dti_plc), "aper-pos-update", G_CALLBACK(aperture_update_slot), form.aperture);
  g_signal_connect_swapped(G_OBJECT(form.dti_plc), "aper-stat-update", G_CALLBACK(aperture_update_stat), form.aperture);
  
  form.instrshutt = instrshutt_new(dti_plc_get_instrshutt_open(DTI_PLC(form.dti_plc)));
//   g_object_ref(G_OBJECT(form.instrshutt));
  gtk_table_attach(GTK_TABLE(form.box_main), form.instrshutt, 2, 3, 3, 4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(form.instrshutt), "send-instrshutt-open", G_CALLBACK(dti_plc_send_instrshutt_toggle), form.dti_plc);
  g_signal_connect_swapped(G_OBJECT(form.instrshutt), "proc-complete", G_CALLBACK(main_process_message_resp), &form);
  g_signal_connect_swapped(G_OBJECT(form.dti_plc), "instrshutt-open-update", G_CALLBACK(instrshutt_update), form.instrshutt);
  
  g_signal_connect_swapped(G_OBJECT(form.dti_net), "message-received", G_CALLBACK(main_receive_message), &form);
  
  main_set_caps((void *)&form);
  request_guisock(form.dti_net);
  form.watchdog_reset_to_id = g_timeout_add_seconds(WATCHDOG_TIMEOUT_S, watchdog_reset, form.dti_plc);
  
  act_log_normal(act_log_msg("Entering main loop."));
  gtk_main();
  // ??? Send quit acknowledge
  g_source_remove(form.watchdog_reset_to_id);
  g_object_unref(form.dti_net);
  g_object_unref(form.dti_plc);
  g_object_unref(form.dti_motor);
  act_log_normal(act_log_msg("Exiting"));
  return 0;
}

gboolean watchdog_reset(gpointer form)
{
  struct form_main *objs = (struct form_main *) form;
  dti_plc_send_watchdog_reset(DTI_PLC(objs->dti_plc));
  return TRUE;
}

void request_guisock(gpointer form)
{
  struct form_main *objs = (struct form_main *) form;
  DtiMsg * msg = dti_msg_new(NULL, 0);
  if (g_object_is_floating(G_OBJECT(msg)))
    g_object_ref_sink(G_OBJECT(msg));
  dti_msg_set_mtype (msg, MT_GUISOCK);
  memset(dti_msg_get_guisock(msg), 0, sizeof(struct act_msg_guisock));
  if (dti_net_send(DTI_NET(objs->dti_net), msg) < 0)
    act_log_crit(act_log_msg("Failed to request GUI socket."));
  g_object_unref(msg);
}

void main_receive_message(gpointer form, gpointer msg)
{
//   struct form_main *objs = (struct form_main *)form;
  if (g_object_is_floating(G_OBJECT(msg)))
    g_object_ref_sink(G_OBJECT(msg));
  dti_msg_set_dtistage(msg, 0);
  gint mtype = dti_msg_get_mtype(msg);
  switch(mtype)
  {
    case MT_QUIT:
      if (!dti_msg_get_quit(msg)->mode_auto)
        process_message_all(form, msg);
      else
        process_quit(form, msg, OBSNSTAT_GOOD);
      break;
    case MT_CAP:
      process_cap(form, msg);
      break;
    case MT_STAT:
      process_message_all(form, msg);
      break;
    case MT_GUISOCK:
      process_guisock(form, msg);
      break;
    case MT_COORD:
      act_log_error(act_log_msg("Received coordinates message. Ignoring."));
      g_object_unref(G_OBJECT(msg));
      break;
    case MT_TIME:
      process_message_all(form, msg);
      break;
    case MT_ENVIRON:
      process_message_all(form, msg);
      break;
    case MT_TARG_CAP:
      process_message_all(form, msg);
      break;
    case MT_TARG_SET:
      process_targset(form, msg, OBSNSTAT_GOOD);
      break;
    case MT_PMT_CAP:
      process_message_all(form, msg);
      break;
    case MT_DATA_PMT:
      process_datapmt(form, msg, OBSNSTAT_GOOD);
      break;
    case MT_CCD_CAP:
      process_message_all(form, msg);
      break;
    case MT_DATA_CCD:
      process_dataccd(form, msg, OBSNSTAT_GOOD);
      break;
    default:
      act_log_error(act_log_msg("Received message with invalid type: %d", mtype));
      g_object_unref(G_OBJECT(msg));
  }
}

void main_process_message_resp(gpointer form, guchar ret, gpointer msg)
{
  gint mtype = dti_msg_get_mtype(msg);
  switch(mtype)
  {
    case MT_QUIT:
      if (!dti_msg_get_quit(msg)->mode_auto)
        process_response_all(msg);
      else
        process_quit(form, msg, ret);
      break;
    case MT_CAP:
      act_log_error(act_log_msg("Received capabilities response. Ignoring."));
      g_object_unref(G_OBJECT(msg));
      break;
    case MT_STAT:
      process_stat_resp(form, msg, ret);
      break;
    case MT_GUISOCK:
      act_log_error(act_log_msg("Received GUI socket response. Ignoring."));
      g_object_unref(G_OBJECT(msg));
      break;
    case MT_COORD:
      process_response_all(msg);
      break;
    case MT_TIME:
      process_response_all(msg);
      break;
    case MT_ENVIRON:
      process_response_all(msg);
      break;
    case MT_TARG_CAP:
      process_response_all(msg);
      break;
    case MT_TARG_SET:
      process_targset(form, msg, ret);
      break;
    case MT_PMT_CAP:
      process_response_all(msg);
      break;
    case MT_DATA_PMT:
      process_datapmt(form, msg, ret);
      break;
    case MT_CCD_CAP:
      process_response_all(msg);
      break;
    case MT_DATA_CCD:
      process_dataccd(form, msg, ret);
      break;
    default:
      act_log_error(act_log_msg("Received message response with invalid type: %d", mtype));
  }
}

void interlock_domeshutter_open(gpointer form, gboolean is_open)
{
  dropout_set_lock(((struct form_main *)form)->dropout, !is_open);
}

void interlock_dropout_closed(gpointer form, gboolean is_closed)
{
  domeshutter_set_lock(((struct form_main *)form)->domeshutter, !is_closed);
}

void main_send_coord(gpointer form, gpointer coord_msg)
{
  if (g_object_is_floating(G_OBJECT(coord_msg)))
    g_object_ref_sink(coord_msg);
  process_message_all(form, coord_msg);
  if (dti_net_send(((struct form_main *)form)->dti_net, coord_msg) < 0)
    act_log_error(act_log_msg("Failed to send coordinates message."));
}

void main_set_caps(gpointer form)
{
  DtiMsg *tmp_msg = dti_msg_new(NULL, 0);
  struct form_main *objs = (struct form_main *)form;

  dti_msg_set_mtype (tmp_msg, MT_TARG_CAP);
  memcpy(dti_msg_get_targcap(tmp_msg), &objs->targcap_msg, sizeof(struct act_msg_targcap));
  domeshutter_process_msg(objs->domeshutter, tmp_msg);
  dropout_process_msg(objs->dropout, tmp_msg);
  domemove_process_msg(objs->domemove, tmp_msg);
  telmove_process_msg(objs->telmove, tmp_msg);
  dtimisc_process_msg(objs->dtimisc, tmp_msg);
  acqmir_process_msg(objs->acqmir, tmp_msg);
  filter_process_msg(objs->filter, tmp_msg);
  aperture_process_msg(objs->aperture, tmp_msg);
  instrshutt_process_msg(objs->instrshutt, tmp_msg);
  
  dti_msg_set_mtype (tmp_msg, MT_PMT_CAP);
  memcpy(dti_msg_get_pmtcap(tmp_msg), &objs->pmtcap_msg, sizeof(struct act_msg_pmtcap));
  domeshutter_process_msg(objs->domeshutter, tmp_msg);
  dropout_process_msg(objs->dropout, tmp_msg);
  domemove_process_msg(objs->domemove, tmp_msg);
  telmove_process_msg(objs->telmove, tmp_msg);
  dtimisc_process_msg(objs->dtimisc, tmp_msg);
  acqmir_process_msg(objs->acqmir, tmp_msg);
  filter_process_msg(objs->filter, tmp_msg);
  aperture_process_msg(objs->aperture, tmp_msg);
  instrshutt_process_msg(objs->instrshutt, tmp_msg);
  
  dti_msg_set_mtype (tmp_msg, MT_CCD_CAP);
  memcpy(dti_msg_get_ccdcap(tmp_msg), &objs->ccdcap_msg, sizeof(struct act_msg_ccdcap));
  domeshutter_process_msg(objs->domeshutter, tmp_msg);
  dropout_process_msg(objs->dropout, tmp_msg);
  domemove_process_msg(objs->domemove, tmp_msg);
  telmove_process_msg(objs->telmove, tmp_msg);
  dtimisc_process_msg(objs->dtimisc, tmp_msg);
  acqmir_process_msg(objs->acqmir, tmp_msg);
  filter_process_msg(objs->filter, tmp_msg);
  aperture_process_msg(objs->aperture, tmp_msg);
  instrshutt_process_msg(objs->instrshutt, tmp_msg);
}

void process_quit(gpointer form, gpointer msg, guchar ret)
{
  dti_msg_inc_dtistage(msg);
  guint stage = dti_msg_get_dtistage(msg);
  if (ret != OBSNSTAT_GOOD)
    act_log_crit(act_log_msg("An error occurred while performing auto close. Error occurred during stage %d. Continuing with auto close and hoping for the best.", stage-1));
  struct form_main *objs = (struct form_main *)form;
  switch (stage)
  {
    case STAGE_QUIT_INSTRSHUTT:
      instrshutt_process_msg(objs->instrshutt, msg);
      break;
    case STAGE_QUIT_ACQMIR:
      acqmir_process_msg(objs->acqmir, msg);
      break;
    case STAGE_QUIT_FILTER:
      filter_process_msg(objs->filter, msg);
      break;
    case STAGE_QUIT_APERTURE:
      aperture_process_msg(objs->aperture, msg);
      break;
    case STAGE_QUIT_DROPOUT:
      dropout_process_msg(objs->dropout, msg);
      break;
    case STAGE_QUIT_DOMESHUTT:
      domeshutter_process_msg(objs->domeshutter, msg);
      break;
    case STAGE_QUIT_TELMOVE:
      telmove_process_msg(objs->telmove, msg);
      break;
    case STAGE_QUIT_DOMEMOVE:
      domemove_process_msg(objs->domemove, msg);
      break;
    case STAGE_QUIT_DONE:
      act_log_debug(act_log_msg("Finished automatic quit."));
      if (dti_net_send(objs->dti_net, msg) < 0)
        act_log_error(act_log_msg("Failed to send automatic quit response message."));
      g_object_unref(G_OBJECT(msg));
      break;
    default:
      act_log_error(act_log_msg("Invalid stage number for automatic quit procedure (%u)", dti_msg_get_dtistage));
      if (stage < STAGE_QUIT_DONE)
        process_quit(form, msg, ret);
      else
      {
        if (dti_net_send(objs->dti_net, msg) < 0)
          act_log_error(act_log_msg("Failed to send automatic quit response message."));
        g_object_unref(G_OBJECT(msg));
      }
  }
}

void process_cap(gpointer form, gpointer msg)
{
  struct act_msg_cap *msg_cap = dti_msg_get_cap(msg);
  msg_cap->service_provides = SERVICE_COORD;
  msg_cap->service_needs = SERVICE_TIME | SERVICE_ENVIRON;
  msg_cap->targset_prov = TARGSET_MOVE_TEL;
  msg_cap->datapmt_prov = DATAPMT_PREP_PHOTOM;
  msg_cap->dataccd_prov = DATACCD_PREP_PHOTOM;
  snprintf(msg_cap->version_str, MAX_VERSION_LEN, "%d.%d", MAJOR_VER, MINOR_VER);
  if (dti_net_send(((struct form_main *)form)->dti_net, msg) < 0)
    act_log_error(act_log_msg("Failed to send capabilities response message."));
}

void process_guisock(gpointer form, gpointer msg)
{
  struct act_msg_guisock *msg_guisock = dti_msg_get_guisock(msg);
  if (msg_guisock->gui_socket <= 0)
  {
    act_log_normal(act_log_msg("Strange: GUI socket message received with 0 GUI socket. Ignoring"));
    g_object_unref(G_OBJECT(msg));
    return;
  }
  struct form_main *objs = (struct form_main *)form;
  if (gtk_widget_get_parent(GTK_WIDGET(objs->box_main)) != NULL)
  {
    act_log_normal(act_log_msg("Strange: Received GUI socket message from act_control, but GUI components already embedded. Ignoring this message."));
    g_object_unref(G_OBJECT(msg));
    return;
  }
  GtkWidget *plg_new = gtk_plug_new(msg_guisock->gui_socket);
  gtk_container_add(GTK_CONTAINER(plg_new),objs->box_main);
  g_signal_connect(G_OBJECT(plg_new),"destroy",G_CALLBACK(destroy_gui_plug),objs->box_main);
  gtk_widget_show_all(plg_new);
  g_object_unref(G_OBJECT(msg));
}

void process_targset(gpointer form, gpointer msg, guchar ret)
{
  struct form_main *objs = (struct form_main *)form;
  if (ret != OBSNSTAT_GOOD)
  {
    act_log_normal(act_log_msg("An error was encountered while processing a target set message (level: %hhu). Error occurred during stage %u. Reporting error.", ret, dti_msg_get_dtistage(msg)));
    struct act_msg_targset *msg_targset = dti_msg_get_targset(msg);
    msg_targset->status = ret;
    if (ret == OBSNSTAT_ERR_CRIT)
    {
      act_log_crit(act_log_msg("Last error is critical. Notifying all DTI components."));
      g_object_ref(G_OBJECT(msg));
      process_message_all(form, msg);
    }
    if (dti_net_send(objs->dti_net, msg) < 0)
      act_log_error(act_log_msg("Failed to send target set response message."));
    g_object_unref(G_OBJECT(msg));
    return;
  }
  dti_msg_inc_dtistage(msg);
  guint stage = dti_msg_get_dtistage(msg);
  switch(stage)
  {
    case STAGE_TARGSET_DOMESHUTT:
      domeshutter_process_msg(objs->domeshutter, msg);
      break;
    case STAGE_TARGSET_DROPOUT:
      dropout_process_msg(objs->dropout, msg);
      break;
    case STAGE_TARGSET_ACQMIR:
      acqmir_process_msg(objs->acqmir, msg);
      break;
    case STAGE_TARGSET_DOMEAUTO:
      domemove_process_msg(objs->domemove, msg);
      break;
    case STAGE_TARGSET_TELMOVE:
      telmove_process_msg(objs->telmove, msg);
      break;
    case STAGE_TARGSET_DONE:
      act_log_debug(act_log_msg("Done processing target set message."));
      if (dti_net_send(objs->dti_net, msg) < 0)
        act_log_error(act_log_msg("Failed to send target set response message."));
      g_object_unref(G_OBJECT(msg));
      break;
    default:
      act_log_error(act_log_msg("Invalid stage number for target set procedure (%u)", dti_msg_get_dtistage));
      if (stage < STAGE_TARGSET_DONE)
        process_targset(form, msg, ret);
      else
      {
        if (dti_net_send(objs->dti_net, msg) < 0)
          act_log_error(act_log_msg("Failed to send target set response message."));
        g_object_unref(G_OBJECT(msg));
      }
  }
}

void process_datapmt(gpointer form, gpointer msg, guchar ret)
{
  struct form_main *objs = (struct form_main *)form;
  if (ret != OBSNSTAT_GOOD)
  {
    act_log_normal(act_log_msg("An error was encountered while processing a data PMT message (level: %hhu). Error occurred during stage %u. Reporting error.", ret, dti_msg_get_dtistage(msg)));
    struct act_msg_datapmt *msg_datapmt = dti_msg_get_datapmt(msg);
    msg_datapmt->status = ret;
    if (ret == OBSNSTAT_ERR_CRIT)
    {
      act_log_crit(act_log_msg("Last error is critical. Notifying all DTI components."));
      g_object_ref(G_OBJECT(msg));
      process_message_all(form, msg);
    }
    if (dti_net_send(objs->dti_net, msg) < 0)
      act_log_error(act_log_msg("Failed to send data PMT response message."));
    g_object_unref(G_OBJECT(msg));
    return;
  }
  dti_msg_inc_dtistage(msg);
  guint stage = dti_msg_get_dtistage(msg);
  switch(stage)
  {
    case STAGE_DATAPMT_MISC:
      dtimisc_process_msg(objs->dtimisc, msg);
      break;
    case STAGE_DATAPMT_FILTER:
      filter_process_msg(objs->filter, msg);
      break;
    case STAGE_DATAPMT_APERTURE:
      aperture_process_msg(objs->aperture, msg);
      break;
    case STAGE_DATAPMT_ACQMIR:
      acqmir_process_msg(objs->acqmir, msg);
      break;
    case STAGE_DATAPMT_INSTRHUTT:
      instrshutt_process_msg(objs->instrshutt, msg);
      break;
    case STAGE_DATAPMT_DONE:
      act_log_debug(act_log_msg("Done processing data PMT message."));
      if (dti_net_send(objs->dti_net, msg) < 0)
        act_log_error(act_log_msg("Failed to send data PMT response message."));
      g_object_unref(G_OBJECT(msg));
      break;
    default:
      act_log_error(act_log_msg("Invalid stage number for data PMT procedure (%u)", dti_msg_get_dtistage));
      if (stage < STAGE_DATAPMT_DONE)
        process_datapmt(form, msg, ret);
      else
      {
        if (dti_net_send(objs->dti_net, msg) < 0)
          act_log_error(act_log_msg("Failed to send data PMT response message."));
        g_object_unref(G_OBJECT(msg));
      }
  }
}

void process_dataccd(gpointer form, gpointer msg, guchar ret)
{
  struct form_main *objs = (struct form_main *)form;
  if (ret != OBSNSTAT_GOOD)
  {
    act_log_normal(act_log_msg("An error was encountered while processing a data CCD message (level: %hhu). Error occurred during stage %u. Reporting error.", ret, dti_msg_get_dtistage(msg)));
    struct act_msg_dataccd *msg_dataccd = dti_msg_get_dataccd(msg);
    msg_dataccd->status = ret;
    if (ret == OBSNSTAT_ERR_CRIT)
    {
      act_log_crit(act_log_msg("Last error is critical. Notifying all DTI components."));
      g_object_ref(G_OBJECT(msg));
      process_message_all(form, msg);
    }
    if (dti_net_send(objs->dti_net, msg) < 0)
      act_log_error(act_log_msg("Failed to send data CCD response message."));
    g_object_unref(G_OBJECT(msg));
    return;
  }
  dti_msg_inc_dtistage(msg);
  guint stage = dti_msg_get_dtistage(msg);
  switch (stage)
  {
    case STAGE_DATACCD_ACQMIR:
      acqmir_process_msg(objs->acqmir, msg);
      break;
    case STAGE_DATACCD_DONE:
      act_log_debug(act_log_msg("Done processing data CCD message."));
      if (dti_net_send(objs->dti_net, msg) < 0)
        act_log_error(act_log_msg("Failed to send data CCD response message."));
      g_object_unref(G_OBJECT(msg));
      break;
    default:
      act_log_error(act_log_msg("Invalid stage number for data CCD procedure (%u)", dti_msg_get_dtistage));
      if (stage < STAGE_DATACCD_DONE)
        process_datapmt(form, msg, ret);
      else
      {
        if (dti_net_send(objs->dti_net, msg) < 0)
          act_log_error(act_log_msg("Failed to send data CCD response message."));
        g_object_unref(G_OBJECT(msg));
      }
  }
}

void process_message_all(gpointer form, gpointer msg)
{
  struct form_main *objs = (struct form_main *)form;
  dti_msg_set_num_pending(msg, NUM_WIDGETS);
  domeshutter_process_msg(objs->domeshutter, msg);
  dropout_process_msg(objs->dropout, msg);
  domemove_process_msg(objs->domemove, msg);
  telmove_process_msg(objs->telmove, msg);
  dtimisc_process_msg(objs->dtimisc, msg);
  acqmir_process_msg(objs->acqmir, msg);
  filter_process_msg(objs->filter, msg);
  aperture_process_msg(objs->aperture, msg);
  instrshutt_process_msg(objs->instrshutt, msg);
}

void process_stat_resp(gpointer form, gpointer msg, guchar ret)
{
  /// TODO: Implement proper status reporting from multiple widgets
  (void) form;
  (void) ret;
  process_response_all(msg);
}

void process_response_all(gpointer msg)
{
  guint num_pending = dti_msg_get_num_pending(msg);
  if (num_pending <= 1)
  {
    if (num_pending < 1)
      act_log_error(act_log_msg("Message response received, but pending counter already at 0."));
    g_object_unref(msg);
  }
  dti_msg_dec_num_pending(msg);
}

void destroy_gui_plug(GtkWidget *plug, gpointer box_main)
{
  gtk_container_remove(GTK_CONTAINER(plug),GTK_WIDGET(box_main));
}
