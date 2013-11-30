#include <gtk/gtk.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <act_plc.h>
#include <act_log.h>
#include <act_ipc.h>
#include "dti_marshallers.h"
#include "filter.h"

#define FILT_FAIL_TIME_S  10

static void filter_class_init (FilterClass *klass);
static void filter_init(GtkWidget *filter);
static void filter_destroy(gpointer filter);
static void filter_update (Filter *objs);
static void filt_select(GtkWidget *cmb_filtsel, gpointer filter);
static guchar process_msg_quit(Filter *objs, struct act_msg_quit *msg_quit);
static guchar process_msg_pmtcaps(Filter *objs, struct act_msg_pmtcap *msg_pmtcap);
static guchar process_msg_datapmt(Filter *objs, struct act_msg_datapmt *msg_datapmt);
static void process_complete(Filter *objs, guchar status);
static void send_filt(Filter *objs, guchar filt_slot);
void add_filt_to_cmb(struct filtaper *filt_info, gpointer filtstore);
static gboolean fail_timeout(gpointer filter);

enum
{
  SEND_FILT_SIGNAL,
  PROC_COMPLETE_SIGNAL,
  LAST_SIGNAL
};

enum 
{
  FILTSTORE_COL_ID = 0,
  FILTSTORE_COL_SLOT,
  FILTSTORE_COL_NAME,
  FILTSTORE_NUM_COLS
};

static guint filter_signals[LAST_SIGNAL] = { 0 };

GType filter_get_type (void)
{
  static GType filter_type = 0;
  
  if (!filter_type)
  {
    const GTypeInfo filter_info =
    {
      sizeof (FilterClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) filter_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (Filter),
      0,
      (GInstanceInitFunc) filter_init,
      NULL
    };
    
    filter_type = g_type_register_static (GTK_TYPE_FRAME, "Filter", &filter_info, 0);
  }
  
  return filter_type;
}

GtkWidget *filter_new (guchar filt_stat, guchar filt_slot)
{
  GtkWidget *filter = g_object_new (filter_get_type (), NULL);
  Filter *objs = FILTER(filter);
  objs->filt_stat = filt_stat;
  objs->filt_goal = filt_slot;
  objs->filt_slot = filt_slot;
  filter_update (objs);
  
  g_signal_connect_swapped(G_OBJECT(filter), "destroy", G_CALLBACK(filter_destroy), filter);
  g_signal_connect(G_OBJECT(objs->cmb_filtsel), "changed", G_CALLBACK(filt_select), filter);
  
  return filter;
}

void filter_update_stat (GtkWidget *filter, guchar new_filt_stat)
{
  Filter *objs = FILTER(filter);
  objs->filt_stat = new_filt_stat;
  filter_update(objs);
}

void filter_update_slot (GtkWidget *filter, guchar new_filt_slot)
{
  Filter *objs = FILTER(filter);
  objs->filt_slot = new_filt_slot;
  filter_update(objs);
}

void filter_process_msg(GtkWidget *filter, DtiMsg *msg)
{
  gchar ret = OBSNSTAT_GOOD;
  Filter *objs = FILTER(filter);
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
    g_signal_emit(filter, filter_signals[PROC_COMPLETE_SIGNAL], 0, ret, msg);
    return;
  }
  g_object_ref(msg);
  objs->pending_msg = msg;
}

static void filter_class_init (FilterClass *klass)
{
  filter_signals[SEND_FILT_SIGNAL] = g_signal_new ("send-filter", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__UCHAR, G_TYPE_NONE, 1, G_TYPE_UCHAR);
  filter_signals[PROC_COMPLETE_SIGNAL] = g_signal_new("proc-complete", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_user_marshal_VOID__UCHAR_POINTER, G_TYPE_NONE, 2, G_TYPE_UCHAR, G_TYPE_POINTER);
}

static void filter_init(GtkWidget *filter)
{
  Filter *objs = FILTER(filter);
  gtk_frame_set_label(GTK_FRAME(filter), "PMT Filter");
  objs->fail_to_id = 0;
  objs->filt_stat = objs->filt_slot = objs->filt_goal = 0;
  objs->pending_msg = NULL;
  objs->box = gtk_table_new(1,2,TRUE);
  gtk_container_add(GTK_CONTAINER(filter), objs->box);
  
  objs->cmb_filtsel = gtk_combo_box_new();
  GtkListStore *filtstore = gtk_list_store_new(FILTSTORE_NUM_COLS, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING);
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(objs->cmb_filtsel), renderer, TRUE);
  gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(objs->cmb_filtsel), renderer, "text", FILTSTORE_COL_NAME);
  gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(filtstore), FILTSTORE_COL_SLOT, GTK_SORT_ASCENDING);
  gtk_combo_box_set_model(GTK_COMBO_BOX(objs->cmb_filtsel), GTK_TREE_MODEL(filtstore));
  gtk_table_attach(GTK_TABLE(objs->box), objs->cmb_filtsel,0,1,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 5, 5);
  objs->evb_filtdisp = gtk_event_box_new();
  gtk_table_attach(GTK_TABLE(objs->box), objs->evb_filtdisp,1,2,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 5, 5);
  objs->lbl_filtdisp = gtk_label_new("");
  gtk_container_add(GTK_CONTAINER(objs->evb_filtdisp), objs->lbl_filtdisp);
}

