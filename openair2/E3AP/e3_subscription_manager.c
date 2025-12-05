#include "e3_subscription_manager.h"
#include "service_models/sm_interface.h"

/**
 * @file e3_subscription_manager.c
 * @brief Implementation of E3 Subscription Manager
 */

/* Private helper functions */

/**
 * Resize dApp list to accommodate more entries
 */
static int resize_dapp_list(e3_dapp_list_t *list, size_t new_capacity) {
    if (new_capacity <= list->capacity) {
        return E3_SM_SUCCESS;
    }
    
    e3_dapp_entry_t *new_entries = realloc(list->entries, 
                                          new_capacity * sizeof(e3_dapp_entry_t));
    if (!new_entries) {
        LOG_E(E3AP, "Failed to resize dApp list to capacity %zu\n", new_capacity);
        return E3_SM_ERROR_MEMORY_ALLOCATION;
    }
    
    list->entries = new_entries;
    list->capacity = new_capacity;
    return E3_SM_SUCCESS;
}

/**
 * Resize subscription list to accommodate more entries
 */
static int resize_subscription_list(e3_subscription_list_t *list, size_t new_capacity) {
    if (new_capacity <= list->capacity) {
        return E3_SM_SUCCESS;
    }
    
    e3_subscription_entry_t *new_entries = realloc(list->entries, 
                                                   new_capacity * sizeof(e3_subscription_entry_t));
    if (!new_entries) {
        LOG_E(E3AP, "Failed to resize subscription list to capacity %zu\n", new_capacity);
        return E3_SM_ERROR_MEMORY_ALLOCATION;
    }
    
    list->entries = new_entries;
    list->capacity = new_capacity;
    return E3_SM_SUCCESS;
}

/**
 * Find dApp entry by ID (assumes manager is locked)
 */
static e3_dapp_entry_t* find_dapp_entry_unlocked(e3_subscription_manager_t *manager, 
                                                 uint32_t dapp_id) {
    for (size_t i = 0; i < manager->registered_dapps.count; i++) {
        if (manager->registered_dapps.entries[i].dapp_identifier == dapp_id) {
            return &manager->registered_dapps.entries[i];
        }
    }
    return NULL;
}

/* Public API implementation */

e3_subscription_manager_t* e3_subscription_manager_create(void) {
    e3_subscription_manager_t *manager = calloc(1, sizeof(e3_subscription_manager_t));
    if (!manager) {
        LOG_E(E3AP, "Failed to allocate memory for subscription manager\n");
        return NULL;
    }
    
    if (e3_subscription_manager_init(manager) != E3_SM_SUCCESS) {
        free(manager);
        return NULL;
    }
    
    return manager;
}

void e3_subscription_manager_destroy(e3_subscription_manager_t *manager) {
    if (!manager) {
        return;
    }
    
    if (manager->initialized) {
        pthread_mutex_destroy(&manager->manager_mutex);
    }
    
    if (manager->registered_dapps.entries) {
        free(manager->registered_dapps.entries);
    }
    
    if (manager->subscriptions.entries) {
        free(manager->subscriptions.entries);
    }
    
    free(manager);
    LOG_D(E3AP, "Subscription manager destroyed\n");
}

int e3_subscription_manager_init(e3_subscription_manager_t *manager) {
    if (!manager) {
        return E3_SM_ERROR_NULL_POINTER;
    }
    
    // Initialize mutex
    if (pthread_mutex_init(&manager->manager_mutex, NULL) != 0) {
        LOG_E(E3AP, "Failed to initialize subscription manager mutex\n");
        return E3_SM_ERROR_MEMORY_ALLOCATION;
    }
    
    // Initialize dApp list
    manager->registered_dapps.entries = calloc(E3_SUBSCRIPTION_MANAGER_INITIAL_CAPACITY,
                                              sizeof(e3_dapp_entry_t));
    if (!manager->registered_dapps.entries) {
        pthread_mutex_destroy(&manager->manager_mutex);
        LOG_E(E3AP, "Failed to allocate initial dApp list\n");
        return E3_SM_ERROR_MEMORY_ALLOCATION;
    }
    manager->registered_dapps.count = 0;
    manager->registered_dapps.capacity = E3_SUBSCRIPTION_MANAGER_INITIAL_CAPACITY;
    
    // Initialize subscription list
    manager->subscriptions.entries = calloc(E3_SUBSCRIPTION_MANAGER_INITIAL_CAPACITY,
                                           sizeof(e3_subscription_entry_t));
    if (!manager->subscriptions.entries) {
        free(manager->registered_dapps.entries);
        pthread_mutex_destroy(&manager->manager_mutex);
        LOG_E(E3AP, "Failed to allocate initial subscription list\n");
        return E3_SM_ERROR_MEMORY_ALLOCATION;
    }
    manager->subscriptions.count = 0;
    manager->subscriptions.capacity = E3_SUBSCRIPTION_MANAGER_INITIAL_CAPACITY;
    
    manager->initialized = true;
    LOG_I(E3AP, "Subscription manager initialized successfully\n");
    return E3_SM_SUCCESS;
}

