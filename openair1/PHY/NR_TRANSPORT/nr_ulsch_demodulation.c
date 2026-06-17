/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "PHY/defs_gNB.h"
#include "PHY/phy_extern.h"
#include "nr_transport_proto.h"
#include "PHY/NR_TRANSPORT/nr_sch_dmrs.h"
#include "PHY/NR_REFSIG/dmrs_nr.h"
#include "PHY/NR_REFSIG/ptrs_nr.h"
#include "PHY/NR_ESTIMATION/nr_ul_estimation.h"
#include "PHY/defs_nr_common.h"
#include "PHY/nr_phy_common/inc/nr_phy_common.h"
#include "common/utils/nr/nr_common.h"
#include <openair1/PHY/TOOLS/phy_scope_interface.h>
#include "PHY/sse_intrin.h"
#include "T.h"
#include "T_messages_creator.h"
#include <sys/time.h>
#include "openair1/SCHED_NR/sched_nr.h"

#if T_TRACER
static void copy_c16_data_to_slot_memory(c16_t *src, c16_t *dst_slot, int nb_re_pusch, int symbol)
{
  memcpy(&dst_slot[nb_re_pusch * symbol], src, nb_re_pusch * sizeof(c16_t));
}
#endif

void nr_idft(int32_t *z, uint32_t Msc_PUSCH)
{

  simde__m128i idft_in128[1][3240], idft_out128[1][3240];
  simde__m128i norm128;
  int16_t *idft_in0 = (int16_t*)idft_in128[0], *idft_out0 = (int16_t*)idft_out128[0];

  int i, ip;

  LOG_T(PHY,"Doing nr_idft for Msc_PUSCH %d\n", Msc_PUSCH);

  if ((Msc_PUSCH % 1536) > 0) {
    // conjugate input
    for (i = 0; i < (Msc_PUSCH>>2); i++) {
      ((simde__m128i*)z)[i] = oai_mm_conj( ((simde__m128i*)z)[i] );
    }
    for (i = 0, ip = 0; i < Msc_PUSCH; i++, ip+=4)
      ((uint32_t*)idft_in0)[ip+0] = z[i];
  }
  dft_size_idx_t dftsize = get_dft(Msc_PUSCH);
  switch (Msc_PUSCH) {
    case 12:
      dft(dftsize, (int16_t *)idft_in0, (int16_t *)idft_out0, 0);

      norm128 = simde_mm_set1_epi16(9459);

      for (i = 0; i < 12; i++) {
        ((simde__m128i *)idft_out0)[i] = simde_mm_slli_epi16(simde_mm_mulhi_epi16(((simde__m128i *)idft_out0)[i], norm128), 1);
      }

      break;
    default:
      dft(dftsize, idft_in0, idft_out0, 1);
      break;
  }

  if ((Msc_PUSCH % 1536) > 0) {
    for (i = 0, ip = 0; i < Msc_PUSCH; i++, ip+=4)
      z[i] = ((uint32_t*)idft_out0)[ip];

    // conjugate output
    for (i = 0; i < (Msc_PUSCH>>2); i++) {
      ((simde__m128i*)z)[i] = oai_mm_conj(((simde__m128i*)z)[i]);
    }
  }
}

static void nr_ulsch_extract_rbs(c16_t* const rxdataF,
                                 c16_t* const chF,
                                 c16_t *rxFext,
                                 c16_t *chFext,
                                 int rxoffset,
                                 int choffset,
                                 int is_dmrs_symbol,
                                 const nfapi_nr_pusch_pdu_t *pusch_pdu,
                                 NR_DL_FRAME_PARMS *frame_parms)
{
  uint8_t delta = 0;
  int start_re = (frame_parms->first_carrier_offset + (pusch_pdu->rb_start + pusch_pdu->bwp_start) * NR_NB_SC_PER_RB)%frame_parms->ofdm_symbol_size;
  int nb_re_pusch = NR_NB_SC_PER_RB * pusch_pdu->rb_size;
  c16_t *rxF = &rxdataF[rxoffset];
  c16_t *rxF_ext = &rxFext[0];
  c16_t *ul_ch0 = &chF[choffset];
  c16_t *ul_ch0_ext = &chFext[0];

  if (is_dmrs_symbol == 0) {
    if (start_re + nb_re_pusch <= frame_parms->ofdm_symbol_size)
      memcpy(rxF_ext, &rxF[start_re], nb_re_pusch * sizeof(c16_t));
    else {
      int neg_length = frame_parms->ofdm_symbol_size - start_re;
      int pos_length = nb_re_pusch - neg_length;
      memcpy(rxF_ext, &rxF[start_re], neg_length * sizeof(c16_t));
      memcpy(&rxF_ext[neg_length], rxF, pos_length * sizeof(c16_t));
    }
    memcpy(ul_ch0_ext, ul_ch0, nb_re_pusch * sizeof(c16_t));
  }
  else if (pusch_pdu->dmrs_config_type == pusch_dmrs_type1) { // 6 REs / PRB
    AssertFatal(delta == 0 || delta == 1, "Illegal delta %d\n",delta);
    c16_t *rxF32 = &rxF[start_re];
    if (start_re + nb_re_pusch < frame_parms->ofdm_symbol_size) {
      for (int idx = 1 - delta; idx < nb_re_pusch; idx += 2) {
        *rxF_ext++ = rxF32[idx];
        *ul_ch0_ext++ = ul_ch0[idx];
      }
    }
    else { // handle the two pieces around DC
      int neg_length = frame_parms->ofdm_symbol_size - start_re;
      int pos_length = nb_re_pusch - neg_length;
      int idx, idx2;
      for (idx = 1 - delta; idx < neg_length; idx += 2) {
        *rxF_ext++ = rxF32[idx];
        *ul_ch0_ext++= ul_ch0[idx];
      }
      rxF32 = rxF;
      idx2 = idx;
      for (idx = 1 - delta; idx < pos_length; idx += 2, idx2 += 2) {
        *rxF_ext++ = rxF32[idx];
        *ul_ch0_ext++ = ul_ch0[idx2];
      }
    }
  }
  else if (pusch_pdu->dmrs_config_type == pusch_dmrs_type2) { // 8 REs / PRB
    AssertFatal(delta==0||delta==2||delta==4,"Illegal delta %d\n",delta);
    if (start_re + nb_re_pusch < frame_parms->ofdm_symbol_size) {
      for (int idx = 0; idx < nb_re_pusch; idx ++) {
        if (idx % 6 == 2 * delta || idx % 6 == 2 * delta + 1)
          continue;
        *rxF_ext++ = rxF[idx];
        *ul_ch0_ext++ = ul_ch0[idx];
      }
    }
    else {
      int neg_length = frame_parms->ofdm_symbol_size - start_re;
      int pos_length = nb_re_pusch - neg_length;
      c16_t *rxF64 = &rxF[start_re];
      int idx, idx2;
      for (idx = 0; idx < neg_length; idx ++) {
        if (idx % 6 == 2 * delta || idx % 6 == 2 * delta + 1)
          continue;
        *rxF_ext++ = rxF64[idx];
        *ul_ch0_ext++ = ul_ch0[idx];
      }
      rxF64 = rxF;
      idx2 = idx;
      for (idx = 0; idx < pos_length; idx++, idx2++) {
        if (idx % 6 == 2 * delta || idx % 6 == 2 * delta + 1)
          continue;
        *rxF_ext++ = rxF64[idx];
        *ul_ch0_ext++ = ul_ch0[idx2];
      }
    }
  }
}

