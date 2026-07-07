/*
 * E2SM-CCC: OAI-side data sources for REPORT Style 2 (cell-level)
 * periodic reporting of O-NRCellDU.
 *
 * Reads the current cell configuration from the gNB-DU MAC layer:
 *   arfcnDL          ← scc->downlinkConfigCommon->frequencyInfoDL->absoluteFrequencyPointA
 *   bSChannelBwDL    ← nrmac->config[0].carrier_config.dl_bandwidth.value  (MHz)
 *   bWPList[0]:
 *     subCarrierSpacing  ← scs_SpecificCarrierList[0]->subcarrierSpacing  (kHz)
 *     numberOfRBs        ← scs_SpecificCarrierList[0]->carrierBandwidth   (PRBs)
 *     startRB            ← scs_SpecificCarrierList[0]->offsetToCarrier
 *     bwpContext         = DL
 *     isInitialBwp       = INITIAL
 */

#include "ran_func_ccc.h"

#include "openair2/E2AP/flexric/src/sm/ccc_sm/ccc_data_ie_wrapper.h"
#include "openair2/E2AP/flexric/src/sm/ccc_sm/ccc_sm_id.h"

#if defined (NGRAN_GNB_DU)
#include "openair2/LAYER2/NR_MAC_gNB/mac_proto.h"
#include "openair2/COMMON/f1ap_messages_types.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static int32_t nr_scs_enum_to_khz(long scs_enum)
{
  switch (scs_enum) {
    case 0: return 15;
    case 1: return 30;
    case 2: return 60;
    case 3: return 120;
    default: return 0;
  }
}

static int32_t nr_ssb_scs_enum_to_khz(long scs_enum)
{
  switch (scs_enum) {
    case 0: return 15;
    case 1: return 30;
    case 2: return 60;
    case 3: return 120;
    case 4: return 240;
    default: return 0;
  }
}

static int32_t nr_ssb_periodicity_enum_to_ms(long e)
{
  switch (e) {
    case 0: return 5;
    case 1: return 10;
    case 2: return 20;
    case 3: return 40;
    case 4: return 80;
    case 5: return 160;
    default: return 0;
  }
}

static void fill_event_time(char* dst, size_t dst_sz)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  struct tm tm_utc;
  gmtime_r(&tv.tv_sec, &tm_utc);
  snprintf(dst, dst_sz, "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
           tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
           tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec,
           (long)(tv.tv_usec / 1000));
}

static void fill_cell_global_id(ccc_nr_cgi_t* out)
{
  snprintf(out->plmn.mcc, sizeof(out->plmn.mcc), "%s", "001");
  snprintf(out->plmn.mnc, sizeof(out->plmn.mnc), "%s", "01");
  snprintf(out->nr_cell_identity, sizeof(out->nr_cell_identity), "%s", "000000001");
}