int e3_subscription_manager_register_dapp(e3_subscription_manager_t *manager,
                                         uint32_t dapp_id) {
    if (!manager || !manager->initialized) {
        return E3_SM_ERROR_NOT_INITIALIZED;
    }
    
    if (dapp_id > 100) {
        LOG_E(E3AP, "Invalid dApp ID %u (must be 0-100)\n", dapp_id);
        return E3_SM_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&manager->manager_mutex);
    
    // Check if dApp is already registered
    if (find_dapp_entry_unlocked(manager, dapp_id) != NULL) {
        pthread_mutex_unlock(&manager->manager_mutex);
        LOG_W(E3AP, "dApp %u is already registered\n", dapp_id);
        return E3_SM_ERROR_ALREADY_EXISTS;
    }
    
    // Resize list if needed
    if (manager->registered_dapps.count >= manager->registered_dapps.capacity) {
        size_t new_capacity = manager->registered_dapps.capacity * E3_SUBSCRIPTION_MANAGER_GROWTH_FACTOR;
        int ret = resize_dapp_list(&manager->registered_dapps, new_capacity);
        if (ret != E3_SM_SUCCESS) {
            pthread_mutex_unlock(&manager->manager_mutex);
            return ret;
        }
    }
    
    // Add new dApp entry
    e3_dapp_entry_t *entry = &manager->registered_dapps.entries[manager->registered_dapps.count];
    entry->dapp_identifier = dapp_id;
    entry->registered_time = time(NULL);
    manager->registered_dapps.count++;
    
    pthread_mutex_unlock(&manager->manager_mutex);
    
    return E3_SM_SUCCESS;
}

int e3_subscription_manager_unregister_dapp(e3_subscription_manager_t *manager,
                                           uint32_t dapp_id) {
    if (!manager || !manager->initialized) {
        return E3_SM_ERROR_NOT_INITIALIZED;
    }
    
    pthread_mutex_lock(&manager->manager_mutex);
    
    // Find and remove dApp entry
    bool found = false;
    for (size_t i = 0; i < manager->registered_dapps.count; i++) {
        if (manager->registered_dapps.entries[i].dapp_identifier == dapp_id) {
            // Move last entry to this position to fill the gap
            if (i < manager->registered_dapps.count - 1) {
                manager->registered_dapps.entries[i] = 
                    manager->registered_dapps.entries[manager->registered_dapps.count - 1];
            }
            manager->registered_dapps.count--;
            found = true;
            break;
        }
    }
    
    if (!found) {
        pthread_mutex_unlock(&manager->manager_mutex);
        LOG_W(E3AP, "dApp %u not found for unregistration\n", dapp_id);
        return E3_SM_ERROR_NOT_FOUND;
    }
    
    // Track RAN functions that may need their SMs stopped
    uint32_t affected_ran_functions[256];  // Max RAN function ID is 255
    size_t affected_count = 0;
    
    // Remove all subscriptions for this dApp and track affected RAN functions
    size_t subscriptions_removed = 0;
    for (size_t i = 0; i < manager->subscriptions.count; ) {
        if (manager->subscriptions.entries[i].dapp_identifier == dapp_id) {
            uint32_t ran_func = manager->subscriptions.entries[i].ran_function_id;
            
            // Track this RAN function if not already tracked
            bool already_tracked = false;
            for (size_t j = 0; j < affected_count; j++) {
                if (affected_ran_functions[j] == ran_func) {
                    already_tracked = true;
                    break;
                }
            }
            if (!already_tracked && affected_count < 256) {
                affected_ran_functions[affected_count++] = ran_func;
            }
            
            // Move last entry to this position to fill the gap
            if (i < manager->subscriptions.count - 1) {
                manager->subscriptions.entries[i] = 
                    manager->subscriptions.entries[manager->subscriptions.count - 1];
            }
            manager->subscriptions.count--;
            subscriptions_removed++;
            // Don't increment i since we moved an entry to this position
        } else {
            i++;
        }
    }
    
    // Check each affected RAN function to see if it still has subscribers
    for (size_t i = 0; i < affected_count; i++) {
        uint32_t ran_func = affected_ran_functions[i];
        bool has_subscribers = false;
        
        for (size_t j = 0; j < manager->subscriptions.count; j++) {
            if (manager->subscriptions.entries[j].ran_function_id == ran_func) {
                has_subscribers = true;
                break;
            }
        }
        
        // Store whether to stop this SM (do the actual stop after unlocking)
        affected_ran_functions[i] = has_subscribers ? 0 : ran_func;
    }
    
    pthread_mutex_unlock(&manager->manager_mutex);
    
    LOG_I(E3AP, "dApp %u unregistered, %zu subscriptions removed\n", 
          dapp_id, subscriptions_removed);
    
    // Stop SMs for RAN functions that no longer have subscribers
    for (size_t i = 0; i < affected_count; i++) {
        if (affected_ran_functions[i] != 0) {
            LOG_I(E3AP, "Stopping SM for RAN function %u (no remaining subscribers)\n", 
                  affected_ran_functions[i]);
            int sm_stop_result = sm_registry_stop_sm(affected_ran_functions[i]);
            if (sm_stop_result != SM_SUCCESS) {
                LOG_W(E3AP, "Failed to stop SM for RAN function %u: %d\n", 
                      affected_ran_functions[i], sm_stop_result);
            }
        }
    }

    return E3_SM_SUCCESS;
}

