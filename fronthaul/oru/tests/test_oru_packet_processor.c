/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <rte_common.h>
#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_byteorder.h>
#include "xran_pkt_api.h"
#include "oru_packet_processor.h"

#include "log.h"
#include "common/config/config_userapi.h"

// OAI Linkage Satisfiers
void exit_function(const char *file, const char *function, const int line, const char *s, const int assertflag)
{
  fprintf(stderr, "Error at %s:%s:%d - %s\n", file, function, line, s ? s : "None");
  exit(1);
}
configmodule_interface_t *uniqCfg = NULL;

/* Global config for tests, matches processor init */
struct xran_eaxcid_config g_eaxcid_config = {.mask_cuPortId = 0xF000,
                                             .mask_bandSectorId = 0x0F00,
                                             .mask_ccId = 0x00F0,
                                             .mask_ruPortId = 0x000F,
                                             .bit_cuPortId = 12,
                                             .bit_bandSectorId = 8,
                                             .bit_ccId = 4,
                                             .bit_ruPortId = 0};

struct rte_mempool *mp = NULL;

void setup_dpdk(int argc, char **argv)
{
  int ret = rte_eal_init(argc, argv);
  assert(ret >= 0);
  logInit();
  mp = rte_pktmbuf_pool_create("test_pool", 1024, 0, 0, 10000, rte_socket_id());
  assert(mp != NULL);
}

void *test_alloc_mbuf(void *io_controller)
{
  return rte_pktmbuf_alloc(mp);
}

void *last_sent_mbuf = NULL;
size_t last_sent_size = 0;

void test_send_mbuf(void *io_controller, struct rte_mbuf **mbufs, uint32_t num_mbufs)
{
  for (uint32_t i = 0; i < num_mbufs; i++) {
    if (last_sent_mbuf) {
      rte_pktmbuf_free(last_sent_mbuf);
    }
    last_sent_mbuf = mbufs[i];
    last_sent_size = rte_pktmbuf_pkt_len(mbufs[i]) + 14;
  }
}

void test_init_cleanup()
{
  printf("Testing init and cleanup...\n");
  void *ctx = init_packet_processor(1, 273, 200, 400, 100, 300, 2, 2, 0, 0, 5, test_alloc_mbuf, test_send_mbuf, NULL, 1500, 0);
  assert(ctx != NULL);
  oru_packet_processor_stats_t stats;
  get_packet_processor_stats(ctx, &stats);
  assert(stats.dl_tdd_mismatch == 0);
  assert(stats.ul_tdd_mismatch == 0);
  cleanup_packet_processor(ctx);
  printf("Init/cleanup passed!\n");
}

void test_cplane_timing_errors()
{
  printf("Testing C-Plane timing errors...\n");
  void *ctx = init_packet_processor(1, 273, 200, 400, 100, 300, 2, 2, 0, 0, 5, test_alloc_mbuf, test_send_mbuf, NULL, 1500, 0);
  assert(ctx != NULL);

  // Set current symbol
  uint64_t current_sym = 1000;
  handle_absolute_symbol_tick(ctx, current_sym);

  struct rte_mbuf *mbuf = rte_pktmbuf_alloc(mp);
  assert(mbuf != NULL);

  // Create eCPRI header
  struct xran_ecpri_hdr *ecpri = (struct xran_ecpri_hdr *)rte_pktmbuf_append(mbuf, sizeof(struct xran_ecpri_hdr));
  ecpri->ecpri_xtc_id = xran_compose_cid(&g_eaxcid_config, 0, 0, 0, 0);
  ecpri->ecpri_seq_id.bits.seq_id = 1;

  // Create App Header (early packet)
  struct xran_cp_radioapp_section1_header *apphdr =
      (struct xran_cp_radioapp_section1_header *)rte_pktmbuf_append(mbuf, sizeof(struct xran_cp_radioapp_section1_header));
  memset(apphdr, 0, sizeof(*apphdr));
  apphdr->cmnhdr.field.dataDirection = XRAN_DIR_DL;
  apphdr->cmnhdr.field.payloadVer = XRAN_PAYLOAD_VER;
  // Let's target a symbol in the future that is early
  int num_symbols_per_frame = 10 * (1 << 1) * 14;
  uint64_t target_sym = current_sym + 50; // Early
  apphdr->cmnhdr.field.frameId = (target_sym / num_symbols_per_frame) % 256;
  int slot_in_frame = (target_sym % num_symbols_per_frame) / 14;
  apphdr->cmnhdr.field.subframeId = slot_in_frame / (1 << 1);
  apphdr->cmnhdr.field.slotId = slot_in_frame % (1 << 1);
  apphdr->cmnhdr.field.startSymbolId = target_sym % 14;
  apphdr->cmnhdr.sectionType = XRAN_CP_SECTIONTYPE_1;
  apphdr->cmnhdr.field.all_bits = rte_cpu_to_be_32(apphdr->cmnhdr.field.all_bits);

  struct xran_cp_radioapp_section1 *sec =
      (struct xran_cp_radioapp_section1 *)rte_pktmbuf_append(mbuf, sizeof(struct xran_cp_radioapp_section1));
  memset(sec, 0, sizeof(*sec));
  sec->hdr.u.s1.numSymbol = 1;
  sec->hdr.u1.common.numPrbc = 0;
  // Need to handle endianness for section
  *((uint64_t *)sec) = rte_be_to_cpu_64(*((uint64_t *)sec));

  // Send early packet
  handle_cplane_packet(ctx, mbuf);

  oru_packet_processor_stats_t stats;
  get_packet_processor_stats(ctx, &stats);
  assert(stats.cplane_err_early == 1);
  assert(stats.dl_tdd_mismatch == 0);
  assert(stats.ul_tdd_mismatch == 0);

  cleanup_packet_processor(ctx);
  printf("C-Plane timing errors passed!\n");
}