#if defined (NGRAN_GNB_DU)
static void fill_o_nr_cell_du(ccc_o_nr_cell_du_t* out)
{
  if (RC.nrmac == NULL || RC.nrmac[0] == NULL) return;

  gNB_MAC_INST* nrmac = RC.nrmac[0];
  NR_COMMON_channels_t* cc = &nrmac->common_channels[0];
  NR_ServingCellConfigCommon_t* scc = cc->ServingCellConfigCommon;
  if (scc == NULL) return;

  f1ap_served_cell_info_t const* cell_info = NULL;
  if (nrmac->f1_config.setup_req != NULL
      && nrmac->f1_config.setup_req->num_cells_available > 0
      && nrmac->f1_config.setup_req->cell != NULL) {
    cell_info = &nrmac->f1_config.setup_req->cell[0].info;
  }

  if (cell_info != NULL) {
    out->has_cell_local_id = true;
    out->cell_local_id = (int64_t)cell_info->nr_cellid;

    out->has_nr_pci = true;
    out->nr_pci = (int64_t)cell_info->nr_pci;

    if (cell_info->tac != NULL) {
      out->has_nr_tac = true;
      out->nr_tac = (int64_t)(*cell_info->tac);
    }
  } else if (scc->physCellId != NULL) {
    // Pre-F1-setup fallback: at least the PCI is in SCC.
    out->has_nr_pci = true;
    out->nr_pci = (int64_t)(*scc->physCellId);
  }

  // DL frequency & bandwidth
  if (scc->downlinkConfigCommon != NULL
      && scc->downlinkConfigCommon->frequencyInfoDL != NULL) {
    NR_FrequencyInfoDL_t const* dl_freq = scc->downlinkConfigCommon->frequencyInfoDL;
    out->has_arfcn_dl = true;
    out->arfcn_dl = (int64_t)dl_freq->absoluteFrequencyPointA;

    if (dl_freq->absoluteFrequencySSB != NULL) {
      out->has_ssb_frequency = true;
      out->ssb_frequency = (int64_t)*dl_freq->absoluteFrequencySSB;
    }
  }

  if (nrmac->config[0].carrier_config.dl_bandwidth.tl.tag != 0
      || nrmac->config[0].carrier_config.dl_bandwidth.value != 0) {
    out->has_bs_channel_bw_dl = true;
    out->bs_channel_bw_dl = (int64_t)nrmac->config[0].carrier_config.dl_bandwidth.value;
  }

  // UL frequency & bandwidth
  if (scc->uplinkConfigCommon != NULL
      && scc->uplinkConfigCommon->frequencyInfoUL != NULL
      && scc->uplinkConfigCommon->frequencyInfoUL->absoluteFrequencyPointA != NULL) {
    out->has_arfcn_ul = true;
    out->arfcn_ul = (int64_t)*scc->uplinkConfigCommon->frequencyInfoUL->absoluteFrequencyPointA;
  } else if (out->has_arfcn_dl) {
    out->has_arfcn_ul = true;
    out->arfcn_ul = out->arfcn_dl;
  }

  if (scc->supplementaryUplinkConfig != NULL
      && scc->supplementaryUplinkConfig->frequencyInfoUL != NULL
      && scc->supplementaryUplinkConfig->frequencyInfoUL->absoluteFrequencyPointA != NULL) {
    out->has_arfcn_sul = true;
    out->arfcn_sul = (int64_t)*scc->supplementaryUplinkConfig->frequencyInfoUL->absoluteFrequencyPointA;
  }

  if (nrmac->config[0].carrier_config.uplink_bandwidth.tl.tag != 0
      || nrmac->config[0].carrier_config.uplink_bandwidth.value != 0) {
    out->has_bs_channel_bw_ul = true;
    out->bs_channel_bw_ul = (int64_t)nrmac->config[0].carrier_config.uplink_bandwidth.value;
  }

  // SSB
  if (scc->ssbSubcarrierSpacing != NULL) {
    int32_t khz = nr_ssb_scs_enum_to_khz(*scc->ssbSubcarrierSpacing);
    if (khz > 0) {
      out->has_ssb_sub_carrier_spacing = true;
      out->ssb_sub_carrier_spacing = khz;
    }
  }

  if (scc->ssb_periodicityServingCell != NULL) {
    int32_t ms = nr_ssb_periodicity_enum_to_ms(*scc->ssb_periodicityServingCell);
    if (ms > 0) {
      out->has_ssb_periodicity = true;
      out->ssb_periodicity = ms;
    }
  }

  // ssbOffset
  if (nrmac->config[0].ssb_table.ssb_offset_point_a.tl.tag != 0
      || nrmac->config[0].ssb_table.ssb_offset_point_a.value != 0) {
    out->has_ssb_offset = true;
    out->ssb_offset = (int64_t)nrmac->config[0].ssb_table.ssb_offset_point_a.value;
  }

  out->has_op_state = true;
  out->op_state = CCC_OP_STATE_ENABLED;
  out->has_admin_state = true;
  out->admin_state = CCC_ADMIN_STATE_UNLOCKED;
  out->has_cell_state = true;
  out->cell_state = CCC_CELL_STATE_ACTIVE;

  // pLMNInfoList
  if (cell_info != NULL) {
    ccc_plmn_info_t* pi = calloc(1, sizeof(*pi));
    assert(pi != NULL && "Memory exhausted");
    snprintf(pi->plmn.mcc, sizeof(pi->plmn.mcc), "%03d", cell_info->plmn.mcc);
    if (cell_info->plmn.mnc_digit_length == 3)
      snprintf(pi->plmn.mnc, sizeof(pi->plmn.mnc), "%03d", cell_info->plmn.mnc);
    else
      snprintf(pi->plmn.mnc, sizeof(pi->plmn.mnc), "%02d", cell_info->plmn.mnc);

    if (cell_info->num_ssi > 0) {
      pi->n_snssai = cell_info->num_ssi;
      pi->snssai = calloc(pi->n_snssai, sizeof(*pi->snssai));
      assert(pi->snssai != NULL && "Memory exhausted");
      for (size_t i = 0; i < pi->n_snssai; i++) {
        pi->snssai[i].sst = cell_info->nssai[i].sst;
        if (cell_info->nssai[i].sd != 0xffffff) {
          pi->snssai[i].has_sd = true;
          // 24-bit SD as 6-hex string per spec §9.3.18.
          snprintf(pi->snssai[i].sd, sizeof(pi->snssai[i].sd),
                   "%06x", cell_info->nssai[i].sd & 0xffffff);
        }
      }
    }

    out->n_plmn_info = 1;
    out->plmn_info = pi;
  }

  out->n_partitions = 0;
  out->partitions = NULL;

  if (scc->downlinkConfigCommon != NULL
      && scc->downlinkConfigCommon->frequencyInfoDL != NULL
      && scc->downlinkConfigCommon->frequencyInfoDL->scs_SpecificCarrierList.list.count > 0) {
    NR_SCS_SpecificCarrier_t const* scs = scc->downlinkConfigCommon->frequencyInfoDL->scs_SpecificCarrierList.list.array[0];

    ccc_o_bwp_t* bwp = calloc(1, sizeof(*bwp));
    assert(bwp != NULL && "Memory exhausted");

    bwp->has_bwp_context = true;
    bwp->bwp_context = CCC_BWP_CTX_DL;

    bwp->has_is_initial_bwp = true;
    bwp->is_initial_bwp = CCC_BWP_INIT_INITIAL;

    bwp->has_sub_carrier_spacing = true;
    bwp->sub_carrier_spacing = nr_scs_enum_to_khz(scs->subcarrierSpacing);

    bwp->has_cyclic_prefix = true;
    bwp->cyclic_prefix = CCC_CP_NORMAL;

    bwp->has_start_rb = true;
    bwp->start_rb = (int64_t)scs->offsetToCarrier;

    bwp->has_number_of_rbs = true;
    bwp->number_of_rbs = (int64_t)scs->carrierBandwidth;

    out->n_bwps = 1;
    out->bwps = bwp;
  }
}
#else
static void fill_o_nr_cell_du(ccc_o_nr_cell_du_t* out)
{
  (void)out;  
}
#endif


