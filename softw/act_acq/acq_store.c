#include <gtk/gtk.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <act_plc.h>
#include <act_log.h>
#include <act_ipc.h>
#include "acq_store.h"

/*
#define DOMESHUTTER_FAIL_TIME_S   300
#define DOMESHUTTER_ENV_TIME_S    600

static void domeshutter_class_init (DomeshutterClass *klass);
static void domeshutter_init(GtkWidget *domeshutter);
static void domeshutter_destroy(gpointer domeshutter);
static void buttons_toggled(gpointer domeshutter);
static void buttons_nand(GtkWidget *button1, gpointer button2);
static void block_button_toggles(Domeshutter *objs);
static void unblock_button_toggles(Domeshutter *objs);
static guchar process_quit(Domeshutter *objs, struct act_msg_quit *msg_quit);
static guchar process_environ(Domeshutter *objs, struct act_msg_environ *msg_environ);
static guchar process_targset(Domeshutter *objs, struct act_msg_targset *msg_targset);
static gboolean fail_timeout(gpointer domeshutter);
static gboolean env_timeout(gpointer domeshutter);
static guchar environ_change(Domeshutter *objs, gboolean weath_ok, gboolean sun_alt_ok);
static void process_complete(Domeshutter *objs, guchar status);
static void send_start_open(Domeshutter *objs);
static void send_start_close(Domeshutter *objs);
static void send_stop(Domeshutter *objs);
*/

#define STAT_STORING      0x01
#define STAT_ERR_RETRY    0x02
#define STAT_ERR_NO_RECOV 0x04

enum
{
  STATUS_UPDATE,
  LAST_SIGNAL
};

static guint acq_store_signals[LAST_SIGNAL] = { 0 };


static void acq_store_instance_init(GObject *acq_store);
static void acq_store_class_init(CcdImgClass *klass);
static void acq_store_instance_dispose(GObject *acq_store);


GType acq_store_get_type (void)
{
  static GType acq_store_type = 0;
  
  if (!acq_store_type)
  {
    const GTypeInfo acq_store_info =
    {
      sizeof (AcqStoreClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) acq_store_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (AcqStore),
      0,
      (GInstanceInitFunc) acq_store_init,
      NULL
    };
    
    acq_store_type = g_type_register_static (G_TYPE_OBJECT, "AcqStore", &acq_store_info, 0);
  }
  
  return acq_store_type;
}

AcqStore *acq_store_new(gchar const *sqlhost)
{
  // check connections
  act_log_debug(act_log_msg("Creating MySQL image storage connection."));
  MYSQL *store_conn;
  store_conn = mysql_init(NULL);
  if (store_conn == NULL)
  {
    act_log_error(act_log_msg("Error initialising MySQL image storage connection handler - %s.", mysql_error(store_conn)));
    return NULL;
  }
  if (mysql_real_connect(store_conn, sqlhost, "act_acq", NULL, "actnew", 0, NULL, 0) == NULL)
  {
    act_log_error(act_log_msg("Error establishing image storage connecting to MySQL database - %s.", mysql_error(store_conn)));
    mysql_close(store_conn);
    return NULL;
  }
  
  act_log_debug(act_log_msg("Creating general MySQL connection."));
  MYSQL *genl_conn;
  genl_conn = mysql_init(NULL);
  if (genl_conn == NULL)
  {
    act_log_error(act_log_msg("Error initialising general MySQL connection handler - %s.", mysql_error(store_conn)));
    mysql_close(store_conn);
    return NULL;
  }
  if (mysql_real_connect(genl_conn, sqlhost, "act_acq", NULL, "actnew", 0, NULL, 0) == NULL)
  {
    act_log_error(act_log_msg("Error establishing image storage connecting to MySQL database - %s.", mysql_error(genl_conn)));
    mysql_close(store_conn);
    mysql_close(genl_conn);
    return NULL;
  }
  
  // create object
  AcqStore *objs = g_object_new (acq_store_get_type(), NULL);
  objs->sqlhost = sqlhost;
  objs->store_conn = store_conn;
  objs->genl_conn = genl_conn;
  pthread_mutex_init (&objs->img_list_mutex, NULL);
  return objs; 
}

