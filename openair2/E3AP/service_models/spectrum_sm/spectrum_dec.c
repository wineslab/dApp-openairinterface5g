#include "spectrum_dec.h"
#include "../sm_interface.h"  
#include "common/utils/LOG/log.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#ifdef SPECTRUM_SM_ASN1_FORMAT
#include "Spectrum-PRBBlacklistControl.h"
#else
#include <json-c/json.h>
#endif

spectrum_prb_control_t* spectrum_decode_prb_control(uint8_t *encoded_data, size_t encoded_size) {
    if (!encoded_data || encoded_size == 0) {
      LOG_E(E3AP, "[SPECTRUM] tried to decode an empty buffer\n");
      return NULL;
    }
    spectrum_prb_control_t *prb_control;

#ifdef SPECTRUM_SM_ASN1_FORMAT
    // ASN.1 decoding implementation
    
    // Decode ASN.1 structure
    Spectrum_PRBBlacklistControl_t *asn_control = NULL;
    asn_dec_rval_t dec_ret = aper_decode(0, &asn_DEF_Spectrum_PRBBlacklistControl,
                                        (void **)&asn_control, encoded_data, encoded_size, 0, 0);
    
    if (dec_ret.code != RC_OK || !asn_control) {
        LOG_E(E3AP, "Failed to decode PRB blacklist control\n");
        if (asn_control) {
            ASN_STRUCT_FREE(asn_DEF_Spectrum_PRBBlacklistControl, asn_control);
        }
        return NULL;
    }

    // LOG_E(E3AP, "----------------- ASN1 DECODER PRINT START----------------- \n");
    // xer_fprint(stdout, &asn_DEF_Spectrum_PRBBlacklistControl, asn_control);
    // LOG_E(E3AP, "----------------- ASN1 DECODER PRINT END ----------------- \n");
    
    // Allocate decoded control structure
    prb_control = malloc(sizeof(spectrum_prb_control_t));
    if (!prb_control) {
        ASN_STRUCT_FREE(asn_DEF_Spectrum_PRBBlacklistControl, asn_control);
        LOG_E(E3AP, "Failed to malloc structure\n");
        return NULL;
    }

    prb_control->blacklisted_prbs = NULL;
    prb_control->prb_count = 0;

    const int n = asn_control->blacklistedPRBs.list.count;
    if (n < 0 || n > 273) {
        LOG_E(E3AP, "Invalid PRB list count: %d\n", n);
        ASN_STRUCT_FREE(asn_DEF_Spectrum_PRBBlacklistControl, asn_control);
        free(prb_control);
        return NULL;
    }

    prb_control->prb_count = (uint32_t)n;

    if (prb_control->prb_count > 0) {
        prb_control->blacklisted_prbs = malloc((size_t)prb_control->prb_count * sizeof(uint16_t));
        if (!prb_control->blacklisted_prbs) {
            LOG_E(E3AP, "Failed to malloc PRB list\n");
            ASN_STRUCT_FREE(asn_DEF_Spectrum_PRBBlacklistControl, asn_control);
            free(prb_control);
            return NULL;
        }

        for (int i = 0; i < n; ++i) {
            if (asn_control->blacklistedPRBs.list.array[i] == NULL) {
                prb_control->blacklisted_prbs[i] = 0;
                continue;
            }

            long v = *(asn_control->blacklistedPRBs.list.array[i]);

            if (v < 0 || v > 272) {
                LOG_E(E3AP, "Invalid PRB value at index %d: %ld\n", i, v);
                ASN_STRUCT_FREE(asn_DEF_Spectrum_PRBBlacklistControl, asn_control);
                free(prb_control->blacklisted_prbs);
                free(prb_control);
                return NULL;
            }

            prb_control->blacklisted_prbs[i] = (uint16_t)v;
        }
    }
    
    // Extract optional sampling threshold
    prb_control->sampling_threshold = 0;
    if (asn_control->samplingThreshold) {
        prb_control->sampling_threshold = *(asn_control->samplingThreshold);
    }
    
    // Extract optional validity period
    prb_control->validity_period = 0;
    if (asn_control->validityPeriod) {
        prb_control->validity_period = *(asn_control->validityPeriod);
    }
    
    // Cleanup ASN.1 structure
    ASN_STRUCT_FREE(asn_DEF_Spectrum_PRBBlacklistControl, asn_control);
    
    LOG_D(E3AP, "Decoded PRB blacklist control: %u PRBs, sampling threshold %u, validity %u seconds\n",
          prb_control->prb_count, prb_control->sampling_threshold, prb_control->validity_period);
    
    return prb_control;
    
#else
    // JSON decoding implementation TBD    
    return NULL;
#endif
}

void spectrum_free_decoded_control(spectrum_prb_control_t *prb_control) {
    if (!prb_control) {
        return;
    }
    
    // Free the dynamically allocated blacklisted_prbs array
    if (prb_control->blacklisted_prbs) {
        free(prb_control->blacklisted_prbs);
        prb_control->blacklisted_prbs = NULL;
    }
    
    // Free the structure itself
    free(prb_control);
}