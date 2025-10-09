#include "e3_agent.h"
#include "config/e3_config.h"
#include "e3_response_queue.h"
#include <zmq.h>

// TODO replace pthreads with itti or use a faster way
// #include "intertask_interface.h"
// #include "create_tasks.h"
#include <pthread.h>
#include <errno.h>
#include <time.h>

#include "e3ap_handler.h"
#include "e3ap_types.h"
#include "e3_subscription_manager.h"
#include "service_models/sm_interface.h"

#include "common/utils/system.h"
#include "common/ran_context.h"
#include "common/utils/LOG/log.h"
#include "e3_connector.h"

#define BUFFER_SIZE 60000

typedef struct {
  e3_config_t *e3_configs;
  E3Connector *connector;
  e3_response_queue_t *response_queue;
} pub_sub_args_t;

typedef struct {
  e3_response_queue_t *response_queue;
} sm_data_args_t;

e3_subscription_manager_t *e3_subscription_manager = NULL;
pthread_t e3_interface_thread;

int e3_agent_init()
{
  LOG_D(E3AP, "Read configuration\n");
  e3_config_t *e3_configs = (e3_config_t *)calloc(sizeof(e3_config_t), 1);
  e3_readconfig(e3_configs);
  LOG_D(E3AP, "Validate configuration\n");
  validate_configuration(e3_configs);


  e3_subscription_manager = e3_subscription_manager_create();
  if (!e3_subscription_manager) {
    LOG_E(E3AP, "Failed to create E3 subscription manager\n");
    return -1;
  }

  LOG_D(E3AP, "Start E3 Agent main thread\n");
  if (pthread_create(&e3_interface_thread, NULL, e3_agent_dapp_task, (void *)e3_configs) != 0) {
    LOG_E(E3AP, "Error creating E3 Agent thread: %s\n", strerror(errno));
    return -1;
  }
  return 0;
}

int e3_agent_destroy()
{
  if (pthread_join(e3_interface_thread, NULL) != 0) {
    LOG_E(E3AP, "Error joining E3 interface thread: %s\n", strerror(errno));
    return -1;
  }

  // Clean up SM registry
  sm_registry_destroy();

  // Clean up subscription manager
  if (e3_subscription_manager) {
    e3_subscription_manager_destroy(e3_subscription_manager);
    e3_subscription_manager = NULL;
  }

  return 0;
}



