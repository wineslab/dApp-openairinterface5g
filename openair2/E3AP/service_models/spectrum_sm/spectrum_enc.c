#include "spectrum_enc.h"
#include "../sm_interface.h"
#include "common/utils/LOG/log.h"
#include <string.h>

#define ENCODE_BUFFER_SIZE 32768  // 32KB - allows 16KB IQ data + ASN.1 overhead

#ifdef SPECTRUM_SM_ASN1_FORMAT
/**
 * ASN.1 encoding implementation
 */

int spectrum_encode_indication(void *iq_data, size_t iq_size, uint32_t timestamp,
                              uint8_t **encoded_data, size_t *encoded_size) {
    LOG_D(E3AP, "[SPECTRUM_ENC] Starting encoding: iq_size=%zu, timestamp=%u\n", iq_size, timestamp);
    
    if (!iq_data || iq_size == 0 || !encoded_data || !encoded_size) {
        LOG_E(E3AP, "[SPECTRUM_ENC] Invalid parameters: iq_data=%p, iq_size=%zu, encoded_data=%p, encoded_size=%p\n",
              iq_data, iq_size, encoded_data, encoded_size);
        return SM_ERROR_INVALID_PARAM;
    }
    
    // Create ASN.1 structure
    Spectrum_IQDataIndication_t *indication = calloc(1, sizeof(Spectrum_IQDataIndication_t));
    if (!indication) {
        LOG_E(E3AP, "[SPECTRUM_ENC] Failed to allocate indication structure\n");
        return SM_ERROR_MEMORY;
    }
    LOG_D(E3AP, "[SPECTRUM_ENC] Allocated indication structure at %p\n", indication);
    
    // Set IQ samples
    indication->iqSamples.buf = malloc(iq_size);
    if (!indication->iqSamples.buf) {
        LOG_E(E3AP, "[SPECTRUM_ENC] Failed to allocate IQ buffer of size %zu\n", iq_size);
        free(indication);
        return SM_ERROR_MEMORY;
    }
    memcpy(indication->iqSamples.buf, iq_data, iq_size);
    indication->iqSamples.size = iq_size;
    LOG_D(E3AP, "[SPECTRUM_ENC] Set IQ samples: buf=%p, size=%zu\n", 
          indication->iqSamples.buf, indication->iqSamples.size);
    
    // Set sample count (assuming int32_t samples)
    indication->sampleCount = iq_size / sizeof(int32_t);
    LOG_D(E3AP, "[SPECTRUM_ENC] Sample count: %ld (iq_size=%zu / sizeof(int32_t)=%zu)\n", 
          indication->sampleCount, iq_size, sizeof(int32_t));
    
    // Set optional timestamp
    if (timestamp > 0) {
        indication->timestamp = malloc(sizeof(long));
        if (indication->timestamp) {
            *(indication->timestamp) = (long)timestamp;
            LOG_D(E3AP, "[SPECTRUM_ENC] Set timestamp: %ld\n", *(indication->timestamp));
        } else {
            LOG_E(E3AP, "[SPECTRUM_ENC] Failed to allocate timestamp\n");
        }
    } else {
        LOG_D(E3AP, "[SPECTRUM_ENC] No timestamp set (timestamp=0)\n");
    }
    
    // Validate structure before encoding
    LOG_D(E3AP, "[SPECTRUM_ENC] Pre-encode validation:\n");
    LOG_D(E3AP, "[SPECTRUM_ENC]   - iqSamples.buf: %p\n", indication->iqSamples.buf);
    LOG_D(E3AP, "[SPECTRUM_ENC]   - iqSamples.size: %zu\n", indication->iqSamples.size);
    LOG_D(E3AP, "[SPECTRUM_ENC]   - sampleCount: %ld\n", indication->sampleCount);
    LOG_D(E3AP, "[SPECTRUM_ENC]   - timestamp: %p\n", indication->timestamp);
    
    // Allocate buffer for encoding
    uint8_t *buffer = malloc(ENCODE_BUFFER_SIZE);
    if (!buffer) {
        LOG_E(E3AP, "[SPECTRUM_ENC] Failed to allocate encoding buffer of size %d\n", ENCODE_BUFFER_SIZE);
        ASN_STRUCT_FREE(asn_DEF_Spectrum_IQDataIndication, indication);
        return SM_ERROR_INVALID_PARAM;
    }
    LOG_D(E3AP, "[SPECTRUM_ENC] Allocated encoding buffer: %p, size: %d\n", buffer, ENCODE_BUFFER_SIZE);
    
    // Encode to buffer
    LOG_D(E3AP, "[SPECTRUM_ENC] Calling aper_encode_to_buffer...\n");
    asn_enc_rval_t enc_ret = aper_encode_to_buffer(&asn_DEF_Spectrum_IQDataIndication,
                                                   NULL, indication, buffer, ENCODE_BUFFER_SIZE);
    
    LOG_D(E3AP, "[SPECTRUM_ENC] Encoding result: encoded=%ld, failed_type=%p, structure_ptr=%p\n",
          enc_ret.encoded, enc_ret.failed_type, enc_ret.structure_ptr);
    
    if (enc_ret.encoded == -1) {
        LOG_E(E3AP, "[SPECTRUM_ENC] Failed to encode Spectrum IQ indication\n");
        if (enc_ret.failed_type) {
            LOG_E(E3AP, "[SPECTRUM_ENC] Failed type: %s\n", enc_ret.failed_type->name);
        }
        if (enc_ret.structure_ptr) {
            LOG_E(E3AP, "[SPECTRUM_ENC] Failed at structure: %p\n", enc_ret.structure_ptr);
        }
        LOG_E(E3AP, "[SPECTRUM_ENC] Data size may exceed buffer (iq_size=%zu, buffer=%d)\n", 
              iq_size, ENCODE_BUFFER_SIZE);
        free(buffer);
        ASN_STRUCT_FREE(asn_DEF_Spectrum_IQDataIndication, indication);
        return SM_ERROR_INVALID_PARAM;
    }
    
    *encoded_size = (enc_ret.encoded + 7) / 8; // Convert bits to bytes
    LOG_D(E3AP, "[SPECTRUM_ENC] Encoded bits: %ld, bytes: %zu\n", enc_ret.encoded, *encoded_size);
    
    *encoded_data = malloc(*encoded_size);
    if (!(*encoded_data)) {
        LOG_E(E3AP, "[SPECTRUM_ENC] Failed to allocate output buffer of size %zu\n", *encoded_size);
        free(buffer);
        ASN_STRUCT_FREE(asn_DEF_Spectrum_IQDataIndication, indication);
        return SM_ERROR_INVALID_PARAM;
    }
    memcpy(*encoded_data, buffer, *encoded_size);
    LOG_D(E3AP, "[SPECTRUM_ENC] Copied %zu bytes to output buffer at %p\n", *encoded_size, *encoded_data);
    free(buffer);
    
    // Cleanup
    ASN_STRUCT_FREE(asn_DEF_Spectrum_IQDataIndication, indication);
    
    LOG_D(E3AP, "[SPECTRUM_ENC] Successfully encoded spectrum indication: %zu bytes\n", *encoded_size);
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