#include "e3_agent.h"

#include "database.h"
#include "event.h"
#include "handler.h"
#include "utils.h"
#include "event_selector.h"
#include "config.h"
#include <zmq.h>

// TODO replace pthreads with itti once we (i) figure out how to link them
// #include "intertask_interface.h"
// #include "create_tasks.h"
#include <pthread.h>
#include <errno.h>
#include "configuration.h"
#include "logger/logger.h"

#include "e3ap_handler.h"

#include "common/utils/system.h"
#include "common/ran_context.h"
#include "common/utils/LOG/log.h"
#include "e3_connector.h"

#define BUFFER_SIZE 60000

#define E3CONFIG_SECTION "E3Configuration"

#define CONFIG_STRING_E3_LINK_PARAM   "link"
#define CONFIG_STRING_E3_TRANSPORT_PARAM "transport"
#define CONFIG_STRING_E3_SAMPLING_PARAM "sampling"

/*-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
/*                                            configuration parameters for the rfsimulator device                                                                              */
/*   optname                     helpstr                     paramflags           XXXptr                               defXXXval                          type         numelt  */
/*-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
#define simOpt PARAMFLAG_NOFREE|PARAMFLAG_CMDLINE_NOPREFIXENABLED
// clang-format off
#define E3_PARAMS_DESC {					\
    {"link",             "Link layer for E3",        simOpt,  .strptr=&e3_configs->link,               .defstrval="posix",           TYPE_STRING,    0 },\
    {"transport",        "Transport layer for E3",   simOpt,  .strptr=&(e3_configs->transport),        .defstrval="ipc",                 TYPE_STRING,    0 },\
    {"sampling",         "Sampling of the sensed spectrum (0 means infinite)",    simOpt,  .iptr=&(e3_configs->sampling),      .defintval=0,                     TYPE_INT,       0 },\
  };
// clang-format on

const char *E3_VALID_CONFIGURATIONS[][2] = {
    {"zmq", "ipc"},
    {"zmq", "tcp"},
    // {"zmq", "sctp"}, // implemented but not working because zeromq does not support it (yet)
    {"posix", "tcp"},
    {"posix", "sctp"},
    {"posix", "ipc"}
};

typedef struct {
  e3_config_t *e3_configs;
  E3Connector *connector;
} pub_sub_args_t;

int *is_on;
e3_agent_tracer_info_t *tracer_info = NULL;
e3_agent_controls_t* e3_agent_control = NULL;
pthread_t e3_interface_thread;

/* this function sends the activated traces to the nr-softmodem */
void activate_traces(int socket, int number_of_events, int *is_on)
{
  char t = 1;
  if (socket_send(socket, &t, 1) == -1 || socket_send(socket, &number_of_events, sizeof(int)) == -1
      || socket_send(socket, is_on, number_of_events * sizeof(int)) == -1)
    abort();
}

/* this macro looks for a particular element and checks its type */
#define GET_DATA_FROM_TRACER(var_name, var_type, var) \
  if (!strcmp(f.name[i], var_name)) {                 \
    if (strcmp(f.type[i], var_type)) {                \
      LOG_E(E3AP, "bad type for %s\n", var_name);     \
      exit(1);                                        \
    }                                                 \
    var = i;                                          \
    continue;                                         \
  }


void e3_readconfig(e3_config_t *e3_configs){
  paramdef_t e3_params[] = E3_PARAMS_DESC;
  
  int ret = config_get(config_get_if(), e3_params, sizeof(e3_params)/sizeof(*(e3_params)), E3CONFIG_SECTION);
  AssertFatal(ret >= 0, "configuration couldn't be performed\n");

  LOG_I(E3AP,
        "this is the configuration extracted: link %s transport %s sampling %d\n",
        e3_configs->link,
        e3_configs->transport,
        e3_configs->sampling);
}

