#ifndef PMTPHOT_PLOT
#define PMTPHOT_PLOT

#include <stdio.h>
#include <gtk/gtk.h>
#include "pmtfuncs.h"

struct plotobjects
{
  GtkWidget *btn_plot_all, *btn_plot_star;
  GtkWidget *evb_plot_box;
  GtkWidget *btn_save_plot_pdf, *btn_plothelp;
  FILE *gnuplot_fp;
  FILE *plotdata_fp;
  char plotdata_filename[100];
  int cur_targ_id;
};

struct plotobjects *create_plotobjs(GtkWidget *container);
void finalise_plotobjs(struct plotobjects *objs);
void plot_set_pmtdetail(struct plotobjects *objs, struct pmtdetailstruct *pmtdetail);
void plot_set_targid(struct plotobjects *objs, int targ_id);
void plot_add_data(struct plotobjects *objs, struct pmtintegstruct *pmtinteg);

#endif