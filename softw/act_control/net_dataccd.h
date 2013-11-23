#ifndef NET_DATACCD_H
#define NET_DATACCD_H

#include <act_ipc.h>
#include "subprogrammes.h"

void init_ccdcap();
void request_ccdcap(struct act_prog *prog);
void process_ccdcap(struct act_prog *prog_array, int num_progs, struct act_msg *msg);
void process_dataccd(struct act_prog *prog_array, int num_progs, struct act_msg *msg);

#endif