static void filter_destroy(gpointer filter)
{
  Filter *objs = FILTER(filter);
  if (objs->fail_to_id != 0)
    g_source_remove(objs->fail_to_id);
  process_complete(objs, OBSNSTAT_CANCEL);
}

static void filter_update (Filter *objs)
{
  GdkColor new_col;
  if ((objs->filt_stat & FILTAPER_MOVING_MASK) || (objs->filt_slot != objs->filt_goal))
  {
    gdk_color_parse("#AAAA00", &new_col);
    gtk_widget_modify_bg(objs->evb_filtdisp, GTK_STATE_NORMAL, &new_col);
  }
  else if (objs->filt_stat & FILTAPER_CENT_MASK)
  {
    gdk_color_parse("#00AA00", &new_col);
    gtk_widget_modify_bg(objs->evb_filtdisp, GTK_STATE_NORMAL, &new_col);
    if (objs->fail_to_id > 0)
    {
      g_source_remove(objs->fail_to_id);
      objs->fail_to_id = 0;
    }
    process_complete(objs, OBSNSTAT_GOOD);
  }
  else
  {
    act_log_debug(act_log_msg("Unknown filter status: %hhu", objs->filt_stat));
    gdk_color_parse("#0000AA", &new_col);
    gtk_widget_modify_bg(objs->evb_filtdisp, GTK_STATE_NORMAL, &new_col);
  }
  
  GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(objs->cmb_filtsel));
  GtkTreeIter iter;
  gpointer filt_name = NULL;
  if (!gtk_tree_model_get_iter_first(model, &iter))
    act_log_debug(act_log_msg("Could not retrieve first iterator for GtkTreeModel on Filters combobox."));
  else
  {
    gint filt_slot = -1;
    while (gtk_list_store_iter_is_valid(GTK_LIST_STORE(model), &iter))
    {
      gtk_tree_model_get(model, &iter, FILTSTORE_COL_SLOT, &filt_slot, -1);
      if (filt_slot == objs->filt_slot)
      {
        gtk_tree_model_get(model, &iter, FILTSTORE_COL_NAME, &filt_name, -1);
        break;
      }
      gtk_tree_model_iter_next(model, &iter);
    }
  }
  if (filt_name != NULL)
  {
    gtk_label_set_text(GTK_LABEL(objs->lbl_filtdisp), filt_name);
    g_free(filt_name);
  }
  else
  {
    act_log_debug(act_log_msg("WARNING: Filter name for filter in slot %hhu could not be retrieved.", objs->filt_slot));
    char tmpstr[10];
    snprintf(tmpstr, sizeof(tmpstr), "(%hhu)", objs->filt_slot);
    gtk_label_set_text(GTK_LABEL(objs->lbl_filtdisp), tmpstr);
  }
}

static void filt_select(GtkWidget *cmb_filtsel, gpointer filter)
{  
  Filter *objs = FILTER(filter);
  GtkTreeIter active_filt;
  if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX(cmb_filtsel),&active_filt))
  {
    act_log_error(act_log_msg("No valid filter selected."));
    return;
  }
  int filt_active = -1;
  GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(cmb_filtsel));
  gtk_tree_model_get(model, &active_filt, FILTSTORE_COL_SLOT, &filt_active, -1);
  send_filt(objs, filt_active);
}

static guchar process_msg_quit(Filter *objs, struct act_msg_quit *msg_quit)
{
  process_complete(objs, OBSNSTAT_CANCEL);
  if (!msg_quit->mode_auto)
    return OBSNSTAT_GOOD;
  send_filt(objs, 9);
  return 0;
}

