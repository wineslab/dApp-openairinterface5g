#include "spectrum_sm.h"
#include "spectrum_enc.h"
#include "spectrum_dec.h"

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
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

// T-tracer constants
#define DEFAULT_REMOTE_IP "127.0.0.1"
#define DEFAULT_REMOTE_PORT 2021

// SM default constants
#define DEFAULT_SM_SAMPLING_THRESHOLD 5

// Spectrum SM RAN function configuration
static uint32_t spectrum_ran_functions[] = {SPECTRUM_SM_RAN_FUNCTION_ID};

e3_sm_spectrum_control_t *e3_sm_spectrum_control = NULL;

// Spectrum SM instance
e3_service_model_t spectrum_sm = {
    .name = "spectrum_sm",
    .version = 1,
    .ran_function_ids = spectrum_ran_functions,
    .ran_function_count = sizeof(spectrum_ran_functions) / sizeof(spectrum_ran_functions[0]),
    .init = spectrum_sm_init,
    .destroy = spectrum_sm_destroy,
    .thread_main = spectrum_sm_thread_main,
    .process_dapp_control_action = spectrum_sm_process_dapp_control_action,
    .thread_context = NULL,
    .is_running = false,
#ifdef SPECTRUM_SM_ASN1_FORMAT
    .format =     FORMAT_ASN1
#else
    .format = FORMAT_JSON
#endif
};

// Global SM context
static spectrum_sm_context_t *spectrum_context = NULL;

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


/**
 * Activate T-tracer events for spectrum sensing
 * this function sends the activated traces to the nr-softmodem
 */
static void activate_traces(int socket, int number_of_events, int *is_on) 
{
    // Send activation command to T-tracer
    char t = 1;
    if (socket_send(socket, &t, 1) == -1 || socket_send(socket, &number_of_events, sizeof(int)) == -1 ||
        socket_send(socket, is_on, number_of_events * sizeof(int)) == -1) {
        LOG_E(E3AP, "[SPECTRUM] Failed to activate T-tracer events\n");
        return;
    }
    
    LOG_I(E3AP, "[SPECTRUM] T-tracer events activated\n");
}

/**
 * Perform T-tracer initialization steps
 * Returns true on success, false on failure
 */
static bool t_tracer_init(spectrum_sm_context_t *sm_ctx) {
    // Initialize T-tracer connection
    char *database_filename = T_MESSAGES_PATH;
    char *ip = DEFAULT_REMOTE_IP;
    int port = DEFAULT_REMOTE_PORT;
    
    LOG_D(E3AP, "[SPECTRUM] Loading T-tracer database\n");
    sm_ctx->database = parse_database(database_filename);
    if (!sm_ctx->database) {
        LOG_E(E3AP, "[SPECTRUM] Failed to parse T-tracer database\n");
        return false;
    }
    LOG_D(E3AP, "[SPECTRUM] T-tracer database loaded successfully\n");
    load_config_file(database_filename);
    LOG_D(E3AP, "[SPECTRUM] Configuration file loaded\n");
    
    // Initialize an array of int for all the events defined in the database */
    sm_ctx->number_of_events = number_of_ids(sm_ctx->database);
    sm_ctx->event_flags = calloc(sm_ctx->number_of_events, sizeof(int));
    if (!sm_ctx->event_flags) {
        LOG_E(E3AP, "[SPECTRUM] Failed to allocate event flags array\n");
        return false;
    }
    LOG_D(E3AP, "[SPECTRUM] Event flags array allocated (%d events)\n", sm_ctx->number_of_events);
    
    LOG_D(E3AP, "[SPECTRUM] Connecting to T-tracer at %s:%d\n", ip, port);
    sm_ctx->t_tracer_socket = connect_to(ip, port);
    if (sm_ctx->t_tracer_socket < 0) {
        LOG_E(E3AP, "[SPECTRUM] Failed to connect to T-tracer\n");
        return false;
    }
    LOG_D(E3AP, "[SPECTRUM] T-tracer connection established (socket: %d)\n", sm_ctx->t_tracer_socket);
    
    // Activate T-tracer events
    activate_traces(sm_ctx->t_tracer_socket, sm_ctx->number_of_events, sm_ctx->event_flags);
    
    return true;
}



/**
 * Initialize the spectrum SM
 */
