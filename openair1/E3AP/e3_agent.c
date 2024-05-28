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
int *is_on;
e3_agent_tracer_info_t *tracer_info = NULL;

int e3_agent_init() {
  e3_agent_control = (e3_agent_controls_t*) malloc(sizeof(e3_agent_controls_t));
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

void e3_agent_t_tracer_init(void){
  tracer_info = (e3_agent_tracer_info_t *) malloc(sizeof(e3_agent_tracer_info_t));
  // This shall just setup the T tracer socket
  char *database_filename = T_MESSAGES_PATH;
  char *ip = DEFAULT_REMOTE_IP;
  int port = DEFAULT_REMOTE_PORT;
  int number_of_events;

  /* load the database T_messages.txt */
  tracer_info->database = parse_database(database_filename);
  load_config_file(database_filename);

  /* an array of int for all the events defined in the database is needed */
  number_of_events = number_of_ids(tracer_info->database);
  is_on = calloc(number_of_events, sizeof(int));
  if (is_on == NULL)
    abort();

  /* connect to the nr-softmodem */
  tracer_info->socket = connect_to(ip, port);
  activate_traces(tracer_info->socket, number_of_events, is_on);
}

int e3_agent_t_tracer_extract(void){

  int number_of_events;
  int i;
  int data = 0;
  int e3_agent_raw_iq_data_id;
  database_event_format f;
  int socket_d;

  /* write on a socket fails if the other end is closed and we get SIGPIPE */
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    abort();

  /* an array of int for all the events defined in the database is needed */
  number_of_events = number_of_ids(tracer_info->database);
  /* activate the GNB_PHY_UL_FREQ_SENSING_SYMBOL trace in this array */
  on_off(tracer_info->database, "GNB_PHY_UL_FREQ_SENSING_SYMBOL", is_on, 1);
  /* activate the trace GNB_PHY_UL_FREQ_SENSING_SYMBOL in the nr-softmodem */
  activate_traces(tracer_info->socket, number_of_events, is_on);

  char *ip_d = "127.0.0.1";
  socket_d = connect_to(ip_d, 9990);

  /* get the format of the GNB_PHY_UL_FREQ_SENSING_SYMBOL trace */
  e3_agent_raw_iq_data_id = event_id_from_name(tracer_info->database, "GNB_PHY_UL_FREQ_SENSING_SYMBOL");
  f = get_format(tracer_info->database, e3_agent_raw_iq_data_id);

  /* get the elements of the GNB_PHY_UL_FREQ_SENSING_SYMBOL trace
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
      unsigned char *buf = e.e[data].b;
      LOG_D(E3AP,"Get GNB_PHY_INPUT_SIGNAL event buffer length %d = [", e.e[data].bsize);
      if(e.e[data].bsize > 0){
        char pkt[e.e[data].bsize+4];
        pkt[0] = (e.e[data].bsize >> 24) & 0xFF;
        pkt[1] = (e.e[data].bsize >> 16) & 0xFF;
        pkt[2] = (e.e[data].bsize >> 8) & 0xFF;
        pkt[3] = e.e[data].bsize & 0xFF;
        memcpy(pkt+4,e.e[data].b,e.e[data].bsize);

        if (socket_send(socket_d, pkt, e.e[data].bsize+4) == -1){
          printf(" couldn't send the data");
          abort();
        }
      }

      for (i = 0; i < e.e[data].bsize; i++)
        LOG_D(E3AP," %2.2x", buf[i]);
      LOG_D(E3AP, "]\n");
    }
  }
  return 1;
}


void *e3_agent_dapp_task(void* args_p){
  // Init the t_tracer
  e3_agent_t_tracer_init();
  e3_agent_t_tracer_extract();
  return NULL;
}

