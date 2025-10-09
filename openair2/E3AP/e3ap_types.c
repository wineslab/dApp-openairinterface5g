/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

#include "e3ap_types.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>

// Internal message ID counter (thread-safe)
static atomic_uint_fast32_t next_message_id = ATOMIC_VAR_INIT(1);

/**
 * Generate next message ID (1-100, wrapping around)
 * @return Next message ID
 */
static uint32_t generate_message_id(void)
{
    uint32_t id = atomic_fetch_add(&next_message_id, 1);
    // Wrap around to stay within valid range (1-100)
    if (id > 100) {
        id = (id % 100) + 1;
        atomic_store(&next_message_id, id + 1);
    }
    return id;
}

e3ap_pdu_t* e3ap_create_setup_request(uint32_t dapp_identifier, 
                                      const uint32_t *ran_function_list, 
                                      uint32_t ran_function_count, 
                                      e3ap_action_type_t action_type)
{
    /* Validate input parameters */
    if (dapp_identifier > 100) return NULL;
    if (ran_function_count > E3AP_MAX_RAN_FUNCTIONS) return NULL;
    if (ran_function_count > 0 && ran_function_list == NULL) return NULL;
    
    /* Allocate and initialize PDU */
    e3ap_pdu_t *pdu = calloc(1, sizeof(e3ap_pdu_t));
    if (pdu == NULL) return NULL;
    
    pdu->pdu_type = E3AP_PDU_TYPE_SETUP_REQUEST;
    pdu->choice.setup_request.id = generate_message_id();
    pdu->choice.setup_request.dapp_identifier = dapp_identifier;
    pdu->choice.setup_request.type = action_type;
    pdu->choice.setup_request.ran_function_count = ran_function_count;
    
    /* Copy RAN function list */
    for (uint32_t i = 0; i < ran_function_count; i++) {
        if (ran_function_list[i] > 255) {
            free(pdu);
            return NULL;
        }
        pdu->choice.setup_request.ran_function_list[i] = ran_function_list[i];
    }
    
    return pdu;
}

e3ap_pdu_t* e3ap_create_setup_response(uint32_t request_id,
                                       e3ap_response_code_t response_code,
                                       const uint32_t *ran_function_list,
                                       uint32_t ran_function_count)
{
    /* Validate input parameters */
    if (request_id == 0 || request_id > 100) return NULL;
    if (ran_function_count > E3AP_MAX_RAN_FUNCTIONS) return NULL;
    if (ran_function_count > 0 && ran_function_list == NULL) return NULL;
    
    /* Allocate and initialize PDU */
    e3ap_pdu_t *pdu = calloc(1, sizeof(e3ap_pdu_t));
    if (pdu == NULL) return NULL;
    
    pdu->pdu_type = E3AP_PDU_TYPE_SETUP_RESPONSE;
    pdu->choice.setup_response.id = generate_message_id();
    pdu->choice.setup_response.request_id = request_id;
    pdu->choice.setup_response.response_code = response_code;
    pdu->choice.setup_response.ran_function_count = ran_function_count;
    
    /* Copy RAN function list */
    for (uint32_t i = 0; i < ran_function_count; i++) {
        if (ran_function_list[i] > 255) {
            free(pdu);
            return NULL;
        }
        pdu->choice.setup_response.ran_function_list[i] = ran_function_list[i];
    }
    
    return pdu;
}

e3ap_pdu_t* e3ap_create_subscription_request(uint32_t dapp_identifier,
                                             e3ap_action_type_t action_type,
                                             uint32_t ran_function_identifier)
{
    /* Validate input parameters */
    if (dapp_identifier > 100) return NULL;
    if (ran_function_identifier > 100) return NULL;
    
    /* Allocate and initialize PDU */
    e3ap_pdu_t *pdu = calloc(1, sizeof(e3ap_pdu_t));
    if (pdu == NULL) return NULL;
    
    pdu->pdu_type = E3AP_PDU_TYPE_SUBSCRIPTION_REQUEST;
    pdu->choice.subscription_request.id = generate_message_id();
    pdu->choice.subscription_request.dapp_identifier = dapp_identifier;
    pdu->choice.subscription_request.type = action_type;
    pdu->choice.subscription_request.ran_function_identifier = ran_function_identifier;
    
    return pdu;
}

e3ap_pdu_t* e3ap_create_subscription_response(uint32_t request_id,
                                              e3ap_response_code_t response_code)
{
    /* Validate input parameters */
    if (request_id == 0 || request_id > 100) return NULL;
    
    /* Allocate and initialize PDU */
    e3ap_pdu_t *pdu = calloc(1, sizeof(e3ap_pdu_t));
    if (pdu == NULL) return NULL;
    
    pdu->pdu_type = E3AP_PDU_TYPE_SUBSCRIPTION_RESPONSE;
    pdu->choice.subscription_response.id = generate_message_id();
    pdu->choice.subscription_response.request_id = request_id;
    pdu->choice.subscription_response.response_code = response_code;
    
    return pdu;
}