int spectrum_sm_init(e3_service_model_t *sm) {
    LOG_D(E3AP, "[SPECTRUM] Initializing spectrum SM\n");
    
    if (spectrum_context && spectrum_context->initialized) {
        LOG_W(E3AP, "[SPECTRUM] SM already initialized\n");
        return SM_SUCCESS;
    }
    
    spectrum_context = malloc(sizeof(spectrum_sm_context_t));
    if (!spectrum_context) {
        LOG_E(E3AP, "[SPECTRUM] Failed to allocate spectrum SM context\n");
        return SM_ERROR_MEMORY;
    }
    
    memset(spectrum_context, 0, sizeof(spectrum_sm_context_t));
    spectrum_context->t_tracer_socket = -1;
    
    // Initialize T-tracer synchronously
    LOG_D(E3AP, "[SPECTRUM] Starting T-tracer initialization\n");
    
    if (!t_tracer_init(spectrum_context)) {
        LOG_E(E3AP, "[SPECTRUM] Failed to initialize T-tracer\n");
        free(spectrum_context);
        spectrum_context = NULL;
        return SM_ERROR_MEMORY;
    }
    
    // Start control action manager
    e3_sm_spectrum_control = (e3_sm_spectrum_control_t *)malloc(sizeof(e3_sm_spectrum_control_t));
    pthread_mutex_init(&e3_sm_spectrum_control->mutex, NULL);
    e3_sm_spectrum_control->ready = 0;
    e3_sm_spectrum_control->action_list = (char *)malloc(MAX_BWP_SIZE * sizeof(uint16_t));

    // Each sensing is done once every 10ms * sampling_threshold
      // this stays here since an xApp or a dApp can potentially change the threshold value
    e3_sm_spectrum_control->sampling_threshold = DEFAULT_SM_SAMPLING_THRESHOLD; // one delivery each sampling_threshold samples captures
    e3_sm_spectrum_control->sampling_counter = 0;


    spectrum_context->initialized = true;
    LOG_D(E3AP, "[SPECTRUM] Spectrum SM initialized successfully\n");
    return SM_SUCCESS;
}

/**
 * Destroy the spectrum SM
 */
int spectrum_sm_destroy(e3_service_model_t *sm) {
    LOG_D(E3AP, "[SPECTRUM] Destroying spectrum SM\n");
    
    if (!spectrum_context) {
        LOG_D(E3AP, "[SPECTRUM] SM context already destroyed\n");
        return SM_SUCCESS;
    }
    
    // Close T-tracer socket
    if (spectrum_context->t_tracer_socket >= 0) {
        close(spectrum_context->t_tracer_socket);
        LOG_D(E3AP, "[SPECTRUM] T-tracer socket closed\n");
    }
    
    // Free event flags
    if (spectrum_context->event_flags) {
        free(spectrum_context->event_flags);
        LOG_D(E3AP, "[SPECTRUM] Event flags freed\n");
    }

    free(e3_sm_spectrum_control->action_list);
    pthread_mutex_destroy(&e3_sm_spectrum_control->mutex);

    free(spectrum_context);
    spectrum_context = NULL;
    
    LOG_D(E3AP, "[SPECTRUM] Spectrum SM destroyed successfully\n");
    return SM_SUCCESS;
}

/**
 * Get current timestamp in seconds
 * Commented to remove the warning
 * This is currently useless, might be deleted in the next iteration
 * Uncomment to use
 */
// static uint32_t get_current_timestamp_s(void) {
//     struct timeval tv;
//     gettimeofday(&tv, NULL);
//     return (uint32_t)tv.tv_sec;
// }

/**
 * Main thread function for spectrum SM
 */