void test_cplane_uplane_match()
{
  printf("Testing C-Plane / U-Plane match...\n");
  int mu = 1; // 30kHz
  int slots_per_subframe = 1 << mu;
  // T2a_cp ranges: 200 to 400 us (approx 5 to 11 symbols)
  // T2a_up ranges: 100 to 300 us (approx 2 to 8 symbols)
  void *ctx = init_packet_processor(mu, 273, 200, 400, 100, 300, 2, 2, 0, 0, 5, test_alloc_mbuf, test_send_mbuf, NULL, 1500, 0);
  assert(ctx != NULL);

  uint64_t current_sym = 1000;
  handle_absolute_symbol_tick(ctx, current_sym);

  uint64_t target_sym = current_sym + 7; // Good for C-plane (between 5 and 11)

  // 1. C-plane packet
  struct rte_mbuf *c_mbuf = rte_pktmbuf_alloc(mp);
  struct xran_ecpri_hdr *ecpri = (struct xran_ecpri_hdr *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_ecpri_hdr));
  ecpri->ecpri_xtc_id = xran_compose_cid(&g_eaxcid_config, 0, 0, 0, 0); // Ant 0

  struct xran_cp_radioapp_section1_header *apphdr =
      (struct xran_cp_radioapp_section1_header *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_cp_radioapp_section1_header));
  memset(apphdr, 0, sizeof(*apphdr));
  apphdr->cmnhdr.field.dataDirection = XRAN_DIR_DL;
  apphdr->cmnhdr.field.payloadVer = XRAN_PAYLOAD_VER;

  int num_symbols_per_frame = 10 * slots_per_subframe * 14;
  apphdr->cmnhdr.field.frameId = (target_sym / num_symbols_per_frame) % 256;
  int slot_in_frame = (target_sym % num_symbols_per_frame) / 14;
  apphdr->cmnhdr.field.subframeId = slot_in_frame / slots_per_subframe;
  apphdr->cmnhdr.field.slotId = slot_in_frame % slots_per_subframe;
  apphdr->cmnhdr.field.startSymbolId = target_sym % 14;
  apphdr->cmnhdr.sectionType = XRAN_CP_SECTIONTYPE_1;
  apphdr->cmnhdr.field.all_bits = rte_cpu_to_be_32(apphdr->cmnhdr.field.all_bits);

  struct xran_cp_radioapp_section1 *sec =
      (struct xran_cp_radioapp_section1 *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_cp_radioapp_section1));
  memset(sec, 0, sizeof(*sec));
  sec->hdr.u.s1.numSymbol = 1;
  sec->hdr.u1.common.numPrbc = 1;
  *((uint64_t *)sec) = rte_be_to_cpu_64(*((uint64_t *)sec));

  handle_cplane_packet(ctx, c_mbuf);

  oru_packet_processor_stats_t stats;
  get_packet_processor_stats(ctx, &stats);
  assert(stats.cplane_err_early == 0);
  assert(stats.cplane_err_late == 0);
  assert(stats.total_cplane == 1);
  assert(stats.dl_tdd_mismatch == 0);
  assert(stats.ul_tdd_mismatch == 0);

  // Advance time so U-plane is in valid range (target_sym = current_sym + 4 -> 4 is between 2 and 8)
  current_sym += 3;
  handle_absolute_symbol_tick(ctx, current_sym);

  // 2. U-plane packet
  struct rte_mbuf *u_mbuf = rte_pktmbuf_alloc(mp);
  struct xran_ecpri_hdr *u_ecpri = (struct xran_ecpri_hdr *)rte_pktmbuf_append(u_mbuf, sizeof(struct xran_ecpri_hdr));
  u_ecpri->ecpri_xtc_id = xran_compose_cid(&g_eaxcid_config, 0, 0, 0, 0); // Ant 0

  struct radio_app_common_hdr *u_app =
      (struct radio_app_common_hdr *)rte_pktmbuf_append(u_mbuf, sizeof(struct radio_app_common_hdr));
  u_app->frame_id = (target_sym / num_symbols_per_frame) % 256;
  u_app->sf_slot_sym.subframe_id = slot_in_frame / slots_per_subframe;
  u_app->sf_slot_sym.slot_id = slot_in_frame % slots_per_subframe;
  u_app->sf_slot_sym.symb_id = target_sym % 14;
  u_app->sf_slot_sym.value = rte_cpu_to_be_16(u_app->sf_slot_sym.value);

  struct data_section_hdr *u_data = (struct data_section_hdr *)rte_pktmbuf_append(u_mbuf, sizeof(struct data_section_hdr));
  u_data->fields.num_prbu = 1; // 1 PRB for testing
  u_data->fields.start_prbu = 0;
  u_data->fields.sect_id = 0;
  u_data->fields.all_bits = rte_cpu_to_be_32(u_data->fields.all_bits);

  // IQ Data
  uint16_t *iq = (uint16_t *)rte_pktmbuf_append(u_mbuf, 1 * 12 * 4); // 1 PRB * 12 SC * 4 bytes
  assert(iq != NULL);
  iq[0] = 0xFF00;
  iq[1] = 0xEE11;
  iq[2] = 0xDD22;
  iq[3] = 0xCC33;

  handle_uplane_packet(ctx, u_mbuf);

  get_packet_processor_stats(ctx, &stats);
  assert(stats.uplane_err_early == 0);
  assert(stats.uplane_err_late == 0);
  assert(stats.dl_tdd_mismatch == 0);
  assert(stats.ul_tdd_mismatch == 0);

  // 3. Advance to trigger window expiry and job completion
  current_sym += 10;
  handle_absolute_symbol_tick(ctx, current_sym);

  uint32_t *txdataF[4] = {0};
  uint32_t output_iq[273 * 12] = {0};
  txdataF[0] = output_iq;

  int frame, slot, symbol;
  do {
    read_dl_iq(ctx, txdataF, 1, &frame, &slot, &symbol);
  } while (!(frame == (target_sym / num_symbols_per_frame) % 1024 && symbol == target_sym % 14));

  assert(symbol == target_sym % 14);
  uint16_t *out_iq = (uint16_t *)output_iq;
  assert(out_iq[0] == 0x00FF);
  assert(out_iq[1] == 0x11EE);
  assert(out_iq[2] == 0x22DD);
  assert(out_iq[3] == 0x33CC);

  cleanup_packet_processor(ctx);
  printf("C-Plane / U-Plane match passed!\n");
}

