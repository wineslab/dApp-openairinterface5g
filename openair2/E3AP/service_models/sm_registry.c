#include "sm_interface.h"
#include "common/utils/LOG/log.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/**
 * SM Registry Implementation
 * 
 * Manages the lifecycle of Service Models and their integration with
 * the E3 subscription manager.
 */

#define MAX_SERVICE_MODELS 16

// Global registry state
static struct {
    e3_service_model_t *registered_sms[MAX_SERVICE_MODELS];
    uint32_t sm_count;
    bool initialized;
    pthread_mutex_t registry_mutex;
} sm_registry;

// Forward declarations for built-in SMs
extern e3_service_model_t spectrum_sm;

/**
 * Initialize the SM registry
 */
int sm_registry_init(void) {
    if (sm_registry.initialized) {
        LOG_W(E3AP, "SM registry already initialized\n");
        return SM_SUCCESS;
    }
    
    memset(&sm_registry, 0, sizeof(sm_registry));
    pthread_mutex_init(&sm_registry.registry_mutex, NULL);
    sm_registry.initialized = true;

    LOG_I(E3AP, "SM registry initialized\n");
    
    // Auto-register built-in SMs
    int ret = sm_registry_register(&spectrum_sm);
    if (ret != SM_SUCCESS) {
        LOG_E(E3AP, "Failed to register spectrum SM: %d\n", ret);
        return ret;
    }
    
    LOG_I(E3AP, "Built-in SMs registered successfully\n");
    return SM_SUCCESS;
}

/**
 * Destroy the SM registry and cleanup all SMs
 */
int sm_registry_destroy(void) {
    LOG_D(E3AP, "[SM_REGISTRY] Destroying SM registry\n");
    if (!sm_registry.initialized) {
        LOG_D(E3AP, "[SM_REGISTRY] Registry not initialized, nothing to destroy\n");
        return SM_SUCCESS;
    }
    
    pthread_mutex_lock(&sm_registry.registry_mutex);
    LOG_D(E3AP, "[SM_REGISTRY] Stopping and destroying %u registered SMs\n", sm_registry.sm_count);
    
    // Stop and destroy all SMs
    for (uint32_t i = 0; i < sm_registry.sm_count; i++) {
        e3_service_model_t *sm = sm_registry.registered_sms[i];
        if (sm && sm->is_running) {
            // Stop SM threads for all its RAN functions
            for (uint32_t j = 0; j < sm->ran_function_count; j++) {
                sm_registry_stop_sm(sm->ran_function_ids[j]);
            }
        }
        if (sm && sm->destroy) {
            sm->destroy(sm);
        }
    }
    
    sm_registry.sm_count = 0;
    sm_registry.initialized = false;
    
    pthread_mutex_unlock(&sm_registry.registry_mutex);
    pthread_mutex_destroy(&sm_registry.registry_mutex);
    
    LOG_I(E3AP, "SM registry destroyed\n");
    return SM_SUCCESS;
}

/**
 * Register a new Service Model
 */