/** \brief Search database for given target name pattern
 * \param objs AcqStore object, must have been initialised
 * \param targ_name_pat Target name pattern to search for - SQL rules regarding search strings (with the 'LIKE' key word) apply
 * \return Internal database target identifier on success, <0 if an error occurred and ==0 if no match was found.
 */
glong acq_store_search_targ_name(AcqStore *objs, gchar const *targ_name_pat)
{
  if (objs->genl_conn == NULL)
  {
    act_log_error(act_log_msg("MySQL connection not available."));
    return -1;
  }
  gchar qrystr[256];
  sprintf(qrystr, "SELECT star_id FROM star_names WHERE star_name LIKE \"%s\";", targ_name_pat);
  MYSQL_RES *result;
  MYSQL_ROW row;
  mysql_query(objs->genl_conn,qrystr);
  result = mysql_store_result(objs->genl_conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve internal database identifier for target names matching \"%s\" - %s.", targ_name_pat, mysql_error(objs->formobjs->mysql_conn)));
    return -1;
  }
  
  int rowcount = mysql_num_rows(result);
  if ((rowcount < 0) || (mysql_num_fields(result) != 1))
  {
    act_log_error(act_log_msg("Could not retrieve internal database identifier for targets names matching \"%s\" - Invalid number of rows/columns returned (%d rows, %d columns).", targ_name_pat, rowcount, mysql_num_fields(result)));
    mysql_free_result(result);
    return -1;
  }
  if (rowcount == 0)
  {
    act_log_debug(act_log_msg("No target found matching search pattern \"%s\"", targ_name_pat));
    return 0;
  }
  if (rowcount > 1)
    act_log_debug(act_log_msg("Multiple targets found matching search pattern \"%s\". Choosing first returned result.", targ_name_pat));
  
  row = mysql_fetch_row(result);
  glong tmp_targ_id;
  if (sscanf(row[0], "%d", &tmp_targ_id) != 1)
  {
    act_log_error(act_log_msg("Error parsing internal database target identifier (%s).", row[0]));
    return -1;
  }
  mysql_free_result(result);
  return tmp_targ_id;
}

/** \brief Search database for name of target matching given DB ID
 * \param objs AcqStore object, must have been initialised
 * \param targ_id Internal database target identifier
 * \return Name of target matching given identifier, or NULL in case of failure
 */
gchar *acq_store_get_targ_name(AcqStore *objs, gulong targ_id)
{
  if (objs->genl_conn == NULL)
  {
    act_log_error(act_log_msg("MySQL connection not available."));
    return NULL;
  }
  gchar qrystr[256];
  sprintf(qrystr, "SELECT star_name FROM star_prim_names WHERE star_id=%lu;", targ_id);
  MYSQL_RES *result;
  MYSQL_ROW row;
  mysql_query(objs->genl_conn,qrystr);
  result = mysql_store_result(objs->genl_conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve name for target with internal database identifier %lu - %s.", targ_id, mysql_error(objs->formobjs->mysql_conn)));
    return NULL;
  }
  
  int rowcount = mysql_num_rows(result);
  if ((rowcount < 0) || (mysql_num_fields(result) != 1))
  {
    act_log_error(act_log_msg("Could not retrieve name for target with internal database identifier %lu - Invalid number of rows/columns returned (%d rows, %d columns).", targ_id, rowcount, mysql_num_fields(result)));
    mysql_free_result(result);
    return NULL;
  }
  if (rowcount == 0)
  {
    act_log_debug(act_log_msg("No target found with idenfitier \"%lu\"", targ_id));
    return NULL;
  }
  
  row = mysql_fetch_row(result);
  gchar *ret = malloc(strlen(row[0]+1));
  sprintf(ret, "%s", row[0]);
  mysql_free_result(result);
  return ret;
}

guint acq_store_get_filt_list(AcqStore *objs, struct filtaper **filters)
{
}

void acq_store_append_image(AcqStore *objs, AcqImg *new_img)
{
}

gboolean acq_store_idle(AcqStore *objs)
{
  return objs->status == 0;
}

gboolean acq_store_storing(AcqStore *objs)
{
  return (objs->status & STAT_STORING) > 0;
}

gboolean acq_store_error_retry(AcqStore *objs)
{
  return (objs->status & STAT_ERR_RETRY) > 0;
}

gboolean acq_store_error_no_recov(AcqStore *objs)
{
  return (objs->status & STAT_ERR_NO_RECOV) > 0;
}

