# include "e3ap_handler.h"

// Function to encode an E3 PDU
int encode_E3_PDU(E3_PDU_t *pdu, uint8_t **buffer, size_t *buffer_size) {
    if (pdu->present == E3_PDU_PR_setupRequest) {
        LOG_D(E3AP, "Encoding setupRequest: ranIdentifier = %ld\n", pdu->choice.setupRequest->ranIdentifier);
        LOG_D(E3AP, "ranFunctionsList count = %d\n", pdu->choice.setupRequest->ranFunctionsList.list.count);
        for (size_t i = 0; i < pdu->choice.setupRequest->ranFunctionsList.list.count; i++) {
            LOG_I(E3AP, "ranFunction[%zu] = %ld\n", i, *pdu->choice.setupRequest->ranFunctionsList.list.array[i]);
        }
    }
    else if (pdu->present == E3_PDU_PR_indicationMessage) {
        LOG_D(E3AP, "Encoding indicationMessage: protocolData size = %ld\n", pdu->choice.indicationMessage->protocolData.size);
    } else if (pdu->present == E3_PDU_PR_setupResponse) {
      LOG_D(E3AP, "Encoding setupResponse:\n");
      LOG_D(E3AP, "Response code: %ld\n", pdu->choice.setupResponse->responseCode);
    } else {
      LOG_E(E3AP, "Unexpected PDU choice\n");
      return -1;
    }

    asn_enc_rval_t enc_rval = aper_encode_to_buffer(&asn_DEF_E3_PDU, NULL, pdu, *buffer, *buffer_size);
    if (enc_rval.encoded == -1) {
        LOG_E(E3AP, "APER encoding failed for type: %s\n", enc_rval.failed_type ? enc_rval.failed_type->name : "Unknown");
        return -1;
    }

    *buffer_size = enc_rval.encoded;
    return 0;
}


// Function to decode an E3 PDU
E3_PDU_t *decode_E3_PDU(uint8_t *buffer, size_t buffer_size) {

    // Ensure buffer size is reasonable
    if (buffer_size == 0) {
        LOG_E(E3AP, "Buffer size is 0, nothing to decode.\n");
        return NULL;
    }

    // Initialize PDU structure pointer to NULL
    E3_PDU_t *pdu = NULL;

    // Decode the buffer into the PDU structure
    asn_dec_rval_t dec_rval = aper_decode(0, &asn_DEF_E3_PDU, (void **)&pdu, buffer, buffer_size, 0, 0);
    if (dec_rval.code != RC_OK) {
        LOG_E(E3AP, "APER decoding failed with code %d\n", dec_rval.code);
        ASN_STRUCT_FREE(asn_DEF_E3_PDU, pdu); // Free if partially allocated
        return NULL;
    }

    return pdu;
}

long parse_setup_response(E3_SetupResponse_t *response){
    LOG_I(E3AP, "Parsing setupResponse: responseCode = %ld\n", response->responseCode);
    if (response->responseCode == 0)
    {
        LOG_I(E3AP, "Response is positive.\n");
    }
    else if (response->responseCode == 1)
    {
        LOG_I(E3AP, "Response is negative.\n");
    }
    else
    {
        LOG_E(E3AP, "Unknown setup response code.\n");
    }
    return response->responseCode;
}


uint8_t* parse_control_action(E3_ControlAction_t *controlAction){
    LOG_D(E3AP, "Parsing Control Action\n");
    size_t actionDataSize = controlAction->actionData.size;
    uint8_t *actionData = (uint8_t *) calloc(actionDataSize, sizeof(uint8_t));
    for (int i = 0; i < actionDataSize;i++)
        actionData[i] = controlAction->actionData.buf[i];
    return actionData;
}

