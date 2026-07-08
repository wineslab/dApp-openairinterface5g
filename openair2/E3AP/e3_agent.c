#include "e3_agent.h"
#include "config/e3_config.h"
#include "service_models/l1_kpm_sm/l1_kpm_sm.h"

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

float e3_get_fp16_beta(void)
{
  return e3.fp16_beta;
}

int e3_get_encoding(void)
{
  return e3.encoding;
}

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

/* Emit cadence for one RAN function: the fastest periodicity any subscribed
 * dApp declared in its subscription (microseconds, 0 = on-data). No
 * subscribers, or any subscriber without a periodicity, means on-data. */
static uint32_t min_subscription_period_us(uint32_t ran_function_id)
{
  size_t n = 0;
  uint32_t *dapps = e3_agent_get_ran_function_subscribers(e3.agent, ran_function_id, &n);
  uint32_t min_us = 0;
  for (size_t i = 0; i < n; i++) {
    const uint32_t p = e3_agent_get_subscription_periodicity(e3.agent, dapps[i], ran_function_id);
    if (p == 0) { /* on-data requested: fastest possible, wins outright */
      min_us = 0;
      break;
    }
    if (min_us == 0 || p < min_us)
      min_us = p;
  }
  e3_agent_free_uint32_array(dapps);
  return min_us;
}

void on_dapp_status_changed(void)
{
  LOG_I(E3AP, "dApp status changed, triggering RIC Service Update\n");
  l1_kpm_sm_set_period_us(min_subscription_period_us(E3_SM_ID_KPM));
#ifdef E2_AGENT
  notify_dapp_status_changed();
#endif
}


