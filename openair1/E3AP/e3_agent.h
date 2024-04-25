#ifndef E3_AGENT_H
#define E3_AGENT_H

#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <assert.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <netdb.h> /* getprotobyname */
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// If this does not work, use /home/wineslab/openairinterface5g/common/
// TODO use relative path passed from CMAKE
#define T_MESSAGES_PATH "/home/wines/spear-openairinterface5g/common/utils/T/T_messages.txt"

typedef struct e3_agent_tracer_info{
    void *database;
    int socket;
} e3_agent_tracer_info_t;

typedef struct e3_agent_interface_info{
    int info;
} e3_agent_interface_info_t;

/**
 * @brief E3 agent control variables
 * This struct is responsible of handling all the shared variables to enable intercommunication between the E3 agent and the rest of the codebase
 *
*/
typedef struct e3_agent_controls{
    int trigger_iq_dump; // IQS: trigger to dump IQs on file
} e3_agent_controls_t;

extern e3_agent_controls_t* e3_agent_control;

int e3_agent_init();
int e3_agent_destroy();

void *e3_agent_t_tracer_task(void* args_p);
void *e3_agent_dapp_task(void* args_p);


#endif // E3_AGENT_H