void test_frame_wrap_around()
{
  printf("Testing frame wrap around...\n");
  int mu = 1; // 30kHz
  int slots_per_subframe = 1 << mu;
  void *ctx = init_packet_processor(mu, 273, 200, 400, 100, 300, 2, 2, 0, 0, 5, test_alloc_mbuf, test_send_mbuf, NULL, 1500, 0);
  assert(ctx != NULL);

  // Total symbols = 5 slots * 14 = 70.
  // DL symbols: 0 to 2*14-1 = 27.
  // target_sym = 71680. 71680 % 70 = 0 (DL symbol)
  uint64_t current_sym = 256 * 14 * 10 * slots_per_subframe - 7;
  handle_absolute_symbol_tick(ctx, current_sym);

  int num_symbols_per_frame = 10 * slots_per_subframe * 14;
  uint64_t target_sym = current_sym + 7;

  // 1. C-plane packet
  struct rte_mbuf *c_mbuf = rte_pktmbuf_alloc(mp);
  struct xran_ecpri_hdr *ecpri = (struct xran_ecpri_hdr *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_ecpri_hdr));
  ecpri->ecpri_xtc_id = xran_compose_cid(&g_eaxcid_config, 0, 0, 0, 0);

  struct xran_cp_radioapp_section1_header *apphdr =
      (struct xran_cp_radioapp_section1_header *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_cp_radioapp_section1_header));
  memset(apphdr, 0, sizeof(*apphdr));
  apphdr->cmnhdr.field.dataDirection = XRAN_DIR_DL;
  apphdr->cmnhdr.field.payloadVer = XRAN_PAYLOAD_VER;

  apphdr->cmnhdr.field.frameId = (target_sym / num_symbols_per_frame) % 256;
  int slot_in_frame = (target_sym % num_symbols_per_frame) / 14;
  apphdr->cmnhdr.field.subframeId = slot_in_frame / slots_per_subframe;
  apphdr->cmnhdr.field.slotId = slot_in_frame % slots_per_subframe;
  apphdr->cmnhdr.field.startSymbolId = target_sym % 14;
  apphdr->cmnhdr.sectionType = XRAN_CP_SECTIONTYPE_1;
  apphdr->cmnhdr.field.all_bits = rte_cpu_to_be_32(apphdr->cmnhdr.field.all_bits);

  struct xran_cp_radioapp_section1 *sec =
      (struct xran_cp_radioapp_section1 *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_cp_radioapp_section1));
  memset(sec, 0, sizeof(*sec));
  sec->hdr.u.s1.numSymbol = 1;
  sec->hdr.u1.common.numPrbc = 1;
  *((uint64_t *)sec) = rte_be_to_cpu_64(*((uint64_t *)sec));

  handle_cplane_packet(ctx, c_mbuf);

  oru_packet_processor_stats_t stats;
  get_packet_processor_stats(ctx, &stats);
  assert(stats.cplane_err_early == 0);
  assert(stats.cplane_err_late == 0);
  assert(stats.total_cplane == 1);
  assert(stats.dl_tdd_mismatch == 0);
  assert(stats.ul_tdd_mismatch == 0);

  // Advance time so U-plane is in valid range
  current_sym += 3; // 279
  handle_absolute_symbol_tick(ctx, current_sym);

  // 2. U-plane packet
  struct rte_mbuf *u_mbuf = rte_pktmbuf_alloc(mp);
  struct xran_ecpri_hdr *u_ecpri = (struct xran_ecpri_hdr *)rte_pktmbuf_append(u_mbuf, sizeof(struct xran_ecpri_hdr));
  u_ecpri->ecpri_xtc_id = xran_compose_cid(&g_eaxcid_config, 0, 0, 0, 0); // Ant 0

  struct radio_app_common_hdr *u_app =
      (struct radio_app_common_hdr *)rte_pktmbuf_append(u_mbuf, sizeof(struct radio_app_common_hdr));
  u_app->frame_id = (target_sym / num_symbols_per_frame) % 256;
  u_app->sf_slot_sym.subframe_id = slot_in_frame / slots_per_subframe;
  u_app->sf_slot_sym.slot_id = slot_in_frame % slots_per_subframe;
  u_app->sf_slot_sym.symb_id = target_sym % 14;
  u_app->sf_slot_sym.value = rte_cpu_to_be_16(u_app->sf_slot_sym.value);

  struct data_section_hdr *u_data = (struct data_section_hdr *)rte_pktmbuf_append(u_mbuf, sizeof(struct data_section_hdr));
  u_data->fields.num_prbu = 1; // 1 PRB for testing
  u_data->fields.start_prbu = 0;
  u_data->fields.sect_id = 0;
  u_data->fields.all_bits = rte_cpu_to_be_32(u_data->fields.all_bits);

  handle_uplane_packet(ctx, u_mbuf);

  get_packet_processor_stats(ctx, &stats);
  assert(stats.uplane_err_early == 0);
  assert(stats.uplane_err_late == 0);
  assert(stats.dl_tdd_mismatch == 0);
  assert(stats.ul_tdd_mismatch == 0);

  // 3. Advance to trigger window expiry and job completion
  current_sym += 10;
  handle_absolute_symbol_tick(ctx, current_sym);

  uint32_t *txdataF[4] = {0};
  uint32_t output_iq[273 * 12] = {0};
  txdataF[0] = output_iq;

  int frame, slot, symbol;
  do {
    read_dl_iq(ctx, txdataF, 1, &frame, &slot, &symbol);
  } while (!(frame == (target_sym / num_symbols_per_frame) % 1024 && symbol == target_sym % 14));

  assert(frame == (target_sym / num_symbols_per_frame) % 1024);
  assert(symbol == target_sym % 14);

  cleanup_packet_processor(ctx);
  printf("Frame wrap around passed!\n");
}

void test_cplane_14_symbols()
{
  printf("Testing 1 C-plane message for 14 symbols...\n");
  int mu = 1; // 30kHz
  void *ctx = init_packet_processor(mu, 273, 200, 400, 100, 300, 5, 0, 0, 0, 5, test_alloc_mbuf, test_send_mbuf, NULL, 1500, 0);
  assert(ctx != NULL);

  uint64_t current_sym = 1000;
  handle_absolute_symbol_tick(ctx, current_sym);

  uint64_t target_sym = current_sym + 7; // Good for C-plane (between 5 and 11)

  // 1. C-plane packet with numSymbol = 14
  struct rte_mbuf *c_mbuf = rte_pktmbuf_alloc(mp);
  struct xran_ecpri_hdr *ecpri = (struct xran_ecpri_hdr *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_ecpri_hdr));
  ecpri->ecpri_xtc_id = xran_compose_cid(&g_eaxcid_config, 0, 0, 0, 0); // Ant 0

  struct xran_cp_radioapp_section1_header *apphdr =
      (struct xran_cp_radioapp_section1_header *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_cp_radioapp_section1_header));
  memset(apphdr, 0, sizeof(*apphdr));
  apphdr->cmnhdr.field.dataDirection = XRAN_DIR_DL;
  apphdr->cmnhdr.field.payloadVer = XRAN_PAYLOAD_VER;

  int num_symbols_per_frame = 10 * 2 * 14;
  apphdr->cmnhdr.field.frameId = (target_sym / num_symbols_per_frame) % 256;
  int slot_in_frame = (target_sym % num_symbols_per_frame) / 14;
  apphdr->cmnhdr.field.subframeId = slot_in_frame / 2;
  apphdr->cmnhdr.field.slotId = slot_in_frame % 2;
  apphdr->cmnhdr.field.startSymbolId = target_sym % 14;
  apphdr->cmnhdr.sectionType = XRAN_CP_SECTIONTYPE_1;
  apphdr->cmnhdr.field.all_bits = rte_cpu_to_be_32(apphdr->cmnhdr.field.all_bits);

  struct xran_cp_radioapp_section1 *sec =
      (struct xran_cp_radioapp_section1 *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_cp_radioapp_section1));
  memset(sec, 0, sizeof(*sec));
  sec->hdr.u.s1.numSymbol = 14;
  sec->hdr.u1.common.numPrbc = 1;
  *((uint64_t *)sec) = rte_be_to_cpu_64(*((uint64_t *)sec));

  handle_cplane_packet(ctx, c_mbuf);

  oru_packet_processor_stats_t stats;
  get_packet_processor_stats(ctx, &stats);
  assert(stats.cplane_err_early == 0);
  assert(stats.cplane_err_late == 0);
  assert(stats.total_cplane == 1);
  assert(stats.dl_tdd_mismatch == 0);
  assert(stats.ul_tdd_mismatch == 0);

  // 2. Advance time so we can send U-plane for ALL 14 symbols
  current_sym += 3;
  handle_absolute_symbol_tick(ctx, current_sym);

  for (int i = 0; i < 14; i++) {
    struct rte_mbuf *u_mbuf = rte_pktmbuf_alloc(mp);
    struct xran_ecpri_hdr *u_ecpri = (struct xran_ecpri_hdr *)rte_pktmbuf_append(u_mbuf, sizeof(struct xran_ecpri_hdr));
    u_ecpri->ecpri_xtc_id = xran_compose_cid(&g_eaxcid_config, 0, 0, 0, 0); // Ant 0

    struct radio_app_common_hdr *u_app =
        (struct radio_app_common_hdr *)rte_pktmbuf_append(u_mbuf, sizeof(struct radio_app_common_hdr));
    uint64_t sym_i = target_sym + i;
    u_app->frame_id = (sym_i / num_symbols_per_frame) % 256;
    int sf_slot_in_frame = (sym_i % num_symbols_per_frame) / 14;
    u_app->sf_slot_sym.subframe_id = sf_slot_in_frame / 2;
    u_app->sf_slot_sym.slot_id = sf_slot_in_frame % 2;
    u_app->sf_slot_sym.symb_id = sym_i % 14;
    u_app->sf_slot_sym.value = rte_cpu_to_be_16(u_app->sf_slot_sym.value);

    struct data_section_hdr *u_data = (struct data_section_hdr *)rte_pktmbuf_append(u_mbuf, sizeof(struct data_section_hdr));
    u_data->fields.num_prbu = 1; // 1 PRB for testing
    u_data->fields.start_prbu = 0;
    u_data->fields.sect_id = 0;
    u_data->fields.all_bits = rte_cpu_to_be_32(u_data->fields.all_bits);

    // IQ Data
    uint16_t *iq = (uint16_t *)rte_pktmbuf_append(u_mbuf, 1 * 12 * 4); // 1 PRB * 12 SC * 4 bytes
    assert(iq != NULL);
    iq[0] = rte_cpu_to_be_16(i);

    handle_uplane_packet(ctx, u_mbuf);

    // Advance tick so next symbol's U-plane is timely
    current_sym++;
    handle_absolute_symbol_tick(ctx, current_sym);
  }

  get_packet_processor_stats(ctx, &stats);
  assert(stats.uplane_err_early == 0);
  assert(stats.uplane_err_late == 0);
  assert(stats.dl_tdd_mismatch == 0);
  assert(stats.ul_tdd_mismatch == 0);

  // 3. Advance to trigger window expiry and job completion
  current_sym += 15;
  handle_absolute_symbol_tick(ctx, current_sym);

  uint32_t *txdataF[4] = {0};
  uint32_t output_iq[273 * 12] = {0};
  txdataF[0] = output_iq;

  for (int i = 0; i < 14; i++) {
    int frame, slot, symbol;
    uint64_t sym_i = target_sym + i;
    do {
      read_dl_iq(ctx, txdataF, 1, &frame, &slot, &symbol);
    } while (!(frame == (sym_i / num_symbols_per_frame) % 1024 && symbol == sym_i % 14));

    assert(frame == (sym_i / num_symbols_per_frame) % 1024);
    assert(symbol == sym_i % 14);
    uint16_t *out_iq = (uint16_t *)output_iq;
    assert(out_iq[0] == i);
  }

  cleanup_packet_processor(ctx);
  printf("1 C-plane message for 14 symbols passed!\n");
}