static void acq_store_instance_init(GObject *acq_store)
{
  AcqStore *objs = ACQ_STORE(acq_store);
  objs->status = 0;
  objs->sqlhost = NULL;
  objs->store_conn = NULL;
  objs->genl_conn = NULL;
  objs->img_pend = NULL;
  objs->img_list_mutex = NULL;
}

static void acq_store_class_init(CcdImgClass *klass)
{
  acq_store_signals[STATUS_UPDATE] = g_signal_new("store-status-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  G_OBJECT_CLASS(klass)->dispose = acq_store_instance_dispose;
}

static void acq_store_instance_dispose(GObject *acq_store)
{
  AcqStore *objs = ACQ_STORE(acq_store);
  if ((objs->status & STAT_STORING) > 0)
  {
    gint ret = pthread_join(objs->store_thr, NULL);
    if (ret != 0)
    {
      act_log_error(act_log_msg("Failed to join storage thread - %s", strerror(ret)));
      objs->status &= ~STAT_STORING;
    }
  }
  if (objs->sqlhost != NULL)
  {
    g_free(objs->sqlhost);
    objs->sqlhost = NULL;
  }
  if (objs->store_conn != NULL)
  {
    if ((objs->status & STAT_STORING) > 0)
      act_log_error(act_log_msg("Need to close MySQL store connection, but images are still being store. Not closing connection."));
    else
    {
      mysql_close(objs->store_conn);
      objs->store_conn = NULL;
    }
  }
  if (objs->genl_conn != NULL)
  {
    mysql_close(objs->genl_conn);
    objs->genl_conn = NULL;
  }
  if (objs->img_pend != NULL)
    act_log_error(act_log_msg("There are images waiting to be stored."));
  if (objs->img_list_mutex != NULL)
  {
    pthread_mutex_destroy(objs->img_list_mutex);
    objs->img_list_mutex = NULL;
  }
}





GType domeshutter_get_type (void)
{
  static GType domeshutter_type = 0;
  
  if (!domeshutter_type)
  {
    const GTypeInfo domeshutter_info =
    {
      sizeof (DomeshutterClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) domeshutter_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (Domeshutter),
      0,
      (GInstanceInitFunc) domeshutter_init,
      NULL
    };
    
    domeshutter_type = g_type_register_static (GTK_TYPE_FRAME, "Domeshutter", &domeshutter_info, 0);
  }
  
  return domeshutter_type;
}

GtkWidget *domeshutter_new (guchar dshutt_stat)
{
  GtkWidget *domeshutter = g_object_new (domeshutter_get_type (), NULL);
  Domeshutter *objs = DOMESHUTTER(domeshutter);
  objs->weath_ok = objs->sun_alt_ok = FALSE;
  objs->dshutt_cur = 0;
//   objs->dshutt_cur = ~dshutt_stat;
//   domeshutter_update (domeshutter, dshutt_stat);
  // Button should actually be insensitive at start, in case of !weath_ok or !sun_alt_ok, but if it'll mess things up if it's already open
  if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_open)))
    gtk_widget_set_sensitive(objs->btn_open, FALSE);
  
  g_signal_connect_swapped(G_OBJECT(domeshutter), "destroy", G_CALLBACK(domeshutter_destroy), domeshutter);
  g_signal_connect(G_OBJECT(objs->btn_open), "toggled", G_CALLBACK(buttons_nand), objs->btn_close);
  g_signal_connect(G_OBJECT(objs->btn_close), "toggled", G_CALLBACK(buttons_nand), objs->btn_open);
  g_signal_connect_swapped(G_OBJECT(objs->btn_open), "toggled", G_CALLBACK(buttons_toggled), domeshutter);
  g_signal_connect_swapped(G_OBJECT(objs->btn_close), "toggled", G_CALLBACK(buttons_toggled), domeshutter);
  
  return domeshutter;
}