// Function to create an E3 Setup Response PDU
E3_PDU_t* create_setup_response(long responseCode) {
    E3_PDU_t *pdu = malloc(sizeof(E3_PDU_t));
    if (!pdu) {
        LOG_E(E3AP, "Failed to allocate memory for E3_PDU_t\n");
        return NULL;
    }

    memset(pdu, 0, sizeof(E3_PDU_t));
    pdu->present = E3_PDU_PR_setupResponse;

    pdu->choice.setupResponse = calloc(1, sizeof(E3_SetupResponse_t));
    if (!pdu->choice.setupResponse) {
        LOG_E(E3AP, "Failed to allocate memory for E3_SetupResponse_t\n");
        free(pdu);
        return NULL;
    }

    pdu->choice.setupResponse->responseCode = responseCode;

    return pdu;
}

// Function to create an E3 Setup Request PDU
E3_PDU_t* create_setup_request(int ranIdentifier, long *ranFunctions, size_t ranFunctionsCount) {
    E3_PDU_t *pdu = malloc(sizeof(E3_PDU_t));
    if (!pdu) {
        LOG_E(E3AP, "Failed to allocate memory for E3_PDU_t\n");
        return NULL;
    }

    memset(pdu, 0, sizeof(E3_PDU_t));
    pdu->present = E3_PDU_PR_setupRequest;

    pdu->choice.setupRequest = calloc(1, sizeof(E3_SetupRequest_t));
    if (!pdu->choice.setupRequest) {
        LOG_E(E3AP, "Failed to allocate memory for E3_SetupRequest_t\n");
        free(pdu);
        return NULL;
    }

    pdu->choice.setupRequest->ranIdentifier = ranIdentifier;

    pdu->choice.setupRequest->ranFunctionsList.list.count = ranFunctionsCount;
    pdu->choice.setupRequest->ranFunctionsList.list.size = ranFunctionsCount * sizeof(long);
    pdu->choice.setupRequest->ranFunctionsList.list.array = malloc(pdu->choice.setupRequest->ranFunctionsList.list.size);

    if (!pdu->choice.setupRequest->ranFunctionsList.list.array) {
        LOG_E(E3AP, "Failed to allocate memory for ranFunctionsList array\n");
        free(pdu->choice.setupRequest);
        free(pdu);
        return NULL;
    }

    for (size_t i = 0; i < ranFunctionsCount; i++) {
        pdu->choice.setupRequest->ranFunctionsList.list.array[i] = malloc(sizeof(long));
        *pdu->choice.setupRequest->ranFunctionsList.list.array[i] = ranFunctions[i];
    }

    return pdu;
}


E3_PDU_t* create_indication_message(const int32_t *payload, size_t payload_length) {
    E3_PDU_t *pdu = malloc(sizeof(E3_PDU_t));
    if (!pdu) {
        LOG_E(E3AP, "Failed to allocate memory for E3_PDU");
        return NULL;
    }

    memset(pdu, 0, sizeof(E3_PDU_t));
    pdu->present = E3_PDU_PR_indicationMessage;
    pdu->choice.indicationMessage = calloc(1, sizeof(E3_IndicationMessage_t));
    if (!pdu->choice.indicationMessage) {
        LOG_E(E3AP, "Failed to allocate memory for E3_IndicationMessage_t\n");
        free(pdu);
        return NULL;
    }

    pdu->choice.indicationMessage->protocolData.buf = malloc(payload_length);
    if (!pdu->choice.indicationMessage->protocolData.buf) {
        LOG_E(E3AP, "Failed to allocate memory for protocolData\n");
        ASN_STRUCT_FREE(asn_DEF_E3_PDU, pdu);
        return NULL;
    }
    memcpy(pdu->choice.indicationMessage->protocolData.buf, payload, payload_length);
    pdu->choice.indicationMessage->protocolData.size = payload_length;

    return pdu;
}


// Function to free an E3 PDU
void free_E3_PDU(E3_PDU_t *pdu) {
    ASN_STRUCT_FREE(asn_DEF_E3_PDU, pdu);
}
