#include "e3_agent.h"

#include "common/utils/T/tracer/database.h"
#include "event.h"
#include "common/utils/T/tracer/handler.h"
#include "utils.h"
#include "common/utils/T/tracer/event_selector.h"
#include "config.h"

# include "intertask_interface.h"
# include "create_tasks.h"

#include "common/utils/T/tracer/logger/logger.h"
#include "common/utils/T/tracer/event_selector.h"

#include <common/utils/system.h>
#include "common/ran_context.h"
#include "common/utils/LOG/log.h"

/* this function sends the activated traces to the nr-softmodem */
void activate_traces(int socket, int number_of_events, int *is_on)
{
  char t = 1;
  if (socket_send(socket, &t, 1) == -1 || socket_send(socket, &number_of_events, sizeof(int)) == -1 || socket_send(socket, is_on, number_of_events * sizeof(int)) == -1)
    abort();
}

/* this macro looks for a particular element and checks its type */
#define GET_DATA_FROM_TRACER(var_name, var_type, var)           \
  if (!strcmp(f.name[i], var_name)) {        \
    if (strcmp(f.type[i], var_type)) {       \
      printf("bad type for %s\n", var_name); \
      exit(1);                               \
    }                                        \
    var = i;                                 \
    continue;                                \
  }

int setup_t_tracer()
{
  e3_agent_tracer_info_t *tracer_info = (e3_agent_tracer_info_t *) malloc(sizeof(e3_agent_tracer_info_t));
  // This shall just setup the T tracer socket
  // If this does not work, use /home/wineslab/openairinterface5g/common/
  char *database_filename = "../utils/T/T_messages.txt";
  char *ip = DEFAULT_REMOTE_IP;
  int port = DEFAULT_REMOTE_PORT;
  int *is_on;
  int number_of_events;

  /* write on a socket fails if the other end is closed and we get SIGPIPE */
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    abort();

  /* load the database T_messages.txt */
  tracer_info->database = parse_database(database_filename);
  load_config_file(database_filename);

  /* an array of int for all the events defined in the database is needed */
  number_of_events = number_of_ids(tracer_info->database);
  is_on = calloc(number_of_events, sizeof(int));
  if (is_on == NULL)
    abort();

  /* activate the E3_AGENT_RAW_IQ_DATA trace in this array */
  on_off(tracer_info->database, "E3_AGENT_RAW_IQ_DATA", is_on, 1);
  /* connect to the nr-softmodem */
  tracer_info->socket = connect_to(ip, port);
  /* activate the trace E3_AGENT_RAW_IQ_DATA in the nr-softmodem */
  activate_traces(tracer_info->socket, number_of_events, is_on);
  free(is_on);

  if (itti_create_task(TASK_E3_AGENT, e3_agent_t_tracer_task, tracer_info) < 0) {
    LOG_E(E3_AGENT, "cannot create ITTI task for T tracer\n");
    return -1;
  }
  return 0;
}

void setup_e3_interface(){

}

e3_agent_controls_t* e3_agent_control = NULL;

int e3_agent_init(){
  // TODO e3
  e3_agent_control = (e3_agent_controls_t*) malloc(sizeof(e3_agent_controls_t));
  // Set T tracer socket
  LOG_D(E3_AGENT, "Setup T Tracer socket");
  setup_t_tracer();
  // Set E3 interface for dApp socket
  LOG_D(E3_AGENT, "Setup E3 Interface socket");
  setup_e3_interface();

  return 0;
}

void *e3_agent_t_tracer_task(void* args_p){
  int i;
  int data;
  int e3_agent_raw_iq_data_id;
  database_event_format f;
  e3_agent_tracer_info_t* tracer_info = (e3_agent_tracer_info_t*) args_p;
  /* get the format of the E3_AGENT_RAW_IQ_DATA trace */
  e3_agent_raw_iq_data_id = event_id_from_name(tracer_info->database, "E3_AGENT_RAW_IQ_DATA");
  f = get_format(tracer_info->database, e3_agent_raw_iq_data_id);

  /* get the elements of the E3_AGENT_RAW_IQ_DATA trace
   * the value is an index in the event, see below
   */
  for (i = 0; i < f.count; i++) {
    GET_DATA_FROM_TRACER("iqsamples", "buffer", data);
  }

  /* a buffer needed to receive events from the nr-softmodem */
  OBUF ebuf = {osize : 0, omaxsize : 0, obuf : NULL};

  /* read events */
  while (1) {
    event e;
    e = get_event(tracer_info->socket, &ebuf, tracer_info->database);
    if (e.type == -1)
      break;
    if (e.type == e3_agent_raw_iq_data_id) {
      /* this is how to access the elements of the LDPC_OK trace.
       * we use e.e[<element>] and then the correct suffix, here
       * it's .i for the integer and .b for the buffer and .bsize
       * for the buffer size
       * see in event.h the structure event_arg
       */
      unsigned char *buf = e.e[data].b;
      printf("get E3_AGENT_RAW_IQ_DATA event buffer length %d = [", e.e[data].bsize);
      for (i = 0; i < e.e[data].bsize; i++)
        printf(" %2.2x", buf[i]);
      printf("]\n");
    }
  }

  return NULL;
}