void domeshutter_update (GtkWidget *domeshutter, guchar new_dshutt_stat)
{
  Domeshutter *objs = DOMESHUTTER(domeshutter);
  if (objs->dshutt_cur == new_dshutt_stat)
    return;
  
  GdkColor new_col;
  block_button_toggles(objs);
  if (new_dshutt_stat & DSHUTT_MOVING_MASK)
  {
    gdk_color_parse("#AAAA00", &new_col);
    gtk_widget_modify_bg(objs->btn_open, GTK_STATE_ACTIVE, &new_col);
    gtk_widget_modify_bg(objs->btn_open, GTK_STATE_INSENSITIVE, &new_col);
    gtk_widget_modify_bg(objs->btn_close, GTK_STATE_ACTIVE, &new_col);
    gtk_widget_modify_bg(objs->btn_close, GTK_STATE_INSENSITIVE, &new_col);
  }
  else if (new_dshutt_stat & DSHUTT_OPEN_MASK)
  {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_open), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_close), FALSE);
    gdk_color_parse("#00AA00", &new_col);
    gtk_widget_modify_bg(objs->btn_open, GTK_STATE_ACTIVE, &new_col);
    gtk_widget_modify_bg(objs->btn_open, GTK_STATE_INSENSITIVE, &new_col);
    gtk_widget_modify_bg(objs->btn_close, GTK_STATE_ACTIVE, NULL);
    gtk_widget_modify_bg(objs->btn_close, GTK_STATE_INSENSITIVE, NULL);
  }
  else if (new_dshutt_stat & DSHUTT_CLOSED_MASK)
  {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_open), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_close), TRUE);
    gdk_color_parse("#00AA00", &new_col);
    gtk_widget_modify_bg(objs->btn_open, GTK_STATE_ACTIVE, NULL);
    gtk_widget_modify_bg(objs->btn_open, GTK_STATE_INSENSITIVE, NULL);
    gtk_widget_modify_bg(objs->btn_close, GTK_STATE_ACTIVE, &new_col);
    gtk_widget_modify_bg(objs->btn_close, GTK_STATE_INSENSITIVE, &new_col);
  }
  else
  {
    gtk_widget_modify_bg(objs->btn_open, GTK_STATE_ACTIVE, NULL);
    gtk_widget_modify_bg(objs->btn_open, GTK_STATE_INSENSITIVE, NULL);
    gtk_widget_modify_bg(objs->btn_close, GTK_STATE_ACTIVE, NULL);
    gtk_widget_modify_bg(objs->btn_close, GTK_STATE_INSENSITIVE, NULL);
  }

  if (((new_dshutt_stat & DSHUTT_OPEN_MASK) != 0) && ((objs->dshutt_cur & DSHUTT_OPEN_MASK) == 0))
    g_signal_emit(domeshutter, domeshutter_signals[IS_OPEN_SIGNAL], 0, TRUE);
  else if (((new_dshutt_stat & DSHUTT_OPEN_MASK) == 0) && (((objs->dshutt_cur & DSHUTT_OPEN_MASK) != 0) || (objs->dshutt_cur==0)))
    g_signal_emit(domeshutter, domeshutter_signals[IS_OPEN_SIGNAL], 0, FALSE);

  if (new_dshutt_stat == objs->dshutt_goal)
  {
    if (objs->fail_to_id > 0)
    {
      g_source_remove(objs->fail_to_id);
      objs->fail_to_id = 0;
    }
    process_complete(objs, OBSNSTAT_GOOD);
  }
  objs->dshutt_cur = new_dshutt_stat;
  
  unblock_button_toggles(objs);
}

void domeshutter_process_msg (GtkWidget *domeshutter, DtiMsg *msg)
{
  guchar ret = OBSNSTAT_GOOD;
  Domeshutter *objs = DOMESHUTTER(domeshutter);
  switch(dti_msg_get_mtype(msg))
  {
    case MT_QUIT:
      ret = process_quit(objs, dti_msg_get_quit(msg));
      break;
    case MT_ENVIRON:
      ret = process_environ(objs, dti_msg_get_environ(msg));
      break;
    case MT_TARG_SET:
      ret = process_targset(objs, dti_msg_get_targset(msg));
      break;
  }
  if (ret != 0)
  {
    g_signal_emit(domeshutter, domeshutter_signals[PROC_COMPLETE_SIGNAL], 0, ret, msg);
    return;
  }
  g_object_ref(msg);
  objs->pending_msg = msg;
}

