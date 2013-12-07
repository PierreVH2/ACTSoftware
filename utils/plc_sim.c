/// Simulator programme ACT PLC using PLC driver - requires that PLC driver be loaded and character device created

#define PLC_SIM

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include "act_plc.h"
#include "plc_definitions.h"

#define TABLE_PADDING 3

struct command_objects
{
  int plc_fd;
  
  GtkWidget *lbl_cmd_str;
  GtkWidget *lbl_dome_pos;
  GtkWidget *lbl_dome_max_flop;
  GtkWidget *lbl_dome_min_flop;
  GtkWidget *lbl_dome_offs;
  GtkWidget *evb_instr_shutt, *evb_acqreset, *evb_watchdog;
  GtkWidget *evb_dropout_open, *evb_dropout_close;
  GtkWidget *evb_shutter_open, *evb_shutter_close;
  GtkWidget *evb_dome_guide, *evb_dome_move_left, *evb_dome_move, *evb_set_dome_offs;
  GtkWidget *evb_foc_go, *evb_foc_out, *evb_foc_in, *evb_foc_reset;
  GtkWidget *evb_aper_init, *evb_aper_go, *evb_aper_reset;
  GtkWidget *evb_filt_init, *evb_filt_go, *evb_filt_reset;
  GtkWidget *evb_eht_lo, *evb_eht_hi, *evb_acqmir_reset, *evb_acqmir_inbeam;
  GtkWidget *lbl_filt_num, *lbl_aper_num;
  GtkWidget *lbl_foc_pos;
};

char G_plc_stat[PLC_STAT_RESP_LEN+1];
char *G_stat_char = NULL;
GdkColor G_col_green;

static char hexchar2int(char c)
{
  if ((c>='0') && (c<='9'))
    return c - '0';
  if ((c>='A') && (c<='F'))
    return c - 'A' + 10;
  if ((c>='a') && (c<='f'))
    return c - 'a' + 10;
  return -1;
}

static char int2hexchar(char n)
{
  if (n < 0)
    return -1;
  if (n < 0xA)
    return n + '0';
  if (n <= 0xF)
    return n - 10 + 'A';
  return -1;
}

static int calc_fcs(char *str, int length)
{
  int A=0, l;
  for (l=0; l<length ; l++)           // perform an exclusive or on each command string character in succession
    A = A ^ str[l];
  return A;
}

void create_init_plc_stat()
{
  memset(G_plc_stat, '0', PLC_STAT_RESP_LEN);
  G_plc_stat[PLC_STAT_RESP_LEN] = '\0';
  strncpy(G_plc_stat, PLC_STAT_REQ, PLC_STAT_REQ_LEN);
  G_plc_stat[STAT_ENDC_OFFS] = '0';
  G_plc_stat[STAT_ENDC_OFFS+1] = '0';
  strncpy(&G_plc_stat[STAT_DOME_POS_OFFS], "0000", STAT_DOME_POS_LEN);
  G_plc_stat[STAT_DROPOUT_OFFS] = int2hexchar(STAT_DSHUTT_CLOSED_MASK);
  G_plc_stat[STAT_SHUTTER_OFFS] = int2hexchar(STAT_DSHUTT_CLOSED_MASK);
  G_plc_stat[STAT_DOME_STAT_OFFS] = '0';
  G_plc_stat[STAT_APER_STAT_OFFS] = int2hexchar(STAT_APER_INIT_MASK | STAT_APER_CENT_MASK);
  G_plc_stat[STAT_FILT_STAT_OFFS] = int2hexchar(STAT_APER_INIT_MASK | STAT_APER_CENT_MASK);
  G_plc_stat[STAT_ACQMIR_OFFS] = int2hexchar(STAT_ACQMIR_VIEW_MASK);
  G_plc_stat[STAT_INSTR_SHUTT_OFFS] = '0';
  strncpy(&G_plc_stat[STAT_FOCUS_REG_OFFS], "8350", STAT_FOCUS_POS_LEN);
  G_plc_stat[STAT_FOC_STAT_OFFS1] = int2hexchar(STAT_FOC_SLOT_MASK | STAT_FOC_REF_MASK | STAT_FOC_OUT_MASK);
  strncpy(&G_plc_stat[STAT_DOME_MAX_FLOP_OFFS], "05", CNTR_DOME_FLOP_LEN);
  strncpy(&G_plc_stat[STAT_DOME_MIN_FLOP_OFFS], "10", CNTR_DOME_FLOP_LEN);
  snprintf(&G_plc_stat[STAT_FCS_OFFS], 5, "%02X*\r", calc_fcs(G_plc_stat,STAT_FCS_OFFS));
}

void update_stat_disp(gpointer user_data)
{
  gtk_label_set_text(GTK_LABEL(user_data), G_plc_stat);
}

void set_stat_char(gpointer user_data)
{
  G_stat_char = (char *)user_data;
}

void set_stat_val(GtkWidget *button, gpointer user_data)
{
  if (G_stat_char == NULL)
    return;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)))
    *G_stat_char = int2hexchar((int)user_data | hexchar2int(*G_stat_char));
  else
    *G_stat_char = int2hexchar(hexchar2int(*G_stat_char) & (~ (int) user_data));
  G_stat_char = NULL;
}

