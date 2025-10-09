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

#ifndef E3AP_TYPES_H_
#define E3AP_TYPES_H_

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/**
 * E3AP Encoding Formats
 */
typedef enum {
    FORMAT_ASN1,
    FORMAT_JSON
} E3EncodingFormat;

/**
 * E3AP Encoded Message Structure
 * Unified wrapper for encoded data regardless of format
 */
typedef struct {
    void* buffer;           // Pointer to encoded data
    size_t size;            // Size of encoded data
    E3EncodingFormat format; // Encoding format used
} E3EncodedMessage;

/* Maximum sizes for E3AP data fields */
#define E3AP_MAX_PROTOCOL_DATA_SIZE   32768
#define E3AP_MAX_ACTION_DATA_SIZE     32768
#define E3AP_MAX_RAN_FUNCTIONS        255

/**
 * E3AP Action Types
 */
typedef enum {
    E3AP_ACTION_TYPE_INSERT = 0,
    E3AP_ACTION_TYPE_UPDATE = 1,
    E3AP_ACTION_TYPE_DELETE = 2
} e3ap_action_type_t;

/**
 * E3AP Response Codes
 */
typedef enum {
    E3AP_RESPONSE_CODE_POSITIVE = 0,
    E3AP_RESPONSE_CODE_NEGATIVE = 1
} e3ap_response_code_t;

/**
 * E3AP PDU Types
 */
typedef enum {
    E3AP_PDU_TYPE_SETUP_REQUEST = 0,
    E3AP_PDU_TYPE_SETUP_RESPONSE = 1,
    E3AP_PDU_TYPE_SUBSCRIPTION_REQUEST = 2,
    E3AP_PDU_TYPE_SUBSCRIPTION_RESPONSE = 3,
    E3AP_PDU_TYPE_INDICATION_MESSAGE = 4,
    E3AP_PDU_TYPE_CONTROL_ACTION = 5,
    E3AP_PDU_TYPE_MESSAGE_ACK = 6
} e3ap_pdu_type_t;

/**
 * E3AP Setup Request Structure
 */
typedef struct {
    uint32_t id;
    uint32_t dapp_identifier;
    uint32_t ran_function_list[E3AP_MAX_RAN_FUNCTIONS];
    uint32_t ran_function_count;
    e3ap_action_type_t type;
} e3ap_setup_request_t;

/**
 * E3AP Setup Response Structure
 */
typedef struct {
    uint32_t id;
    uint32_t request_id;
    e3ap_response_code_t response_code;
    uint32_t ran_function_list[E3AP_MAX_RAN_FUNCTIONS];
    uint32_t ran_function_count;
} e3ap_setup_response_t;

/**
 * E3AP Subscription Request Structure
 */
typedef struct {
    uint32_t id;
    uint32_t dapp_identifier;
    e3ap_action_type_t type;
    uint32_t ran_function_identifier;
} e3ap_subscription_request_t;

/**
 * E3AP Subscription Response Structure
 */
typedef struct {
    uint32_t id;
    uint32_t request_id;
    e3ap_response_code_t response_code;
} e3ap_subscription_response_t;

/**
 * E3AP Indication Message Structure
 */
typedef struct {
    uint32_t id;
    uint32_t dapp_identifier;
    uint8_t protocol_data[E3AP_MAX_PROTOCOL_DATA_SIZE];
    size_t protocol_data_size;
} e3ap_indication_message_t;

/**
 * E3AP Control Action Structure
 */
typedef struct {
    uint32_t id;
    uint32_t dapp_identifier;
    uint32_t ran_function_identifier;
    uint8_t action_data[E3AP_MAX_ACTION_DATA_SIZE];
    size_t action_data_size;
} e3ap_control_action_t;

/**
 * E3AP Message Acknowledgment Structure
 */
typedef struct {
    uint32_t id;
    uint32_t request_id;
    e3ap_response_code_t response_code;
} e3ap_message_ack_t;

/**
 * Generic E3AP PDU Structure
 * This structure holds any E3AP PDU regardless of encoding used
 */
typedef struct {
    e3ap_pdu_type_t pdu_type;
    union {
        e3ap_setup_request_t setup_request;
        e3ap_setup_response_t setup_response;
        e3ap_subscription_request_t subscription_request;
        e3ap_subscription_response_t subscription_response;
        e3ap_indication_message_t indication_message;
        e3ap_control_action_t control_action;
        e3ap_message_ack_t message_ack;
    } choice;
} e3ap_pdu_t;

/**
 * Function prototypes for E3AP PDU creation and manipulation
 */

/**
 * Create an E3AP Setup Request PDU
 * @param dapp_identifier dApp identifier (0-100)
 * @param ran_function_list Array of RAN function IDs (0-255)
 * @param ran_function_count Number of RAN functions in the list
 * @param action_type Type of action (insert/update/delete)
 * @return Pointer to created PDU, NULL on error (caller must free with e3ap_pdu_free)
 * @note Message ID is automatically generated internally
 */
