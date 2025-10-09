#ifndef SPECTRUM_ENC_H
#define SPECTRUM_ENC_H

#include <stdint.h>
#include <stddef.h>

/**
 * Spectrum SM Encoding Functions
 * 
 * Provides encoding for spectrum indication and control messages
 * with compile-time format selection (ASN.1 or JSON)
 */

// Compile-time format selection
#ifdef SPECTRUM_SM_ASN1_FORMAT
    #include "Spectrum-IQDataIndication.h"
    #include "Spectrum-PRBBlacklistControl.h"
#endif

#ifdef SPECTRUM_SM_JSON_FORMAT
    #include <json-c/json.h>
#endif

/**
 * Encode spectrum IQ data indication message
 * 
 * @param iq_data Raw IQ data buffer
 * @param iq_size Size of IQ data
 * @param timestamp Timestamp when data was captured
 * @param encoded_data Output buffer (allocated by function)
 * @param encoded_size Output size
 * @return 0 on success, negative on error
 */
int spectrum_encode_indication(void *iq_data, size_t iq_size, uint32_t timestamp,
                              uint8_t **encoded_data, size_t *encoded_size);



#endif // SPECTRUM_ENC_H