#ifndef RAN_FUNC_SM_DAPP_READ_WRITE_AGENT_H
#define RAN_FUNC_SM_DAPP_READ_WRITE_AGENT_H

#include "openair2/E2AP/flexric/src/agent/../sm/sm_io.h"
#include "ran_func_dapp_subs.h"
#include "ran_func_dapp_extern.h"
#include "ran_e2sm_ue_id.h"
#include "../../flexric/src/agent/e2_agent_api.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "common/ran_context.h"

#if defined(E3_AGENT)
#include "e3_agent.h"
#include <endian.h>
#endif

void read_dapp_setup_sm(void* data);

sm_ag_if_ans_t write_subs_dapp_sm(void const* src);

sm_ag_if_ans_t write_ctrl_dapp_sm(void const* data);

bool read_dapp_sm(void*);

#endif