bool e3_subscription_manager_is_dapp_registered(e3_subscription_manager_t *manager,
                                               uint32_t dapp_id) {
    if (!manager || !manager->initialized) {
        return false;
    }
    
    pthread_mutex_lock(&manager->manager_mutex);
    bool registered = (find_dapp_entry_unlocked(manager, dapp_id) != NULL);
    pthread_mutex_unlock(&manager->manager_mutex);
    
    return registered;
}

int e3_subscription_manager_add_subscription(e3_subscription_manager_t *manager,
                                           uint32_t dapp_id,
                                           uint32_t ran_function_id) {
    if (!manager || !manager->initialized) {
        return E3_SM_ERROR_NOT_INITIALIZED;
    }
    
    if (dapp_id > 100 || ran_function_id > 255) {
        LOG_E(E3AP, "Invalid parameters: dApp ID %u (must be 0-100), RAN function ID %u (must be 0-255)\n",
              dapp_id, ran_function_id);
        return E3_SM_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&manager->manager_mutex);
    
    // Check if dApp is registered
    if (find_dapp_entry_unlocked(manager, dapp_id) == NULL) {
        pthread_mutex_unlock(&manager->manager_mutex);
        LOG_E(E3AP, "dApp %u is not registered\n", dapp_id);
        return E3_SM_ERROR_NOT_FOUND;
    }
    
    // Check if subscription already exists
    for (size_t i = 0; i < manager->subscriptions.count; i++) {
        if (manager->subscriptions.entries[i].dapp_identifier == dapp_id &&
            manager->subscriptions.entries[i].ran_function_id == ran_function_id) {
            pthread_mutex_unlock(&manager->manager_mutex);
            LOG_W(E3AP, "Subscription already exists: dApp %u -> RAN function %u\n",
                  dapp_id, ran_function_id);
            return E3_SM_ERROR_ALREADY_EXISTS;
        }
    }
    
    // Check if this is the first subscriber for this RAN function
    bool is_first_subscriber = true;
    for (size_t i = 0; i < manager->subscriptions.count; i++) {
        if (manager->subscriptions.entries[i].ran_function_id == ran_function_id) {
            is_first_subscriber = false;
            break;
        }
    }
    
    // Resize list if needed
    if (manager->subscriptions.count >= manager->subscriptions.capacity) {
        size_t new_capacity = manager->subscriptions.capacity * E3_SUBSCRIPTION_MANAGER_GROWTH_FACTOR;
        int ret = resize_subscription_list(&manager->subscriptions, new_capacity);
        if (ret != E3_SM_SUCCESS) {
            pthread_mutex_unlock(&manager->manager_mutex);
            return ret;
        }
    }
    
    // Add new subscription entry
    e3_subscription_entry_t *entry = &manager->subscriptions.entries[manager->subscriptions.count];
    entry->dapp_identifier = dapp_id;
    entry->ran_function_id = ran_function_id;
    entry->created_time = time(NULL);
    manager->subscriptions.count++;
    pthread_mutex_unlock(&manager->manager_mutex);
    
    // Start SM thread if this is the first subscriber
    if (is_first_subscriber) {
        LOG_I(E3AP, "First subscriber for RAN function %u, starting SM thread\n", ran_function_id);
        int sm_start_result = sm_registry_start_sm(ran_function_id);
        if (sm_start_result != SM_SUCCESS) {
            LOG_E(E3AP, "Failed to start SM for RAN function %u: %d\n", ran_function_id, sm_start_result);
            // Remove the subscription we just added since SM failed to start
            pthread_mutex_lock(&manager->manager_mutex);
            manager->subscriptions.count--;
            pthread_mutex_unlock(&manager->manager_mutex);
            return E3_SM_ERROR_SM_START_FAILED;
        }
    }

    return E3_SM_SUCCESS;
}