e3ap_pdu_t* e3ap_create_indication_message(uint32_t dapp_identifier,
                                           const uint8_t *protocol_data,
                                           size_t protocol_data_size)
{
    /* Validate input parameters */
    if (dapp_identifier > 100) return NULL;
    if (protocol_data == NULL || protocol_data_size == 0) return NULL;
    if (protocol_data_size > E3AP_MAX_PROTOCOL_DATA_SIZE) return NULL;
    
    /* Allocate and initialize PDU */
    e3ap_pdu_t *pdu = calloc(1, sizeof(e3ap_pdu_t));
    if (pdu == NULL) return NULL;
    
    pdu->pdu_type = E3AP_PDU_TYPE_INDICATION_MESSAGE;
    pdu->choice.indication_message.id = generate_message_id();
    pdu->choice.indication_message.dapp_identifier = dapp_identifier;
    pdu->choice.indication_message.protocol_data_size = protocol_data_size;
    
    /* Copy protocol data */
    memcpy(pdu->choice.indication_message.protocol_data, protocol_data, protocol_data_size);
    
    return pdu;
}

e3ap_pdu_t* e3ap_create_control_action(uint32_t dapp_identifier,
                                       uint32_t ran_function_identifier,
                                       const uint8_t *action_data,
                                       size_t action_data_size)
{
    /* Validate input parameters */
    if (dapp_identifier > 100) return NULL;
    if (ran_function_identifier > 100) return NULL;
    if (action_data == NULL || action_data_size == 0) return NULL;
    if (action_data_size > E3AP_MAX_ACTION_DATA_SIZE) return NULL;
    
    /* Allocate and initialize PDU */
    e3ap_pdu_t *pdu = calloc(1, sizeof(e3ap_pdu_t));
    if (pdu == NULL) return NULL;
    
    pdu->pdu_type = E3AP_PDU_TYPE_CONTROL_ACTION;
    pdu->choice.control_action.id = generate_message_id();
    pdu->choice.control_action.dapp_identifier = dapp_identifier;
    pdu->choice.control_action.ran_function_identifier = ran_function_identifier;
    pdu->choice.control_action.action_data_size = action_data_size;
    
    /* Copy action data */
    memcpy(pdu->choice.control_action.action_data, action_data, action_data_size);
    
    return pdu;
}

e3ap_pdu_t* e3ap_create_message_ack(uint32_t request_id,
                                    e3ap_response_code_t response_code)
{
    /* Validate input parameters */
    if (request_id == 0 || request_id > 100) return NULL;
    
    /* Allocate and initialize PDU */
    e3ap_pdu_t *pdu = calloc(1, sizeof(e3ap_pdu_t));
    if (pdu == NULL) return NULL;
    
    pdu->pdu_type = E3AP_PDU_TYPE_MESSAGE_ACK;
    pdu->choice.message_ack.id = generate_message_id();
    pdu->choice.message_ack.request_id = request_id;
    pdu->choice.message_ack.response_code = response_code;
    
    return pdu;
}

void e3ap_pdu_free(e3ap_pdu_t *pdu)
{
    if (pdu == NULL) {
        return;
    }
    
    /* Clear the PDU structure and free memory */
    memset(pdu, 0, sizeof(e3ap_pdu_t));
    free(pdu);
}

size_t e3ap_pdu_get_size(e3ap_pdu_type_t pdu_type)
{
    switch (pdu_type) {
        case E3AP_PDU_TYPE_SETUP_REQUEST:
            return sizeof(e3ap_setup_request_t);
        case E3AP_PDU_TYPE_SETUP_RESPONSE:
            return sizeof(e3ap_setup_response_t);
        case E3AP_PDU_TYPE_SUBSCRIPTION_REQUEST:
            return sizeof(e3ap_subscription_request_t);
        case E3AP_PDU_TYPE_SUBSCRIPTION_RESPONSE:
            return sizeof(e3ap_subscription_response_t);
        case E3AP_PDU_TYPE_INDICATION_MESSAGE:
            return sizeof(e3ap_indication_message_t);
        case E3AP_PDU_TYPE_CONTROL_ACTION:
            return sizeof(e3ap_control_action_t);
        case E3AP_PDU_TYPE_MESSAGE_ACK:
            return sizeof(e3ap_message_ack_t);
        default:
            return 0;
    }
}

const char* e3ap_pdu_type_to_string(e3ap_pdu_type_t pdu_type)
{
    switch (pdu_type) {
        case E3AP_PDU_TYPE_SETUP_REQUEST:
            return "SetupRequest";
        case E3AP_PDU_TYPE_SETUP_RESPONSE:
            return "SetupResponse";
        case E3AP_PDU_TYPE_SUBSCRIPTION_REQUEST:
            return "SubscriptionRequest";
        case E3AP_PDU_TYPE_SUBSCRIPTION_RESPONSE:
            return "SubscriptionResponse";
        case E3AP_PDU_TYPE_INDICATION_MESSAGE:
            return "IndicationMessage";
        case E3AP_PDU_TYPE_CONTROL_ACTION:
            return "ControlAction";
        case E3AP_PDU_TYPE_MESSAGE_ACK:
            return "MessageAck";
        default:
            return "Unknown";
    }
}

