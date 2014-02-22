/*!
 * \file act_pmtphot.c
 * \brief PMT photometry programme
 * \author Pierre van Heerden
 *
 * Collect photometry with a photomultiplier tube.
 */

#include <act_ipc.h>
#include <act_site.h>
#include <act_log.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <argtable2.h>
#include <mysql/mysql.h>
#include "pmtfuncs.h"
#include "pmtphot_plot.h"
#include "pmtphot_view.h"
#include "pmtphot_storeinteg.h"

#define TABLE_PADDING             3
#define GUICHECK_TIMEOUT_PERIOD   3000
#define PHOTCHECK_TIMEOUT_PERIOD  1000

enum
{
  FILTAPER_COL_ID = 0,
  FILTAPER_COL_SLOT,
  FILTAPER_COL_NAME,
  FILTAPER_NUM_COLS
};

struct formobjects
{
  GIOChannel *net_chan;
  MYSQL *mysql_conn;
  struct act_msg_datapmt *msg_datapmt;
  
  struct pmtdetailstruct *pmtdetail;
  struct pmtintegstruct *pmtinteg;
  struct storeinteg_objects *store_objs;
  struct plotobjects *plot_objs;
  struct viewobjects *photview_objs;
  
  GtkWidget *box_main;
};

struct maninteg_objects
{
  struct formobjects *formobjs;
  
  int targid;
  int userid;
  
  GtkWidget *ent_username, *ent_targname, *chk_sky;
  GtkWidget *spn_sampling, *spn_prebin, *spn_repeat;
  GtkWidget *cmb_filtspec, *cmb_aperspec;
};

static int G_maninteg_targid = 1;
static char G_maninteg_targname[256] = "ACT_ANY";
static int G_maninteg_userid = 1;
static char G_maninteg_username[256] = "ACT_ANY";
static char G_maninteg_sky = FALSE;
static double G_maninteg_sampleperiod = 0.001;
static int G_maninteg_prebin = 1;
static int G_maninteg_repeat = 1;
static char G_maninteg_filtid = -1;
static char G_maninteg_aperid = -1;
static struct act_msg_pmtcap G_pmtcaps;

char net_send(GIOChannel *channel, struct act_msg *msg)
{
  unsigned int num_bytes;
  int status;
  GError *error = NULL;
  status = g_io_channel_write_chars (channel, (gchar *)msg, sizeof(struct act_msg), &num_bytes, &error);
  if (error != NULL)
  {
    act_log_error(act_log_msg("Error sending message - %s", error->message));
    g_error_free(error);
    return -1;
  }
  if (status != G_IO_STATUS_NORMAL)
  {
    act_log_error(act_log_msg("Incorrect status returned while attempting to send message over network."));
    return -1;
  }
  if (num_bytes != sizeof(struct act_msg))
  {
    act_log_error(act_log_msg("Entire message was not transmitted (%d bytes)", num_bytes));
    return -1;
  }
  return num_bytes;
}

/** \brief Callback when plug (parent of all GTK objects) destroyed, adds a reference to plug's child so it (and it's
 *  \brief children) won't be deleted.
 * \param plug GTK plug containing all on-screen objects.
 * \param user_data The parent of all objects contained within plug.
 * \return (void)
 */
void destroy_plug(GtkWidget *plug, gpointer user_data)
{
  gtk_container_remove(GTK_CONTAINER(plug),GTK_WIDGET(user_data));
}

/** \brief Request socket information from controller (if GUI objects are not embedded within a socket).
 * \return void
 */
void request_guisock(GIOChannel *channel)
{
  struct act_msg guimsg;
  guimsg.mtype = MT_GUISOCK;
  memset(&guimsg.content.msg_guisock, 0, sizeof(struct act_msg_guisock));
  if (net_send(channel, &guimsg) < 0)
    act_log_error(act_log_msg("Error sending GUI socket request message."));
}

// void integrate_toggled(GtkWidget *btn_integrate, gpointer user_data)
// {
//   struct formobjects *objs = (struct formobjects *)user_data;
//   if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn_integrate)))
//   {
//     if (objs->pmt_info->integrating)
//     {
//       act_log_debug(act_log_msg("Strange. User ordered integration, but one seems to be underway already."));
//       cancel_integ(objs->pmt_info);
//     }
//     if (objs->pmt_integ != NULL)
//     {
//       act_log_debug(act_log_msg("Strange. User ordered integration, but an integration information structure is already present."));
//       free(objs->pmt_integ);
//       objs->pmt_integ = NULL;
//     }
//     unsigned long integt_ms = gtk_spin_button_get_value(GTK_SPIN_BUTTON(objs->spn_integt)) * 1000.0;
//     unsigned long repetitions = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(objs->spn_repeat));
//     char start_at_sec = FALSE;
//     struct filtaper *filter, *aperture;
//     if (objs->msg_datapmt != NULL)
//     {
//       filter = &objs->msg_datapmt->filter;
//       aperture = &objs->msg_datapmt->aperture;
//     }
//     else
//     {
//       filter = NULL;
//       aperture = NULL;
//     }
//     objs->pmt_integ = init_integ(objs->pmt_info, objs->targid, objs->sky, objs->userid, objs->pmt_info->pmt_min_sampling_ms, integt_ms, repetitions, start_at_sec, filter, aperture);
//     if (order_integ(objs->pmt_info, objs->pmt_integ) < 0)
//     {
//       act_log_error(act_log_msg("Failed to start integration."));
//       view_integ_error(objs->photview_objs, "Failed to start");
//       gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn_integrate), FALSE);
//     }
//     else
//     {
//       act_log_debug(act_log_msg("Starting integration: %d %c %d %lu %lu %c", objs->targid, objs->sky ? 's' : '*', objs->userid, integt_ms, repetitions, start_at_sec ? 'Y' : 'N'));
//       view_integ_start(objs->photview_objs, objs->pmt_integ);
//     }
//   }
//   else if (objs->pmt_info->integrating)
//   {
//     act_log_debug(act_log_msg("Integration cancelled by user."));
//     cancel_integ(objs->pmt_info);
//     view_integ_cancelled(objs->photview_objs);
//     if (objs->pmt_integ != NULL)
//     {
//       gtk_spin_button_set_value(GTK_SPIN_BUTTON(objs->spn_integt), objs->pmt_integ->integt_ms / 1000.0);
//       free(objs->pmt_integ);
//       objs->pmt_integ = NULL;
//     }
//     if (objs->msg_datapmt != NULL)
//     {
//       objs->msg_datapmt->status = OBSNSTAT_CANCEL;
//       struct act_msg msg;
//       msg.mtype = MT_DATA_PMT;
//       memcpy(&msg.content.msg_datapmt, objs->msg_datapmt, sizeof(struct act_msg_datapmt));
//       if (net_send(objs->net_chan, &msg) < 0)
//         act_log_error(act_log_msg("Error sending DATA PMT response message over network connection - %s", strerror(errno)));
//       free(objs->msg_datapmt);
//       objs->msg_datapmt = NULL;
//     }
//   }
//   else
//   {
//     act_log_debug(act_log_msg("Integration cancelled due to error."));
//     if (objs->msg_datapmt != NULL)
//     {
//       act_log_debug(act_log_msg("Sending error message."));
//       objs->msg_datapmt->status = OBSNSTAT_ERR_NEXT;
//       struct act_msg msg;
//       msg.mtype = MT_DATA_PMT;
//       memcpy(&msg.content.msg_datapmt, objs->msg_datapmt, sizeof(struct act_msg_datapmt));
//       if (net_send(objs->net_chan, &msg) < 0)
//         act_log_error(act_log_msg("Error sending DATA PMT response message over network connection - %s", strerror(errno)));
//       free(objs->msg_datapmt);
//       objs->msg_datapmt = NULL;
//     }
//   }
// }