int e3_subscription_manager_remove_subscription_for_dapp(
                                               e3_subscription_manager_t *manager,
                                               uint32_t dapp_id,
                                               uint32_t ran_function_id) {
    if (!manager || !manager->initialized) {
        return E3_SM_ERROR_NOT_INITIALIZED;
    }

    pthread_mutex_lock(&manager->manager_mutex);

    bool found = false;
    for (size_t i = 0; i < manager->subscriptions.count; i++) {
        if (manager->subscriptions.entries[i].dapp_identifier == dapp_id &&
            manager->subscriptions.entries[i].ran_function_id == ran_function_id) {
            if (i < manager->subscriptions.count - 1) {
                manager->subscriptions.entries[i] =
                    manager->subscriptions.entries[manager->subscriptions.count - 1];
            }
            manager->subscriptions.count--;
            found = true;
            LOG_I(E3AP, "Subscription removed: dApp %u -> RAN function %u\n",
                  dapp_id, ran_function_id);
            break;
        }
    }

    // Check if there are any remaining subscribers for this RAN function
    bool has_remaining_subscribers = false;
    if (found) {
        for (size_t i = 0; i < manager->subscriptions.count; i++) {
            if (manager->subscriptions.entries[i].ran_function_id == ran_function_id) {
                has_remaining_subscribers = true;
                break;
            }
        }
    }

    pthread_mutex_unlock(&manager->manager_mutex);

    if (!found) {
        LOG_W(E3AP, "Subscription not found for dApp %u -> RAN function %u\n", dapp_id, ran_function_id);
        return E3_SM_ERROR_NOT_SUBSCRIBED;
    }
    
    // Stop SM thread if this was the last subscriber
    if (!has_remaining_subscribers) {
        LOG_I(E3AP, "Last subscriber removed for RAN function %u, stopping SM thread\n", ran_function_id);
        int sm_stop_result = sm_registry_stop_sm(ran_function_id);
        if (sm_stop_result != SM_SUCCESS) {
            LOG_W(E3AP, "Failed to stop SM for RAN function %u: %d\n", ran_function_id, sm_stop_result);
        }
    }

    return E3_SM_SUCCESS;
}

int e3_subscription_manager_remove_all_subscriptions_for_dapp(
                                               e3_subscription_manager_t *manager,
                                               uint32_t dapp_id) {
    if (!manager || !manager->initialized) {
        return E3_SM_ERROR_NOT_INITIALIZED;
    }
    
    pthread_mutex_lock(&manager->manager_mutex);
    
    size_t subscriptions_removed = 0;
    for (size_t i = 0; i < manager->subscriptions.count; ) {
        if (manager->subscriptions.entries[i].dapp_identifier == dapp_id) {
            // Move last entry to this position to fill the gap
            if (i < manager->subscriptions.count - 1) {
                manager->subscriptions.entries[i] = 
                    manager->subscriptions.entries[manager->subscriptions.count - 1];
            }
            manager->subscriptions.count--;
            subscriptions_removed++;
            // Don't increment i since we moved an entry to this position
        } else {
            i++;
        }
    }
    
    pthread_mutex_unlock(&manager->manager_mutex);
    
    LOG_I(E3AP, "Removed %zu subscriptions for dApp %u\n", subscriptions_removed, dapp_id);
    return (int)subscriptions_removed;
}

