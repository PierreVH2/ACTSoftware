#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <act_plc.h>
#include <act_log.h>
#include <act_ipc.h>
#include "form_main.h"
#include "domeshutter.h"
#include "dropout.h"
#include "domemove.h"
#include "telmove.h"
#include "dtimisc.h"
#include "aperture.h"
#include "filter.h"
#include "instrshutt.h"

#define PROC_SEQ_NUM_FUNCS  9

static void form_main_class_init (FormMainClass *klass);
static void form_main_init(GtkWidget *form_main);
static void start_process_msg(GtkWidget *form_main);

enum
{
  SEND_NET_MSG_SIGNAL,
  LAST_SIGNAL
};

static guint form_main_signals[LAST_SIGNAL] = { 0 };
static void (*proc_default_seq_funcs[PROC_SEQ_NUM_FUNCS]) (GtkWidget *widget, struct act_msg *msg) = 
{
  dtimisc_process_msg,
  instrshutt_process_msg,
  acqmir_process_msg,
  filter_process_msg,
  aperture_process_msg,
  domemove_process_msg,
  telmove_process_msg,
  domeshutter_process_msg,
  dropout_process_msg
};

GType form_main_get_type (void)
{
  static GType form_main_type = 0;
  
  if (!form_main_type)
  {
    const GTypeInfo form_main_info =
    {
      sizeof (FormMainClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) form_main_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (FormMain),
      0,
      (GInstanceInitFunc) form_main_init,
      NULL
    };
    
    form_main_type = g_type_register_static (GTK_TYPE_WIDGET, "FormMain", &form_main_info, 0);
  }
  
  return form_main_type;
}

