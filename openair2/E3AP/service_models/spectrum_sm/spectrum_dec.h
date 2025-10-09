#ifndef SPECTRUM_DEC_H
#define SPECTRUM_DEC_H

#include <stdint.h>
#include <stddef.h>

/**
 * Spectrum SM Decoding Functions
 * 
 * Provides decoding for spectrum control messages
 * with compile-time format selection (ASN.1 or JSON)
 */

// Forward declarations for decoded structures
typedef struct spectrum_prb_control_s spectrum_prb_control_t;

/**
 * Decoded PRB blacklist control structure
 */
typedef struct spectrum_prb_control_s {
    uint16_t *blacklisted_prbs;
    uint32_t prb_count;
    uint32_t sampling_threshold;  // 0 if not specified
    uint32_t validity_period;  // 0 if not specified
} spectrum_prb_control_t;

/**
 * Decode PRB blacklist control specifically
 * 
 * @param encoded_data Input encoded data
 * @param encoded_size Size of encoded data  
 * @return spectrum_decode_prb_control on success, NULL on error
 */
spectrum_prb_control_t* spectrum_decode_prb_control(uint8_t *encoded_data, size_t encoded_size);

/**
 * Free a decoded PRB control structure
 * 
 * @param prb_control Pointer to the structure to free
 */
void spectrum_free_decoded_control(spectrum_prb_control_t *prb_control);

#endif // SPECTRUM_DEC_H