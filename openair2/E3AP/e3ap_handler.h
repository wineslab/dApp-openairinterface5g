#ifndef E3AP_HANDLER_H
#define E3AP_HANDLER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "common/utils/LOG/log.h"
#include "e3ap_types.h"

#include <string.h>

#ifdef E3_ASN1_FORMAT
    #include "E3-PDU.h"
    #include "E3-SetupRequest.h"
    #include "E3-SetupResponse.h"
    #include "E3-SubscriptionRequest.h"
    #include "E3-SubscriptionResponse.h"
    #include "E3-ControlAction.h"
    #include "E3-IndicationMessage.h"
#endif

#ifdef E3_JSON_FORMAT
    #include <json-c/json.h>
#endif

/**
 * Unified encoding/decoding functions with compile-time format selection
 * Format determined by E3_ASN1_FORMAT or E3_JSON_FORMAT compile definitions
 */

/**
 * Encode an E3AP PDU using the compile-time selected format
 * @param pdu Generic E3AP PDU structure
 * @return Pointer to encoded message, NULL on error (caller must free with e3_free_encoded_message)
 */
E3EncodedMessage* e3_encode_pdu(const e3ap_pdu_t *pdu);

/**
 * Decode an encoded message to generic E3AP PDU structure
 * @param encoded_msg Encoded message structure
 * @return Pointer to decoded PDU, NULL on error (caller must free with e3ap_pdu_free)
 */
e3ap_pdu_t* e3_decode_message(const E3EncodedMessage *encoded_msg);

/**
 * Free an encoded message structure
 * @param encoded_msg Encoded message to free
 */
void e3_free_encoded_message(E3EncodedMessage *encoded_msg);

/**
 * Parse control action from encoded message
 * @param encoded_msg Encoded control action message
 * @param action_data Output buffer for action data
 * @param action_data_size Output size of action data
 * @return 0 on success, -1 on error
 */
int e3_parse_control_action(const E3EncodedMessage *encoded_msg, uint8_t **action_data, size_t *action_data_size);

/**
 * Parse setup response from encoded message
 * @param encoded_msg Encoded setup response message
 * @return Response code, -1 on error
 */
long e3_parse_setup_response(const E3EncodedMessage *encoded_msg);

#endif // E3AP_HANDLER_H