GtkWidget *form_main_new (void)
{
  GtkWidget *form_main = g_object_new (form_main_get_type (), NULL);
  gtk_table_set_homogeneous(GTK_TABLE(form_main), FALSE);
  FormMain *objs = FORM_MAIN(form_main);
  
  objs->dti_plc = (void *)dti_plc_new();
  if (objs->dti_plc == NULL)
  {
    act_log_error(act_log_msg("Failed to create PLC connection object."));
    return NULL;
  }
  if (g_object_is_floating(G_OBJECT(objs->dti_plc)))
    g_object_ref_sink(G_OBJECT(objs->dti_plc));
  
  objs->domeshutter = domeshutter_new(dti_plc_get_domeshutt_stat(DTI_PLC(objs->dti_plc)));
  g_object_ref(G_OBJECT(objs->domeshutter));
  gtk_table_attach(GTK_TABLE(form_main), objs->domeshutter, 0, 1, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(objs->domeshutter), "is-open", G_CALLBACK(interlock_domeshutter_open), form_main);
  g_signal_connect_swapped(G_OBJECT(objs->domeshutter), "start-open", G_CALLBACK(dti_plc_send_domeshutter_open), objs->dti_plc);
  g_signal_connect_swapped(G_OBJECT(objs->domeshutter), "start-close", G_CALLBACK(dti_plc_send_domeshutter_close), objs->dti_plc);
  g_signal_connect_swapped(G_OBJECT(objs->domeshutter), "stop", G_CALLBACK(dti_plc_send_domeshutter_stop), objs->dti_plc);
  g_signal_connect_swapped(G_OBJECT(objs->domeshutter), "proc-complete", G_CALLBACK(main_process_message), form_main);
  
  objs->dropout = dropout_new(dti_plc_get_dropout_stat(DTI_PLC(objs->dti_plc)));
  g_object_ref(G_OBJECT(objs->dropout));
  gtk_table_attach(GTK_TABLE(form_main), objs->dropout, 0, 1, 1, 2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(objs->dropout), "is-closed", G_CALLBACK(interlock_dropout_closed), form_main);
  g_signal_connect_swapped(G_OBJECT(objs->dropout), "start-open", G_CALLBACK(dti_plc_send_dropout_open), objs->dti_plc);
  g_signal_connect_swapped(G_OBJECT(objs->dropout), "start-close", G_CALLBACK(dti_plc_send_dropout_close), objs->dti_plc);
  g_signal_connect_swapped(G_OBJECT(objs->dropout), "stop", G_CALLBACK(dti_plc_send_dropout_stop), objs->dti_plc);
  g_signal_connect_swapped(G_OBJECT(objs->dropout), "proc-complete", G_CALLBACK(main_process_message), form_main);

  objs->domemove = domemove_new(dti_plc_get_dome_moving(DTI_PLC(objs->dti_plc)),dti_plc_get_dome_azm(DTI_PLC(objs->dti_plc)));
  g_object_ref(G_OBJECT(objs->domemove));
  gtk_table_attach(GTK_TABLE(form_main), objs->domemove, 0, 1, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  objs->dtimisc = dtimisc_new(TRUE, dti_plc_get_watchdog_tripped(DTI_PLC(objs->dti_plc)), dti_plc_get_power_failed(DTI_PLC(objs->dti_plc)), dti_plc_get_trapdoor_open(DTI_PLC(objs->dti_plc)), dti_plc_get_eht_mode(DTI_PLC(objs->dti_plc)), dti_plc_get_focus_stat(DTI_PLC(objs->dti_plc)), dti_plc_get_focus_pos(DTI_PLC(objs->dti_plc)));
  g_object_ref(G_OBJECT(objs->dtimisc));
  gtk_table_attach(GTK_TABLE(form_main), objs->dtimisc, 0, 2, 3, 4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  objs->telmove = telmove_new();
  g_object_ref(G_OBJECT(objs->telmove));
  gtk_table_attach(GTK_TABLE(form_main), objs->telmove, 1, 2, 0, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  objs->acqmir = acqmir_new(dti_plc_get_acqmir_stat(DTI_PLC(objs->dti_plc)));
  g_object_ref(G_OBJECT(objs->acqmir));
  gtk_table_attach(GTK_TABLE(form_main), objs->acqmir, 2, 3, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  objs->filter = filter_new(dti_plc_get_filter_stat(DTI_PLC(objs->dti_plc)));
  g_object_ref(G_OBJECT(objs->filter));
  gtk_table_attach(GTK_TABLE(form_main), objs->filter, 2, 3, 1, 2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  objs->aperture = aperture_new(dti_plc_get_aperture_stat(DTI_PLC(objs->dti_plc)));
  g_object_ref(G_OBJECT(objs->aperture));
  gtk_table_attach(GTK_TABLE(form_main), objs->aperture, 2, 3, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  objs->instrshutt = instrshutt_new(dti_plc_get_instrshutt_stat(DTI_PLC(objs->dti_plc)));
  g_object_ref(G_OBJECT(objs->instrshutt));
  gtk_table_attach(GTK_TABLE(form_main), objs->instrshutt, 2, 3, 3, 4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  return form_main;
}

int form_main_process_msg (GtkWidget *form_main, struct act_msg *msg)
{
  if ((msg->mtype < 1) || (msg_mtype >= MT_INVAL) || (msg->mtype == MT_CAP) || (msg->mtype == MT_STAT) || (msg->mtype == MT_GUISOCK))
  {
    act_log_debug(act_log_msg("Invalid message type (%d)", msg->mtype));
    return -EINVAL;
  }
  if ((msg->mtype == MT_QUIT) || (msg->mtype == MT_TARG_SET) || (msg->mtype == MT_DATA_PMT) || (msg->mtype == MT_DATA_CCD))
    return process_msg_seq(form_main, msg);
  if ((msg->mtype == MT_COORD) || (msg->mtype == MT_TIME) || (msg->mtype == MT_ENVIRON) || (msg->mtype == MT_TARG_CAP) || (msg->mtype == MT_PMT_CAP) || (msg->mtype == MT_CCD_CAP))
    return process_msg_simult(form_main, msg);
    
    
  MT_COORD,          /**< Telescope coordinates.*/
  MT_TIME,           /**< Current time.*/
  MT_ENVIRON,        /**< Environment/situational parameters.*/
  MT_TARG_CAP,       /**< Capabilities for setting target (software telescope limits etc.) */
  MT_PMT_CAP,        /**< Photometric capabilities with PMT */
  MT_CCD_CAP,        /**< Photometric capabilities with CCD */
  
  struct act_msg *msg_copy = malloc(sizeof(struct act_msg));
  FormMain *objs = FORM_MAIN(form_main);
  objs->pending_msgs = g_slist_append(objs->pending_msgs, msg_copy);
  if (objs->proc_seq_num > 0)
    return;
  start_process_msg(objs);
}

static void form_main_class_init (FormMainClass *klass)
{
  telmove_signals[SEND_MSG_SIGNAL] = g_signal_new("send-msg", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);
}

static void form_main_init(GtkWidget *form_main)
{
  FormMain *objs = FORM_MAIN(form_main);
  objs->mode_auto = FALSE;
  objs->proc_seq_num = 0;
  objs->pending_msgs = NULL;
  objs->box = objs->domeshutter = objs->dropout = objs->domemove = objs->telmove = objs->dtimisc = objs->acqmir = objs->filter = objs->aperture = objs->instrshutt = FALSE;
}

static void process_msg_next(GtkWidget *form_main)
{
  FormMain *objs = FORM_MAIN(form_main);
  if (objs->pending_msgs == NULL)
  {
    act_log_debug(act_log_msg("No message in queue."));
    return;
  }
  struct act_msg *msg = objs->pending_msgs->data;
  if (msg == NULL)
  {
    act_log_error(act_log_msg("Message queue entry with no message found. Skipping to next entry."));
    objs->proc_seq_num = PROC_SEQ_NUM_FUNCS;
  }
  objs->proc_seq_num++;
  if (objs->proc_seq_num > PROC_SEQ_NUM_FUNCS)
  {
    act_log_debug(act_log_msg("Done processing message."));
    objs->proc_seq_num = 0;
    GSList *tmp = objs->pending_msgs;
    objs->pending_msgs = tmp->next;
    tmp->next = NULL;
    g_signal_emit(G_OBJECT(form_main), form_main_signals[SEND_MSG_SIGNAL], 0, msg);
    g_free(tmp);
    process_msg_next(form_main);
    return;
  }
  unsigned char next_num = objs->proc_seq_num;
  if (msg->mtype == MT_QUIT)
    next_num = PROC_SEQ_NUM_FUNCS - next_num + 1;
  switch(next_num)
  {
    case 1:
      dtimisc_process_msg(objs->dtimisc, msg);
      break;
    case 2:
      instrshutt_process_msg(objs->instrshutt, msg);
      break;
    case 3:
      acqmir_process_msg(objs->acqmir, msg);
      break;
    case 4:
      filter_process_msg(objs->filter, msg);
      break;
    case 5:
      aperture_process_msg(objs->aperture, msg);
      break;
    case 6:
      domemove_process_msg(objs->domemove, msg);
      break;
    case 7:
      telmove_process_msg(objs->telmove, msg);
      break;
    case 8:
      domeshutter_process_msg(objs->domeshutter, msg);
      break;
    case 9:
      dropout_process_msg(objs->dropout, msg);
      break;
    default:
      act_log_error(act_log_msg("Invalid procedure sequence number (%d). This should have been caught earlier. Continuing to next message.", next_num));
      process_msg_next(form_main);
  }
}


/*  
 *  gint plc_fd = g_io_channel_unix_get_fd (objs->plc_chan);
 *  long newacqmir = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_meas)) ? 2 : 1;
 *  gint ret = ioctl(plc_fd, IOCTL_SET_ACQMIR, &newacqmir);
 *  if (ret != 0)
 *  {
 *    act_log_error(act_log_msg("Error communicating with PLC driver while trying to move acquisition mirror - %s.", strerror(ret)));
 *    g_signal_emit (acqmir, acqmir_signals[PLC_COMM_STAT_SIGNAL], 0, FALSE);
 *    if (objs->proc_pending)
 *    {
 *      objs->proc_pending = FALSE;
 *      g_signal_emit(acqmir, acqmir_signals[PROC_COMPLETE_SIGNAL], 0, OBSNSTAT_ERR_RETRY);
 *    }
 *    return;
 *  }
 *  g_signal_emit (acqmir, acqmir_signals[PLC_COMM_STAT_SIGNAL], 0, TRUE);
 *  if (objs->fail_to_id)
 *    g_source_remove(objs->fail_to_id);
 *  objs->fail_to_id = g_timeout_add_seconds(ACQMIR_FAIL_TIME_S, fail_timeout, objs);*/



#define TIME_TIMEOUT_MS 300000
#define ENV_TIMEOUT_MS  600000

enum
{
  DTIPROC_INIT = 1,
  DTIPROC_TARGSET,
  DTIPROC_INSTRPREP,
  DTIPROC_INSTRFIN,
  DTIPROC_CLOSE,
  DTIPROC_EMERG_CLOSE
};

enum
{
  DTIINIT_START = 1,
  DTIINIT_INSTRINIT,
  DTIINIT_TELPREINIT,
  DTIINIT_DOMEPREINIT,
  DTIINIT_TELINIT,
  DTIINIT_DONE
};

enum
{
  DTITARGSET_START = 1,
  DTITARGSET_INSTRACQ,
  DTITARGSET_DSHUTTOPEN,
  DTITARGSET_DRPOUTOPEN,
  DTITARGSET_MOVETEL,
  DTITARGSET_MOVEDOME,
  DTITARGSET_DONE
};

enum
{
  DTIINSTRPREP_START = 1,
  DTIINSTRPREP_BUSY,
  DTIINSTRPREP_DONE
};

enum
{
  DTIINSTRFIN_START = 1,
  DTIINSTRFIN_BUSY,
  DTIINSTRFIN_DONE
};

enum
{
  DTICLOSE_START = 1,
  DTICLOSE_INSTRCLOSE,
  DTICLOSE_DRPOUTCLOSE,
  DTICLOSE_DSHUTTCLOSE,
  DTICLOSE_TELPARK,
  DTICLOSE_DOMEPARK,
  DTICLOSE_DONE
};

void update_dshutt_dropout_sens(struct domeshutter_objects *dshutt_objs, struct dropout_objects *dropout_objs, struct plc_status *old_stat, struct plc_status *new_stat);
void update_handset(struct telmove_objects *objs, struct plc_status *old_stat, struct plc_status *new_stat);
int form_check_init_stat(struct formobjects *objs);
int form_check_targset_stat(struct formobjects *objs);
int form_start_tel_goto(struct formobjects *objs, struct act_msg_targset *targst_msg);
int form_check_instrprep_stat(struct formobjects *objs);
int form_check_instrprep_pmt_stat(struct formobjects *objs);
int form_check_instrprep_ccd_stat(struct formobjects *objs);
int form_check_instrfin_stat(struct formobjects *objs);
int form_check_close_stat(struct formobjects *objs);
int form_check_emerg_close_stat(struct formobjects *objs);

struct formobjects * init_gui(struct plc_status *status, GIOChannel *plc_chan, int *motor_fd)
{
  if ((status == NULL) || (plc_chan == NULL) || (motor_fd == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return NULL;
  }
  
  struct formobjects *objs = malloc(sizeof(struct formobjects));

  objs->dti_proc = objs->dtiinit_stat = objs->dtitargset_stat = objs->dtiinstrprep_stat = objs->dtiinstrfin_stat = objs->dticlose_stat = 0;
  objs->weath_ok = 0;
  objs->last_env_msec = -1;
  objs->sidt_msec = 0;
  objs->last_sidt_msec = -1;
  
  objs->box_main = gtk_hbox_new(FALSE,5);
  g_object_ref(objs->box_main);
  GtkWidget *box_left = gtk_vbox_new(FALSE,0);
  gtk_box_pack_start(GTK_BOX(objs->box_main),box_left,TRUE,TRUE,5);
  GtkWidget *box_mid = gtk_vbox_new(FALSE,0);
  gtk_box_pack_start(GTK_BOX(objs->box_main),box_mid,TRUE,TRUE,5);
  GtkWidget *box_right = gtk_vbox_new(FALSE,0);
  gtk_box_pack_start(GTK_BOX(objs->box_main),box_right,TRUE,TRUE,5);

  objs->domeshutter = create_domeshutter(box_left, status, plc_chan);
  if (objs->domeshutter == NULL)
    act_log_error(act_log_msg("Could not create dome shutter GUI components."));
  objs->dropout = create_dropout(box_left, status, plc_chan);
  if (objs->dropout == NULL)
    act_log_error(act_log_msg("Could not create dome dropout GUI components."));
  objs->domemove = create_domemove(box_left, status, plc_chan);
  if (objs->domemove == NULL)
    act_log_error(act_log_msg("Could not create dome move GUI components."));
  objs->plcmisc = create_plcmisc(box_left, status);
  if (objs->plcmisc == NULL)
    act_log_error(act_log_msg("Could not create miscellaneous PLC status GUI components."));
  objs->telfocus = create_telfocus(box_mid, status, plc_chan);
  if (objs->telfocus == NULL)
    act_log_error(act_log_msg("Could not create telescope focus GUI components."));
  objs->telmove = create_telmove(box_mid, motor_fd, &objs->sidt_msec, &objs->last_sidt_msec);
  if (objs->telmove == NULL)
    act_log_error(act_log_msg("Could not create telescope move GUI components."));
  objs->ehtcntrl = create_ehtcntrl(box_right, status, plc_chan);
  if (objs->ehtcntrl == NULL)
    act_log_error(act_log_msg("Could not create EHT control GUI components."));
  objs->filter = create_filter(box_right, status, plc_chan);
  if (objs->filter == NULL)
    act_log_error(act_log_msg("Could not create filter GUI components."));
  objs->aperture = create_aperture(box_right, status, plc_chan);
  if (objs->aperture == NULL)
    act_log_error(act_log_msg("Could not create aperture GUI components."));
  objs->acqmir = create_acqmir(box_right, status, plc_chan);
  if (objs->acqmir == NULL)
    act_log_error(act_log_msg("Could not create acquisition mirror GUI components."));
  objs->instrshutt = create_instrshutt(box_right, status, plc_chan);
  if (objs->instrshutt == NULL)
    act_log_error(act_log_msg("Could not create instrument shutter GUI components."));

  update_dshutt_dropout_sens(objs->domeshutter, objs->dropout, NULL, status);

  return objs;
}

void form_set_targcaps(struct formobjects *objs, struct act_msg_targcap *targcap_msg)
{
  // ??? Not implemented
  (void) objs;
  (void) targcap_msg;
}

void form_set_pmtcaps(struct formobjects *objs, struct act_msg_pmtcap *pmtcap_msg)
{
  if ((objs == NULL) || (pmtcap_msg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  set_filter_table(objs->filter, pmtcap_msg);
  set_aperture_table(objs->aperture, pmtcap_msg);
}

void form_set_ccdcaps(struct formobjects *objs, struct act_msg_ccdcap *ccdcap_msg)
{
  // ??? Not implemented
  (void) objs;
  (void) ccdcap_msg;
}

void update_plc_indicators(struct formobjects *objs, struct plc_status *old_stat, struct plc_status *new_stat)
{
  if (objs == NULL)
    return;
  update_domeshutter(objs->domeshutter, old_stat, new_stat);
  update_dropout(objs->dropout, old_stat, new_stat);
  update_domemove(objs->domemove, old_stat, new_stat);
  update_plcmisc(objs->plcmisc, old_stat, new_stat);
  update_telfocus(objs->telfocus, old_stat, new_stat);
  update_ehtcntrl(objs->ehtcntrl, old_stat, new_stat);
  update_filter(objs->filter, old_stat, new_stat);
  update_aperture(objs->aperture, old_stat, new_stat);
  update_acqmir(objs->acqmir, old_stat, new_stat);
  update_instrshutt(objs->instrshutt, old_stat, new_stat);

  update_dshutt_dropout_sens(objs->domeshutter, objs->dropout, old_stat, new_stat);
  update_handset(objs->telmove, old_stat, new_stat);
}

void update_plccomm_indicator(struct formobjects *objs, char comm_good)
{
  if (objs == NULL)
    return;
  update_plccomm(objs->plcmisc, comm_good);
}

void form_update_telmove_stat(struct formobjects *objs, unsigned char comm_ok, unsigned char stat)
{
  if (objs == NULL)
    return;
  update_telmove_stat(objs->telmove, comm_ok, stat);
}

void form_update_telmove_coord(struct formobjects *objs)
{
  if (objs == NULL)
    return;
  update_telmove_coord(objs->telmove);
}

void form_update_telmove_limits(struct formobjects *objs)
{
  if (objs == NULL)
    return;
  update_telmove_limits(objs->telmove);
}

void form_set_env(struct formobjects *objs, struct act_msg_environ *env_msg)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters"));
    return;
  }
  
  struct timeval cur_time;
  gettimeofday(&cur_time, NULL);
  objs->last_env_msec = (cur_time.tv_sec%86400)*1000 + cur_time.tv_usec/1000;
  float sun_alt = convert_DMS_D_alt(&env_msg->sun_alt);
  if (((objs->weath_ok > 0) == (env_msg->weath_ok > 0)) && ((sun_alt > 0) == (objs->sun_alt > 0)))
    return;
  domeshutter_set_env(objs->domeshutter, env_msg);
  objs->weath_ok = env_msg->weath_ok;
  objs->sun_alt = sun_alt;
  if ((objs->weath_ok == 0) || (sun_alt > 0))
  {
    act_log_normal(act_log_msg("Received bad weather warning."));
    if (form_start_proc_close(objs) < 0)
    {
      act_log_error(act_log_msg("Error encountered while attempting to close down and secure telescope."));
      form_start_proc_emerg_close(objs);
    }
  }
}

char form_check_weath(struct formobjects *objs)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return -1;
  }
  if (objs->last_env_msec < 0)
    return 0;
  struct timeval cur_time;
  gettimeofday(&cur_time, NULL);
  long cur_time_msec = (cur_time.tv_sec%86400)*1000 + cur_time.tv_usec/1000;
  if (abs(cur_time_msec - abs(objs->last_env_msec)) > ENV_TIMEOUT_MS)
  {
    act_log_error(act_log_msg("Timed out while waiting for environmental message. Assuming weather not safe for observing."));
    struct act_msg_environ env_msg = { .weath_ok = FALSE };
    form_set_env(objs, &env_msg);
    objs->last_env_msec *= -1;
    return 0;
  }
  return 1;
}

void form_update_time(struct formobjects *objs, struct act_msg_time *time_msg)
{
  if (objs == NULL)
    return;
  objs->sidt_msec = convert_HMSMS_MS_time(&time_msg->sidt);
  objs->last_sidt_msec = convert_HMSMS_MS_time(&time_msg->unit);
}

char form_get_time_ok(struct formobjects *objs)
{
  if (objs == NULL)
    return 0;
  struct timeval cur_time;
  gettimeofday(&cur_time, NULL);
  long cur_time_msec = (cur_time.tv_sec%86400)*1000 + cur_time.tv_usec/1000;
  if (abs(cur_time_msec - objs->last_sidt_msec) > TIME_TIMEOUT_MS)
    objs->last_sidt_msec = -1;
  return objs->last_sidt_msec >= 0;
}

void form_get_coords(struct formobjects *objs, struct act_msg_coord *coord_msg)
{
  if ((objs == NULL) || (coord_msg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  telmove_get_coords(objs->telmove, coord_msg);
//   send_coords(objs->telmove, netfd);
}

void form_set_dome_azm(struct formobjects *objs)
{
  if (objs == NULL)
    return;
  if (!domemove_get_auto_on(objs->domemove))
    return;
  set_dome_azm(objs->domemove, get_tel_azm(objs->telmove));
}

int form_start_proc_init(struct formobjects *objs)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters"));
    return -OBSNSTAT_ERR_RETRY;
  }
  if (objs->dti_proc > 0)
  {
    act_log_error(act_log_msg("A DTI procedure is currently underway. Cannot start DTI initialisation."));
    return -OBSNSTAT_ERR_WAIT;
  }
  act_log_normal(act_log_msg("Starting DTI initialisation."));
  objs->dti_proc = DTIPROC_INIT;
  objs->dtiinit_stat = DTIINIT_START;
  return 0;
}

int form_start_proc_targset(struct formobjects *objs, struct act_msg *obsn_msg)
{
  if ((objs == NULL) || (obsn_msg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters"));
    return -OBSNSTAT_ERR_RETRY;
  }
  if (objs->dti_proc > 0)
  {
    act_log_error(act_log_msg("A DTI procedure is currently underway. Cannot set target."));
    return -OBSNSTAT_ERR_WAIT;
  }
  if (!form_check_init_stat(objs))
  {
    act_log_normal(act_log_msg("DTI has not yet been initialised."));
    form_start_proc_init(objs);
    return -OBSNSTAT_ERR_WAIT;
  }
  if (obsn_msg->mtype != MT_TARG_SET)
  {
    act_log_error(act_log_msg("Invalid input message type (expected %d, got %d)", MT_TARG_SET, obsn_msg->mtype));
    return -OBSNSTAT_ERR_RETRY;
  }
  if (objs->obsn_msg != NULL)
    act_log_error(act_log_msg("An observation message is currently saved, but no DTI procedure is underway. Strange. Overwriting existing message."));
  else
  {
    objs->obsn_msg = malloc(sizeof(struct act_msg));
    if (objs->obsn_msg == NULL)
    {
      act_log_error(act_log_msg("No space could be allocated for observation message."));
      return -OBSNSTAT_ERR_WAIT;
    }
  }
  memcpy(objs->obsn_msg, obsn_msg, sizeof(struct act_msg));
  act_log_normal(act_log_msg("Starting target set."));
  objs->dti_proc = DTIPROC_TARGSET;
  objs->dtitargset_stat = DTITARGSET_START;
  return 0;
}

int form_start_proc_instrprep(struct formobjects *objs, struct act_msg *obsn_msg)
{
  if ((objs == NULL) || (obsn_msg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters"));
    return -OBSNSTAT_ERR_RETRY;
  }
  if (objs->dti_proc > 0)
  {
    act_log_error(act_log_msg("A DTI procedure is currently underway. Cannot start pre-observation instrument preparation."));
    return -OBSNSTAT_ERR_WAIT;
  }
  if ((obsn_msg->mtype != MT_DATA_PMT) && (obsn_msg->mtype != MT_DATA_CCD))
  {
    act_log_error(act_log_msg("Invalid input message type (expected %d or %d, got %d)", MT_DATA_PMT, MT_DATA_CCD, obsn_msg->mtype));
    return -OBSNSTAT_ERR_RETRY;
  }
  if (objs->obsn_msg != NULL)
    act_log_error(act_log_msg("An observation message is currently saved, but no DTI procedure is underway. Strange. Overwriting existing message."));
  else
  {
    objs->obsn_msg = malloc(sizeof(struct act_msg));
    if (objs->obsn_msg == NULL)
    {
      act_log_error(act_log_msg("No space could be allocated for observation message."));
      return -OBSNSTAT_ERR_WAIT;
    }
  }
  memcpy(objs->obsn_msg, obsn_msg, sizeof(struct act_msg));
  act_log_normal(act_log_msg("Starting pre-observation instrument preparation."));
  objs->dti_proc = DTIPROC_INSTRPREP;
  objs->dtiinstrprep_stat = DTIINSTRPREP_START;
  return 0;
}

int form_start_proc_instrfin(struct formobjects *objs)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters"));
    return -OBSNSTAT_ERR_RETRY;
  }
  if (objs->dti_proc > 0)
  {
    act_log_error(act_log_msg("A DTI procedure is currently underway. Cannot start post-observation instrument closure."));
    return -1;
  }
  act_log_normal(act_log_msg("Starting post-observation instrument closure."));
  objs->dti_proc = DTIPROC_INSTRFIN;
  objs->dtiinstrfin_stat = DTIINSTRFIN_START;
  return 0;
}

int form_start_proc_close(struct formobjects *objs)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters"));
    return -OBSNSTAT_ERR_RETRY;
  }
  if (objs->dti_proc >= DTIPROC_CLOSE)
  {
    act_log_error(act_log_msg("DTI is already closing."));
    return 0;
  }
  act_log_normal(act_log_msg("Starting DTI closure."));
  objs->dti_proc = DTIPROC_CLOSE;
  objs->dticlose_stat = DTICLOSE_START;
  return 0;
}

int form_start_proc_emerg_close(struct formobjects *objs)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input paramters."));
    return -1;
  }
  if (objs->dti_proc == DTIPROC_EMERG_CLOSE)
  {
    act_log_normal(act_log_msg("DTI emergency closure has already been initiated."));
    return 0;
  }
  if (objs->obsn_msg != NULL)
  {
    free(objs->obsn_msg);
    objs->obsn_msg = NULL;
  }
  act_log_normal(act_log_msg("Starting DTI emergency closure."));
  objs->dti_proc = DTIPROC_EMERG_CLOSE;
  telmove_set_emerg_stop(objs->telmove, TRUE);
  set_instrshutt_open(objs->instrshutt, FALSE);
  domeshutter_set_open(objs->domeshutter, FALSE);
  dropout_set_open(objs->dropout, FALSE);
  if ((get_instrshutt_isopen(objs->instrshutt) < 0) ||
      (domeshutter_get_isclosed(objs->domeshutter) < 0) ||
      (dropout_get_isclosed(objs->dropout) < 0))
    return -1;
  if ((get_instrshutt_isopen(objs->instrshutt) == 0) &&
      (domeshutter_get_isclosed(objs->domeshutter) > 0) &&
      (dropout_get_isclosed(objs->dropout) > 0))
    return 1;
  return 0;
}

int form_check_proc_stat(struct formobjects *objs)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return -OBSNSTAT_ERR_RETRY;
  }
  if (objs->dti_proc == 0)
    return 0;
  int ret_val = 0;
  switch(objs->dti_proc)
  {
    case DTIPROC_INIT:
    {
      ret_val = form_check_init_stat(objs);
      if (ret_val < 0)
        act_log_error(act_log_msg("An error occurred while attempting to initialise DTI."));
      else if (ret_val > 0)
        act_log_normal(act_log_msg("DTI initialisation complete."));
      break;
    }
    case DTIPROC_TARGSET:
    {
      ret_val = form_check_targset_stat(objs);
      if (ret_val < 0)
        act_log_error(act_log_msg("An error occurred while attempting to set the target."));
      else if (ret_val > 0)
        act_log_normal(act_log_msg("Target is set."));
      break;
    }
    case DTIPROC_INSTRPREP:
    {
      ret_val = form_check_instrprep_stat(objs);
      if (ret_val < 0)
        act_log_error(act_log_msg("An error occurred while performing pre-observation instrument preparation."));
      else if (ret_val > 0)
        act_log_normal(act_log_msg("Instrument is ready for observation."));
      break;
    }
    case DTIPROC_INSTRFIN:
    {
      ret_val = form_check_instrfin_stat(objs);
      if (ret_val < 0)
        act_log_error(act_log_msg("An error occurred while performing post-observation instrument closure."));
      else if (ret_val > 0)
        act_log_normal(act_log_msg("Instrument is closed."));
      break;
    }
    case DTIPROC_CLOSE:
    {
      ret_val = form_check_close_stat(objs);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("An error occurred while closing DTI."));
        form_start_proc_emerg_close(objs);
      }
      else if (ret_val > 0)
        act_log_normal(act_log_msg("DTI is closed."));
      break;
    }
    case DTIPROC_EMERG_CLOSE:
    {
      act_log_normal(act_log_msg("Checking DTI emergency closure."));
      ret_val = form_check_emerg_close_stat(objs);
      break;
    }
    default:
    {
      act_log_error(act_log_msg("Invalid DTI procedure number."));
      ret_val = -OBSNSTAT_ERR_RETRY;
      break;
    }
  }
  if (ret_val > 0)
  {
    if (objs->dti_proc != DTIPROC_EMERG_CLOSE)
        objs->dti_proc = 0;
  }
  else if (ret_val < 0)
  {
    objs->dti_proc *= -1;
    if (ret_val == -OBSNSTAT_ERR_CRIT)
      form_start_proc_emerg_close(objs);
  }
  return ret_val;
}

