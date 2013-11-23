#ifndef NET_GENL_H
#define NET_GENL_H

#include "subprogrammes.h"

int check_prog_messages(struct act_prog *prog_array, int num_progs);
char send_statreq(struct act_prog *prog);
void send_allstop(struct act_prog *prog_array, int num_progs);

#endif
