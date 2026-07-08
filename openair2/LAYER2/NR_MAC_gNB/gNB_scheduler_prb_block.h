/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef GNB_SCHEDULER_PRB_BLOCK_H
#define GNB_SCHEDULER_PRB_BLOCK_H

#include <stdint.h>

#include "LAYER2/NR_MAC_gNB/nr_mac_gNB.h"

/* dApp PRB-blocking state. mask_dl/ul are per-PRB 14-bit symbol bitmaps (vrb_map
 * encoding) OR'd into the VRB maps at slot start, so every allocator sees blocked
 * PRBs as occupied. prev_mask_* hold just-released bits that may still linger in
 * not-yet-reseeded vrb_map ring slices; helpers read mask_*|prev_mask_* until the
 * retire tick clears them. All fields under lock. */
typedef struct prb_block_state_s {
  pthread_mutex_t lock;
  bool active_dl;
  bool active_ul;
  bool needs_ul_full_stamp;            /* one-shot: next apply stamps mask_ul into every ring slice */
  uint16_t mask_dl[MAX_BWP_SIZE];      /* per-PRB symbol bitmap (absolute PRB index) */
  uint16_t mask_ul[MAX_BWP_SIZE];
  uint16_t prev_mask_dl[MAX_BWP_SIZE]; /* bits dropped from mask_dl, still in the ring */
  uint16_t prev_mask_ul[MAX_BWP_SIZE];
  int64_t apply_counter;               /* ++ per apply_prb_block_masks */
  int64_t dl_retire_at;                /* zero prev_mask_dl when apply_counter >= this (INT64_MAX = idle) */
  int64_t ul_retire_at;
} prb_block_state_t;

typedef enum { PRB_BLOCK_DIR_DL = 0, PRB_BLOCK_DIR_UL = 1 } prb_block_dir_t;

/* Log rate-limiter: ++*counter, true for the first 5 then every 1000th. Inline
 * and unconditional: the graceful-skip log paths use it in every build. */
static inline bool prb_block_ratelimit(uint64_t *counter)
{
  const uint64_t count = ++(*counter);
  return count <= 5 || (count % 1000) == 0;
}

#ifdef E3_AGENT

/* Replace a direction's block mask (NULL/len 0 = clear). False if state unallocated. */
bool set_prb_block_mask(gNB_MAC_INST *mac, prb_block_dir_t dir, const uint16_t *mask, int len);

/* OR both masks into the current-slot VRB maps. Call once per slot, after vrb_map
 * seeding and before any scheduling step. */
void apply_prb_block_masks(gNB_MAC_INST *mac, frame_t frame, slot_t slot);

/* Copy the effective UL mask (mask_ul | prev_mask_ul) into out; true iff any bit set. */
bool get_effective_prb_block_mask_ul(gNB_MAC_INST *mac, uint16_t out[MAX_BWP_SIZE]);

/* Copy the effective DL mask (mask_dl | prev_mask_dl) into out; true iff any bit set. */
bool get_effective_prb_block_mask_dl(gNB_MAC_INST *mac, uint16_t out[MAX_BWP_SIZE]);

/* OR the active UL block into one vrb_map_UL row (no-op if no UL block). Re-applies
 * the block after a sensing-reserved row was reset to ulprbbl. */
void prb_block_reapply_ul_row(gNB_MAC_INST *mac, uint16_t *row);

/* True iff conflict bits `alloc` are entirely the dApp UL block, so the caller can
 * skip the OR instead of asserting. Caller pre-filters alloc == 0. */
bool vrb_map_UL_conflict_is_dapp_block_only(gNB_MAC_INST *mac, int rb, uint16_t alloc);

/* Effective UL occupancy of one PRB for the PUCCH occasion search (corrects a stale
 * lookahead slice). Lock-free when no UL block is installed/retiring. */
uint16_t prb_block_pucch_effective_ul(gNB_MAC_INST *mac, uint16_t slice_bits, int rb);

/* One rate-limited LOG_W summarizing a channel's per-RB collisions. No-op if count 0. */
void log_prb_block_collision_summary(const char *site,
                                     int first_rb, int last_rb, int count,
                                     uint16_t mask,
                                     frame_t frame, slot_t slot,
                                     rnti_t rnti);

/* Reserve `mask` over [rb_start, rb_start+nb_rb) of vrb_map_UL for a cell UL channel,
 * skipping dApp-block collisions (logged once) and asserting only on a genuine
 * conflict. Shared by fill_vrb/nr_add_msg3/nr_configure_srs. allow_static_blacklist
 * also ORs RBs in the operator ulprbbl (SRS). channel_label = "PRACH"/"Msg3"/"SRS". */
void prb_block_reserve_ul_channel(gNB_MAC_INST *mac,
                                  uint16_t *vrb_map_UL,
                                  int rb_start, int nb_rb, uint16_t mask,
                                  bool allow_static_blacklist,
                                  frame_t frame, slot_t slot, rnti_t rnti,
                                  const char *channel_label);


#endif /* E3_AGENT */

#endif /* GNB_SCHEDULER_PRB_BLOCK_H */