void test_other_bw_4ant_prb_offset()
{
  printf("Testing other bandwidth, 4 antennas, and PRB offset...\n");
  int mu = 1; // 30kHz
  int num_prb = 106; // Different bandwidth
  void *ctx = init_packet_processor(mu, num_prb, 200, 400, 100, 300, 4, 4, 0, 0, 5, test_alloc_mbuf, test_send_mbuf, NULL, 1500, 0);
  assert(ctx != NULL);

  uint64_t current_sym = 1000;
  handle_absolute_symbol_tick(ctx, current_sym);

  uint64_t target_sym = current_sym + 7;

  int num_symbols_per_frame = 10 * 2 * 14;
  int frameId = (target_sym / num_symbols_per_frame) % 256;
  int slot_in_frame = (target_sym % num_symbols_per_frame) / 14;
  int subframeId = slot_in_frame / 2;
  int slotId = slot_in_frame % 2;
  int startSymbolId = target_sym % 14;

  // Send 1 C-plane packet for ALL 4 antennas using eAxC ID
  // Actually, O-RAN allows 1 C-plane packet per antenna, let's just send 4 C-plane packets.
  for (int ant = 0; ant < 4; ant++) {
    struct rte_mbuf *c_mbuf = rte_pktmbuf_alloc(mp);
    struct xran_ecpri_hdr *ecpri = (struct xran_ecpri_hdr *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_ecpri_hdr));
    ecpri->ecpri_xtc_id = xran_compose_cid(&g_eaxcid_config, 0, 0, 0, ant);

    struct xran_cp_radioapp_section1_header *apphdr =
        (struct xran_cp_radioapp_section1_header *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_cp_radioapp_section1_header));
    memset(apphdr, 0, sizeof(*apphdr));
    apphdr->cmnhdr.field.dataDirection = XRAN_DIR_DL;
    apphdr->cmnhdr.field.payloadVer = XRAN_PAYLOAD_VER;
    apphdr->cmnhdr.field.frameId = frameId;
    apphdr->cmnhdr.field.subframeId = subframeId;
    apphdr->cmnhdr.field.slotId = slotId;
    apphdr->cmnhdr.field.startSymbolId = startSymbolId;
    apphdr->cmnhdr.sectionType = XRAN_CP_SECTIONTYPE_1;
    apphdr->cmnhdr.field.all_bits = rte_cpu_to_be_32(apphdr->cmnhdr.field.all_bits);

    struct xran_cp_radioapp_section1 *sec =
        (struct xran_cp_radioapp_section1 *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_cp_radioapp_section1));
    memset(sec, 0, sizeof(*sec));
    sec->hdr.u.s1.numSymbol = 1;
    sec->hdr.u1.common.startPrbc = 20; // PRB offset
    sec->hdr.u1.common.numPrbc = 20; // subset of PRBs
    *((uint64_t *)sec) = rte_be_to_cpu_64(*((uint64_t *)sec));
    handle_cplane_packet(ctx, c_mbuf);
  }

  current_sym += 3;
  handle_absolute_symbol_tick(ctx, current_sym);

  // Send U-plane packets for all 4 antennas
  for (int ant = 0; ant < 4; ant++) {
    struct rte_mbuf *u_mbuf = rte_pktmbuf_alloc(mp);
    struct xran_ecpri_hdr *u_ecpri = (struct xran_ecpri_hdr *)rte_pktmbuf_append(u_mbuf, sizeof(struct xran_ecpri_hdr));
    u_ecpri->ecpri_xtc_id = xran_compose_cid(&g_eaxcid_config, 0, 0, 0, ant);

    struct radio_app_common_hdr *u_app =
        (struct radio_app_common_hdr *)rte_pktmbuf_append(u_mbuf, sizeof(struct radio_app_common_hdr));
    u_app->frame_id = frameId;
    u_app->sf_slot_sym.subframe_id = subframeId;
    u_app->sf_slot_sym.slot_id = slotId;
    u_app->sf_slot_sym.symb_id = startSymbolId;
    u_app->sf_slot_sym.value = rte_cpu_to_be_16(u_app->sf_slot_sym.value);

    struct data_section_hdr *u_data = (struct data_section_hdr *)rte_pktmbuf_append(u_mbuf, sizeof(struct data_section_hdr));
    u_data->fields.num_prbu = 20;
    u_data->fields.start_prbu = 20; // Match C-plane
    u_data->fields.sect_id = 0;
    u_data->fields.all_bits = rte_cpu_to_be_32(u_data->fields.all_bits);

    uint16_t *iq = (uint16_t *)rte_pktmbuf_append(u_mbuf, 20 * 12 * 4); // 20 PRBs
    assert(iq != NULL);
    for (int j = 0; j < 20 * 12 * 2; j++) {
      iq[j] = rte_cpu_to_be_16(ant + 1); // Specific pattern per antenna
    }

    handle_uplane_packet(ctx, u_mbuf);
  }

  current_sym += 10;
  handle_absolute_symbol_tick(ctx, current_sym);

  uint32_t *txdataF[4] = {0};
  uint32_t out_iq0[106 * 12] = {0};
  uint32_t out_iq1[106 * 12] = {0};
  uint32_t out_iq2[106 * 12] = {0};
  uint32_t out_iq3[106 * 12] = {0};
  txdataF[0] = out_iq0;
  txdataF[1] = out_iq1;
  txdataF[2] = out_iq2;
  txdataF[3] = out_iq3;

  int frame, slot, symbol;
  do {
    read_dl_iq(ctx, txdataF, 4, &frame, &slot, &symbol);
  } while (!(frame == frameId && symbol == startSymbolId));

  // Verify memory contents for each antenna
  for (int ant = 0; ant < 4; ant++) {
    uint16_t *out_iq = (uint16_t *)txdataF[ant];
    for (int prb = 0; prb < num_prb; prb++) {
      for (int sc = 0; sc < 12; sc++) {
        int idx = prb * 12 + sc;
        if (prb >= 20 && prb < 40) {
          assert(out_iq[2 * idx] == ant + 1);
          assert(out_iq[2 * idx + 1] == ant + 1);
        } else {
          // Unused PRBs should be zero
          assert(out_iq[2 * idx] == 0);
          assert(out_iq[2 * idx + 1] == 0);
        }
      }
    }
  }

  oru_packet_processor_stats_t stats;
  get_packet_processor_stats(ctx, &stats);
  assert(stats.dl_tdd_mismatch == 0);
  assert(stats.ul_tdd_mismatch == 0);

  cleanup_packet_processor(ctx);
  printf("Other bandwidth, 4 antennas, and PRB offset passed!\n");
}

