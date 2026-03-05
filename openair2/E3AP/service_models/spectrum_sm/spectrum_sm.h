#ifndef SPECTRUM_SM_H
#define SPECTRUM_SM_H

#include <libe3/c_api.h>

#include "common/utils/nr/nr_common.h"
#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>

e3_c_service_model_desc_t* create_spectrum_sm_model(void);
void spectrum_sm_set_handle(e3_service_model_handle_t *sm_handle);

/**
 * @brief E3 agent control variables
 * This struct is responsible of handling all the shared variables to enable intercommunication between the E3 agent and the rest of
 * the codebase
 *
 */
typedef struct e3_sm_spectrum_control {
  uint16_t* action_list;
  int action_size;
  uint16_t dyn_prbbl[MAX_BWP_SIZE];
  int ready;
  uint32_t sampling_threshold;
  uint32_t sampling_counter;
  pthread_mutex_t mutex;
} e3_sm_spectrum_control_t;

extern e3_sm_spectrum_control_t* e3_sm_spectrum_control;

// Spectrum SM specific structures
typedef struct {
    int t_tracer_socket;
    void *database;
    int *event_flags;
    int number_of_events;
    int iq_data_event_id;

    pthread_t connector_thread;
    pthread_t worker_thread;
    pthread_mutex_t lock;
    bool connector_started;
    bool connector_running;
    bool tracer_ready;
    bool running;
    bool initialized;
    e3_service_model_handle_t *sm_handle;
} spectrum_sm_context_t;

#endif // SPECTRUM_SM_H