void manual_integ_dialog_destroy(gpointer user_data)
{
  struct maninteg_objects *objs = (struct maninteg_objects *)user_data;
  g_object_unref(objs->ent_username);
  g_object_unref(objs->ent_targname);
  g_object_unref(objs->chk_sky);
  g_object_unref(objs->spn_sampling);
  g_object_unref(objs->spn_prebin);
  g_object_unref(objs->spn_repeat);
  g_object_unref(objs->cmb_filtspec);
  g_object_unref(objs->cmb_aperspec);
  free(objs);
}

void manual_integ_error(GtkWidget *dialog, const char *reason)
{
  GtkWidget *err_dialog = gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_toplevel(dialog)), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "Cannot start manual integration: \"%s\"", reason);
  g_signal_connect_swapped(G_OBJECT(err_dialog), "response", G_CALLBACK(gtk_widget_destroy), err_dialog);
  gtk_widget_show_all(err_dialog);
}

void manual_integ_response(GtkWidget *dialog, int response_id, gpointer user_data)
{
  if (response_id != GTK_RESPONSE_OK)
  {
    gtk_widget_destroy(dialog);
    return;
  }
  
  struct maninteg_objects *objs = (struct maninteg_objects *)user_data;
  
  if ((pmt_integrating(objs->formobjs->pmtdetail->pmt_stat)))
  {
    act_log_error(act_log_msg("An integration is currently underway. Cannot start manual integration."));
    manual_integ_error(dialog, "An integration is currently underway. Cannot start manual integration.");
    return;
  }
  GtkTreeIter iter;
  if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(objs->cmb_filtspec), &iter))
  {
    act_log_error(act_log_msg("No filter specified for manual integration."));
    manual_integ_error(dialog, "No filter specified.");
    return;
  }
  struct filtaper filter;
  int tmp_fa_id = -1, tmp_fa_slot = -1;
  char *tmp_fa_name = NULL;
  GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(objs->cmb_filtspec));
  gtk_tree_model_get(model, &iter, FILTAPER_COL_ID, &tmp_fa_id, FILTAPER_COL_SLOT, &tmp_fa_slot, FILTAPER_COL_NAME, &tmp_fa_name, -1);
  filter.db_id = tmp_fa_id;
  filter.slot = tmp_fa_slot;
  snprintf(filter.name, IPC_MAX_FILTAPER_NAME_LEN-1, "%s", tmp_fa_name);
  free(tmp_fa_name);
  if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(objs->cmb_aperspec), &iter))
  {
    act_log_error(act_log_msg("No aperture specified for manual integration."));
    manual_integ_error(dialog, "No aperture specified.");
    return;
  }
  struct filtaper aperture;
  tmp_fa_id = -1, tmp_fa_slot = -1;
  tmp_fa_name = NULL;
  model = gtk_combo_box_get_model(GTK_COMBO_BOX(objs->cmb_aperspec));
  gtk_tree_model_get(model, &iter, FILTAPER_COL_ID, &tmp_fa_id, FILTAPER_COL_SLOT, &tmp_fa_slot, FILTAPER_COL_NAME, &tmp_fa_name, -1);
  aperture.db_id = tmp_fa_id;
  aperture.slot = tmp_fa_slot;
  snprintf(aperture.name, IPC_MAX_FILTAPER_NAME_LEN-1, "%s", tmp_fa_name);
  struct pmtintegstruct *pmtinteg = malloc(sizeof(struct pmtintegstruct));
  if (pmtinteg == NULL)
  {
    act_log_error(act_log_msg("Failed to allocate space for PMT integration information structure."));
    manual_integ_error(dialog, "Failed to allocate space for PMT integration information structure. (Probably the computer has run out of memory.");
  }
  pmtinteg->targid = G_maninteg_targid = objs->targid;
  snprintf(G_maninteg_targname, sizeof(G_maninteg_targname), "%s", gtk_entry_get_text(GTK_ENTRY(objs->ent_targname)));
  pmtinteg->sky = G_maninteg_sky = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->chk_sky));
  pmtinteg->userid = G_maninteg_userid = objs->userid;
  snprintf(G_maninteg_username, sizeof(G_maninteg_username), "%s", gtk_entry_get_text(GTK_ENTRY(objs->ent_username)));
  pmtinteg->sample_period_s = G_maninteg_sampleperiod = gtk_spin_button_get_value(GTK_SPIN_BUTTON(objs->spn_sampling));
  pmtinteg->prebin = G_maninteg_prebin = gtk_spin_button_get_value(GTK_SPIN_BUTTON(objs->spn_prebin));
  pmtinteg->repetitions = G_maninteg_repeat = gtk_spin_button_get_value(GTK_SPIN_BUTTON(objs->spn_repeat));
  memcpy(&pmtinteg->filter, &filter, sizeof(struct filtaper));
  G_maninteg_filtid = filter.db_id;
  memcpy(&pmtinteg->aperture, &aperture, sizeof(struct filtaper));
  G_maninteg_aperid = aperture.db_id;
  char pmtinteg_reason[512];
  if (check_integ_params(objs->formobjs->pmtdetail, pmtinteg, pmtinteg_reason) <= 0)
  {
    act_log_error(act_log_msg("Invalid parameters for manual PMT integration. Reason(s) follow.\n%s", pmtinteg_reason));
    manual_integ_error(dialog, pmtinteg_reason);
    free(pmtinteg);
    return;
  }
  view_set_targ_id(objs->formobjs->photview_objs, objs->targid, gtk_entry_get_text(GTK_ENTRY(objs->ent_targname)));
  if (pmt_start_integ(objs->formobjs->pmtdetail, pmtinteg) <= 0)
  {
    act_log_error(act_log_msg("Failed to start manual integration."));
    manual_integ_error(dialog, "Failed to start integration (an internal error has occurred)");
    free(pmtinteg);
    return;
  }
  objs->formobjs->pmtinteg = pmtinteg;
  gtk_widget_destroy(dialog);
}