e3ap_pdu_t* e3ap_create_setup_request(uint32_t dapp_identifier, 
                                      const uint32_t *ran_function_list, 
                                      uint32_t ran_function_count, 
                                      e3ap_action_type_t action_type);

/**
 * Create an E3AP Setup Response PDU
 * @param request_id Original request ID (1-100)
 * @param response_code Response code (positive/negative)
 * @param ran_function_list Array of RAN function IDs (0-255)
 * @param ran_function_count Number of RAN functions in the list
 * @return Pointer to created PDU, NULL on error (caller must free with e3ap_pdu_free)
 * @note Message ID is automatically generated internally
 */
e3ap_pdu_t* e3ap_create_setup_response(uint32_t request_id,
                                       e3ap_response_code_t response_code,
                                       const uint32_t *ran_function_list,
                                       uint32_t ran_function_count);

/**
 * Create an E3AP Subscription Request PDU
 * @param dapp_identifier dApp identifier (0-100)
 * @param action_type Type of action (insert/update/delete)
 * @param ran_function_identifier RAN function identifier (0-100)
 * @return Pointer to created PDU, NULL on error (caller must free with e3ap_pdu_free)
 * @note Message ID is automatically generated internally
 */
e3ap_pdu_t* e3ap_create_subscription_request(uint32_t dapp_identifier,
                                             e3ap_action_type_t action_type,
                                             uint32_t ran_function_identifier);

/**
 * Create an E3AP Subscription Response PDU
 * @param request_id Original request ID (1-100)
 * @param response_code Response code (positive/negative)
 * @return Pointer to created PDU, NULL on error (caller must free with e3ap_pdu_free)
 * @note Message ID is automatically generated internally
 */
e3ap_pdu_t* e3ap_create_subscription_response(uint32_t request_id,
                                              e3ap_response_code_t response_code);

/**
 * Create an E3AP Indication Message PDU
 * @param dapp_identifier dApp identifier (0-100)
 * @param protocol_data Pointer to protocol data
 * @param protocol_data_size Size of protocol data (1-16384 bytes)
 * @return Pointer to created PDU, NULL on error (caller must free with e3ap_pdu_free)
 * @note Message ID is automatically generated internally
 */
e3ap_pdu_t* e3ap_create_indication_message(uint32_t dapp_identifier,
                                           const uint8_t *protocol_data,
                                           size_t protocol_data_size);

/**
 * Create an E3AP Control Action PDU
 * @param dapp_identifier dApp identifier (0-100)
 * @param ran_function_identifier RAN function identifier (0-100)
 * @param action_data Pointer to action data
 * @param action_data_size Size of action data (1-16384 bytes)
 * @return Pointer to created PDU, NULL on error (caller must free with e3ap_pdu_free)
 * @note Message ID is automatically generated internally
 */
e3ap_pdu_t* e3ap_create_control_action(uint32_t dapp_identifier,
                                       uint32_t ran_function_identifier,
                                       const uint8_t *action_data,
                                       size_t action_data_size);

/**
 * Create an E3AP Message Acknowledgment PDU
 * @param request_id Original request ID (1-100)
 * @param response_code Response code (positive/negative)
 * @return Pointer to created PDU, NULL on error (caller must free with e3ap_pdu_free)
 * @note Message ID is automatically generated internally
 */
e3ap_pdu_t* e3ap_create_message_ack(uint32_t request_id,
                                    e3ap_response_code_t response_code);

/**
 * Free resources allocated for an E3AP PDU
 * @param pdu Pointer to the PDU structure to free
 */
void e3ap_pdu_free(e3ap_pdu_t *pdu);

/**
 * Validate an E3AP PDU structure
 * @param pdu Pointer to the PDU structure to validate
 * @return 0 if valid, -1 if invalid
 */
int e3ap_pdu_validate(const e3ap_pdu_t *pdu);

/**
 * Copy an E3AP PDU structure
 * @param dest Destination PDU structure
 * @param src Source PDU structure
 * @return 0 on success, -1 on error
 */
int e3ap_pdu_copy(e3ap_pdu_t *dest, const e3ap_pdu_t *src);

/**
 * Get the size of a specific PDU type
 * @param pdu_type Type of PDU
 * @return Size in bytes, 0 if invalid type
 */
size_t e3ap_pdu_get_size(e3ap_pdu_type_t pdu_type);

/**
 * Convert PDU type to string representation
 * @param pdu_type Type of PDU
 * @return String representation of the PDU type
 */
const char* e3ap_pdu_type_to_string(e3ap_pdu_type_t pdu_type);

/**
 * Convert action type to string representation
 * @param action_type Action type
 * @return String representation of the action type
 */
const char* e3ap_action_type_to_string(e3ap_action_type_t action_type);

/**
 * Convert response code to string representation
 * @param response_code Response code
 * @return String representation of the response code
 */
const char* e3ap_response_code_to_string(e3ap_response_code_t response_code);

#endif /* E3AP_TYPES_H_ */