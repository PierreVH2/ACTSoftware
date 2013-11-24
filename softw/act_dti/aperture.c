#include <gtk/gtk.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <act_plc.h>
#include <act_log.h>
#include <act_ipc.h>
#include "dti_marshallers.h"
#include "aperture.h"

#define APER_FAIL_TIME_S  60

static void aperture_class_init (ApertureClass *klass);
static void aperture_init(GtkWidget *aperture);
static void aperture_destroy(gpointer aperture);
static void aperture_update (Aperture *objs);
static void aper_select(GtkWidget *cmb_apersel, gpointer aperture);
static guchar process_msg_quit(Aperture *objs, struct act_msg_quit *msg_quit);
static guchar process_msg_datapmt(Aperture *objs, struct act_msg_datapmt *msg_datapmt);
static guchar process_msg_pmtcaps(Aperture *objs, struct act_msg_pmtcap *msg_pmtcap);
static void process_complete(Aperture *objs, guchar status);
static void send_aper(Aperture *objs, guchar aper_slot);
static void add_aper_to_cmb(struct filtaper *aper_info, gpointer aperstore);
static gboolean fail_timeout(gpointer aperture);

enum
{
  SEND_APER_SIGNAL,
  PROC_COMPLETE_SIGNAL,
  LAST_SIGNAL
};

enum 
{
  APERSTORE_COL_ID = 0,
  APERSTORE_COL_SLOT,
  APERSTORE_COL_NAME,
  APERSTORE_NUM_COLS
};

static guint aperture_signals[LAST_SIGNAL] = { 0 };

GType aperture_get_type (void)
{
  static GType aperture_type = 0;
  
  if (!aperture_type)
  {
    const GTypeInfo aperture_info =
    {
      sizeof (ApertureClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) aperture_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (Aperture),
      0,
      (GInstanceInitFunc) aperture_init,
      NULL
    };
    
    aperture_type = g_type_register_static (GTK_TYPE_FRAME, "Aperture", &aperture_info, 0);
  }
  
  return aperture_type;
}

GtkWidget* aperture_new (guchar aper_stat, guchar aper_slot)
{
  GtkWidget *aperture = g_object_new (aperture_get_type (), NULL);
  Aperture *objs = APERTURE(aperture);
  objs->aper_stat = aper_stat;
  objs->aper_goal = aper_slot;
  objs->aper_slot = aper_slot;
  aperture_update (objs);
  
  g_signal_connect_swapped(G_OBJECT(aperture), "destroy", G_CALLBACK(aperture_destroy), aperture);
  g_signal_connect(G_OBJECT(objs->cmb_apersel), "changed", G_CALLBACK(aper_select), aperture);
  
  return aperture;
}

void aperture_update_stat (GtkWidget *aperture, guchar new_aper_stat)
{
  Aperture *objs = APERTURE(aperture);
  objs->aper_stat = new_aper_stat;
  aperture_update(objs);
}

void aperture_update_slot (GtkWidget *aperture, guchar new_aper_slot)
{
  Aperture *objs = APERTURE(aperture);
  objs->aper_slot = new_aper_slot;
  aperture_update(objs);
}

void aperture_process_msg(GtkWidget *aperture, DtiMsg *msg)
{
  gchar ret = OBSNSTAT_GOOD;
  Aperture *objs = APERTURE(aperture);
  switch (dti_msg_get_mtype(msg))
  {
    case MT_QUIT:
      ret = process_msg_quit(objs, dti_msg_get_quit(msg));
      break;
    case MT_PMT_CAP:
      ret = process_msg_pmtcaps(objs, dti_msg_get_pmtcap(msg));
      break;
    case MT_DATA_PMT:
      ret = process_msg_datapmt(objs, dti_msg_get_datapmt(msg));
      break;
  }
  if (ret != 0)
  {
    g_signal_emit(aperture, aperture_signals[PROC_COMPLETE_SIGNAL], 0, ret, msg);
    return;
  }
  g_object_ref(msg);
  objs->pending_msg = msg;
}

static void aperture_class_init (ApertureClass *klass)
{
  aperture_signals[SEND_APER_SIGNAL] = g_signal_new ("send-aperture", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__UCHAR, G_TYPE_NONE, 1, G_TYPE_UCHAR);
  aperture_signals[PROC_COMPLETE_SIGNAL] = g_signal_new("proc-complete", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_user_marshal_VOID__UCHAR_POINTER, G_TYPE_NONE, 2, G_TYPE_UCHAR, G_TYPE_POINTER);
}