void maninteg_search_user_fail(GtkWidget *ent_username, const char *reason)
{
  const gchar *username = gtk_entry_get_text(GTK_ENTRY(ent_username));
  GtkWidget *err_dialog = gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_toplevel(ent_username)), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "An error occurred while retrieving unique ID for user \"%s\".\n\n%s\n\nSetting default username.", reason, username);
  g_signal_connect_swapped(G_OBJECT(err_dialog), "response", G_CALLBACK(gtk_widget_destroy), err_dialog);
  gtk_widget_show_all(err_dialog);
}

void maninteg_search_user(gpointer user_data)
{
  struct maninteg_objects *objs = (struct maninteg_objects *)user_data;
  const gchar *username = gtk_entry_get_text(GTK_ENTRY(objs->ent_username));
  char qrystr[256];
  sprintf(qrystr, "SELECT user_id FROM users WHERE name LIKE \"%s\";", username);
  
  MYSQL_RES *result;
  MYSQL_ROW row;
  mysql_query(objs->formobjs->mysql_conn,qrystr);
  result = mysql_store_result(objs->formobjs->mysql_conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve internal user ID matching username %s - %s.", username, mysql_error(objs->formobjs->mysql_conn)));
    maninteg_search_user_fail(objs->ent_username, "Could not contact ACT database.");
    objs->userid = 1;
    gtk_entry_set_text(GTK_ENTRY(objs->ent_username), "ACT_ANY");
    return;
  }
  
  int rowcount = mysql_num_rows(result);
  if ((rowcount < 0) || (mysql_num_fields(result) != 1))
  {
    act_log_error(act_log_msg("Could not retrieve internal user ID matching username %s - Invalid number of rows/columns returned (%d rows, %d columns).", username, rowcount, mysql_num_fields(result)));
    mysql_free_result(result);
    maninteg_search_user_fail(objs->ent_username, "An internal database error occurred.");
    objs->userid = 1;
    gtk_entry_set_text(GTK_ENTRY(objs->ent_username), "ACT_ANY");
    return;
  }
  if (rowcount != 1)
  {
    if (rowcount == 0)
    {
      act_log_error(act_log_msg("Username %s not found.", username));
      maninteg_search_user_fail(objs->ent_username, "User not found.");
    }
    else if (rowcount > 1)
    {
      act_log_error(act_log_msg("Multiple users with username %s found (%d).", username, rowcount));
      maninteg_search_user_fail(objs->ent_username, "Multiple users found.");
    }
    mysql_free_result(result);
    objs->userid = 1;
    gtk_entry_set_text(GTK_ENTRY(objs->ent_username), "ACT_ANY");
    return;
  }
  
  row = mysql_fetch_row(result);
  int tmp_user_id;
  if (sscanf(row[0], "%d", &tmp_user_id) != 1)
  {
    act_log_error(act_log_msg("Error parsing user identifier (%s).", row[0]));
    maninteg_search_user_fail(objs->ent_username, "Database returned invalid result.");
    tmp_user_id = -1;
    objs->userid = 1;
    gtk_entry_set_text(GTK_ENTRY(objs->ent_username), "ACT_ANY");
  }
  else
    objs->userid = tmp_user_id;
  mysql_free_result(result);
}

void maninteg_search_targ_fail(GtkWidget *ent_targname, const char *reason)
{  
  const gchar *targname = gtk_entry_get_text(GTK_ENTRY(ent_targname));
  GtkWidget *err_dialog = gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_toplevel(ent_targname)), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "An error occurred while retrieving unique ID for target \"%s\".\n%s\nSetting default target.", reason, targname);
  g_signal_connect_swapped(G_OBJECT(err_dialog), "response", G_CALLBACK(gtk_widget_destroy), err_dialog);
  gtk_widget_show_all(err_dialog);
}

void maninteg_search_targ(gpointer user_data)
{
  struct maninteg_objects *objs = (struct maninteg_objects *)user_data;
  const gchar *targname = gtk_entry_get_text(GTK_ENTRY(objs->ent_targname));
  char qrystr[256];
  sprintf(qrystr, "SELECT star_id FROM star_names WHERE star_name LIKE \"%s\";", targname);
  
  MYSQL_RES *result;
  MYSQL_ROW row;
  mysql_query(objs->formobjs->mysql_conn,qrystr);
  result = mysql_store_result(objs->formobjs->mysql_conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve internal target ID matching target name %s - %s.", targname, mysql_error(objs->formobjs->mysql_conn)));
    maninteg_search_targ_fail(objs->ent_targname, "Could not contact ACT database.");
    objs->targid = 1;
    gtk_entry_set_text(GTK_ENTRY(objs->ent_targname), "ACT_ANY");
    return;
  }
  
  int rowcount = mysql_num_rows(result);
  if ((rowcount < 0) || (mysql_num_fields(result) != 1))
  {
    act_log_error(act_log_msg("Could not retrieve internal target ID matching target name %s - Invalid number of rows/columns returned (%d rows, %d columns).", targname, rowcount, mysql_num_fields(result)));
    mysql_free_result(result);
    maninteg_search_targ_fail(objs->ent_targname, "An internal database error occurred.");
    objs->targid = 1;
    gtk_entry_set_text(GTK_ENTRY(objs->ent_targname), "ACT_ANY");
    return;
  }
  if (rowcount != 1)
  {
    if (rowcount == 0)
    {
      act_log_error(act_log_msg("Target name %s not found.", targname));
      maninteg_search_user_fail(objs->ent_targname, "Target not found.");
    }
    else if (rowcount > 1)
    {
      act_log_error(act_log_msg("Multiple targets with name %s found (%d).", targname, rowcount));
      maninteg_search_user_fail(objs->ent_targname, "Multiple targets found.");
    }
    mysql_free_result(result);
    objs->targid = 1;
    gtk_entry_set_text(GTK_ENTRY(objs->ent_targname), "ACT_ANY");
    return;
  }
  
  row = mysql_fetch_row(result);
  int tmp_targ_id;
  if (sscanf(row[0], "%d", &tmp_targ_id) != 1)
  {
    act_log_error(act_log_msg("Error parsing target identifier (%s).", row[0]));
    maninteg_search_user_fail(objs->ent_targname, "Database returned invalid result.");
    objs->targid = 1;
    gtk_entry_set_text(GTK_ENTRY(objs->ent_targname), "ACT_ANY");
  }
  else
    objs->targid = tmp_targ_id;
  mysql_free_result(result);
}