// The subscriber thread is responsible of receiving the control action messages and the dApp report messages
void *subscriber_thread(void *arg)
{
  pub_sub_args_t *sub_args = (pub_sub_args_t *)arg;
  E3Connector *e3connector = sub_args->connector;
  e3_response_queue_t *response_queue = sub_args->response_queue;
  uint8_t *buffer = malloc(BUFFER_SIZE);
  size_t buffer_size = BUFFER_SIZE;

  int ret;

  e3connector->setup_inbound_connection(e3connector);

  while (1) {
    ret = e3connector->receive(e3connector, buffer, buffer_size);
    if (ret < 0) {
      LOG_E(E3AP, "Error in inbound connection: %s\n", strerror(errno));
      abort();
    }
    if (ret == 0) {
      LOG_I(E3AP, "No bytes received in the inbound connection, closing\n");
      break;
    }
   
    // Create encoded message wrapper
    E3EncodedMessage encoded_msg = {
        .buffer = buffer,
        .size = ret,
#ifdef E3_ASN1_FORMAT
        .format = FORMAT_ASN1   
#else
        .format = FORMAT_JSON
#endif
    };

    e3ap_pdu_t *received_pdu = e3_decode_message(&encoded_msg);
    
    if (received_pdu) {
      switch (received_pdu->pdu_type) {
        case E3AP_PDU_TYPE_SUBSCRIPTION_REQUEST: {
          // Handle subscription request
          uint32_t dapp_id = received_pdu->choice.subscription_request.dapp_identifier;
          uint32_t ran_function_id = received_pdu->choice.subscription_request.ran_function_identifier;
          
          LOG_I(E3AP, "Received subscription request for RAN function %u\n", ran_function_id);
          e3ap_response_code_t response_code = E3AP_RESPONSE_CODE_NEGATIVE;
          
          // Add subscription if dApp is registered
          if (e3_subscription_manager_is_dapp_registered(e3_subscription_manager, dapp_id)) {
            int sub_result = e3_subscription_manager_add_subscription(e3_subscription_manager, 
                                                                     dapp_id, ran_function_id);
            if (sub_result > 0) {
              LOG_I(E3AP, "Subscription added: dApp %u -> RAN function %u (ID: %d)\n", 
                    dapp_id, ran_function_id, sub_result);
              response_code = E3AP_RESPONSE_CODE_POSITIVE;
              
              // Start SM if this is the first subscription for this RAN function
              if (!sm_registry_is_sm_running(ran_function_id)) {
                int start_result = sm_registry_start_sm(ran_function_id);
                if (start_result == SM_SUCCESS) {
                  LOG_I(E3AP, "Started SM for RAN function %u\n", ran_function_id);
                } else {
                  LOG_E(E3AP, "Failed to start SM for RAN function %u: %d\n", ran_function_id, start_result);
                  response_code = E3AP_RESPONSE_CODE_NEGATIVE; // Failed to start SM
                }
              }
            } else {
              LOG_E(E3AP, "Failed to add subscription: %d\n", sub_result);
              response_code = E3AP_RESPONSE_CODE_NEGATIVE; // Failed to start subscription
            }
          } else {
            LOG_E(E3AP, "dApp %u not registered, cannot add subscription\n", dapp_id);
            response_code = E3AP_RESPONSE_CODE_NEGATIVE;
          }
          
          // Create subscription response PDU
          e3ap_pdu_t *response_pdu = e3ap_create_subscription_response(
              received_pdu->choice.subscription_request.id,  // Original request ID
              response_code
          );
           
          if (response_pdu && e3_response_queue_push(response_queue, response_pdu) == 0) {
              LOG_D(E3AP, "Queued subscription response for dApp %u\n", dapp_id);
          } else {
            if (response_queue->count >= RESPONSE_QUEUE_SIZE) {
              LOG_E(E3AP, "Failed to queue subscription response - queue is full\n");
            } else if (!response_pdu) {
              LOG_E(E3AP, "Failed to queue subscription response - response PDU is NULL\n");
            } else {
              LOG_E(E3AP, "Failed to queue subscription response - PDU copy failed\n");
            }
            LOG_E(E3AP, "Queue status: count=%d/%d, head=%d, tail=%d\n", 
                  response_queue->count, RESPONSE_QUEUE_SIZE, 
                  response_queue->head, response_queue->tail);
          }
            
          if (response_pdu) {
              e3ap_pdu_free(response_pdu);
          }    
          e3ap_pdu_free(received_pdu);
          continue;
        }
        
        case E3AP_PDU_TYPE_CONTROL_ACTION: {
          uint32_t ran_function_id = received_pdu->choice.control_action.ran_function_identifier;
          uint32_t dapp_id = received_pdu->choice.control_action.dapp_identifier;
          uint8_t *action_data = received_pdu->choice.control_action.action_data;
          size_t action_data_size = received_pdu->choice.control_action.action_data_size;

          LOG_I(E3AP, "Received control action for RAN function %u from dApp %u (%zu bytes)\n", ran_function_id, dapp_id, action_data_size);
          e3_service_model_t *sm = sm_registry_get_by_ran_function(ran_function_id);
          if (sm && sm->process_dapp_control_action) {
                int control_result = sm->process_dapp_control_action(ran_function_id, action_data, action_data_size);
                if (control_result == SM_SUCCESS) {
                  LOG_I(E3AP, "Control action processed successfully by SM\n");
                  // Send message here ack if needed. At the moment we do not have SM needing it so no ack
                } else {
                  LOG_E(E3AP, "SM failed to process control action: %d\n", control_result);
                }
            } else {
              LOG_E(E3AP, "No SM found for RAN function %u or SM doesn't support control\n", ran_function_id);
            }
  
          e3ap_pdu_free(received_pdu);
          continue;
        }
        
        default:
          LOG_W(E3AP, "Received unknown PDU type: %d\n", received_pdu->pdu_type);
          e3ap_pdu_free(received_pdu);
          continue;
      }
    } else {
      LOG_E(E3AP, "Failed to decode PDU in subscriber thread\n");
    }
  }

  return NULL;
}

