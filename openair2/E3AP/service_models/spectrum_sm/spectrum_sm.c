#include "spectrum_sm.h"
#include "spectrum_enc.h"
#include "spectrum_dec.h"

#include "../../e3_agent.h"

#include "database.h"
#include "event.h"
#include "handler.h"
#include "utils.h"
#include "event_selector.h"
#include "configuration.h"
#include "logger/logger.h"

#include "common/utils/LOG/log.h"

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

// T-tracer constants
#define DEFAULT_REMOTE_IP "127.0.0.1"
#define DEFAULT_REMOTE_PORT 2021

// SM default constants
#define DEFAULT_SM_SAMPLING_THRESHOLD 5

e3_sm_spectrum_control_t *e3_sm_spectrum_control = NULL;
static spectrum_sm_context_t spectrum_ctx = {0};
static uint8_t *spectrum_ran_function_data = NULL;
static size_t spectrum_ran_function_data_len = 0;
static int spectrum_ran_function_data_ready = 0;


// Spectrum SM RAN function IDs
#define SPECTRUM_SM_RAN_FUNCTION_ID 1

static uint32_t spectrum_telemetry_ids[] = {SPECTRUM_SM_RAN_FUNCTION_ID};
static uint32_t spectrum_control_ids[] = {SPECTRUM_SM_RAN_FUNCTION_ID};

/* this macro looks for a particular element and checks its type */
#define GET_DATA_FROM_TRACER(var_name, var_type, var) \
  if (!strcmp(f.name[i], var_name)) {                 \
    if (strcmp(f.type[i], var_type)) {                \
      LOG_E(E3AP, "bad type for %s\n", var_name);     \
      return NULL;                                     \
    }                                                 \
    var = i;                                          \
    continue;                                         \
  }

static e3_error_t spectrum_sm_init(void *sm_context);
static void spectrum_sm_destroy(void *sm_context);
static e3_error_t spectrum_sm_start(void *sm_context);
static void spectrum_sm_stop(void *sm_context);
static int spectrum_sm_is_running(void *sm_context);
static e3_error_t spectrum_sm_process_control(e3_service_model_handle_t* sm_handle,
                                              void* sm_context,
                                              uint32_t request_message_id,
                                              uint32_t dapp_id,
                                              uint32_t ran_function_id,
                                              uint32_t control_id,
                                              const uint8_t* data,
                                              size_t data_len);

static e3_c_service_model_desc_t spectrum_sm_desc = {
    .name = "spectrum_sm",
    .version = 1,
    .ran_function_id = SPECTRUM_SM_RAN_FUNCTION_ID,
    .telemetry_ids = spectrum_telemetry_ids,
    .telemetry_ids_len = sizeof(spectrum_telemetry_ids) / sizeof(spectrum_telemetry_ids[0]),
    .control_ids = spectrum_control_ids,
    .control_ids_len = sizeof(spectrum_control_ids) / sizeof(spectrum_control_ids[0]),
    .ran_function_data = NULL,
    .ran_function_data_len = 0,
    .sm_init = spectrum_sm_init,
    .sm_destroy = spectrum_sm_destroy,
    .sm_start = spectrum_sm_start,
    .sm_stop = spectrum_sm_stop,
    .sm_is_running = spectrum_sm_is_running,
    .sm_process_control = spectrum_sm_process_control,
    .sm_context = &spectrum_ctx,
};

static int is_running_locked(const spectrum_sm_context_t *ctx)
{
  return ctx->running ? 1 : 0;
}

void spectrum_sm_set_handle(e3_service_model_handle_t *sm_handle)
{
  pthread_mutex_lock(&spectrum_ctx.lock);
  spectrum_ctx.sm_handle = sm_handle;
  pthread_mutex_unlock(&spectrum_ctx.lock);
}

e3_c_service_model_desc_t *create_spectrum_sm_model(void)
{
  if (!spectrum_ran_function_data_ready) {
#ifdef SPECTRUM_SM_ASN1_FORMAT
    LOG_I(E3AP, "[SPECTRUM] Encoding RAN function data with ASN.1 encoder\n");
#else
    LOG_I(E3AP, "[SPECTRUM] Encoding RAN function data with JSON encoder\n");
#endif
    int rc = spectrum_encode_ran_function_data(&spectrum_ran_function_data,
                                               &spectrum_ran_function_data_len);
    if (rc != E3_SUCCESS) {
      LOG_E(E3AP, "[SPECTRUM] Failed to encode RAN function data (%d)\n", rc);
      spectrum_ran_function_data = NULL;
      spectrum_ran_function_data_len = 0;
    }
    spectrum_ran_function_data_ready = 1;
  }

  spectrum_sm_desc.ran_function_data = spectrum_ran_function_data;
  spectrum_sm_desc.ran_function_data_len = spectrum_ran_function_data_len;
  return &spectrum_sm_desc;
}