// True if SM id is in the configuration file's enabled_sms list, or if the list is
// empty/NULL (empty = enable every compiled-in SM).
static int sm_enabled(int32_t id, const int32_t *enabled, int n)
{
  if (!enabled || n <= 0)
    return 1;
  for (int i = 0; i < n; i++)
    if (enabled[i] == id)
      return 1;
  return 0;
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

  /* Create e3_config_t from e3_cmdline_configs. link/transport/encoding arrive
   * already mapped to the libe3 enum values by e3_readconfig. */
  e3_config_t config = {0};
  config.ran_identifier = "OAI DU";
  config.log_level = 3; // INFO

  config.link_layer      = e3_cmdline_configs->link_layer;
  config.transport_layer = e3_cmdline_configs->transport_layer;
  config.encoding        = e3_cmdline_configs->encoding;
  config.setup_port      = e3_cmdline_configs->setup_port;
  config.subscriber_port = e3_cmdline_configs->subscriber_port;
  config.publisher_port  = e3_cmdline_configs->publisher_port;
  // All other config fields (endpoints, io_threads, log_path) left as default (0 or NULL)
  e3.encoding         = e3_cmdline_configs->encoding;
  e3.fp16_beta        = e3_cmdline_configs->fp16_beta;

  // enabled_sms points into config-system-owned (PARAMFLAG_NOFREE) memory, so it
  // stays valid after the cmdline-config struct is freed below.
  int32_t *enabled_sms = e3_cmdline_configs->enabled_sms;
  int num_enabled_sms = e3_cmdline_configs->num_enabled_sms;

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

  err = e3_agent_set_dapp_report_handler(e3.agent, e2_e3_bridge);
  if (err != 0) {
    LOG_E(E3AP, "Failed to set dApp report handler (err=%d: %s)\n", err, e3_error_to_string(err));
    e3_agent_destroy(e3.agent);
    e3.agent = NULL;
    return -1;
  }

  err = e3_agent_set_dapp_status_changed_handler(e3.agent, on_dapp_status_changed);
  if (err != 0) {
    LOG_E(E3AP, "Failed to set dApp status changed handler (err=%d: %s)\n", err, e3_error_to_string(err));
    e3_agent_destroy(e3.agent);
    e3.agent = NULL;
    return -1;
  }
  
  // Register the SMs (each only if listed in enabled_sms, or all if the list is empty)
  // SM L1-KPM
  if (sm_enabled(E3_SM_ID_KPM, enabled_sms, num_enabled_sms)) {
    e3_c_service_model_desc_t *desc_sm_kpm = create_l1_kpm_sm_model();
    if (!desc_sm_kpm) {
      LOG_E(E3AP, "Failed to create L1-KPM SM descriptor\n");
      e3_agent_destroy(e3.agent);
      e3.agent = NULL;
      return -1;
    }
    e3_service_model_handle_t *sm_kpm = e3_service_model_create_from_c(desc_sm_kpm);
    if (!sm_kpm) {
      LOG_E(E3AP, "Failed to create L1-KPM SM handle\n");
      e3_agent_destroy(e3.agent);
      e3.agent = NULL;
      return -1;
    }

    l1_kpm_sm_set_handle(sm_kpm);

    err = e3_agent_register_sm(e3.agent, sm_kpm);
    if (err != 0) {
      LOG_E(E3AP, "Failed to register L1-KPM SM (err=%d: %s)\n", err, e3_error_to_string(err));
      e3_service_model_destroy(sm_kpm);
      e3_agent_destroy(e3.agent);
      e3.agent = NULL;
      return -1;
    }
  }

  /* Start LAST, once every SM and handler is in place: libe3's contract is
   * register-before-start. start() spawns the setup thread immediately, and a
   * dApp connecting before registration would get an empty ranFunctionList
   * (late registrations are accepted but never re-advertised); the report and
   * status handlers are plain function members read by the running threads,
   * so installing them post-start is a data race. */
  err = e3_agent_start(e3.agent);
  if (err != 0) {
    LOG_E(E3AP, "Failed to start E3Agent (err=%d)\n", err);
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

e3_dapp_subscription_map_t e3_get_dapp_subscription_map(void)
{
  e3_dapp_subscription_map_t map = {0};

  if (!e3.agent) {
    LOG_W(E3AP, "E3 agent not initialized: cannot query dApp subscriptions\n");
    return map;
  }

  size_t num_dapps = 0;
  uint32_t *dapp_ids = e3_agent_get_registered_dapps(e3.agent, &num_dapps);
  if (!dapp_ids || num_dapps == 0) {
    e3_agent_free_uint32_array(dapp_ids);
    return map;
  }

  map.dapps = (e3_dapp_info_t *)calloc(num_dapps, sizeof(e3_dapp_info_t));
  if (!map.dapps) {
    LOG_E(E3AP, "Failed to allocate dApp subscription map\n");
    e3_agent_free_uint32_array(dapp_ids);
    return map;
  }
  map.num_dapps = num_dapps;

  for (size_t i = 0; i < num_dapps; i++) {
    map.dapps[i].dapp_id = dapp_ids[i];

    size_t num_subs = 0;
    uint32_t *subs = e3_agent_get_dapp_subscriptions(e3.agent, dapp_ids[i], &num_subs);

    if (subs && num_subs > 0) {
      map.dapps[i].e3_ran_func_ids = subs; // transfer ownership from libe3 malloc
      map.dapps[i].num_e3_ran_funcs = num_subs;
    } else {
      map.dapps[i].e3_ran_func_ids = NULL;
      map.dapps[i].num_e3_ran_funcs = 0;
      e3_agent_free_uint32_array(subs);
    }
  }

  e3_agent_free_uint32_array(dapp_ids);
  return map;
}

void e3_free_dapp_subscription_map(e3_dapp_subscription_map_t *map)
{
  if (!map || !map->dapps)
    return;

  for (size_t i = 0; i < map->num_dapps; i++) {
    e3_agent_free_uint32_array(map->dapps[i].e3_ran_func_ids);
  }
  free(map->dapps);

  map->dapps = NULL;
  map->num_dapps = 0;
}
