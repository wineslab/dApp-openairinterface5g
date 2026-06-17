/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Top-level routines for transmission of the PUSCH TS 38.211 v 15.4.0
 */
#include <stdint.h>
#include "PHY/NR_REFSIG/dmrs_nr.h"
#include "PHY/NR_REFSIG/ptrs_nr.h"
#include "PHY/NR_REFSIG/nr_refsig.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_ue.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_proto_ue.h"
#include "PHY/MODULATION/nr_modulation.h"
#include "PHY/MODULATION/modulation_common.h"
#include "common/utils/assertions.h"
#include "common/utils/nr/nr_common.h"
#include "PHY/NR_TRANSPORT/nr_transport_common_proto.h"
#include "PHY/NR_TRANSPORT/nr_sch_dmrs.h"
#include "PHY/defs_nr_common.h"
#include "PHY/TOOLS/tools_defs.h"
#include "executables/nr-softmodem.h"
#include "executables/softmodem-common.h"
#include "T_messages_creator.h"
#include "PHY/NR_REFSIG/ul_ref_seq_nr.h"
#include <openair2/UTIL/OPT/opt.h>
#include "PHY/NR_UE_TRANSPORT/pucch_nr.h"
#include <math.h>

#define MAX_RE_PER_SYMBOL_IN_ALLOC (275 * 12)
#define MAX_NLQM (4 * 8)
#define MAX_UCI_CODED_BITS 1024

//#define DEBUG_PUSCH_MAPPING
//#define DEBUG_MAC_PDU
//#define DEBUG_DFT_IDFT

//extern int32_t uplink_counter;

static void nr_pusch_codeword_scrambling_uci(uint8_t *in,
                                             uint32_t size,
                                             uint32_t Nid,
                                             uint32_t n_RNTI,
                                             const uci_on_pusch_bit_type_t *template,
                                             uint32_t *out)
{
  uint32_t *seq = gold_cache((n_RNTI << 15) + Nid, (size + 31) / 32);
  uint32_t num_words = (size + 31) / 32;

  // Step 1: Initial general scrambling
  // First convert unpacked input to bit-packed words
  uint32_t in_words[num_words];
  memset(in_words, 0, num_words * sizeof(uint32_t));

  for (uint32_t i = 0; i < size; i++) {
    uint32_t word_idx = i / 32;
    uint32_t bit_idx = i % 32;
    if (in[i] & 1) {
      in_words[word_idx] |= (1U << bit_idx);
    }
  }

  for (uint32_t i = 0; i < num_words; i++) {
    out[i] = in_words[i] ^ seq[i];
  }

  // According to 38.211 6.3.1.1
  for (uint32_t i = 0; i < size; i++) {
    if (template[i] == BIT_TYPE_ACK_ULSCH) {
      // Step 2: Overwrite/Correct positions for UCI bits including placeholders X, Y when O_ACK <= 2
      uint32_t pos = i;
      uint32_t idx = pos / 32;
      uint32_t b_idx = pos % 32;

      if (in[pos] == NR_PUSCH_y) {
        // Clear bit
        out[idx] &= ~(1U << b_idx);
        if (b_idx > 0) {
          // Y depends on the final value of the previous bit in the same word.
          // This previous bit could be an ACK (already corrected) or ULSCH (from initial scramble).
          out[idx] |= ((out[idx] >> (b_idx - 1)) & 1) << b_idx;
        } else if (idx > 0) {
          // Y depends on the last bit of the previous word.
          out[idx] |= ((out[idx - 1] >> 31) & 1);
        }
      } else if (in[pos] == NR_PUSCH_x) {
        out[idx] |= (1U << b_idx);
      }
    }
  }
}

void nr_pusch_codeword_scrambling(uint8_t *in,
                                  uint32_t size,
                                  uint32_t Nid,
                                  uint32_t n_RNTI,
                                  bool uci_on_pusch,
                                  const uci_on_pusch_bit_type_t *template,
                                  uint32_t *out)
{
  if (uci_on_pusch)
    // in buffer is in byte-packed format
    nr_pusch_codeword_scrambling_uci(in, size, Nid, n_RNTI, template, out);
  else
    // in buffer is in bit-packed format
    nr_codeword_scrambling(in, size, 0, Nid, n_RNTI, out);
}

/*
The function pointers are set once before calling the mapping funcion for
all symbols based on different parameters. Then the mapping is done for
each symbol by calling the function pointers.
*/
typedef void (*map_dmrs_func_t)(const unsigned int, const c16_t *, c16_t *);
typedef void (*map_data_dmrs_func_t)(const unsigned int, const c16_t *, c16_t *);

/*
The following set of functions map dmrs and/or data REs in one RB based on
configuration of DMRS type, number of CDM groups with no data and delta.
For all other combinations of the parameters not present below is not
applicable.
*/

/*
DMRS mapping in a RB for Type 1.
Mapping as in TS 38.211 6.4.1.1.3 k = 4n + 2k^prime + delta
*/
static void map_dmrs_type1_cdm1_rb(const unsigned int delta, const c16_t *dmrs, c16_t *out)
{
  *(out + delta) = *dmrs++;
  *(out + delta + 2) = *dmrs++;
  *(out + delta + 4) = *dmrs++;
  *(out + delta + 6) = *dmrs++;
  *(out + delta + 8) = *dmrs++;
  *(out + delta + 10) = *dmrs++;
}

/*
Data in DMRS symbol for Type 1, NumCDMGroupNoData = 1 and delta 0 (antenna port 0 and 1).
There is no data in DMRS symbol for other scenarios in type 1.
*/
static void map_data_dmrs_type1_cdm1_rb(const unsigned int num_cdm_no_data, const c16_t *data, c16_t *out)
{
  UNUSED(num_cdm_no_data);
  *(out + 1) = *data++;
  *(out + 3) = *data++;
  *(out + 5) = *data++;
  *(out + 7) = *data++;
  *(out + 9) = *data++;
  *(out + 11) = *data++;
}

#define NR_DMRS_TYPE2_CDM_GRP_SIZE 2
#define NR_DMRS_TYPE2_NUM_CDM_GRP 3

/*
Map DMRS for type 2
Mapping as in TS 38.211 6.4.1.1.3 k = 6n + k^prime + delta
*/
static void map_dmrs_type2_rb(const unsigned int delta, const c16_t *dmrs, c16_t *out)
{
  memcpy(out + delta, dmrs, sizeof(c16_t) * NR_DMRS_TYPE2_CDM_GRP_SIZE);
  out += (NR_DMRS_TYPE2_CDM_GRP_SIZE * NR_DMRS_TYPE2_NUM_CDM_GRP);
  dmrs += NR_DMRS_TYPE2_CDM_GRP_SIZE;
  memcpy(out + delta, dmrs, sizeof(c16_t) * NR_DMRS_TYPE2_CDM_GRP_SIZE);
}

/*
Map data for type 2 DMRS
*/
static void map_data_dmrs_type2_rb(const unsigned int num_cdm_no_data, const c16_t *data, c16_t *out)
{
  unsigned int offset = num_cdm_no_data * NR_DMRS_TYPE2_CDM_GRP_SIZE;
  const unsigned int size = (NR_DMRS_TYPE2_NUM_CDM_GRP - num_cdm_no_data) * NR_DMRS_TYPE2_CDM_GRP_SIZE;
  memcpy(out + offset, data, sizeof(c16_t) * size);
  offset += NR_DMRS_TYPE2_CDM_GRP_SIZE * NR_DMRS_TYPE2_NUM_CDM_GRP;
  data += size;
  memcpy(out + offset, data, sizeof(c16_t) * size);
}

/*
Map data and PTRS in RB
*/
static void map_data_ptrs(const unsigned int ptrsIdx, const c16_t *data, const c16_t *ptrs, c16_t *out)
{
  memcpy(out, data, sizeof(c16_t) * ptrsIdx);
  data += ptrsIdx;
  *(out + ptrsIdx) = *ptrs;
  memcpy(out + ptrsIdx + 1, data, sizeof(c16_t) * NR_NB_SC_PER_RB - ptrsIdx - 1);
}

/*
Map data only in RB
*/
static void map_data_rb(const c16_t *data, c16_t *out)
{
  memcpy(out, data, sizeof(c16_t) * NR_NB_SC_PER_RB);
}

