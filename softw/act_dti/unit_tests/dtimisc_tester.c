#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <act_plc.h>
#include <act_ipc.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "dtimisc.h"

void quickmsg_select(GtkWidget *cmb_quick_msg, gpointer ent_sendmsg);
void send_msg(gpointer user_data);
void console_scroll_to_end(GtkWidget *txv_console);

gboolean plc_stat_update(GIOChannel *plc_chan, GIOCondition condition, gpointer dtimisc);
void watchdog_trip(gpointer txv_console);
void trapdoor_open(gpointer txv_console);
void trapdoor_close(gpointer txv_console);
void focus_fail(gpointer txv_console);
void power_fail(gpointer txv_console);
void proc_complete(gpointer txv_console, gint status);
void plc_comm_stat(gpointer txv_console, gboolean status);
void user_interact(gpointer txv_console);

enum
{
  QM_MTYPE = 0,
  QM_MNAME,
  QM_MTEXT,
  QM_NUMCOLS
};

int main (int argc, char ** argv)
{
  gtk_init(&argc, &argv);
  int plc_fd = open("/dev/" PLC_DEVICE_NAME, 0);
  struct plc_status plc_stat;
  int ret = ioctl(plc_fd, IOCTL_GET_STATUS, &plc_stat);
  if (ret < 0)
  {
    fprintf(stderr, "Error reading PLC status - %s (%d).\n", strerror(errno), errno);
    close(plc_fd);
    return 1;
  }
  GError *error = NULL;
  GIOChannel *plc_chan = g_io_channel_unix_new(plc_fd);
  g_io_channel_set_close_on_unref (plc_chan, TRUE);
  g_io_channel_set_encoding (plc_chan, NULL, &error);
  if (error != NULL)
  {
    fprintf(stderr, "Failed to set encoding type for PLC driver channel (%d - %s).\n", error->code, error->message);
    g_error_free(error);
    return 1;
  }
  g_io_channel_set_buffered (plc_chan, FALSE);

  GtkWidget *wnd_main = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size (GTK_WINDOW(wnd_main), 1, 300);
  GtkWidget *box_main = gtk_table_new(0, 0, FALSE);
  gtk_container_add(GTK_CONTAINER(wnd_main), box_main);
  
  GtkListStore *qmstore = gtk_list_store_new(QM_NUMCOLS, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
  GtkTreeIter iter;
  gtk_list_store_append(GTK_LIST_STORE(qmstore), &iter);
  gtk_list_store_set(GTK_LIST_STORE(qmstore), &iter, QM_MTYPE, MT_QUIT, QM_MNAME, "QUIT", QM_MTEXT, "1,1", -1);
  gtk_list_store_append(GTK_LIST_STORE(qmstore), &iter);
  gtk_list_store_set(GTK_LIST_STORE(qmstore), &iter, QM_MTYPE, MT_CAP, QM_MNAME, "CAP", QM_MTEXT, "2,0,0,0,0,0,2.0", -1);
  gtk_list_store_append(GTK_LIST_STORE(qmstore), &iter);
  gtk_list_store_set(GTK_LIST_STORE(qmstore), &iter, QM_MTYPE, MT_STAT, QM_MNAME, "STAT", QM_MTEXT, "3,2", -1);
  gtk_list_store_append(GTK_LIST_STORE(qmstore), &iter);
  gtk_list_store_set(GTK_LIST_STORE(qmstore), &iter, QM_MTYPE, MT_GUISOCK, QM_MNAME, "GUISOCK", QM_MTEXT, "4,12345,localhost:1.0", -1);
  gtk_list_store_append(GTK_LIST_STORE(qmstore), &iter);
  gtk_list_store_set(GTK_LIST_STORE(qmstore), &iter, QM_MTYPE, MT_COORD, QM_MNAME, "COORD", QM_MTEXT, "5,0.0,0.0,0.0,0.0,0.0,2000.0", -1);
  gtk_list_store_append(GTK_LIST_STORE(qmstore), &iter);
  gtk_list_store_set(GTK_LIST_STORE(qmstore), &iter, QM_MTYPE, MT_TIME, QM_MNAME, "TIME", QM_MTEXT, "6,12.0,14.0,15.540,2013/05/07,2013/05/07,2456455.0,2456455.00264", -1);
  gtk_list_store_append(GTK_LIST_STORE(qmstore), &iter);
  gtk_list_store_set(GTK_LIST_STORE(qmstore), &iter, QM_MTYPE, MT_ENVIRON, QM_MNAME, "ENVIRON", QM_MTEXT, "7,3,1,45,20,0,-15.0,22.0,0.0,35.0,270.0,1.0", -1);
  gtk_list_store_append(GTK_LIST_STORE(qmstore), &iter);
  gtk_list_store_set(GTK_LIST_STORE(qmstore), &iter, QM_MTYPE, MT_TARG_CAP, QM_MNAME, "TARG CAP", QM_MTEXT, "8,0,1,5.0,-5.0,41.0,-105.0,15.0", -1);
  gtk_list_store_append(GTK_LIST_STORE(qmstore), &iter);
  gtk_list_store_set(GTK_LIST_STORE(qmstore), &iter, QM_MTYPE, MT_TARG_SET, QM_MNAME, "TARG SET", QM_MTEXT, "9,1,1,0,534,E123,1,12.0,-34.0,2000.0,0.0,0.0,1,-235,0", -1);
  gtk_list_store_append(GTK_LIST_STORE(qmstore), &iter);
  gtk_list_store_set(GTK_LIST_STORE(qmstore), &iter, QM_MTYPE, MT_PMT_CAP, QM_MNAME, "PMT CAP", QM_MTEXT, "10,Hamamatsu,1,1,0.001,1.0,U,1,1,B,2,2,V,3,3,R,4,4,I,5,5,N,0,-1,N,0,-1,N,0,-1,N,0,-1,N,0,-1,45,1,1,35,2,2,30,3,3,25,4,4,20,5,5,N,0,-1,N,0,-1,N,0,-1,N,0,-1,N,0,-1", -1);
  gtk_list_store_append(GTK_LIST_STORE(qmstore), &iter);
  gtk_list_store_set(GTK_LIST_STORE(qmstore), &iter, QM_MTYPE, MT_DATA_PMT, QM_MNAME, "DATA PMT", QM_MTEXT, "11,1,1,0,1,1,E123,0,12.0,-34.0,2000.0,1,0.001,1000,1,U,1,1,35,2,2", -1);
  gtk_list_store_append(GTK_LIST_STORE(qmstore), &iter);
  gtk_list_store_set(GTK_LIST_STORE(qmstore), &iter, QM_MTYPE, MT_CCD_CAP, QM_MNAME, "CCD CAP", QM_MTEXT, "12,40,60000,1,MERLIN,U,1,1,B,2,2,V,3,3,R,4,4,I,5,5,N,0,-1,N,0,-1,N,0,-1,N,0,-1,N,0,-1,0,407,288,0,0", -1);
  gtk_list_store_append(GTK_LIST_STORE(qmstore), &iter);
  gtk_list_store_set(GTK_LIST_STORE(qmstore), &iter, QM_MTYPE, MT_DATA_CCD, QM_MNAME, "DATA CCD", QM_MTEXT, "13,1,1,0,1,1,ACT_ANY,12.0,-34.0,2000.0,1000,1,U,1,1,1,1,0", -1);
  gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(qmstore), QM_MTYPE, GTK_SORT_ASCENDING);
  GtkWidget *cmb_quick_msg = gtk_combo_box_new_with_model(GTK_TREE_MODEL(qmstore));
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(cmb_quick_msg), renderer, TRUE);
  gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(cmb_quick_msg), renderer, "text", QM_MNAME);
  
  gtk_table_attach(GTK_TABLE(box_main), cmb_quick_msg, 0, 1, 0, 1, GTK_FILL|GTK_EXPAND, GTK_SHRINK, 5, 5);
  GtkWidget *ent_sendmsg = gtk_entry_new();
  gtk_entry_set_width_chars(GTK_ENTRY(ent_sendmsg), 100);
  gtk_table_attach(GTK_TABLE(box_main), ent_sendmsg, 1, 2, 0, 1, GTK_FILL|GTK_EXPAND, GTK_SHRINK, 5, 5);
  GtkWidget *btn_sendmsg = gtk_button_new_with_label("Send");
  gtk_table_attach(GTK_TABLE(box_main), btn_sendmsg, 2, 3, 0, 1, GTK_FILL|GTK_EXPAND, GTK_SHRINK, 5, 5);
  GtkWidget *scr_console = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scr_console), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_table_attach(GTK_TABLE(box_main), scr_console, 0, 3, 1, 2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 5, 5);
  GtkWidget *txv_console = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(txv_console), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(txv_console), FALSE);
  gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Console\n", -1);
  gtk_container_add(GTK_CONTAINER(scr_console), txv_console);
  GtkWidget *btn_quit = gtk_button_new_with_label("Quit");
  gtk_table_attach(GTK_TABLE(box_main), btn_quit, 2, 3, 4, 5, GTK_FILL|GTK_EXPAND, GTK_SHRINK, 5, 5);
  
  GtkWidget *dtimisc = dtimisc_new (&plc_stat, plc_chan);
  gtk_table_attach(GTK_TABLE(box_main), dtimisc, 0, 3, 3, 4, GTK_FILL|GTK_EXPAND, GTK_SHRINK, 5, 5);
  g_signal_connect_swapped(G_OBJECT(dtimisc), "watchdog-trip", G_CALLBACK(watchdog_trip), txv_console);
  g_signal_connect_swapped(G_OBJECT(dtimisc), "trapdoor-open", G_CALLBACK(trapdoor_open), txv_console);
  g_signal_connect_swapped(G_OBJECT(dtimisc), "trapdoor-close", G_CALLBACK(trapdoor_close), txv_console);
  g_signal_connect_swapped(G_OBJECT(dtimisc), "focus-fail", G_CALLBACK(focus_fail), txv_console);
  g_signal_connect_swapped(G_OBJECT(dtimisc), "power-fail", G_CALLBACK(power_fail), txv_console);
  g_signal_connect_swapped(G_OBJECT(dtimisc), "proc-complete", G_CALLBACK(proc_complete), txv_console);
  g_signal_connect_swapped(G_OBJECT(dtimisc), "plc-comm-stat", G_CALLBACK(plc_comm_stat), txv_console);
  g_signal_connect_swapped(G_OBJECT(dtimisc), "user-interact", G_CALLBACK(user_interact), txv_console);
  
  g_signal_connect(G_OBJECT(cmb_quick_msg), "changed", G_CALLBACK(quickmsg_select), ent_sendmsg);
  g_signal_connect(G_OBJECT(btn_quit), "clicked", G_CALLBACK(gtk_main_quit), NULL);
  GObject *send_msg_objects = g_object_new(G_TYPE_OBJECT, NULL);
  g_object_set_data(send_msg_objects, "ent_sendmsg", ent_sendmsg);
  g_object_set_data(send_msg_objects, "dtimisc", dtimisc);
  g_object_set_data(send_msg_objects, "txv_console", txv_console);
  g_signal_connect_swapped(G_OBJECT(btn_sendmsg), "clicked", G_CALLBACK(send_msg), send_msg_objects);
  int plc_watch_id = g_io_add_watch(plc_chan, G_IO_IN, plc_stat_update, dtimisc);
  
  gtk_widget_show_all(wnd_main);
  gtk_main();
  
  g_source_remove(plc_watch_id);
  gtk_widget_destroy(wnd_main);
  g_io_channel_unref(plc_chan);
  
  return 0;
}

