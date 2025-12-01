#ifndef E3_SUBSCRIPTION_MANAGER_H
#define E3_SUBSCRIPTION_MANAGER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>

#include "common/utils/LOG/log.h"
#include "e3ap_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file e3_subscription_manager.h
 * @brief E3 Subscription Manager for tracking dApp registrations and RAN function subscriptions
 * 
 * This module manages the associations between dApps and RAN functions on the RAN side.
 * It handles:
 * - dApp registration from E3 Setup requests
 * - Subscription management from E3 Subscription requests  
 * - Message routing for indication and control messages
 * - RAN function lifecycle management
 */

/* Constants */
#define E3_SUBSCRIPTION_MANAGER_INITIAL_CAPACITY 16
#define E3_SUBSCRIPTION_MANAGER_GROWTH_FACTOR 2

/* Error codes */
#define E3_SM_SUCCESS                    0
#define E3_SM_ERROR_NULL_POINTER        -1
#define E3_SM_ERROR_INVALID_PARAM       -2
#define E3_SM_ERROR_MEMORY_ALLOCATION   -3
#define E3_SM_ERROR_NOT_FOUND           -4
#define E3_SM_ERROR_ALREADY_EXISTS      -5
#define E3_SM_ERROR_NOT_SUBSCRIBED      -6
#define E3_SM_ERROR_NOT_INITIALIZED     -7
/* Error when SM start failed after adding a subscription */
#define E3_SM_ERROR_SM_START_FAILED     -8

/* Type definitions */
typedef time_t timestamp_t;

/**
 * dApp registration entry (from E3 Setup Request)
 */
typedef struct {
    uint32_t dapp_identifier;          /**< dApp ID (0-100) */
    timestamp_t registered_time;       /**< When dApp was registered */
} e3_dapp_entry_t;

/**
 * Subscription entry representing dApp-RAN function association
 */
typedef struct {
    uint32_t dapp_identifier;          /**< dApp ID (0-100) */
    uint32_t ran_function_id;          /**< RAN function ID (0-255) */
    timestamp_t created_time;          /**< When subscription was created */
} e3_subscription_entry_t;

/**
 * Dynamic list for dApp registrations
 */
typedef struct {
    e3_dapp_entry_t *entries;          /**< Array of dApp entries */
    size_t count;                      /**< Current number of registered dApps */
    size_t capacity;                   /**< Current array capacity */
} e3_dapp_list_t;

/**
 * Dynamic list for subscriptions
 */
typedef struct {
    e3_subscription_entry_t *entries;  /**< Array of subscription entries */
    size_t count;                      /**< Current number of subscriptions */
    size_t capacity;                   /**< Current array capacity */
} e3_subscription_list_t;

/**
 * Main subscription manager structure
 */
typedef struct {
    e3_dapp_list_t registered_dapps;   /**< List of registered dApps */
    e3_subscription_list_t subscriptions; /**< List of active subscriptions */
    pthread_mutex_t manager_mutex;     /**< Thread safety mutex */
    bool initialized;                  /**< Initialization status */
} e3_subscription_manager_t;

/* Manager lifecycle functions */

/**
 * Create and initialize a new subscription manager
 * @return Pointer to created manager, NULL on error
 */
e3_subscription_manager_t* e3_subscription_manager_create(void);

/**
 * Destroy subscription manager and free all resources
 * @param manager Subscription manager to destroy
 */
void e3_subscription_manager_destroy(e3_subscription_manager_t *manager);

/**
 * Initialize an existing subscription manager structure
 * @param manager Subscription manager to initialize
 * @return E3_SM_SUCCESS on success, error code on failure
 */
int e3_subscription_manager_init(e3_subscription_manager_t *manager);

/* dApp registration management (E3 Setup flow) */

/**
 * Register a dApp from E3 Setup Request
 * @param manager Subscription manager
 * @param dapp_id dApp identifier (0-100)
 * @return E3_SM_SUCCESS on success, error code on failure
 */
int e3_subscription_manager_register_dapp(e3_subscription_manager_t *manager,
                                         uint32_t dapp_id);

/**
 * Unregister a dApp and clean up all its subscriptions
 * @param manager Subscription manager
 * @param dapp_id dApp identifier (0-100)
 * @return E3_SM_SUCCESS on success, error code on failure
 */
int e3_subscription_manager_unregister_dapp(e3_subscription_manager_t *manager,
                                           uint32_t dapp_id);

/**
 * Check if a dApp is registered
 * @param manager Subscription manager
 * @param dapp_id dApp identifier (0-100)
 * @return true if registered, false otherwise
 */
bool e3_subscription_manager_is_dapp_registered(e3_subscription_manager_t *manager,
                                               uint32_t dapp_id);