int form_get_obsn_msg(struct formobjects *objs, struct act_msg *msg)
{
  if ((objs == NULL) || (msg == NULL))
  {
    act_log_error(act_log_msg("Invalind input parameters."));
    return -1;
  }
  if (objs->obsn_msg == NULL)
    return 0;
  memcpy(msg, objs->obsn_msg, sizeof(struct act_msg));
  return 1;
}

void form_clear_obsn_msg(struct formobjects *objs)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  if (objs->obsn_msg == NULL)
    return;
  free(objs->obsn_msg);
  objs->obsn_msg = NULL;
}

int form_get_dti_proc(struct formobjects *objs)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return -1;
  }
  return abs(objs->dti_proc);
}

void free_form_objects(struct formobjects *objs)
{
  if (objs == NULL)
    return;

  free(objs->domeshutter);
  free(objs->dropout);
  free(objs->domemove);
  free(objs->plcmisc);
  free(objs->telfocus);
  free(objs->telmove);
  free(objs->ehtcntrl);
  free(objs->filter);
  free(objs->aperture);
  free(objs->acqmir);
  free(objs->instrshutt);

  objs->domeshutter = NULL;
  objs->dropout = NULL;
  objs->domemove = NULL;
  objs->plcmisc = NULL;
  objs->telfocus = NULL;
  objs->ehtcntrl = NULL;
  objs->filter = NULL;
  objs->aperture = NULL;
  objs->acqmir = NULL;
  objs->instrshutt = NULL;
}