void manual_integ_dialog(gpointer user_data)
{
  struct maninteg_objects *objs = malloc(sizeof(struct maninteg_objects));
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Could not allocate space for manual integration objects."));
    return;
  }
  objs->formobjs = (struct formobjects*)user_data;
 
  GtkWidget *dialog = gtk_dialog_new_with_buttons("Manual Integration", GTK_WINDOW(gtk_widget_get_toplevel(objs->formobjs->box_main)), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_STOCK_OK, GTK_RESPONSE_OK, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, NULL);
  GtkWidget *box_content = gtk_table_new(0,0,FALSE);
  gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),box_content);
  
  struct act_msg_pmtcap pmtcaps;
  pmt_get_caps(objs->formobjs->pmtdetail, &pmtcaps);
  
  objs->userid = G_maninteg_userid;
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Username"), 0, 2, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  objs->ent_username = gtk_entry_new();
  g_object_ref(objs->ent_username);
  gtk_entry_set_text(GTK_ENTRY(objs->ent_username), G_maninteg_username);
  gtk_table_attach(GTK_TABLE(box_content), objs->ent_username, 2, 4, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *btn_search_user = gtk_button_new_with_label("Search");
  g_signal_connect_swapped(G_OBJECT(btn_search_user), "clicked", G_CALLBACK(maninteg_search_user), objs);
  gtk_table_attach(GTK_TABLE(box_content), btn_search_user, 4, 6, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_hseparator_new(), 0, 6, 1, 2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  
  objs->targid = G_maninteg_targid;
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Target"), 0, 2, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  objs->ent_targname = gtk_entry_new();
  g_object_ref(objs->ent_targname);
  gtk_entry_set_text(GTK_ENTRY(objs->ent_targname), G_maninteg_targname);
  gtk_table_attach(GTK_TABLE(box_content), objs->ent_targname, 2, 4, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *btn_search_targ = gtk_button_new_with_label("Search");
  g_signal_connect_swapped(G_OBJECT(btn_search_targ), "clicked", G_CALLBACK(maninteg_search_targ), objs);
  gtk_table_attach(GTK_TABLE(box_content), btn_search_targ, 4, 6, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  
  objs->chk_sky = gtk_check_button_new_with_label("Sky");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->chk_sky), G_maninteg_sky);
  g_object_ref(objs->chk_sky);
  gtk_table_attach(GTK_TABLE(box_content), objs->chk_sky, 0, 6, 3, 4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_hseparator_new(), 0, 6, 4, 5, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Sample period (s)"), 0, 3, 5, 6, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  objs->spn_sampling = gtk_spin_button_new_with_range(pmtcaps.min_sample_period_s, pmtcaps.max_sample_period_s, pmtcaps.min_sample_period_s);
  g_object_ref(objs->spn_sampling);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(objs->spn_sampling), G_maninteg_sampleperiod);
  gtk_table_attach(GTK_TABLE(box_content), objs->spn_sampling, 3, 6, 5, 6, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);

  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Prebinning"), 0, 3, 6, 7, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  objs->spn_prebin = gtk_spin_button_new_with_range(1, 560000, 1);
  g_object_ref(objs->spn_prebin);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(objs->spn_prebin),G_maninteg_prebin);
  gtk_table_attach(GTK_TABLE(box_content), objs->spn_prebin, 3, 6, 6, 7, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Repetitions"), 0, 3, 7, 8, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  objs->spn_repeat = gtk_spin_button_new_with_range(0, 560000, 1);
  g_object_ref(objs->spn_repeat);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(objs->spn_repeat),G_maninteg_repeat);
  gtk_table_attach(GTK_TABLE(box_content), objs->spn_repeat, 3, 6, 7, 8, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_hseparator_new(), 0, 6, 8, 9, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Filt. spec."), 0, 3, 9, 10, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  objs->cmb_filtspec = gtk_combo_box_new();
  g_object_ref(objs->cmb_filtspec);
  GtkListStore *filtstore = gtk_list_store_new(FILTAPER_NUM_COLS, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING);
  int i, num_filtaper=0;
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(objs->cmb_filtspec), renderer, TRUE);
  gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(objs->cmb_filtspec), renderer, "text", FILTAPER_COL_NAME);
  gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(filtstore), FILTAPER_COL_SLOT, GTK_SORT_ASCENDING);
  gtk_combo_box_set_model(GTK_COMBO_BOX(objs->cmb_filtspec), GTK_TREE_MODEL(filtstore));
  GtkTreeIter iter;
  for (i=0; i<IPC_MAX_NUM_FILTAPERS; i++)
  {
    if (G_pmtcaps.filters[i].db_id < 0)
      continue;
    num_filtaper++;
    gtk_list_store_append(GTK_LIST_STORE(filtstore), &iter);
    gtk_list_store_set(GTK_LIST_STORE(filtstore), &iter, FILTAPER_COL_ID, G_pmtcaps.filters[i].db_id, FILTAPER_COL_SLOT, G_pmtcaps.filters[i].slot, FILTAPER_COL_NAME, G_pmtcaps.filters[i].name, -1);
    if (G_pmtcaps.filters[i].db_id == G_maninteg_filtid)
      gtk_combo_box_set_active_iter(GTK_COMBO_BOX(objs->cmb_filtspec), &iter);
  }
  if (num_filtaper <= 0)
  {
    gtk_list_store_append(GTK_LIST_STORE(filtstore), &iter);
    gtk_list_store_set(GTK_LIST_STORE(filtstore), &iter, FILTAPER_COL_ID, 1, FILTAPER_COL_SLOT, 0, FILTAPER_COL_NAME, "N/A", -1);
    gtk_widget_set_sensitive(objs->cmb_filtspec, FALSE);
  }
  gtk_table_attach(GTK_TABLE(box_content), objs->cmb_filtspec, 3, 6, 9, 10, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);  
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Aper. spec."), 0, 3, 10, 11, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  objs->cmb_aperspec = gtk_combo_box_new();
  g_object_ref(objs->cmb_aperspec);
  GtkListStore *aperstore = gtk_list_store_new(FILTAPER_NUM_COLS, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING);
  renderer = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(objs->cmb_aperspec), renderer, TRUE);
  gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(objs->cmb_aperspec), renderer, "text", FILTAPER_COL_NAME);
  gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(aperstore), FILTAPER_COL_SLOT, GTK_SORT_ASCENDING);
  gtk_combo_box_set_model(GTK_COMBO_BOX(objs->cmb_aperspec), GTK_TREE_MODEL(aperstore));
  num_filtaper = 0;
  for (i=0; i<IPC_MAX_NUM_FILTAPERS; i++)
  {
    if (G_pmtcaps.apertures[i].db_id < 0)
      continue;
    num_filtaper++;
    gtk_list_store_append(GTK_LIST_STORE(aperstore), &iter);
    gtk_list_store_set(GTK_LIST_STORE(aperstore), &iter, FILTAPER_COL_ID, G_pmtcaps.apertures[i].db_id, FILTAPER_COL_SLOT, G_pmtcaps.apertures[i].slot, FILTAPER_COL_NAME, G_pmtcaps.apertures[i].name, -1);
    if (G_pmtcaps.apertures[i].db_id == G_maninteg_aperid)
      gtk_combo_box_set_active_iter(GTK_COMBO_BOX(objs->cmb_aperspec), &iter);
  }
  if (num_filtaper <= 0)
  {
    gtk_list_store_append(GTK_LIST_STORE(aperstore), &iter);
    gtk_list_store_set(GTK_LIST_STORE(aperstore), &iter, FILTAPER_COL_ID, 1, FILTAPER_COL_SLOT, 0, FILTAPER_COL_NAME, "N/A", -1);
    gtk_widget_set_sensitive(objs->cmb_aperspec, FALSE);
  }
  gtk_table_attach(GTK_TABLE(box_content), objs->cmb_aperspec, 3, 6, 10, 11, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);  
  
  gtk_widget_show_all(dialog);
  gtk_window_set_keep_above (GTK_WINDOW(dialog), TRUE);
  g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(manual_integ_response), objs);
  g_signal_connect_swapped(G_OBJECT(dialog), "destroy", G_CALLBACK(manual_integ_dialog_destroy), objs);
}

