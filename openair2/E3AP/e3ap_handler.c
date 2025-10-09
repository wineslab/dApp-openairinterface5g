# include "e3ap_handler.h"

#define BUFFER_SIZE 60000

/**
 * Unified encoding/decoding functions with compile-time format selection
 */

#ifdef E3_JSON_FORMAT
// JSON encoding implementation
// Helper function to encode PDU to JSON
static E3EncodedMessage* encode_pdu_to_json(const e3ap_pdu_t *pdu) {
    json_object *root = json_object_new_object();
    json_object *pdu_type_obj = json_object_new_string(e3ap_pdu_type_to_string(pdu->pdu_type));
    json_object_object_add(root, "pdu_type", pdu_type_obj);
    
    json_object *data_obj = json_object_new_object();
    
    switch (pdu->pdu_type) {
        case E3AP_PDU_TYPE_SETUP_REQUEST: {
            json_object_object_add(data_obj, "id", json_object_new_int(pdu->choice.setup_request.id));
            json_object_object_add(data_obj, "dapp_identifier", json_object_new_int(pdu->choice.setup_request.dapp_identifier));
            json_object_object_add(data_obj, "action_type", json_object_new_string(e3ap_action_type_to_string(pdu->choice.setup_request.type)));
            
            json_object *ran_funcs_array = json_object_new_array();
            for (uint32_t i = 0; i < pdu->choice.setup_request.ran_function_count; i++) {
                json_object_array_add(ran_funcs_array, json_object_new_int(pdu->choice.setup_request.ran_function_list[i]));
            }
            json_object_object_add(data_obj, "ran_function_list", ran_funcs_array);
            break;
        }
        case E3AP_PDU_TYPE_SETUP_RESPONSE: {
            json_object_object_add(data_obj, "id", json_object_new_int(pdu->choice.setup_response.id));
            json_object_object_add(data_obj, "request_id", json_object_new_int(pdu->choice.setup_response.request_id));
            json_object_object_add(data_obj, "response_code", json_object_new_string(e3ap_response_code_to_string(pdu->choice.setup_response.response_code)));
            
            json_object *ran_funcs_array = json_object_new_array();
            for (uint32_t i = 0; i < pdu->choice.setup_response.ran_function_count; i++) {
                json_object_array_add(ran_funcs_array, json_object_new_int(pdu->choice.setup_response.ran_function_list[i]));
            }
            json_object_object_add(data_obj, "ran_function_list", ran_funcs_array);
            break;
        }
        case E3AP_PDU_TYPE_INDICATION_MESSAGE: {
            json_object_object_add(data_obj, "id", json_object_new_int(pdu->choice.indication_message.id));
            json_object_object_add(data_obj, "dapp_identifier", json_object_new_int(pdu->choice.indication_message.dapp_identifier));
            json_object_object_add(data_obj, "protocol_data", json_object_new_string_len((char*)pdu->choice.indication_message.protocol_data, pdu->choice.indication_message.protocol_data_size));
            break;
        }
        case E3AP_PDU_TYPE_CONTROL_ACTION: {
            json_object_object_add(data_obj, "id", json_object_new_int(pdu->choice.control_action.id));
            json_object_object_add(data_obj, "dapp_identifier", json_object_new_int(pdu->choice.control_action.dapp_identifier));
            json_object_object_add(data_obj, "action_data", json_object_new_string_len((char*)pdu->choice.control_action.action_data, pdu->choice.control_action.action_data_size));
            break;
        }
        default:
            json_object_put(root);
            json_object_put(data_obj);
            return NULL;
    }
    
    json_object_object_add(root, "data", data_obj);
    
    const char *json_string = json_object_to_json_string(root);
    size_t json_len = strlen(json_string);
    
    E3EncodedMessage *encoded = malloc(sizeof(E3EncodedMessage));
    if (!encoded) {
        json_object_put(root);
        return NULL;
    }
    
    encoded->buffer = malloc(json_len + 1);
    if (!encoded->buffer) {
        free(encoded);
        json_object_put(root);
        return NULL;
    }
    
    memcpy(encoded->buffer, json_string, json_len + 1);
    encoded->size = json_len;
    encoded->format = FORMAT_JSON;
    
    json_object_put(root);
    return encoded;
}