void* spectrum_sm_thread_main(void *context) {
    sm_thread_context_t *thread_ctx = (sm_thread_context_t *)context;
    spectrum_sm_context_t *sm_ctx = spectrum_context;

    /* a buffer needed to receive events from the nr-softmodem */
    OBUF ebuf = {osize : 0, omaxsize : 0, obuf : NULL};
    
    if (!sm_ctx || !sm_ctx->initialized) {
        LOG_E(E3AP, "[SPECTRUM] SM context not allocated or  SM not initialized, exiting main thread\n");
        return NULL;
    }
    
    LOG_D(E3AP, "[SPECTRUM] Main thread started\n");

    // Set up signal handling for socket errors
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        LOG_E(E3AP, "[SPECTRUM] Failed to set SIGPIPE handler\n");
        return NULL;
    }
    LOG_D(E3AP, "[SPECTRUM] SIGPIPE handler configured\n");

    /* Activate the spectrum sensing trace GNB_PHY_UL_FREQ_SENSING_SYMBOL trace in the database */
    on_off(spectrum_context->database, "GNB_PHY_UL_FREQ_SENSING_SYMBOL", 
           spectrum_context->event_flags, 1);
    /* Activate the spectrum sensing trace GNB_PHY_UL_FREQ_SENSING_SYMBOL in the nr-softmodem */
    activate_traces(spectrum_context->t_tracer_socket, spectrum_context->number_of_events, spectrum_context->event_flags);

    /* Get the format of the GNB_PHY_UL_FREQ_SENSING_SYMBOL trace */
    sm_ctx->iq_data_event_id = event_id_from_name(sm_ctx->database, 
                                                  "GNB_PHY_UL_FREQ_SENSING_SYMBOL");    
    database_event_format f = get_format(sm_ctx->database, sm_ctx->iq_data_event_id);
    
    /* Get the elements of the GNB_PHY_UL_FREQ_SENSING_SYMBOL trace 
    the value is an index in the event, see below
    */    
    int data_index = -1;
    for (int i = 0; i < f.count; i++) {
        GET_DATA_FROM_TRACER("rxdata", "buffer", data_index);
    }
    
    if (data_index == -1) {
        LOG_E(E3AP, "[SPECTRUM] Failed to find rxdata field in event format\n");
        return NULL;
    }

    LOG_D(E3AP, "[SPECTRUM] Found rxdata field at index %d\n", data_index);    
    
    LOG_I(E3AP, "[SPECTRUM] Starting event processing loop\n");
    
    // Main event processing loop
    // read events
    while (1) {
        // Check if we should stop
        pthread_mutex_lock(&thread_ctx->stop_mutex);
        bool should_stop = thread_ctx->should_stop;
        pthread_mutex_unlock(&thread_ctx->stop_mutex);
        
        if (should_stop) {
            LOG_I(E3AP, "[SPECTRUM] Main thread stopping\n");
            break;
        }
        
        // Get event from T-tracer
        event e = get_event(sm_ctx->t_tracer_socket, &ebuf, sm_ctx->database);
        if (e.type == -1) {
            LOG_W(E3AP, "[SPECTRUM] T-tracer connection lost\n");
            break;
        }
        
        if (e.type == sm_ctx->iq_data_event_id) {    
            if (e.e[data_index].bsize > 0) {
                int32_t *samples = (int32_t *)e.e[data_index].b;
                for (size_t i = 0; i < 5 && i < (e.e[data_index].bsize / sizeof(int32_t)); i++) {
                    LOG_D(E3AP, "[SPECTRUM] Sample[%zu] = %d\n", i, samples[i]);
                }
            }
            
            uint8_t *encoded_data = NULL;
            size_t encoded_size = 0;
            uint32_t timestamp = 0;
            // get_current_timestamp_s(); is working but it is disabled in this implementation as not useful

            int encode_result = spectrum_encode_indication(
                e.e[data_index].b,       // Raw IQ data
                e.e[data_index].bsize,   // Data size
                timestamp,               // Timestamp
                &encoded_data,           // Output buffer
                &encoded_size            // Output size
            );
            
            LOG_D(E3AP, "[SPECTRUM] Encoding result: %d, encoded_data=%p, encoded_size=%zu\n", 
                  encode_result, encoded_data, encoded_size);
            
            // Detailed error reporting
            if (encode_result != SM_SUCCESS) {
                switch (encode_result) {
                    case SM_ERROR_INVALID_PARAM:
                        LOG_E(E3AP, "[SPECTRUM] Encoding failed: Invalid parameters\n");
                        break;
                    case SM_ERROR_MEMORY:
                        LOG_E(E3AP, "[SPECTRUM] Encoding failed: Memory allocation error\n");
                        break;
                    default:
                        LOG_E(E3AP, "[SPECTRUM] Encoding failed: Unknown error code %d\n", encode_result);
                        break;
                }
            }
            
            if (encode_result == SM_SUCCESS && encoded_data) {
                // Store in shared data structure
                int set_result = sm_indication_data_set(thread_ctx->output_data, 
                                                      encoded_data, encoded_size, timestamp);
                if (set_result == SM_SUCCESS) {
                    LOG_D(E3AP, "[SPECTRUM] Indication data ready (%zu bytes)\n", encoded_size);
                } else {
                    LOG_E(E3AP, "[SPECTRUM] Failed to set indication data: %d\n", set_result);
                }
                
                free(encoded_data);
            } else {
                LOG_E(E3AP, "[SPECTRUM] Failed to encode spectrum indication: %d\n", encode_result);
            }
        }
    }
    
    // Cleanup
    if (ebuf.obuf) {
        free(ebuf.obuf);
        LOG_D(E3AP, "[SPECTRUM] Event buffer cleaned up\n");
    }
    
    LOG_D(E3AP, "[SPECTRUM] Main thread cleanup completed\n");
    
    LOG_I(E3AP, "[SPECTRUM] Main thread finished\n");
    return NULL;
}