void quickmsg_select(GtkWidget *cmb_quick_msg, gpointer ent_sendmsg)
{
  GtkTreeIter iter;
  gtk_combo_box_get_active_iter(GTK_COMBO_BOX(cmb_quick_msg), &iter);
  GtkTreeModel *qmstore = gtk_combo_box_get_model(GTK_COMBO_BOX(cmb_quick_msg));
  gchar *msgtext;
  gtk_tree_model_get(qmstore, &iter, QM_MTEXT, &msgtext, -1);
  if (msgtext == NULL)
  {
    fprintf(stderr, "Failed to get message text from store.");
    return;
  }
  gtk_entry_set_text(GTK_ENTRY(ent_sendmsg), msgtext);
  g_free(msgtext);
}

void send_msg(gpointer user_data)
{
  GtkWidget *ent_sendmsg = GTK_WIDGET(g_object_get_data(user_data, "ent_sendmsg"));
  GtkWidget *dtimisc = GTK_WIDGET(g_object_get_data(user_data, "dtimisc"));
  GtkWidget *txv_console =GTK_WIDGET(g_object_get_data(user_data, "txv_console"));
  const char *text = gtk_entry_get_text(GTK_ENTRY(ent_sendmsg));
  char params[strlen(text)+1];
  struct act_msg msg;
  memset(&msg, 0, sizeof(struct act_msg));
  if (sscanf(text, "%hhd,%s", &msg.mtype, params) != 2)
  {
    gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Cannot send message, failed to read message type.\n", -1);
    console_scroll_to_end(txv_console);
    return;
  }
  
  switch (msg.mtype)
  {
    case MT_QUIT:
    {
      struct act_msg_quit *msg_quit = &msg.content.msg_quit;
      if (sscanf(params, "%hhd", &msg_quit->mode_auto) != 1)
      {
        gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Invalid parameters for message MT_QUIT.\n", -1);
        console_scroll_to_end(txv_console);
        return;
      }
      break;
    }
    
    case MT_CAP:
    {
      struct act_msg_cap *msg_cap = &msg.content.msg_cap;
      if (sscanf(params, "%u,%u,%hu,%hu,%hu,%s", &msg_cap->service_provides, &msg_cap->service_needs, &msg_cap->targset_prov, &msg_cap->datapmt_prov, &msg_cap->dataccd_prov, msg_cap->version_str) != 6)
      {
        gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Invalid parameters for message MT_CAP.\n", -1);
        console_scroll_to_end(txv_console);
        return;
      }
      break;
    }
    
    case MT_STAT:
    {
      struct act_msg_stat *msg_stat = &msg.content.msg_stat;
      if (sscanf(params, "%hhd", &msg_stat->status) != 1)
      {
        gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Invalid parameters for message MT_STAT.\n", -1);
        console_scroll_to_end(txv_console);
        return;
      }
      break;
    }
    
    case MT_GUISOCK:
    {
      struct act_msg_guisock *msg_guisock = &msg.content.msg_guisock;
      if (sscanf(params, "%u,%s", &msg_guisock->gui_socket, msg_guisock->display) != 2)
      {
        gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Invalid parameters for message MT_GUISOCK.\n", -1);
        console_scroll_to_end(txv_console);
        return;
      }
      break;
    }
    
    case MT_COORD:
    {
      struct act_msg_coord *msg_coord = &msg.content.msg_coord;
      double tmpra_h, tmpha_h, tmpdec_d, tmpalt_d, tmpazm_d;
      if (sscanf(params, "%lf,%lf,%lf,%lf,%lf,%f", &tmpra_h, &tmpha_h, &tmpdec_d, &tmpalt_d, &tmpazm_d, &msg_coord->epoch) != 6)
      {
        gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Invalid parameters for message MT_COORD.\n", -1);
        console_scroll_to_end(txv_console);
        return;
      }
      convert_H_HMSMS_ra(tmpra_h, &msg_coord->ra);
      convert_H_HMSMS_ha(tmpha_h, &msg_coord->ha);
      convert_D_DMS_dec(tmpdec_d, &msg_coord->dec);
      convert_D_DMS_alt(tmpalt_d, &msg_coord->alt);
      convert_D_DMS_azm(tmpazm_d, &msg_coord->azm);
      break;
    }
    
    case MT_TIME:
    {
      struct act_msg_time *msg_time = &msg.content.msg_time;
      double tmploct, tmpunit, tmpsidt;
      if (sscanf(params, "%lf,%lf,%lf,%hd/%hhu/%hhu,%hd/%hhu/%hhu,%lf,%lf", &tmploct, &tmpunit, &tmpsidt, &msg_time->locd.year, &msg_time->locd.month, &msg_time->locd.day, &msg_time->unid.year, &msg_time->unid.month, &msg_time->unid.day, &msg_time->gjd, &msg_time->hjd) != 11)
      {
        gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Invalid parameters for message MT_TIME.\n", -1);
        console_scroll_to_end(txv_console);
        return;
      }
      convert_H_HMSMS_time(tmploct, &msg_time->loct);
      convert_H_HMSMS_time(tmpunit, &msg_time->unit);
      convert_H_HMSMS_time(tmpsidt, &msg_time->sidt);
      break;
    }
    
    case MT_ENVIRON:
    {
      struct act_msg_environ *msg_environ = &msg.content.msg_environ;
      double tmpsunalt_d, tmpmoonra_h, tmpmoondec_d, tmpwindazm_d;
      if (sscanf(params, "%hhu,%hhu,%hhu,%hhu,%hhu,%lf,%lf,%lf,%f,%lf,%hu", &msg_environ->status_active, &msg_environ->weath_ok, &msg_environ->humidity, &msg_environ->clouds, &msg_environ->rain, &tmpsunalt_d, &tmpmoonra_h, &tmpmoondec_d, &msg_environ->wind_vel, &tmpwindazm_d, &msg_environ->psf_asec) != 11)
      {
        gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Invalid parameters for message MT_ENVIRON.\n", -1);
        console_scroll_to_end(txv_console);
        return;
      }
      convert_D_DMS_alt(tmpsunalt_d, &msg_environ->sun_alt);
      convert_H_HMSMS_ra(tmpmoonra_h, &msg_environ->moon_ra);
      convert_D_DMS_dec(tmpmoondec_d, &msg_environ->moon_dec);
      convert_D_DMS_azm(tmpwindazm_d, &msg_environ->wind_azm);
      break;
    }
    
    case MT_TARG_CAP:
    {
      struct act_msg_targcap *msg_targcap = &msg.content.msg_targcap;
      double tmplimW_h, tmplimE_h,tmplimN_d, tmplimS_d, tmplimalt_d;
      if (sscanf(params, "%hhu,%hu,%lf,%lf,%lf,%lf,%lf", &msg_targcap->autoguide, &msg_targcap->targset_stage, &tmplimW_h, &tmplimE_h, &tmplimS_d, &tmplimN_d, &tmplimalt_d) != 7)
      {
        gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Invalid parameters for message MT_TARG_CAP.\n", -1);
        console_scroll_to_end(txv_console);
        return;
      }
      convert_H_HMSMS_ha(tmplimW_h, &msg_targcap->ha_lim_W);
      convert_H_HMSMS_ha(tmplimE_h, &msg_targcap->ha_lim_E);
      convert_D_DMS_dec(tmplimS_d, &msg_targcap->dec_lim_S);
      convert_D_DMS_dec(tmplimN_d, &msg_targcap->dec_lim_N);
      convert_D_DMS_alt(tmplimalt_d, &msg_targcap->alt_lim);
      break;
    }
    
    case MT_TARG_SET:
    {
      struct act_msg_targset *msg_targset = &msg.content.msg_targset;
      double tmpra_h=0.0, tmpdec_d=0.0, tmpraadj_h=0.0, tmpdecadj_d=0.0;
      if (sscanf(params, "%hhu,%hu,%hhu,%d,%[^,],%hhd,%lf,%lf,%f,%lf,%lf,%hhu,%hd,%hhu", &msg_targset->status, &msg_targset->targset_stage, &msg_targset->mode_auto, &msg_targset->targ_id, msg_targset->targ_name, &msg_targset->sky, &tmpra_h, &tmpdec_d, &msg_targset->targ_epoch, &tmpraadj_h, &tmpdecadj_d, &msg_targset->targ_cent, &msg_targset->focus_pos, &msg_targset->autoguide) != 14)
      {
        gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Invalid parameters for message MT_TARG_SET.\n", -1);
        console_scroll_to_end(txv_console);
        return;
      }
      convert_H_HMSMS_ra(tmpra_h, &msg_targset->targ_ra);
      convert_D_DMS_dec(tmpdec_d, &msg_targset->targ_dec);
      convert_H_HMSMS_ra(tmpraadj_h, &msg_targset->adj_ra);
      convert_D_DMS_dec(tmpdecadj_d, &msg_targset->adj_dec);
      break;
    }
    
    case MT_PMT_CAP:
    {
      struct act_msg_pmtcap *msg_pmtcap = &msg.content.msg_pmtcap;
      if (sscanf(params, "%[^,],%hu,%hhu,%f,%f,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d", 
        msg_pmtcap->pmt_id, &msg_pmtcap->datapmt_stage, &msg_pmtcap->pmt_mode, &msg_pmtcap->min_sample_period_s, &msg_pmtcap->max_sample_period_s, 
        msg_pmtcap->filters[0].name, &msg_pmtcap->filters[0].slot, &msg_pmtcap->filters[0].db_id,
        msg_pmtcap->filters[1].name, &msg_pmtcap->filters[1].slot, &msg_pmtcap->filters[1].db_id,
        msg_pmtcap->filters[2].name, &msg_pmtcap->filters[2].slot, &msg_pmtcap->filters[2].db_id,
        msg_pmtcap->filters[3].name, &msg_pmtcap->filters[3].slot, &msg_pmtcap->filters[3].db_id,
        msg_pmtcap->filters[4].name, &msg_pmtcap->filters[4].slot, &msg_pmtcap->filters[4].db_id,
        msg_pmtcap->filters[5].name, &msg_pmtcap->filters[5].slot, &msg_pmtcap->filters[5].db_id,
        msg_pmtcap->filters[6].name, &msg_pmtcap->filters[6].slot, &msg_pmtcap->filters[6].db_id,
        msg_pmtcap->filters[7].name, &msg_pmtcap->filters[7].slot, &msg_pmtcap->filters[7].db_id,
        msg_pmtcap->filters[8].name, &msg_pmtcap->filters[8].slot, &msg_pmtcap->filters[8].db_id,
        msg_pmtcap->filters[9].name, &msg_pmtcap->filters[9].slot, &msg_pmtcap->filters[9].db_id,
        msg_pmtcap->apertures[0].name, &msg_pmtcap->apertures[0].slot, &msg_pmtcap->apertures[0].db_id,
        msg_pmtcap->apertures[1].name, &msg_pmtcap->apertures[1].slot, &msg_pmtcap->apertures[1].db_id,
        msg_pmtcap->apertures[2].name, &msg_pmtcap->apertures[2].slot, &msg_pmtcap->apertures[2].db_id,
        msg_pmtcap->apertures[3].name, &msg_pmtcap->apertures[3].slot, &msg_pmtcap->apertures[3].db_id,
        msg_pmtcap->apertures[4].name, &msg_pmtcap->apertures[4].slot, &msg_pmtcap->apertures[4].db_id,
        msg_pmtcap->apertures[5].name, &msg_pmtcap->apertures[5].slot, &msg_pmtcap->apertures[5].db_id,
        msg_pmtcap->apertures[6].name, &msg_pmtcap->apertures[6].slot, &msg_pmtcap->apertures[6].db_id,
        msg_pmtcap->apertures[7].name, &msg_pmtcap->apertures[7].slot, &msg_pmtcap->apertures[7].db_id,
        msg_pmtcap->apertures[8].name, &msg_pmtcap->apertures[8].slot, &msg_pmtcap->apertures[8].db_id,
        msg_pmtcap->apertures[9].name, &msg_pmtcap->apertures[9].slot, &msg_pmtcap->apertures[9].db_id) != 65)
      {
        gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Invalid parameters for message MT_PMTCAP.\n", -1);
        console_scroll_to_end(txv_console);
        return;
      }
      break;
    }
    
    case MT_DATA_PMT:
    {
      struct act_msg_datapmt *msg_datapmt = &msg.content.msg_datapmt;
      double tmpra_h, tmpdec_d;
      if (sscanf(params, "%hhu,%hu,%hhu,%d,%d,%[^,],%hhd,%lf,%lf,%f,%hhu,%f,%lu,%lu,%[^,],%hhu,%d,%[^,],%hhu,%d", &msg_datapmt->status, &msg_datapmt->datapmt_stage, &msg_datapmt->mode_auto, &msg_datapmt->user_id, &msg_datapmt->targ_id, msg_datapmt->targ_name, &msg_datapmt->sky, &tmpra_h, &tmpdec_d, &msg_datapmt->targ_epoch, &msg_datapmt->pmt_mode, &msg_datapmt->sample_period_s, &msg_datapmt->prebin_num, &msg_datapmt->repetitions, msg_datapmt->filter.name, &msg_datapmt->filter.slot, &msg_datapmt->filter.db_id, msg_datapmt->aperture.name, &msg_datapmt->aperture.slot, &msg_datapmt->aperture.db_id) != 20)
      {
        gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Invalid parameters for message MT_DATA_PMT.\n", -1);
        console_scroll_to_end(txv_console);
        return;
      }
      convert_H_HMSMS_ra(tmpra_h, &msg_datapmt->targ_ra);
      convert_D_DMS_dec(tmpdec_d, &msg_datapmt->targ_dec);
      break;
    }
    
    case MT_CCD_CAP:
    {
      struct act_msg_ccdcap *msg_ccdcap = &msg.content.msg_ccdcap;
      if (sscanf(params, "%lu,%lu,%hu,%[^,],%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%[^,],%hhu,%d,%u,%hu,%hu,%hu,%hu", &msg_ccdcap->min_exp_t_msec, &msg_ccdcap->max_exp_t_msec, &msg_ccdcap->dataccd_stage, msg_ccdcap->ccd_id, 
        msg_ccdcap->filters[0].name, &msg_ccdcap->filters[0].slot, &msg_ccdcap->filters[0].db_id,
        msg_ccdcap->filters[1].name, &msg_ccdcap->filters[1].slot, &msg_ccdcap->filters[1].db_id,
        msg_ccdcap->filters[2].name, &msg_ccdcap->filters[2].slot, &msg_ccdcap->filters[2].db_id,
        msg_ccdcap->filters[3].name, &msg_ccdcap->filters[3].slot, &msg_ccdcap->filters[3].db_id,
        msg_ccdcap->filters[4].name, &msg_ccdcap->filters[4].slot, &msg_ccdcap->filters[4].db_id,
        msg_ccdcap->filters[5].name, &msg_ccdcap->filters[5].slot, &msg_ccdcap->filters[5].db_id,
        msg_ccdcap->filters[6].name, &msg_ccdcap->filters[6].slot, &msg_ccdcap->filters[6].db_id,
        msg_ccdcap->filters[7].name, &msg_ccdcap->filters[7].slot, &msg_ccdcap->filters[7].db_id,
        msg_ccdcap->filters[8].name, &msg_ccdcap->filters[8].slot, &msg_ccdcap->filters[8].db_id,
        msg_ccdcap->filters[9].name, &msg_ccdcap->filters[9].slot, &msg_ccdcap->filters[9].db_id,
        &msg_ccdcap->prebin, &msg_ccdcap->windows[0].width_px, &msg_ccdcap->windows[0].height_px, &msg_ccdcap->windows[0].origin_x, &msg_ccdcap->windows[0].origin_y) != 39)
      {
        gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Invalid parameters for message MT_CCDCAP.\n", -1);
        console_scroll_to_end(txv_console);
        return;
      }
      break;
    }
    
    case MT_DATA_CCD:
    {
      struct act_msg_dataccd *msg_dataccd = &msg.content.msg_dataccd;
      double tmpra_h, tmpdec_d;
      if (sscanf(params, "%hhu,%hu,%hhu,%d,%d,%[^,],%lf,%lf,%f,%lu,%lu,%[^,],%hhu,%d,%hhu,%hhu,%hhu", &msg_dataccd->status, &msg_dataccd->dataccd_stage, &msg_dataccd->mode_auto, &msg_dataccd->user_id, &msg_dataccd->targ_id, msg_dataccd->targ_name, &tmpra_h, &tmpdec_d, &msg_dataccd->targ_epoch, &msg_dataccd->exp_t_msec, &msg_dataccd->repetitions, msg_dataccd->filter.name, &msg_dataccd->filter.slot, &msg_dataccd->filter.db_id, &msg_dataccd->frame_transfer, &msg_dataccd->prebin, &msg_dataccd->window_mode_num) != 17)
      {
        gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Invalid parameters for message MT_DATA_CCD.\n", -1);
        console_scroll_to_end(txv_console);
        return;
      }
      convert_H_HMSMS_ra(tmpra_h, &msg_dataccd->targ_ra);
      convert_D_DMS_dec(tmpdec_d, &msg_dataccd->targ_dec);
      break;
    }
    
    default:
      gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Cannot send message, invalid message type.\n", -1);
      console_scroll_to_end(txv_console);
      return;
  }

  gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Sending message: ", -1);
  gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), text, -1);
  gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "\n", -1);
  console_scroll_to_end(txv_console);
  dtimisc_process_msg(dtimisc, &msg);
}