void domeshutter_set_lock (GtkWidget *domeshutter, gboolean lock_on)
{
  Domeshutter *objs = DOMESHUTTER(domeshutter);
  act_log_debug(act_log_msg("Setting lock %hhu (%hhu %hhu)", lock_on, objs->weath_ok, objs->sun_alt_ok));
  objs->locked = lock_on;
  gtk_widget_set_sensitive(objs->btn_open, (!lock_on) && objs->weath_ok && objs->sun_alt_ok);
  gtk_widget_set_sensitive(objs->btn_close, !lock_on);
}

static void domeshutter_class_init (DomeshutterClass *klass)
{
  domeshutter_signals[IS_OPEN_SIGNAL] = g_signal_new("is-open", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  domeshutter_signals[SEND_START_OPEN_SIGNAL] = g_signal_new("start-open", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  domeshutter_signals[SEND_START_CLOSE_SIGNAL] = g_signal_new("start-close", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  domeshutter_signals[SEND_STOP_MOVE_SIGNAL] = g_signal_new("stop-move", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  domeshutter_signals[PROC_COMPLETE_SIGNAL] = g_signal_new("proc-complete", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_user_marshal_VOID__UCHAR_POINTER, G_TYPE_NONE, 2, G_TYPE_UCHAR, G_TYPE_POINTER);
}

static void domeshutter_init(GtkWidget *domeshutter)
{
  Domeshutter *objs = DOMESHUTTER(domeshutter);
  gtk_frame_set_label(GTK_FRAME(domeshutter), "Dome Shutter");
  objs->dshutt_cur = objs->dshutt_goal = objs->fail_to_id = objs->env_to_id = 0;
  objs->weath_ok = objs->sun_alt_ok = objs->locked = FALSE;
  objs->pending_msg = NULL;
  objs->box = gtk_table_new(1,2,TRUE);
  gtk_container_add(GTK_CONTAINER(domeshutter), objs->box);
  
  objs->btn_open = gtk_toggle_button_new_with_label("Open");
  gtk_table_attach(GTK_TABLE(objs->box), objs->btn_open, 0, 1, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 5, 5);
  objs->btn_close = gtk_toggle_button_new_with_label("Close");
  gtk_table_attach(GTK_TABLE(objs->box), objs->btn_close, 1, 2, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 5, 5);
}

static void domeshutter_destroy(gpointer domeshutter)
{
  Domeshutter *objs = DOMESHUTTER(domeshutter);
  if (objs->fail_to_id != 0)
    g_source_remove(objs->fail_to_id);
  if (objs->env_to_id != 0)
    g_source_remove(objs->env_to_id);
  process_complete(objs, OBSNSTAT_CANCEL);
}

static void buttons_toggled(gpointer domeshutter)
{
  act_log_debug(act_log_msg("Buttons toggled."));
  Domeshutter *objs = DOMESHUTTER(domeshutter);
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_open)) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_close)))
  {
    act_log_error(act_log_msg("Both domeshutter open and close buttons simultaneously active. Stopping domeshutter."));
    send_stop(objs);
  }
  else if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_open)) && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_close)))
    send_stop(objs);
  else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_open)))
    send_start_open(objs);
  else
    send_start_close(objs);
}

static void buttons_nand(GtkWidget *button1, gpointer button2)
{
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button1)) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button2)))
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button2), FALSE);
}

static void block_button_toggles(Domeshutter *objs)
{
  g_signal_handlers_block_by_func(G_OBJECT(objs->btn_open), G_CALLBACK(buttons_toggled), objs);
  g_signal_handlers_block_by_func(G_OBJECT(objs->btn_close), G_CALLBACK(buttons_toggled), objs);
  g_signal_handlers_block_by_func(G_OBJECT(objs->btn_open), G_CALLBACK(buttons_nand), objs->btn_close);
  g_signal_handlers_block_by_func(G_OBJECT(objs->btn_close), G_CALLBACK(buttons_nand), objs->btn_open);
}

static void unblock_button_toggles(Domeshutter *objs)
{
  g_signal_handlers_unblock_by_func(G_OBJECT(objs->btn_open), G_CALLBACK(buttons_toggled), objs);
  g_signal_handlers_unblock_by_func(G_OBJECT(objs->btn_close), G_CALLBACK(buttons_toggled), objs);
  g_signal_handlers_unblock_by_func(G_OBJECT(objs->btn_open), G_CALLBACK(buttons_nand), objs->btn_close);
  g_signal_handlers_unblock_by_func(G_OBJECT(objs->btn_close), G_CALLBACK(buttons_nand), objs->btn_open);
}