void set_dome_azmoffs(GtkWidget *button, gpointer user_data)
{
  char tmpstr[STAT_DOME_POS_LEN+1];
  snprintf(tmpstr, STAT_DOME_POS_LEN+1, STAT_DOME_POS_FMT, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(button)));
  memcpy(&G_plc_stat[(unsigned int)user_data], tmpstr, STAT_DOME_POS_LEN);
}

void set_dome_flop(GtkWidget *button, gpointer user_data)
{
  char tmpstr[STAT_DOME_FLOP_LEN+1];
  snprintf(tmpstr, STAT_DOME_FLOP_LEN+1, STAT_DOME_FLOP_FMT, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(button)));
  memcpy(&G_plc_stat[(unsigned int)user_data], tmpstr, STAT_DOME_FLOP_LEN);
}

void set_focus_pos(GtkWidget *button)
{
  char tmpstr[STAT_FOCUS_POS_LEN];
  int value = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(button));
  if (value < 0)
    G_plc_stat[STAT_FOCUS_REG_OFFS] = '8';
  else
    G_plc_stat[STAT_FOCUS_REG_OFFS] = '0';
  snprintf(tmpstr, STAT_FOCUS_POS_LEN, STAT_FOCUS_POS_FMT, abs(value));
  memcpy(&G_plc_stat[STAT_FOCUS_POS_OFFS], tmpstr, STAT_FOCUS_POS_LEN-1);
}

void set_filtfilt_num(GtkWidget *button, gpointer user_data)
{
  G_plc_stat[(unsigned int)user_data] = '0' + gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(button));
}