static guchar process_msg_pmtcaps(Filter *objs, struct act_msg_pmtcap *msg_pmtcap)
{
  long filtchange_handler = g_signal_handler_find(G_OBJECT(objs->cmb_filtsel), G_SIGNAL_MATCH_FUNC, 0, 0, NULL, G_CALLBACK(filt_select), NULL);
  if (filtchange_handler > 0)
    g_signal_handler_block(G_OBJECT(objs->cmb_filtsel), filtchange_handler);
  
  GtkListStore *filtstore = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(objs->cmb_filtsel)));
  gtk_list_store_clear(filtstore);
  
  int i;
  char cur_filt_name[IPC_MAX_FILTAPER_NAME_LEN]  = { 0 };
  for (i=0; i<IPC_MAX_NUM_FILTAPERS; i++)
  {
    if (msg_pmtcap->filters[i].db_id < 0)
      continue;
    add_filt_to_cmb(&msg_pmtcap->filters[i], filtstore);
    if (msg_pmtcap->filters[i].slot == objs->filt_slot)
      memcpy(cur_filt_name, msg_pmtcap->filters[i].name, IPC_MAX_FILTAPER_NAME_LEN);
  }
  struct filtaper filt_init;
  filt_init.slot = IPC_MAX_NUM_FILTAPERS;
  filt_init.db_id = 0;
  snprintf(filt_init.name, sizeof(filt_init.name), "Init");
  add_filt_to_cmb(&filt_init, filtstore);
  if (strlen(cur_filt_name) > 0)
    gtk_label_set_text(GTK_LABEL(objs->lbl_filtdisp), cur_filt_name);
  else
    act_log_debug(act_log_msg("Name of current filter is not available."));
  
  if (filtchange_handler > 0)
    g_signal_handler_unblock(G_OBJECT(objs->cmb_filtsel), filtchange_handler);

  return OBSNSTAT_GOOD;
}

static guchar process_msg_datapmt(Filter *objs, struct act_msg_datapmt *msg_datapmt)
{
  if ((msg_datapmt->status != OBSNSTAT_GOOD) || (!msg_datapmt->mode_auto))
    return OBSNSTAT_GOOD;
  if (objs->pending_msg != NULL)
    return OBSNSTAT_ERR_WAIT;
  if ((msg_datapmt->filter.slot == objs->filt_slot) && ((objs->filt_stat & FILTAPER_MOVING_MASK) == 0))
    return OBSNSTAT_GOOD;
  if (msg_datapmt->filter.slot >= IPC_MAX_NUM_FILTAPERS)
    return OBSNSTAT_ERR_NEXT;
  send_filt(objs, (guchar)msg_datapmt->filter.slot);
  return 0;
}

static void process_complete(Filter *objs, guchar status)
{
  if (objs->pending_msg == NULL)
    return;
  act_log_debug(act_log_msg("Filter process complete: status %hhu - %hhu %hhu %hhd", status, objs->filt_stat, objs->filt_slot, objs->filt_goal));
  guchar ret_stat = status;
  if (status == 0)
  {
    if (((objs->filt_stat & FILTAPER_MOVING_MASK) == 0) && (objs->filt_slot == objs->filt_goal))
      ret_stat = OBSNSTAT_GOOD;
    else
      ret_stat = OBSNSTAT_ERR_RETRY;
  }
  g_signal_emit(G_OBJECT(objs), filter_signals[PROC_COMPLETE_SIGNAL], 0, ret_stat, objs->pending_msg);
  g_object_unref(objs->pending_msg);
  objs->pending_msg = NULL;
}

static void send_filt(Filter *objs, guchar filt_slot)
{
  act_log_debug(act_log_msg("Sending filter %hhu", filt_slot));
  g_signal_emit(G_OBJECT(objs), filter_signals[SEND_FILT_SIGNAL], 0, filt_slot);
  if (filt_slot >= IPC_MAX_NUM_FILTAPERS)
    objs->filt_goal = 0;
  else
    objs->filt_goal = filt_slot;
  if (objs->fail_to_id)
    g_source_remove(objs->fail_to_id);
  if (objs->filt_slot != objs->filt_goal)
    objs->fail_to_id = g_timeout_add_seconds(FILT_FAIL_TIME_S, fail_timeout, objs);
}

void add_filt_to_cmb(struct filtaper *filt_info, gpointer filtstore)
{
  GtkTreeIter iter;
  gtk_list_store_append(GTK_LIST_STORE(filtstore), &iter);
  gtk_list_store_set(GTK_LIST_STORE(filtstore), &iter, FILTSTORE_COL_ID, filt_info->db_id, FILTSTORE_COL_SLOT, filt_info->slot, FILTSTORE_COL_NAME, filt_info->name, -1);
}

static gboolean fail_timeout(gpointer filter)
{
  Filter *objs = FILTER(filter);
  process_complete(objs, OBSNSTAT_ERR_RETRY);
  objs->fail_to_id = 0;
  GdkColor new_col;
  gdk_color_parse("#AAAA00", &new_col);
  gtk_widget_modify_bg(objs->evb_filtdisp, GTK_STATE_NORMAL, &new_col);

  return FALSE;
}