static void aperture_init(GtkWidget *aperture)
{
  Aperture *objs = APERTURE(aperture);
  gtk_frame_set_label(GTK_FRAME(aperture), "PMT Aperture");
  objs->fail_to_id = 0;
  objs->aper_stat = objs->aper_slot = objs->aper_goal = 0;
  objs->pending_msg = NULL;
  objs->box = gtk_table_new(1,2,TRUE);
  gtk_container_add(GTK_CONTAINER(aperture), objs->box);

  objs->cmb_apersel = gtk_combo_box_new();
  GtkListStore *aperstore = gtk_list_store_new(APERSTORE_NUM_COLS, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING);
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(objs->cmb_apersel), renderer, TRUE);
  gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(objs->cmb_apersel), renderer, "text", APERSTORE_COL_NAME);
  gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(aperstore), APERSTORE_COL_SLOT, GTK_SORT_ASCENDING);
  gtk_combo_box_set_model(GTK_COMBO_BOX(objs->cmb_apersel), GTK_TREE_MODEL(aperstore));
  gtk_table_attach(GTK_TABLE(objs->box), objs->cmb_apersel,0,1,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 5, 5);
  objs->evb_aperdisp = gtk_event_box_new();
  gtk_table_attach(GTK_TABLE(objs->box), objs->evb_aperdisp,1,2,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 5, 5);
  objs->lbl_aperdisp = gtk_label_new("");
  gtk_container_add(GTK_CONTAINER(objs->evb_aperdisp), objs->lbl_aperdisp);
}

static void aperture_destroy(gpointer aperture)
{
  Aperture *objs = APERTURE(aperture);
  if (objs->fail_to_id != 0)
    g_source_remove(objs->fail_to_id);
  process_complete(objs, OBSNSTAT_CANCEL);
}

static void aperture_update (Aperture *objs)
{
  GdkColor new_col;
  if ((objs->aper_stat & FILTAPER_MOVING_MASK) || (objs->aper_slot != objs->aper_goal))
  {
    gdk_color_parse("#AAAA00", &new_col);
    gtk_widget_modify_bg(objs->evb_aperdisp, GTK_STATE_NORMAL, &new_col);
  }
  else if (objs->aper_stat & FILTAPER_CENT_MASK)
  {
    gdk_color_parse("#00AA00", &new_col);
    gtk_widget_modify_bg(objs->evb_aperdisp, GTK_STATE_NORMAL, &new_col);
    if (objs->fail_to_id > 0)
    {
      g_source_remove(objs->fail_to_id);
      objs->fail_to_id = 0;
    }
    process_complete(objs, OBSNSTAT_GOOD);
  }
  else
  {
    act_log_debug(act_log_msg("Unknown aperture status: %hhu", objs->aper_stat));
    gdk_color_parse("#0000AA", &new_col);
    gtk_widget_modify_bg(objs->evb_aperdisp, GTK_STATE_NORMAL, &new_col);
  }
  
  GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(objs->cmb_apersel));
  GtkTreeIter iter;
  gpointer aper_name = NULL;
  if (!gtk_tree_model_get_iter_first(model, &iter))
    act_log_debug(act_log_msg("Could not retrieve first iterator for GtkTreeModel on Apertures combobox."));
  else
  {
    gint aper_slot = -1;
    while (gtk_list_store_iter_is_valid(GTK_LIST_STORE(model), &iter))
    {
      gtk_tree_model_get(model, &iter, APERSTORE_COL_SLOT, &aper_slot, -1);
      if (aper_slot == objs->aper_slot)
      {
        gtk_tree_model_get(model, &iter, APERSTORE_COL_NAME, &aper_name, -1);
        break;
      }
      gtk_tree_model_iter_next(model, &iter);
    }
  }
  if (aper_name != NULL)
  {
    gtk_label_set_text(GTK_LABEL(objs->lbl_aperdisp), aper_name);
    g_free(aper_name);
  }
  else
  {
    act_log_debug(act_log_msg("WARNING: Aperture name for aperture in slot %hhu could not be retrieved.", objs->aper_slot));
    char tmpstr[10];
    snprintf(tmpstr, sizeof(tmpstr)-1, "(%hhu)", objs->aper_slot);
    gtk_label_set_text(GTK_LABEL(objs->lbl_aperdisp), tmpstr);
  }
}

static void aper_select(GtkWidget *cmb_apersel, gpointer aperture)
{  
  Aperture *objs = APERTURE(aperture);
  GtkTreeIter active_aper;
  if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX(cmb_apersel),&active_aper))
  {
    act_log_error(act_log_msg("No valid aperture selected."));
    return;
  }
  int aper_active = -1;
  GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(cmb_apersel));
  gtk_tree_model_get(model, &active_aper, APERSTORE_COL_SLOT, &aper_active, -1);
  if ((aper_active < 0) || (aper_active >= IPC_MAX_NUM_FILTAPERS))
  {
    act_log_error(act_log_msg("Invalid aperture slot: %d", aper_active));
    return;
  }
  send_aper(objs, aper_active);
}

