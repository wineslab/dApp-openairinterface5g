#include "e3_agent.h"
#include "config/e3_config.h"
#include "service_models/spectrum_sm/spectrum_sm.h"

// TODO replace pthreads with itti or use a faster way
// #include "intertask_interface.h"
// #include "create_tasks.h"
#include <pthread.h>
#include <errno.h>
#include <time.h>

#include <libe3/c_api.h>

#include "common/utils/system.h"
#include "common/ran_context.h"
#include "common/utils/LOG/log.h"

e3_agent_global_t e3 = {0};

static void e2_e3_bridge(uint32_t dapp_id,
                              uint32_t ran_function_id,
                              const uint8_t *report_data,
                              size_t report_size)
{
  LOG_D(E3AP,
        "Received dApp report for RAN function %u from dApp %u (%zu bytes)\n",
        ran_function_id,
        dapp_id,
        report_size);
#ifdef E2_AGENT
  if (!report_data && report_size > 0) {
    LOG_E(E3AP, "Invalid dApp report payload: report_data is NULL while report_size=%zu\n", report_size);
    return;
  }
  generate_e2_indication_from_e3_dapp_report(ran_function_id,
                                              dapp_id,
                                              report_size,
                                              report_data);
#else
  (void)report_data;
#endif
}


int e3_init()
{
  LOG_D(E3AP, "Read configuration\n");
  e3_cmdline_config_t *e3_cmdline_configs =
      (e3_cmdline_config_t *)calloc(1, sizeof(e3_cmdline_config_t));
  if (!e3_cmdline_configs) {
    LOG_E(E3AP, "Failed to allocate E3 cmdline config\n");
    return -1;
  }
  e3_readconfig(e3_cmdline_configs);
  LOG_D(E3AP, "Validate configuration\n");
  validate_configuration(e3_cmdline_configs);

  // Create e3_config_t from e3_cmdline_configs (only link and transport, rest defaults)
  e3_config_t config = {0};
  config.ran_identifier = "OAI DU";
  config.log_level = 3; // INFO

#ifdef E3_ASN1_FORMAT
  config.encoding = 0;
#else
  config.encoding = 1;
#endif

  // Map string values to enum/int as needed for link_layer and transport_layer
  if (e3_cmdline_configs->link && strcmp(e3_cmdline_configs->link, "zmq") == 0) {
    config.link_layer = 0; // ZMQ
  } else if (e3_cmdline_configs->link && strcmp(e3_cmdline_configs->link, "posix") == 0) {
    config.link_layer = 1; // POSIX
  } else {
    config.link_layer = -1; // default
  }

  if (e3_cmdline_configs->transport && strcmp(e3_cmdline_configs->transport, "sctp") == 0) {
    config.transport_layer = 0; // SCTP
  } else if (e3_cmdline_configs->transport && strcmp(e3_cmdline_configs->transport, "tcp") == 0) {
    config.transport_layer = 1; // TCP
  } else if (e3_cmdline_configs->transport && strcmp(e3_cmdline_configs->transport, "ipc") == 0) {
    config.transport_layer = 2; // IPC
  } else {
    config.transport_layer = -1; // default
  }

  // All other fields left as default (0 or NULL)

  // Create E3Agent with config
  e3.agent = e3_agent_create_with_config(&config);
  free(e3_cmdline_configs);
  e3_cmdline_configs = NULL;
  if (!e3.agent) {
    LOG_E(E3AP, "Failed to create E3Agent with config\n");
    return -1;
  }

  // Initialize agent
  e3_error_t err = e3_agent_init(e3.agent);
  if (err != 0) {
    LOG_E(E3AP, "Failed to initialize E3Agent (err=%d)\n", err);
    e3_agent_destroy(e3.agent);
    e3.agent = NULL;
    return -1;
  }

  // Start agent
  err = e3_agent_start(e3.agent);
  if (err != 0) {
    LOG_E(E3AP, "Failed to start E3Agent (err=%d)\n", err);
    e3_agent_destroy(e3.agent);
    e3.agent = NULL;
    return -1;
  }

  err = e3_agent_set_dapp_report_handler(e3.agent, e2_e3_bridge);
  if (err != 0) {
    LOG_E(E3AP, "Failed to set dApp report handler (err=%d: %s)\n", err, e3_error_to_string(err));
    e3_agent_destroy(e3.agent);
    e3.agent = NULL;
    return -1;
  }
  
  // Register the SMs
  // SM Spectrum
  e3_c_service_model_desc_t *desc_sm_spectrum = create_spectrum_sm_model();
  if (!desc_sm_spectrum) {
    LOG_E(E3AP, "Failed to create Spectrum SM descriptor\n");
    e3_agent_destroy(e3.agent);
    e3.agent = NULL;
    return -1;
  }
  e3_service_model_handle_t *sm_spectrum = e3_service_model_create_from_c(desc_sm_spectrum);
  if (!sm_spectrum) {
    LOG_E(E3AP, "Failed to create Spectrum SM handle\n");
    e3_agent_destroy(e3.agent);
    e3.agent = NULL;
    return -1;
  }

  spectrum_sm_set_handle(sm_spectrum);

  err = e3_agent_register_sm(e3.agent, sm_spectrum);
  if (err != 0) {
    LOG_E(E3AP, "Failed to register Spectrum SM (err=%d: %s)\n", err, e3_error_to_string(err));
    e3_service_model_destroy(sm_spectrum);
    e3_agent_destroy(e3.agent);
    e3.agent = NULL;
    return -1;
  }

  return 0;
}

int e3_destroy()
{
  // Stop and destroy the E3Agent if it exists
  if (e3.agent) {
    e3_agent_stop(e3.agent);
    e3_agent_destroy(e3.agent);
    e3.agent = NULL;
  }

  return 0;
}

int e3_send_xapp_control(uint32_t dapp_id,
                         uint32_t ran_function_id,
                         const uint8_t *data,
                         size_t len)
{
  if (!e3.agent) {
    LOG_E(E3AP, "E3 agent not initialized: cannot send xApp control\n");
    return -1;
  }
  
  if (data == NULL && len > 0) {
    LOG_E(E3AP, "data is not initialized, but len > 0\n");
    return -1;
  }

  e3_error_t err = e3_agent_send_xapp_control(e3.agent, dapp_id, ran_function_id, data, len);
  if (err != E3_SUCCESS) {
    LOG_E(E3AP,
          "Failed to send xApp control to dApp %u for RAN function %u (err=%d)\n",
          dapp_id,
          ran_function_id,
          err);
    return -1;
  }
  return 0;
}