int g_packets_sent = 0;

void test_send_mbuf_no_frag(void *io_controller, struct rte_mbuf **mbufs, uint32_t num_mbufs)
{
  for (uint32_t i = 0; i < num_mbufs; i++) {
    struct rte_mbuf *mbuf = mbufs[i];
    size_t size = rte_pktmbuf_pkt_len(mbuf) + 14;
    g_packets_sent++;
    struct xran_ecpri_hdr *sent_ecpri = rte_pktmbuf_mtod((struct rte_mbuf *)mbuf, struct xran_ecpri_hdr *);
    struct radio_app_common_hdr *sent_app = (struct radio_app_common_hdr *)(sent_ecpri + 1);
    struct data_section_hdr *sent_sec = (struct data_section_hdr *)(sent_app + 1);
    sent_sec->fields.all_bits = rte_be_to_cpu_32(sent_sec->fields.all_bits);

    size_t expected_header_len = sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr)
                                 + sizeof(struct data_section_hdr) + sizeof(struct data_section_compression_hdr);

    // In this test, we expect exactly 20 PRBs in a single packet
    assert(sent_sec->fields.num_prbu == (uint8_t)XRAN_CONVERT_NUMPRBC(20));
    assert(sent_sec->fields.start_prbu == 0);
    // size includes 14 bytes ethernet header
    assert(size == expected_header_len + 20 * 12 * 4 + 14);

    rte_pktmbuf_free(mbuf);
  }
}

void test_send_mbuf_prach(void *io_controller, struct rte_mbuf **mbufs, uint32_t num_mbufs)
{
  for (uint32_t i = 0; i < num_mbufs; i++) {
    struct rte_mbuf *mbuf = mbufs[i];
    size_t size = rte_pktmbuf_pkt_len(mbuf) + 14;
    g_packets_sent++;
    struct xran_ecpri_hdr *sent_ecpri = rte_pktmbuf_mtod((struct rte_mbuf *)mbuf, struct xran_ecpri_hdr *);
    struct radio_app_common_hdr *sent_app = (struct radio_app_common_hdr *)(sent_ecpri + 1);
    struct data_section_hdr *sent_sec = (struct data_section_hdr *)(sent_app + 1);
    sent_sec->fields.all_bits = rte_be_to_cpu_32(sent_sec->fields.all_bits);

    size_t expected_header_len =
        sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr) + sizeof(struct data_section_hdr);

    // In this test, we expect exactly 20 PRBs in the header
    assert(sent_sec->fields.num_prbu == (uint8_t)XRAN_CONVERT_NUMPRBC(20));
    assert(sent_sec->fields.start_prbu == 0);
    // size includes 14 bytes ethernet header. Data length is exactly 139 SCs (139 * 4 bytes).
    assert(size == expected_header_len + 139 * 4 + 14);

    rte_pktmbuf_free(mbuf);
  }
}

void test_send_mbuf_frag(void *io_controller, struct rte_mbuf **mbufs, uint32_t num_mbufs)
{
  for (uint32_t i = 0; i < num_mbufs; i++) {
    struct rte_mbuf *mbuf = mbufs[i];
    size_t size = rte_pktmbuf_pkt_len(mbuf) + 14;
    g_packets_sent++;
    struct xran_ecpri_hdr *sent_ecpri = rte_pktmbuf_mtod((struct rte_mbuf *)mbuf, struct xran_ecpri_hdr *);
    struct radio_app_common_hdr *sent_app = (struct radio_app_common_hdr *)(sent_ecpri + 1);
    struct data_section_hdr *sent_sec = (struct data_section_hdr *)(sent_app + 1);
    sent_sec->fields.all_bits = rte_be_to_cpu_32(sent_sec->fields.all_bits);

    size_t expected_header_len = sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr)
                                 + sizeof(struct data_section_hdr) + sizeof(struct data_section_compression_hdr);

    if (g_packets_sent == 1) {
      // First fragment: 30 PRBs
      assert(sent_sec->fields.num_prbu == (uint8_t)XRAN_CONVERT_NUMPRBC(30));
      assert(sent_sec->fields.start_prbu == 0);
      assert(size == expected_header_len + 30 * 12 * 4 + 14);
    } else if (g_packets_sent == 2) {
      // Second fragment: 10 PRBs
      assert(sent_sec->fields.num_prbu == (uint8_t)XRAN_CONVERT_NUMPRBC(10));
      assert(sent_sec->fields.start_prbu == 30);
      assert(size == expected_header_len + 10 * 12 * 4 + 14);
    }
    rte_pktmbuf_free(mbuf);
  }
}

void test_send_mbuf_large_mtu(void *io_controller, struct rte_mbuf **mbufs, uint32_t num_mbufs)
{
  for (uint32_t i = 0; i < num_mbufs; i++) {
    struct rte_mbuf *mbuf = mbufs[i];
    size_t size = rte_pktmbuf_pkt_len(mbuf) + 14;
    g_packets_sent++;
    struct xran_ecpri_hdr *sent_ecpri = rte_pktmbuf_mtod((struct rte_mbuf *)mbuf, struct xran_ecpri_hdr *);
    struct radio_app_common_hdr *sent_app = (struct radio_app_common_hdr *)(sent_ecpri + 1);
    struct data_section_hdr *sent_sec = (struct data_section_hdr *)(sent_app + 1);
    sent_sec->fields.all_bits = rte_be_to_cpu_32(sent_sec->fields.all_bits);

    size_t expected_header_len = sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr)
                                 + sizeof(struct data_section_hdr) + sizeof(struct data_section_compression_hdr);

    // With MTU 9600, 100 PRBs should fit in a single packet
    assert(g_packets_sent == 1);
    assert(sent_sec->fields.num_prbu == (uint8_t)XRAN_CONVERT_NUMPRBC(100));
    assert(sent_sec->fields.start_prbu == 0);

    // size should be trimmed to actual data, not full MTU
    size_t expected_data_len = 100 * 12 * 4;
    size_t expected_total_size = expected_header_len + expected_data_len + 14; // +14 for Ethernet
    assert(size == expected_total_size);
    assert(size < 9600); // Verify it's trimmed and not just MTU size

    rte_pktmbuf_free(mbuf);
  }
}