static int get_nb_re_pusch (NR_DL_FRAME_PARMS *frame_parms, const nfapi_nr_pusch_pdu_t *rel15_ul, int symbol)
{
  uint8_t dmrs_symbol_flag = (rel15_ul->ul_dmrs_symb_pos >> symbol) & 0x01;
  if (dmrs_symbol_flag == 1) {
    if (rel15_ul->dmrs_config_type == 0) {
      // if no data in dmrs cdm group is 1 only even REs have no data
      // if no data in dmrs cdm group is 2 both odd and even REs have no data
      return(rel15_ul->rb_size *(12 - (rel15_ul->num_dmrs_cdm_grps_no_data*6)));
    }
    else return(rel15_ul->rb_size *(12 - (rel15_ul->num_dmrs_cdm_grps_no_data*4)));
  } else
    return (rel15_ul->rb_size * NR_NB_SC_PER_RB);
}

static void inner_rx(PHY_VARS_gNB *gNB,
                     int slot,
                     NR_DL_FRAME_PARMS *frame_parms,
                     NR_gNB_PUSCH *pusch_vars,
                     const nfapi_nr_pusch_pdu_t *rel15_ul,
                     c16_t **rxF,
                     int16_t **llr,
                     int soffset,
                     int symbol,
                     int output_shift,
                     uint32_t nvar,
                     c16_t *rxFext_slot,
                     c16_t *chFext_slot,
                     time_stats_t *pusch_extr,
                     time_stats_t *pusch_ch_comp,
                     time_stats_t *ulsch_llr)
{
  int nb_layer = rel15_ul->nrOfLayers;
  int nb_rx_ant = rel15_ul->param_v4.numSpatialStreamIndices;
  int dmrs_symbol_flag = (rel15_ul->ul_dmrs_symb_pos >> symbol) & 0x01;
  int buffer_length = ceil_mod(rel15_ul->rb_size * NR_NB_SC_PER_RB, 16);
  c16_t rxFext[nb_rx_ant][buffer_length] __attribute__((aligned(64)));
  c16_t chFext[nb_layer][nb_rx_ant][buffer_length] __attribute__((aligned(64)));

  memset(rxFext, 0, sizeof(rxFext));
  memset(chFext, 0, sizeof(chFext));
  int dmrs_symbol;
  if (gNB->chest_time == 0)
    dmrs_symbol = dmrs_symbol_flag ? symbol : get_valid_dmrs_idx_for_channel_est(rel15_ul->ul_dmrs_symb_pos, symbol);
  else { // average of channel estimates stored in first symbol
    int end_symbol = rel15_ul->start_symbol_index + rel15_ul->nr_of_symbols;
    dmrs_symbol = get_next_dmrs_symbol_in_slot(rel15_ul->ul_dmrs_symb_pos, rel15_ul->start_symbol_index, end_symbol);
  }

  for (int aarx = 0; aarx < nb_rx_ant; aarx++) {
    for (int aatx = 0; aatx < nb_layer; aatx++) {
      start_meas(pusch_extr);
      nr_ulsch_extract_rbs(rxF[aarx],
                           (c16_t *)pusch_vars->ul_ch_estimates[aatx * nb_rx_ant + aarx],
                           rxFext[aarx],
                           chFext[aatx][aarx],
                           soffset+(symbol * frame_parms->ofdm_symbol_size),
                           dmrs_symbol * frame_parms->ofdm_symbol_size,
                           dmrs_symbol_flag, 
                           rel15_ul,
                           frame_parms);
      stop_meas(pusch_extr);
#if T_TRACER
      // Data Recording application supports only 1 layer and 1 Tx antenna, so only record the first layer and first Tx antenna
      if (aatx == 0 && aarx == 0) {
        int nb_re_pusch = NR_NB_SC_PER_RB * rel15_ul->rb_size;
        // Assume assume Tx and Rx = 1
        if (T_ACTIVE(T_GNB_PHY_UL_FD_PUSCH_IQ)) {
          copy_c16_data_to_slot_memory(rxFext[aarx], rxFext_slot, nb_re_pusch, symbol);
        }
        if (T_ACTIVE(T_GNB_PHY_UL_FD_CHAN_EST_DMRS_INTERPL)) {
          copy_c16_data_to_slot_memory(chFext[aatx][aarx], chFext_slot, nb_re_pusch, symbol);
        }
      }
#endif
    }
  }
  start_meas(pusch_ch_comp);
  c16_t rho[nb_layer][nb_layer][buffer_length] __attribute__((aligned(64)));
  c16_t rxF_ch_maga[nb_layer][buffer_length] __attribute__((aligned(64)));
  c16_t rxF_ch_magb[nb_layer][buffer_length] __attribute__((aligned(64)));
  c16_t rxF_ch_magc[nb_layer][buffer_length] __attribute__((aligned(64)));

  memset(rho, 0, sizeof(rho));
  for (int i = 0; i < nb_layer; i++)
    memset(&pusch_vars->rxdataF_comp[i][symbol * buffer_length], 0, sizeof(int32_t) * buffer_length);

  nr_channel_compensation(buffer_length,
                          nb_rx_ant,
                          nb_layer,
                          rxFext,
                          chFext,
                          rxF_ch_maga,
                          rxF_ch_magb,
                          rxF_ch_magc,
                          pusch_vars->rxdataF_comp,
                          (nb_layer > 1) ? rho : NULL,
                          rel15_ul->qam_mod_order,
                          symbol,
                          output_shift);
  stop_meas(pusch_ch_comp);

  if (nb_layer == 1 && rel15_ul->transform_precoding == transformPrecoder_enabled && rel15_ul->qam_mod_order <= 6) {
    if (rel15_ul->qam_mod_order > 2)
      nr_freq_equalization(frame_parms,
                           &pusch_vars->rxdataF_comp[0][symbol * buffer_length],
                           rxF_ch_maga[0],
                           rxF_ch_magb[0],
                           symbol,
                           pusch_vars->ul_valid_re_per_slot[symbol],
                           rel15_ul->qam_mod_order);
    nr_idft((int32_t *)&pusch_vars->rxdataF_comp[0][symbol * buffer_length], pusch_vars->ul_valid_re_per_slot[symbol]);
  }
  /* PTRS processing for multiple antenna ports is broken because the following
  function estimates phase offset from and applies compensation to rxdataF_comp
  for each antenna port but rxdataF_comp has MRCed data. */
  /* TODO: Move PTRS phase estimation before immediately after DMRS channels
  estimation and apply PTRS phase compensation in nr_channel_compensationi() */
  if (rel15_ul->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_PTRS) {
    // rxdataF_comp is MRCed so no point in processing all antenna ports. Fixme.
    nr_pusch_ptrs_processing(gNB, frame_parms, rel15_ul, pusch_vars, slot, symbol, 1, buffer_length);
    pusch_vars->ul_valid_re_per_slot[symbol] -= pusch_vars->ptrs_re_per_slot;
  }
  start_meas(ulsch_llr);
  if (nb_layer == 2) {
    if (rel15_ul->qam_mod_order <= 6) {
      nr_compute_ML_llr((c16_t *)&pusch_vars->rxdataF_comp[0][symbol * buffer_length],
                        (c16_t *)&pusch_vars->rxdataF_comp[1][symbol * buffer_length],
                        rxF_ch_maga[0],
                        rxF_ch_maga[1],
                        llr[0],
                        llr[1],
                        rho[0][1],
                        rho[1][0],
                        pusch_vars->ul_valid_re_per_slot[symbol],
                        rel15_ul->qam_mod_order);
    }
    else {
      nr_mmse_2layers(pusch_vars->rxdataF_comp,
                      buffer_length,
                      nb_rx_ant,
                      nb_layer,
                      rxF_ch_maga,
                      rxF_ch_magb,
                      rxF_ch_magc,
                      chFext,
                      rel15_ul->rb_size,
                      rel15_ul->qam_mod_order,
                      pusch_vars->log2_maxh,
                      symbol,
                      pusch_vars->ul_valid_re_per_slot[symbol],
                      nvar);
    }
  }
  if (nb_layer != 2 || rel15_ul->qam_mod_order > 6)
    for (int aatx = 0; aatx < nb_layer; aatx++)
      nr_compute_llr(&pusch_vars->rxdataF_comp[aatx][symbol * buffer_length],
                     rxF_ch_maga[aatx],
                     rxF_ch_magb[aatx],
                     rxF_ch_magc[aatx],
                     llr[aatx],
                     pusch_vars->ul_valid_re_per_slot[symbol],
                     symbol,
                     rel15_ul->qam_mod_order);
  stop_meas(ulsch_llr);
}