int sm_registry_register(e3_service_model_t *sm) {
    if (!sm_registry.initialized) {
        LOG_E(E3AP, "SM registry not initialized\n");
        return SM_ERROR_INVALID_PARAM;
    }
    
    if (!sm || !sm->name || !sm->ran_function_ids || sm->ran_function_count == 0) {
        LOG_E(E3AP, "Invalid SM parameters\n");
        return SM_ERROR_INVALID_PARAM;
    }
    
    LOG_D(E3AP, "[SM_REGISTRY] Registering SM '%s' with %u RAN functions\n", 
          sm->name, sm->ran_function_count);
    
    pthread_mutex_lock(&sm_registry.registry_mutex);
    
    if (sm_registry.sm_count >= MAX_SERVICE_MODELS) {
        pthread_mutex_unlock(&sm_registry.registry_mutex);
        LOG_E(E3AP, "SM registry full\n");
        return SM_ERROR_MEMORY;
    }
    
    // Check for RAN function ID conflicts
    LOG_D(E3AP, "[SM_REGISTRY] Checking RAN function ID conflicts against %u existing SMs\n", 
          sm_registry.sm_count);
    for (uint32_t i = 0; i < sm_registry.sm_count; i++) {
        e3_service_model_t *existing_sm = sm_registry.registered_sms[i];
        for (uint32_t j = 0; j < existing_sm->ran_function_count; j++) {
            for (uint32_t k = 0; k < sm->ran_function_count; k++) {
                if (existing_sm->ran_function_ids[j] == sm->ran_function_ids[k]) {
                    pthread_mutex_unlock(&sm_registry.registry_mutex);
                    LOG_E(E3AP, "RAN function ID %u already registered by SM %s\n", 
                          sm->ran_function_ids[k], existing_sm->name);
                    return SM_ERROR_ALREADY_EXISTS;
                }
            }
        }
    }
    
    // Initialize the SM
    LOG_D(E3AP, "[SM_REGISTRY] Initializing SM '%s'\n", sm->name);
    if (sm->init && sm->init(sm) != SM_SUCCESS) {
        pthread_mutex_unlock(&sm_registry.registry_mutex);
        LOG_E(E3AP, "Failed to initialize SM %s\n", sm->name);
        return SM_ERROR_INVALID_PARAM;
    }
    LOG_D(E3AP, "[SM_REGISTRY] SM '%s' initialized successfully\n", sm->name);
    
    // Register the SM
    sm_registry.registered_sms[sm_registry.sm_count] = sm;
    sm_registry.sm_count++;
    
    pthread_mutex_unlock(&sm_registry.registry_mutex);
    
    LOG_I(E3AP, "Registered SM %s with %u RAN functions\n", sm->name, sm->ran_function_count);
    for (uint32_t i = 0; i < sm->ran_function_count; i++) {
        LOG_I(E3AP, "  RAN function ID: %u\n", sm->ran_function_ids[i]);
    }
    
    return SM_SUCCESS;
}

/**
 * Get SM by RAN function ID
 */
e3_service_model_t* sm_registry_get_by_ran_function(uint32_t ran_function_id) {
    if (!sm_registry.initialized) {
        return NULL;
    }
    
    pthread_mutex_lock(&sm_registry.registry_mutex);
    
    for (uint32_t i = 0; i < sm_registry.sm_count; i++) {
        e3_service_model_t *sm = sm_registry.registered_sms[i];
        for (uint32_t j = 0; j < sm->ran_function_count; j++) {
            if (sm->ran_function_ids[j] == ran_function_id) {
                pthread_mutex_unlock(&sm_registry.registry_mutex);
                return sm;
            }
        }
    }
    
    pthread_mutex_unlock(&sm_registry.registry_mutex);
    return NULL;
}

/**
 * Start SM thread for a specific RAN function
 */
int sm_registry_start_sm(uint32_t ran_function_id) {
    LOG_D(E3AP, "[SM_REGISTRY] Starting SM thread for RAN function %u\n", ran_function_id);
    e3_service_model_t *sm = sm_registry_get_by_ran_function(ran_function_id);
    if (!sm) {
        LOG_E(E3AP, "No SM found for RAN function %u\n", ran_function_id);
        return SM_ERROR_NOT_FOUND;
    }
    
    if (sm->is_running) {
        LOG_D(E3AP, "SM %s already running\n", sm->name);
        return SM_SUCCESS;
    }
    
    if (!sm->thread_main) {
        LOG_E(E3AP, "SM %s has no thread main function\n", sm->name);
        return SM_ERROR_INVALID_PARAM;
    }
    
    // Create thread context
    LOG_D(E3AP, "[SM_REGISTRY] Creating thread context for SM '%s'\n", sm->name);
    sm->thread_context = malloc(sizeof(sm_thread_context_t));
    if (!sm->thread_context) {
        LOG_E(E3AP, "Failed to allocate thread context for SM %s\n", sm->name);
        return SM_ERROR_MEMORY;
    }
    LOG_D(E3AP, "[SM_REGISTRY] Thread context allocated for SM '%s'\n", sm->name);
    
    sm->thread_context->sm = sm;
    sm->thread_context->should_stop = false;
    pthread_mutex_init(&sm->thread_context->stop_mutex, NULL);
    
    // Create shared indication data
    LOG_D(E3AP, "[SM_REGISTRY] Creating indication data for RAN function %u\n", ran_function_id);
    sm->thread_context->output_data = sm_indication_data_create(ran_function_id);
    if (!sm->thread_context->output_data) {
        LOG_E(E3AP, "[SM_REGISTRY] Failed to create indication data\n");
        free(sm->thread_context);
        sm->thread_context = NULL;
        return SM_ERROR_MEMORY;
    }
    LOG_D(E3AP, "[SM_REGISTRY] Indication data created successfully\n");
    
    // Start the SM thread
    LOG_D(E3AP, "[SM_REGISTRY] Creating pthread for SM '%s'\n", sm->name);
    int ret = pthread_create(&sm->thread_context->thread_id, NULL, 
                            sm->thread_main, sm->thread_context);
    if (ret != 0) {
        LOG_E(E3AP, "Failed to create thread for SM %s: %s\n", sm->name, strerror(ret));
        sm_indication_data_destroy(sm->thread_context->output_data);
        free(sm->thread_context);
        sm->thread_context = NULL;
        return SM_ERROR_THREAD_FAILED;
    }
    LOG_D(E3AP, "[SM_REGISTRY] Thread created successfully for SM '%s'\n", sm->name);
    
    sm->is_running = true;
    LOG_I(E3AP, "Started SM %s thread for RAN function %u\n", sm->name, ran_function_id);
    
    return SM_SUCCESS;
}