static void activate_traces(int socket, int number_of_events, int *is_on)
{
  char t = 1;
  if (socket_send(socket, &t, 1) == -1 ||
      socket_send(socket, &number_of_events, sizeof(int)) == -1 ||
      socket_send(socket, is_on, number_of_events * sizeof(int)) == -1) {
    LOG_E(E3AP, "[SPECTRUM] Failed to activate T-tracer events\n");
    return;
  }

  LOG_I(E3AP, "[SPECTRUM] T-tracer events activated\n");
}

static void *t_tracer_connector_thread_main(void *context)
{
  spectrum_sm_context_t *ctx = (spectrum_sm_context_t *)context;
  if (!ctx) {
    return NULL;
  }

  while (1) {
    pthread_mutex_lock(&ctx->lock);
    int keep_running = ctx->connector_running ? 1 : 0;
    int has_socket = (ctx->t_tracer_socket >= 0) ? 1 : 0;
    int prepared = (ctx->database != NULL && ctx->event_flags != NULL) ? 1 : 0;
    pthread_mutex_unlock(&ctx->lock);

    if (!keep_running) {
      break;
    }

    if (!prepared) {
      char *database_filename = T_MESSAGES_PATH;
      LOG_D(E3AP, "[SPECTRUM] Loading T-tracer database\n");
      void *db = parse_database(database_filename);
      if (!db) {
        LOG_E(E3AP, "[SPECTRUM] Failed to parse T-tracer database\n");
        sleep(1);
        continue;
      }
      load_config_file(database_filename);

      int n_events = number_of_ids(db);
      int *flags = calloc(n_events, sizeof(int));
      if (!flags) {
        LOG_E(E3AP, "[SPECTRUM] Failed to allocate event flags array\n");
        free_database(db);
        sleep(1);
        continue;
      }

      pthread_mutex_lock(&ctx->lock);
      if (!ctx->database && !ctx->event_flags) {
        ctx->database = db;
        ctx->number_of_events = n_events;
        ctx->event_flags = flags;
      } else {
        free(flags);
        free_database(db);
      }
      pthread_mutex_unlock(&ctx->lock);
      continue;
    }

    if (has_socket) {
      sleep(1);
      continue;
    }

    int s = try_connect_to((char *)DEFAULT_REMOTE_IP, DEFAULT_REMOTE_PORT);
    if (s < 0) {
      sleep(1);
      continue;
    }

    int should_activate = 0;
    pthread_mutex_lock(&ctx->lock);
    if (ctx->t_tracer_socket < 0) {
      ctx->t_tracer_socket = s;
      ctx->tracer_ready = true;
      should_activate = 1;
      LOG_I(E3AP, "[SPECTRUM] Connected to T-tracer at %s:%d\n",
            DEFAULT_REMOTE_IP,
            DEFAULT_REMOTE_PORT);
    } else {
      close(s);
    }
    pthread_mutex_unlock(&ctx->lock);

    if (should_activate) {
      /* Keep old init behavior: activate traces right after successful connect. */
      activate_traces(ctx->t_tracer_socket, ctx->number_of_events, ctx->event_flags);
    }
  }

  return NULL;
}

static int wait_for_t_tracer_ready(spectrum_sm_context_t *ctx)
{
  while (1) {
    pthread_mutex_lock(&ctx->lock);
    int running = is_running_locked(ctx);
    int sock = ctx->t_tracer_socket;
    int ready = ctx->tracer_ready ? 1 : 0;
    pthread_mutex_unlock(&ctx->lock);

    if (!running) {
      return -1;
    }
    if (ready && sock >= 0) {
      return sock;
    }
    sleep(1);
  }
}