// Helper function to decode JSON to PDU
static e3ap_pdu_t* decode_json_to_pdu(const char *json_str) {
    json_object *root = json_tokener_parse(json_str);
    if (!root) {
        LOG_E(E3AP, "Failed to parse JSON string\n");
        return NULL;
    }
    
    json_object *pdu_type_obj, *data_obj;
    if (!json_object_object_get_ex(root, "pdu_type", &pdu_type_obj) ||
        !json_object_object_get_ex(root, "data", &data_obj)) {
        LOG_E(E3AP, "Missing pdu_type or data in JSON\n");
        json_object_put(root);
        return NULL;
    }
    
    const char *pdu_type_str = json_object_get_string(pdu_type_obj);
    e3ap_pdu_type_t pdu_type;
    
    // Convert string to enum
    if (strcmp(pdu_type_str, "SetupRequest") == 0) {
        pdu_type = E3AP_PDU_TYPE_SETUP_REQUEST;
    } else if (strcmp(pdu_type_str, "SetupResponse") == 0) {
        pdu_type = E3AP_PDU_TYPE_SETUP_RESPONSE;
    } else if (strcmp(pdu_type_str, "IndicationMessage") == 0) {
        pdu_type = E3AP_PDU_TYPE_INDICATION_MESSAGE;
    } else if (strcmp(pdu_type_str, "ControlAction") == 0) {
        pdu_type = E3AP_PDU_TYPE_CONTROL_ACTION;
    } else {
        LOG_E(E3AP, "Unknown PDU type: %s\n", pdu_type_str);
        json_object_put(root);
        return NULL;
    }
    
    e3ap_pdu_t *pdu = calloc(1, sizeof(e3ap_pdu_t));
    if (!pdu) {
        json_object_put(root);
        return NULL;
    }
    
    pdu->pdu_type = pdu_type;
    
    json_object *temp_obj;
    switch (pdu_type) {
        case E3AP_PDU_TYPE_SETUP_REQUEST: {
            if (json_object_object_get_ex(data_obj, "id", &temp_obj)) {
                pdu->choice.setup_request.id = json_object_get_int(temp_obj);
            }
            if (json_object_object_get_ex(data_obj, "dapp_identifier", &temp_obj)) {
                pdu->choice.setup_request.dapp_identifier = json_object_get_int(temp_obj);
            }
            break;
        }
        case E3AP_PDU_TYPE_CONTROL_ACTION: {
            if (json_object_object_get_ex(data_obj, "id", &temp_obj)) {
                pdu->choice.control_action.id = json_object_get_int(temp_obj);
            }
            if (json_object_object_get_ex(data_obj, "dapp_identifier", &temp_obj)) {
                pdu->choice.control_action.dapp_identifier = json_object_get_int(temp_obj);
            }
            if (json_object_object_get_ex(data_obj, "action_data", &temp_obj)) {
                const char *action_data_str = json_object_get_string(temp_obj);
                size_t data_len = strlen(action_data_str);
                if (data_len <= E3AP_MAX_ACTION_DATA_SIZE) {
                    memcpy(pdu->choice.control_action.action_data, action_data_str, data_len);
                    pdu->choice.control_action.action_data_size = data_len;
                }
            }
            break;
        }
        // Add other cases as needed
        default:
            break;
    }
    
    json_object_put(root);
    return pdu;
}
#endif /* E3_JSON_FORMAT */

