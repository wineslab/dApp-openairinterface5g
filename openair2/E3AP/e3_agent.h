#ifndef E3_AGENT_H
#define E3_AGENT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <assert.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <netdb.h> /* getprotobyname */
#include <sys/socket.h>

#include <unistd.h>

#include <netinet/in.h>
#include <sys/un.h>

#include "config/e3_config.h"
#include "e3ap_types.h"
#include "e3_subscription_manager.h"
#include "service_models/sm_interface.h"
#include "e3_response_queue.h"

#ifdef E2_AGENT
#include "ran_func_dapp_extern.h"
#endif

extern e3_subscription_manager_t* e3_subscription_manager;
extern e3_response_queue_t* ran_to_e3_agent_queue;

int e3_agent_init();
int e3_agent_destroy();

void* e3_agent_dapp_task(void* args_p);

#endif // E3_AGENT_H