/*
This function is used when a PRB is on both sides of DC.
The destination buffer in this case in not contiguous so REs are mapped on to a temporary buffer
so that we can reuse the existing functions. Then it is copied to the destination buffer.
*/
static void map_over_dc(const unsigned int right_dc,
                        const unsigned int num_cdm_no_data,
                        const unsigned int fft_size,
                        const unsigned int dmrs_per_rb,
                        const unsigned int data_per_rb,
                        const unsigned int delta,
                        const unsigned int ptrsIdx,
                        const c16_t **ptrs,
                        const c16_t **dmrs,
                        const c16_t **data,
                        c16_t **out,
                        map_dmrs_func_t map_data_dmrs_ptr,
                        map_dmrs_func_t map_dmrs_ptr)
{
  // if first RE is DC no need to map in this function
  if (right_dc == 0)
    return;

  c16_t *out_tmp = *out;
  c16_t tmp_out_buf[NR_NB_SC_PER_RB];
  const unsigned int left_dc = NR_NB_SC_PER_RB - right_dc;
  /* copy out to temp buffer. incase we want to preserve the REs in the out buffer
     as we call mapping of data in DMRS symbol after mapping DMRS REs
  */
  memcpy(tmp_out_buf, out_tmp, sizeof(c16_t) * left_dc);
  out_tmp -= (fft_size - left_dc);
  memcpy(tmp_out_buf + left_dc, out_tmp, sizeof(c16_t) * right_dc);

  /* map on to temp buffer */
  if (dmrs && data) {
    map_data_dmrs_ptr(num_cdm_no_data, *data, tmp_out_buf);
    *data += data_per_rb;
  } else if (dmrs) {
    map_dmrs_ptr(delta, *dmrs, tmp_out_buf);
    *dmrs += dmrs_per_rb;
  } else if (ptrs) {
    map_data_ptrs(ptrsIdx, *data, *ptrs, tmp_out_buf);
    *data += (NR_NB_SC_PER_RB - 1);
    *ptrs += 1;
  } else if (data) {
    map_data_rb(*data, tmp_out_buf);
    *data += NR_NB_SC_PER_RB;
  } else {
    DevAssert(false);
  }

  /* copy back to out buffer */
  out_tmp = *out;
  memcpy(out_tmp, tmp_out_buf, sizeof(c16_t) * left_dc);
  out_tmp -= (fft_size - left_dc);
  memcpy(out_tmp, tmp_out_buf + left_dc, sizeof(c16_t) * right_dc);
  out_tmp += right_dc;
  *out = out_tmp;
}

/*
Holds params needed for PUSCH resoruce mapping
*/
typedef struct {
  rnti_t rnti;
  unsigned int K_ptrs;
  unsigned int k_RE_ref;
  unsigned int first_sc_offset;
  unsigned int fft_size;
  unsigned int num_rb_max;
  unsigned int symbols_per_slot;
  unsigned int slot;
  unsigned int dmrs_scrambling_id;
  unsigned int scid;
  unsigned int dmrs_port;
  int *Wt;
  int *Wf;
  unsigned int dmrs_symb_pos;
  unsigned int ptrs_symb_pos;
  unsigned int pdu_bit_map;
  transformPrecoder_t transform_precoding;
  unsigned int bwp_start;
  unsigned int start_rb;
  unsigned int nb_rb;
  unsigned int start_symbol;
  unsigned int num_symbols;
  pusch_dmrs_type_t dmrs_type;
  unsigned int delta;
  unsigned int num_cdm_no_data;
} nr_phy_pxsch_params_t;

/*
Map all REs in one OFDM symbol
This function operation is as follows:
mapping is done on RB basis. if RB contains DC and if DC is in middle
of the RB, then the mapping is done via map_over_dc().
*/
static void map_current_symbol(const nr_phy_pxsch_params_t p,
                               const bool dmrs_symbol,
                               const bool ptrs_symbol,
                               const c16_t *dmrs_seq,
                               const c16_t *ptrs_seq,
                               const c16_t **data,
                               c16_t *out,
                               map_dmrs_func_t map_dmrs_ptr,
                               map_data_dmrs_func_t map_data_dmrs_ptr)
{
  const unsigned int abs_start_rb = p.bwp_start + p.start_rb;
  const unsigned int start_sc = (p.first_sc_offset + abs_start_rb * NR_NB_SC_PER_RB) % p.fft_size;
  const unsigned int dc_rb = (p.fft_size - start_sc) / NR_NB_SC_PER_RB;
  const unsigned int rb_over_dc = (p.fft_size - start_sc) % NR_NB_SC_PER_RB;
  const unsigned int n_cdm = p.num_cdm_no_data;
  const c16_t *data_tmp = *data;
  /* If current symbol is DMRS symbol */
  if (dmrs_symbol) {
    const unsigned int dmrs_per_rb = (p.dmrs_type == pusch_dmrs_type1) ? 6 : 4;
    const unsigned int data_per_rb = NR_NB_SC_PER_RB - dmrs_per_rb;

    const c16_t *p_mod_dmrs = dmrs_seq + abs_start_rb * dmrs_per_rb;
    c16_t *out_tmp = out + start_sc;
    for (unsigned int rb = 0; rb < p.nb_rb; rb++) {
      if (rb == dc_rb) {
        // map RB at DC
        if (rb_over_dc) {
          // if DC is in middle of RB, the following function handles it.
          map_over_dc(rb_over_dc, n_cdm, p.fft_size, dmrs_per_rb, data_per_rb, p.delta, 0, NULL, &p_mod_dmrs, NULL, &out_tmp, map_data_dmrs_ptr, map_dmrs_ptr);
          continue;
        } else {
          // else just move the pointer and following function will map the rb
          out_tmp -= p.fft_size;
        }
      }
      map_dmrs_ptr(p.delta, p_mod_dmrs, out_tmp);
      p_mod_dmrs += dmrs_per_rb;
      out_tmp += NR_NB_SC_PER_RB;
    }

    /* if there is data in current DMRS symbol, we map it here. */
    if (map_data_dmrs_ptr) {
      c16_t *out_tmp = out + start_sc;
      for (unsigned int rb = 0; rb < p.nb_rb; rb++) {
        if (rb == dc_rb) {
          if (rb_over_dc) {
            map_over_dc(rb_over_dc, n_cdm, p.fft_size, dmrs_per_rb, data_per_rb, p.delta, 0, NULL, &p_mod_dmrs, &data_tmp, &out_tmp, map_data_dmrs_ptr, map_dmrs_ptr);
            continue;
          } else {
            out_tmp -= p.fft_size;
          }
        }
        map_data_dmrs_ptr(n_cdm, data_tmp, out_tmp);
        data_tmp += data_per_rb;
        out_tmp += NR_NB_SC_PER_RB;
      }
    }
  /* If current symbol is a PTRS symbol */
  } else if (ptrs_symbol) {
    const unsigned int first_ptrs_re = get_first_ptrs_re(p.rnti, p.K_ptrs, p.nb_rb, p.k_RE_ref) + start_sc;
    const unsigned int ptrs_idx_re = (start_sc - first_ptrs_re) % NR_NB_SC_PER_RB; // PTRS RE index within RB
    unsigned int non_ptrs_rb = (start_sc - first_ptrs_re) / NR_NB_SC_PER_RB; // number of RBs before the first PTRS RB
    int ptrs_idx_rb = -non_ptrs_rb; // RB count to check for PTRS RB
    c16_t *out_tmp = out + start_sc;
    const c16_t *p_mod_ptrs = ptrs_seq;
    /* map data to RBs before the first PTRS RB or if current RB has no PTRS */
    for (unsigned int rb = 0; rb < p.nb_rb; rb++) {
      if (rb < non_ptrs_rb || ptrs_idx_rb % p.K_ptrs) {
        if (rb == dc_rb) {
          if (rb_over_dc) {
            map_over_dc(rb_over_dc, n_cdm, p.fft_size, 0, 0, p.delta, 0, NULL, NULL, &data_tmp, &out_tmp, map_data_dmrs_ptr, map_dmrs_ptr);
            continue;
          } else {
            out_tmp -= p.fft_size;
          }
        }
        map_data_rb(data_tmp, out_tmp);
        data_tmp += NR_NB_SC_PER_RB;
        out_tmp += NR_NB_SC_PER_RB;
      } else {
        if (rb == dc_rb) {
          if (rb_over_dc) {
            map_over_dc(rb_over_dc, n_cdm, p.fft_size, 0, 0, p.delta, ptrs_idx_re, &p_mod_ptrs, NULL, &data_tmp, &out_tmp, map_data_dmrs_ptr, map_dmrs_ptr);
            continue;
          } else {
            out_tmp -= p.fft_size;
          }
        }
        map_data_ptrs(ptrs_idx_re, data_tmp, p_mod_ptrs, out_tmp);
        p_mod_ptrs++; // increament once as only one PTRS RE per RB
        data_tmp += (NR_NB_SC_PER_RB - 1);
        out_tmp += NR_NB_SC_PER_RB;
      }
      ptrs_idx_rb++;
    }
  } else {
    /* only data in this symbol */
    c16_t *out_tmp = out + start_sc;
    for (unsigned int rb = 0; rb < p.nb_rb; rb++) {
      if (rb == dc_rb) {
        if (rb_over_dc) {
          map_over_dc(rb_over_dc, n_cdm, p.fft_size, 0, 0, p.delta, 0, NULL, NULL, &data_tmp, &out_tmp, map_data_dmrs_ptr, map_dmrs_ptr);
          continue;
        } else {
          out_tmp -= p.fft_size;
        }
      }
      map_data_rb(data_tmp, out_tmp);
      data_tmp += NR_NB_SC_PER_RB;
      out_tmp += NR_NB_SC_PER_RB;
    }
  }
  *data = data_tmp;
}

