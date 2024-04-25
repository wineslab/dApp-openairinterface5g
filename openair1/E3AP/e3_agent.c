#include "e3_agent.h"

#include "database.h"
#include "event.h"
#include "handler.h"
#include "utils.h"
#include "event_selector.h"
#include "config.h"

// TODO replace pthreads with itti once we (i) figure out how to link them
// #include "intertask_interface.h"
// #include "create_tasks.h"
#include <pthread.h>
#include <errno.h>
#include "configuration.h"
#include "logger/logger.h"

#include "common/utils/system.h"
#include "common/ran_context.h"
#include "common/utils/LOG/log.h"

pthread_t t_tracer_thread;
pthread_t e3_interface_thread;

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
      LOG_D(E3AP, "bad type for %s\n", var_name); \
      exit(1);                               \
    }                                        \
    var = i;                                 \
    continue;                                \
  }

e3_agent_controls_t* e3_agent_control = NULL;

int e3_agent_init() {
    e3_agent_control = (e3_agent_controls_t*) malloc(sizeof(e3_agent_controls_t));
    LOG_D(E3AP, "Setup T Tracer socket thread\n");
    if (pthread_create(&t_tracer_thread, NULL, e3_agent_t_tracer_task, NULL) != 0) {
      LOG_E(E3AP, "Error creating T tracer thread: %s\n", strerror(errno));
      return -1;
    }

    // TODO fix after linkage
    // if (itti_create_task(TASK_E3_AGENT, e3_agent_t_tracer_task, NULL) < 0) {
    //   LOG_E(E3AP, "cannot create ITTI task for T tracer\n");
    //   return -1;
    // }

    LOG_D(E3AP, "Setup E3 Interface socket thread\n");
    if (pthread_create(&e3_interface_thread, NULL, e3_agent_dapp_task, NULL) != 0) {
        LOG_E(E3AP, "Error creating E3 interface thread: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

int e3_agent_destroy(){

  if (pthread_join(t_tracer_thread, NULL) != 0) {
        LOG_E(E3AP, "Error joining T tracer thread: %s\n", strerror(errno));
        return -1;
  }
  
  if (pthread_join(e3_interface_thread, NULL) != 0) {
        LOG_E(E3AP, "Error joining E3 interface thread: %s\n", strerror(errno));
        return -1;
  }

  return 0;
}

void *e3_agent_t_tracer_task(void* args_p){
  e3_agent_tracer_info_t *tracer_info = (e3_agent_tracer_info_t *) malloc(sizeof(e3_agent_tracer_info_t));
  // This shall just setup the T tracer socket
  char *database_filename = T_MESSAGES_PATH;
  char *ip = DEFAULT_REMOTE_IP;
  int port = DEFAULT_REMOTE_PORT;
  int *is_on;
  int number_of_events;
  int i;
  int data = 0;
  int e3_agent_raw_iq_data_id;
  database_event_format f;

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

  /* activate the GNB_PHY_INPUT_SIGNAL trace in this array */
  on_off(tracer_info->database, "GNB_PHY_INPUT_SIGNAL", is_on, 1);
  /* connect to the nr-softmodem */
  tracer_info->socket = connect_to(ip, port);
  /* activate the trace GNB_PHY_INPUT_SIGNAL in the nr-softmodem */
  activate_traces(tracer_info->socket, number_of_events, is_on);
  free(is_on);

  /* get the format of the GNB_PHY_INPUT_SIGNAL trace */
  e3_agent_raw_iq_data_id = event_id_from_name(tracer_info->database, "GNB_PHY_INPUT_SIGNAL");
  f = get_format(tracer_info->database, e3_agent_raw_iq_data_id);

  /* get the elements of the GNB_PHY_INPUT_SIGNAL trace
   * the value is an index in the event, see below
   */
  for (i = 0; i < f.count; i++) {
    GET_DATA_FROM_TRACER("rxdata", "buffer", data);
  }

  /* a buffer needed to receive events from the nr-softmodem */
  OBUF ebuf = {osize : 0, omaxsize : 0, obuf : NULL};
  LOG_D(E3AP, "Start infinte loop\n");
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
      LOG_D(E3AP,"Get GNB_PHY_INPUT_SIGNAL event buffer length %d = [", e.e[data].bsize);
      for (i = 0; i < e.e[data].bsize; i++)
        LOG_D(E3AP," %2.2x", buf[i]);
      LOG_D(E3AP, "]\n");
    }
  }

  return NULL;
}

void *e3_agent_dapp_task(void* args_p){
    char buffer[BUFSIZ];
    char protoname[] = "tcp";
    struct protoent *protoent;
    int enable = 1;
    int server_sockfd, client_sockfd;
    socklen_t client_len;
    ssize_t nbytes_read;
    struct sockaddr_in client_address, server_address;
    unsigned short server_port = 12346u;

    protoent = getprotobyname(protoname);
    if (protoent == NULL) {
        LOG_E(E3AP, "Error getprotobyname: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    server_sockfd = socket(
        AF_INET,
        SOCK_STREAM,
        protoent->p_proto
        /* 0 */
    );

    if (server_sockfd == -1) {
        LOG_E(E3AP, "Error socket creation: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        LOG_E(E3AP, "Error setsockopt: %s\n", strerror(errno));
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
        LOG_E(E3AP, "Error bind: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (listen(server_sockfd, 5) == -1) {
        LOG_E(E3AP, "Error listen: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    LOG_D(E3AP, "E3 Agent is listening on port %d for dApps\n", server_port);
    while (1) {
      client_len = sizeof(client_address);
      client_sockfd = accept(server_sockfd, (struct sockaddr *) &client_address, &client_len);

      while ((nbytes_read = read(client_sockfd, buffer, BUFSIZ)) > 0) {
        // fprintf(stdout, "Received from socket:\n");
        // fflush(stdout);

        if (write(STDOUT_FILENO, buffer, nbytes_read) < 0) {
          LOG_E(E3AP, "write(STDOUT_FILENO) failed: %s\n", strerror(errno));
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
        LOG_D(E3AP, "buffer is %s", buffer);
      }

      close(client_sockfd);
    }

}