static void filter_values_by_action(ccc_o_nr_cell_du_t* v,
                                    ccc_ran_cfg_struct_for_adf_t const* req)
{
  if (req == NULL || req->n_attrs == 0) return;

  bool want_cell_local_id = false;
  bool want_op_state = false;
  bool want_admin_state = false;
  bool want_cell_state = false;
  bool want_plmn_info = false;
  bool want_nr_pci = false;
  bool want_nr_tac = false;
  bool want_arfcn_dl = false;
  bool want_arfcn_ul = false;
  bool want_arfcn_sul = false;
  bool want_bs_channel_bw_dl = false;
  bool want_ssb_frequency = false;
  bool want_ssb_periodicity = false;
  bool want_ssb_scs = false;
  bool want_ssb_offset = false;
  bool want_ssb_duration = false;
  bool want_bs_channel_bw_ul = false;
  bool want_bs_channel_bw_sul = false;
  bool want_bwp_list = false;
  bool want_partition_list = false;

  for (size_t i = 0; i < req->n_attrs; i++) {
    const char* n = req->attrs[i].name;
    if      (strcmp(n, "cellLocalId") == 0)          want_cell_local_id = true;
    else if (strcmp(n, "operationalState") == 0)     want_op_state = true;
    else if (strcmp(n, "administrativeState") == 0)  want_admin_state = true;
    else if (strcmp(n, "cellState") == 0)            want_cell_state = true;
    else if (strcmp(n, "pLMNInfoList") == 0)         want_plmn_info = true;
    else if (strcmp(n, "nRPCI") == 0)                want_nr_pci = true;
    else if (strcmp(n, "nRTAC") == 0)                want_nr_tac = true;
    else if (strcmp(n, "arfcnDL") == 0)              want_arfcn_dl = true;
    else if (strcmp(n, "arfcnUL") == 0)              want_arfcn_ul = true;
    else if (strcmp(n, "arfcnSUL") == 0)             want_arfcn_sul = true;
    else if (strcmp(n, "bSChannelBwDL") == 0)        want_bs_channel_bw_dl = true;
    else if (strcmp(n, "ssbFrequency") == 0)         want_ssb_frequency = true;
    else if (strcmp(n, "ssbPeriodicity") == 0)       want_ssb_periodicity = true;
    else if (strcmp(n, "ssbSubCarrierSpacing") == 0) want_ssb_scs = true;
    else if (strcmp(n, "ssbOffset") == 0)            want_ssb_offset = true;
    else if (strcmp(n, "ssbDuration") == 0)          want_ssb_duration = true;
    else if (strcmp(n, "bSChannelBwUL") == 0)        want_bs_channel_bw_ul = true;
    else if (strcmp(n, "bSChannelBwSUL") == 0)       want_bs_channel_bw_sul = true;
    else if (strcmp(n, "bWPList") == 0)              want_bwp_list = true;
    else if (strcmp(n, "partitionList") == 0)        want_partition_list = true;
  }

  if (!want_cell_local_id)      v->has_cell_local_id = false;
  if (!want_op_state)           v->has_op_state = false;
  if (!want_admin_state)        v->has_admin_state = false;
  if (!want_cell_state)         v->has_cell_state = false;
  if (!want_nr_pci)             v->has_nr_pci = false;
  if (!want_nr_tac)             v->has_nr_tac = false;
  if (!want_arfcn_dl)           v->has_arfcn_dl = false;
  if (!want_arfcn_ul)           v->has_arfcn_ul = false;
  if (!want_arfcn_sul)          v->has_arfcn_sul = false;
  if (!want_bs_channel_bw_dl)   v->has_bs_channel_bw_dl = false;
  if (!want_ssb_frequency)      v->has_ssb_frequency = false;
  if (!want_ssb_periodicity)    v->has_ssb_periodicity = false;
  if (!want_ssb_scs)            v->has_ssb_sub_carrier_spacing = false;
  if (!want_ssb_offset)         v->has_ssb_offset = false;
  if (!want_ssb_duration)       v->has_ssb_duration = false;
  if (!want_bs_channel_bw_ul)   v->has_bs_channel_bw_ul = false;
  if (!want_bs_channel_bw_sul)  v->has_bs_channel_bw_sul = false;

  if (!want_plmn_info) {
    for (size_t i = 0; i < v->n_plmn_info; i++) {
      if (v->plmn_info[i].snssai != NULL) free(v->plmn_info[i].snssai);
    }
    if (v->plmn_info != NULL) free(v->plmn_info);
    v->plmn_info = NULL;
    v->n_plmn_info = 0;
  }
  if (!want_bwp_list) {
    if (v->bwps != NULL) free(v->bwps);
    v->bwps = NULL;
    v->n_bwps = 0;
  }
  if (!want_partition_list) {
    for (size_t i = 0; i < v->n_partitions; i++) {
      if (v->partitions[i].ranges != NULL) free(v->partitions[i].ranges);
    }
    if (v->partitions != NULL) free(v->partitions);
    v->partitions = NULL;
    v->n_partitions = 0;
  }
}