const char* e3ap_action_type_to_string(e3ap_action_type_t action_type)
{
    switch (action_type) {
        case E3AP_ACTION_TYPE_INSERT:
            return "Insert";
        case E3AP_ACTION_TYPE_UPDATE:
            return "Update";
        case E3AP_ACTION_TYPE_DELETE:
            return "Delete";
        default:
            return "Unknown";
    }
}

const char* e3ap_response_code_to_string(e3ap_response_code_t response_code)
{
    switch (response_code) {
        case E3AP_RESPONSE_CODE_POSITIVE:
            return "Positive";
        case E3AP_RESPONSE_CODE_NEGATIVE:
            return "Negative";
        default:
            return "Unknown";
    }
}

int e3ap_pdu_validate(const e3ap_pdu_t *pdu)
{
    if (pdu == NULL) {
        return -1;
    }
    
    /* Validate PDU type */
    if (pdu->pdu_type < E3AP_PDU_TYPE_SETUP_REQUEST || 
        pdu->pdu_type > E3AP_PDU_TYPE_MESSAGE_ACK) {
        return -1;
    }
    
    /* Validate specific PDU fields */
    switch (pdu->pdu_type) {
        case E3AP_PDU_TYPE_SETUP_REQUEST:
            if (pdu->choice.setup_request.id == 0 || 
                pdu->choice.setup_request.id > 100) {
                return -1;
            }
            if (pdu->choice.setup_request.dapp_identifier > 100) {
                return -1;
            }
            if (pdu->choice.setup_request.ran_function_count > E3AP_MAX_RAN_FUNCTIONS) {
                return -1;
            }
            break;
            
        case E3AP_PDU_TYPE_SETUP_RESPONSE:
            if (pdu->choice.setup_response.id == 0 || 
                pdu->choice.setup_response.id > 100) {
                return -1;
            }
            if (pdu->choice.setup_response.request_id == 0 || 
                pdu->choice.setup_response.request_id > 100) {
                return -1;
            }
            if (pdu->choice.setup_response.ran_function_count > E3AP_MAX_RAN_FUNCTIONS) {
                return -1;
            }
            break;
            
        case E3AP_PDU_TYPE_SUBSCRIPTION_REQUEST:
            if (pdu->choice.subscription_request.id == 0 || 
                pdu->choice.subscription_request.id > 100) {
                return -1;
            }
            if (pdu->choice.subscription_request.ran_function_identifier > 100) {
                return -1;
            }
            break;
            
        case E3AP_PDU_TYPE_SUBSCRIPTION_RESPONSE:
            if (pdu->choice.subscription_response.id == 0 || 
                pdu->choice.subscription_response.id > 100) {
                return -1;
            }
            if (pdu->choice.subscription_response.request_id == 0 || 
                pdu->choice.subscription_response.request_id > 100) {
                return -1;
            }
            break;
            
        case E3AP_PDU_TYPE_INDICATION_MESSAGE:
            if (pdu->choice.indication_message.id == 0 || 
                pdu->choice.indication_message.id > 100) {
                return -1;
            }
            if (pdu->choice.indication_message.protocol_data_size == 0 ||
                pdu->choice.indication_message.protocol_data_size > E3AP_MAX_PROTOCOL_DATA_SIZE) {
                return -1;
            }
            break;
            
        case E3AP_PDU_TYPE_CONTROL_ACTION:
            if (pdu->choice.control_action.id == 0 || 
                pdu->choice.control_action.id > 100) {
                return -1;
            }
            if (pdu->choice.control_action.action_data_size == 0 ||
                pdu->choice.control_action.action_data_size > E3AP_MAX_ACTION_DATA_SIZE) {
                return -1;
            }
            break;
            
        case E3AP_PDU_TYPE_MESSAGE_ACK:
            if (pdu->choice.message_ack.id == 0 || 
                pdu->choice.message_ack.id > 100) {
                return -1;
            }
            if (pdu->choice.message_ack.request_id == 0 || 
                pdu->choice.message_ack.request_id > 100) {
                return -1;
            }
            break;
            
        default:
            return -1;
    }
    
    return 0;
}

int e3ap_pdu_copy(e3ap_pdu_t *dest, const e3ap_pdu_t *src)
{
    if (dest == NULL || src == NULL) {
        return -1;
    }
    
    /* Validate source PDU */
    if (e3ap_pdu_validate(src) != 0) {
        return -1;
    }
    
    /* Copy the entire structure */
    memcpy(dest, src, sizeof(e3ap_pdu_t));
    
    return 0;
}