void test_send_mbuf_prb_offset(void *io_controller, struct rte_mbuf **mbufs, uint32_t num_mbufs)
{
  for (uint32_t i = 0; i < num_mbufs; i++) {
    struct rte_mbuf *mbuf = mbufs[i];
    g_packets_sent++;
    struct xran_ecpri_hdr *sent_ecpri = rte_pktmbuf_mtod((struct rte_mbuf *)mbuf, struct xran_ecpri_hdr *);
    struct radio_app_common_hdr *sent_app = (struct radio_app_common_hdr *)(sent_ecpri + 1);
    struct data_section_hdr *sent_sec = (struct data_section_hdr *)(sent_app + 1);
    sent_sec->fields.all_bits = rte_be_to_cpu_32(sent_sec->fields.all_bits);

    // We expect start_prbu = 10, num_prbu = 20
    assert(sent_sec->fields.start_prbu == 10);
    assert(sent_sec->fields.num_prbu == (uint8_t)XRAN_CONVERT_NUMPRBC(20));

    // Check data: should match the offset in txdataF
    uint32_t *iq =
        (uint32_t *)((uint8_t *)sent_sec + sizeof(struct data_section_hdr) + sizeof(struct data_section_compression_hdr));
    assert(rte_be_to_cpu_32(iq[0]) == 0x10101010);

    rte_pktmbuf_free(mbuf);
  }
}

void test_uplink_basic()
{
  printf("Testing basic uplink...\n");
  int mu = 1;
  int num_prb = 100;
  g_packets_sent = 0;
  void *ctx =
      init_packet_processor(mu, num_prb, 200, 400, 100, 300, 2, 2, 0, 0, 5, test_alloc_mbuf, test_send_mbuf_no_frag, NULL, 1500, 0);
  assert(ctx != NULL);

  // Pattern: 5 slots, 2 DL, 2 UL. Symbols 0-27 DL, 42-69 UL.
  uint64_t current_sym = 47; // 47 % 70 = 47 (UL)
  handle_absolute_symbol_tick(ctx, current_sym);

  uint64_t target_sym = current_sym + 5; // 52 % 70 = 52 (UL)
  int slots_per_subframe = 1 << mu;
  int num_symbols_per_frame = 10 * slots_per_subframe * 14;
  int frameId = (target_sym / num_symbols_per_frame) % 256;
  int slot_in_frame = (target_sym % num_symbols_per_frame) / 14;
  int subframeId = slot_in_frame / slots_per_subframe;
  int slotId = slot_in_frame % slots_per_subframe;
  int startSymbolId = target_sym % 14;

  // 1. Send Uplink C-plane for 20 PRBs
  struct rte_mbuf *c_mbuf = rte_pktmbuf_alloc(mp);
  struct xran_ecpri_hdr *ecpri = (struct xran_ecpri_hdr *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_ecpri_hdr));
  ecpri->ecpri_xtc_id = xran_compose_cid(&g_eaxcid_config, 0, 0, 0, 0);

  struct xran_cp_radioapp_section1_header *apphdr =
      (struct xran_cp_radioapp_section1_header *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_cp_radioapp_section1_header));
  memset(apphdr, 0, sizeof(*apphdr));
  apphdr->cmnhdr.field.dataDirection = XRAN_DIR_UL;
  apphdr->cmnhdr.field.payloadVer = XRAN_PAYLOAD_VER;
  apphdr->cmnhdr.field.frameId = frameId;
  apphdr->cmnhdr.field.subframeId = subframeId;
  apphdr->cmnhdr.field.slotId = slotId;
  apphdr->cmnhdr.field.startSymbolId = startSymbolId;
  apphdr->cmnhdr.sectionType = XRAN_CP_SECTIONTYPE_1;
  apphdr->cmnhdr.field.all_bits = rte_cpu_to_be_32(apphdr->cmnhdr.field.all_bits);

  struct xran_cp_radioapp_section1 *sec =
      (struct xran_cp_radioapp_section1 *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_cp_radioapp_section1));
  memset(sec, 0, sizeof(*sec));
  sec->hdr.u.s1.numSymbol = 1;
  sec->hdr.u1.common.numPrbc = 20;
  sec->hdr.u1.common.startPrbc = 0;
  sec->hdr.u1.common.sectionId = 123;
  *((uint64_t *)sec) = rte_be_to_cpu_64(*((uint64_t *)sec));

  handle_cplane_packet(ctx, c_mbuf);

  // 2. Call write_ul_iq
  uint32_t *txdataF[1];
  uint32_t iq_input[100 * 12];
  for (int i = 0; i < 100 * 12; i++) {
    iq_input[i] = 0x11223344;
  }
  txdataF[0] = iq_input;

  write_ul_iq(ctx, txdataF, 1, frameId, slot_in_frame, startSymbolId);

  oru_packet_processor_stats_t stats;
  get_packet_processor_stats(ctx, &stats);
  assert(stats.dl_tdd_mismatch == 0);
  assert(stats.ul_tdd_mismatch == 0);
  assert(g_packets_sent == 1);

  cleanup_packet_processor(ctx);
  printf("Basic uplink passed!\n");
}

void test_uplink_fragmentation()
{
  printf("Testing uplink fragmentation...\n");
  int mu = 1;
  int num_prb = 100;
  g_packets_sent = 0;
  void *ctx =
      init_packet_processor(mu, num_prb, 200, 400, 100, 300, 2, 2, 0, 0, 5, test_alloc_mbuf, test_send_mbuf_frag, NULL, 1500, 0);
  assert(ctx != NULL);

  // Pattern: 5 slots, 2 DL, 2 UL. Symbols 0-27 DL, 42-69 UL.
  uint64_t current_sym = 1030; // 1030 % 70 = 50 (UL)
  handle_absolute_symbol_tick(ctx, current_sym);

  uint64_t target_sym = current_sym + 5; // 1035 % 70 = 55 (UL)
  int slots_per_subframe = 1 << mu;
  int num_symbols_per_frame = 10 * slots_per_subframe * 14;
  int frameId = (target_sym / num_symbols_per_frame) % 256;
  int slot_in_frame = (target_sym % num_symbols_per_frame) / 14;
  int subframeId = slot_in_frame / slots_per_subframe;
  int slotId = slot_in_frame % slots_per_subframe;
  int startSymbolId = target_sym % 14;

  // 1. Send Uplink C-plane for 40 PRBs (should split into 30 + 10)
  struct rte_mbuf *c_mbuf = rte_pktmbuf_alloc(mp);
  struct xran_ecpri_hdr *ecpri = (struct xran_ecpri_hdr *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_ecpri_hdr));
  ecpri->ecpri_xtc_id = xran_compose_cid(&g_eaxcid_config, 0, 0, 0, 0);

  struct xran_cp_radioapp_section1_header *apphdr =
      (struct xran_cp_radioapp_section1_header *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_cp_radioapp_section1_header));
  memset(apphdr, 0, sizeof(*apphdr));
  apphdr->cmnhdr.field.dataDirection = XRAN_DIR_UL;
  apphdr->cmnhdr.field.payloadVer = XRAN_PAYLOAD_VER;
  apphdr->cmnhdr.field.frameId = frameId;
  apphdr->cmnhdr.field.subframeId = subframeId;
  apphdr->cmnhdr.field.slotId = slotId;
  apphdr->cmnhdr.field.startSymbolId = startSymbolId;
  apphdr->cmnhdr.sectionType = XRAN_CP_SECTIONTYPE_1;
  apphdr->cmnhdr.field.all_bits = rte_cpu_to_be_32(apphdr->cmnhdr.field.all_bits);

  struct xran_cp_radioapp_section1 *sec =
      (struct xran_cp_radioapp_section1 *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_cp_radioapp_section1));
  memset(sec, 0, sizeof(*sec));
  sec->hdr.u.s1.numSymbol = 1;
  sec->hdr.u1.common.numPrbc = 40;
  sec->hdr.u1.common.startPrbc = 0;
  sec->hdr.u1.common.sectionId = 123;
  *((uint64_t *)sec) = rte_be_to_cpu_64(*((uint64_t *)sec));

  handle_cplane_packet(ctx, c_mbuf);

  // 2. Call write_ul_iq
  uint32_t *txdataF[1];
  uint32_t iq_input[100 * 12];
  for (int i = 0; i < 100 * 12; i++) {
    iq_input[i] = 0x11223344;
  }
  txdataF[0] = iq_input;

  write_ul_iq(ctx, txdataF, 1, frameId, slot_in_frame, startSymbolId);

  oru_packet_processor_stats_t stats;
  get_packet_processor_stats(ctx, &stats);
  assert(stats.dl_tdd_mismatch == 0);
  assert(stats.ul_tdd_mismatch == 0);
  assert(g_packets_sent == 2);

  cleanup_packet_processor(ctx);
  printf("Uplink fragmentation passed!\n");
}