// The SM data handler thread is responsible for polling SMs and queuing indication messages
void *sm_data_handler_thread(void *arg)
{
  sm_data_args_t *sm_args = (sm_data_args_t *)arg;
  
  LOG_I(E3AP, "SM data handler thread started\n");
  
  while (1) {
    bool data_processed = false;
    
    // Poll all active RAN functions for available indication data
    uint32_t *ran_function_ids = NULL;
    int num_ran_functions = e3_subscription_manager_get_active_ran_functions(e3_subscription_manager, 
                                                                             &ran_function_ids);
    
    if (num_ran_functions < 0) {
      LOG_D(E3AP, "No active RAN functions: %d\n", num_ran_functions);
      sleep(1); // 1s
      continue;
    }

    for (int i = 0; i < num_ran_functions; i++) {
      uint32_t ran_function_id = ran_function_ids[i];
      
      // Check if there are subscribers for this RAN function
      uint32_t *subscribed_dapps = NULL;
      int subscriber_count = e3_subscription_manager_get_subscribed_dapps(e3_subscription_manager, 
                                                                         ran_function_id, 
                                                                         &subscribed_dapps);
      
      if (subscriber_count > 0) {
        // Get SM for this RAN function
        e3_service_model_t *sm = sm_registry_get_by_ran_function(ran_function_id);
        if (sm && sm->is_running && sm->thread_context && sm->thread_context->output_data) {
          // Try to get indication data from SM
          uint8_t *sm_encoded_data = NULL;
          size_t sm_encoded_size = 0;
          uint64_t timestamp = 0;
          
          int get_result = sm_indication_data_get(sm->thread_context->output_data,
                                                 &sm_encoded_data, &sm_encoded_size, &timestamp);
          
          if (get_result == SM_SUCCESS && sm_encoded_data) {
            data_processed = true;
            LOG_D(E3AP, "Found indication data from SM for RAN function %u (%zu bytes)\n", 
                  ran_function_id, sm_encoded_size);
            
            // Create separate indication messages for each subscriber
            for (int k = 0; k < subscriber_count; k++) {
              e3ap_pdu_t *indicationMessage = e3ap_create_indication_message(
                subscribed_dapps[k],     // dApp identifier for this subscriber
                sm_encoded_data, 
                sm_encoded_size
              );

              if (indicationMessage) {
                if (e3_response_queue_push(sm_args->response_queue, indicationMessage) != 0) {
                  LOG_E(E3AP, "Failed to queue indication message for dApp %u\n", subscribed_dapps[k]);
                  e3ap_pdu_free(indicationMessage);
                } else {
                  LOG_D(E3AP, "Queued indication message for dApp %u\n", subscribed_dapps[k]);
                }
              } else {
                LOG_E(E3AP, "Failed to create indication message for dApp %u\n", subscribed_dapps[k]);
              }
            }
            
            LOG_D(E3AP, "SM indication data queued for delivery to %d subscribers\n", subscriber_count);
            
            free(sm_encoded_data);
          }
        }
      }
      
      if (subscribed_dapps) {
        free(subscribed_dapps);
      }
    }
    
    // Free the dynamically allocated RAN function IDs array
    if (ran_function_ids) {
      free(ran_function_ids);
    }
    
    // If no data was processed, sleep briefly to avoid busy waiting
    if (!data_processed) {
      usleep(10000); // 10ms
    }
  }
  
  return NULL;
}

// The publisher thread is responsible of sending the indication messages and the xApp control actions
void *publisher_thread(void *args)
{
  pub_sub_args_t *pub_sub_args = (pub_sub_args_t *)args;
  E3Connector *e3connector = pub_sub_args->connector;
  e3_response_queue_t *response_queue = pub_sub_args->response_queue;
  int ret;
  
  /* write on a socket fails if the other end is closed and we get SIGPIPE */
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    abort();

  do {
    sleep(3);
    LOG_I(E3AP, "Trying to setup the outbound connection\n");
    ret = e3connector->setup_outbound_connection(e3connector);
    if (ret != 0) {
      LOG_D(E3AP, "Failed to send Indication PDU: %s\n", strerror(errno));
    } else {
      LOG_I(E3AP, "Outbound connection setup\n");
    }
  } while (ret);

  LOG_D(E3AP, "Start response message processing loop\n");

  while (1) {

      e3ap_pdu_t response_pdu;
      // Block waiting for response messages
      if (e3_response_queue_pop(response_queue, &response_pdu, 0) == 0) {
        LOG_D(E3AP, "Processing queued response PDU\n");
        
        // Encode and send the PDU
        E3EncodedMessage *encoded_response = e3_encode_pdu(&response_pdu);
        if (encoded_response) {
          ret = e3connector->send(e3connector, encoded_response->buffer, encoded_response->size);
          e3_free_encoded_message(encoded_response);
          if (ret < 0) {
            LOG_E(E3AP, "Failed to send PDU: %s\n", strerror(errno));
          } else {
            LOG_D(E3AP, "Sent PDU successfully\n");
          }
        } else {
          LOG_E(E3AP, "Failed to encode PDU\n");
        }
      }
  }

  return NULL;
}