/*
TS 38.211 table 6.4.1.1.3-1 and 2
*/
static void dmrs_amp_mult(const int Wt,
                          const int Wf[2],
                          const c16_t *mod_dmrs,
                          c16_t *mod_dmrs_out,
                          const uint32_t n_dmrs,
                          const pusch_dmrs_type_t dmrs_type,
                          const unsigned int num_cdm_groups_no_data)
{
  float beta_dmrs_pusch = get_beta_dmrs(num_cdm_groups_no_data, dmrs_type == pusch_dmrs_type2);
  /* short array that hold amplitude for k_prime = 0 and k_prime = 1 */
  int32_t alpha_dmrs[2] __attribute((aligned(16)));
  for (int_fast8_t i = 0; i < sizeofArray(alpha_dmrs); i++) {
    const int32_t a = Wf[i] * Wt * AMP;
    alpha_dmrs[i] = a * beta_dmrs_pusch;
  }

  /* multiply amplitude with complex DMRS vector */
  for (int_fast16_t i = 0; i < n_dmrs; i++) {
    mod_dmrs_out[i] = c16mulRealShift(mod_dmrs[i], alpha_dmrs[i % 2], 15);
  }
}

/*
Map ULSCH data and DMRS in all of the scheduled symbols and PRBs
*/
static void map_symbols(const nr_phy_pxsch_params_t p,
                        const unsigned int slot,
                        const c16_t *dmrs_seq,
                        const c16_t *data,
                        c16_t *out)
{
  // asign the function pointers
  map_dmrs_func_t map_dmrs_ptr = NULL;
  map_data_dmrs_func_t map_data_dmrs_ptr = NULL;
  if (p.dmrs_type == pusch_dmrs_type1) {
    map_dmrs_ptr = map_dmrs_type1_cdm1_rb;
    map_data_dmrs_ptr = (p.num_cdm_no_data == 1) ? map_data_dmrs_type1_cdm1_rb : NULL;
  } else {
    map_dmrs_ptr = map_dmrs_type2_rb;
    map_data_dmrs_ptr = (p.num_cdm_no_data < 3) ? map_data_dmrs_type2_rb : NULL;
  }
  // for all symbols
  const unsigned int n_dmrs = (p.bwp_start + p.start_rb + p.nb_rb) * ((p.dmrs_type == pusch_dmrs_type1) ? 6 : 4);
  const c16_t *cur_data = data;
  uint8_t dmrs_symb_idx = 0;
  for (int l = p.start_symbol; l < p.start_symbol + p.num_symbols; l++) {
    const bool dmrs_symbol = is_dmrs_symbol(l, p.dmrs_symb_pos);
    const bool ptrs_symbol = is_ptrs_symbol(l, p.ptrs_symb_pos);
    c16_t mod_dmrs_amp[ALNARS_16_4(n_dmrs)] __attribute((aligned(16)));
    c16_t mod_ptrs_amp[ALNARS_16_4(p.nb_rb)] __attribute((aligned(16)));
    const uint32_t *gold = NULL;
    if (dmrs_symbol || ptrs_symbol) {
      gold = nr_gold_pusch(p.num_rb_max, p.symbols_per_slot, p.dmrs_scrambling_id, p.scid, slot, l);
    }
    if (dmrs_symbol) {
      c16_t mod_dmrs[ALNARS_16_4(n_dmrs)] __attribute((aligned(16)));
      if (p.transform_precoding == transformPrecoder_disabled) {
        nr_modulation(gold, n_dmrs * 2, DMRS_MOD_ORDER, (int16_t *)mod_dmrs);
        dmrs_amp_mult(p.Wt[dmrs_symb_idx % 2], p.Wf, mod_dmrs, mod_dmrs_amp, n_dmrs, p.dmrs_type, p.num_cdm_no_data);
      } else {
        dmrs_amp_mult(p.Wt[dmrs_symb_idx % 2], p.Wf, dmrs_seq, mod_dmrs_amp, n_dmrs, p.dmrs_type, p.num_cdm_no_data);
      }
      dmrs_symb_idx++;
    } else if ((p.pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_PTRS) && ptrs_symbol) {
      AssertFatal(p.transform_precoding == transformPrecoder_disabled, "PTRS NOT SUPPORTED IF TRANSFORM PRECODING IS ENABLED\n");
      c16_t mod_ptrs[ALNARS_16_4(p.nb_rb)] __attribute((aligned(16)));
      nr_modulation(gold, p.nb_rb, DMRS_MOD_ORDER, (int16_t *)mod_ptrs);
      const unsigned int beta_ptrs = 1; // temp value until power control is implemented
      mult_complex_vector_real_scalar(mod_ptrs, beta_ptrs * AMP, mod_ptrs_amp, p.nb_rb);
    }
    map_current_symbol(p,
                       dmrs_symbol,
                       ptrs_symbol,
                       mod_dmrs_amp,
                       mod_ptrs_amp,
                       &cur_data, // increments every symbol
                       out + l * p.fft_size,
                       map_dmrs_ptr,
                       map_data_dmrs_ptr);
  }
}

// Function to lookup beta offset value from Table 9.3-2 in TS 38.213
static double get_beta_offset_csi(const uint8_t beta_offset_idx)
{
  static const double beta_offset_values[19] = {1.125,
                                                1.250,
                                                1.375,
                                                1.625,
                                                1.750,
                                                2.000,
                                                2.250,
                                                2.500,
                                                2.875,
                                                3.125,
                                                3.500,
                                                4.000,
                                                5.000,
                                                6.250,
                                                8.000,
                                                10.000,
                                                12.625,
                                                15.875,
                                                20.000};

  if (beta_offset_idx >= sizeofArray(beta_offset_values)) {
    LOG_E(PHY, "Invalid beta_offset_index %d, using default value\n", beta_offset_idx);
    return beta_offset_values[9];
  }

  return beta_offset_values[beta_offset_idx];
}

static uint32_t get_d_factor_re(const uint32_t a, const uint32_t b)
{
  uint32_t d_factor_re;
  if (a >= b) {
    d_factor_re = 1;
  } else {
    d_factor_re = floor((double)b / a);
    if (d_factor_re == 0) {
      d_factor_re = 1;
    }
  }
  return d_factor_re;
}

// Function to lookup beta offset value from Table 9.3-1 in TS 38.213
static double get_beta_offset_harq_ack(uint8_t beta_offset_index)
{
  static const double beta_offset_values[21] = {
      1.000, // Index 0
      2.000, // Index 1
      2.500, // Index 2
      3.125, // Index 3
      4.000, // Index 4
      5.000, // Index 5
      6.250, // Index 6
      8.000, // Index 7
      10.000, // Index 8
      12.625, // Index 9
      15.875, // Index 10
      20.000, // Index 11
      31.000, // Index 12
      50.000, // Index 13
      80.000, // Index 14
      126.000, // Index 15
      0.6, // Index 16
      0.4, // Index 17
      0.2, // Index 18
      0.1, // Index 19
      0.05, // Index 20
  };

  if (beta_offset_index > 20) {
    LOG_E(PHY, "Invalid beta_offset_index %d, using default value\n", beta_offset_index);
    return 20.000; // Default value using index 11
  }

  return beta_offset_values[beta_offset_index];
}

static double get_alpha_scaling_value(uint8_t alpha_scaling)
{
  switch (alpha_scaling) {
    case 0:
      return 0.5;
    case 1:
      return 0.65;
    case 2:
      return 0.8;
    case 3:
      return 1.0;
    default:
      AssertFatal(false, "Invalid alpha_scaling value %d, valid range is 0-3", alpha_scaling);
      return 1.0;
  }
}

/*
 * This function gets the CRC size of UCI
 */
static int get_crc_uci(const uint16_t ouci)
{
  int L = 0;
  if (ouci > 19) {
    L = 11;
  } else if (ouci > 11) {
    L = 6;
  } else {
    L = 0; // no ACK/NACK
  }

  return L;
}

static uint16_t get_Qd(const uint16_t oack,
                       double beta,
                       double alpha,
                       const uint32_t sumKr,
                       const uint32_t s1,
                       const uint32_t s2,
                       const uint32_t sub)
{
  if (oack == 0)
    return 0;

  uint16_t first_term = ceil(((double)oack + get_crc_uci(oack)) * (double)beta * s1 / sumKr);
  uint16_t second_term = ceil(alpha * s2) - sub;

  return (first_term < second_term) ? first_term : second_term;
}

/*
 * This function calculates the rate matching information for UCI multiplexing with PUSCH
 */