static guchar process_msg_quit(Aperture *objs, struct act_msg_quit *msg_quit)
{
  process_complete(objs, OBSNSTAT_CANCEL);
  if (!msg_quit->mode_auto)
    return OBSNSTAT_GOOD;
  send_aper(objs, 9);
  return 0;
}

static guchar process_msg_pmtcaps(Aperture *objs, struct act_msg_pmtcap *msg_pmtcap)
{
  long aperchange_handler = g_signal_handler_find(G_OBJECT(objs->cmb_apersel), G_SIGNAL_MATCH_FUNC, 0, 0, NULL, G_CALLBACK(aper_select), NULL);
  if (aperchange_handler > 0)
    g_signal_handler_block(G_OBJECT(objs->cmb_apersel), aperchange_handler);
  
  GtkListStore *aperstore = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(objs->cmb_apersel)));
  gtk_list_store_clear(aperstore);
  
  int i;
  char cur_aper_name[IPC_MAX_FILTAPER_NAME_LEN]  = { 0 };
  for (i=0; i<IPC_MAX_NUM_FILTAPERS; i++)
  {
    if (msg_pmtcap->apertures[i].db_id < 0)
      continue;
    add_aper_to_cmb(&msg_pmtcap->apertures[i], aperstore);
    if (msg_pmtcap->apertures[i].slot == objs->aper_slot)
      memcpy(cur_aper_name, msg_pmtcap->apertures[i].name, IPC_MAX_FILTAPER_NAME_LEN);
  }
  struct filtaper aper_init;
  aper_init.slot = IPC_MAX_NUM_FILTAPERS;
  aper_init.db_id = 0;
  snprintf(aper_init.name, sizeof(aper_init.name), "Init");
  add_aper_to_cmb(&aper_init, aperstore);
  if (strlen(cur_aper_name) > 0)
    gtk_label_set_text(GTK_LABEL(objs->lbl_aperdisp), cur_aper_name);
  else
    act_log_debug(act_log_msg("Name of current aperture is not available."));
  
  if (aperchange_handler > 0)
    g_signal_handler_unblock(G_OBJECT(objs->cmb_apersel), aperchange_handler);
  return OBSNSTAT_GOOD;
}

static guchar process_msg_datapmt(Aperture *objs, struct act_msg_datapmt *msg_datapmt)
{
  if ((msg_datapmt->status != OBSNSTAT_GOOD) || (!msg_datapmt->mode_auto))
    return OBSNSTAT_GOOD;
  if (objs->pending_msg != NULL)
    return OBSNSTAT_ERR_WAIT;
  if ((msg_datapmt->aperture.slot == objs->aper_slot) && ((objs->aper_stat & FILTAPER_MOVING_MASK) == 0))
    return OBSNSTAT_GOOD;
  if (msg_datapmt->aperture.slot >= IPC_MAX_NUM_FILTAPERS)
    return OBSNSTAT_ERR_NEXT;
  send_aper(objs, (guchar)msg_datapmt->aperture.slot);
  return 0;
}

static void process_complete(Aperture *objs, guchar status)
{
  if (objs->pending_msg == NULL)
    return;
  act_log_debug(act_log_msg("Aperture process complete: status %hhu - %hhu %hhu %hhd", status, objs->aper_stat, objs->aper_slot, objs->aper_goal));
  g_signal_emit(G_OBJECT(objs), aperture_signals[PROC_COMPLETE_SIGNAL], 0, status, objs->pending_msg);
  g_object_unref(objs->pending_msg);
  objs->pending_msg = NULL;
}

static void send_aper(Aperture *objs, guchar aper_slot)
{
  g_signal_emit(G_OBJECT(objs), aperture_signals[SEND_APER_SIGNAL], 0, aper_slot);
  objs->aper_goal = aper_slot;
  if (objs->fail_to_id)
    g_source_remove(objs->fail_to_id);
  objs->fail_to_id = g_timeout_add_seconds(APER_FAIL_TIME_S, fail_timeout, objs);
}

static void add_aper_to_cmb(struct filtaper *aper_info, gpointer aperstore)
{
  GtkTreeIter iter;
  gtk_list_store_append(GTK_LIST_STORE(aperstore), &iter);
  gtk_list_store_set(GTK_LIST_STORE(aperstore), &iter, APERSTORE_COL_ID, aper_info->db_id, APERSTORE_COL_SLOT, aper_info->slot, APERSTORE_COL_NAME, aper_info->name, -1);
}

static gboolean fail_timeout(gpointer aperture)
{
  Aperture *objs = APERTURE(aperture);
  process_complete(objs, OBSNSTAT_ERR_RETRY);
  objs->fail_to_id = 0;
  GdkColor new_col;
  gdk_color_parse("#AA0000", &new_col);
  gtk_widget_modify_bg(objs->evb_aperdisp, GTK_STATE_NORMAL, &new_col);

  return FALSE;
}
