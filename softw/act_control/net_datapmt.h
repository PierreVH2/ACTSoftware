#ifndef NET_DATAPMT_H
#define NET_DATAPMT_H

#include <act_ipc.h>
#include "subprogrammes.h"

void init_pmtcap();
void request_pmtcap(struct act_prog *prog);
void process_pmtcap(struct act_prog *prog_array, int num_progs, struct act_msg *msg);
void process_datapmt(struct act_prog *prog_array, int num_progs, struct act_msg *msg);

#endif
