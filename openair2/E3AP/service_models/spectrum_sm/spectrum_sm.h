#ifndef SPECTRUM_SM_H
#define SPECTRUM_SM_H

#include "../sm_interface.h"
#include "common/utils/nr/nr_common.h"

// Forward declarations for spectrum SM functions
int spectrum_sm_init(e3_service_model_t *sm);
int spectrum_sm_destroy(e3_service_model_t *sm);
void* spectrum_sm_thread_main(void *context);
int spectrum_sm_process_dapp_control_action(uint32_t ran_function_id, uint8_t *encoded_data, size_t size);

// Spectrum SM RAN function IDs
#define SPECTRUM_SM_RAN_FUNCTION_ID 1

/**
 * @brief E3 agent control variables
 * This struct is responsible of handling all the shared variables to enable intercommunication between the E3 agent and the rest of
 * the codebase
 *
 */
typedef struct e3_sm_spectrum_control {
  char* action_list;
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
    
    // State
    bool initialized;
} spectrum_sm_context_t;

// Export the spectrum SM instance
extern e3_service_model_t spectrum_sm;

#endif // SPECTRUM_SM_H