void update_dshutt_dropout_sens(struct domeshutter_objects *dshutt_objs, struct dropout_objects *dropout_objs, struct plc_status *old_stat, struct plc_status *new_stat)
{
  if ((dshutt_objs == NULL) || (dropout_objs == NULL) || (new_stat == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }

  if (old_stat == NULL)
  {
    if (!DROPOUT_CLOSED(new_stat))
    {
      gtk_widget_set_sensitive(dshutt_objs->btn_open, FALSE);
      gtk_widget_set_sensitive(dshutt_objs->btn_close, FALSE);
    }
    else if (DROPOUT_CLOSED(new_stat))
    {
      gtk_widget_set_sensitive(dshutt_objs->btn_open, TRUE);
      gtk_widget_set_sensitive(dshutt_objs->btn_close, TRUE);
    }

    if (!SHUTTER_OPEN(new_stat))
    {
      gtk_widget_set_sensitive(dropout_objs->btn_open, FALSE);
      gtk_widget_set_sensitive(dropout_objs->btn_close, FALSE);
    }
    else if (SHUTTER_OPEN(new_stat))
    {
      gtk_widget_set_sensitive(dropout_objs->btn_open, TRUE);
      gtk_widget_set_sensitive(dropout_objs->btn_close, TRUE);
    }
    return;
  }

  if (!DROPOUT_CLOSED(new_stat) && DROPOUT_CLOSED(old_stat))
  {
    gtk_widget_set_sensitive(dshutt_objs->btn_open, FALSE);
    gtk_widget_set_sensitive(dshutt_objs->btn_close, FALSE);
  }
  else if (DROPOUT_CLOSED(new_stat) && !DROPOUT_CLOSED(old_stat))
  {
    gtk_widget_set_sensitive(dshutt_objs->btn_open, TRUE);
    gtk_widget_set_sensitive(dshutt_objs->btn_close, TRUE);
  }

  if (!SHUTTER_OPEN(new_stat) && SHUTTER_OPEN(old_stat))
  {
    gtk_widget_set_sensitive(dropout_objs->btn_open, FALSE);
    gtk_widget_set_sensitive(dropout_objs->btn_close, FALSE);
  }
  else if (SHUTTER_OPEN(new_stat) && !SHUTTER_OPEN(old_stat))
  {
    gtk_widget_set_sensitive(dropout_objs->btn_open, TRUE);
    gtk_widget_set_sensitive(dropout_objs->btn_close, TRUE);
  }
}

void update_handset(struct telmove_objects *objs, struct plc_status *old_stat, struct plc_status *new_stat)
{
  if ((objs == NULL) || (old_stat == NULL) || (new_stat == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  if (new_stat->handset == old_stat->handset)
    return;

  if (HS_SPEED_SLEW(new_stat) && !HS_SPEED_SLEW(old_stat))
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_speed_slew), TRUE);
  if (HS_SPEED_SET(new_stat) && !HS_SPEED_SET(old_stat))
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_speed_set), TRUE);
  if (HS_SPEED_GUIDE(new_stat) && !HS_SPEED_GUIDE(old_stat))
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_speed_guide), TRUE);
  
  if (HS_DIR_NORTH(new_stat) && HS_DIR_SOUTH(new_stat))
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_tel_stop), !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_tel_stop)));
  else if (HS_DIR_NORTH(old_stat) && HS_DIR_SOUTH(old_stat))
    /* do nothing */ ;
  else if (HS_DIR_EAST(new_stat) && HS_DIR_WEST(new_stat))
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_track), !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_track)));
  else if (HS_DIR_EAST(old_stat) && HS_DIR_WEST(old_stat))
    /* do nothing */ ;
  else if (HS_DIR_NORTH(new_stat) != HS_DIR_NORTH(old_stat))
  {
    GdkEventButton ev = {.send_event=TRUE, .time=0, .x=0.0, .y=0.0, .axes=NULL, .state=GDK_BUTTON1_MASK, .button=1, .device=NULL, .x_root=0.0, .y_root=0.0};
    ev.window = objs->btn_moveN->window;
    if (HS_DIR_NORTH(new_stat))
      ev.type = GDK_BUTTON_PRESS;
    else
      ev.type = GDK_BUTTON_RELEASE;
    gtk_widget_event(objs->btn_moveN, (GdkEvent*)(&ev));
  }
  else if (HS_DIR_SOUTH(new_stat) != HS_DIR_SOUTH(old_stat))
  {
    GdkEventButton ev = {.send_event=TRUE, .time=0, .x=0.0, .y=0.0, .axes=NULL, .state=GDK_BUTTON1_MASK, .button=1, .device=NULL, .x_root=0.0, .y_root=0.0};
    ev.window = objs->btn_moveS->window;
    if (HS_DIR_SOUTH(new_stat))
      ev.type = GDK_BUTTON_PRESS;
    else
      ev.type = GDK_BUTTON_RELEASE;
    gtk_widget_event(objs->btn_moveS, (GdkEvent*)(&ev));
  }
  else if (HS_DIR_WEST(new_stat) != HS_DIR_WEST(old_stat))
  {
    GdkEventButton ev = {.send_event=TRUE, .time=0, .x=0.0, .y=0.0, .axes=NULL, .state=GDK_BUTTON1_MASK, .button=1, .device=NULL, .x_root=0.0, .y_root=0.0};
    ev.window = objs->btn_moveW->window;
    if (HS_DIR_WEST(new_stat))
      ev.type = GDK_BUTTON_PRESS;
    else
      ev.type = GDK_BUTTON_RELEASE;
    gtk_widget_event(objs->btn_moveW, (GdkEvent*)(&ev));
  }
  else if (HS_DIR_EAST(new_stat) != HS_DIR_EAST(old_stat))
  {
    GdkEventButton ev = {.send_event=TRUE, .time=0, .x=0.0, .y=0.0, .axes=NULL, .state=GDK_BUTTON1_MASK, .button=1, .device=NULL, .x_root=0.0, .y_root=0.0};
    ev.window = objs->btn_moveE->window;
    if (HS_DIR_EAST(new_stat))
      ev.type = GDK_BUTTON_PRESS;
    else
      ev.type = GDK_BUTTON_RELEASE;
    gtk_widget_event(objs->btn_moveE, (GdkEvent*)(&ev));
  }
}

