#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <motor_driver.h>

void limit_change(gpointer user_data);
gboolean check_motor(gpointer user_data);

int main(int argc, char ** argv)
{
  gtk_init(&argc, &argv);
  
  int motor_fd = open("/dev/" MOTOR_DEVICE_NAME, O_RDWR | O_NONBLOCK);
  if (motor_fd < 0)
  {
    fprintf(stderr, "Failed to open motor device (/dev/%s) - %s\n", MOTOR_DEVICE_NAME, strerror(errno));
    return 1;
  }
  char motor_stat;
  int ret = read(motor_fd, &motor_stat, 1);
  if (ret < 0)
  {
    fprintf(stderr, "Failed to read motor status - %s\n", strerror(errno));
    close(motor_fd);
    return 1;
  }
  
  GError *error = NULL;
  GIOChannel *motor_chan = g_io_channel_unix_new(motor_fd);
  g_io_channel_ref(motor_chan);
  g_io_channel_set_close_on_unref (motor_chan, TRUE);
  g_io_channel_set_encoding (motor_chan, NULL, &error);
  if (error != NULL)
  {
    fprintf(stderr, "Failed to set encoding type for motor driver channel (%d - %s).\n", error->code, error->message);
    g_error_free(error);
    return 1;
  }
  g_io_channel_set_buffered (motor_chan, FALSE);
  
  GtkWidget *wnd_main = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  GtkWidget *box_main = gtk_table_new(0,0,FALSE);
  gtk_container_add(GTK_CONTAINER(wnd_main), box_main);
  
  gtk_table_attach(GTK_TABLE(box_main), gtk_label_new("Command steps:"), 0, 1, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *lbl_cmd_steps = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(box_main), lbl_cmd_steps, 1, 5, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  gtk_table_attach(GTK_TABLE(box_main), gtk_label_new("Command speed:"), 0, 1, 1, 2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *lbl_cmd_speed = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(box_main), lbl_cmd_speed, 1, 5, 1, 2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  gtk_table_attach(GTK_TABLE(box_main), gtk_label_new("Command direction:"), 0, 1, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *lbl_cmd_dir = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(box_main), lbl_cmd_dir, 1, 5, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  
  gtk_table_attach(GTK_TABLE(box_main), gtk_hseparator_new(), 0, 5, 3, 4, GTK_FILL|GTK_EXPAND, GTK_SHRINK, 3, 3);
  
  gtk_table_attach(GTK_TABLE(box_main), gtk_label_new("Simulated steps:"), 0, 1, 4, 5, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *lbl_sim_steps = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(box_main), lbl_sim_steps, 1, 5, 4, 5, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  gtk_table_attach(GTK_TABLE(box_main), gtk_label_new("Simulated limits:"), 0, 1, 5, 6, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *btn_sim_limN = gtk_toggle_button_new_with_label("N");
  gtk_table_attach(GTK_TABLE(box_main), btn_sim_limN, 1, 2, 5, 6, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *btn_sim_limS = gtk_toggle_button_new_with_label("S");
  gtk_table_attach(GTK_TABLE(box_main), btn_sim_limS, 2, 3, 5, 6, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *btn_sim_limE = gtk_toggle_button_new_with_label("E");
  gtk_table_attach(GTK_TABLE(box_main), btn_sim_limE, 3, 4, 5, 6, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *btn_sim_limW = gtk_toggle_button_new_with_label("W");
  gtk_table_attach(GTK_TABLE(box_main), btn_sim_limW, 4, 5, 5, 6, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  
  g_signal_connect_swapped(wnd_main,"destroy",G_CALLBACK(gtk_main_quit),NULL);
  GObject *limit_change_objects = g_object_new(G_TYPE_OBJECT, NULL);
  g_object_set_data(limit_change_objects, "btn_sim_limN", btn_sim_limN);
  g_object_set_data(limit_change_objects, "btn_sim_limS", btn_sim_limS);
  g_object_set_data(limit_change_objects, "btn_sim_limE", btn_sim_limE);
  g_object_set_data(limit_change_objects, "btn_sim_limW", btn_sim_limW);
  g_object_set_data(limit_change_objects, "lbl_sim_steps", lbl_sim_steps);
  g_signal_connect_swapped(G_OBJECT(btn_sim_limN), "toggled", G_CALLBACK(limit_change), limit_change_objects);
  g_signal_connect_swapped(G_OBJECT(btn_sim_limS), "toggled", G_CALLBACK(limit_change), limit_change_objects);
  g_signal_connect_swapped(G_OBJECT(btn_sim_limE), "toggled", G_CALLBACK(limit_change), limit_change_objects);
  g_signal_connect_swapped(G_OBJECT(btn_sim_limW), "toggled", G_CALLBACK(limit_change), limit_change_objects);
  GObject *check_motor_objects = g_object_new(G_TYPE_OBJECT, NULL);
  g_object_set_data(check_motor_objects, "lbl_cmd_steps", lbl_cmd_steps);
  g_object_set_data(check_motor_objects, "lbl_cmd_speed", lbl_cmd_speed);
  g_object_set_data(check_motor_objects, "lbl_cmd_dir", lbl_cmd_dir);
  g_object_set_data(check_motor_objects, "lbl_sim_steps", lbl_sim_steps);
  g_object_set_data(check_motor_objects, "motor_chan", motor_chan);
  g_timeout_add(500, check_motor, check_motor_objects);
  
  gtk_widget_show_all(wnd_main);
  gtk_main();
  g_io_channel_unref(motor_chan);
  
  return 0;
}

void limit_change(gpointer user_data)
{
  GtkWidget *btn_sim_limN = GTK_WIDGET(g_object_get_data(user_data, "btn_sim_limN"));
  GtkWidget *btn_sim_limS = GTK_WIDGET(g_object_get_data(user_data, "btn_sim_limS"));
  GtkWidget *btn_sim_limE = GTK_WIDGET(g_object_get_data(user_data, "btn_sim_limE"));
  GtkWidget *btn_sim_limW = GTK_WIDGET(g_object_get_data(user_data, "btn_sim_limW"));
  GIOChannel *motor_chan = g_object_get_data(user_data, "motor_chan");
  g_io_channel_ref(motor_chan);
  long new_lim = 0;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn_sim_limN)))
    new_lim |= DIR_NORTH_MASK;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn_sim_limS)))
    new_lim |= DIR_SOUTH_MASK;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn_sim_limE)))
    new_lim |= DIR_EAST_MASK;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn_sim_limW)))
    new_lim |= DIR_WEST_MASK;
  int motor_fd = g_io_channel_unix_get_fd(motor_chan);
  int ret = ioctl(motor_fd, IOCTL_SET_SIM_LIMITS, &new_lim);
  if (ret != 0)
    fprintf(stderr, "Failed to set simulated motor limits.\n");
  g_io_channel_unref(motor_chan);
}