static void *spectrum_sm_thread_main(void *context)
{
  spectrum_sm_context_t *ctx = (spectrum_sm_context_t *)context;
  OBUF ebuf = {osize : 0, omaxsize : 0, obuf : NULL};

  if (!ctx || !ctx->initialized) {
    LOG_E(E3AP, "[SPECTRUM] SM not initialized, exiting worker\n");
    return NULL;
  }

  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
    LOG_E(E3AP, "[SPECTRUM] Failed to set SIGPIPE handler\n");
    return NULL;
  }

  if (wait_for_t_tracer_ready(ctx) < 0) {
    LOG_E(E3AP, "[SPECTRUM] T-tracer not ready, exiting worker\n");
    return NULL;
  }
  on_off(ctx->database, "GNB_PHY_UL_FREQ_SENSING_SYMBOL", ctx->event_flags, 1);
  activate_traces(ctx->t_tracer_socket, ctx->number_of_events, ctx->event_flags);

  ctx->iq_data_event_id = event_id_from_name(ctx->database, "GNB_PHY_UL_FREQ_SENSING_SYMBOL");
  database_event_format f = get_format(ctx->database, ctx->iq_data_event_id);

  int data_index = -1;
  for (int i = 0; i < f.count; i++) {
    GET_DATA_FROM_TRACER("rxdata", "buffer", data_index);
  }

  if (data_index == -1) {
    LOG_E(E3AP, "[SPECTRUM] Failed to find rxdata in event format\n");
    return NULL;
  }

  LOG_I(E3AP, "[SPECTRUM] Start main extraction loop\n");

  while (1) {
    pthread_mutex_lock(&ctx->lock);
    int running = is_running_locked(ctx);
    e3_service_model_handle_t *sm_handle = ctx->sm_handle;
    pthread_mutex_unlock(&ctx->lock);

    if (!running) {
      LOG_D(E3AP, "[SPECTRUM] Worker loop stopping (running=0)\n");
      break;
    }

    if (ctx->t_tracer_socket < 0 && wait_for_t_tracer_ready(ctx) < 0) {
      LOG_E(E3AP, "[SPECTRUM] T-tracer became unavailable, stopping worker loop\n");
      break;
    }

    event e = get_event(ctx->t_tracer_socket, &ebuf, ctx->database);
    if (e.type == -1) {
      LOG_W(E3AP, "[SPECTRUM] T-tracer connection lost\n");
      pthread_mutex_lock(&ctx->lock);
      if (ctx->t_tracer_socket >= 0) {
        close(ctx->t_tracer_socket);
        ctx->t_tracer_socket = -1;
        ctx->tracer_ready = false;
      }
      pthread_mutex_unlock(&ctx->lock);
      continue;
    }

    if (e.type != ctx->iq_data_event_id) {
      continue;
    }

    if (!sm_handle) {
      LOG_E(E3AP, "[SPECTRUM] Missing SM handle, cannot emit indication\n");
      continue;
    }

    LOG_D(E3AP,
          "[SPECTRUM] IQ event received (sampling handled in PHY producer, threshold=%u)\n",
          e3_sm_spectrum_control->sampling_threshold);

    uint8_t *encoded_data = NULL;
    size_t encoded_size = 0;
    LOG_D(E3AP, "[SPECTRUM] Encoding indication from rxdata buffer (%d bytes)\n",
          e.e[data_index].bsize);
    int rc = spectrum_encode_indication(e.e[data_index].b,
                                        e.e[data_index].bsize,
                                        0,
                                        &encoded_data,
                                        &encoded_size);
    if (rc != E3_SUCCESS || !encoded_data) {
      LOG_E(E3AP,
            "[SPECTRUM] Failed to encode indication (rc=%d, rxdata_size=%d)\n",
            rc,
            e.e[data_index].bsize);
      continue;
    }
    LOG_D(E3AP, "[SPECTRUM] Encoded indication payload (%zu bytes)\n", encoded_size);

    size_t dapp_len = 0;
    uint32_t *dapp_ids = e3_agent_get_ran_function_subscribers(e3.agent,
                                                                SPECTRUM_SM_RAN_FUNCTION_ID,
                                                                &dapp_len);
    LOG_D(E3AP, "[SPECTRUM] Found %zu subscribers for ran_function_id=%u\n",
          dapp_len,
          SPECTRUM_SM_RAN_FUNCTION_ID);
    
    if (dapp_ids) {
      if (dapp_len > 0) {
        for (size_t i = 0; i < dapp_len; ++i) {
          e3_error_t emit_rc =
              e3_service_model_emit_indication(sm_handle, dapp_ids[i], SPECTRUM_SM_RAN_FUNCTION_ID, encoded_data, encoded_size);
          if (emit_rc != E3_SUCCESS) {
            LOG_E(E3AP, "[SPECTRUM] Failed to emit indication for dApp %u (err=%d)\n", dapp_ids[i], emit_rc);
          } else {
            LOG_D(E3AP, "[SPECTRUM] Emitted indication to dApp %u (%zu bytes)\n", dapp_ids[i], encoded_size);
          }
        }
      } else {
        size_t total_subscriptions = e3_agent_subscription_count(e3.agent);
        size_t registered_dapp_len = 0;
        uint32_t *registered_dapps = e3_agent_get_registered_dapps(e3.agent, &registered_dapp_len);
        LOG_E(E3AP,
              "[SPECTRUM] No subscribers for ran_function_id=%u "
              "(total_subscriptions=%zu, registered_dapps=%zu)\n",
              SPECTRUM_SM_RAN_FUNCTION_ID,
              total_subscriptions,
              registered_dapp_len);
        if (registered_dapps) {
          e3_agent_free_uint32_array(registered_dapps);
        }
      }
    }
    e3_agent_free_uint32_array(dapp_ids);

    free(encoded_data);
  }

  if (ebuf.obuf) {
    free(ebuf.obuf);
  }
  return NULL;
}