void console_scroll_to_end(GtkWidget *txv_console)
{
  GtkTextMark *mk = gtk_text_buffer_get_insert (gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)));
  gtk_text_view_scroll_to_mark (GTK_TEXT_VIEW (txv_console), mk, 0.0, FALSE, 0.0, 0.0);
}

gboolean plc_stat_update(GIOChannel *plc_chan, GIOCondition condition, gpointer dtimisc)
{
  if (condition != G_IO_IN)
    return TRUE;
  char tmp_plcdrv_stat;
  GError *error = NULL;
  unsigned int num_bytes;
  int ret = g_io_channel_read_chars (plc_chan, &tmp_plcdrv_stat, 1, &num_bytes, &error);
  if (error != NULL)
  {
    fprintf(stderr, "An error occurred while attempting to read PLC driver status (%d - %s)\n", error->code, error->message);
    g_error_free(error);
    return TRUE;
  }
  if ((ret != G_IO_STATUS_NORMAL) || (num_bytes != 1))
  {
    fprintf(stderr, "An unknown error occurred while attempting to read the PLC driver status.\n");
    return TRUE;
  }
  if ((tmp_plcdrv_stat & PLC_COMM_OK) == 0)
    fprintf(stderr, "PLC communications failure.\n");
  if ((tmp_plcdrv_stat & NEW_STAT_AVAIL) == 0)
    return TRUE;
  int plc_fd = g_io_channel_unix_get_fd (plc_chan);
  struct plc_status new_stat;
  ret = ioctl(plc_fd, IOCTL_GET_STATUS, &new_stat);
  if (ret < 0)
  {
    fprintf(stderr, "Error reading PLC status - %s (%d).\n", strerror(errno), errno);
    return TRUE;
  }
  dtimisc_update(GTK_WIDGET(dtimisc), &new_stat);
  
  return TRUE;
}