gboolean check_motor(gpointer user_data)
{
  GtkWidget *lbl_cmd_steps = GTK_WIDGET(g_object_get_data(user_data, "lbl_cmd_steps"));
  GtkWidget *lbl_cmd_speed = GTK_WIDGET(g_object_get_data(user_data, "lbl_cmd_speed"));
  GtkWidget *lbl_cmd_dir = GTK_WIDGET(g_object_get_data(user_data, "lbl_cmd_dir"));
  GtkWidget *lbl_sim_steps = GTK_WIDGET(g_object_get_data(user_data, "lbl_sim_steps"));
  GIOChannel *motor_chan = g_object_get_data(user_data, "motor_chan");
  g_io_channel_ref(motor_chan);
  char tmpstr[256];
  long tmp;
  unsigned long steps=0, speed=0;
  unsigned char dir=0;
  int motor_fd = g_io_channel_unix_get_fd(motor_chan);
  int ret = ioctl(motor_fd, IOCTL_GET_SIM_SPEED, &tmp);
  if (ret != 0)
    fprintf(stderr, "Failed to get speed command.\n");
  else
  {
    speed = tmp;
    sprintf(tmpstr, "%lu", speed);
    gtk_label_set_text(GTK_LABEL(lbl_cmd_speed), tmpstr);
  }
  ret = ioctl(motor_fd, IOCTL_GET_SIM_STEPS, &tmp);
  if (ret != 0)
    fprintf(stderr, "Failed to get steps command.\n");
  else
  {
    steps = tmp & 0xFFFFFF;
    sprintf(tmpstr, "%lu", steps);
    gtk_label_set_text(GTK_LABEL(lbl_cmd_steps), tmpstr);
  }
  ret = ioctl(motor_fd, IOCTL_GET_SIM_DIR, &tmp);
  if (ret != 0)
    fprintf(stderr, "Failed to get direction command.\n");
  else
  {
    dir = tmp;
    sprintf(tmpstr, "%s %s %s %s", (dir & DIR_NORTH_MASK)  > 0 ? "N" : " ", (dir & DIR_SOUTH_MASK)  > 0 ? "S" : " ", (dir & DIR_EAST_MASK)  > 0 ? "E" : " ", (dir & DIR_WEST_MASK)  > 0 ? "W" : " ");
    gtk_label_set_text(GTK_LABEL(lbl_cmd_dir), tmpstr);
  }
  if ((speed > 0) && (steps > 0) && (dir != 0))
  {
    unsigned long sim_steps = 0xFFFF;
//     tmp = (steps & 0xFFFFFF) - 55780 / speed;
//     if (sim_steps < 0)
//       sim_steps = 0;
    printf("Sending new steps: %lu (%lu %lu %lu)\n", sim_steps, steps, speed, 55780 / speed);
    ret = ioctl(motor_fd, IOCTL_SET_SIM_STEPS, &sim_steps);
    if (ret < 0)
      fprintf(stderr, "Failed to set simulated steps.\n");
    else
    {
      sprintf(tmpstr, "%ld", sim_steps);
      gtk_label_set_text(GTK_LABEL(lbl_sim_steps), tmpstr);
    }
  }
  g_io_channel_unref(motor_chan);
  return TRUE;
}