void validate_configuration(e3_config_t *config) {
    if (!config) {
        LOG_E(E3AP, "Configuration is null");
        abort();
    }

    // Check if link is "posix" or "zmq" using strncmp
    if (strncmp(config->link, "posix", 5) != 0 && strncmp(config->link, "zmq", 3) != 0) {
        LOG_E(E3AP, "Wrong link");
        abort();
    }

    // Check if transport is "tcp", "sctp", or "ipc" using strncmp
    if (strncmp(config->transport, "tcp", 3) != 0 &&
        strncmp(config->transport, "sctp", 4) != 0 &&
        strncmp(config->transport, "ipc", 3) != 0) {
        LOG_E(E3AP, "Wrong transport");
        abort();
    }

    // Validate the combination of link and transport
    int combo_valid = 0;
    for (size_t i = 0; i < sizeof(E3_VALID_CONFIGURATIONS) / sizeof(E3_VALID_CONFIGURATIONS[0]); i++) {
        if (strncmp(config->link, E3_VALID_CONFIGURATIONS[i][0], strlen(E3_VALID_CONFIGURATIONS[i][0])) == 0 &&
            strncmp(config->transport, E3_VALID_CONFIGURATIONS[i][1], strlen(E3_VALID_CONFIGURATIONS[i][1])) == 0) {
            combo_valid = 1;
            break;
        }
    }

    if (!combo_valid) {
        LOG_E(E3AP, "Wrong combination");
        abort();
    }
}

int e3_agent_init()
{
  LOG_D(E3AP, "Read configuration\n");
  e3_config_t *e3_configs = (e3_config_t *)calloc(sizeof(e3_config_t), 1);
  e3_readconfig(e3_configs);
  LOG_D(E3AP, "Validate configuration\n");
  validate_configuration(e3_configs);

  tracer_info = (e3_agent_tracer_info_t *)malloc(sizeof(e3_agent_tracer_info_t));
  e3_agent_control = (e3_agent_controls_t *)malloc(sizeof(e3_agent_controls_t));
  
  pthread_mutex_init(&e3_agent_control->mutex, NULL);
  pthread_cond_init(&e3_agent_control->cond, NULL);
  e3_agent_control->ready = 0;

  LOG_D(E3AP, "Start E3 Agent main thread\n");
  if (pthread_create(&e3_interface_thread, NULL, e3_agent_dapp_task, (void *) e3_configs) != 0) {
    LOG_E(E3AP, "Error creating E3 Agent thread: %s\n", strerror(errno));
    return -1;
  }
  return 0;
}

int e3_agent_destroy()
{
  if (pthread_join(e3_interface_thread, NULL) != 0) {
    LOG_E(E3AP, "Error joining E3 interface thread: %s\n", strerror(errno));
    return -1;
  }


  return 0;
}

