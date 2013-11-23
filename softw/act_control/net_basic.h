#ifndef NET_BASIC_H
#define NET_BASIC_H

#include <act_ipc.h>
#include "subprogrammes.h"

unsigned char act_send(struct act_prog *prog, struct act_msg *msg);
unsigned char act_recv(struct act_prog *prog, struct act_msg *msg);
int net_setup(const char *port);

#endif