static rate_match_info_uci_t calc_rate_match_info_uci(const nfapi_nr_ue_pusch_pdu_t *pusch_pdu,
                                                      const NR_UL_UE_HARQ_t *harq_process_ul_ue,
                                                      const uint8_t nlqm,
                                                      unsigned int *G)
{
  // get beta offset
  uint8_t beta_offset_index = pusch_pdu->pusch_uci.beta_offset_harq_ack;
  double beta = get_beta_offset_harq_ack(beta_offset_index);

  // get alpha scaling value
  uint8_t alpha_scaling = pusch_pdu->pusch_uci.alpha_scaling;
  double alpha = get_alpha_scaling_value(alpha_scaling);

  // Calculate sumKr (total bits in all code blocks)
  uint32_t sumKr = 0;
  if (harq_process_ul_ue->C == 0) {
    sumKr = 0;
  } else if (harq_process_ul_ue->C == 1) {
    sumKr = harq_process_ul_ue->K;
  } else {
    sumKr = harq_process_ul_ue->K * harq_process_ul_ue->C;
  }

  // Calculate s1: total number of non-DMRS REs in allocation
  uint16_t nb_rb = pusch_pdu->rb_size;
  uint8_t start_symbol = pusch_pdu->start_symbol_index;
  uint8_t number_of_symbols = pusch_pdu->nr_of_symbols;
  uint16_t ul_dmrs_symb_pos = pusch_pdu->ul_dmrs_symb_pos;

  uint32_t s1 = 0;
  for (int l = start_symbol; l < start_symbol + number_of_symbols; l++) {
    if (!((ul_dmrs_symb_pos >> l) & 0x01)) {
      s1 += nb_rb * NR_NB_SC_PER_RB;
    }
  }

  // Calculate s2: number of non-DMRS REs after first DMRS symbol
  int first_dmrs_symbol = -1;
  for (int l = start_symbol; l < start_symbol + number_of_symbols; l++) {
    if ((ul_dmrs_symb_pos >> l) & 0x01) {
      first_dmrs_symbol = l;
      break;
    }
  }
  int l0 = -1;
  if (first_dmrs_symbol >= 0 && first_dmrs_symbol < start_symbol + number_of_symbols - 1) {
    l0 = first_dmrs_symbol + 1;
  }
  uint32_t s2 = 0;
  for (int l = l0; l < start_symbol + number_of_symbols; l++) {
    if (!((ul_dmrs_symb_pos >> l) & 0x01)) {
      s2 += nb_rb * NR_NB_SC_PER_RB;
    }
  }

  uint16_t oack = pusch_pdu->pusch_uci.harq_ack_bit_length;
  uint16_t oack_rvd = (oack <= 2) ? 2 : 0; // get the reserved bits when oACK <= 2 according to TS 38.212 section 6.2.7, step 1

  rate_match_info_uci_t rminfo = {0};

  // get the number of coded HARQ-ACK symbols and bits, TS 38.212 section 6.3.2.4.1.1
  rminfo.Q_dash_ACK = get_Qd(oack, beta, alpha, sumKr, s1, s2, 0);
  rminfo.E_uci_ACK = rminfo.Q_dash_ACK * nlqm;

  if (oack_rvd > 0) {
    rminfo.Q_dash_ACK_rvd = get_Qd(oack_rvd, beta, alpha, sumKr, s1, s2, 0);
    rminfo.E_uci_ACK_rvd = rminfo.Q_dash_ACK_rvd * nlqm;
  }

  // get beta offset for csi
  const double beta_csi1 = get_beta_offset_csi(pusch_pdu->pusch_uci.beta_offset_csi1);

  // get the number of coded CSI part 1 symbols and bits, TS 38.212 section 6.3.2.4.1.2
  const uint16_t ocsi1 = pusch_pdu->pusch_uci.csi_payload.p1_bits;
  rminfo.Q_dash_CSI1 = get_Qd(ocsi1, beta_csi1, alpha, sumKr, s1, s1, rminfo.Q_dash_ACK);
  rminfo.E_uci_CSI1 = rminfo.Q_dash_CSI1 * nlqm;

  // get the number of coded CSI part 2 symbols and bits, TS 38.212 section 6.3.2.4.1.3
  const double beta_csi2 = get_beta_offset_csi(pusch_pdu->pusch_uci.beta_offset_csi2);
  const uint16_t ocsi2 = pusch_pdu->pusch_uci.csi_payload.p2_bits;
  rminfo.Q_dash_CSI2 = get_Qd(ocsi2, beta_csi2, alpha, sumKr, s1, s1, rminfo.Q_dash_ACK + rminfo.Q_dash_CSI1);
  rminfo.E_uci_CSI2 = rminfo.Q_dash_CSI2 * nlqm;

  rminfo.G_ulsch = *G - (rminfo.E_uci_CSI1 + rminfo.E_uci_CSI2);
  if (oack_rvd == 0) {
    rminfo.G_ulsch -= rminfo.E_uci_ACK;
  }

  *G = rminfo.G_ulsch;
  LOG_D(PHY, "[UCI_RATE_MATCH] sumKr=%u, s1=%u, s2=%u, Final G_ulsch (output G): %u\n", sumKr, s1, s2, *G);
  LOG_D(PHY,
        "[UCI_RATE_MATCH] rate matching info returned: E_uci_ACK=%u, E_uci_ACK_rvd=%u, E_uci_CSI1=%u, E_uci_CSI2=%u, G_ulsch=%u\n",
        rminfo.E_uci_ACK,
        rminfo.E_uci_ACK_rvd,
        rminfo.E_uci_CSI1,
        rminfo.E_uci_CSI2,
        rminfo.G_ulsch);

  return rminfo;
}

static int initialize_mapping_resources(const nfapi_nr_ue_pusch_pdu_t *pusch_pdu,
                                        uint32_t *m_ulsch_initial,
                                        uint32_t *m_uci_current)
{
  if (!pusch_pdu || !m_ulsch_initial || !m_uci_current)
    return -1;

  const uint8_t n_pusch_sym_all = pusch_pdu->nr_of_symbols;
  const uint16_t ul_dmrs_symb_pos = pusch_pdu->ul_dmrs_symb_pos;
  const uint8_t dmrs_type = pusch_pdu->dmrs_config_type;
  const uint8_t cdm_grps_no_data = pusch_pdu->num_dmrs_cdm_grps_no_data;
  const uint32_t res_per_symbol_non_dmrs = pusch_pdu->rb_size * NR_NB_SC_PER_RB;
  const uint32_t data_re_on_dmrs_sym_per_prb = NR_NB_SC_PER_RB - get_num_dmrs_re_per_rb(dmrs_type, cdm_grps_no_data);

  // Initialize resources per symbol for ULSCH and UCI
  for (uint8_t i = 0; i < n_pusch_sym_all; i++) {
    uint8_t absolute_symbol_idx = pusch_pdu->start_symbol_index + i;

    if ((ul_dmrs_symb_pos >> absolute_symbol_idx) & 0x01) {
      // Calculate available data REs on DMRS symbols based on DMRS configuration

      m_ulsch_initial[i] = pusch_pdu->rb_size * data_re_on_dmrs_sym_per_prb;
      m_uci_current[i] = 0; // UCI is not mapped on DMRS symbols

    } else { // Not a DMRS symbol

      m_ulsch_initial[i] = res_per_symbol_non_dmrs;
      m_uci_current[i] = res_per_symbol_non_dmrs;
    }
  }

  return 0;
}

static void get_first_uci_symbol(const uint8_t start_symbol,
                                 const uint8_t num_symbols,
                                 const uint16_t dmrs_map,
                                 uint8_t *first_non_dmrs_sym,
                                 uint8_t *dmrs_p1)
{
  // First non-DMRS symbol
  const uint16_t last_sym = start_symbol + num_symbols;
  for (uint_fast8_t s = start_symbol; s < last_sym; s++) {
    if (!is_dmrs_symbol(s, dmrs_map)) {
      *first_non_dmrs_sym = s;
      break;
    }
  }

  // Symbol after first consequtive DMRS symbol
  const uint8_t first_dmrs_sym = get_next_dmrs_symbol_in_slot(dmrs_map, start_symbol, last_sym);
  *dmrs_p1 = first_dmrs_sym + 1;
  while (is_dmrs_symbol(*dmrs_p1, dmrs_map) && *dmrs_p1 < last_sym) {
    (*dmrs_p1)++;
  }

  // Return relative symbol idx
  *first_non_dmrs_sym -= start_symbol;
  *dmrs_p1 -= start_symbol;
}

static inline bool skip_mapping_current_uci(const uci_on_pusch_bit_type_t template, const uci_on_pusch_bit_type_t uci_type_to_map)
{
  bool ret = false;
  switch (uci_type_to_map) {
    case BIT_TYPE_ACK_RESERVED:
    case BIT_TYPE_ACK:
      // ACK bits get the highest priority. Don't skip
      ret = false;
      break;

    case BIT_TYPE_CSI1:
      // Skip if already occupied by ACK and reserved ACK. Template is initialized with ULSCH
      ret = (template != BIT_TYPE_ULSCH);
      break;

    case BIT_TYPE_CSI2:
      // Skip if already occupied by ACK but not reserved ACK
      ret = (template != BIT_TYPE_ACK_RESERVED && template != BIT_TYPE_ULSCH);
      break;

    default:
      AssertFatal(0, "Mapping called with incorrect UCI type\n");
  }
  return ret;
}

struct map_uci_common_arg {
  uci_on_pusch_bit_type_t *template;
  uci_on_pusch_bit_type_t uci_type_to_map;
  uint32_t n_symbols;
  uint32_t nlqm;
  uint16_t G_uci;
  uint8_t l1_c;
  uint32_t *m_uci_current;
  uint32_t *m_ulsch_initial;
  uint32_t (*resv_ack_pos_symb)[MAX_UCI_CODED_BITS];
  uint32_t *resv_ack_count_symb;
};

