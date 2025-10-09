#include "spectrum_enc.h"
#include "../sm_interface.h"
#include "common/utils/LOG/log.h"
#include <string.h>

#define ENCODE_BUFFER_SIZE 8192

#ifdef SPECTRUM_SM_ASN1_FORMAT
/**
 * ASN.1 encoding implementation
 */

int spectrum_encode_indication(void *iq_data, size_t iq_size, uint32_t timestamp,
                              uint8_t **encoded_data, size_t *encoded_size) {
    if (!iq_data || iq_size == 0 || !encoded_data || !encoded_size) {
        return SM_ERROR_INVALID_PARAM;
    }
    
    // Create ASN.1 structure
    Spectrum_IQDataIndication_t *indication = calloc(1, sizeof(Spectrum_IQDataIndication_t));
    if (!indication) {
        return SM_ERROR_MEMORY;
    }
    
    // Set IQ samples
    indication->iqSamples.buf = malloc(iq_size);
    if (!indication->iqSamples.buf) {
        free(indication);
        return SM_ERROR_MEMORY;
    }
    memcpy(indication->iqSamples.buf, iq_data, iq_size);
    indication->iqSamples.size = iq_size;
    
    // Set sample count (assuming int32_t samples)
    indication->sampleCount = iq_size / sizeof(int32_t);
    
    // Set optional timestamp
    if (timestamp > 0) {
        indication->timestamp = malloc(sizeof(long));
        if (indication->timestamp) {
            *(indication->timestamp) = (long)timestamp;
        }
    }
    
    // Allocate buffer for encoding
    uint8_t *buffer = malloc(ENCODE_BUFFER_SIZE);
    if (!buffer) {
        LOG_E(E3AP, "Failed to allocate encoding buffer\n");
        ASN_STRUCT_FREE(asn_DEF_Spectrum_IQDataIndication, indication);
        return SM_ERROR_INVALID_PARAM;
    }
    
    // Encode to buffer
    asn_enc_rval_t enc_ret = aper_encode_to_buffer(&asn_DEF_Spectrum_IQDataIndication,
                                                   NULL, indication, buffer, ENCODE_BUFFER_SIZE);
    
    if (enc_ret.encoded == -1) {
        LOG_E(E3AP, "Failed to encode Spectrum IQ indication\n");
        free(buffer);
        ASN_STRUCT_FREE(asn_DEF_Spectrum_IQDataIndication, indication);
        return SM_ERROR_INVALID_PARAM;
    }
    
    *encoded_size = (enc_ret.encoded + 7) / 8; // Convert bits to bytes
    *encoded_data = malloc(*encoded_size);
    if (!(*encoded_data)) {
        LOG_E(E3AP, "Failed to allocate output buffer\n");
        free(buffer);
        ASN_STRUCT_FREE(asn_DEF_Spectrum_IQDataIndication, indication);
        return SM_ERROR_INVALID_PARAM;
    }
    memcpy(*encoded_data, buffer, *encoded_size);
    free(buffer);
    
    // Cleanup
    ASN_STRUCT_FREE(asn_DEF_Spectrum_IQDataIndication, indication);
    
    LOG_D(E3AP, "Encoded spectrum indication: %zu bytes\n", *encoded_size);
    return SM_SUCCESS;
}

#else
/**
 * JSON encoding implementation
 */

int spectrum_encode_indication(void *iq_data, size_t iq_size, uint32_t timestamp,
                              uint8_t **encoded_data, size_t *encoded_size) {
    if (!iq_data || iq_size == 0 || !encoded_data || !encoded_size) {
        return SM_ERROR_INVALID_PARAM;
    }
    
    // Create JSON object
    json_object *indication = json_object_new_object();
    if (!indication) {
        return SM_ERROR_MEMORY;
    }
    
    // Convert IQ data to base64 or hex string for JSON
    // For simplicity, we'll use hex encoding
    size_t hex_size = iq_size * 2 + 1;
    char *hex_string = malloc(hex_size);
    if (!hex_string) {
        json_object_put(indication);
        return SM_ERROR_MEMORY;
    }
    
    uint8_t *data_bytes = (uint8_t *)iq_data;
    for (size_t i = 0; i < iq_size; i++) {
        sprintf(hex_string + (i * 2), "%02x", data_bytes[i]);
    }
    hex_string[hex_size - 1] = '\0';
    
    // Add fields to JSON
    json_object_object_add(indication, "iqSamples", json_object_new_string(hex_string));
    json_object_object_add(indication, "sampleCount", 
                          json_object_new_int64(iq_size / sizeof(int32_t)));
    
    if (timestamp > 0) {
        json_object_object_add(indication, "timestamp", json_object_new_int64(timestamp));
    }
    
    // Convert JSON to string
    const char *json_string = json_object_to_json_string(indication);
    if (!json_string) {
        free(hex_string);
        json_object_put(indication);
        return SM_ERROR_INVALID_PARAM;
    }
    
    // Allocate output buffer
    *encoded_size = strlen(json_string);
    *encoded_data = malloc(*encoded_size);
    if (!*encoded_data) {
        free(hex_string);
        json_object_put(indication);
        return SM_ERROR_MEMORY;
    }
    
    memcpy(*encoded_data, json_string, *encoded_size);
    
    // Cleanup
    free(hex_string);
    json_object_put(indication);
    
    LOG_D(E3AP, "Encoded spectrum indication (JSON): %zu bytes\n", *encoded_size);
    return SM_SUCCESS;
}

#endif