/**
 * Stop SM thread for a specific RAN function
 */
int sm_registry_stop_sm(uint32_t ran_function_id) {
    LOG_D(E3AP, "[SM_REGISTRY] Stopping SM thread for RAN function %u\n", ran_function_id);
    e3_service_model_t *sm = sm_registry_get_by_ran_function(ran_function_id);
    if (!sm) {
        LOG_E(E3AP, "No SM found for RAN function %u\n", ran_function_id);
        return SM_ERROR_NOT_FOUND;
    }
    
    if (!sm->is_running || !sm->thread_context) {
        LOG_D(E3AP, "SM %s not running\n", sm->name);
        return SM_SUCCESS;
    }
    
    // Signal thread to stop
    pthread_mutex_lock(&sm->thread_context->stop_mutex);
    sm->thread_context->should_stop = true;
    pthread_mutex_unlock(&sm->thread_context->stop_mutex);
    
    // Wait for thread to finish
    LOG_D(E3AP, "[SM_REGISTRY] Waiting for SM '%s' thread to finish\n", sm->name);
    int ret = pthread_join(sm->thread_context->thread_id, NULL);
    if (ret != 0) {
        LOG_E(E3AP, "Failed to join thread for SM %s: %s\n", sm->name, strerror(ret));
    } else {
        LOG_D(E3AP, "[SM_REGISTRY] SM '%s' thread joined successfully\n", sm->name);
    }
    
    // Cleanup thread context
    sm_indication_data_destroy(sm->thread_context->output_data);
    pthread_mutex_destroy(&sm->thread_context->stop_mutex);
    free(sm->thread_context);
    sm->thread_context = NULL;
    sm->is_running = false;
    
    LOG_I(E3AP, "Stopped SM %s thread for RAN function %u\n", sm->name, ran_function_id);
    
    return SM_SUCCESS;
}

/**
 * Check if SM is running for a specific RAN function
 */
bool sm_registry_is_sm_running(uint32_t ran_function_id) {
    e3_service_model_t *sm = sm_registry_get_by_ran_function(ran_function_id);
    return sm ? sm->is_running : false;
}

/**
 * SM Indication Data Management Functions
 */

sm_indication_data_t* sm_indication_data_create(uint32_t ran_function_id) {
    sm_indication_data_t *data = malloc(sizeof(sm_indication_data_t));
    if (!data) {
        LOG_E(E3AP, "[SM_REGISTRY] Failed to allocate indication data\n");
        return NULL;
    }
    
    memset(data, 0, sizeof(sm_indication_data_t));
    data->ran_function_id = ran_function_id;
    pthread_mutex_init(&data->mutex, NULL);
    
    return data;
}