static void map_uci_common(struct map_uci_common_arg p)
{
  DevAssert((p.resv_ack_count_symb && p.resv_ack_pos_symb && (p.uci_type_to_map == BIT_TYPE_ACK_RESERVED))
            || (!p.resv_ack_count_symb && !p.resv_ack_pos_symb && (p.uci_type_to_map != BIT_TYPE_ACK_RESERVED)));

  uint32_t symbol_start_bit_idx[NR_SYMBOLS_PER_SLOT] = {0};
  for (uint8_t s = 1; s < p.n_symbols; s++) {
    symbol_start_bit_idx[s] = symbol_start_bit_idx[s - 1] + (p.m_ulsch_initial[s - 1] * p.nlqm);
  }

  if (p.uci_type_to_map == BIT_TYPE_ACK_RESERVED)
    memset(p.resv_ack_count_symb, 0, sizeof(*p.resv_ack_count_symb) * NR_SYMBOLS_PER_SLOT);

  uint32_t total_placed = 0;
  for (uint8_t sym = p.l1_c; sym < p.n_symbols && total_placed < p.G_uci; sym++) {
    const uint32_t uci_re_on_sym = p.m_uci_current[sym];

    if (uci_re_on_sym <= 0) {
      continue;
    }

    const uint32_t remaining_to_place = p.G_uci - total_placed;
    const uint32_t num_re_to_select = ceil((double)remaining_to_place / p.nlqm);

    uint32_t d_factor_re = get_d_factor_re(num_re_to_select, uci_re_on_sym);
    uint32_t re_offset = 0;
    uint32_t *cur_sym_resv_ack_pos = p.resv_ack_pos_symb[sym];
    while (re_offset < uci_re_on_sym && total_placed < p.G_uci) {
      uci_on_pusch_bit_type_t cur_template = p.template[symbol_start_bit_idx[sym] + (re_offset * p.nlqm)];
      if (skip_mapping_current_uci(cur_template, p.uci_type_to_map)) {
        re_offset++;
        continue; // if RE already allocated to UCI or reserved for ACK
      }
      for (uint32_t bit_in_re = 0; bit_in_re < p.nlqm; bit_in_re++) {
        if (total_placed >= p.G_uci) {
          break;
        }

        uint32_t bit_offset_in_sym = (re_offset * p.nlqm) + bit_in_re;
        uint32_t cw_idx = symbol_start_bit_idx[sym] + bit_offset_in_sym;
        p.template[cw_idx] = p.uci_type_to_map;
        if (p.uci_type_to_map == BIT_TYPE_ACK_RESERVED) {
          cur_sym_resv_ack_pos[p.resv_ack_count_symb[sym]++] = cw_idx;
        }

        total_placed++;
      }
      re_offset += d_factor_re;
    }
  }
}

/*
 * This function maps the HARQ-ACK bits when O_ACK <= 2
 */
static void map_overlapped_ack(uci_on_pusch_bit_type_t *template,
                               uint16_t G_ack,
                               uint8_t l1_c,
                               uint8_t n_symbols,
                               uint32_t positions_by_sym[][MAX_UCI_CODED_BITS],
                               const uint32_t *count_by_sym)
{
  uint32_t ack_bits_marked = 0;

  for (uint8_t sym_iter = l1_c; sym_iter < n_symbols && ack_bits_marked < G_ack; sym_iter++) {
    const uint32_t num_reserved_bits_on_sym = count_by_sym[sym_iter];

    if (num_reserved_bits_on_sym > 0) {
      const uint32_t num_ack_remaining = G_ack - ack_bits_marked;

      // This d-factor is calculated for stepping through the list of *reserved bits*.
      const uint32_t d_factor_re = get_d_factor_re(num_ack_remaining, num_reserved_bits_on_sym);

      const uint32_t *reserved_indices_on_this_sym = positions_by_sym[sym_iter];

      for (uint32_t i = 0; i < num_reserved_bits_on_sym && ack_bits_marked < G_ack; i += d_factor_re) {
        uint32_t pos_to_mark = reserved_indices_on_this_sym[i];
        template[pos_to_mark] = BIT_TYPE_ACK_ULSCH;

        ack_bits_marked++;
      }
    }
  }
}

/*
 * Applies the template to build the final codeword
 */
static void apply_template_to_codeword(uint8_t *codeword,
                                       const uci_on_pusch_bit_type_t *template,
                                       uint32_t codeword_len,
                                       const uint8_t *ulsch_bits,
                                       const uint64_t *cack,
                                       const uint64_t *csi1,
                                       const uint64_t *csi2,
                                       uint16_t G_ack,
                                       uint32_t G_csi1,
                                       uint32_t G_csi2,
                                       uint32_t G_ulsch)
{
  uint32_t ulsch_idx = 0;
  uint32_t ack_idx = 0;
  uint32_t csi1_idx = 0;
  uint32_t csi2_idx = 0;

  for (uint32_t i = 0; i < codeword_len; i++) {
    switch (template[i]) {
      case BIT_TYPE_ACK:
        if (G_ack > 0 && ack_idx < G_ack) {
          uint32_t word_idx = ack_idx / 64;
          uint32_t bit_in_word_idx = ack_idx % 64;
          codeword[i] = (cack[word_idx] >> bit_in_word_idx) & 1;
          ack_idx++;
        }
        break;

      case BIT_TYPE_ACK_ULSCH:
        if (G_ack > 0 && ack_idx < G_ack) {
          codeword[i] = ((const uint8_t *)cack)[ack_idx++];
          if (G_ulsch > 0 && ulsch_idx < G_ulsch) {
            ulsch_idx++;
          }
        }
        break;

      case BIT_TYPE_CSI1:
        if (G_csi1 > 0 && csi1_idx < G_csi1) {
          uint32_t word_idx = csi1_idx / 64;
          uint32_t bit_in_word_idx = csi1_idx % 64;
          codeword[i] = (csi1[word_idx] >> bit_in_word_idx) & 1;
          csi1_idx++;
        }
        break;

      case BIT_TYPE_CSI2:
        if (G_csi2 > 0 && csi2_idx < G_csi2) {
          uint32_t word_idx = csi2_idx / 64;
          uint32_t bit_in_word_idx = csi2_idx % 64;
          codeword[i] = (csi2[word_idx] >> bit_in_word_idx) & 1;
          csi2_idx++;
        }
        break;

      case BIT_TYPE_ACK_RESERVED:
      case BIT_TYPE_ULSCH:
      default:
        if (G_ulsch > 0 && ulsch_idx < G_ulsch) {
          uint32_t byte_idx = ulsch_idx / 8;
          uint32_t bit_in_byte_idx = ulsch_idx % 8;
          codeword[i] = (ulsch_bits[byte_idx] >> bit_in_byte_idx) & 1;
          ulsch_idx++;
        }
        break;
    }
  }
}

/*
 * This function implements the UCI multiplexing on PUSCH according to TS 38.212 section 6.2.7.
 */
static uci_on_pusch_bit_type_t *nr_data_control_mapping(const nfapi_nr_ue_pusch_pdu_t *pusch_pdu,
                                                        uci_on_pusch_bit_type_t *template,
                                                        unsigned int G_ulsch,
                                                        uint16_t G_ack,
                                                        uint32_t G_ack_rvd,
                                                        uint32_t G_csi1,
                                                        uint32_t G_csi2,
                                                        uint8_t *codeword,
                                                        uint32_t codeword_len,
                                                        const uint8_t *ulsch_bits,
                                                        const uint64_t *cack,
                                                        const uint64_t *csi1,
                                                        const uint64_t *csi2)
{
  if (!pusch_pdu || !codeword || codeword_len == 0 || !template)
    return NULL;
  const uint8_t n_symbols = pusch_pdu->nr_of_symbols;
  if (n_symbols == 0 || n_symbols > NR_SYMBOLS_PER_SLOT)
    return NULL;

  uint32_t m_ulsch_initial[NR_SYMBOLS_PER_SLOT] = {0};
  uint32_t m_uci_current[NR_SYMBOLS_PER_SLOT] = {0}; // This holds RE counts, not bit counts

  if (initialize_mapping_resources(pusch_pdu, m_ulsch_initial, m_uci_current) != 0) {
    LOG_E(PHY, "Failed to initialize mapping resources\n");
    return NULL;
  }

  uint8_t first_non_dmrs_sym = 0;
  uint8_t l1_c = 0;
  get_first_uci_symbol(pusch_pdu->start_symbol_index,
                       pusch_pdu->nr_of_symbols,
                       pusch_pdu->ul_dmrs_symb_pos,
                       &first_non_dmrs_sym,
                       &l1_c);

  memset(template, 0, codeword_len * sizeof(uci_on_pusch_bit_type_t));

  uint32_t positions_by_sym[NR_SYMBOLS_PER_SLOT][MAX_UCI_CODED_BITS] = {0};
  uint32_t count_by_sym[NR_SYMBOLS_PER_SLOT] = {0};

  struct map_uci_common_arg map_arg = {.template = template,
                                       .n_symbols = pusch_pdu->nr_of_symbols,
                                       .nlqm = pusch_pdu->qam_mod_order * pusch_pdu->nrOfLayers,
                                       .l1_c = l1_c,
                                       .m_uci_current = m_uci_current,
                                       .m_ulsch_initial = m_ulsch_initial};
  if (G_ack_rvd > 0) {
    map_arg.uci_type_to_map = BIT_TYPE_ACK_RESERVED;
    map_arg.G_uci = G_ack_rvd;
    map_arg.resv_ack_pos_symb = positions_by_sym;
    map_arg.resv_ack_count_symb = count_by_sym;
    map_uci_common(map_arg);
  } else if (G_ack > 0) {
    map_arg.uci_type_to_map = BIT_TYPE_ACK;
    map_arg.G_uci = G_ack;
    map_uci_common(map_arg);
  }

  // CSI part 1
  map_arg.uci_type_to_map = BIT_TYPE_CSI1;
  map_arg.G_uci = G_csi1;
  map_arg.resv_ack_pos_symb = NULL;
  map_arg.resv_ack_count_symb = NULL;
  map_uci_common(map_arg);
  // CSI part 2
  map_arg.uci_type_to_map = BIT_TYPE_CSI2;
  map_arg.G_uci = G_csi2;
  map_uci_common(map_arg);

  if (G_ack > 0 && G_ack_rvd > 0) {
    map_overlapped_ack(template, G_ack, l1_c, n_symbols, positions_by_sym, count_by_sym);
  }

  apply_template_to_codeword(codeword, template, codeword_len, ulsch_bits, cack, csi1, csi2, G_ack, G_csi1, G_csi2, G_ulsch);

  return template;
}