/**
 * Process control messages for spectrum SM
 */
int spectrum_sm_process_dapp_control_action(uint32_t ran_function_id, uint8_t *encoded_data, size_t size) {
    if (ran_function_id != SPECTRUM_SM_RAN_FUNCTION_ID) {
        LOG_E(E3AP, "[SPECTRUM] Invalid RAN function ID %u\n", ran_function_id);
        return SM_ERROR_INVALID_PARAM;
    }
    
    if (!encoded_data || size == 0) {
        LOG_E(E3AP, "[SPECTRUM] Invalid control data parameters\n");
        return SM_ERROR_INVALID_PARAM;
    }
    
    LOG_I(E3AP, "[SPECTRUM] Processing control message (%zu bytes)\n", size);
    spectrum_prb_control_t* control_payload = spectrum_decode_prb_control(encoded_data, size);
    
    if (!control_payload) {
        LOG_E(E3AP, "[SPECTRUM] Failed to decode control message\n");
        return SM_ERROR_INVALID_PARAM;
    }

    
    LOG_D(E3AP, "[SPECTRUM] PRB count: %d\n", control_payload->prb_count);
    
    int prb_count_bits = control_payload->prb_count * sizeof(uint16_t);
    int write_size = (prb_count_bits < MAX_BWP_SIZE) ? prb_count_bits : MAX_BWP_SIZE;

    pthread_mutex_lock(&e3_sm_spectrum_control->mutex);
    memcpy(e3_sm_spectrum_control->action_list, control_payload->blacklisted_prbs, write_size);
    e3_sm_spectrum_control->action_size = control_payload->prb_count;
    for (size_t i = 0; i < control_payload->prb_count; i++) {
      LOG_D(E3AP, "e3_sm_spectrum_control[%zu] = %d\n", i, ((uint16_t *)e3_sm_spectrum_control->action_list)[i]);
    }
    memset(e3_sm_spectrum_control->dyn_prbbl, 0, MAX_BWP_SIZE * sizeof(uint16_t));
    for (int j = 0; j < e3_sm_spectrum_control->action_size && j < MAX_BWP_SIZE; j++) {
      e3_sm_spectrum_control
          ->dyn_prbbl[(e3_sm_spectrum_control->action_list[2 * j + 1] << 8 & 0xFF) | (e3_sm_spectrum_control->action_list[2 * j] & 0xFF)] =
          0x3FFF;
    }
    memset(e3_sm_spectrum_control->action_list, 0, e3_sm_spectrum_control->action_size * sizeof(uint16_t));

    // We don't care about the validity period atm, but in case the parsing
    // should be implemented here

    if(control_payload->sampling_threshold){
        LOG_I(E3AP, "[SPECTRUM] Change sampling threshold from %d to %d\n", e3_sm_spectrum_control->sampling_threshold, control_payload->sampling_threshold);
        e3_sm_spectrum_control->sampling_threshold = control_payload->sampling_threshold;
    }

    e3_sm_spectrum_control->ready = 1; // Set ready flag to 1 to indicate data is available
    pthread_mutex_unlock(&e3_sm_spectrum_control->mutex);
    
    LOG_D(E3AP, "[SPECTRUM] Control action applied successfully\n");
    spectrum_free_decoded_control(control_payload);

    return SM_SUCCESS;
}