void e3_agent_t_tracer_init(void)
{
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

// The subscriber thread is responsible of receiving the control action messages and the dApp report messages
void *subscriber_thread(void *arg)
{
  E3Connector *e3connector = (E3Connector *)arg;
  uint8_t *buffer = malloc(BUFFER_SIZE);
  size_t buffer_size = BUFFER_SIZE;

  int ret;
  int action_list_size;

  e3connector->setup_inbound_connection(e3connector);

  while (1) {
    ret = e3connector->receive(e3connector, buffer, buffer_size);
    if (ret < 0) {
      LOG_E(E3AP, "Error in inbound connection: %s\n", strerror(errno));
      abort();
    }
    if (ret == 0) {
      LOG_I(E3AP, "No bytes received in the inbound connection, closing\n");
      break;
    }

    E3_PDU_t *controlAction = decode_E3_PDU(buffer, ret);

    // xer_fprint(stderr, &asn_DEF_E3_ControlAction, controlAction);
    // xer_fprint(stderr, &asn_DEF_E3_SetupResponse, controlAction);

    if (controlAction->present == E3_PDU_PR_controlAction) {
      // This code can be improved, especially the internal function
      u_int8_t *controlPayload = parse_control_action(controlAction->choice.controlAction);
  
      action_list_size = ((controlPayload[1]<<8) & 0xFF) | (controlPayload[0] & 0xFF);

      LOG_D(E3AP, "action_list_size = %d\n", action_list_size);
      pthread_mutex_lock(&e3_agent_control->mutex);
      for (size_t i = 0; i < controlAction->choice.controlAction->actionData.size; i++) {
        LOG_D(E3AP, "controlPayload[%zu] = %u\n", i, controlPayload[i]);
      }
      e3_agent_control->action_list = (char *)malloc(action_list_size * sizeof(uint16_t));
      memcpy(e3_agent_control->action_list, controlPayload + 2, action_list_size * sizeof(uint16_t));
      
      e3_agent_control->action_size = action_list_size;
      for (size_t i = 0; i < action_list_size; i++) {
          LOG_D(E3AP, "e3_agent_control[%zu] = %d\n", i, ((uint16_t*)e3_agent_control->action_list)[i]);
      }

      e3_agent_control->ready = 1; // Set ready flag to 1 to indicate data is available
      pthread_cond_signal(&e3_agent_control->cond); // Notify consumer
      pthread_mutex_unlock(&e3_agent_control->mutex);

      free(controlPayload);

    } else {
      LOG_E(E3AP, "Unexpected PDU choice instead of control: %d\n", controlAction->present);
      break;
    }

    free_E3_PDU(controlAction);
  }

  return NULL;
}

// The publisher thread is responsible of sending the indication messages and the xApp control actions
void *publisher_thread(void *arg)
{
  pub_sub_args_t *pub_sub_args = (pub_sub_args_t *)arg;
  E3Connector *e3connector = pub_sub_args->connector;
  int number_of_events;
  int i;
  int data = 0;
  int e3_agent_raw_iq_data_id;
  database_event_format f;

  // Each sensing is done once every 10ms * sampling_threshold
  // this stays here since an xApp or a dApp can potentially change the threshold value
  e3_agent_control->sampling_threshold = pub_sub_args->e3_configs->sampling; // one delivery each sampling_threshold samples captures
  e3_agent_control->sampling_counter = 0;

  /* write on a socket fails if the other end is closed and we get SIGPIPE */
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    abort();

  /* an array of int for all the events defined in the database is needed */
  number_of_events = number_of_ids(tracer_info->database);
  /* activate the GNB_PHY_UL_FREQ_SENSING_SYMBOL trace in this array */
  on_off(tracer_info->database, "GNB_PHY_UL_FREQ_SENSING_SYMBOL", is_on, 1);
  /* activate the trace GNB_PHY_UL_FREQ_SENSING_SYMBOL in the nr-softmodem */
  activate_traces(tracer_info->socket, number_of_events, is_on);

  // Create a socket and connect to the dApp
  size_t buffer_size = BUFFER_SIZE;
  uint8_t *buffer = malloc(buffer_size);
  int ret;

  /* get the format of the GNB_PHY_UL_FREQ_SENSING_SYMBOL trace */
  e3_agent_raw_iq_data_id = event_id_from_name(tracer_info->database, "GNB_PHY_UL_FREQ_SENSING_SYMBOL");
  f = get_format(tracer_info->database, e3_agent_raw_iq_data_id);

  /* get the elements of the GNB_PHY_UL_FREQ_SENSING_SYMBOL trace
   * the value is an index in the event, see below
   */
  for (i = 0; i < f.count; i++) {
    GET_DATA_FROM_TRACER("rxdata", "buffer", data);
  }

  do {
    sleep(3);
    LOG_I(E3AP, "Trying to setup the outbound connection\n");
    ret = e3connector->setup_outbound_connection(e3connector);
    if (ret != 0) {
      LOG_D(E3AP, "Failed to send Indication PDU: %s\n", strerror(errno));
    } else {
      LOG_I(E3AP, "Outbound connection setup\n");
    }
  } while (ret);

  // We only implement the indication messages atm

  /* a buffer needed to receive events from the nr-softmodem */
  OBUF ebuf = {osize : 0, omaxsize : 0, obuf : NULL};
  LOG_D(E3AP, "Start infinite loop\n");
  /* read events */

  // size_t fake_payload_length = sizeof(int32_t)*768 ;
  // const int32_t *fake_payload = (int32_t *) calloc(768, sizeof(int32_t));

  while (1) {
    event e;
    e = get_event(tracer_info->socket, &ebuf, tracer_info->database);
    if (e.type == -1)
      break;
    if (e.type == e3_agent_raw_iq_data_id) {
      LOG_D(E3AP, "Get GNB_PHY_UL_FREQ_SENSING_SYMBOL event buffer length %d\n", e.e[data].bsize);
      for (size_t i = 0; i < 10; i++) {
        LOG_D(E3AP, "e.e[data].b[%zu] = %d\n", i, ((int32_t *)e.e[data].b)[i]);
      }

      if (e.e[data].bsize > 0) {
        // E3_PDU_t *indicationMessage = create_indication_message(fake_payload, fake_payload_length);        
        E3_PDU_t *indicationMessage = create_indication_message(e.e[data].b, e.e[data].bsize);

        if (encode_E3_PDU(indicationMessage, &buffer, &buffer_size) == 0) {
          ret = e3connector->send(e3connector, buffer, buffer_size);
          free_E3_PDU(indicationMessage);
          if (ret < 0) {
            LOG_E(E3AP, "Failed to send Indication PDU: %s\n", strerror(errno));
            break;
          } else {
            LOG_D(E3AP, "Delivered message correctly\n");
          }
        } else {
          LOG_E(E3AP, "Failed to encode Indication PDU\n");
          break;
        }
      }
    }
  }

  return NULL;
}

void *e3_agent_dapp_task(void *args_p)
{
  e3_config_t *e3_configs = (e3_config_t *)args_p;

  LOG_D(E3AP, "Init tracer configuration\n");
  // Init the t_tracer
  e3_agent_t_tracer_init();
  LOG_D(E3AP, "Tracer initialized\n");

  int ret;

  pthread_t pub_thread, sub_thread;

  E3Connector* e3connector = create_connector(e3_configs->link, e3_configs->transport);
  if (e3connector == NULL) {
    LOG_E(E3AP, "Failed to create the E3Connector\n");
    abort();
  }
  uint8_t *buffer = malloc(BUFFER_SIZE);
  size_t buffer_size = BUFFER_SIZE;

  LOG_D(E3AP, "Create sub_thread\n");
  pthread_create(&sub_thread, NULL, subscriber_thread, (void*)e3connector);

  LOG_D(E3AP, "Create pub_thread\n");
  pub_sub_args_t *pub_sub_args = malloc(sizeof(pub_sub_args_t));
  pub_sub_args->e3_configs = e3_configs;
  pub_sub_args->connector = e3connector;
  pthread_create(&pub_thread, NULL, publisher_thread, (void*)pub_sub_args);

  LOG_D(E3AP, "Setup connection\n");
  ret = e3connector->setup_initial_connection(e3connector);
  if (ret < 0) {
    LOG_E(E3AP, "Bind in setup initial connection failed: %s\n", strerror(errno));
    abort();
  }

  LOG_D(E3AP, "Start setup loop\n");
  while (1) {
    ret = e3connector->recv_setup_request(e3connector, buffer, buffer_size);
    E3_PDU_t *setupRequest = decode_E3_PDU(buffer, ret);

    if (setupRequest->present == E3_PDU_PR_setupRequest) {
      // No need to parse atm, just send the response
      E3_PDU_t *setupResponse = create_setup_response(0); // 0 is positive, 1 is negative
      if (encode_E3_PDU(setupResponse, &buffer, &buffer_size) == 0) {
        e3connector->send_response(e3connector, buffer, buffer_size);
        free_E3_PDU(setupResponse);
      } else {
        LOG_E(E3AP, "Failed to encode PDU\n");
      }
    } else {
      LOG_E(E3AP, "Unexpected PDU choice\n");
      e3connector->dispose(e3connector);
    }

    free_E3_PDU(setupRequest);
    buffer_size = BUFFER_SIZE; // reset the variable
  }

  pthread_join(sub_thread, NULL);
  pthread_join(pub_thread, NULL);

  free(buffer);
  
  pthread_mutex_destroy(&e3_agent_control->mutex);
  pthread_cond_destroy(&e3_agent_control->cond);

  e3connector->dispose(e3connector);

  return NULL;
}