bool read_ccc_sm(void* data)
{
  assert(data != NULL);
  ccc_rd_ind_data_t* rd = (ccc_rd_ind_data_t*)data;

  rd->ind.hdr.indication_reason = CCC_IND_REASON_PERIODIC;
  fill_event_time(rd->ind.hdr.event_time, sizeof(rd->ind.hdr.event_time));

  ccc_cell_reported_t* cell = calloc(1, sizeof(*cell));
  assert(cell != NULL && "Memory exhausted");
  fill_cell_global_id(&cell->cell_global_id);

  ccc_config_structure_reported_t* s = calloc(1, sizeof(*s));
  assert(s != NULL && "Memory exhausted");
  s->change_type = CCC_CHANGE_NONE;
  snprintf(s->ran_cfg_structure_name, sizeof(s->ran_cfg_structure_name), "%s", "O-NRCellDU");
  fill_o_nr_cell_du(&s->values);

  if (rd->act_def != NULL
      && rd->act_def->format == CCC_ACT_DEF_FORMAT_2
      && rd->act_def->fmt2.n_cells > 0
      && rd->act_def->fmt2.cells[0].n_structures > 0) {
    filter_values_by_action(&s->values, &rd->act_def->fmt2.cells[0].structures[0]);
  }

  cell->n_structures = 1;
  cell->structures = s;

  rd->ind.msg.format = CCC_IND_MSG_FORMAT_2;
  rd->ind.msg.fmt2.n_cells = 1;
  rd->ind.msg.fmt2.cells = cell;

  return true;
}