void cancel_integ(gpointer user_data)
{
  struct formobjects *objs = (struct formobjects *)user_data;
  pmt_cancel_integ(objs->pmtdetail);
  view_integ_complete(objs->photview_objs);
  if (objs->pmtinteg != NULL)
  {
    free(objs->pmtinteg);
    objs->pmtinteg = NULL;
  }
  if (objs->msg_datapmt != NULL)
  {
    struct act_msg msg;
    msg.mtype = MT_DATA_PMT;
    memcpy(&msg.content.msg_datapmt, objs->msg_datapmt, sizeof(struct act_msg_datapmt));
    if (net_send(objs->net_chan, &msg) < 0)
      act_log_error(act_log_msg("Error sending DATA PMT response message over network connection - %s", strerror(errno)));
    free(objs->msg_datapmt);
    objs->msg_datapmt = NULL;
  }
}

gboolean timeout_check_gui(gpointer user_data)
{
  struct formobjects *objs = (struct formobjects *)user_data;
  unsigned char main_embedded = gtk_widget_get_parent(GTK_WIDGET(objs->box_main)) != NULL;
  if (!main_embedded)
    request_guisock(objs->net_chan);
  return TRUE;
}

void integ_check_err(struct formobjects *objs, const char *longerr)
{
  act_log_error(act_log_msg("%s", longerr));
  view_integ_error(objs->photview_objs, "Internal error");
  pmt_cancel_integ(objs->pmtdetail);
  if (objs->pmtinteg != NULL)
  {
    free(objs->pmtinteg);
    objs->pmtinteg = NULL;
  }
  if (pmt_data_ready(objs->pmtdetail->pmt_stat))
    pmt_integ_clear_data(objs->pmtdetail);
  if (objs->msg_datapmt != NULL)
  {
    struct act_msg msgbuf;
    msgbuf.mtype = MT_DATA_PMT;
    memcpy(&msgbuf.content.msg_datapmt, objs->msg_datapmt, sizeof(struct act_msg_datapmt));
    msgbuf.content.msg_datapmt.status = OBSNSTAT_ERR_RETRY;
    if (net_send(objs->net_chan, &msgbuf) < 0)
    {
      act_log_error(act_log_msg("Error sending DATA PMT response message over network connection"));
      view_integ_error(objs->photview_objs, "NET COMM ERR");
    }
    free(objs->msg_datapmt);
    objs->msg_datapmt = NULL;
  }
}

gboolean timeout_check_phot(gpointer user_data)
{
  struct formobjects *objs = (struct formobjects *)user_data;
  char old_pmt_stat = objs->pmtdetail->pmt_stat;
  pmt_reg_checks(objs->pmtdetail);
  if ((pmt_probing(objs->pmtdetail->pmt_stat)) && (!pmt_probing(old_pmt_stat)))
    view_probe_start(objs->photview_objs);
  if ((!pmt_integrating(objs->pmtdetail->pmt_stat)) && (!pmt_data_ready(objs->pmtdetail->pmt_stat)))
  {
    act_log_debug(act_log_msg("Not integrating."));
    view_set_pmtdetail(objs->photview_objs, objs->pmtdetail);
    plot_set_pmtdetail(objs->plot_objs, objs->pmtdetail);
    return TRUE;
  }
  if (!pmt_integrating(old_pmt_stat))
  {
    act_log_debug(act_log_msg("Starting integration."));
    view_integ_start(objs->photview_objs, objs->pmtinteg);
  }
  else
    act_log_debug(act_log_msg("Integrating"));
  if (objs->pmtinteg == NULL)
  {
    integ_check_err(objs, "Integration is underway but no information on integration is available. Cancelling integration.");
    return TRUE;
  }
  int num_data = pmt_integ_get_data(objs->pmtdetail, objs->pmtinteg);
  if (num_data < 0)
  {
    integ_check_err(objs, "An unkown error occurred while integrating.");
    return TRUE;
  }
  if (num_data > 0)
  {
    storeinteg(objs->store_objs, objs->pmtinteg, num_data);
    view_dispinteg(objs->photview_objs, objs->pmtinteg);
    plot_add_data(objs->plot_objs, objs->pmtinteg);
    objs->pmtinteg = free_integ_data(objs->pmtinteg);
    if (objs->pmtinteg == NULL)
    {
      integ_check_err(objs, "An unkown error occurred while integrating.");
      return TRUE;
    }
  }
  if (objs->pmtinteg->done > 0)
  {
    act_log_debug(act_log_msg("Strange: Last integration data structure isn't a dummy."));
    if (!pmt_integrating(objs->pmtdetail->pmt_stat))
      objs->pmtinteg->done = -1;
  }
  if (objs->pmtinteg->done == 0)
    view_update_integ(objs->photview_objs, objs->pmtinteg);
/*  else if (pmt_integrating(objs->pmtdetail->pmt_stat))
  {
    act_log_debug(act_log_msg("Starting next integration."));
    view_integ_start(objs->photview_objs, objs->pmtinteg);
  }*/
  else
  {
    view_integ_complete(objs->photview_objs);
    free(objs->pmtinteg);
    objs->pmtinteg = NULL;
    if (objs->msg_datapmt != NULL)
    {
      struct act_msg msg;
      msg.mtype = MT_DATA_PMT;
      memcpy(&msg.content.msg_datapmt, objs->msg_datapmt, sizeof(struct act_msg_datapmt));
      msg.content.msg_datapmt.status = OBSNSTAT_GOOD;
      if (net_send(objs->net_chan, &msg) < 0)
        act_log_error(act_log_msg("Error sending DATA PMT response message over network connection - %s", strerror(errno)));
      free(objs->msg_datapmt);
      objs->msg_datapmt = NULL;
    }
  }
  act_log_debug(act_log_msg("Done with collected data."));
//   view_update_integ(objs->photview_objs, objs->pmtinteg);
  return TRUE;
}