void nr_ue_ulsch_procedures(PHY_VARS_NR_UE *UE,
                            const uint32_t frame,
                            const uint8_t slot,
                            nr_phy_data_tx_t *phy_data,
                            c16_t **txdataF,
                            bool was_symbol_used[NR_SYMBOLS_PER_SLOT])
{

  int harq_pid = phy_data->ulsch.pusch_pdu.pusch_data.harq_process_id;

  if (phy_data->ulsch.status != NR_ACTIVE)
    return;

  start_meas_nr_ue_phy(UE, PUSCH_PROC_STATS);

  uint8_t ULSCH_ids[1];
  unsigned int G[1];
  uint8_t pusch_id = 0;
  ULSCH_ids[pusch_id] = 0;
  LOG_D(PHY, "nr_ue_ulsch_procedures_slot hard_id %d %d.%d prepare for coding\n", harq_pid, frame, slot);

  NR_UE_ULSCH_t *ulsch_ue = &phy_data->ulsch;
  NR_UE_PUCCH *pucch_ue = &phy_data->pucch_vars;
  NR_UL_UE_HARQ_t *harq_process_ul_ue = &UE->ul_harq_processes[harq_pid];
  const nfapi_nr_ue_pusch_pdu_t *pusch_pdu = &ulsch_ue->pusch_pdu;
  const fapi_nr_ul_config_pucch_pdu *pucch_pdu = &pucch_ue->pucch_pdu[0];
  uci_on_pusch_bit_type_t *uci_mapping_template = NULL;

  uint16_t number_dmrs_symbols = 0;

  uint16_t nb_rb = pusch_pdu->rb_size;
  uint8_t number_of_symbols = pusch_pdu->nr_of_symbols;
  uint8_t dmrs_type = pusch_pdu->dmrs_config_type;
  uint8_t cdm_grps_no_data = pusch_pdu->num_dmrs_cdm_grps_no_data;
  uint8_t nb_dmrs_re_per_rb = ((dmrs_type == pusch_dmrs_type1) ? 6 : 4) * cdm_grps_no_data;
  int start_symbol = pusch_pdu->start_symbol_index;
  uint16_t ul_dmrs_symb_pos = pusch_pdu->ul_dmrs_symb_pos;
  uint8_t mod_order = pusch_pdu->qam_mod_order;
  uint8_t Nl = pusch_pdu->nrOfLayers;
  uint32_t tb_size = pusch_pdu->pusch_data.tb_size;
  uint16_t rnti = pusch_pdu->rnti;

  for (int i = start_symbol; i < start_symbol + number_of_symbols; i++) {
    was_symbol_used[i] = true;
    if ((ul_dmrs_symb_pos >> i) & 0x01)
      number_dmrs_symbols += 1;
  }

  ///////////////////////PTRS parameters' initialization///////////////////

  unsigned int K_ptrs = 0, k_RE_ref = 0;
  uint32_t unav_res = 0;
  if (pusch_pdu->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_PTRS) {
    K_ptrs = pusch_pdu->pusch_ptrs.ptrs_freq_density;
    k_RE_ref = pusch_pdu->pusch_ptrs.ptrs_ports_list[0].ptrs_re_offset;
    uint8_t L_ptrs = 1 << pusch_pdu->pusch_ptrs.ptrs_time_density;

    ulsch_ue->ptrs_symbols = 0;

    set_ptrs_symb_idx(&ulsch_ue->ptrs_symbols, number_of_symbols, start_symbol, L_ptrs, ul_dmrs_symb_pos);
    int n_ptrs = (nb_rb + K_ptrs - 1) / K_ptrs;
    int ptrsSymbPerSlot = get_ptrs_symbols_in_slot(ulsch_ue->ptrs_symbols, start_symbol, number_of_symbols);
    unav_res = n_ptrs * ptrsSymbPerSlot;
  }

  G[pusch_id] = nr_get_G(nb_rb, number_of_symbols, nb_dmrs_re_per_rb, number_dmrs_symbols, unav_res, mod_order, Nl);

  // Capture the initial total PUSCH bits. This is the total_codeword_length for mapping.
  unsigned int G_initial_total_pusch_bits = G[pusch_id];

  uci_on_pusch_bit_type_t template_buffer[G_initial_total_pusch_bits];

  ws_trace_t tmp = {.nr = true,
                    .direction = DIRECTION_UPLINK,
                    .type = UE->frame_parms.frame_type == FDD ? FDD_RADIO : TDD_RADIO,
                    .pdu_buffer = harq_process_ul_ue->payload_AB,
                    .pdu_buffer_size = tb_size,
                    .ueid = 0,
                    .rntiType = WS_C_RNTI,
                    .rnti = rnti,
                    .sysFrame = frame,
                    .subframe = slot,
                    .harq_pid = harq_pid,
                    .oob_event = 0,
                    .oob_event_value = 0};
  trace_pdu(&tmp);

  /////////////////////////ULSCH coding/////////////////////////

  rate_match_info_uci_t rm_info = {0};
  const uint8_t nl_qm = Nl * mod_order; // product of number of layers and modulation order
  if(nr_ulsch_pre_encoding(UE, &phy_data->ulsch, frame, slot, G, 1, ULSCH_ids) != 0) {
    LOG_E(PHY, "Error pre-encoding\n");
    return;
  }

  bool uci_present = (pusch_pdu->pusch_uci.harq_ack_bit_length != 0) || (pusch_pdu->pusch_uci.csi_payload.p1_bits != 0);
  if (uci_present) {
    rm_info = calc_rate_match_info_uci(pusch_pdu, harq_process_ul_ue, nl_qm, &G[pusch_id]);
  }

  if (nr_ulsch_encoding(UE, &phy_data->ulsch, frame, slot, G, 1, ULSCH_ids) == -1) {
    stop_meas_nr_ue_phy(UE, PUSCH_PROC_STATS);
    return;
  }

  LOG_D(PHY, "nr_ue_ulsch_procedures_slot hard_id %d %d.%d\n", harq_pid, frame, slot);

  NR_DL_FRAME_PARMS *frame_parms = &UE->frame_parms;

  int N_PRB_oh = 0; // higher layer (RRC) parameter xOverhead in PUSCH-ServingCellConfig

  // b is the block of bits transmitted on the physical channel after payload coding
  uint64_t b_ack[16] = {0}; // limit to 1024-bit encoded length

  if (pusch_pdu->pusch_uci.harq_ack_bit_length != 0) {
    if (pucch_pdu == NULL) {
      LOG_E(PHY, "nr_ue_ulsch_procedures: pucch_pdu is NULL but HARQ-ACK is present. Cannot proceed with UCI encoding.\n");
      stop_meas_nr_ue_phy(UE, PUSCH_PROC_STATS);
      return;
    }

    nr_uci_encoding(pusch_pdu->pusch_uci.harq_payload,
                    pusch_pdu->pusch_uci.harq_ack_bit_length,
                    pucch_pdu->prb_size,
                    true,
                    rm_info.E_uci_ACK,
                    mod_order,
                    &b_ack[0]);

    LOG_D(PHY,
          "[UCI_ON_PUSCH] G_ulsch=%u (updated G[pusch_id]), G_ack=%u (M_bit), G_ack_rvd=%u, total_len=%u "
          "(G_initial_total_pusch_bits).\n",
          G[pusch_id],
          rm_info.E_uci_ACK,
          rm_info.E_uci_ACK_rvd,
          G_initial_total_pusch_bits);
  }

  uint64_t b_csi1[16] = {0}; // limit to 1024-bit encoded length
  uint64_t b_csi2[16] = {0}; // limit to 1024-bit encoded length
  if (pusch_pdu->pusch_uci.csi_payload.p1_bits != 0) {
    nr_uci_encoding(pusch_pdu->pusch_uci.csi_payload.part1_payload,
                    pusch_pdu->pusch_uci.csi_payload.p1_bits,
                    pucch_pdu->prb_size,
                    true,
                    rm_info.E_uci_CSI1,
                    mod_order,
                    &b_csi1[0]);

    // Process CSI Part 2 if any
    if (pusch_pdu->pusch_uci.csi_payload.p2_bits > 0)
      nr_uci_encoding(pusch_pdu->pusch_uci.csi_payload.part2_payload,
                      pusch_pdu->pusch_uci.csi_payload.p2_bits,
                      pucch_pdu->prb_size,
                      true,
                      rm_info.E_uci_CSI2,
                      mod_order,
                      &b_csi2[0]);
  }

  if (uci_present) {
    uint8_t temp_codeword[G_initial_total_pusch_bits];
    start_meas_nr_ue_phy(UE, UCI_ON_PUSCH_MAPPING);
    nr_data_control_mapping(pusch_pdu,
                            template_buffer,
                            G[pusch_id],
                            rm_info.E_uci_ACK,
                            rm_info.E_uci_ACK_rvd,
                            rm_info.E_uci_CSI1,
                            rm_info.E_uci_CSI2,
                            temp_codeword,
                            G_initial_total_pusch_bits,
                            harq_process_ul_ue->f,
                            b_ack,
                            b_csi1,
                            b_csi2);
    stop_meas_nr_ue_phy(UE, UCI_ON_PUSCH_MAPPING);
    memcpy(harq_process_ul_ue->f, temp_codeword, G_initial_total_pusch_bits);
    uci_mapping_template = template_buffer;
  }

  uint16_t start_rb = pusch_pdu->rb_start;
  uint16_t start_sc = frame_parms->first_carrier_offset + (start_rb + pusch_pdu->bwp_start) * NR_NB_SC_PER_RB;

  if (start_sc >= frame_parms->ofdm_symbol_size)
    start_sc -= frame_parms->ofdm_symbol_size;

  ulsch_ue->Nid_cell = frame_parms->Nid_cell;

  LOG_D(PHY,
        "ulsch TX %x : start_rb %d nb_rb %d mod_order %d Nl %d Tpmi %d bwp_start %d start_sc %d start_symbol %d num_symbols %d "
        "cdmgrpsnodata %d "
        "num_dmrs %d dmrs_re_per_rb %d\n",
        rnti,
        start_rb,
        nb_rb,
        mod_order,
        Nl,
        pusch_pdu->Tpmi,
        pusch_pdu->bwp_start,
        start_sc,
        start_symbol,
        number_of_symbols,
        cdm_grps_no_data,
        number_dmrs_symbols,
        nb_dmrs_re_per_rb);
  // TbD num_of_mod_symbols is set but never used
  const uint32_t N_RE_prime = NR_NB_SC_PER_RB * number_of_symbols - nb_dmrs_re_per_rb * number_dmrs_symbols - N_PRB_oh;
  harq_process_ul_ue->num_of_mod_symbols = N_RE_prime * nb_rb;

  /////////////////////////ULSCH scrambling/////////////////////////

  uint32_t available_bits;

  if (uci_present) {
    // UCI on PUSCH is present, so available bits are the total codeword length
    available_bits = G_initial_total_pusch_bits;
  } else {
    // No UCI on PUSCH, so available bits are the initial G value
    available_bits = G[pusch_id];
  }

  // +1 because size can be not modulo 4 for the uint32_t array
  uint32_t scrambled_output_len_u32 = (available_bits + 31) / 32; // Round up to nearest uint32_t count
  uint32_t scrambled_output[scrambled_output_len_u32];
  memset(scrambled_output, 0, sizeof(scrambled_output));

  nr_pusch_codeword_scrambling(harq_process_ul_ue->f,
                               available_bits,
                               pusch_pdu->data_scrambling_id,
                               rnti,
                               uci_present,
                               uci_mapping_template,
                               scrambled_output);
  if (UE->phy_sim_test_buf) {
    memcpy(UE->phy_sim_test_buf, scrambled_output, (available_bits + 7) / 8);
  }
#if T_TRACER
    {
      // capture scrambled Tx bits via T-Tracer
      log_ul_scrambled_tx_bits(frame, slot, frame_parms, pusch_pdu,
                               number_dmrs_symbols,
                               get_dmrs_port(0, pusch_pdu->dmrs_ports),
                               (const uint8_t *)scrambled_output,
                               available_bits);
    }
#endif
  /////////////////////////ULSCH modulation/////////////////////////

  int max_num_re = Nl * number_of_symbols * nb_rb * NR_NB_SC_PER_RB;
  c16_t d_mod[max_num_re] __attribute__((aligned(16)));

  nr_modulation(scrambled_output, // assume one codeword for the moment
                available_bits,
                mod_order,
                (int16_t *)d_mod);

  /////////////////////////ULSCH layer mapping/////////////////////////

  const int sz = available_bits / mod_order / Nl;
  c16_t ulsch_mod[Nl][sz];

  nr_ue_layer_mapping(d_mod, Nl, sz, ulsch_mod);

  //////////////////////// ULSCH transform precoding ////////////////////////

  uint8_t u = 0, v = 0;
  c16_t *dmrs_seq = NULL;
  /// Transform-coded "y"-sequences (for definition see 38-211 V15.3.0 2018-09, subsection 6.3.1.4)
  c16_t ulsch_mod_tp[max_num_re] __attribute__((aligned(16)));
  memset(ulsch_mod_tp, 0, sizeof(ulsch_mod_tp));

  if (pusch_pdu->transform_precoding == transformPrecoder_enabled) {
    uint32_t nb_re_pusch = nb_rb * NR_NB_SC_PER_RB;
    uint32_t y_offset = 0;
    uint16_t num_dmrs_res_per_symbol = nb_rb * (NR_NB_SC_PER_RB / 2);

    // Calculate index to dmrs seq array based on number of DMRS Subcarriers on this symbol
    int index = get_index_for_dmrs_lowpapr_seq(num_dmrs_res_per_symbol);
    u = pusch_pdu->dfts_ofdm.low_papr_group_number;
    v = pusch_pdu->dfts_ofdm.low_papr_sequence_number;
    dmrs_seq = dmrs_lowpaprtype1_ul_ref_sig[u][v][index];

    AssertFatal(index >= 0,
                "Num RBs not configured according to 3GPP 38.211 section 6.3.1.4. For PUSCH with transform precoding, num RBs "
                "cannot be multiple "
                "of any other primenumber other than 2,3,5\n");
    AssertFatal(dmrs_seq != NULL, "DMRS low PAPR seq not found, check if DMRS sequences are generated");

    LOG_D(PHY, "Transform Precoding params. u: %d, v: %d, index for dmrsseq: %d\n", u, v, index);

    for (int l = start_symbol; l < start_symbol + number_of_symbols; l++) {
      if ((ul_dmrs_symb_pos >> l) & 0x01)
        /* In the symbol with DMRS no data would be transmitted CDM groups is 2*/
        continue;

      nr_dft(&ulsch_mod_tp[y_offset], &ulsch_mod[0][y_offset], nb_re_pusch);

      y_offset = y_offset + nb_re_pusch;

      LOG_D(PHY, "Transform precoding being done on data- symbol: %d, nb_re_pusch: %d, y_offset: %d\n", l, nb_re_pusch, y_offset);

#ifdef DEBUG_PUSCH_MAPPING
      printf("NR_ULSCH_UE: y_offset %u\t nb_re_pusch %u \t Symbol %d \t nb_rb %d \n", y_offset, nb_re_pusch, l, nb_rb);
#endif
    }

#ifdef DEBUG_DFT_IDFT
    int32_t debug_symbols[MAX_NUM_NR_RE] __attribute__((aligned(16)));
    int offset = 0;
    printf("NR_ULSCH_UE: available_bits: %u, mod_order: %d", available_bits, mod_order);

    for (int ll = 0; ll < (available_bits / mod_order); ll++) {
      debug_symbols[ll] = ulsch_ue->ulsch_mod_tp[ll];
    }

    printf("NR_ULSCH_UE: numSym: %d, num_dmrs_sym: %d", number_of_symbols, number_dmrs_symbols);
    for (int ll = 0; ll < (number_of_symbols - number_dmrs_symbols); ll++) {
      nr_idft(&debug_symbols[offset], nb_re_pusch);
      offset = offset + nb_re_pusch;
    }
    LOG_M("preDFT_all_symbols.m", "UE_preDFT", ulsch_mod[0], number_of_symbols * nb_re_pusch, 1, 1);
    LOG_M("postDFT_all_symbols.m", "UE_postDFT", ulsch_mod_tp, number_of_symbols * nb_re_pusch, 1, 1);
    LOG_M("DEBUG_IDFT_SYMBOLS.m", "UE_Debug_IDFT", debug_symbols, number_of_symbols * nb_re_pusch, 1, 1);
    LOG_M("UE_DMRS_SEQ.m", "UE_DMRS_SEQ", dmrs_seq, nb_re_pusch, 1, 1);
#endif
  }

  /////////////////////////ULSCH RE mapping/////////////////////////

  const int slot_sz = frame_parms->ofdm_symbol_size * frame_parms->symbols_per_slot;
  c16_t tx_precoding[Nl][slot_sz];
  memset(tx_precoding, 0, sizeof(tx_precoding));

  for (int nl = 0; nl < Nl; nl++) {
#ifdef DEBUG_PUSCH_MAPPING
    printf("NR_ULSCH_UE: Value of CELL ID %d /t, u %d \n", frame_parms->Nid_cell, u);
#endif

    const uint8_t dmrs_port = get_dmrs_port(nl, pusch_pdu->dmrs_ports);
    const uint8_t delta = get_delta(dmrs_port, dmrs_type);
    int Wt[2];
    int Wf[2];
    get_Wt(Wt, dmrs_port, dmrs_type);
    get_Wf(Wf, dmrs_port, dmrs_type);

    c16_t *data = (pusch_pdu->transform_precoding == transformPrecoder_enabled) ? ulsch_mod_tp : ulsch_mod[nl];

    nr_phy_pxsch_params_t params = {.rnti = rnti,
                                    .K_ptrs = K_ptrs,
                                    .k_RE_ref = k_RE_ref,
                                    .first_sc_offset = frame_parms->first_carrier_offset,
                                    .fft_size = frame_parms->ofdm_symbol_size,
                                    .num_rb_max = frame_parms->N_RB_UL,
                                    .symbols_per_slot = frame_parms->symbols_per_slot,
                                    .dmrs_scrambling_id = pusch_pdu->ul_dmrs_scrambling_id,
                                    .scid = pusch_pdu->scid,
                                    .dmrs_port = dmrs_port,
                                    .Wt = Wt,
                                    .Wf = Wf,
                                    .dmrs_symb_pos = ul_dmrs_symb_pos,
                                    .ptrs_symb_pos = ulsch_ue->ptrs_symbols,
                                    .pdu_bit_map = pusch_pdu->pdu_bit_map,
                                    .transform_precoding = pusch_pdu->transform_precoding,
                                    .bwp_start = pusch_pdu->bwp_start,
                                    .start_rb = start_rb,
                                    .nb_rb = nb_rb,
                                    .start_symbol = start_symbol,
                                    .num_symbols = number_of_symbols,
                                    .dmrs_type = dmrs_type,
                                    .delta = delta,
                                    .num_cdm_no_data = cdm_grps_no_data};

    map_symbols(params, slot, dmrs_seq, data, tx_precoding[nl]);

  } // for (nl=0; nl < Nl; nl++)

  /////////////////////////ULSCH precoding/////////////////////////

  /// Layer Precoding and Antenna port mapping
  // ulsch_mod 0-3 are mapped on antenna ports
  // The precoding info is supported by nfapi such as num_prgs, prg_size, prgs_list and pm_idx
  // The same precoding matrix is applied on prg_size RBs, Thus
  //        pmi = prgs_list[rbidx/prg_size].pm_idx, rbidx =0,...,rbSize-1

  // The Precoding matrix:
  for (int ap = 0; ap < frame_parms->nb_antennas_tx; ap++) {
    for (int l = start_symbol; l < start_symbol + number_of_symbols; l++) {
      uint16_t k = start_sc;

      for (int rb = 0; rb < nb_rb; rb++) {
        // get pmi info
        uint8_t pmi = pusch_pdu->Tpmi;

        if (pmi == 0) { // unitary Precoding
          if (k + NR_NB_SC_PER_RB <= frame_parms->ofdm_symbol_size) { // RB does not cross DC
            if (ap < pusch_pdu->nrOfLayers)
              memcpy(&txdataF[ap][l * frame_parms->ofdm_symbol_size + k],
                     &tx_precoding[ap][l * frame_parms->ofdm_symbol_size + k],
                     NR_NB_SC_PER_RB * sizeof(c16_t));
            else
              memset(&txdataF[ap][l * frame_parms->ofdm_symbol_size + k], 0, NR_NB_SC_PER_RB * sizeof(int32_t));
          } else { // RB does cross DC
            int neg_length = frame_parms->ofdm_symbol_size - k;
            int pos_length = NR_NB_SC_PER_RB - neg_length;
            if (ap < pusch_pdu->nrOfLayers) {
              memcpy(&txdataF[ap][l * frame_parms->ofdm_symbol_size + k],
                     &tx_precoding[ap][l * frame_parms->ofdm_symbol_size + k],
                     neg_length * sizeof(c16_t));
              memcpy(&txdataF[ap][l * frame_parms->ofdm_symbol_size],
                     &tx_precoding[ap][l * frame_parms->ofdm_symbol_size],
                     pos_length * sizeof(int32_t));
            } else {
              memset(&txdataF[ap][l * frame_parms->ofdm_symbol_size + k], 0, neg_length * sizeof(int32_t));
              memset(&txdataF[ap][l * frame_parms->ofdm_symbol_size], 0, pos_length * sizeof(int32_t));
            }
          }
          k += NR_NB_SC_PER_RB;
          if (k >= frame_parms->ofdm_symbol_size) {
            k -= frame_parms->ofdm_symbol_size;
          }
        } else {
          // get the precoding matrix weights:
          const char *W_prec;
          switch (frame_parms->nb_antennas_tx) {
            case 1: // 1 antenna port
              W_prec = nr_W_1l_2p[pmi][ap];
              break;
            case 2: // 2 antenna ports
              if (pusch_pdu->nrOfLayers == 1) // 1 layer
                W_prec = nr_W_1l_2p[pmi][ap];
              else // 2 layers
                W_prec = nr_W_2l_2p[pmi][ap];
              break;
            case 4: // 4 antenna ports
              if (pusch_pdu->nrOfLayers == 1) // 1 layer
                W_prec = nr_W_1l_4p[pmi][ap];
              else if (pusch_pdu->nrOfLayers == 2) // 2 layers
                W_prec = nr_W_2l_4p[pmi][ap];
              else if (pusch_pdu->nrOfLayers == 3) // 3 layers
                W_prec = nr_W_3l_4p[pmi][ap];
              else // 4 layers
                W_prec = nr_W_4l_4p[pmi][ap];
              break;
            default:
              LOG_D(PHY, "Precoding 1,2, or 4 antenna ports are currently supported\n");
              W_prec = nr_W_1l_2p[pmi][ap];
              break;
          }

          for (int i = 0; i < NR_NB_SC_PER_RB; i++) {
            int32_t re_offset = l * frame_parms->ofdm_symbol_size + k;
            txdataF[ap][re_offset] = nr_layer_precoder(slot_sz, tx_precoding, W_prec, pusch_pdu->nrOfLayers, re_offset);
            if (++k >= frame_parms->ofdm_symbol_size) {
              k -= frame_parms->ofdm_symbol_size;
            }
          }
        }
      } // RB loop
    } // symbol loop
  } // port loop

  stop_meas_nr_ue_phy(UE, PUSCH_PROC_STATS);
}