static ccc_supported_attribute_t* make_attrs(const char* const* names, size_t n)
{
  ccc_supported_attribute_t* arr = calloc(n, sizeof(*arr));
  assert(arr != NULL && "Memory exhausted");
  for (size_t i = 0; i < n; i++) {
    snprintf(arr[i].name, sizeof(arr[i].name), "%s", names[i]);
    arr[i].supported_services_mask = 0x1;  // REPORT
  }
  return arr;
}

void read_ccc_setup_sm(void* data)
{
  assert(data != NULL);
  ccc_e2_setup_t* setup = (ccc_e2_setup_t*)data;

  ccc_cell_for_ran_func_def_t* cell = calloc(1, sizeof(*cell));
  assert(cell != NULL && "Memory exhausted");
  fill_cell_global_id(&cell->cell_global_id);

  static const char* const onrcell_du_attrs[] = {
    "cellLocalId",
    "operationalState",
    "administrativeState",
    "cellState",
    "pLMNInfoList",
    "nRPCI",
    "nRTAC",
    "arfcnDL",
    "arfcnUL",
    "arfcnSUL",
    "bSChannelBwDL",
    "ssbFrequency",
    "ssbPeriodicity",
    "ssbSubCarrierSpacing",
    "ssbOffset",
    "ssbDuration",
    "bSChannelBwUL",
    "bSChannelBwSUL",
    "bWPList",
    "partitionList",
  };
  ccc_supported_struct_t* sup = calloc(1, sizeof(*sup));
  assert(sup != NULL && "Memory exhausted");
  snprintf(sup->ran_cfg_structure_name, sizeof(sup->ran_cfg_structure_name), "%s", "O-NRCellDU");
  sup->n_attrs = sizeof(onrcell_du_attrs) / sizeof(onrcell_du_attrs[0]);
  sup->attrs = make_attrs(onrcell_du_attrs, sup->n_attrs);

  cell->n_structures = 1;
  cell->structures = sup;

  setup->ran_func_def.n_cells = 1;
  setup->ran_func_def.cells = cell;
}