int e3_subscription_manager_get_subscribed_dapps(e3_subscription_manager_t *manager,
                                                uint32_t ran_function_id,
                                                uint32_t **dapp_ids) {
    if (!manager || !manager->initialized || !dapp_ids) {
        return E3_SM_ERROR_NULL_POINTER;
    }
    
    pthread_mutex_lock(&manager->manager_mutex);
    
    // Count subscriptions for this RAN function
    size_t count = 0;
    for (size_t i = 0; i < manager->subscriptions.count; i++) {
        if (manager->subscriptions.entries[i].ran_function_id == ran_function_id) {
            count++;
        }
    }
    
    if (count == 0) {
        pthread_mutex_unlock(&manager->manager_mutex);
        *dapp_ids = NULL;
        return 0;
    }
    
    // Allocate result array
    *dapp_ids = malloc(count * sizeof(uint32_t));
    if (!*dapp_ids) {
        pthread_mutex_unlock(&manager->manager_mutex);
        LOG_E(E3AP, "Failed to allocate memory for dApp IDs array\n");
        return E3_SM_ERROR_MEMORY_ALLOCATION;
    }
    
    // Fill result array
    size_t idx = 0;
    for (size_t i = 0; i < manager->subscriptions.count && idx < count; i++) {
        if (manager->subscriptions.entries[i].ran_function_id == ran_function_id) {
            (*dapp_ids)[idx++] = manager->subscriptions.entries[i].dapp_identifier;
        }
    }
    
    pthread_mutex_unlock(&manager->manager_mutex);
    
    return (int)count;
}

bool e3_subscription_manager_is_dapp_subscribed_to_ran_function(
                                               e3_subscription_manager_t *manager,
                                               uint32_t dapp_id,
                                               uint32_t ran_function_id) {
    if (!manager || !manager->initialized) {
        return false;
    }
    
    pthread_mutex_lock(&manager->manager_mutex);
    
    bool subscribed = false;
    for (size_t i = 0; i < manager->subscriptions.count; i++) {
        if (manager->subscriptions.entries[i].dapp_identifier == dapp_id &&
            manager->subscriptions.entries[i].ran_function_id == ran_function_id) {
            subscribed = true;
            break;
        }
    }
    
    pthread_mutex_unlock(&manager->manager_mutex);
    
    return subscribed;
}

int e3_subscription_manager_get_active_ran_functions(e3_subscription_manager_t *manager,
                                                   uint32_t **ran_function_ids) {
    if (!manager || !manager->initialized || !ran_function_ids) {
        return E3_SM_ERROR_NULL_POINTER;
    }
    
    pthread_mutex_lock(&manager->manager_mutex);
    
    // Use a simple array to track unique RAN function IDs
    // Maximum possible is 256 (0-255)
    bool ran_functions_active[256] = {false};
    size_t unique_count = 0;
    
    // Mark active RAN functions
    for (size_t i = 0; i < manager->subscriptions.count; i++) {
        uint32_t ran_func_id = manager->subscriptions.entries[i].ran_function_id;
        if (!ran_functions_active[ran_func_id]) {
            ran_functions_active[ran_func_id] = true;
            unique_count++;
        }
    }
    
    if (unique_count == 0) {
        pthread_mutex_unlock(&manager->manager_mutex);
        *ran_function_ids = NULL;
        return 0;
    }
    
    // Allocate result array
    *ran_function_ids = malloc(unique_count * sizeof(uint32_t));
    if (!*ran_function_ids) {
        pthread_mutex_unlock(&manager->manager_mutex);
        LOG_E(E3AP, "Failed to allocate memory for RAN function IDs array\n");
        return E3_SM_ERROR_MEMORY_ALLOCATION;
    }
    
    // Fill result array
    size_t idx = 0;
    for (uint32_t ran_func_id = 0; ran_func_id < 256 && idx < unique_count; ran_func_id++) {
        if (ran_functions_active[ran_func_id]) {
            (*ran_function_ids)[idx++] = ran_func_id;
        }
    }
    
    pthread_mutex_unlock(&manager->manager_mutex);
    
    LOG_D(E3AP, "Found %zu active RAN functions\n", unique_count);
    return (int)unique_count;
}

bool e3_subscription_manager_has_subscribers_for_ran_function(
                                               e3_subscription_manager_t *manager,
                                               uint32_t ran_function_id) {
    if (!manager || !manager->initialized) {
        return false;
    }
    
    pthread_mutex_lock(&manager->manager_mutex);
    
    bool has_subscribers = false;
    for (size_t i = 0; i < manager->subscriptions.count; i++) {
        if (manager->subscriptions.entries[i].ran_function_id == ran_function_id) {
            has_subscribers = true;
            break;
        }
    }
    
    pthread_mutex_unlock(&manager->manager_mutex);
    
    return has_subscribers;
}