void nr_tx_rotation_and_ofdm_mod(const uint8_t slot,
                                 const NR_DL_FRAME_PARMS *frame_parms,
                                 const uint8_t n_antenna_ports,
                                 c16_t **txdataF,
                                 c16_t **txdata,
                                 uint32_t linktype,
                                 bool was_symbol_used[NR_SYMBOLS_PER_SLOT],
                                 bool no_phase_pre_comp)
{
  int N_RB = (linktype == link_type_sl) ? frame_parms->N_RB_SL : frame_parms->N_RB_UL;

  if (!no_phase_pre_comp) {
    for (int i = 0; i < frame_parms->symbols_per_slot; i++) {
      if (was_symbol_used[i] == false)
        continue;
      for (int ap = 0; ap < n_antenna_ports; ap++) {
        apply_nr_rotation_TX(frame_parms,
                             txdataF[ap],
                             false,
                             frame_parms->symbol_rotation[linktype],
                             slot,
                             N_RB,
                             i,
                             1);
      }
    }
  }

  for (int ap = 0; ap < n_antenna_ports; ap++) {
    if (frame_parms->Ncp == 1) { // extended cyclic prefix
      for (int i = 0; i < frame_parms->symbols_per_slot; i++) {
        if (was_symbol_used[i] == false) {
          memset(&txdata[ap][(frame_parms->ofdm_symbol_size + frame_parms->nb_prefix_samples) * i],
                 0,
                 (frame_parms->nb_prefix_samples + frame_parms->ofdm_symbol_size) * sizeof(int32_t));
          continue;
        }
        PHY_ofdm_mod((int *)&txdataF[ap][frame_parms->ofdm_symbol_size * i],
                     (int *)&txdata[ap][frame_parms->ofdm_symbol_size * i],
                     frame_parms->ofdm_symbol_size,
                     1,
                     frame_parms->nb_prefix_samples,
                     CYCLIC_PREFIX);
      }
    } else { // normal cyclic prefix
      nr_normal_prefix_mod(txdataF[ap], txdata[ap], frame_parms->symbols_per_slot, frame_parms, slot, was_symbol_used);
    }
  }
}