#ifdef E3_ASN1_FORMAT
// ASN.1 encoding implementation
// Helper function to encode PDU to ASN.1
static E3EncodedMessage* encode_pdu_to_asn1(const e3ap_pdu_t *pdu) {
    // Create ASN.1 PDU directly from generic PDU
    E3_PDU_t *asn1_pdu = malloc(sizeof(E3_PDU_t));
    if (!asn1_pdu) {
        return NULL;
    }
    
    memset(asn1_pdu, 0, sizeof(E3_PDU_t));
    
    // Convert generic PDU to ASN.1 PDU
    switch (pdu->pdu_type) {
        case E3AP_PDU_TYPE_SETUP_REQUEST: { // Not sure this is actually helpful
            asn1_pdu->present = E3_PDU_PR_setupRequest;
            asn1_pdu->choice.setupRequest = calloc(1, sizeof(E3_SetupRequest_t));
            if (!asn1_pdu->choice.setupRequest) {
                free(asn1_pdu);
                return NULL;
            }
            asn1_pdu->choice.setupRequest->id = pdu->choice.setup_request.id;
            asn1_pdu->choice.setupRequest->dAppIdentifier = pdu->choice.setup_request.dapp_identifier;
            asn1_pdu->choice.setupRequest->type = pdu->choice.setup_request.type;
            
            // Handle RAN function list
            asn1_pdu->choice.setupRequest->ranFunctionList.list.count = pdu->choice.setup_request.ran_function_count;
            asn1_pdu->choice.setupRequest->ranFunctionList.list.size = pdu->choice.setup_request.ran_function_count * sizeof(long*);
            if (pdu->choice.setup_request.ran_function_count > 0) {
                asn1_pdu->choice.setupRequest->ranFunctionList.list.array = malloc(asn1_pdu->choice.setupRequest->ranFunctionList.list.size);
                if (!asn1_pdu->choice.setupRequest->ranFunctionList.list.array) {
                    free(asn1_pdu->choice.setupRequest);
                    free(asn1_pdu);
                    return NULL;
                }
                for (uint32_t i = 0; i < pdu->choice.setup_request.ran_function_count; i++) {
                    asn1_pdu->choice.setupRequest->ranFunctionList.list.array[i] = malloc(sizeof(long));
                    *asn1_pdu->choice.setupRequest->ranFunctionList.list.array[i] = pdu->choice.setup_request.ran_function_list[i];
                }
            }
            break;
        }
        case E3AP_PDU_TYPE_SETUP_RESPONSE: {
            asn1_pdu->present = E3_PDU_PR_setupResponse;
            asn1_pdu->choice.setupResponse = calloc(1, sizeof(E3_SetupResponse_t));
            if (!asn1_pdu->choice.setupResponse) {
                free(asn1_pdu);
                return NULL;
            }
            asn1_pdu->choice.setupResponse->id = pdu->choice.setup_response.id;
            asn1_pdu->choice.setupResponse->requestId = pdu->choice.setup_response.request_id;
            asn1_pdu->choice.setupResponse->responseCode = pdu->choice.setup_response.response_code;
            asn1_pdu->choice.setupResponse->ranFunctionList.list.count = pdu->choice.setup_response.ran_function_count;
            asn1_pdu->choice.setupResponse->ranFunctionList.list.size = pdu->choice.setup_response.ran_function_count * sizeof(long*);
            if (pdu->choice.setup_response.ran_function_count > 0) {
                asn1_pdu->choice.setupResponse->ranFunctionList.list.array = malloc(asn1_pdu->choice.setupResponse->ranFunctionList.list.size);
                if (!asn1_pdu->choice.setupResponse->ranFunctionList.list.array) {
                    free(asn1_pdu->choice.setupResponse);
                    free(asn1_pdu);
                    return NULL;
                }
                for (uint32_t i = 0; i < pdu->choice.setup_response.ran_function_count; i++) {
                    asn1_pdu->choice.setupResponse->ranFunctionList.list.array[i] = malloc(sizeof(long));
                    if (!asn1_pdu->choice.setupResponse->ranFunctionList.list.array[i]) {
                        // Cleanup on failure
                        for (uint32_t j = 0; j < i; j++) {
                            free(asn1_pdu->choice.setupResponse->ranFunctionList.list.array[j]);
                        }
                        free(asn1_pdu->choice.setupResponse->ranFunctionList.list.array);
                        free(asn1_pdu->choice.setupResponse);
                        free(asn1_pdu);
                        return NULL;
                    }
                    *asn1_pdu->choice.setupResponse->ranFunctionList.list.array[i] = pdu->choice.setup_response.ran_function_list[i];
                }
            } else {
                asn1_pdu->choice.setupResponse->ranFunctionList.list.array = NULL;
            }
            break;
        }
        case E3AP_PDU_TYPE_SUBSCRIPTION_RESPONSE: {
            asn1_pdu->present = E3_PDU_PR_subscriptionResponse;
            asn1_pdu->choice.subscriptionResponse = calloc(1, sizeof(E3_SubscriptionResponse_t));
            if (!asn1_pdu->choice.subscriptionResponse) {
                free(asn1_pdu);
                return NULL;
            }
            asn1_pdu->choice.subscriptionResponse->id = pdu->choice.subscription_response.id;
            asn1_pdu->choice.subscriptionResponse->requestId = pdu->choice.subscription_response.request_id;
            asn1_pdu->choice.subscriptionResponse->responseCode = pdu->choice.subscription_response.response_code;
            break;
        }
        case E3AP_PDU_TYPE_INDICATION_MESSAGE: {
            asn1_pdu->present = E3_PDU_PR_indicationMessage;
            asn1_pdu->choice.indicationMessage = calloc(1, sizeof(E3_IndicationMessage_t));
            if (!asn1_pdu->choice.indicationMessage) {
                free(asn1_pdu);
                return NULL;
            }
            asn1_pdu->choice.indicationMessage->id = pdu->choice.indication_message.id;
            asn1_pdu->choice.indicationMessage->dAppIdentifier = pdu->choice.indication_message.dapp_identifier;
            
            asn1_pdu->choice.indicationMessage->protocolData.buf = malloc(pdu->choice.indication_message.protocol_data_size);
            if (!asn1_pdu->choice.indicationMessage->protocolData.buf) {
                free(asn1_pdu->choice.indicationMessage);
                free(asn1_pdu);
                return NULL;
            }
            memcpy(asn1_pdu->choice.indicationMessage->protocolData.buf, 
                   pdu->choice.indication_message.protocol_data, 
                   pdu->choice.indication_message.protocol_data_size);
            asn1_pdu->choice.indicationMessage->protocolData.size = pdu->choice.indication_message.protocol_data_size;
            break;
        }
        case E3AP_PDU_TYPE_CONTROL_ACTION: {
            asn1_pdu->present = E3_PDU_PR_controlAction;
            asn1_pdu->choice.controlAction = calloc(1, sizeof(E3_ControlAction_t));
            if (!asn1_pdu->choice.controlAction) {
                free(asn1_pdu);
                return NULL;
            }
            asn1_pdu->choice.controlAction->id = pdu->choice.control_action.id;
            asn1_pdu->choice.controlAction->dAppIdentifier = pdu->choice.control_action.dapp_identifier;
            asn1_pdu->choice.controlAction->ranFunctionIdentifier = pdu->choice.control_action.ran_function_identifier;
            
            asn1_pdu->choice.controlAction->actionData.buf = malloc(pdu->choice.control_action.action_data_size);
            if (!asn1_pdu->choice.controlAction->actionData.buf) {
                free(asn1_pdu->choice.controlAction);
                free(asn1_pdu);
                return NULL;
            }
            memcpy(asn1_pdu->choice.controlAction->actionData.buf, 
                   pdu->choice.control_action.action_data, 
                   pdu->choice.control_action.action_data_size);
            asn1_pdu->choice.controlAction->actionData.size = pdu->choice.control_action.action_data_size;
            break;
        }
        default:
            free(asn1_pdu);
            return NULL;
    }
    
    // Encode using ASN.1 APER
    uint8_t *buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        ASN_STRUCT_FREE(asn_DEF_E3_PDU, asn1_pdu);
        return NULL;
    }
    
    asn_enc_rval_t enc_rval = aper_encode_to_buffer(&asn_DEF_E3_PDU, NULL, asn1_pdu, buffer, BUFFER_SIZE);
    if (enc_rval.encoded == -1) {
        LOG_E(E3AP, "APER encoding failed for type: %s\n", enc_rval.failed_type ? enc_rval.failed_type->name : "Unknown");
        free(buffer);
        ASN_STRUCT_FREE(asn_DEF_E3_PDU, asn1_pdu);
        return NULL;
    }
    
    E3EncodedMessage *encoded = malloc(sizeof(E3EncodedMessage));
    if (!encoded) {
        free(buffer);
        ASN_STRUCT_FREE(asn_DEF_E3_PDU, asn1_pdu);
        return NULL;
    }
    
    encoded->buffer = buffer;
    encoded->size = enc_rval.encoded;
    encoded->format = FORMAT_ASN1;
    
    ASN_STRUCT_FREE(asn_DEF_E3_PDU, asn1_pdu);
    return encoded;
}

