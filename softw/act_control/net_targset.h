#ifndef NET_TARGSET_H
#define NET_TARGSET_H

#include <act_ipc.h>
#include "subprogrammes.h"

void init_targcap();
void request_targcap(struct act_prog *prog);
void process_targcap(struct act_prog *prog_array, int num_progs, struct act_msg *msg);
void process_targset(struct act_prog *prog_array, int num_progs, struct act_msg *msg);

#endif