/** \brief Check for incoming messages over network connection.
 * \param box_main GTK box containing all graphical objects.
 * \param main_embedded Flag indicating whether box_main is embedded in a plug.
 * \return TRUE if a message was received, FALSE if incoming queue is empty, <0 upon error.
 *
 * Recognises the following message types:
 *  - MT_QUIT: Exit main programme loop.
 *  - MT_CAP: Respond with capabilities and requirements.
 *  - MT_STAT: Respond with current status.
 *  - MT_GUISOCK: Construct GtkPlug for given socket and embed box_main within plug.
 *  - MT_OBSN:
 *    -# Collect photometry with the PMT as indicated in the observation command message.
 */
gboolean read_net_message (GIOChannel *source, GIOCondition condition, gpointer net_read_data)
{
  (void)condition;
  struct formobjects *objs = (struct formobjects *)net_read_data;
  struct act_msg msgbuf;
  unsigned int num_bytes;
  int status;
  GError *error = NULL;
  status = g_io_channel_read_chars (source, (gchar *)&msgbuf, sizeof(msgbuf), &num_bytes, &error);
  if (error != NULL)
  {
    act_log_error(act_log_msg("An error occurred while attempting to read message from network - ", error->message));
    g_error_free(error);
    return TRUE;
  }
  if (status != G_IO_STATUS_NORMAL)
  {
    act_log_error(act_log_msg("Incorrect status returned while attempting to read message from network."));
    return TRUE;
  }
  if (num_bytes != sizeof(msgbuf))
  {
    act_log_error(act_log_msg("Received message has invalid length: %d", num_bytes));
    return TRUE;
  }
  switch (msgbuf.mtype)
  {
    case MT_QUIT:
    {
      gtk_main_quit();
      break;
    }
    case MT_CAP:
    {
      struct act_msg_cap *cap_msg = &msgbuf.content.msg_cap;
      cap_msg->service_provides = 0;
      cap_msg->service_needs = SERVICE_TIME;
      cap_msg->targset_prov = 0;
      cap_msg->datapmt_prov = DATAPMT_PHOTOM;
      cap_msg->dataccd_prov = 0;
      snprintf(cap_msg->version_str,MAX_VERSION_LEN, "%d.%d", MAJOR_VER, MINOR_VER);
      msgbuf.mtype = MT_CAP;
      if (net_send(source, &msgbuf) < 0)
        act_log_error(act_log_msg("Failed to send capabilities response message."));
      break;
    }
    case MT_STAT:
    {
      if (msgbuf.content.msg_stat.status != 0)
        break;
      if (gtk_widget_get_parent(GTK_WIDGET(objs->box_main)) == NULL)
        msgbuf.content.msg_stat.status = PROGSTAT_STARTUP;
      else
        msgbuf.content.msg_stat.status = PROGSTAT_RUNNING;
      if (net_send(source, &msgbuf) < 0)
        act_log_error(act_log_msg("Failed to send status response message."));
      break;
    }
    case MT_GUISOCK:
    {
      if (gtk_widget_get_parent(GTK_WIDGET(objs->box_main)) != NULL)
      {
        act_log_debug(act_log_msg("GUI already embedded."));
        break;
      }
      if (msgbuf.content.msg_guisock.gui_socket == 0)
      {
        act_log_error(act_log_msg("Received 0 GUI socket."));
        break;
      }
      GtkWidget *plug = gtk_widget_get_parent(objs->box_main);
      if (plug != NULL)
      {
        gtk_container_remove(GTK_CONTAINER(plug), objs->box_main);
        gtk_widget_destroy(plug);
      }
      plug = gtk_plug_new(msgbuf.content.msg_guisock.gui_socket);
      act_log_normal(act_log_msg("Received GUI socket (%d).", msgbuf.content.msg_guisock.gui_socket));
      gtk_container_add(GTK_CONTAINER(plug),objs->box_main);
      g_signal_connect(G_OBJECT(plug),"destroy",G_CALLBACK(destroy_plug),objs->box_main);
      gtk_widget_show_all(plug);
      break;
    }
    case MT_TIME:
    {
      struct act_msg_time *msg_time = &msgbuf.content.msg_time;
      pmt_set_datetime(objs->pmtdetail, &msg_time->unid, &msg_time->unit);
      view_set_date(objs->photview_objs, &msg_time->unid);
      break;
    }
    case MT_PMT_CAP:
    {
      struct act_msg_pmtcap *msg_pmtcap = (struct act_msg_pmtcap *)&msgbuf.content.msg_pmtcap;
      if (msg_pmtcap->datapmt_stage != DATAPMT_PHOTOM)
      {
        pmt_get_caps(objs->pmtdetail, msg_pmtcap);
        if (net_send(source, &msgbuf) < 0)
          act_log_error(act_log_msg("Error sending PMT capabilities response message over network connection."));
      }
      else
        memcpy(&G_pmtcaps, msg_pmtcap, sizeof(struct act_msg_pmtcap));
      break;
    }
    case MT_DATA_PMT:
    {
      act_log_debug(act_log_msg("Received DATA_PMT message."));
      struct act_msg_datapmt *msg_datapmt = (struct act_msg_datapmt *)&msgbuf.content.msg_datapmt;
      if (msg_datapmt->datapmt_stage != DATAPMT_PHOTOM)
      {
        act_log_error(act_log_msg("Observation message with incorrect stage received."));
        break;
      }
      if (msg_datapmt->status != OBSNSTAT_GOOD)
      {
        act_log_debug(act_log_msg("Cancel observation."));
        if (objs->msg_datapmt != NULL)
        {
          free(objs->msg_datapmt);
          objs->msg_datapmt = NULL;
        }
        pmt_cancel_integ(objs->pmtdetail);
        if (objs->pmtinteg == NULL)
          act_log_debug(act_log_msg("Strange: PMT observation cancel message was received, but no PMT integration struct is available."));
        else
        {
          free(objs->pmtinteg);
          objs->pmtinteg = NULL;
        }
        if (net_send(source, &msgbuf) < 0)
          act_log_error(act_log_msg("Error sending DATA PMT response message over network connection"));
        break;
      }
      if (msg_datapmt->mode_auto == 0)
      {
        act_log_debug(act_log_msg("Manual mode"));
        if (net_send(source, &msgbuf) < 0)
          act_log_error(act_log_msg("Error sending DATA PMT response message over network connection"));
        break;
      }
      act_log_debug(act_log_msg("Auto observation."));
      if ((pmt_integrating(objs->pmtdetail->pmt_stat)))
      {
        act_log_error(act_log_msg("An integration is currently underway. Cannot start automatic integration."));
        msg_datapmt->status = OBSNSTAT_ERR_WAIT;
        if (net_send(source, &msgbuf) < 0)
          act_log_error(act_log_msg("Error sending DATA PMT response message over network connection"));
        break;
      }
      view_set_targ_id(objs->photview_objs, msg_datapmt->targ_id, msg_datapmt->targ_name);
      struct pmtintegstruct pmtinteg =
      {
        .targid = msg_datapmt->targ_id,
        .sky = msg_datapmt->sky,
        .userid = msg_datapmt->user_id,
        .sample_period_s = msg_datapmt->sample_period_s,
        .prebin = msg_datapmt->prebin_num,
        .repetitions = msg_datapmt->repetitions,
      };
      memcpy(&pmtinteg.filter, &msg_datapmt->filter, sizeof(struct filtaper));
      memcpy(&pmtinteg.aperture, &msg_datapmt->aperture, sizeof(struct filtaper));
      char pmtinteg_reason[512];
      act_log_debug(act_log_msg("Checking integ parameters"));
      if (check_integ_params(objs->pmtdetail, &pmtinteg, pmtinteg_reason) <= 0)
      {
        act_log_error(act_log_msg("Invalid parameters for automatic PMT integration. Reason(s) follow.\n%s", pmtinteg_reason));
        msg_datapmt->status = OBSNSTAT_ERR_NEXT;
        if (net_send(source, &msgbuf) < 0)
          act_log_error(act_log_msg("Error sending DATA PMT response message over network connection"));
        break;
      }
      act_log_debug(act_log_msg("Starting integ"));
      if (pmt_start_integ(objs->pmtdetail, &pmtinteg) <= 0)
      {
        act_log_error(act_log_msg("Failed to start integration."));
        msg_datapmt->status = OBSNSTAT_ERR_RETRY;
        if (net_send(source, &msgbuf) < 0)
          act_log_error(act_log_msg("Error sending DATA PMT response message over network connection"));
        break;
      }
      objs->msg_datapmt = malloc(sizeof(struct act_msg_datapmt));
      objs->pmtinteg = malloc(sizeof(struct pmtintegstruct));
      if ((objs->msg_datapmt == NULL) || (objs->pmtinteg == NULL))
      {
        act_log_error(act_log_msg("Could not allocate memory for message buffer and/or integration structure. Cancelling this integration."));
        pmt_cancel_integ(objs->pmtdetail);
        if (objs->pmtinteg != NULL)
        {
          free(objs->pmtinteg);
          objs->pmtinteg = NULL;
        }
        if (objs->msg_datapmt != NULL)
        {
          free(objs->msg_datapmt);
          objs->msg_datapmt = NULL;
        }
        msg_datapmt->status = OBSNSTAT_ERR_RETRY;
        if (net_send(source, &msgbuf) < 0)
          act_log_error(act_log_msg("Error sending DATA PMT response message over network connection"));
        break;
      }
      memcpy(objs->msg_datapmt, msg_datapmt, sizeof(struct act_msg_datapmt));
      memcpy(objs->pmtinteg, &pmtinteg, sizeof(struct pmtintegstruct));
      break;
    }
    default:
      if (msgbuf.mtype >= MT_INVAL)
        act_log_error(act_log_msg("Invalid message type received."));
  }
  int new_cond = g_io_channel_get_buffer_condition (source);
  if ((new_cond & G_IO_IN) != 0)
    return read_net_message(source, new_cond, net_read_data);
  return TRUE;
}