int e3_subscription_manager_get_subscriber_count_for_ran_function(
                                               e3_subscription_manager_t *manager,
                                               uint32_t ran_function_id) {
    if (!manager || !manager->initialized) {
        return E3_SM_ERROR_NOT_INITIALIZED;
    }
    
    pthread_mutex_lock(&manager->manager_mutex);
    
    size_t count = 0;
    for (size_t i = 0; i < manager->subscriptions.count; i++) {
        if (manager->subscriptions.entries[i].ran_function_id == ran_function_id) {
            count++;
        }
    }
    
    pthread_mutex_unlock(&manager->manager_mutex);
    
    return (int)count;
}

int e3_subscription_manager_get_dapp_count(e3_subscription_manager_t *manager) {
    if (!manager || !manager->initialized) {
        return E3_SM_ERROR_NOT_INITIALIZED;
    }
    
    pthread_mutex_lock(&manager->manager_mutex);
    size_t count = manager->registered_dapps.count;
    pthread_mutex_unlock(&manager->manager_mutex);
    
    return (int)count;
}

int e3_subscription_manager_get_subscription_count(e3_subscription_manager_t *manager) {
    if (!manager || !manager->initialized) {
        return E3_SM_ERROR_NOT_INITIALIZED;
    }
    
    pthread_mutex_lock(&manager->manager_mutex);
    size_t count = manager->subscriptions.count;
    pthread_mutex_unlock(&manager->manager_mutex);
    
    return (int)count;
}

void e3_subscription_manager_print_status(e3_subscription_manager_t *manager) {
    if (!manager || !manager->initialized) {
        printf("E3 Subscription Manager: Not initialized\n");
        return;
    }
    
    pthread_mutex_lock(&manager->manager_mutex);
    
    printf("=== E3 Subscription Manager Status ===\n");
    printf("Registered dApps: %zu\n", manager->registered_dapps.count);
    for (size_t i = 0; i < manager->registered_dapps.count; i++) {
        printf("  dApp %u (registered: %ld)\n", 
               manager->registered_dapps.entries[i].dapp_identifier,
               manager->registered_dapps.entries[i].registered_time);
    }
    
    printf("Active Subscriptions: %zu\n", manager->subscriptions.count);
    for (size_t i = 0; i < manager->subscriptions.count; i++) {
        printf("  dApp %u -> RAN function %u (created: %ld)\n",
               manager->subscriptions.entries[i].dapp_identifier,
               manager->subscriptions.entries[i].ran_function_id,
               manager->subscriptions.entries[i].created_time);
    }
    printf("=======================================\n");
    
    pthread_mutex_unlock(&manager->manager_mutex);
}

int e3_subscription_manager_get_dapp_subscriptions(e3_subscription_manager_t *manager,
                                                  uint32_t dapp_id,
                                                  uint32_t **ran_function_ids) {
    if (!manager || !manager->initialized || !ran_function_ids) {
        return E3_SM_ERROR_NULL_POINTER;
    }
    
    pthread_mutex_lock(&manager->manager_mutex);
    
    // Count subscriptions for this dApp
    size_t count = 0;
    for (size_t i = 0; i < manager->subscriptions.count; i++) {
        if (manager->subscriptions.entries[i].dapp_identifier == dapp_id) {
            count++;
        }
    }
    
    if (count == 0) {
        pthread_mutex_unlock(&manager->manager_mutex);
        *ran_function_ids = NULL;
        return 0;
    }
    
    // Allocate result array
    *ran_function_ids = malloc(count * sizeof(uint32_t));
    if (!*ran_function_ids) {
        pthread_mutex_unlock(&manager->manager_mutex);
        LOG_E(E3AP, "Failed to allocate memory for RAN function IDs array\n");
        return E3_SM_ERROR_MEMORY_ALLOCATION;
    }
    
    // Fill result array
    size_t idx = 0;
    for (size_t i = 0; i < manager->subscriptions.count && idx < count; i++) {
        if (manager->subscriptions.entries[i].dapp_identifier == dapp_id) {
            (*ran_function_ids)[idx++] = manager->subscriptions.entries[i].ran_function_id;
        }
    }
    
    pthread_mutex_unlock(&manager->manager_mutex);
    
    LOG_D(E3AP, "Found %zu subscriptions for dApp %u\n", count, dapp_id);
    return (int)count;
}