int form_check_init_stat(struct formobjects *objs)
{
  int ret_val = 0;
  switch(objs->dtiinit_stat)
  {
    case DTIINIT_START:
    {
      act_log_normal(act_log_msg("Closing down instrument for DTI initialisation."));
      objs->dtiinit_stat++;
      ret_val = set_instrshutt_open(objs->instrshutt, FALSE);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Error closing instrument shutter for DTI initialisation."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      ret_val = set_acqmir_meas(objs->acqmir, FALSE);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Error moving acquisitiong mirror for DTI initialisation."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      ret_val = set_filter(objs->filter, 0);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Error setting filter for DTI initialisation."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      ret_val = set_aperture(objs->aperture, 0);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Error setting aperture for DTI initialisation."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      ret_val = form_check_init_stat(objs);
      break;
    }
    case DTIINIT_INSTRINIT:
    {
      if ((get_instrshutt_isopen(objs->instrshutt) < 0) ||
          (get_acqmir_view(objs->acqmir) < 0) ||
          (get_filter_ready(objs->filter, 0) < 0) ||
          (get_aperture_ready(objs->aperture, 0) < 0))
      {
        act_log_error(act_log_msg("Failed to close instrument for DTI initialisation."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      if ((get_instrshutt_isopen(objs->instrshutt) == 0) &&
          (get_acqmir_view(objs->acqmir) > 0) &&
          (get_filter_ready(objs->filter, 0) == 0) &&
          (get_aperture_ready(objs->aperture, 0) == 0))
      {
        act_log_normal(act_log_msg("Instrument closed. Starting telescope initialisation (2)"));
        objs->dtiinit_stat++;
        ret_val = telmove_start_init(objs->telmove);
        if (ret_val < 0)
          act_log_error(act_log_msg("Failed to start telescope initialisation (1)."));
        else
          ret_val = form_check_init_stat(objs);
        break;
      }
      ret_val = 0;
      break;
    }
    case DTIINIT_TELPREINIT:
    {
      ret_val = telmove_get_init(objs->telmove);
      if (ret_val < 0)
      {
        act_log_normal(act_log_msg("Telescope initialisation (1) failed, most likely because the dome is not pointing due North. Moving dome to fiducial."));
        objs->dtiinit_stat++;
        ret_val = domemove_park(objs->domemove);
        if (ret_val < 0)
        {
          act_log_error(act_log_msg("Failed to move dome for telescope initialisation."));
          ret_val = -OBSNSTAT_ERR_CRIT;
          break;
        }
        ret_val = form_check_init_stat(objs);
        break;
      }
      if (ret_val > 0)
      {
        act_log_normal(act_log_msg("Telescope initialised (1)."));
        objs->dtiinit_stat += 2;
        ret_val = form_check_init_stat(objs);
      }
      break;
    }
    case DTIINIT_DOMEPREINIT:
    {
      ret_val = domemove_get_parked(objs->domemove);
      if (ret_val > 0)
      {
        act_log_normal(act_log_msg("Dome parked due North. Re-attempting telescope initialisation (2)."));
        objs->dtiinit_stat++;
        ret_val = telmove_start_init(objs->telmove);
        if (ret_val < 0)
        {
          act_log_error(act_log_msg("Failed to start telescope initialisation (2)."));
          ret_val = -OBSNSTAT_ERR_CRIT;
          break;
        }
        ret_val = form_check_init_stat(objs);
        break;
      }
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Failed to move dome for telescope initialisation."));
        ret_val = -OBSNSTAT_ERR_CRIT;
      }
      break;
    }
    case DTIINIT_TELINIT:
    {
      ret_val = telmove_get_init(objs->telmove);
      if (ret_val > 0)
      {
        act_log_normal(act_log_msg("Telescope initialised (2). Opening dome shutter."));
        objs->dtiinit_stat++;
        ret_val = domeshutter_set_open(objs->domeshutter, TRUE);
        if (ret_val < 0)
        {
          act_log_error(act_log_msg("Failed to open dome shutter."));
          ret_val = -OBSNSTAT_ERR_CRIT;
          break;
        }
        ret_val = form_check_init_stat(objs);
        break;
      }
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Failed to initialise telescope (2)."));
        ret_val = -OBSNSTAT_ERR_CRIT;
      }
      break;
    }
    case DTIINIT_DONE:
    {
      act_log_normal(act_log_msg("DTI initialised."));
      ret_val = 1;
      break;
    }
    default:
    {
      act_log_error(act_log_msg("Invalid DTI initialisation procedure stage."));
      ret_val = -OBSNSTAT_ERR_CRIT;
    }
  }
  return ret_val;
}

int form_check_targset_stat(struct formobjects *objs)
{
  if (objs->obsn_msg == NULL)
  {
    act_log_error(act_log_msg("Target set procedure is underway, but no target set message is saved. Cancelling."));
    return -OBSNSTAT_ERR_RETRY;
  }
  if (objs->obsn_msg->mtype != MT_TARG_SET)
  {
    act_log_error(act_log_msg("Target set procedure is underway, but target set message has incorrect type."));
    return -OBSNSTAT_ERR_RETRY;
  }
  int ret_val = 0;
  switch(objs->dtitargset_stat)
  {
    case DTITARGSET_START:
    {
      act_log_normal(act_log_msg("Closing down instrument for target set."));
      objs->dtitargset_stat++;
      ret_val = set_instrshutt_open(objs->instrshutt, FALSE);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Error closing instrument shutter for target set."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      ret_val = set_acqmir_meas(objs->acqmir, FALSE);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Error moving acquisitiong mirror for target set."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      ret_val = form_check_targset_stat(objs);
      break;
    }
    case DTITARGSET_INSTRACQ:
    {
      if ((get_instrshutt_isopen(objs->instrshutt) < 0) ||
          (get_acqmir_view(objs->acqmir) < 0))
      {
        act_log_error(act_log_msg("Failed to close instrument for target set."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      if ((get_instrshutt_isopen(objs->instrshutt) == 0) &&
          (get_acqmir_view(objs->acqmir) > 0))
      {
        act_log_normal(act_log_msg("Instrument closed. Opening Dome Shutter."));
        objs->dtitargset_stat++;
        if (objs->weath_ok == 0)
        {
          act_log_error(act_log_msg("Weather warning has been issued. Cannot open dome shutter."));
          ret_val = -OBSNSTAT_ERR_WAIT;
          break;
        }
        ret_val = domeshutter_set_open(objs->domeshutter,TRUE);
        if (ret_val < 0)
        {
          act_log_error(act_log_msg("Failed to open dome shutter."));
          ret_val = -OBSNSTAT_ERR_CRIT;
          break;
        }
        ret_val = form_check_targset_stat(objs);
        break;
      }
      ret_val = 0;
      break;
    }
    case DTITARGSET_DSHUTTOPEN:
    {
      ret_val = domeshutter_get_isopen(objs->domeshutter);
      if (ret_val > 0)
      {
        act_log_normal(act_log_msg("Dome shutter open. Opening dome dropout."));
        objs->dtitargset_stat++;
        ret_val = dropout_set_open(objs->dropout, TRUE);
        if (ret_val < 0)
        {
          act_log_error(act_log_msg("Failed to open dome dropout."));
          ret_val = -OBSNSTAT_ERR_CRIT;
          break;
        }
        ret_val = form_check_targset_stat(objs);
        break;
      }
      if (ret_val < 0)
        act_log_error(act_log_msg("Failed to open dome shutter."));
      break;
    }
    case DTITARGSET_DRPOUTOPEN:
    {
      ret_val = dropout_get_isopen(objs->dropout);
      if (ret_val > 0)
      {
        act_log_normal(act_log_msg("Dome dropout is open. Moving telescope to target."));
        objs->dtitargset_stat++;
        ret_val = domemove_set_auto_on(objs->domemove,FALSE);
        if (ret_val < 0)
        {
          act_log_error(act_log_msg("Failed to disable auto dome tracking before moving telescope."));
          ret_val = -OBSNSTAT_ERR_CRIT;
          break;
        }
        ret_val = form_start_tel_goto(objs,&objs->obsn_msg->content.msg_targset);
        if (ret_val < 0)
        {
          act_log_error(act_log_msg("Failed to move telescope to target."));
          break;
        }
        ret_val = form_check_targset_stat(objs);
        break;
      }
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Failed to open dome dropout."));
        ret_val = -OBSNSTAT_ERR_CRIT;
      }
      break;
    }
    case DTITARGSET_MOVETEL:
    {
      ret_val = telmove_get_goto_done(objs->telmove, &objs->obsn_msg->content.msg_targset.targ_ra, &objs->obsn_msg->content.msg_targset.targ_dec);
      if (ret_val > 0)
      {
        act_log_normal(act_log_msg("Telescope is at target. Moving dome to align with telescope."));
        objs->dtitargset_stat++;
        ret_val = domemove_set_auto_on(objs->domemove,TRUE);
        if (ret_val < 0)
        {
          act_log_error(act_log_msg("Failed to activate auto dome tracking."));
          ret_val = -OBSNSTAT_ERR_CRIT;
          break;
        }
        ret_val = form_check_targset_stat(objs);
        break;
      }
      if (ret_val < 0)
      {
        act_log_normal(act_log_msg("Failed to move telescope to target."));
        ret_val = -OBSNSTAT_ERR_CRIT;
      }
      break;
    }
    case DTITARGSET_MOVEDOME:
    {
      ret_val = domemove_get_aligned(objs->domemove);
      if (ret_val > 0)
      {
        act_log_normal(act_log_msg("Dome is aligned with telescope."));
        objs->dtitargset_stat++;
        ret_val = form_check_targset_stat(objs);
        break;
      }
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Failed to align dome with telescope."));
        ret_val = -OBSNSTAT_ERR_CRIT;
      }
      break;
    }
    case DTITARGSET_DONE:
    {
      act_log_normal(act_log_msg("Target is set."));
      ret_val = 1;
      break;
    }
    default:
    {
      act_log_error(act_log_msg("Invalid target set procedure stage."));
      ret_val = -OBSNSTAT_ERR_RETRY;
    }
  }
  return ret_val;
}

int form_start_tel_goto(struct formobjects *objs, struct act_msg_targset *targset_msg)
{
  if (objs->last_sidt_msec < 0)
  {
    act_log_error(act_log_msg("GoTo requested, but no up-to-date sidereal time is available."));
    return -OBSNSTAT_ERR_WAIT;
  }
  
  struct rastruct tel_ra;
  struct hastruct tel_ha;
  struct decstruct tel_dec;
  memcpy(&tel_ra, &targset_msg->targ_ra, sizeof(struct rastruct));
  memcpy(&tel_dec, &targset_msg->targ_dec, sizeof(struct decstruct));
  
  convert_MS_HMSMS_ra(convert_HMSMS_MS_ra(&tel_ra) + convert_HMSMS_MS_ra(&targset_msg->adj_ra), &tel_ra);
  convert_ASEC_DMS_dec(convert_DMS_ASEC_dec(&tel_dec) + convert_DMS_ASEC_dec(&targset_msg->adj_dec), &tel_dec);
  
  struct timeval cur_time;
  gettimeofday(&cur_time, NULL);
  long cur_time_msec = (cur_time.tv_sec%86400)*1000 + cur_time.tv_usec/1000;
  struct timestruct tmp_sidt;
  convert_MS_HMSMS_time(objs->sidt_msec + cur_time_msec - objs->last_sidt_msec, &tmp_sidt);
  calc_HAngle(&tel_ra, &tmp_sidt, &tel_ha);
  if ((convert_HMSMS_H_ha(&tel_ha) > SOFT_LIM_W_H) ||
      (convert_HMSMS_H_ha(&tel_ha) < SOFT_LIM_E_H) ||
      (convert_DMS_D_dec(&tel_dec) > SOFT_LIM_N_D) ||
      (convert_DMS_D_dec(&tel_dec) < SOFT_LIM_S_D))
  {
    act_log_error(act_log_msg("Requested coordinates fall beyond the soft limits."));
    return -OBSNSTAT_ERR_NEXT;
  }
  struct altstruct tel_alt;
  convert_EQUI_ALTAZ(&tel_ha, &tel_dec, &tel_alt, NULL);
  if (convert_DMS_D_alt(&tel_alt) < SOFT_LIM_ALT_D)
  {
    act_log_error(act_log_msg("Requested coordinates fall below safe altitude limit."));
    return -OBSNSTAT_ERR_NEXT;
  }
  if (telmove_start_goto(objs->telmove, &tel_ha, &tel_dec) < 0)
  {
    act_log_error(act_log_msg("An error occurred while attempting to start a telescope Goto."));
    return -OBSNSTAT_ERR_RETRY;
  }
  return 0;
}

int form_check_instrprep_stat(struct formobjects *objs)
{
  if (objs->obsn_msg == NULL)
  {
    act_log_error(act_log_msg("Target set procedure is underway, but no target set message is saved. Cancelling."));
    return -OBSNSTAT_ERR_RETRY;
  }
  int ret_val = 0;
  switch (objs->obsn_msg->mtype)
  {
    case MT_DATA_PMT:
    {
      ret_val = form_check_instrprep_pmt_stat(objs);
      break;
    }
    case MT_DATA_CCD:
    {
      ret_val = form_check_instrprep_ccd_stat(objs);
      break;
    }
    default:
    {
      act_log_error(act_log_msg("Invalid input message type for instrument preparation procedure."));
      ret_val = -OBSNSTAT_ERR_RETRY;
    }
  }
  return ret_val;
}

int form_check_instrprep_pmt_stat(struct formobjects *objs)
{
  if (objs->obsn_msg->mtype != MT_DATA_PMT)
  {
    act_log_error(act_log_msg("PMT data collection preparation procedure is underway, but target set message has incorrect type."));
    return -OBSNSTAT_ERR_RETRY;
  }
  int ret_val = 0;
  switch (objs->dtiinstrprep_stat)
  {
    case DTIINSTRPREP_START:
    {
      act_log_normal(act_log_msg("Preparing for data collection with PMT."));
      objs->dtiinstrprep_stat++;
      ret_val = set_eht_on(objs->ehtcntrl, TRUE);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Error activating EHT. Cannot collect data with PMT."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      if (ret_val == 0)
      {
        act_log_error(act_log_msg("EHT was disabled. Need to wait a while before observation can start."));
        ret_val = -OBSNSTAT_ERR_WAIT;
        break;
      }
      ret_val = set_instrshutt_open(objs->instrshutt, TRUE);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Error opening instrument shutter for data collection with PMT."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      ret_val = set_acqmir_meas(objs->acqmir, TRUE);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Error moving acquisitiong mirror for data collection with PMT."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      ret_val = set_filter(objs->filter, objs->obsn_msg->content.msg_datapmt.filter.slot);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Error setting filter for data collection with PMT."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      ret_val = set_aperture(objs->aperture, objs->obsn_msg->content.msg_datapmt.aperture.slot);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Error setting aperture for data collection with PMT."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      ret_val = form_check_instrprep_pmt_stat(objs);
      break;
    }
    case DTIINSTRPREP_BUSY:
    {
      if ((get_instrshutt_isopen(objs->instrshutt) < 0) ||
          (get_acqmir_meas(objs->acqmir) < 0) ||
          (get_filter_ready(objs->filter, objs->obsn_msg->content.msg_datapmt.filter.slot) < 0) ||
          (get_aperture_ready(objs->aperture, objs->obsn_msg->content.msg_datapmt.aperture.slot) < 0) ||
          (get_eht_high(objs->ehtcntrl) < 0))
      {
        act_log_error(act_log_msg("Failed to set instrument for PMT data collection preparation."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      if ((get_instrshutt_isopen(objs->instrshutt) > 0) &&
          (get_acqmir_meas(objs->acqmir) > 0) &&
          (get_filter_ready(objs->filter, objs->obsn_msg->content.msg_datapmt.filter.slot) > 0) &&
          (get_aperture_ready(objs->aperture, objs->obsn_msg->content.msg_datapmt.aperture.slot) > 0))
      {
        act_log_normal(act_log_msg("Instrument shutter, acquisition mirror, filter, aperture and EHT are set for data collection with PMT."));
        objs->dtiinstrprep_stat++;
        ret_val = form_check_instrprep_pmt_stat(objs);
        break;
      }
      ret_val = 0;
      break;
    }
    case DTIINSTRPREP_DONE:
    {
      act_log_normal(act_log_msg("Instrument is ready for PMT data acquisition"));
      ret_val = 1;
      break;
    }
    default:
    {
      act_log_error(act_log_msg("Invalid instrument preparation procedure stage (DATA_PMT)."));
      ret_val = -OBSNSTAT_ERR_RETRY;
    }
  }
  return ret_val;
}

int form_check_instrprep_ccd_stat(struct formobjects *objs)
{
  if (objs->obsn_msg->mtype != MT_DATA_CCD)
  {
    act_log_error(act_log_msg("CCD data collection preparation procedure is underway, but target set message has incorrect type."));
    return -OBSNSTAT_ERR_RETRY;
  }
  int ret_val = 0;
  switch (objs->dtiinstrprep_stat)
  {
    case DTIINSTRPREP_START:
    {
      act_log_normal(act_log_msg("Preparing for data collection with CCD."));
      objs->dtiinstrprep_stat++;
      ret_val = set_instrshutt_open(objs->instrshutt, FALSE);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Error closing instrument shutter for data collection with CCD."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      ret_val = set_acqmir_meas(objs->acqmir, FALSE);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Error moving acquisitiong mirror for data collection with CCD."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      ret_val = form_check_instrprep_ccd_stat(objs);
      break;
    }
    case DTIINSTRPREP_BUSY:
    {
      if ((get_instrshutt_isopen(objs->instrshutt) < 0) ||
          (get_acqmir_view(objs->acqmir) < 0))
      {
        act_log_error(act_log_msg("Failed to set instrument for data collection with CCD."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      if ((get_instrshutt_isopen(objs->instrshutt) == 0) &&
          (get_acqmir_view(objs->acqmir) > 0))
      {
        act_log_normal(act_log_msg("Instrument shutter and acquisition mirror are set for data collection with CCD."));
        objs->dtiinstrprep_stat++;
        ret_val = form_check_instrprep_ccd_stat(objs);
        break;
      }
      ret_val = 0;
      break;
    }
    default:
    {
      act_log_error(act_log_msg("Invalid instrument preparation procedure stage (DATA_CCD)."));
      ret_val = -OBSNSTAT_ERR_RETRY;
    }
  }
  return ret_val;
}

int form_check_instrfin_stat(struct formobjects *objs)
{
  int ret_val = 0;
  switch (objs->dtiinstrfin_stat)
  {
    case DTIINSTRFIN_START:
    {
      act_log_normal(act_log_msg("Closing instrument after data collection."));
      objs->dtiinstrfin_stat++;
      set_telmove_tracking(objs->telmove, FALSE);
      ret_val = set_instrshutt_open(objs->instrshutt, FALSE);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Error closing instrument shutter after data collection."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      ret_val = set_acqmir_meas(objs->acqmir, FALSE);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Error moving acquisitiong mirror after data collection."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      ret_val = form_check_instrfin_stat(objs);
      break;
    }
    case DTIINSTRFIN_BUSY:
    {
      if ((get_instrshutt_isopen(objs->instrshutt) < 0) ||
          (get_acqmir_view(objs->acqmir) < 0))
      {
        act_log_error(act_log_msg("Failed to close instrument after data collection."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      if ((get_instrshutt_isopen(objs->instrshutt) == 0) &&
          (get_acqmir_view(objs->acqmir) == 0))
      {
        act_log_normal(act_log_msg("PMT shutter closed and acquisition mirror in VIEW position."));
        objs->dtiinstrfin_stat++;
        ret_val = form_check_instrfin_stat(objs);
        break;
      }
      ret_val = 0;
      break;
    }
    case DTIINSTRFIN_DONE:
    {
      act_log_normal(act_log_msg("Instrument is closed following data collection."));
      ret_val = 1;
      break;
    }
  }
  return ret_val;
}

int form_check_close_stat(struct formobjects *objs)
{
  int ret_val = 0;
  switch (objs->dticlose_stat)
  {
    case DTICLOSE_START:
    {
      act_log_normal(act_log_msg("Closing down instrument for DTI closure."));
      objs->dticlose_stat++;
      ret_val = set_instrshutt_open(objs->instrshutt, FALSE);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Error closing instrument shutter for DTI closure."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      ret_val = set_acqmir_meas(objs->acqmir, FALSE);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Error moving acquisitiong mirror for DTI closure."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      ret_val = form_check_close_stat(objs);
      break;
    }
    case DTICLOSE_INSTRCLOSE:
    {
      if ((get_instrshutt_isopen(objs->instrshutt) < 0) ||
          (get_acqmir_view(objs->acqmir) < 0))
      {
        act_log_error(act_log_msg("Failed to close instrument for DTI closure."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      if ((get_instrshutt_isopen(objs->instrshutt) == 0) &&
          (get_acqmir_view(objs->acqmir) > 0))
      {
        act_log_normal(act_log_msg("Instrument closed. Opening dome dropout."));
        objs->dticlose_stat++;
        ret_val = domemove_set_auto_on(objs->domemove,FALSE);
        if (ret_val < 0)
        {
          act_log_error(act_log_msg("Failed to disable auto dome tracking for DTI closure."));
          ret_val = -OBSNSTAT_ERR_CRIT;
          break;
        }
        ret_val = domeshutter_set_open(objs->domeshutter,FALSE);
        if (ret_val < 0)
        {
          act_log_error(act_log_msg("Failed to close dome dropout for DTI closure."));
          ret_val = -OBSNSTAT_ERR_CRIT;
          break;
        }
        ret_val = form_check_close_stat(objs);
        break;
      }
      ret_val = 0;
      break;
    }
    case DTICLOSE_DRPOUTCLOSE:
    {
      ret_val = dropout_get_isclosed(objs->dropout);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Failed to close dome dropout."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      if (ret_val == 0)
      {
        act_log_normal(act_log_msg("Dome dropout is closed. Closing dome shutter."));
        objs->dticlose_stat++;
        ret_val = domeshutter_set_open(objs->domeshutter,FALSE);
        if (ret_val < 0)
        {
          act_log_error(act_log_msg("Failed to close dome shutter for DTI closure."));
          ret_val = -OBSNSTAT_ERR_CRIT;
          break;
        }
        ret_val = form_check_close_stat(objs);
        break;
      }
      ret_val = 0;
      break;
    }
    case DTICLOSE_DSHUTTCLOSE:
    {
      ret_val = domeshutter_get_isclosed(objs->domeshutter);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Failed to close dome shutter."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      if (ret_val == 0)
      {
        act_log_normal(act_log_msg("Dome shutter is closed. Parking telescope."));
        objs->dticlose_stat++;
        ret_val = telmove_park(objs->telmove);
        if (ret_val < 0)
        {
          act_log_error(act_log_msg("Failed to park telescope for DTI closure."));
          ret_val = -OBSNSTAT_ERR_CRIT;
          break;
        }
        ret_val = form_check_close_stat(objs);
        break;
      }
      ret_val = 0;
      break;
    }
    case DTICLOSE_TELPARK:
    {
      ret_val = telmove_get_parked(objs->telmove);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Failed to park telescope."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      if (ret_val > 0)
      {
        act_log_normal(act_log_msg("Telescope parked. Parking dome."));
        objs->dticlose_stat++;
        ret_val = domemove_park(objs->domemove);
        if (ret_val < 0)
        {
          act_log_error(act_log_msg("Failed to park dome."));
          ret_val = -OBSNSTAT_ERR_CRIT;
          break;
        }
        ret_val = form_check_close_stat(objs);
        break;
      }
      break;
    }
    case DTICLOSE_DOMEPARK:
    {
      ret_val = domemove_get_parked(objs->domemove);
      if (ret_val < 0)
      {
        act_log_error(act_log_msg("Failed to park dome."));
        ret_val = -OBSNSTAT_ERR_CRIT;
        break;
      }
      if (ret_val > 0)
      {
        act_log_normal(act_log_msg("Dome parked."));
        objs->dticlose_stat++;
        ret_val = form_check_close_stat(objs);
        break;
      }
      break;
    }
    case DTICLOSE_DONE:
    {
      act_log_normal(act_log_msg("DTI closed."));
      ret_val = 1;
      break;
    }
    default:
    {
      act_log_error(act_log_msg("Invalid DTI closure procedure stage."));
      ret_val = -OBSNSTAT_ERR_RETRY;
    }
  }
  return ret_val;
}

int form_check_emerg_close_stat(struct formobjects *objs)
{
  int instrshutt_stat = get_instrshutt_isopen(objs->instrshutt);
  if (instrshutt_stat > 0)
    set_instrshutt_open(objs->instrshutt, FALSE);
  else if (instrshutt_stat < 0)
  {
    act_log_error(act_log_msg("Error reading instrument shutter status for emergency DTI closure."));
    set_instrshutt_open(objs->instrshutt, FALSE);
  }
  else
    act_log_normal(act_log_msg("Instrument shutter is closed for emergency DTI closure."));
  int domeshutter_stat = domeshutter_get_isclosed(objs->domeshutter);
  if (domeshutter_stat == 0)
    domeshutter_set_open(objs->domeshutter, FALSE);
  else if (domeshutter_stat < 0)
  {
    act_log_error(act_log_msg("Error reading domeshutter status for emergency DTI closure."));
    domeshutter_set_open(objs->domeshutter, FALSE);
  }
  else
    act_log_normal(act_log_msg("Dome shutter is closed for emergency DTI closure."));
  int dropout_stat = dropout_get_isclosed(objs->dropout);
  if (dropout_stat == 0)
    dropout_set_open(objs->dropout, FALSE);
  else if (dropout_stat < 0)
  {
    act_log_error(act_log_msg("Error reading dropout status for emergency DTI closure."));
    dropout_set_open(objs->dropout, FALSE);
  }
  else
    act_log_normal(act_log_msg("Dome dropout is closed for DTI closure."));
  if ((instrshutt_stat != 0) ||
      (domeshutter_stat <= 0) ||
      (dropout_stat <= 0))
    return 0;
  return 1;
}