/** \brief Setup network socket with ACT control
 * \param host 
 *   C string containing hostname (dns name) or IP address of the computer running ACT control
 * \param port
 *   C string containing port number or port name on which server is listening for connections.
 * \return File descriptor for socket representing connection to controller (>0), or <=0 if an error occurred.
 */
GIOChannel *setup_net(const char* host, const char* port)
{
  char hostport[256];
  if (snprintf(hostport, sizeof(hostport), "%s:%s", host, port) != (int)(strlen(host)+strlen(port)+1))
  {
    act_log_error(act_log_msg("Failed to create host-and-port string."));
    return NULL;
  }
  
  GError *error = NULL;
  GSocketClient *socket_client;
  GSocketConnection *socket_connection;
  GSocket *socket;
  int fd;
  GIOChannel *channel;

  socket_client = g_socket_client_new();
  socket_connection = g_socket_client_connect_to_host(socket_client, hostport, 0, NULL, &error);
  if (error != NULL)
  {
    act_log_error(act_log_msg("Failed to connect to server %s", hostport));
    g_error_free(error);
    return NULL;
  }

  socket = g_socket_connection_get_socket(socket_connection);
  fd = g_socket_get_fd(socket);
  act_log_debug(act_log_msg("Connected on socket %d", fd));
  channel = g_io_channel_unix_new(fd);
  g_io_channel_set_close_on_unref (channel, TRUE);
  g_io_channel_set_encoding (channel, NULL, &error);
  if (error != NULL)
  {
    act_log_error(act_log_msg("Failed to set encoding type to binary for network communications channel."));
    g_error_free(error);
    return NULL;
  }
  g_io_channel_set_buffered (channel, FALSE);
  
  return channel;
}

void finish_all(struct formobjects *objs)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  if (objs->net_chan != NULL)
  {
    act_log_debug(act_log_msg("Closing network link."));
    g_io_channel_unref(objs->net_chan);
    objs->net_chan = NULL;
  }
  if (objs->mysql_conn != NULL)
  {
    act_log_debug(act_log_msg("Closing SQL connection."));
    mysql_close(objs->mysql_conn);
    objs->mysql_conn = NULL;
  }
  if (objs->pmtdetail != NULL)
  {
    act_log_debug(act_log_msg("Destroying PMT detail structure."));
    if (pmt_integrating(objs->pmtdetail->pmt_stat))
      pmt_cancel_integ(objs->pmtdetail);
    finalise_pmtdetail(objs->pmtdetail);
    free(objs->pmtdetail);
    objs->pmtinteg = NULL;
  }
  if (objs->pmtinteg != NULL)
  {
    act_log_debug(act_log_msg("Destroying PMT integration structure. (This should actually not have happened)."));
    free(objs->pmtinteg);
    objs->pmtinteg = NULL;
  }
  if (objs->store_objs != NULL)
  {
    act_log_debug(act_log_msg("Destroying data storage structure."));
    finalise_storeinteg(objs->store_objs);
    free(objs->store_objs);
    objs->store_objs = NULL;
  }
  if (objs->plot_objs != NULL)
  {
    act_log_debug(act_log_msg("Destroying plot objects structure."));
    finalise_plotobjs(objs->plot_objs);
    free(objs->plot_objs);
    objs->plot_objs = NULL;
  }
  if (objs->photview_objs != NULL)
  {
    act_log_debug(act_log_msg("Destroying photometry view objects."));
    finalise_view_objects(objs->photview_objs);
    free(objs->photview_objs);
    objs->photview_objs = NULL;
  }
}

