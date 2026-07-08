/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/**
 * @file l1_kpm_enc.c
 * @brief Encoder for L1-KPM indications (ASN.1 or JSON, selected at runtime).
 *
 * Both paths build the payload without allocating on the indication hot path:
 * the ASN.1 encoder assembles the PDU on the stack and aliases the constant
 * shm-name string into the OCTET STRING; the JSON encoder is a single snprintf.
 */
#include "l1_kpm_enc.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../e3_agent.h"   /* e3_get_encoding() */
#include "L1KPM-Indication.h"
#include "L1KPM-ShmRef.h"
#include "L1KPM-RanFunctionData.h"
#include "aper_encoder.h"

#define L1_KPM_SHM_NAME_LEN  (sizeof(E3_RB_SHM_NAME) - 1u)

int l1_kpm_enc_indication(const e3_ran_buffers_slot_info_t *slot,
                          uint8_t *out_buf,
                          size_t out_buf_size)
{
    if (!slot || !out_buf || out_buf_size == 0) {
        return -1;
    }

    if (e3_get_encoding() == E3_ENCODING_ASN1) {
    L1KPM_ShmRef_t shm_ref;
    memset(&shm_ref, 0, sizeof(shm_ref));
    shm_ref.shmName.buf  = (uint8_t *)E3_RB_SHM_NAME;
    shm_ref.shmName.size = (int)L1_KPM_SHM_NAME_LEN;
    shm_ref.fhBufferIndex = (long)slot->fh_buffer_index;
    shm_ref.fhWriteIndex  = (long)slot->fh_write_index;

    L1KPM_Indication_t pdu;
    memset(&pdu, 0, sizeof(pdu));
    pdu.iqSamplesRef = &shm_ref;
    pdu.timestamp    = (long)slot->timestamp_ns;
    pdu.sfn          = (long)slot->sfn;
    pdu.slot         = (long)slot->slot;

    long cell_id_value    = (long)slot->cell_id;
    long n_rx_ant_value   = (long)slot->n_rx_ant;
    long valid_mask_value = (long)slot->valid_symbol_mask;
    pdu.cellId = &cell_id_value;
    pdu.nRxAnt = &n_rx_ant_value;
    pdu.validSymbolMask = &valid_mask_value;

    asn_enc_rval_t encode_result = aper_encode_to_buffer(&asn_DEF_L1KPM_Indication,
                                                         NULL,
                                                         &pdu,
                                                         out_buf,
                                                         out_buf_size);

    if (encode_result.encoded == -1) {
        return -1;
    }
    /* aper_encode_to_buffer returns BITS — ceiling-divide to bytes. */
    const size_t bytes = (size_t)((encode_result.encoded + 7) / 8);
    if (bytes > out_buf_size) {
        return -1;
    }
    return (int)bytes;
    } else {
    /* ATTRIBUTION: this JSON payload follows NVIDIA Aerial's public E3 message
     * schema (indicationMessage.protocolData), Copyright (c) 2026 NVIDIA
     * CORPORATION & AFFILIATES, SPDX-License-Identifier: Apache-2.0. See
     *   https://github.com/NVIDIA/aerial-sample-apps (dapps/docs/e3_message_schemas.json)
     */
    int written = snprintf((char *)out_buf, out_buf_size,
        "{"
            "\"iq_samples\":{"
                "\"shm_name\":\"" E3_RB_SHM_NAME "\","
                "\"fh_buffer_index\":%u,"
                "\"fh_write_index\":%u"
            "},"
            "\"timestamp\":%" PRIu64 ","
            "\"sfn\":%u,"
            "\"slot\":%u,"
            "\"cell_id\":%u,"
            "\"n_rx_ant\":%u,"
            "\"valid_symbol_mask\":%u"
        "}",
        (unsigned)slot->fh_buffer_index,
        (unsigned)slot->fh_write_index,
        slot->timestamp_ns,
        (unsigned)slot->sfn,
        (unsigned)slot->slot,
        (unsigned)slot->cell_id,
        (unsigned)slot->n_rx_ant,
        (unsigned)slot->valid_symbol_mask);
    if (written < 0 || (size_t)written >= out_buf_size) return -1;
    return written;
    }
}

int l1_kpm_enc_ran_function_data(const char *name, int version, const char *description,
                                 uint8_t **encoded_data, size_t *encoded_size)
{
    if (!name || !description || !encoded_data || !encoded_size) {
        return -1;
    }
    *encoded_data = NULL;
    *encoded_size = 0;

    if (e3_get_encoding() == E3_ENCODING_ASN1) {
    L1KPM_RanFunctionData_t rf;
    memset(&rf, 0, sizeof(rf));

    const size_t name_len = strlen(name);
    const size_t desc_len = strlen(description);
    rf.name.buf        = malloc(name_len);
    rf.description.buf = malloc(desc_len);
    if (!rf.name.buf || !rf.description.buf) {
        free(rf.name.buf);
        free(rf.description.buf);
        return -1;
    }
    memcpy(rf.name.buf, name, name_len);
    rf.name.size = (int)name_len;
    rf.version   = version;
    memcpy(rf.description.buf, description, desc_len);
    rf.description.size = (int)desc_len;

    uint8_t buffer[256];
    asn_enc_rval_t r = aper_encode_to_buffer(&asn_DEF_L1KPM_RanFunctionData,
                                             NULL, &rf, buffer, sizeof(buffer));
    free(rf.name.buf);
    free(rf.description.buf);
    if (r.encoded == -1) {
        return -1;
    }

    *encoded_size = (size_t)((r.encoded + 7) / 8);
    *encoded_data = malloc(*encoded_size);
    if (!*encoded_data) {
        *encoded_size = 0;
        return -1;
    }
    memcpy(*encoded_data, buffer, *encoded_size);
    return 0;
    } else {
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "{\"name\":\"%s\",\"version\":%d,\"description\":\"%s\"}",
        name, version, description);
    if (n < 0 || (size_t)n >= sizeof(buf)) {
        return -1;
    }

    *encoded_size = (size_t)n;
    *encoded_data = malloc(*encoded_size);
    if (!*encoded_data) {
        *encoded_size = 0;
        return -1;
    }
    memcpy(*encoded_data, buf, *encoded_size);
    return 0;
    }
}