static guchar process_quit(Domeshutter *objs, struct act_msg_quit *msg_quit)
{
  if (!msg_quit->mode_auto)
    return OBSNSTAT_GOOD;
  if ((objs->dshutt_cur & DSHUTT_CLOSED_MASK) > 0)
    return OBSNSTAT_GOOD;
  if (objs->pending_msg)
  {
    act_log_debug(act_log_msg("Quit message received, but another message is being processed. Cancelling earlier."));
    process_complete(objs, OBSNSTAT_CANCEL);
  }
  if (objs->locked)
    act_log_error(act_log_msg("Auto-quit message received, but lock is engaged. Sending close anyway and hoping for the best."));
  else
    act_log_debug(act_log_msg("Closing dome shutter for auto-quit."));
  block_button_toggles(objs);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_open), FALSE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_close), TRUE);
  unblock_button_toggles(objs);
  send_start_close(objs);
  return 0;
}

static guchar process_environ(Domeshutter *objs, struct act_msg_environ *msg_environ)
{
  if (objs->env_to_id)
    g_source_remove(objs->env_to_id);
  objs->env_to_id = g_timeout_add_seconds(DOMESHUTTER_ENV_TIME_S, env_timeout, objs);
  act_log_debug(act_log_msg("Processing environ (%hhu %hhu)", msg_environ->weath_ok>0, (msg_environ->status_active & ACTIVE_TIME_NIGHT) > 0));
  return environ_change(objs, msg_environ->weath_ok>0, (msg_environ->status_active & ACTIVE_TIME_NIGHT) > 0);
}

static guchar process_targset(Domeshutter *objs, struct act_msg_targset *msg_targset)
{
  if (msg_targset->status == OBSNSTAT_ERR_CRIT)
  {
    if ((objs->dshutt_cur & DSHUTT_CLOSED_MASK) > 0)
    {
      act_log_debug(act_log_msg("Observation message with critical error flag set, dome shutter already closed."));
      return OBSNSTAT_GOOD;
    }
    act_log_debug(act_log_msg("Observation message with critical error flag set. Closing dome shutter."));
    if (objs->pending_msg)
    {
      act_log_debug(act_log_msg("Previous message still being processed. Cancelling earlier message."));
      process_complete(objs, OBSNSTAT_CANCEL);
    }
    if (objs->locked)
      act_log_error(act_log_msg("Critical target set message received, but lock is engaged. Sending domeshutter close command anyway."));
    else
      act_log_debug(act_log_msg("Closing dome shutter for critical condition."));
    block_button_toggles(objs);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_open), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_close), TRUE);
    unblock_button_toggles(objs);
    send_start_close(objs);
    return 0;
  }
  if (!msg_targset->mode_auto)
    return OBSNSTAT_GOOD;
  if ((objs->dshutt_cur & DSHUTT_OPEN_MASK) > 0)
  {
    act_log_debug(act_log_msg("Dome shutter already open for automatic observation."));
    return OBSNSTAT_GOOD;
  }
  if (!(objs->weath_ok) || !(objs->sun_alt_ok))
  {
    act_log_error(act_log_msg("Cannot open dome shutter for automatic target set - a weather alert is asserted."));
    return OBSNSTAT_ERR_WAIT;
  }
  if (objs->pending_msg)
  {
    act_log_error(act_log_msg("Cannot open dome shutter for automatic target set, another command is being processed."));
    return OBSNSTAT_ERR_WAIT;
  }
  if (objs->locked)
    act_log_debug(act_log_msg("Received auto target set message and need to open dome shutter, but dome shutter is locked. This should be impossible, continuing and hoping for the best."));
  block_button_toggles(objs);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_open), TRUE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_close), FALSE);
  unblock_button_toggles(objs);
  send_start_open(objs);
  return 0;
}

static gboolean fail_timeout(gpointer domeshutter)
{
  act_log_crit(act_log_msg("Timed out while waiting for domeshutter to open/close."));
  Domeshutter *objs = DOMESHUTTER(domeshutter);
  process_complete(objs, OBSNSTAT_ERR_CRIT);
  GdkColor new_col;
  gdk_color_parse("#AA0000", &new_col);
  gtk_widget_modify_bg(objs->btn_open, GTK_STATE_ACTIVE, &new_col);
  gtk_widget_modify_bg(objs->btn_close, GTK_STATE_ACTIVE, &new_col);
  send_start_close(objs);
  objs->fail_to_id = 0;
  return FALSE;
}

