#ifndef RAN_FUNC_SM_DAPP_READ_WRITE_AGENT_H
#define RAN_FUNC_SM_DAPP_READ_WRITE_AGENT_H

#include "openair2/E2AP/flexric/src/agent/../sm/sm_io.h"
#include "ran_func_dapp_subs.h"
#include "ran_func_dapp_extern.h"
#include "ran_e2sm_ue_id.h"
#include "../../flexric/src/agent/e2_agent_api.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "common/ran_context.h"

#if defined(E3_AGENT)
#include "e3_agent.h"
#include <endian.h>
#include "openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h"
#endif

/**
 * @brief Populate the DAPP RAN function definition during E2 setup.
 *
 * Called by the E2 agent when building the E2 SETUP REQUEST.
 * Fills the RAN function definition with:
 *   - RAN function name, OID, and description
 *   - Three report styles:
 *       Style 1 (DAPP-ALL): registers for both E3 data reports and subscription map
 *       Style 2 (DAPP-E3-DATA-REPORT): E3 data reports only (ind format 1)
 *       Style 3 (DAPP-E3-SUBSCRIPTION-MAP): subscription map only (ind format 2)
 *   - One control style (format 1)
 *   - Current dApp E3 subscription map (if any dApps are connected)
 * Also initializes the global DAPP subscription state (once).
 *
 * @param data  Pointer to a dapp_e2_setup_t to be filled.
 */
void read_dapp_setup_sm(void* data);

/**
 * @brief Handle DAPP SM subscription requests at the agent.
 *
 * Routes the subscription into the appropriate indication lists based on
 * the report style type in the action definition:
 *   - Style 1: registers for both Format 1 and Format 2 indications
 *   - Style 2: registers for Format 1 indications only (E3 data reports)
 *   - Style 3: registers for Format 2 indications only (subscription map)
 *
 * Returns an aperiodic subscription outcome.
 *
 * @param src  Pointer to a wr_dapp_sub_data_t containing the subscription request.
 * @return     Subscription outcome (APERIODIC_SUBSCRIPTION_FLRC).
 */
sm_ag_if_ans_t write_subs_dapp_sm(void const* src);

/**
 * @brief Handle DAPP SM control requests at the agent.
 *
 * For Format 1 control messages (when compiled with E3_AGENT):
 *   - Extracts RAN function ID, dApp ID, and control payload
 *   - Forwards the control to the E3 agent via e3_send_xapp_control()
 *
 * @param data  Pointer to a dapp_ctrl_req_data_t containing the control request.
 * @return      Control outcome of type DAPP_AGENT_IF_CTRL_ANS_V0.
 */
sm_ag_if_ans_t write_ctrl_dapp_sm(void const* data);

/**
 * @brief Read DAPP SM state from the agent (not implemented).
 *
 * Placeholder — asserts unconditionally if called.
 *
 * @param data  Unused.
 * @return      Never returns (asserts).
 */
bool read_dapp_sm(void*);

#endif