static e3_error_t spectrum_sm_init(void *sm_context)
{
  spectrum_sm_context_t *ctx = (spectrum_sm_context_t *)sm_context;
  if (!ctx) {
    return E3_SM_ERROR_INVALID_PARAM;
  }

  /* Preserve handle set before SM registration; memset below would clear it. */
  e3_service_model_handle_t *preset_sm_handle = ctx->sm_handle;
  memset(ctx, 0, sizeof(*ctx));
  ctx->t_tracer_socket = -1;
  ctx->sm_handle = preset_sm_handle;
  pthread_mutex_init(&ctx->lock, NULL);
  ctx->initialized = true;
  LOG_D(E3AP, "[SPECTRUM] SM init complete (preset_sm_handle=%p)\n", (void *)ctx->sm_handle);

  ctx->connector_running = true;
  if (pthread_create(&ctx->connector_thread, NULL, t_tracer_connector_thread_main, ctx) != 0) {
    spectrum_sm_destroy(ctx);
    return E3_SM_ERROR_THREAD_FAILED;
  }
  ctx->connector_started = true;

  e3_sm_spectrum_control = calloc(1, sizeof(*e3_sm_spectrum_control));
  if (!e3_sm_spectrum_control) {
    spectrum_sm_destroy(ctx);
    return E3_SM_ERROR_MEMORY;
  }

  e3_sm_spectrum_control->action_list = calloc(MAX_BWP_SIZE, sizeof(uint16_t));
  if (!e3_sm_spectrum_control->action_list) {
    spectrum_sm_destroy(ctx);
    return E3_SM_ERROR_MEMORY;
  }

  pthread_mutex_init(&e3_sm_spectrum_control->mutex, NULL);
  e3_sm_spectrum_control->sampling_threshold = DEFAULT_SM_SAMPLING_THRESHOLD;

  return E3_SUCCESS;
}

static e3_error_t spectrum_sm_start(void *sm_context)
{
  spectrum_sm_context_t *ctx = (spectrum_sm_context_t *)sm_context;
  if (!ctx || !ctx->initialized) {
    return E3_NOT_INITIALIZED;
  }

  pthread_mutex_lock(&ctx->lock);
  if (ctx->running) {
    pthread_mutex_unlock(&ctx->lock);
    return E3_SUCCESS;
  }
  ctx->running = true;
  pthread_mutex_unlock(&ctx->lock);

  if (pthread_create(&ctx->worker_thread, NULL, spectrum_sm_thread_main, ctx) != 0) {
    pthread_mutex_lock(&ctx->lock);
    ctx->running = false;
    pthread_mutex_unlock(&ctx->lock);
    return E3_SM_ERROR_THREAD_FAILED;
  }

  return E3_SUCCESS;
}

static void spectrum_sm_stop(void *sm_context)
{
  spectrum_sm_context_t *ctx = (spectrum_sm_context_t *)sm_context;
  if (!ctx) {
    return;
  }

  pthread_mutex_lock(&ctx->lock);
  int was_running = ctx->running;
  ctx->running = false;
  pthread_mutex_unlock(&ctx->lock);

  if (was_running) {
    pthread_cancel(ctx->worker_thread);
    pthread_join(ctx->worker_thread, NULL);
  }

  if (ctx->connector_started) {
    pthread_mutex_lock(&ctx->lock);
    ctx->connector_running = false;
    pthread_mutex_unlock(&ctx->lock);
    pthread_join(ctx->connector_thread, NULL);
    ctx->connector_started = false;
  }
}

static int spectrum_sm_is_running(void *sm_context)
{
  spectrum_sm_context_t *ctx = (spectrum_sm_context_t *)sm_context;
  if (!ctx) {
    return 0;
  }
  pthread_mutex_lock(&ctx->lock);
  int running = is_running_locked(ctx);
  pthread_mutex_unlock(&ctx->lock);
  return running;
}