void *e3_agent_dapp_task(){

}


int open_trigger_socket(void) {

    char buffer[BUFSIZ];
    char protoname[] = "tcp";
    struct protoent *protoent;
    int enable = 1;
    // int i;
    // int newline_found = 0;
    int server_sockfd, client_sockfd;
    socklen_t client_len;
    ssize_t nbytes_read;
    struct sockaddr_in client_address, server_address;
    unsigned short server_port = 12346u;
    int trigger;

    protoent = getprotobyname(protoname);
    if (protoent == NULL) {
        LOG_E(E3_AGENT,"getprotobyname");
        exit(EXIT_FAILURE);
    }

    server_sockfd = socket(
        AF_INET,
        SOCK_STREAM,
        protoent->p_proto
        /* 0 */
    );

    if (server_sockfd == -1) {
        LOG_E(E3_AGENT,"socket");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        LOG_E(E3_AGENT,"setsockopt(SO_REUSEADDR) failed");
        exit(EXIT_FAILURE);
    }

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(server_port);
    if (bind(
            server_sockfd,
            (struct sockaddr*)&server_address,
            sizeof(server_address)
        ) == -1
    ) {
        LOG_E(E3_AGENT,"bind");
        exit(EXIT_FAILURE);
    }

    if (listen(server_sockfd, 5) == -1) {
        LOG_E(E3_AGENT,"listen");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "listening on port %d\n", server_port);

    while (1) {
      client_len = sizeof(client_address);
      client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_address, &client_len);

      while ((nbytes_read = read(client_sockfd, buffer, BUFSIZ)) > 0) {
        // fprintf(stdout, "Received from socket:\n");
        // fflush(stdout);

        if (write(STDOUT_FILENO, buffer, nbytes_read) < 0) {
          LOG_E(E3_AGENT, "write(STDOUT_FILENO) failed");
          exit(EXIT_FAILURE);
        }
        // if (buffer[nbytes_read - 1] == '\n')
        //     newline_found;
        // for (i = 0; i < nbytes_read - 1; i++)
        //     buffer[i]++;
        // write(client_sockfd, buffer, nbytes_read);
        // if (newline_found)
        //     break;

        // convert to int and update global variable with trigger received from socket
        // NOTE: if below: > 1 does not make sense, and atof returns 0.0 if conversion is unsuccessful (e.g., the string is not a number)
        trigger = (int)atof(buffer);

        // we comment this part to allow the controller to set the number of iqs to save
        // if (trigger > 0)
        //     trigger = 1;
        // TODO improve this shit
        // if (trigger == 150) {
        //   pthread_mutex_lock(&(e2_agent_db->mutex));
        //   e2_agent_db->iq_mapping = trigger;
        //   pthread_mutex_unlock(&(e2_agent_db->mutex));
        // }

        e3_agent_control->trigger_iq_dump = trigger;
        fprintf(stdout, "Trigger is set to %d\n", trigger);
        fflush(stdout);
      }

      close(client_sockfd);
    }

    return EXIT_SUCCESS;
}

// This may be deprecated now that we are using T Tracer
// Marked to deletion
// void *dump_iqs_on_file(void* vargp) {

//     FILE * fp;
//     int16_t* data_to_dump = (int16_t*) vargp;

//     fp = fopen ("../../../iqs_dump/iqs.txt", "a+");

//     int prnt_idx;
//     for (prnt_idx=0;prnt_idx<12*5*2;prnt_idx+=2){
// //        fprintf(fp, "Received inside thread rxF[%d] = (%d,%d)\n", prnt_idx>>1, data_to_dump[prnt_idx],data_to_dump[prnt_idx+1]);
//         // fprintf(fp, "%d,%d,%d\n", prnt_idx>>1, data_to_dump[prnt_idx],data_to_dump[prnt_idx+1]);
//         fprintf(fp, "%d+%dj\n", data_to_dump[prnt_idx],data_to_dump[prnt_idx+1]);
//     }
//     fclose(fp);

//     return NULL;
// }