typedef struct puschSymbolProc_s {
  PHY_VARS_gNB *gNB;
  NR_DL_FRAME_PARMS *frame_parms;
  const nfapi_nr_pusch_pdu_t *rel15_ul;
  NR_gNB_PUSCH *pusch_vars;
  int slot;
  int startSymbol;
  int numSymbols;
  int16_t *llr;
  int16_t *scramblingSequence;
  uint32_t nvar;
  int beam_nb;
  time_stats_t pusch_extr;
  time_stats_t pusch_ch_comp;
  time_stats_t ulsch_llr;
  time_stats_t ul_demap;
  time_stats_t ul_unscram;
  // TODO: Remove assumption of contiguous ports after DAS is properly handled in beamforming
  uint16_t ant_port_start;
  task_ans_t *ans;
  c16_t *pusch_ch_est_dmrs_interpl_slot_mem;
  c16_t *rxFext_slot_mem;
} puschSymbolProc_t;

static void nr_pusch_symbol_processing(void *arg)
{
  puschSymbolProc_t *rdata=(puschSymbolProc_t*)arg;

  PHY_VARS_gNB *gNB = rdata->gNB;
  NR_DL_FRAME_PARMS *frame_parms = rdata->frame_parms;
  const nfapi_nr_pusch_pdu_t *rel15_ul = rdata->rel15_ul;
  int slot = rdata->slot;
  NR_gNB_PUSCH *pusch_vars = rdata->pusch_vars;
  for (int symbol = rdata->startSymbol; symbol < rdata->startSymbol + rdata->numSymbols; symbol++) {
    if (pusch_vars->ul_valid_re_per_slot[symbol] == 0)
      continue;
    int soffset = (slot % RU_RX_SLOT_DEPTH) * frame_parms->symbols_per_slot * frame_parms->ofdm_symbol_size;
    int buffer_length = ceil_mod(pusch_vars->ul_valid_re_per_slot[symbol] * NR_NB_SC_PER_RB, 16);
    int16_t llrs[rel15_ul->nrOfLayers][ceil_mod(buffer_length * rel15_ul->qam_mod_order, 64)] __attribute__((aligned(32)));
    int16_t *llrss[rel15_ul->nrOfLayers];
    for (int l = 0; l < rel15_ul->nrOfLayers; l++)
      llrss[l] = llrs[l];

    inner_rx(gNB,
             slot,
             frame_parms,
             pusch_vars,
             rel15_ul,
             gNB->common_vars.rxdataF + rdata->ant_port_start,
             llrss,
             soffset,
             symbol,
             pusch_vars->log2_maxh,
             rdata->nvar,
             rdata->rxFext_slot_mem,
             rdata->pusch_ch_est_dmrs_interpl_slot_mem,
             &rdata->pusch_extr,
             &rdata->pusch_ch_comp,
             &rdata->ulsch_llr);

    int nb_re_pusch = pusch_vars->ul_valid_re_per_slot[symbol];
    // layer de-mapping
    start_meas(&rdata->ul_demap);
    int16_t *llr_ptr = llrs[0];
    if (rel15_ul->nrOfLayers != 1) {
      llr_ptr = &rdata->llr[pusch_vars->llr_offset[symbol] * rel15_ul->nrOfLayers];
      for (int i = 0; i < (nb_re_pusch); i++)
        for (int l = 0; l < rel15_ul->nrOfLayers; l++)
          for (int m = 0; m < rel15_ul->qam_mod_order; m++)
            llr_ptr[i * rel15_ul->nrOfLayers * rel15_ul->qam_mod_order + l * rel15_ul->qam_mod_order + m] =
                llrss[l][i * rel15_ul->qam_mod_order + m];
    }
    stop_meas(&rdata->ul_demap);
    // unscrambling
    start_meas(&rdata->ul_unscram);
    int16_t *llr16 = (int16_t*)&rdata->llr[pusch_vars->llr_offset[symbol] * rel15_ul->nrOfLayers];
    int16_t *s = rdata->scramblingSequence + pusch_vars->llr_offset[symbol] * rel15_ul->nrOfLayers;
    const int end = nb_re_pusch * rel15_ul->qam_mod_order * rel15_ul->nrOfLayers;
    int i = 0;
    for (; (i + 8) <= end; i += 8) {
      simde__m128i llr128 = simde_mm_loadu_si128((simde__m128i *)&llr_ptr[i]);
      simde__m128i s128 = simde_mm_loadu_si128((simde__m128i *)&s[i]);
      simde_mm_storeu_si128(llr16 + i, simde_mm_mullo_epi16(llr128, s128));
    }
    for (; i < end; i++)
      llr16[i] = llr_ptr[i] * s[i];
    stop_meas(&rdata->ul_unscram);
  }

  // Task running in // completed
  completed_task_ans(rdata->ans);
}