void watchdog_trip(gpointer txv_console)
{
  gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Watchdog tripped\n", -1);
  console_scroll_to_end(txv_console);
}

void trapdoor_open(gpointer txv_console)
{
  gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Trapdoor open\n", -1);
  console_scroll_to_end(txv_console);
}

void trapdoor_close(gpointer txv_console)
{
  gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Trapdoor closed\n", -1);
  console_scroll_to_end(txv_console);
}

void focus_fail(gpointer txv_console)
{
  gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Focus failure\n", -1);
  console_scroll_to_end(txv_console);
}

void power_fail(gpointer txv_console)
{
  gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "Power failure\n", -1);
  console_scroll_to_end(txv_console);
}

void proc_complete(gpointer txv_console, gint status)
{
  char text[256];
  sprintf(text, "Procedure complete: %d\n", status);
  gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), text, -1);
  console_scroll_to_end(txv_console);
}

void plc_comm_stat(gpointer txv_console, gboolean status)
{
  char text[256];
  sprintf(text, "PLC communications status: %s\n", status > 0 ? "OK" : "Fail");
  gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), text, -1);
  console_scroll_to_end(txv_console);
}

void user_interact(gpointer txv_console)
{
  gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(txv_console)), "User interaction\n", -1);
  console_scroll_to_end(txv_console);
}

