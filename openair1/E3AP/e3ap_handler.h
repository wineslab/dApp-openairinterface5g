#ifndef E3AP_HANDLER_H
#define E3AP_HANDLER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "E3-PDU.h"
#include "E3-SetupRequest.h"
#include "E3-SetupResponse.h"
#include "E3-ControlAction.h"
#include "E3-IndicationMessage.h"
#include "E3-ControlAction.h"

#include "common/utils/LOG/log.h"


E3_PDU_t *create_setup_request(int ranIdentifier, long *ranFunctions, size_t ranFunctionsCount);
E3_PDU_t *create_setup_response(long responseCode);
E3_PDU_t *create_indication_message(const int32_t *payload, size_t payload_length);
int encode_E3_PDU(E3_PDU_t *pdu, uint8_t **buffer, size_t *buffer_size);
E3_PDU_t *decode_E3_PDU(uint8_t *buffer, size_t buffer_size);
void free_E3_PDU(E3_PDU_t *pdu);
long parse_setup_response(E3_SetupResponse_t *response);
uint8_t *parse_control_action(E3_ControlAction_t *controlAction);

#endif // E3AP_HANDLER_H