// Helper function to decode ASN.1 to PDU
static e3ap_pdu_t* decode_asn1_to_pdu(const uint8_t *buffer, size_t buffer_size) {
    // Decode ASN.1 buffer directly
    E3_PDU_t *asn1_pdu = NULL;
    asn_dec_rval_t dec_rval = aper_decode(0, &asn_DEF_E3_PDU, (void **)&asn1_pdu, 
                                          buffer, buffer_size, 0, 0);
    if (dec_rval.code != RC_OK) {
        LOG_E(E3AP, "APER decoding failed with code %d\n", dec_rval.code);
        if (asn1_pdu) ASN_STRUCT_FREE(asn_DEF_E3_PDU, asn1_pdu);
        return NULL;
    }

    // LOG_E(E3AP, "----------------- ASN1 DECODER PRINT START----------------- \n");
    // xer_fprint(stdout, &asn_DEF_E3_PDU, asn1_pdu);
    // LOG_E(E3AP, "----------------- ASN1 DECODER PRINT END ----------------- \n");
    
    // Convert ASN.1 PDU to generic PDU
    e3ap_pdu_t *generic_pdu = NULL;
    switch (asn1_pdu->present) {
        case E3_PDU_PR_setupRequest: {
            uint32_t ran_funcs[E3AP_MAX_RAN_FUNCTIONS];
            uint32_t ran_func_count = 0;
            
            if (asn1_pdu->choice.setupRequest->ranFunctionList.list.count > 0) {
                ran_func_count = asn1_pdu->choice.setupRequest->ranFunctionList.list.count;
                if (ran_func_count > E3AP_MAX_RAN_FUNCTIONS) {
                    ran_func_count = E3AP_MAX_RAN_FUNCTIONS;
                }
                for (uint32_t i = 0; i < ran_func_count; i++) {
                    ran_funcs[i] = *asn1_pdu->choice.setupRequest->ranFunctionList.list.array[i];
                }
            }
            
            generic_pdu = e3ap_create_setup_request(
                asn1_pdu->choice.setupRequest->dAppIdentifier,
                ran_funcs,
                ran_func_count,
                (e3ap_action_type_t)asn1_pdu->choice.setupRequest->type
            );
            break;
        }

        case E3_PDU_PR_setupResponse: {
            uint32_t ran_funcs[E3AP_MAX_RAN_FUNCTIONS];
            uint32_t ran_func_count = 0;
            
            if (asn1_pdu->choice.setupResponse->ranFunctionList.list.count > 0) {
                ran_func_count = asn1_pdu->choice.setupResponse->ranFunctionList.list.count;
                if (ran_func_count > E3AP_MAX_RAN_FUNCTIONS) {
                    ran_func_count = E3AP_MAX_RAN_FUNCTIONS;
                }
                for (uint32_t i = 0; i < ran_func_count; i++) {
                    ran_funcs[i] = *asn1_pdu->choice.setupResponse->ranFunctionList.list.array[i];
                }
            }
            
            generic_pdu = e3ap_create_setup_response(
                asn1_pdu->choice.setupResponse->requestId,
                (e3ap_response_code_t)asn1_pdu->choice.setupResponse->responseCode,
                ran_funcs, ran_func_count
            );
            break;
        }

        case E3_PDU_PR_subscriptionRequest: {
            generic_pdu = e3ap_create_subscription_request(
                asn1_pdu->choice.subscriptionRequest->dAppIdentifier,
                (e3ap_action_type_t)asn1_pdu->choice.subscriptionRequest->type,
                asn1_pdu->choice.subscriptionRequest->ranFunctionIdentifier);
            break;
        }

        case E3_PDU_PR_subscriptionResponse: {
            generic_pdu = e3ap_create_subscription_response(
                asn1_pdu->choice.subscriptionResponse->requestId,
                (e3ap_response_code_t)asn1_pdu->choice.subscriptionResponse->responseCode);
            break;
        }

        case E3_PDU_PR_controlAction: {
            generic_pdu = e3ap_create_control_action(
                asn1_pdu->choice.controlAction->dAppIdentifier,
                asn1_pdu->choice.controlAction->ranFunctionIdentifier,
                asn1_pdu->choice.controlAction->actionData.buf,
                asn1_pdu->choice.controlAction->actionData.size
            );
            break;
        }
        case E3_PDU_PR_indicationMessage: {
            generic_pdu = e3ap_create_indication_message(
                asn1_pdu->choice.indicationMessage->dAppIdentifier,
                asn1_pdu->choice.indicationMessage->protocolData.buf,
                asn1_pdu->choice.indicationMessage->protocolData.size
            );
            break;
        }

        default:
            LOG_E(E3AP, "Unsupported ASN.1 PDU type: %d\n", asn1_pdu->present);
            break;
    }
    
    ASN_STRUCT_FREE(asn_DEF_E3_PDU, asn1_pdu);
    return generic_pdu;
}
#endif /* E3_ASN1_FORMAT */