/**
* Handle dApp disconnection and cleanup subscriptions
* @param dapp_id dApp identifier to cleanup
*/

void handle_dapp_disconnection(uint32_t dapp_id) {
    if (!e3_subscription_manager) {
         LOG_W(E3AP, "Subscription manager not initialized\n");
         return;
    }  
    // Get current subscriptions for logging 
  uint32_t *ran_functions = NULL;
  int sub_count = e3_subscription_manager_get_dapp_subscriptions(e3_subscription_manager, dapp_id, &ran_functions);
 
 if (sub_count > 0) {
   LOG_I(E3AP, "Cleaning up %d subscriptions for disconnected dApp %u\n", sub_count, dapp_id);
   
   // Check if any RAN functions will no longer have subscribers
   for (int i = 0; i < sub_count; i++) {
     uint32_t ran_func_id = ran_functions[i];
     int remaining_subs = e3_subscription_manager_get_subscriber_count_for_ran_function(
                                                  e3_subscription_manager, ran_func_id);
     if (remaining_subs <= 1) { 
        // This dApp was the last subscriber, stop the SM
       LOG_I(E3AP, "RAN function %u will be deactivated (no remaining subscribers)\n", ran_func_id);
       
       int stop_result = sm_registry_stop_sm(ran_func_id);
       if (stop_result == SM_SUCCESS) {
         LOG_I(E3AP, "Stopped SM for RAN function %u\n", ran_func_id);
       } else {
         LOG_E(E3AP, "Failed to stop SM for RAN function %u: %d\n", ran_func_id, stop_result);
       }
     }
   }
   
   free(ran_functions);
 }
 
 // Unregister dApp (this also removes all subscriptions)
 int result = e3_subscription_manager_unregister_dapp(e3_subscription_manager, dapp_id);
 if (result == E3_SM_SUCCESS) {
   LOG_I(E3AP, "dApp %u unregistered and cleaned up successfully\n", dapp_id);
 } else {
   LOG_E(E3AP, "Failed to unregister dApp %u: %d\n", dapp_id, result);
 }
}
  
