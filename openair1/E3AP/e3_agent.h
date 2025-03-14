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

#include "common/utils/nr/nr_common.h"
#include "common/config/config_userapi.h"
#include "common/config/config_paramdesc.h"

typedef struct e3_agent_tracer_info{
    void *database;
    int socket;
} e3_agent_tracer_info_t;

typedef struct e3_config{
    char* link;
    char* transport;
    int sampling;
} e3_config_t;

/**
 * @brief E3 agent control variables
 * This struct is responsible of handling all the shared variables to enable intercommunication between the E3 agent and the rest of the codebase
 *
*/
typedef struct e3_agent_controls{
    char* action_list;
    int action_size;
    uint16_t dyn_prbbl[MAX_BWP_SIZE];
    int ready;
    int sampling_threshold;
    int sampling_counter;
    pthread_mutex_t mutex;
} e3_agent_controls_t;

extern e3_agent_controls_t* e3_agent_control;

int e3_agent_init();
int e3_agent_destroy();

void *e3_agent_dapp_task(void* args_p);
void e3_agent_t_tracer_init(void);

#endif // E3_AGENT_H
