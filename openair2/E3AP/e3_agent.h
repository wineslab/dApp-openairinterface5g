#ifndef E3_AGENT_H
#define E3_AGENT_H

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <unistd.h>

#include <libe3/c_api.h>

#include "config/e3_config.h"

#ifdef E2_AGENT
#include "ran_func_dapp_extern.h"
#endif

typedef struct {
  e3_agent_handle_t *agent;
  e3_service_model_handle_t **service_models;
  size_t num_service_models;
} e3_agent_global_t;

extern e3_agent_global_t e3;

int e3_init();
int e3_destroy();

int e3_send_xapp_control(uint32_t dapp_id,
                         uint32_t ran_function_id,
                         const uint8_t *data,
                         size_t len);

#endif // E3_AGENT_H