void *e3_agent_dapp_task(void *args_p){
  e3_config_t *e3_configs = (e3_config_t *)args_p;
  pthread_t pub_thread, sub_thread, sm_data_thread;
  int ret;
  uint32_t *available_ran_functions = NULL;
  uint32_t ran_function_count = 0;

  LOG_D(E3AP, "Initialize SM registry\n");
  if (sm_registry_init() != SM_SUCCESS) {
    LOG_E(E3AP, "Failed to initialize SM registry\n");
    abort();
  }
  
  int get_result = sm_registry_get_available_ran_functions(&available_ran_functions, &ran_function_count);
  if (get_result != SM_SUCCESS || ran_function_count == 0) {
    LOG_W(E3AP, "No RAN functions available or failed to get them: %d\n", get_result);
    // Fallback to empty list
    available_ran_functions = NULL;
    ran_function_count = 0;
  } else {
    LOG_D(E3AP, "Found %u available RAN functions\n", ran_function_count);
  }

  E3Connector *e3connector = create_connector(e3_configs->link, e3_configs->transport);
  if (e3connector == NULL) {
    LOG_E(E3AP, "Failed to create the E3Connector\n");
    abort();
  }
  uint8_t *buffer = malloc(BUFFER_SIZE);
  size_t buffer_size = BUFFER_SIZE;

  LOG_D(E3AP, "Initialize response queue\n");
  e3_response_queue_t *response_queue = e3_response_queue_create();
  if (!response_queue) {
    LOG_E(E3AP, "Failed to create response queue\n");
    abort();
  }

  pub_sub_args_t *shared_args = malloc(sizeof(pub_sub_args_t));
  shared_args->e3_configs = e3_configs;
  shared_args->connector = e3connector;
  shared_args->response_queue = response_queue;

  sm_data_args_t *sm_args = malloc(sizeof(sm_data_args_t));
  sm_args->response_queue = response_queue;

  LOG_D(E3AP, "Create sub_thread\n");
  pthread_create(&sub_thread, NULL, subscriber_thread, (void *)shared_args);

  LOG_D(E3AP, "Create pub_thread\n");
  pthread_create(&pub_thread, NULL, publisher_thread, (void *)shared_args);

  LOG_D(E3AP, "Create sm_data_thread\n");
  pthread_create(&sm_data_thread, NULL, sm_data_handler_thread, (void *)sm_args);

  LOG_D(E3AP, "Setup connection\n");
  ret = e3connector->setup_initial_connection(e3connector);
  if (ret < 0) {
    LOG_E(E3AP, "Bind in setup initial connection failed: %s\n", strerror(errno));
    abort();
  }

  LOG_D(E3AP, "Start setup loop\n");
  while (1) {
    ret = e3connector->recv_setup_request(e3connector, buffer, buffer_size);
    
    // Create encoded message wrapper for received data
    E3EncodedMessage received_msg = {
        .buffer = buffer,
        .size = ret,
#ifdef E3_JSON_FORMAT
        .format = FORMAT_JSON
#else
        .format = FORMAT_ASN1
#endif
    };
    
    e3ap_pdu_t *setupRequest = e3_decode_message(&received_msg);

    if (setupRequest && setupRequest->pdu_type == E3AP_PDU_TYPE_SETUP_REQUEST) {
      uint32_t dapp_id = setupRequest->choice.setup_request.dapp_identifier;
      LOG_I(E3AP, "Received setup request from dApp ID: %u\n", dapp_id);
      
      // Register dApp in subscription manager
      int reg_result = e3_subscription_manager_register_dapp(e3_subscription_manager, dapp_id);
      e3ap_response_code_t response_code;
      if(reg_result == E3_SM_ERROR_ALREADY_EXISTS)
      {
        // This behavior may change in the future
        response_code = E3AP_RESPONSE_CODE_POSITIVE;
        LOG_W(E3AP, "dApp %u already registered, sending positive ack \n", dapp_id);
      } else if (reg_result != E3_SM_SUCCESS) 
      {
        response_code = E3AP_RESPONSE_CODE_NEGATIVE;
        LOG_E(E3AP, "Failed to register dApp %u: %d\n", dapp_id, reg_result);
      } else {
        response_code = E3AP_RESPONSE_CODE_POSITIVE;
        LOG_I(E3AP, "dApp %u registered successfully\n", dapp_id);
      }

      e3ap_pdu_t *setupResponse = e3ap_create_setup_response(
          setupRequest->choice.setup_request.id,  // Original request ID
          response_code,                          // Response code
          available_ran_functions,                // Available RAN functions
          ran_function_count                      // RAN function count
      );
      
      if (setupResponse) {
        // Encode response using compile-time selected format
        E3EncodedMessage *encoded_response = e3_encode_pdu(setupResponse);
        if (encoded_response) {
          e3connector->send_response(e3connector, encoded_response->buffer, encoded_response->size);
          e3_free_encoded_message(encoded_response);
          LOG_I(E3AP, "Sent setup response\n");
        } else {
          LOG_E(E3AP, "Failed to encode setup response PDU\n");
        }
        e3ap_pdu_free(setupResponse);
      } else {
        LOG_E(E3AP, "Failed to create setup response PDU\n");
      }
    } else {
      LOG_E(E3AP, "Failed to decode setup request or unexpected PDU type, ignored\n");
    }

    if (setupRequest) {
      e3ap_pdu_free(setupRequest);
    }
    buffer_size = BUFFER_SIZE; // reset the variable
  }

  pthread_join(sub_thread, NULL);
  pthread_join(pub_thread, NULL);
  pthread_join(sm_data_thread, NULL);

  free(buffer);
  
  if (response_queue) {
   e3_response_queue_destroy(response_queue); 
  }
  free(shared_args);
  free(sm_args);


  if (available_ran_functions) {
      free(available_ran_functions);
  }

  e3connector->dispose(e3connector);

  return NULL;
}