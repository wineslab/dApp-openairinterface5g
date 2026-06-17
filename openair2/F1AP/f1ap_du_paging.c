/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "f1ap_common.h"
#include "f1ap_du_paging.h"
#include "lib/f1ap_paging.h"
#include "conversions.h"
#include "oai_asn1.h"
#include "openair2/RRC/LTE/rrc_proto.h"

#include "F1AP_F1AP-PDU.h"

/* @brief Handle F1AP Paging message at DU */
int DU_handle_Paging(instance_t instance, sctp_assoc_t assoc_id, uint32_t stream, F1AP_F1AP_PDU_t *pdu)
{
  f1ap_paging_t decoded = {0};

  DevAssert(pdu);
  (void)instance;
  (void)assoc_id;
  (void)stream;

  if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
    xer_fprint(stdout, &asn_DEF_F1AP_F1AP_PDU, pdu);
  }

  if (!decode_f1ap_paging(&decoded, pdu)) {
    LOG_E(F1AP, "Failed to decode F1AP Paging\n");
    return -1;
  }

  /** @todo Build PCCH-Message (Paging) at DU per TS 38.331 §5.3.2; apply
   *  RRC padding per §8.5; deliver as RLC SDU per §8.2. For each
   *  cell in Paging Cell List that belongs to this DU, queue for MAC; MAC schedules
   *  at PF/PO per TS 38.304 §7. */

  free_f1ap_paging(&decoded);
  return 0;
}