/* Subscription management (E3 Subscription flow) */

/**
 * Add a subscription between dApp and RAN function
 * @param manager Subscription manager
 * @param dapp_id dApp identifier (0-100)
 * @param ran_function_id RAN function identifier (0-255)
 * @return Subscription ID on success, negative error code on failure
 */
int e3_subscription_manager_add_subscription(e3_subscription_manager_t *manager,
                                           uint32_t dapp_id,
                                           uint32_t ran_function_id);



/**
 * Remove a subscription for a specific dApp and RAN function
 * @param manager Subscription manager
 * @param dapp_id dApp identifier (0-100)
 * @param ran_function_id RAN function identifier (0-255)
 * @return E3_SM_SUCCESS on success, E3_SM_ERROR_NOT_SUBSCRIBED if not found, other error codes on failure
 */
int e3_subscription_manager_remove_subscription_for_dapp(
                                               e3_subscription_manager_t *manager,
                                               uint32_t dapp_id,
                                               uint32_t ran_function_id);

/**
 * Remove all subscriptions for a specific dApp
 * @param manager Subscription manager
 * @param dapp_id dApp identifier (0-100)
 * @return Number of subscriptions removed, negative error code on failure
 */
int e3_subscription_manager_remove_all_subscriptions_for_dapp(
                                               e3_subscription_manager_t *manager,
                                               uint32_t dapp_id);

/* Message routing queries */

/**
 * Get list of dApps subscribed to a RAN function (for indication messages)
 * @param manager Subscription manager
 * @param ran_function_id RAN function identifier (0-255)
 * @param dapp_ids Output array of dApp IDs (caller must free)
 * @return Number of subscribed dApps, negative error code on failure
 */
int e3_subscription_manager_get_subscribed_dapps(e3_subscription_manager_t *manager,
                                                uint32_t ran_function_id,
                                                uint32_t **dapp_ids);

/**
 * Check if dApp is subscribed to RAN function (for control message validation)
 * @param manager Subscription manager
 * @param dapp_id dApp identifier (0-100)
 * @param ran_function_id RAN function identifier (0-255)
 * @return true if subscribed, false otherwise
 */
bool e3_subscription_manager_is_dapp_subscribed_to_ran_function(
                                               e3_subscription_manager_t *manager,
                                               uint32_t dapp_id,
                                               uint32_t ran_function_id);

/* RAN function lifecycle management */

/**
 * Get list of RAN functions that have active subscriptions
 * @param manager Subscription manager
 * @param ran_function_ids Output array of RAN function IDs (caller must free)
 * @return Number of active RAN functions, negative error code on failure
 */
int e3_subscription_manager_get_active_ran_functions(e3_subscription_manager_t *manager,
                                                   uint32_t **ran_function_ids);

/**
 * Check if a RAN function has any subscribers
 * @param manager Subscription manager
 * @param ran_function_id RAN function identifier (0-255)
 * @return true if has subscribers, false otherwise
 */
bool e3_subscription_manager_has_subscribers_for_ran_function(
                                               e3_subscription_manager_t *manager,
                                               uint32_t ran_function_id);

/**
 * Get count of subscribers for a specific RAN function
 * @param manager Subscription manager
 * @param ran_function_id RAN function identifier (0-255)
 * @return Number of subscribers, negative error code on failure
 */
int e3_subscription_manager_get_subscriber_count_for_ran_function(
                                               e3_subscription_manager_t *manager,
                                               uint32_t ran_function_id);

/* Statistics and utility functions */

/**
 * Get number of registered dApps
 * @param manager Subscription manager
 * @return Number of registered dApps, negative error code on failure
 */
int e3_subscription_manager_get_dapp_count(e3_subscription_manager_t *manager);

/**
 * Get total number of active subscriptions
 * @param manager Subscription manager
 * @return Number of subscriptions, negative error code on failure
 */
int e3_subscription_manager_get_subscription_count(e3_subscription_manager_t *manager);

/**
 * Print current status of subscription manager (for debugging)
 * @param manager Subscription manager
 */
void e3_subscription_manager_print_status(e3_subscription_manager_t *manager);

/**
 * Get all subscriptions for a specific dApp
 * @param manager Subscription manager
 * @param dapp_id dApp identifier (0-100)
 * @param ran_function_ids Output array of RAN function IDs (caller must free)
 * @return Number of subscriptions, negative error code on failure
 */
int e3_subscription_manager_get_dapp_subscriptions(e3_subscription_manager_t *manager,
                                                  uint32_t dapp_id,
                                                  uint32_t **ran_function_ids);

#ifdef __cplusplus
}
#endif

#endif /* E3_SUBSCRIPTION_MANAGER_H */