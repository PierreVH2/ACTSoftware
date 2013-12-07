#ifndef DTI_CONFIG_H
#define DTI_CONFIG_H

#include <act_ipc.h>

char parse_config(const char *sqlhost, struct act_msg_targcap *targcap_msg, struct act_msg_pmtcap *pmtcap_msg, struct act_msg_ccdcap *ccdcap_msg);

#endif