static void spectrum_sm_destroy(void *sm_context)
{
  spectrum_sm_context_t *ctx = (spectrum_sm_context_t *)sm_context;
  if (!ctx) {
    return;
  }

  spectrum_sm_stop(sm_context);

  if (ctx->event_flags) {
    free(ctx->event_flags);
    ctx->event_flags = NULL;
  }

  if (ctx->database) {
    free_database(ctx->database);
    ctx->database = NULL;
  }

  if (ctx->t_tracer_socket >= 0) {
    close(ctx->t_tracer_socket);
    ctx->t_tracer_socket = -1;
  }

  if (e3_sm_spectrum_control) {
    if (e3_sm_spectrum_control->action_list) {
      free(e3_sm_spectrum_control->action_list);
      e3_sm_spectrum_control->action_list = NULL;
    }
    pthread_mutex_destroy(&e3_sm_spectrum_control->mutex);
    free(e3_sm_spectrum_control);
    e3_sm_spectrum_control = NULL;
  }

  if (ctx->initialized) {
    pthread_mutex_destroy(&ctx->lock);
  }

  if (spectrum_ran_function_data) {
    free(spectrum_ran_function_data);
    spectrum_ran_function_data = NULL;
    spectrum_ran_function_data_len = 0;
    spectrum_ran_function_data_ready = 0;
    spectrum_sm_desc.ran_function_data = NULL;
    spectrum_sm_desc.ran_function_data_len = 0;
  }

  memset(ctx, 0, sizeof(*ctx));
  ctx->t_tracer_socket = -1;
}

static e3_error_t spectrum_sm_process_control(e3_service_model_handle_t* sm_handle,
                                              void* sm_context,
                                              uint32_t request_message_id,
                                              uint32_t dapp_id,
                                              uint32_t ran_function_id,
                                              uint32_t control_id,
                                              const uint8_t* data,
                                              size_t data_len)
{
  (void)dapp_id;
  int response_code = 0; // Positive
  
  spectrum_sm_context_t *ctx = (spectrum_sm_context_t *)sm_context;
  if (!ctx || !sm_handle || ran_function_id != SPECTRUM_SM_RAN_FUNCTION_ID ||
      control_id != SPECTRUM_SM_RAN_FUNCTION_ID) {
    if (sm_handle) {
      response_code = 1; // Negative
      e3_service_model_emit_message_ack(sm_handle, request_message_id, response_code);
    }
    return E3_SM_ERROR_INVALID_PARAM;
  }

  pthread_mutex_lock(&ctx->lock);
  ctx->sm_handle = sm_handle;
  pthread_mutex_unlock(&ctx->lock);

  if (!data || data_len == 0 || !e3_sm_spectrum_control) {
    response_code = 1; // Negative
    e3_service_model_emit_message_ack(sm_handle, request_message_id, response_code);
    return E3_SM_ERROR_INVALID_PARAM;
  }

  spectrum_prb_control_t* control_payload =
      spectrum_decode_prb_control((uint8_t *)data, data_len);
  if (!control_payload) {
    response_code = 1; // Negative
    e3_service_model_emit_message_ack(sm_handle, request_message_id, response_code);
    return E3_SM_ERROR_INVALID_PARAM;
  }

  const size_t max_elems = MAX_BWP_SIZE;
  size_t elems = control_payload->prb_count;
  if (elems > max_elems) {
    elems = max_elems;
  }

  pthread_mutex_lock(&e3_sm_spectrum_control->mutex);

  uint16_t *alist16 = e3_sm_spectrum_control->action_list;
  memcpy(alist16, control_payload->blacklisted_prbs, elems * sizeof(uint16_t));
  e3_sm_spectrum_control->action_size = (int)elems;

  memset(e3_sm_spectrum_control->dyn_prbbl, 0, max_elems * sizeof(uint16_t));
  for (size_t j = 0; j < elems; ++j) {
    uint16_t prb = alist16[j];
    if (prb < MAX_BWP_SIZE) {
      e3_sm_spectrum_control->dyn_prbbl[prb] = 0x3FFF;
    }
  }

  memset(alist16, 0, max_elems * sizeof(uint16_t));
  if (control_payload->sampling_threshold > 0) {
    e3_sm_spectrum_control->sampling_threshold = control_payload->sampling_threshold;
  }

  e3_sm_spectrum_control->ready = 1;
  pthread_mutex_unlock(&e3_sm_spectrum_control->mutex);

  spectrum_free_decoded_control(control_payload);

  e3_service_model_emit_message_ack(sm_handle, request_message_id, response_code);
  return E3_SUCCESS;
}
