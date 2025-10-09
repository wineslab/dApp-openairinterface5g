#ifndef SM_INTERFACE_H
#define SM_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include "../e3ap_types.h"

/**
 * Generic Service Model Interface for E3 Agent
 * 
 * This interface defines the contract that all Service Models must implement
 * to be managed by the SM registry and subscription manager.
 */

// Forward declarations
typedef struct e3_service_model_s e3_service_model_t;
typedef struct sm_indication_data_s sm_indication_data_t;

/**
 * SM indication data structure - for performance-oriented data passing
 * between SM threads and main publisher thread
 */
typedef struct sm_indication_data_s {
    uint32_t ran_function_id;
    uint8_t *encoded_data;
    size_t encoded_size;
    uint64_t timestamp;
    bool ready;                    // Flag for shared variable approach
    pthread_mutex_t mutex;         // Mutex for thread-safe access
    struct sm_indication_data_s *next; // For queue-based approach
} sm_indication_data_t;

/**
 * SM thread context - contains everything an SM thread needs
 */
typedef struct {
    e3_service_model_t *sm;
    bool should_stop;
    pthread_t thread_id;
    pthread_mutex_t stop_mutex;
    sm_indication_data_t *output_data; // Shared data with publisher
} sm_thread_context_t;

/**
 * Service Model Interface
 * 
 * Each SM must implement these function pointers to integrate with the E3 agent
 */
typedef struct e3_service_model_s {
    // SM metadata
    char *name;
    uint32_t version;
    uint32_t *ran_function_ids;    // Array of RAN function IDs this SM handles
    uint32_t ran_function_count;   // Number of RAN functions

    // SM lifecycle functions
    int (*init)(e3_service_model_t *sm);
    int (*destroy)(e3_service_model_t *sm);
    
    // SM thread functions
    void* (*thread_main)(void *context); // Main SM thread function
    int (*process_dapp_control_action)(uint32_t ran_function_id, uint8_t *encoded_data, size_t size);
    
    // SM state
    sm_thread_context_t *thread_context;
    bool is_running;
    
    // Format support (compile-time determined)
    E3EncodingFormat format;
} e3_service_model_t;

/**
 * SM Registry Return Codes
 */
#define SM_SUCCESS 0
#define SM_ERROR_INVALID_PARAM -1
#define SM_ERROR_NOT_FOUND -2
#define SM_ERROR_ALREADY_EXISTS -3
#define SM_ERROR_THREAD_FAILED -4
#define SM_ERROR_MEMORY -5

/**
 * SM Registry Functions - implemented in sm_registry.c
 */
int sm_registry_init(void);
int sm_registry_destroy(void);
int sm_registry_register(e3_service_model_t *sm);
e3_service_model_t* sm_registry_get_by_ran_function(uint32_t ran_function_id);
int sm_registry_start_sm(uint32_t ran_function_id);
int sm_registry_stop_sm(uint32_t ran_function_id);
bool sm_registry_is_sm_running(uint32_t ran_function_id);
int sm_registry_get_available_ran_functions(uint32_t **ran_function_ids, uint32_t *count);

/**
 * SM Indication Data Management Functions
 */
sm_indication_data_t* sm_indication_data_create(uint32_t ran_function_id);
void sm_indication_data_destroy(sm_indication_data_t *data);
int sm_indication_data_set(sm_indication_data_t *data, uint8_t *encoded_data, 
                          size_t encoded_size, uint64_t timestamp);
int sm_indication_data_get(sm_indication_data_t *data, uint8_t **encoded_data, 
                          size_t *encoded_size, uint64_t *timestamp);

#endif // SM_INTERFACE_H