void sm_indication_data_destroy(sm_indication_data_t *data) {
    if (!data) {
        LOG_W(E3AP, "[SM_REGISTRY] Indication data destroy called with NULL pointer\n");
        return;
    }
    
    pthread_mutex_destroy(&data->mutex);
    if (data->encoded_data) {
        free(data->encoded_data);
    }
    free(data);
}

int sm_indication_data_set(sm_indication_data_t *data, uint8_t *encoded_data, 
                          size_t encoded_size, uint64_t timestamp) {
    if (!data || !encoded_data || encoded_size == 0) {
        return SM_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&data->mutex);
    
    // Free previous data if exists
    if (data->encoded_data) {
        free(data->encoded_data);
    }
    
    // Copy new data
    data->encoded_data = malloc(encoded_size);
    if (!data->encoded_data) {
        pthread_mutex_unlock(&data->mutex);
        return SM_ERROR_MEMORY;
    }
    
    memcpy(data->encoded_data, encoded_data, encoded_size);
    data->encoded_size = encoded_size;
    data->timestamp = timestamp;
    data->ready = true;
    
    pthread_mutex_unlock(&data->mutex);
    
    return SM_SUCCESS;
}

int sm_indication_data_get(sm_indication_data_t *data, uint8_t **encoded_data, 
                          size_t *encoded_size, uint64_t *timestamp) {
    if (!data || !encoded_data || !encoded_size || !timestamp) {
        return SM_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&data->mutex);
    
    if (!data->ready || !data->encoded_data) {
        pthread_mutex_unlock(&data->mutex);
        return SM_ERROR_NOT_FOUND;
    }
    
    // Allocate and copy data
    *encoded_data = malloc(data->encoded_size);
    if (!*encoded_data) {
        pthread_mutex_unlock(&data->mutex);
        return SM_ERROR_MEMORY;
    }
    
    memcpy(*encoded_data, data->encoded_data, data->encoded_size);
    *encoded_size = data->encoded_size;
    *timestamp = data->timestamp;
    
    // Mark as consumed
    data->ready = false;
    
    pthread_mutex_unlock(&data->mutex);

    return SM_SUCCESS;
}

/**
 * Get all available RAN function IDs from registered SMs
 */
int sm_registry_get_available_ran_functions(uint32_t **ran_function_ids, uint32_t *count) {
    
    if (!sm_registry.initialized) {
        return SM_ERROR_INVALID_PARAM;
    }
    
    if (!ran_function_ids || !count) {
        return SM_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&sm_registry.registry_mutex);
    
    // Count total RAN functions across all SMs
    uint32_t total_count = 0;
    for (uint32_t i = 0; i < sm_registry.sm_count; i++) {
        if (sm_registry.registered_sms[i]) {
            total_count += sm_registry.registered_sms[i]->ran_function_count;
        }
    }
    
    if (total_count == 0) {
        LOG_W(E3AP, "[SM_REGISTRY] No RAN functions available\n");
        *ran_function_ids = NULL;
        *count = 0;
        pthread_mutex_unlock(&sm_registry.registry_mutex);
        return SM_SUCCESS;
    }
    
    // Allocate array for RAN function IDs
    uint32_t *ids = malloc(total_count * sizeof(uint32_t));
    if (!ids) {
        LOG_E(E3AP, "[SM_REGISTRY] Failed to allocate RAN function IDs array\n");
        pthread_mutex_unlock(&sm_registry.registry_mutex);
        return SM_ERROR_MEMORY;
    }
    
    // Collect all RAN function IDs
    uint32_t idx = 0;
    for (uint32_t i = 0; i < sm_registry.sm_count; i++) {
        e3_service_model_t *sm = sm_registry.registered_sms[i];
        if (sm && sm->ran_function_ids) {
            for (uint32_t j = 0; j < sm->ran_function_count; j++) {
                ids[idx++] = sm->ran_function_ids[j];
                LOG_D(E3AP, "[SM_REGISTRY] Found RAN function ID: %u (from SM: %s)\n", 
                      sm->ran_function_ids[j], sm->name ? sm->name : "unknown");
            }
        }
    }
    
    pthread_mutex_unlock(&sm_registry.registry_mutex);
    
    *ran_function_ids = ids;
    *count = total_count;

    return SM_SUCCESS;
}