E3EncodedMessage* e3_encode_pdu(const e3ap_pdu_t *pdu) {
    if (!pdu) {
        LOG_E(E3AP, "NULL PDU provided to e3_encode_pdu\n");
        return NULL;
    }
    
    if (e3ap_pdu_validate(pdu) != 0) {
        LOG_E(E3AP, "Invalid PDU provided to e3_encode_pdu\n");
        return NULL;
    }
    
    // Use compile-time format selection
#ifdef E3_JSON_FORMAT
    return encode_pdu_to_json(pdu);
#elif defined(E3_ASN1_FORMAT)
    return encode_pdu_to_asn1(pdu);
#endif
}

e3ap_pdu_t* e3_decode_message(const E3EncodedMessage *encoded_msg) {
    if (!encoded_msg || !encoded_msg->buffer || encoded_msg->size == 0) {
        LOG_E(E3AP, "Invalid encoded message provided to e3_decode_message\n");
        return NULL;
    }
    
    // Note: Format validation is implicit through compile-time selection
    
    // Use compile-time format selection
#ifdef E3_JSON_FORMAT
    return decode_json_to_pdu((const char*)encoded_msg->buffer);
#elif defined(E3_ASN1_FORMAT)
    return decode_asn1_to_pdu((uint8_t*)encoded_msg->buffer, encoded_msg->size);
#endif
}

