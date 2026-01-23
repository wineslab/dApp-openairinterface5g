#ifndef RAN_FUNC_SM_DAPP_SUBSCRIPTION_AGENT_H
#define RAN_FUNC_SM_DAPP_SUBSCRIPTION_AGENT_H

#include "openair2/E2AP/flexric/src/sm/dapp_sm/ie/dapp_data_ie.h"
#include "common/utils/ds/seq_arr.h"

typedef struct ran_param_data_dapp_sm {
  uint32_t ric_req_id;
  e2sm_dapp_event_trigger_t ev_tr;
} ran_param_data_dapp_sm_t;

typedef struct {
  seq_arr_t fmt_0_subs;
} dapp_subs_data_t;

void insert_fmt_0_ric_id(dapp_subs_data_t *d, uint32_t ric_req_id);
void init_dapp_subs_data(dapp_subs_data_t *dapp_subs_data);
void remove_dapp_subs_data(dapp_subs_data_t *dapp_subs_data, uint32_t ric_req_id);

size_t ric_subs_snapshot(uint32_t **out);
void ric_subs_add(uint32_t ric_req_id);
void ric_subs_remove(uint32_t ric_req_id);

#endif