void test_uplink_large_mtu()
{
  printf("Testing uplink with large MTU (9600)...\n");
  int mu = 1;
  int num_prb = 273;
  g_packets_sent = 0;
  // Initialize with jumbo frame MTU
  void *ctx = init_packet_processor(mu,
                                    num_prb,
                                    200,
                                    400,
                                    100,
                                    300,
                                    2,
                                    2,
                                    0,
                                    0,
                                    5,
                                    test_alloc_mbuf,
                                    test_send_mbuf_large_mtu,
                                    NULL,
                                    9600,
                                    0);
  assert(ctx != NULL);

  // Pattern: 5 slots, 2 DL, 2 UL. Symbols 0-27 DL, 42-69 UL.
  uint64_t current_sym = 1030; // 1030 % 70 = 50 (UL)
  handle_absolute_symbol_tick(ctx, current_sym);

  uint64_t target_sym = current_sym + 5; // 1035 % 70 = 55 (UL)
  int slots_per_subframe = 1 << mu;
  int num_symbols_per_frame = 10 * slots_per_subframe * 14;
  int frameId = (target_sym / num_symbols_per_frame) % 256;
  int slot_in_frame = (target_sym % num_symbols_per_frame) / 14;
  int subframeId = slot_in_frame / slots_per_subframe;
  int slotId = slot_in_frame % slots_per_subframe;
  int startSymbolId = target_sym % 14;

  // 1. Send Uplink C-plane for 100 PRBs
  struct rte_mbuf *c_mbuf = rte_pktmbuf_alloc(mp);
  struct xran_ecpri_hdr *ecpri = (struct xran_ecpri_hdr *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_ecpri_hdr));
  ecpri->ecpri_xtc_id = xran_compose_cid(&g_eaxcid_config, 0, 0, 0, 0);

  struct xran_cp_radioapp_section1_header *apphdr =
      (struct xran_cp_radioapp_section1_header *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_cp_radioapp_section1_header));
  memset(apphdr, 0, sizeof(*apphdr));
  apphdr->cmnhdr.field.dataDirection = XRAN_DIR_UL;
  apphdr->cmnhdr.field.payloadVer = XRAN_PAYLOAD_VER;
  apphdr->cmnhdr.field.frameId = frameId;
  apphdr->cmnhdr.field.subframeId = subframeId;
  apphdr->cmnhdr.field.slotId = slotId;
  apphdr->cmnhdr.field.startSymbolId = startSymbolId;
  apphdr->cmnhdr.sectionType = XRAN_CP_SECTIONTYPE_1;
  apphdr->cmnhdr.field.all_bits = rte_cpu_to_be_32(apphdr->cmnhdr.field.all_bits);

  struct xran_cp_radioapp_section1 *sec =
      (struct xran_cp_radioapp_section1 *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_cp_radioapp_section1));
  memset(sec, 0, sizeof(*sec));
  sec->hdr.u.s1.numSymbol = 1;
  sec->hdr.u1.common.numPrbc = 100;
  sec->hdr.u1.common.startPrbc = 0;
  sec->hdr.u1.common.sectionId = 123;
  *((uint64_t *)sec) = rte_be_to_cpu_64(*((uint64_t *)sec));

  handle_cplane_packet(ctx, c_mbuf);

  // 2. Call write_ul_iq
  uint32_t *txdataF[1];
  uint32_t iq_input[num_prb * 12];
  for (int i = 0; i < num_prb * 12; i++) {
    iq_input[i] = 0x55667788;
  }
  txdataF[0] = iq_input;

  write_ul_iq(ctx, txdataF, 1, frameId, slot_in_frame, startSymbolId);

  oru_packet_processor_stats_t stats;
  get_packet_processor_stats(ctx, &stats);
  assert(stats.dl_tdd_mismatch == 0);
  assert(stats.ul_tdd_mismatch == 0);
  assert(g_packets_sent == 1);

  cleanup_packet_processor(ctx);
  printf("Uplink large MTU passed!\n");
}