int main(int argc, char** argv)
{
  act_log_open();
  act_log_normal(act_log_msg("Starting"));
  
  const char *host, *port, *sqlconfig;
  gtk_init(&argc, &argv);
  struct arg_str *addrarg = arg_str1("a", "addr", "<str>", "The host to connect to. May be a hostname, IP4 address or IP6 address.");
  struct arg_str *portarg = arg_str1("p", "port", "<str>", "The port to connect to. Must be an unsigned short integer.");
  struct arg_str *sqlconfigarg = arg_str1("s", "sqlconfighost", "<server ip/hostname>", "The hostname or IP address of the SQL server than contains act_control's configuration information");
  struct arg_end *endargs = arg_end(10);
  void* argtable[] = {addrarg, portarg, sqlconfigarg, endargs};
  if (arg_nullcheck(argtable) != 0)
    act_log_error(act_log_msg("Argument parsing error: insufficient memory."));
  int argparse_errors = arg_parse(argc,argv,argtable);
  if (argparse_errors != 0)
  {
    arg_print_errors(stderr,endargs,argv[0]);
    exit(EXIT_FAILURE);
  }
  host = addrarg->sval[0];
  port = portarg->sval[0];
  sqlconfig = sqlconfigarg->sval[0];
  arg_freetable(argtable,sizeof(argtable)/sizeof(argtable[0]));
  
  memset(&G_pmtcaps, 0, sizeof(struct act_msg_pmtcap));

  struct formobjects formobjs;
  memset(&formobjs, 0, sizeof(struct formobjects));
  
  formobjs.net_chan = setup_net(host, port);
  if (formobjs.net_chan == NULL)
  {
    act_log_error(act_log_msg("Error setting up network connection."));
    return 1;
  }
  
  formobjs.mysql_conn = mysql_init(NULL);
  if (formobjs.mysql_conn == NULL)
  {
    act_log_error(act_log_msg("Error initialising MySQL connection handler."));
    finish_all(&formobjs);
    return 1;
  }
  else if (mysql_real_connect(formobjs.mysql_conn, sqlconfig, "act_pmtphot", NULL, "actnew", 0, NULL, 0) == NULL)
  {
    act_log_error(act_log_msg("Error connecting to MySQL database - %s.", mysql_error(formobjs.mysql_conn)));
    finish_all(&formobjs);
    return 1;
  }
  
  formobjs.box_main = gtk_table_new(2,2,FALSE);
  g_object_ref(formobjs.box_main);
  
  GtkWidget *evb_pmt_stat = gtk_event_box_new();
  gtk_table_attach(GTK_TABLE(formobjs.box_main),evb_pmt_stat, 0, 1, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL, 3, 3);
  formobjs.pmtdetail = init_pmtdetail(evb_pmt_stat);
  if (formobjs.pmtdetail == NULL)
  {
    act_log_error(act_log_msg("Error initialising PMT detail structure."));
    finish_all(&formobjs);
    return 1;
  }
  
  GtkWidget *evb_store_stat = gtk_event_box_new();
  gtk_table_attach(GTK_TABLE(formobjs.box_main),evb_store_stat, 1, 2, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL, 3, 3);
  formobjs.store_objs = create_storeinteg(evb_store_stat, formobjs.mysql_conn);
  if (formobjs.store_objs == NULL)
  {
    act_log_error(act_log_msg("Error initialising PMT data storage information structure."));
    finish_all(&formobjs);
    return 1;
  }
  
  GtkWidget *evb_plot_box = gtk_event_box_new();
  gtk_table_attach(GTK_TABLE(formobjs.box_main),evb_plot_box, 0, 2, 1, 2, GTK_FILL|GTK_EXPAND, GTK_FILL, 3, 3);
  formobjs.plot_objs = create_plotobjs(evb_plot_box);
  if (formobjs.plot_objs == NULL)
  {
    act_log_error(act_log_msg("Error creating plot objects."));
    finish_all(&formobjs);
    return 1;
  }
  
  GtkWidget *evb_photview_box = gtk_event_box_new();
  gtk_table_attach(GTK_TABLE(formobjs.box_main),evb_photview_box, 0, 2, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  formobjs.photview_objs = create_view_objects(evb_photview_box);
  if (formobjs.photview_objs == NULL)
  {
    act_log_error(act_log_msg("Error creating photometry view objects."));
    finish_all(&formobjs);
    return 1;
  }
  
  GtkWidget *btn_man_integ = gtk_button_new_with_label("Man. Integ.");
  g_signal_connect_swapped(G_OBJECT(btn_man_integ), "clicked", G_CALLBACK(manual_integ_dialog), &formobjs);
  gtk_table_attach(GTK_TABLE(formobjs.box_main),btn_man_integ, 0, 1, 3, 4, GTK_FILL|GTK_EXPAND, GTK_FILL, 3, 3);
  GtkWidget *btn_integ_cancel = gtk_button_new_with_label("Cancel Integ.");
  g_signal_connect_swapped(G_OBJECT(btn_integ_cancel), "clicked", G_CALLBACK(cancel_integ), &formobjs);
  gtk_table_attach(GTK_TABLE(formobjs.box_main),btn_integ_cancel, 1, 2, 3, 4, GTK_FILL|GTK_EXPAND, GTK_FILL, 3, 3);
  
  act_log_normal(act_log_msg("Entering main loop."));
  int photcheck_to_id;
  if (PHOTCHECK_TIMEOUT_PERIOD % 1000 == 0)
    photcheck_to_id = g_timeout_add_seconds(PHOTCHECK_TIMEOUT_PERIOD/1000, timeout_check_phot, &formobjs);
  else
    photcheck_to_id = g_timeout_add(PHOTCHECK_TIMEOUT_PERIOD, timeout_check_phot, &formobjs);
  int guicheck_to_id;
  if (GUICHECK_TIMEOUT_PERIOD % 1000 == 0)
    guicheck_to_id = g_timeout_add_seconds(GUICHECK_TIMEOUT_PERIOD/1000, timeout_check_gui, &formobjs);
  else
    guicheck_to_id = g_timeout_add(GUICHECK_TIMEOUT_PERIOD, timeout_check_gui, &formobjs);
  int net_watch_id = g_io_add_watch(formobjs.net_chan, G_IO_IN, read_net_message, &formobjs);
  gtk_main();
  g_source_remove(net_watch_id);
  g_source_remove(photcheck_to_id);
  g_source_remove(guicheck_to_id);
  finish_all(&formobjs);
  act_log_normal(act_log_msg("Exiting"));
  return 0;
}