gboolean read_command(gpointer user_data)
{
  struct command_objects *cmd_objs = (struct command_objects *)user_data;
  
  char tmp_cmd[PLC_CMD_LEN+1] = { '0' };
  char tmp_val;
  int ret = ioctl(cmd_objs->plc_fd, IOCTL_GET_SIM_CMD, tmp_cmd);
  if (ret < 0)
  {
    fprintf(stderr, "Error communicating with device driver (%d - %s)\n", ret, strerror(-ret));
    return TRUE;
  }

  char tmpstr[10];
  gtk_label_set_text(GTK_LABEL(cmd_objs->lbl_cmd_str), tmp_cmd);
  snprintf(tmpstr, CNTR_DOME_POS_LEN+1, "%s", &tmp_cmd[CNTR_DOME_POS_OFFS]);
  gtk_label_set_text(GTK_LABEL(cmd_objs->lbl_dome_pos), tmpstr);
  snprintf(tmpstr, CNTR_DOME_FLOP_LEN+1, "%s", &tmp_cmd[CNTR_DOME_MAX_FLOP_OFFS]);
  gtk_label_set_text(GTK_LABEL(cmd_objs->lbl_dome_max_flop), tmpstr);
  snprintf(tmpstr, CNTR_DOME_FLOP_LEN+1, "%s", &tmp_cmd[CNTR_DOME_MIN_FLOP_OFFS]);
  gtk_label_set_text(GTK_LABEL(cmd_objs->lbl_dome_min_flop), tmpstr);
  snprintf(tmpstr, CNTR_DOME_POS_LEN+1, "%s",&tmp_cmd[CNTR_DOME_OFFS_OFFS]);
  gtk_label_set_text(GTK_LABEL(cmd_objs->lbl_dome_offs), tmpstr);
  tmpstr[0] = tmp_cmd[CNTR_FOCUS_REG_OFFS] == CNTR_FOCUS_REG_OUT_VAL ? '-' : ' ';
  snprintf(&tmpstr[1], CNTR_FOCUS_POS_LEN+1, "%s",&tmp_cmd[CNTR_FOCUS_POS_OFFS]);
  gtk_label_set_text(GTK_LABEL(cmd_objs->lbl_foc_pos), tmpstr);
  snprintf(tmpstr, 2, "%c", tmp_cmd[CNTR_FILT_NUM_OFFS+1]);
  gtk_label_set_text(GTK_LABEL(cmd_objs->lbl_filt_num), tmpstr);
  snprintf(tmpstr, 2, "%c", tmp_cmd[CNTR_APER_NUM_OFFS+1]);
  gtk_label_set_text(GTK_LABEL(cmd_objs->lbl_aper_num), tmpstr);
  tmp_val = hexchar2int(tmp_cmd[CNTR_INSTR_SHUTT_OFFS]);
  if ((tmp_val & CNTR_INSTR_SHUTT_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_instr_shutt,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_instr_shutt,GTK_STATE_NORMAL,NULL);
  if ((tmp_val & CNTR_ACQRESET_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_acqreset,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_acqreset,GTK_STATE_NORMAL,NULL);
  if ((tmp_val & CNTR_WATCHDOG_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_watchdog,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_watchdog,GTK_STATE_NORMAL,NULL);
  tmp_val = hexchar2int(tmp_cmd[CNTR_DROPOUT_OFFS]);
  if ((tmp_val & CNTR_DSHUTT_OPEN_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_dropout_open,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_dropout_open,GTK_STATE_NORMAL,NULL);
  if ((tmp_val & CNTR_DSHUTT_CLOSE_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_dropout_close,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_dropout_close,GTK_STATE_NORMAL,NULL);
  tmp_val = hexchar2int(tmp_cmd[CNTR_SHUTTER_OFFS]);
  if ((tmp_val & CNTR_DSHUTT_OPEN_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_shutter_open,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_shutter_open,GTK_STATE_NORMAL,NULL);
  if ((tmp_val & CNTR_DSHUTT_CLOSE_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_shutter_close,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_shutter_close,GTK_STATE_NORMAL,NULL);
  tmp_val = hexchar2int(tmp_cmd[CNTR_DOME_STAT_OFFS]);
  if ((tmp_val & CNTR_DOME_GUIDE_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_dome_guide,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_dome_guide,GTK_STATE_NORMAL,NULL);
  if ((tmp_val & CNTR_DOME_MOVE_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_dome_move,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_dome_move,GTK_STATE_NORMAL,NULL);
  if ((tmp_val & CNTR_DOME_MOVE_LEFT_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_dome_move_left,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_dome_move_left,GTK_STATE_NORMAL,NULL);
  if ((tmp_val & CNTR_DOME_SET_OFFS_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_set_dome_offs,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_set_dome_offs,GTK_STATE_NORMAL,NULL);
  if ((tmp_val & CNTR_DOME_GUIDE_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_dome_guide,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_dome_guide,GTK_STATE_NORMAL,NULL);
  tmp_val = hexchar2int(tmp_cmd[CNTR_FOCUS_OFFS]);
  if ((tmp_val & CNTR_FOC_GO_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_foc_go,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_foc_go,GTK_STATE_NORMAL,NULL);
  if ((tmp_val & CNTR_FOC_OUT_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_foc_out,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_foc_out,GTK_STATE_NORMAL,NULL);
  if ((tmp_val & CNTR_FOC_IN_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_foc_in,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_foc_in,GTK_STATE_NORMAL,NULL);
  if ((tmp_val & CNTR_FOC_RESET_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_foc_reset,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_foc_reset,GTK_STATE_NORMAL,NULL);
  tmp_val = hexchar2int(tmp_cmd[CNTR_APER_STAT_OFFS]);
  if ((tmp_val & CNTR_APER_INIT_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_aper_init,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_aper_init,GTK_STATE_NORMAL,NULL);
  if ((tmp_val & CNTR_APER_GO_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_aper_go,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_aper_go,GTK_STATE_NORMAL,NULL);
  if ((tmp_val & CNTR_APER_RESET_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_aper_reset,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_aper_reset,GTK_STATE_NORMAL,NULL);
  tmp_val = hexchar2int(tmp_cmd[CNTR_FILT_STAT_OFFS]);
  if ((tmp_val & CNTR_APER_INIT_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_filt_init,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_filt_init,GTK_STATE_NORMAL,NULL);
  if ((tmp_val & CNTR_APER_GO_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_filt_go,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_filt_go,GTK_STATE_NORMAL,NULL);
  if ((tmp_val & CNTR_APER_RESET_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_filt_reset,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_filt_reset,GTK_STATE_NORMAL,NULL);
  tmp_val = hexchar2int(tmp_cmd[CNTR_ACQMIR_EHT_OFFS]);
  if ((tmp_val & CNTR_EHT_LO_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_eht_lo,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_eht_lo,GTK_STATE_NORMAL,NULL);
  if ((tmp_val & CNTR_EHT_HI_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_eht_hi,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_eht_hi,GTK_STATE_NORMAL,NULL);
  if ((tmp_val & CNTR_ACQMIR_RESET_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_acqmir_reset,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_acqmir_reset,GTK_STATE_NORMAL,NULL);
  if ((tmp_val & CNTR_ACQMIR_INBEAM_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_acqmir_inbeam,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_acqmir_inbeam,GTK_STATE_NORMAL,NULL);
  /*if ((tmp_cmd[CNTR_ACQMIR_EHT_OFFS] & CNTR_EHT_LO_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_eht_lo,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_eht_lo,GTK_STATE_NORMAL,NULL);
  if ((tmp_cmd[CNTR_ACQMIR_EHT_OFFS] & CNTR_EHT_HI_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_eht_hi,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_eht_hi,GTK_STATE_NORMAL,NULL);
  if ((tmp_cmd[CNTR_ACQMIR_EHT_OFFS] & CNTR_ACQMIR_RESET_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_acqmir_reset,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_acqmir_reset,GTK_STATE_NORMAL,NULL);
  if ((tmp_cmd[CNTR_ACQMIR_EHT_OFFS] & CNTR_ACQMIR_INBEAM_MASK) > 0)
    gtk_widget_modify_bg(cmd_objs->evb_acqmir_inbeam,GTK_STATE_NORMAL,&G_col_green);
  else
    gtk_widget_modify_bg(cmd_objs->evb_acqmir_inbeam,GTK_STATE_NORMAL,NULL);*/
  
  return TRUE;
}

void send_stat(gpointer user_data)
{
  int plc_fd = *((int *) user_data);
  int ret = ioctl(plc_fd, IOCTL_SET_SIM_STATUS, G_plc_stat);
  if (ret < 0)
    fprintf(stderr, "Cannot set simulated PLC status (%d - %s)\n", ret, strerror(-ret));
}

int main(int argc, char **argv)
{
  gtk_init(&argc, &argv);
  
  int plc_fd = open("/dev/"PLC_DEVICE_NAME, O_RDWR);
  if (plc_fd < 0)
  {
    printf("Error opening serial terminal to PLC (%d - %s)\n", errno, strerror(errno));
    return 1;
  }
  
  create_init_plc_stat();
  
  GtkWidget *wnd_main = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  g_signal_connect_swapped(wnd_main,"destroy",G_CALLBACK(gtk_main_quit),NULL);
  GtkWidget *box_main = gtk_hbox_new(FALSE,5);
  gtk_container_add(GTK_CONTAINER(wnd_main), box_main);

  GtkWidget *frm_status = gtk_frame_new("Status");
  gtk_box_pack_start(GTK_BOX(box_main), frm_status, TRUE,TRUE,5);
  GtkWidget *box_status = gtk_table_new(10,4,FALSE);
  gtk_container_add(GTK_CONTAINER(frm_status), box_status);
  GtkWidget *lbl_stat_str = gtk_label_new(G_plc_stat);
  gtk_table_attach(GTK_TABLE(box_status), lbl_stat_str, 0,10,10,11, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  GtkWidget *btn_plc_send = gtk_button_new_with_label("Send");
  gtk_table_attach(GTK_TABLE(box_status), btn_plc_send, 0,10,11,12, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(btn_plc_send), "clicked", G_CALLBACK(send_stat), &plc_fd);

  gtk_table_attach(GTK_TABLE(box_status), gtk_label_new("Dome azm"), 0,1,0,1, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  GtkWidget *spn_domeazm = gtk_spin_button_new_with_range(0,3600,1);
  gtk_table_attach(GTK_TABLE(box_status), spn_domeazm, 1,2,0,1, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect(G_OBJECT(spn_domeazm), "value-changed", G_CALLBACK(set_dome_azmoffs), (unsigned char*)STAT_DOME_POS_OFFS);
  g_signal_connect_swapped(G_OBJECT(spn_domeazm), "value-changed", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_shutter_open = gtk_check_button_new_with_label("Dome shutter open");
  gtk_table_attach(GTK_TABLE(box_status), chk_shutter_open, 0,2,1,2, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_shutter_open), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_SHUTTER_OFFS]);
  g_signal_connect(G_OBJECT(chk_shutter_open), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_DSHUTT_OPEN_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_shutter_open), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_shutter_closed = gtk_check_button_new_with_label("Dome shutter closed");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_shutter_closed), TRUE);
  gtk_table_attach(GTK_TABLE(box_status), chk_shutter_closed, 0,2,2,3, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_shutter_closed), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_SHUTTER_OFFS]);
  g_signal_connect(G_OBJECT(chk_shutter_closed), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_DSHUTT_CLOSED_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_shutter_closed), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_shutter_moving = gtk_check_button_new_with_label("Dome shutter moving");
  gtk_table_attach(GTK_TABLE(box_status), chk_shutter_moving, 0,2,3,4, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_shutter_moving), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_SHUTTER_OFFS]);
  g_signal_connect(G_OBJECT(chk_shutter_moving), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_DSHUTT_MOVING_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_shutter_moving), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_dropout_open = gtk_check_button_new_with_label("Dome dropout open");
  gtk_table_attach(GTK_TABLE(box_status), chk_dropout_open, 0,2,4,5, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_dropout_open), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_DROPOUT_OFFS]);
  g_signal_connect(G_OBJECT(chk_dropout_open), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_DSHUTT_OPEN_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_dropout_open), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_dropout_closed = gtk_check_button_new_with_label("Dome dropout closed");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_dropout_closed), TRUE);
  gtk_table_attach(GTK_TABLE(box_status), chk_dropout_closed, 0,2,5,6, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_dropout_closed), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_DROPOUT_OFFS]);
  g_signal_connect(G_OBJECT(chk_dropout_closed), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_DSHUTT_CLOSED_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_dropout_closed), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);  
  GtkWidget *chk_dropout_moving = gtk_check_button_new_with_label("Dome dropout moving");
  gtk_table_attach(GTK_TABLE(box_status), chk_dropout_moving, 0,2,6,7, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_dropout_moving), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_DROPOUT_OFFS]);
  g_signal_connect(G_OBJECT(chk_dropout_moving), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_DSHUTT_MOVING_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_dropout_moving), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_instrshutt_open = gtk_check_button_new_with_label("Instrshutt open");
  gtk_table_attach(GTK_TABLE(box_status), chk_instrshutt_open, 0,2,7,8, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_instrshutt_open), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_INSTR_SHUTT_OFFS]);
  g_signal_connect(G_OBJECT(chk_instrshutt_open), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_INSTR_SHUTT_OPEN_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_instrshutt_open), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_trapdoor_open = gtk_check_button_new_with_label("Trapdoor open");
  gtk_table_attach(GTK_TABLE(box_status), chk_trapdoor_open, 0,2,8,9, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_trapdoor_open), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_DOME_STAT_OFFS]);
  g_signal_connect(G_OBJECT(chk_trapdoor_open), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_TRAPDOOR_OPEN_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_trapdoor_open), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_dome_moving = gtk_check_button_new_with_label("Dome moving");
  gtk_table_attach(GTK_TABLE(box_status), chk_dome_moving, 0,2,9,10, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_dome_moving), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_DOME_STAT_OFFS]);
  g_signal_connect(G_OBJECT(chk_dome_moving), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_DOME_MOVING_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_dome_moving), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  
  GtkWidget *chk_aper_init = gtk_check_button_new_with_label("Aperture initialised");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_aper_init), TRUE);
  gtk_table_attach(GTK_TABLE(box_status), chk_aper_init, 2,4,0,1, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_aper_init), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_APER_STAT_OFFS]);
  g_signal_connect(G_OBJECT(chk_aper_init), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_APER_INIT_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_aper_init), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_aper_cent = gtk_check_button_new_with_label("Aperture centred");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_aper_cent), TRUE);
  gtk_table_attach(GTK_TABLE(box_status), chk_aper_cent, 2,4,1,2, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_aper_cent), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_APER_STAT_OFFS]);
  g_signal_connect(G_OBJECT(chk_aper_cent), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_APER_CENT_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_aper_cent), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_aper_moving = gtk_check_button_new_with_label("Aperture moving");
  gtk_table_attach(GTK_TABLE(box_status), chk_aper_moving, 2,4,2,3, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_aper_moving), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_APER_STAT_OFFS]);
  g_signal_connect(G_OBJECT(chk_aper_moving), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_APER_MOVING_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_aper_moving), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_filt_init = gtk_check_button_new_with_label("Filter initialised");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_filt_init), TRUE);
  gtk_table_attach(GTK_TABLE(box_status), chk_filt_init, 2,4,3,4, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_filt_init), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_FILT_STAT_OFFS]);
  g_signal_connect(G_OBJECT(chk_filt_init), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_APER_INIT_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_filt_init), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_filt_cent = gtk_check_button_new_with_label("Filter centred");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_filt_cent), TRUE);
  gtk_table_attach(GTK_TABLE(box_status), chk_filt_cent, 2,4,4,5, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_filt_cent), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_FILT_STAT_OFFS]);
  g_signal_connect(G_OBJECT(chk_filt_cent), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_APER_CENT_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_filt_cent), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_filt_moving = gtk_check_button_new_with_label("Filter moving");
  gtk_table_attach(GTK_TABLE(box_status), chk_filt_moving, 2,4,5,6, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_filt_moving), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_FILT_STAT_OFFS]);
  g_signal_connect(G_OBJECT(chk_filt_moving), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_APER_MOVING_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_filt_moving), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_acqmir_view = gtk_check_button_new_with_label("Acqmir View");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_acqmir_view), TRUE);
  gtk_table_attach(GTK_TABLE(box_status), chk_acqmir_view, 2,4,6,7, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_acqmir_view), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_ACQMIR_OFFS]);
  g_signal_connect(G_OBJECT(chk_acqmir_view), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_ACQMIR_VIEW_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_acqmir_view), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_acqmir_meas = gtk_check_button_new_with_label("Acqmir Measure");
  gtk_table_attach(GTK_TABLE(box_status), chk_acqmir_meas, 2,4,7,8, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_acqmir_meas), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_ACQMIR_OFFS]);
  g_signal_connect(G_OBJECT(chk_acqmir_meas), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_ACQMIR_MEAS_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_acqmir_meas), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_acqmir_moving = gtk_check_button_new_with_label("Acqmir Moving");
  gtk_table_attach(GTK_TABLE(box_status), chk_acqmir_moving, 2,4,8,9, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_acqmir_moving), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_ACQMIR_OFFS]);
  g_signal_connect(G_OBJECT(chk_acqmir_moving), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_ACQMIR_MOVING_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_acqmir_moving), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  gtk_table_attach(GTK_TABLE(box_status), gtk_label_new("Focus pos"), 2,3,9,10, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  GtkWidget *spn_focus_pos = gtk_spin_button_new_with_range(-660,140,1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spn_focus_pos), -350);
  gtk_table_attach(GTK_TABLE(box_status), spn_focus_pos, 3,4,9,10, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect(G_OBJECT(spn_focus_pos), "value-changed", G_CALLBACK(set_focus_pos), NULL);
  g_signal_connect_swapped(G_OBJECT(spn_focus_pos), "value-changed", G_CALLBACK(update_stat_disp), lbl_stat_str);  
  
  GtkWidget *chk_focus_slot = gtk_check_button_new_with_label("Focus slot");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_focus_slot), TRUE);
  gtk_table_attach(GTK_TABLE(box_status), chk_focus_slot, 4,6,0,1, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING); 
  g_signal_connect_swapped(G_OBJECT(chk_focus_slot), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_FOC_STAT_OFFS2]);
  g_signal_connect(G_OBJECT(chk_focus_slot), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_FOC_SLOT_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_focus_slot), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_focus_ref = gtk_check_button_new_with_label("Focus ref");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_focus_ref), TRUE);
  gtk_table_attach(GTK_TABLE(box_status), chk_focus_ref, 4,6,1,2, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_focus_ref), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_FOC_STAT_OFFS2]);
  g_signal_connect(G_OBJECT(chk_focus_ref), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_FOC_REF_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_focus_ref), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_focus_out = gtk_check_button_new_with_label("Focus out");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_focus_out), TRUE);
  gtk_table_attach(GTK_TABLE(box_status), chk_focus_out, 4,6,2,3, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_focus_out), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_FOC_STAT_OFFS2]);
  g_signal_connect(G_OBJECT(chk_focus_out), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_FOC_OUT_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_focus_out), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_focus_in = gtk_check_button_new_with_label("Focus in");
  gtk_table_attach(GTK_TABLE(box_status), chk_focus_in, 4,6,3,4, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_focus_in), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_FOC_STAT_OFFS2]);
  g_signal_connect(G_OBJECT(chk_focus_in), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_FOC_IN_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_focus_in), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_focus_moving = gtk_check_button_new_with_label("Focus moving");
  gtk_table_attach(GTK_TABLE(box_status), chk_focus_moving, 4,6,4,5, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_focus_moving), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_FOC_STAT_OFFS1]);
  g_signal_connect(G_OBJECT(chk_focus_moving), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_FOC_MOVING_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_focus_moving), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_focus_init = gtk_check_button_new_with_label("Focus init");
  gtk_table_attach(GTK_TABLE(box_status), chk_focus_init, 4,6,5,6, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_focus_init), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_FOC_STAT_OFFS1]);
  g_signal_connect(G_OBJECT(chk_focus_init), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_FOC_INIT_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_focus_init), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_focus_stall = gtk_check_button_new_with_label("Focus stall");
  gtk_table_attach(GTK_TABLE(box_status), chk_focus_stall, 4,6,6,7, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_focus_stall), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_FOC_STAT_OFFS1]);
  g_signal_connect(G_OBJECT(chk_focus_stall), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_FOC_STALL_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_focus_stall), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_power_fail = gtk_check_button_new_with_label("Power failure");
  gtk_table_attach(GTK_TABLE(box_status), chk_power_fail, 4,6,7,8, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_power_fail), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_CRIT_ERR_OFFS]);
  g_signal_connect(G_OBJECT(chk_power_fail), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_POWER_FAIL_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_power_fail), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_watchdog = gtk_check_button_new_with_label("Watchdog trip");
  gtk_table_attach(GTK_TABLE(box_status), chk_watchdog, 4,6,8,9, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_watchdog), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_CRIT_ERR_OFFS]);
  g_signal_connect(G_OBJECT(chk_watchdog), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_WATCHDOG_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_watchdog), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);
  GtkWidget *chk_eht_man_off = gtk_check_button_new_with_label("EHT manual off");
  gtk_table_attach(GTK_TABLE(box_status), chk_eht_man_off, 4,6,9,10, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_eht_man_off), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_EHT_OFFS]);
  g_signal_connect(G_OBJECT(chk_eht_man_off), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_EHT_MAN_OFF_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_eht_man_off), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);  
  
  GtkWidget *chk_eht_lo = gtk_check_button_new_with_label("EHT low");
  gtk_table_attach(GTK_TABLE(box_status), chk_eht_lo, 6,8,0,1, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_eht_lo), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_EHT_OFFS]);
  g_signal_connect(G_OBJECT(chk_eht_lo), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_EHT_LO_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_eht_lo), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);  
  GtkWidget *chk_eht_hi = gtk_check_button_new_with_label("EHT high");
  gtk_table_attach(GTK_TABLE(box_status), chk_eht_hi, 6,8,1,2, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_eht_hi), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_EHT_OFFS]);
  g_signal_connect(G_OBJECT(chk_eht_hi), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_EHT_HI_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_eht_hi), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);  
  GtkWidget *chk_hs_foc_in = gtk_check_button_new_with_label("HS focus in");
  gtk_table_attach(GTK_TABLE(box_status), chk_hs_foc_in, 6,8,2,3, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_hs_foc_in), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_HANDSET_OFFS1]);
  g_signal_connect(G_OBJECT(chk_hs_foc_in), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_HANDSET_FOC_IN_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_hs_foc_in), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);  
  GtkWidget *chk_hs_foc_out = gtk_check_button_new_with_label("HS focus out");
  gtk_table_attach(GTK_TABLE(box_status), chk_hs_foc_out, 6,8,3,4, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_hs_foc_out), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_HANDSET_OFFS1]);
  g_signal_connect(G_OBJECT(chk_hs_foc_out), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_HANDSET_FOC_OUT_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_hs_foc_out), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);  
  GtkWidget *chk_hs_slew = gtk_check_button_new_with_label("HS slew");
  gtk_table_attach(GTK_TABLE(box_status), chk_hs_slew, 6,8,4,5, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_hs_slew), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_HANDSET_OFFS1]);
  g_signal_connect(G_OBJECT(chk_hs_slew), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_HANDSET_SLEW_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_hs_slew), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);  
  GtkWidget *chk_hs_guide = gtk_check_button_new_with_label("HS guide");
  gtk_table_attach(GTK_TABLE(box_status), chk_hs_guide, 6,8,5,6, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_hs_guide), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_HANDSET_OFFS1]);
  g_signal_connect(G_OBJECT(chk_hs_guide), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_HANDSET_GUIDE_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_hs_guide), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);  
  GtkWidget *chk_hs_south = gtk_check_button_new_with_label("HS South");
  gtk_table_attach(GTK_TABLE(box_status), chk_hs_south, 6,8,6,7, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_hs_south), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_HANDSET_OFFS2]);
  g_signal_connect(G_OBJECT(chk_hs_south), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_HANDSET_SOUTH_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_hs_south), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);  
  GtkWidget *chk_hs_north = gtk_check_button_new_with_label("HS North");
  gtk_table_attach(GTK_TABLE(box_status), chk_hs_north, 6,8,7,8, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_hs_north), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_HANDSET_OFFS2]);
  g_signal_connect(G_OBJECT(chk_hs_north), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_HANDSET_NORTH_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_hs_north), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);  
  GtkWidget *chk_hs_east = gtk_check_button_new_with_label("HS East");
  gtk_table_attach(GTK_TABLE(box_status), chk_hs_east, 6,8,8,9, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_hs_east), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_HANDSET_OFFS2]);
  g_signal_connect(G_OBJECT(chk_hs_east), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_HANDSET_EAST_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_hs_east), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);  
  GtkWidget *chk_hs_west = gtk_check_button_new_with_label("HS West");
  gtk_table_attach(GTK_TABLE(box_status), chk_hs_west, 6,8,9,10, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect_swapped(G_OBJECT(chk_hs_west), "toggled", G_CALLBACK(set_stat_char), &G_plc_stat[STAT_HANDSET_OFFS2]);
  g_signal_connect(G_OBJECT(chk_hs_west), "toggled", G_CALLBACK(set_stat_val), (char *)STAT_HANDSET_WEST_MASK);
  g_signal_connect_swapped(G_OBJECT(chk_hs_west), "toggled", G_CALLBACK(update_stat_disp), lbl_stat_str);  
  
  gtk_table_attach(GTK_TABLE(box_status), gtk_label_new("Aperture"), 8,9,0,2, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  GtkWidget *spn_aper_num = gtk_spin_button_new_with_range(0,9,1);
  gtk_table_attach(GTK_TABLE(box_status), spn_aper_num, 9,10,0,2, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect(G_OBJECT(spn_aper_num), "value-changed", G_CALLBACK(set_filtfilt_num), (unsigned char*)STAT_APER_NUM_OFFS);
  g_signal_connect_swapped(G_OBJECT(spn_aper_num), "value-changed", G_CALLBACK(update_stat_disp), lbl_stat_str);  
  gtk_table_attach(GTK_TABLE(box_status), gtk_label_new("Filter"), 8,9,2,4, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  GtkWidget *spn_filt_num = gtk_spin_button_new_with_range(0,9,1);
  gtk_table_attach(GTK_TABLE(box_status), spn_filt_num, 9,10,2,4, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect(G_OBJECT(spn_filt_num), "value-changed", G_CALLBACK(set_filtfilt_num), (unsigned char*)STAT_FILT_NUM_OFFS);
  g_signal_connect_swapped(G_OBJECT(spn_filt_num), "value-changed", G_CALLBACK(update_stat_disp), lbl_stat_str);  
  gtk_table_attach(GTK_TABLE(box_status), gtk_label_new("Dome offset"), 8,9,4,6, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  GtkWidget *spn_dome_offs = gtk_spin_button_new_with_range(0,3600,1);
  gtk_table_attach(GTK_TABLE(box_status), spn_dome_offs, 9,10,4,6, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect(G_OBJECT(spn_dome_offs), "value-changed", G_CALLBACK(set_dome_azmoffs), (unsigned char*)STAT_DOME_OFFS_OFFS);
  g_signal_connect_swapped(G_OBJECT(spn_dome_offs), "value-changed", G_CALLBACK(update_stat_disp), lbl_stat_str);
  gtk_table_attach(GTK_TABLE(box_status), gtk_label_new("Dome max flop"), 8,9,6,8, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  GtkWidget *spn_dome_max_flop = gtk_spin_button_new_with_range(0,99,1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spn_dome_max_flop), 5);
  gtk_table_attach(GTK_TABLE(box_status), spn_dome_max_flop, 9,10,6,8, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect(G_OBJECT(spn_dome_max_flop), "value-changed", G_CALLBACK(set_dome_flop), (unsigned char*)STAT_DOME_MAX_FLOP_OFFS);
  g_signal_connect_swapped(G_OBJECT(spn_dome_max_flop), "value-changed", G_CALLBACK(update_stat_disp), lbl_stat_str);
  gtk_table_attach(GTK_TABLE(box_status), gtk_label_new("Dome min flop"), 8,9,8,10, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  GtkWidget *spn_dome_min_flop = gtk_spin_button_new_with_range(0,99,1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spn_dome_min_flop), 10);
  gtk_table_attach(GTK_TABLE(box_status), spn_dome_min_flop, 9,10,8,10, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  g_signal_connect(G_OBJECT(spn_dome_min_flop), "value-changed", G_CALLBACK(set_dome_flop), (unsigned char*)STAT_DOME_MIN_FLOP_OFFS);
  g_signal_connect_swapped(G_OBJECT(spn_dome_min_flop), "value-changed", G_CALLBACK(update_stat_disp), lbl_stat_str);
  
  gdk_color_parse("green", &G_col_green);
  struct command_objects cmd_objs;
  cmd_objs.plc_fd = plc_fd;
  GtkWidget *frm_command = gtk_frame_new("Command");
  gtk_box_pack_start(GTK_BOX(box_main), frm_command, TRUE,TRUE,5);
  GtkWidget *box_command = gtk_table_new(9,8,FALSE);
  gtk_container_add(GTK_CONTAINER(frm_command), box_command);
  cmd_objs.lbl_cmd_str = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.lbl_cmd_str, 0,8,8,9, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  
  gtk_table_attach(GTK_TABLE(box_command), gtk_label_new("Dome azm"), 0,1,0,1, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.lbl_dome_pos = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.lbl_dome_pos, 1,2,0,1, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(box_command), gtk_label_new("Dome offset"), 0,1,1,2, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.lbl_dome_offs = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.lbl_dome_offs, 1,2,1,2, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(box_command), gtk_label_new("Dome offset"), 0,1,1,2, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.lbl_dome_offs = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.lbl_dome_offs, 1,2,1,2, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_set_dome_offs = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_set_dome_offs), gtk_label_new("Set dome offset"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_set_dome_offs, 0,2,2,3, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(box_command), gtk_label_new("Dome max flop"), 0,1,3,4, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.lbl_dome_max_flop = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.lbl_dome_max_flop, 1,2,3,4, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(box_command), gtk_label_new("Dome min flop"), 0,1,4,5, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.lbl_dome_min_flop = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.lbl_dome_min_flop, 1,2,4,5, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_dome_guide = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_dome_guide), gtk_label_new("Dome guide"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_dome_guide, 0,2,5,6, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_dome_move = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_dome_move), gtk_label_new("Dome move"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_dome_move, 0,2,6,7, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_dome_move_left = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_dome_move_left), gtk_label_new("Dome move left"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_dome_move_left, 0,2,7,8, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  
  cmd_objs.evb_acqreset = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_acqreset), gtk_label_new("Reset ACQ CCD"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_acqreset, 2,4,0,1, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_watchdog = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_watchdog), gtk_label_new("Watchdog reset"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_watchdog, 2,4,1,2, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_dropout_open = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_dropout_open), gtk_label_new("Dropout Open"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_dropout_open, 2,4,2,3, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_dropout_close = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_dropout_close), gtk_label_new("Dropout Close"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_dropout_close, 2,4,3,4, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_shutter_open = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_shutter_open), gtk_label_new("Shutter Open"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_shutter_open, 2,4,4,5, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_shutter_close = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_shutter_close), gtk_label_new("Shutter Close"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_shutter_close, 2,4,5,6, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_acqmir_reset = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_acqmir_reset), gtk_label_new("Acq mirror reset"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_acqmir_reset, 2,4,6,7, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_acqmir_inbeam = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_acqmir_inbeam), gtk_label_new("Acq mirror in-beam"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_acqmir_inbeam, 2,4,7,8, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);

  cmd_objs.evb_instr_shutt = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_instr_shutt), gtk_label_new("Instr shutt open"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_instr_shutt, 4,6,0,1, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_eht_lo = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_eht_lo), gtk_label_new("EHT Low"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_eht_lo, 4,6,1,2, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_eht_hi = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_eht_hi), gtk_label_new("EHT High"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_eht_hi, 4,6,2,3, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(box_command), gtk_label_new("Focus pos"), 4,5,3,4, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.lbl_foc_pos = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.lbl_foc_pos, 5,6,3,4, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_foc_go = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_foc_go), gtk_label_new("Focus go"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_foc_go, 4,6,4,5, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_foc_out = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_foc_out), gtk_label_new("Focus out"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_foc_out, 4,6,5,6, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_foc_in = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_foc_in), gtk_label_new("Focus in"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_foc_in, 4,6,6,7, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_foc_reset = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_foc_reset), gtk_label_new("Focus reset"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_foc_reset, 4,6,7,8, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);

  gtk_table_attach(GTK_TABLE(box_command), gtk_label_new("Filter pos"), 6,7,0,1, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.lbl_filt_num = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.lbl_filt_num, 7,8,0,1, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_filt_go = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_filt_go), gtk_label_new("Filter go"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_filt_go, 6,8,1,2, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_filt_init = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_filt_init), gtk_label_new("Filter init"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_filt_init, 6,8,2,3, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_filt_reset = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_filt_reset), gtk_label_new("Filter reset"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_filt_reset, 6,8,3,4, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(box_command), gtk_label_new("Aperture pos"), 6,7,4,5, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.lbl_aper_num = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.lbl_aper_num, 7,8,4,5, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_aper_go = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_aper_go), gtk_label_new("Aperture go"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_aper_go, 6,8,5,6, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_aper_init = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_aper_init), gtk_label_new("Aperture init"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_aper_init, 6,8,6,7, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  cmd_objs.evb_aper_reset = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(cmd_objs.evb_aper_reset), gtk_label_new("Aperture reset"));
  gtk_table_attach(GTK_TABLE(box_command), cmd_objs.evb_aper_reset, 6,8,7,8, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,TABLE_PADDING,TABLE_PADDING);
  
  g_timeout_add_seconds(1, read_command, &cmd_objs);
  gtk_widget_show_all(wnd_main);
  gtk_main();
  close(plc_fd);
  
  return 0;
}