void test_uplink_prb_offset()
{
  printf("Testing uplink with PRB offset...\n");
  int mu = 1;
  int num_prb = 100;
  g_packets_sent = 0;
  void *ctx = init_packet_processor(mu,
                                    num_prb,
                                    200,
                                    400,
                                    100,
                                    300,
                                    2,
                                    2,
                                    0,
                                    0,
                                    5,
                                    test_alloc_mbuf,
                                    test_send_mbuf_prb_offset,
                                    NULL,
                                    1500,
                                    0);
  assert(ctx != NULL);

  // Pattern: 5 slots, 2 DL, 2 UL. Symbols 0-27 DL, 42-69 UL.
  uint64_t current_sym = 1030; // 1030 % 70 = 50 (UL)
  handle_absolute_symbol_tick(ctx, current_sym);

  uint64_t target_sym = current_sym + 5; // 1035 % 70 = 55 (UL)
  int slots_per_subframe = 1 << mu;
  int num_symbols_per_frame = 10 * slots_per_subframe * 14;
  int frameId = (target_sym / num_symbols_per_frame) % 256;
  int slot_in_frame = (target_sym % num_symbols_per_frame) / 14;
  int subframeId = slot_in_frame / slots_per_subframe;
  int slotId = slot_in_frame % slots_per_subframe;
  int startSymbolId = target_sym % 14;

  // 1. Send Uplink C-plane for 20 PRBs starting at PRB 10
  struct rte_mbuf *c_mbuf = rte_pktmbuf_alloc(mp);
  struct xran_ecpri_hdr *ecpri = (struct xran_ecpri_hdr *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_ecpri_hdr));
  ecpri->ecpri_xtc_id = xran_compose_cid(&g_eaxcid_config, 0, 0, 0, 0);

  struct xran_cp_radioapp_section1_header *apphdr =
      (struct xran_cp_radioapp_section1_header *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_cp_radioapp_section1_header));
  memset(apphdr, 0, sizeof(*apphdr));
  apphdr->cmnhdr.field.dataDirection = XRAN_DIR_UL;
  apphdr->cmnhdr.field.payloadVer = XRAN_PAYLOAD_VER;
  apphdr->cmnhdr.field.frameId = frameId;
  apphdr->cmnhdr.field.subframeId = subframeId;
  apphdr->cmnhdr.field.slotId = slotId;
  apphdr->cmnhdr.field.startSymbolId = startSymbolId;
  apphdr->cmnhdr.sectionType = XRAN_CP_SECTIONTYPE_1;
  apphdr->cmnhdr.field.all_bits = rte_cpu_to_be_32(apphdr->cmnhdr.field.all_bits);

  struct xran_cp_radioapp_section1 *sec =
      (struct xran_cp_radioapp_section1 *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_cp_radioapp_section1));
  memset(sec, 0, sizeof(*sec));
  sec->hdr.u.s1.numSymbol = 1;
  sec->hdr.u1.common.numPrbc = 20;
  sec->hdr.u1.common.startPrbc = 10; // PRB offset 10
  sec->hdr.u1.common.sectionId = 123;
  *((uint64_t *)sec) = rte_be_to_cpu_64(*((uint64_t *)sec));

  handle_cplane_packet(ctx, c_mbuf);

  // 2. Call write_ul_iq
  uint32_t *txdataF[1];
  uint32_t iq_input[100 * 12];
  memset(iq_input, 0, sizeof(iq_input));
  iq_input[10 * 12] = 0x10101010; // Mark PRB 10
  txdataF[0] = iq_input;

  write_ul_iq(ctx, txdataF, 1, frameId, slot_in_frame, startSymbolId);

  oru_packet_processor_stats_t stats;
  get_packet_processor_stats(ctx, &stats);
  assert(stats.dl_tdd_mismatch == 0);
  assert(stats.ul_tdd_mismatch == 0);
  assert(g_packets_sent == 1);

  cleanup_packet_processor(ctx);
  printf("Uplink PRB offset passed!\n");
}

void test_prach_generation()
{
  printf("Testing PRACH generation...\n");
  int mu = 1;
  int num_prb = 100;
  int prach_eaxc_offset = 4;
  g_packets_sent = 0;
  void *ctx = init_packet_processor(mu,
                                    num_prb,
                                    200,
                                    400,
                                    100,
                                    300,
                                    2,
                                    2,
                                    0,
                                    0,
                                    5,
                                    test_alloc_mbuf,
                                    test_send_mbuf_prach,
                                    NULL,
                                    1500,
                                    prach_eaxc_offset);
  assert(ctx != NULL);

  // Pattern: 5 slots, 2 DL, 2 UL. Symbols 0-27 DL, 42-69 UL.
  uint64_t current_sym = 1030; // 1030 % 70 = 50 (UL)
  handle_absolute_symbol_tick(ctx, current_sym);

  uint64_t target_sym = current_sym + 5; // 1035 % 70 = 55 (UL)
  int slots_per_subframe = 1 << mu;
  int num_symbols_per_frame = 10 * slots_per_subframe * 14;
  int frameId = (target_sym / num_symbols_per_frame) % 256;
  int slot_in_frame = (target_sym % num_symbols_per_frame) / 14;
  int subframeId = slot_in_frame / slots_per_subframe;
  int slotId = slot_in_frame % slots_per_subframe;
  int startSymbolId = target_sym % 14;

  // 1. Send PRACH C-plane for 20 PRBs using section type 3
  struct rte_mbuf *c_mbuf = rte_pktmbuf_alloc(mp);
  struct xran_ecpri_hdr *ecpri = (struct xran_ecpri_hdr *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_ecpri_hdr));
  ecpri->ecpri_xtc_id = xran_compose_cid(&g_eaxcid_config, 0, 0, 0, prach_eaxc_offset);

  struct xran_cp_radioapp_section3_header *apphdr =
      (struct xran_cp_radioapp_section3_header *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_cp_radioapp_section3_header));
  memset(apphdr, 0, sizeof(*apphdr));
  apphdr->cmnhdr.field.dataDirection = XRAN_DIR_UL;
  apphdr->cmnhdr.field.payloadVer = XRAN_PAYLOAD_VER;
  apphdr->cmnhdr.field.frameId = frameId;
  apphdr->cmnhdr.field.subframeId = subframeId;
  apphdr->cmnhdr.field.slotId = slotId;
  apphdr->cmnhdr.field.startSymbolId = startSymbolId;
  apphdr->cmnhdr.field.filterIndex = XRAN_FILTERINDEX_PRACH_012;
  apphdr->cmnhdr.numOfSections = 1;
  apphdr->cmnhdr.sectionType = XRAN_CP_SECTIONTYPE_3;
  apphdr->cmnhdr.field.all_bits = rte_cpu_to_be_32(apphdr->cmnhdr.field.all_bits);

  struct xran_cp_radioapp_section3 *sec =
      (struct xran_cp_radioapp_section3 *)rte_pktmbuf_append(c_mbuf, sizeof(struct xran_cp_radioapp_section3));
  memset(sec, 0, sizeof(*sec));
  sec->hdr.u.s3.numSymbol = 4;
  sec->hdr.u1.common.numPrbc = 20;
  sec->hdr.u1.common.startPrbc = 0;
  sec->hdr.u1.common.sectionId = 123;
  *((uint64_t *)sec) = rte_be_to_cpu_64(*((uint64_t *)sec));

  handle_cplane_packet(ctx, c_mbuf);

  // 2. Call write_prach_iq for each of the 4 symbols
  uint32_t *txdataF[1];
  uint32_t iq_input[100 * 12];
  memset(iq_input, 0, sizeof(iq_input));
  txdataF[0] = iq_input;

  for (int i = 0; i < 4; i++) {
    write_prach_iq(ctx, txdataF, 1, frameId, slot_in_frame, startSymbolId + i);
  }

  // Expect 4 packets to be sent
  assert(g_packets_sent == 4);

  // Attempting to write a 5th symbol should not send a packet, as the job should be deactivated
  write_prach_iq(ctx, txdataF, 1, frameId, slot_in_frame, startSymbolId + 4);
  assert(g_packets_sent == 4);

  cleanup_packet_processor(ctx);
  printf("PRACH generation passed!\n");
}

int main(int argc, char **argv)
{
  setup_dpdk(argc, argv);
  test_uplink_prb_offset();
  usleep(10000); // Delay is needed to let dpdk cleanup internal structures between cleanup/init calls
  test_init_cleanup();
  usleep(10000);
  test_cplane_timing_errors();
  usleep(10000);
  test_cplane_uplane_match();
  usleep(10000);
  test_frame_wrap_around();
  usleep(10000);
  test_cplane_14_symbols();
  usleep(10000);
  test_other_bw_4ant_prb_offset();
  usleep(10000);

  test_uplink_basic();
  usleep(10000);
  test_uplink_fragmentation();
  usleep(10000);
  test_uplink_large_mtu();
  usleep(10000);
  test_prach_generation();
  usleep(10000);

  printf("All tests passed!\n");
  return 0;
}