static uint32_t average_u32(const uint32_t *x, uint16_t size)
{
  AssertFatal(size > 0 && x != NULL, "x is NULL or size is 0\n");

  uint64_t sum_x = 0;
  simde__m256i vec_sum = simde_mm256_setzero_si256();

  int i = 0;
  for (; i + 8 <= size; i += 8) {
    simde__m256i vec_data = simde_mm256_loadu_si256((simde__m256i *)&x[i]);
    vec_sum = simde_mm256_add_epi32(vec_sum, vec_data);
  }
  uint32_t *vec_sum32 = (uint32_t *)&vec_sum;
  for (int k = 0; k < 8; k++) {
    sum_x += vec_sum32[k];
  }
  for (; i < size; i++) {
    sum_x += x[i];
  }

  return (uint32_t)(sum_x / size);
}

int nr_rx_pusch_tp(PHY_VARS_gNB *gNB,
                   NR_gNB_PUSCH *pusch_vars,
                   const nfapi_nr_pusch_pdu_t *rel15_ul,
                   uint32_t *ret_unav_res,
                   uint32_t frame,
                   uint8_t slot)
{
  NR_DL_FRAME_PARMS *frame_parms = &gNB->frame_parms;
  const nfapi_nr_spatial_stream_index_t *p = &rel15_ul->param_v4;
  uint16_t ant_port_start = get_first_ant_idx(gNB->enable_analog_das,
                                              frame_parms->nb_antennas_tx / gNB->common_vars.num_beams_period,
                                              rel15_ul->beamforming.prgs_list[0].dig_bf_interface_list[0].beam_idx,
                                              p->numSpatialStreamIndices > 0 ? p->spatialStreamIndices[0] : 0);

  uint32_t bwp_start_subcarrier = ((rel15_ul->rb_start + rel15_ul->bwp_start) * NR_NB_SC_PER_RB + frame_parms->first_carrier_offset) % frame_parms->ofdm_symbol_size;
  LOG_D(PHY,"pusch %d.%d : bwp_start_subcarrier %d, rb_start %d, first_carrier_offset %d\n", frame,slot,bwp_start_subcarrier, rel15_ul->rb_start, frame_parms->first_carrier_offset);
  LOG_D(PHY,"pusch %d.%d : ul_dmrs_symb_pos %x\n",frame,slot,rel15_ul->ul_dmrs_symb_pos);

  // Memories to store data for data recording
  int buffer_length_slot = rel15_ul->rb_size * NR_NB_SC_PER_RB * 14; // 14 OFDM Symbols per slot
  // data recording application supports only a single layer.
  // nb_rx_ant (= frame_parms->nb_antennas_rx) is limited to 1 for data recording application.
  // int nb_layer (= rel15_ul->nrOfLayers) is limited to 1 for data recording application.

  // Initialize memory for DMRS signals
  c16_t pusch_dmrs_slot_mem[1 * buffer_length_slot] __attribute__((aligned(64)));
  // Initialize memory for channel estimates based on DMRS positions
  c16_t pusch_ch_est_dmrs_pos_slot_mem[buffer_length_slot * 1 * 1] __attribute__((aligned(64)));
  // memory to store slot grid with channel coefficients based on DMRS positions after interpolation
  c16_t pusch_ch_est_dmrs_interpl_slot_mem[buffer_length_slot * 1 * 1] __attribute__((aligned(64)));
  // memory to store extracted data including PUSCH + DMRS
  c16_t rxFext_slot_mem[1 * buffer_length_slot] __attribute__((aligned(64)));

#if T_TRACER
  // Initialize memory for DMRS signals
  if (T_ACTIVE(T_GNB_PHY_UL_FD_DMRS))
    memset(pusch_dmrs_slot_mem, 0, sizeof(c16_t) * 1 * buffer_length_slot);

  // Initialize memory for channel estimates based on DMRS positions
  if (T_ACTIVE(T_GNB_PHY_UL_FD_CHAN_EST_DMRS_POS))
    memset(pusch_ch_est_dmrs_pos_slot_mem, 0, sizeof(c16_t) * buffer_length_slot * 1 * 1);

  // memory to store slot grid with channel coefficients based on DMRS positions after interpolation
  if (T_ACTIVE(T_GNB_PHY_UL_FD_CHAN_EST_DMRS_INTERPL))
    memset(pusch_ch_est_dmrs_interpl_slot_mem, 0, sizeof(c16_t) * buffer_length_slot * 1 * 1);

  // memory to store extracted data including PUSCH + DMRS
  if (T_ACTIVE(T_GNB_PHY_UL_FD_PUSCH_IQ))
    memset(rxFext_slot_mem, 0, sizeof(c16_t) * buffer_length_slot * 1 * 1);
#endif

  //----------------------------------------------------------
  //------------------- Channel estimation -------------------
  //----------------------------------------------------------
  start_meas(&gNB->ulsch_channel_estimation_stats);
  int max_ch = 0;
  uint32_t nvar = 0;
  int end_symbol = rel15_ul->start_symbol_index + rel15_ul->nr_of_symbols;
  uint8_t dmrs_symb_idx = 0;
  for (uint8_t symbol = rel15_ul->start_symbol_index; symbol < end_symbol; symbol++) {
    uint8_t dmrs_symbol_flag = (rel15_ul->ul_dmrs_symb_pos >> symbol) & 0x01;
    LOG_D(PHY, "symbol %d, dmrs_symbol_flag :%d\n", symbol, dmrs_symbol_flag);

    if (dmrs_symbol_flag == 1) {
      for (int nl = 0; nl < rel15_ul->nrOfLayers; nl++) {
        uint32_t nvar_tmp = 0;
        nr_pusch_channel_estimation(gNB,
                                    slot,
                                    nl,
                                    get_dmrs_port(nl, rel15_ul->dmrs_ports),
                                    dmrs_symb_idx,
                                    symbol,
                                    pusch_vars,
                                    ant_port_start,
                                    bwp_start_subcarrier,
                                    rel15_ul,
                                    &max_ch,
                                    &nvar_tmp,
                                    pusch_dmrs_slot_mem,
                                    pusch_ch_est_dmrs_pos_slot_mem);
        nvar += nvar_tmp;
      }
      dmrs_symb_idx++;
    }
  }

  if (dmrs_symb_idx > 0)
    nvar /= (dmrs_symb_idx * rel15_ul->nrOfLayers);

  allocCast2D(n0_subband_power,
              unsigned int,
              gNB->measurements.n0_subband_power,
              frame_parms->nb_antennas_rx,
              frame_parms->N_RB_UL,
              false);

  const uint8_t num_sp_streams = rel15_ul->param_v4.numSpatialStreamIndices;
  int start_sc = (rel15_ul->bwp_start + rel15_ul->rb_start) * NR_NB_SC_PER_RB;
  int middle_sc = frame_parms->ofdm_symbol_size - frame_parms->first_carrier_offset;
  int end_sc = (start_sc + rel15_ul->rb_size * NR_NB_SC_PER_RB - 1) % frame_parms->ofdm_symbol_size;
  for (int aa_pusch = 0; aa_pusch < num_sp_streams; aa_pusch++) {
    const int aarx = ant_port_start + aa_pusch;
    DevAssert(aarx < sizeofArray(pusch_vars->ulsch_power));
    pusch_vars->ulsch_power[aa_pusch] = 0;
    pusch_vars->ulsch_noise_power[aa_pusch] = 0;
    int64_t symb_energy = 0;

    for (uint8_t symbol = rel15_ul->start_symbol_index; symbol < end_symbol; symbol++) {
      int offset0 = ((slot % RU_RX_SLOT_DEPTH) * frame_parms->symbols_per_slot + symbol) * frame_parms->ofdm_symbol_size;
      int offset = offset0 + (frame_parms->first_carrier_offset + start_sc) % frame_parms->ofdm_symbol_size;
      c16_t *ul_ch = &gNB->common_vars.rxdataF[aarx][offset];
      if (end_sc < start_sc) {
        int64_t symb_energy_aux = signal_energy_nodc(ul_ch, middle_sc - start_sc) * (middle_sc - start_sc);
        ul_ch = &gNB->common_vars.rxdataF[aarx][offset0];
        symb_energy_aux += (signal_energy_nodc(ul_ch, end_sc + 1) * (end_sc + 1));
        symb_energy += symb_energy_aux / (rel15_ul->rb_size * NR_NB_SC_PER_RB);
      } else {
        symb_energy += signal_energy_nodc(ul_ch, rel15_ul->rb_size * NR_NB_SC_PER_RB);
      }
    }
    pusch_vars->ulsch_power[aa_pusch] += (symb_energy / rel15_ul->nr_of_symbols);

    pusch_vars->ulsch_noise_power[aa_pusch] +=
        average_u32(&n0_subband_power[aarx][rel15_ul->bwp_start + rel15_ul->rb_start], rel15_ul->rb_size);

    LOG_D(PHY,
          "aa %d, bwp_start%d, rb_start %d, rb_size %d: ulsch_power %d, ulsch_noise_power %d\n",
          aarx,
          rel15_ul->bwp_start,
          rel15_ul->rb_start,
          rel15_ul->rb_size,
          pusch_vars->ulsch_power[aa_pusch],
          pusch_vars->ulsch_noise_power[aa_pusch]);
  }

  // averaging time domain channel estimates
  if (gNB->chest_time == 1)
    nr_chest_time_domain_avg(frame_parms,
                             pusch_vars->ul_ch_estimates,
                             rel15_ul->nr_of_symbols,
                             rel15_ul->start_symbol_index,
                             rel15_ul->ul_dmrs_symb_pos,
                             rel15_ul->rb_size,
                             rel15_ul->nrOfLayers,
                             num_sp_streams);

  stop_meas(&gNB->ulsch_channel_estimation_stats);

  start_meas(&gNB->rx_pusch_init_stats);

  // Scrambling initialization
  int number_dmrs_symbols = 0;
  for (int l = rel15_ul->start_symbol_index; l < end_symbol; l++)
    number_dmrs_symbols += ((rel15_ul->ul_dmrs_symb_pos)>>l) & 0x01;
  int nb_re_dmrs;
  if (rel15_ul->dmrs_config_type == pusch_dmrs_type1)
    nb_re_dmrs = 6*rel15_ul->num_dmrs_cdm_grps_no_data;
  else
    nb_re_dmrs = 4*rel15_ul->num_dmrs_cdm_grps_no_data;

  uint32_t unav_res = 0;
  if (rel15_ul->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_PTRS) {
    uint16_t ptrsSymbPos = 0;
    set_ptrs_symb_idx(&ptrsSymbPos,
                      rel15_ul->nr_of_symbols,
                      rel15_ul->start_symbol_index,
                      1 << rel15_ul->pusch_ptrs.ptrs_time_density,
                      rel15_ul->ul_dmrs_symb_pos);
    int ptrsSymbPerSlot = get_ptrs_symbols_in_slot(ptrsSymbPos, rel15_ul->start_symbol_index, rel15_ul->nr_of_symbols);
    int n_ptrs = (rel15_ul->rb_size + rel15_ul->pusch_ptrs.ptrs_freq_density - 1) / rel15_ul->pusch_ptrs.ptrs_freq_density;
    unav_res = n_ptrs * ptrsSymbPerSlot;
  }

  // get how many bit in a slot //
  int G = nr_get_G(rel15_ul->rb_size,
                   rel15_ul->nr_of_symbols,
                   nb_re_dmrs,
                   number_dmrs_symbols, // number of dmrs symbols irrespective of single or double symbol dmrs
                   unav_res,
                   rel15_ul->qam_mod_order,
                   rel15_ul->nrOfLayers);
  *ret_unav_res = unav_res;

  // initialize scrambling sequence //
  int16_t scramblingSequence[G + 96] __attribute__((aligned(64)));

  nr_codeword_unscrambling_init(scramblingSequence, G, 0, rel15_ul->data_scrambling_id, rel15_ul->rnti);

  // first the computation of channel levels

  int nb_re_pusch = 0, meas_symbol = -1;
  for(meas_symbol = rel15_ul->start_symbol_index; meas_symbol < end_symbol; meas_symbol++) 
    if ((nb_re_pusch = get_nb_re_pusch(frame_parms, rel15_ul, meas_symbol)) > 0)
      break;

  AssertFatal(nb_re_pusch > 0 && meas_symbol >= 0,
              "nb_re_pusch %d cannot be 0 or meas_symbol %d cannot be negative here\n",
              nb_re_pusch,
              meas_symbol);

  // extract the first dmrs for the channel level computation
  // extract the data in the OFDM frame, to the start of the array
  int soffset = (slot % RU_RX_SLOT_DEPTH) * frame_parms->symbols_per_slot * frame_parms->ofdm_symbol_size;

  nb_re_pusch = ceil_mod(nb_re_pusch, 16);
  int dmrs_symbol;
  if (gNB->chest_time == 0)
    dmrs_symbol = get_valid_dmrs_idx_for_channel_est(rel15_ul->ul_dmrs_symb_pos, meas_symbol);
  else // average of channel estimates stored in first symbol
    dmrs_symbol = get_next_dmrs_symbol_in_slot(rel15_ul->ul_dmrs_symb_pos, rel15_ul->start_symbol_index, end_symbol);
  int size_est = nb_re_pusch * frame_parms->symbols_per_slot;
  __attribute__((aligned(32))) int ul_ch_estimates_ext[rel15_ul->nrOfLayers * num_sp_streams][size_est];
  memset(ul_ch_estimates_ext, 0, sizeof(ul_ch_estimates_ext));
  int buffer_length = rel15_ul->rb_size * NR_NB_SC_PER_RB;
  c16_t temp_rxFext[num_sp_streams][buffer_length] __attribute__((aligned(32)));
  for (int aarx = 0; aarx < num_sp_streams; aarx++)
    for (int nl = 0; nl < rel15_ul->nrOfLayers; nl++) {
      start_meas(&gNB->pusch_extraction_stats);
      nr_ulsch_extract_rbs(gNB->common_vars.rxdataF[ant_port_start + aarx],
                           (c16_t *)pusch_vars->ul_ch_estimates[nl * num_sp_streams + aarx],
                           temp_rxFext[aarx],
                           (c16_t *)&ul_ch_estimates_ext[nl * num_sp_streams + aarx][meas_symbol * nb_re_pusch],
                           soffset + meas_symbol * frame_parms->ofdm_symbol_size,
                           dmrs_symbol * frame_parms->ofdm_symbol_size,
                           (rel15_ul->ul_dmrs_symb_pos >> meas_symbol) & 0x01, 
                           rel15_ul,
                           frame_parms);
      stop_meas(&gNB->pusch_extraction_stats);
    }

  uint8_t shift_ch_ext = rel15_ul->nrOfLayers > 1 ? log2_approx(max_ch >> 11) : 0;

  //----------------------------------------------------------
  //--------------------- Channel Scaling --------------------
  //----------------------------------------------------------
  nr_scale_channel(size_est, ul_ch_estimates_ext, meas_symbol, nb_re_pusch, rel15_ul->nrOfLayers, num_sp_streams, shift_ch_ext);

  int avg[num_sp_streams * rel15_ul->nrOfLayers];
  nr_channel_level(meas_symbol,
                   size_est,
                   (c16_t(*)[size_est])ul_ch_estimates_ext,
                   num_sp_streams,
                   rel15_ul->nrOfLayers,
                   avg,
                   nb_re_pusch);

  int avgs = 0;
  for (int nl = 0; nl < rel15_ul->nrOfLayers; nl++)
    for (int aarx = 0; aarx < num_sp_streams; aarx++)
      avgs = cmax(avgs, avg[nl * num_sp_streams + aarx]);

  if (rel15_ul->nrOfLayers == 2 && rel15_ul->qam_mod_order > 6)
    pusch_vars->log2_maxh = (log2_approx(avgs) >> 1) - 3; // for MMSE
  else if (rel15_ul->nrOfLayers == 2)
    pusch_vars->log2_maxh = (log2_approx(avgs) >> 1) - 2 + log2_approx(num_sp_streams >> 1);
  else
    pusch_vars->log2_maxh = (log2_approx(avgs) >> 1) + 1 + log2_approx(num_sp_streams >> 1);

  if (pusch_vars->log2_maxh < 0)
    pusch_vars->log2_maxh = 0;

  stop_meas(&gNB->rx_pusch_init_stats);

  start_meas(&gNB->rx_pusch_symbol_processing_stats);
  int numSymbols = gNB->num_pusch_symbols_per_thread;
  int total_res = 0;
  int const loop_iter = CEILIDIV(rel15_ul->nr_of_symbols, numSymbols);
  puschSymbolProc_t arr[loop_iter];
  task_ans_t ans;
  init_task_ans(&ans, loop_iter);

  int sz_arr = 0;
  for(uint8_t task_index = 0; task_index < loop_iter; task_index++) {
    int symbol = task_index * numSymbols + rel15_ul->start_symbol_index;
    int res_per_task = 0;
    for (int s = 0; s < numSymbols && s + symbol < end_symbol; s++) {
      pusch_vars->ul_valid_re_per_slot[symbol+s] = get_nb_re_pusch(frame_parms,rel15_ul,symbol+s);
      pusch_vars->llr_offset[symbol+s] = ((symbol+s) == rel15_ul->start_symbol_index) ? 
                                         0 : 
                                         pusch_vars->llr_offset[symbol+s-1] + pusch_vars->ul_valid_re_per_slot[symbol+s-1] * rel15_ul->qam_mod_order;
      res_per_task += pusch_vars->ul_valid_re_per_slot[symbol + s];
    }
    total_res += res_per_task;
    if (res_per_task > 0) {
      puschSymbolProc_t *rdata = &arr[sz_arr];
      rdata->ans = &ans;
      ++sz_arr;

      rdata->gNB = gNB;
      rdata->frame_parms = frame_parms;
      rdata->rel15_ul = rel15_ul;
      rdata->slot = slot;
      rdata->startSymbol = symbol;
      // Last task processes remainder symbols
      rdata->numSymbols = task_index == loop_iter - 1 ? rel15_ul->nr_of_symbols - (loop_iter - 1) * numSymbols : numSymbols;
      rdata->pusch_vars = pusch_vars;
      rdata->llr = pusch_vars->llr;
      rdata->scramblingSequence = scramblingSequence;
      rdata->nvar = nvar;
      rdata->ant_port_start = ant_port_start;
      rdata->rxFext_slot_mem = rxFext_slot_mem;
      rdata->pusch_ch_est_dmrs_interpl_slot_mem = pusch_ch_est_dmrs_interpl_slot_mem;
      reset_meas(&rdata->pusch_extr);
      reset_meas(&rdata->pusch_ch_comp);
      reset_meas(&rdata->ulsch_llr);
      reset_meas(&rdata->ul_demap);
      reset_meas(&rdata->ul_unscram);

      if (rel15_ul->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_PTRS) {
        nr_pusch_symbol_processing(rdata);
      } else {
        task_t t = {.func = &nr_pusch_symbol_processing, .args = rdata};
        pushTpool(&gNB->threadPool, t);
      }

      LOG_D(PHY, "%d.%d Added symbol %d to process, in pipe\n", frame, slot, symbol);
    } else {
      completed_task_ans(&ans);
    }
  } // symbol loop

#if T_TRACER
  int dmrs_port = get_dmrs_port(0, rel15_ul->dmrs_ports);

  log_ul_fd_dmrs(frame, slot, frame_parms, rel15_ul,
                 number_dmrs_symbols, dmrs_port,
                 (const c16_t *)(&(pusch_dmrs_slot_mem[0])),
                 rel15_ul->rb_size * NR_NB_SC_PER_RB * rel15_ul->nr_of_symbols * 4);

  log_ul_fd_chan_est_dmrs_pos(frame, slot, frame_parms, rel15_ul,
                              number_dmrs_symbols, dmrs_port,
                              (const c16_t *)(&(pusch_ch_est_dmrs_pos_slot_mem[0])),
                              rel15_ul->rb_size * NR_NB_SC_PER_RB * rel15_ul->nr_of_symbols * 4);

  log_ul_fd_pusch_iq(frame,
                     slot,
                     frame_parms,
                     rel15_ul,
                     number_dmrs_symbols,
                     dmrs_port,
                     (const c16_t *)(&(rxFext_slot_mem[0])),
                     rel15_ul->rb_size * NR_NB_SC_PER_RB * rel15_ul->nr_of_symbols * num_sp_streams * 4);

  log_ul_fd_chan_est_dmrs_interpl(
      frame,
      slot,
      frame_parms,
      rel15_ul,
      number_dmrs_symbols,
      dmrs_port,
      (const c16_t *)pusch_ch_est_dmrs_interpl_slot_mem,
      rel15_ul->rb_size * NR_NB_SC_PER_RB * rel15_ul->nr_of_symbols * num_sp_streams * rel15_ul->nrOfLayers * 4);
#endif

  join_task_ans(&ans);
  for (int i = 0; i < sz_arr; ++i) {
    // retrieve measurements
    puschSymbolProc_t *rdata = &arr[i];
    merge_meas(&gNB->pusch_extraction_stats, &rdata->pusch_extr);
    merge_meas(&gNB->pusch_channel_compensation_stats, &rdata->pusch_ch_comp);
    merge_meas(&gNB->ulsch_llr_stats, &rdata->ulsch_llr);
    merge_meas(&gNB->ulsch_layer_demapping_stats, &rdata->ul_demap);
    merge_meas(&gNB->ulsch_unscrambling_stats, &rdata->ul_unscram);
  }
  stop_meas(&gNB->rx_pusch_symbol_processing_stats);

  // Copy the data to the scope. This cannot be performed in one call to gNBscopeCopy because the data is not contiguous in the
  // buffer due to reference symbol extraction and padding. The gNBscopeCopy call is broken up into steps: trylock, copy, unlock.
  metadata mt = {.slot = slot, .frame = frame};
  if (gNBTryLockScopeData(gNB, gNBPuschRxIq, sizeof(c16_t), 1, total_res, &mt)) {
    int buffer_length = ceil_mod(rel15_ul->rb_size * NR_NB_SC_PER_RB, 16);
    size_t offset = 0;
    for (uint8_t symbol = rel15_ul->start_symbol_index; symbol < (rel15_ul->start_symbol_index + rel15_ul->nr_of_symbols);
         symbol++) {
      gNBscopeCopyUnsafe(gNB,
                         gNBPuschRxIq,
                         &pusch_vars->rxdataF_comp[0][symbol * buffer_length],
                         sizeof(c16_t) * pusch_vars->ul_valid_re_per_slot[symbol],
                         offset,
                         symbol - rel15_ul->start_symbol_index);
      offset += sizeof(c16_t) * pusch_vars->ul_valid_re_per_slot[symbol];
    }
    gNBunlockScopeData(gNB, gNBPuschRxIq)
  }
  uint32_t total_llrs = total_res * rel15_ul->qam_mod_order * rel15_ul->nrOfLayers;
  gNBscopeCopyWithMetadata(gNB, gNBPuschLlr, pusch_vars->llr, sizeof(c16_t), 1, total_llrs, 0, &mt);
  return 0;
}