void e3_free_encoded_message(E3EncodedMessage *encoded_msg) {
    if (!encoded_msg) {
        return;
    }
    
    if (encoded_msg->buffer) {
        free(encoded_msg->buffer);
    }
    free(encoded_msg);
}



int e3_parse_control_action(const E3EncodedMessage *encoded_msg, uint8_t **action_data, size_t *action_data_size) {
    if (!encoded_msg || !action_data || !action_data_size) {
        return -1;
    }
    
    e3ap_pdu_t *pdu = e3_decode_message(encoded_msg);
    if (!pdu || pdu->pdu_type != E3AP_PDU_TYPE_CONTROL_ACTION) {
        if (pdu) e3ap_pdu_free(pdu);
        return -1;
    }
    
    *action_data_size = pdu->choice.control_action.action_data_size;
    *action_data = malloc(*action_data_size);
    if (!*action_data) {
        e3ap_pdu_free(pdu);
        return -1;
    }
    
    memcpy(*action_data, pdu->choice.control_action.action_data, *action_data_size);
    e3ap_pdu_free(pdu);
    return 0;
}

long e3_parse_setup_response(const E3EncodedMessage *encoded_msg) {
    if (!encoded_msg) {
        return -1;
    }
    
    e3ap_pdu_t *pdu = e3_decode_message(encoded_msg);
    if (!pdu || pdu->pdu_type != E3AP_PDU_TYPE_SETUP_RESPONSE) {
        if (pdu) e3ap_pdu_free(pdu);
        return -1;
    }
    
    long response_code = pdu->choice.setup_response.response_code;
    e3ap_pdu_free(pdu);
    return response_code;
}