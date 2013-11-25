#ifndef PMTPHOT_VIEW
#define PMTPHOT_VIEW

#include <gtk/gtk.h>
#include <mysql/mysql.h>
#include <act_timecoord.h>
#include "pmtfuncs.h"

struct viewobjects
{
  int targ_id;
  struct datestruct unidate;
  char high_counts_warn, time_sync_warn, zero_counts_err, overflow_err, overillum_err;

  GtkTreeModel *phot_store;
  GtkTreeIter targ_iter, integ_iter, *cur_iter;
  GtkWidget *trv_photview;
};

struct viewobjects *create_view_objects(GtkWidget *container);
void finalise_view_objects(struct viewobjects *objs);
void view_set_pmtdetail(struct viewobjects *objs, struct pmtdetailstruct *pmtdetail);
void view_set_targ_id(struct viewobjects *objs, int targ_id, const char *targ_name);
void view_set_date(struct viewobjects *objs, struct datestruct *unidate);
void view_probe_start(struct viewobjects *objs);
void view_integ_start(struct viewobjects *objs, struct pmtintegstruct *pmtinteg);
void view_integ_cancelled(struct viewobjects *objs);
void view_integ_complete(struct viewobjects *objs);
void view_integ_error(struct viewobjects *objs, const char* msg);
void view_dispinteg(struct viewobjects *objs, struct pmtintegstruct *pmtinteg);
void view_update_integ(struct viewobjects *objs, struct pmtintegstruct *pmtinteg);

#endif