static gboolean env_timeout(gpointer domeshutter)
{
  act_log_debug(act_log_msg("Timed out while waiting for environment message."));
  Domeshutter *objs = DOMESHUTTER(domeshutter);
  environ_change(objs, FALSE, FALSE);
  objs->env_to_id = 0;
  return FALSE;
}

static guchar environ_change(Domeshutter *objs, gboolean weath_ok, gboolean sun_alt_ok)
{
  if (((weath_ok>0) == (objs->weath_ok>0)) && ((sun_alt_ok>0) == (objs->sun_alt_ok>0)))
    return OBSNSTAT_GOOD;
  objs->weath_ok = weath_ok > 0;
  objs->sun_alt_ok = sun_alt_ok > 0;
  if ((objs->weath_ok) && (objs->sun_alt_ok))
  {
    if (!objs->locked)
      gtk_widget_set_sensitive(objs->btn_open, TRUE);
    return OBSNSTAT_GOOD;
  }
  act_log_debug(act_log_msg("Weather alert asserted"));
  gtk_widget_set_sensitive(objs->btn_open, FALSE);
  if ((objs->dshutt_cur & DSHUTT_CLOSED_MASK) > 0)
  {
    act_log_debug(act_log_msg("Dome already closed."));
    return OBSNSTAT_GOOD;
  }
  if (objs->pending_msg)
    process_complete(objs, OBSNSTAT_CANCEL);
  if (objs->locked)
    act_log_error(act_log_msg("Cannot close domeshutter because lock is engaged. Sending close anyway and hoping for the best."));
  else
    act_log_debug(act_log_msg("Closing domeshutter for weather alert."));
  block_button_toggles(objs);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_open), FALSE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_close), TRUE);
  unblock_button_toggles(objs);
  send_start_close(objs);
  return 0;
}

static void process_complete(Domeshutter *objs, guchar status)
{
  if (objs->pending_msg == NULL)
    return;
  act_log_debug(act_log_msg("Dome shutter process complete, status - %hhu %hhu %hhd", status, objs->dshutt_cur, objs->dshutt_goal));
  g_signal_emit(G_OBJECT(objs), domeshutter_signals[PROC_COMPLETE_SIGNAL], 0, status, objs->pending_msg);
  g_object_unref(objs->pending_msg);
  objs->pending_msg = NULL;
}

static void send_start_open(Domeshutter *objs)
{
  act_log_debug(act_log_msg("Sending domeshutter open"));
  g_signal_emit(objs, domeshutter_signals[SEND_START_OPEN_SIGNAL], 0);
  if (objs->fail_to_id != 0)
    g_source_remove(objs->fail_to_id);
  objs->dshutt_goal = DSHUTT_OPEN_MASK;
  if (objs->dshutt_cur != DSHUTT_OPEN_MASK)
    objs->fail_to_id = g_timeout_add_seconds(DOMESHUTTER_FAIL_TIME_S, fail_timeout, objs);
}

static void send_start_close(Domeshutter *objs)
{
  act_log_debug(act_log_msg("Sending domeshutter close"));
  g_signal_emit(objs, domeshutter_signals[SEND_START_CLOSE_SIGNAL], 0);
  if (objs->fail_to_id != 0)
    g_source_remove(objs->fail_to_id);
  objs->dshutt_goal = DSHUTT_CLOSED_MASK;
  if (objs->dshutt_cur != DSHUTT_CLOSED_MASK)
    objs->fail_to_id = g_timeout_add_seconds(DOMESHUTTER_FAIL_TIME_S, fail_timeout, objs);
}

static void send_stop(Domeshutter *objs)
{ 
  act_log_debug(act_log_msg("Sending stop."));
  g_signal_emit(objs, domeshutter_signals[SEND_STOP_MOVE_SIGNAL], 0);
  if (objs->fail_to_id != 0)
    g_source_remove(objs->fail_to_id);
  objs->fail_to_id = 0;
  objs->dshutt_goal = 0;
}

