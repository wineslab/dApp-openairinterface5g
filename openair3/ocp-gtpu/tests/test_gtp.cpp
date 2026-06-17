/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdlib.h>
#include <arpa/inet.h>

#include <gtest/gtest.h>

#include "common/platform_types.h"
#include "common/utils/LOG/log.h"
#include "common/config/config_userapi.h"

#include "gtp_itf.h"

extern "C" {
configmodule_interface_t *uniqCfg;

void exit_function(const char *file, const char *function, const int line, const char *s, const int assert)
{
  UNUSED(assert);
  LOG_E(GNB_APP, "error at %s:%d:%s: %s\n", file, line, function, s);
  abort();
}

int nr_rlc_get_available_tx_space(const rnti_t rntiP, const logical_chan_id_t channel_idP)
{
  UNUSED(rntiP);
  UNUSED(channel_idP);
  abort();
  return 0;
}
void *get_softmodem_params(void)
{
  abort();
  return NULL;
};
}

/* @brief initialize GTP interface for IP:port */
static instance_t init_gtp(const char *ip, uint16_t port)
{
  openAddr_t iface = {0};
  DevAssert(strlen(ip) <= sizeof(iface.originHost));
  strcpy(iface.originHost, ip);
  snprintf(iface.originService, sizeof(iface.originService), "%d", port);
  snprintf(iface.destinationService, sizeof(iface.destinationService), "%d", port);
  return gtpv1Init(iface);
}

static in_addr_t get_addr(int ai_family, const char *ip)
{
  AssertFatal(ai_family == AF_INET, "only IPv4 supported\n");
  in_addr_t addr = {0};
  inet_pton(ai_family, ip, &addr);
  return addr;
}

static transport_layer_addr_t get_tl_addr(int ai_family, const char *ip)
{
  in_addr_t addr = get_addr(ai_family, ip);
  transport_layer_addr_t tl_addr = {.length = 32};
  memcpy(tl_addr.buffer, &addr, 4);
  return tl_addr;
}

static void run_basic_test(uint32_t ue_id,
                           long pdu_id,
                           long qfi,
                           int num_send,
                           int *rcv_count,
                           gtpCallback callBack,
                           gtpCallbackSDAP callBackSDAP)
{
  /* set up two instances on different IPs */
  const char *ip1 = "127.0.0.1";
  const char *ip2 = "127.0.0.2";
  uint16_t port = 4567;
  instance_t ep1 = init_gtp(ip1, port);
  EXPECT_GE(ep1, 1);
  instance_t ep2 = init_gtp(ip2, port);
  EXPECT_GE(ep2, 1);
  EXPECT_NE(ep1, ep2); // different IPs => different interfaces

  /* Create receiving end on ep1. Since we don't know the ep2's TEID, we also
   * don't provide an address yet, hence "null_addr". Install the callback
   * specific to this test. */
  transport_layer_addr_t null_addr = {.length = 32};
  teid_t t1 = newGtpuCreateTunnel(ep1, ue_id, pdu_id, pdu_id, -1, null_addr, callBack, callBackSDAP);

  /* Create the sending end on ep2. We have ep1's address/TEID, so create the
   * remote endpoint. Don't provide a callback, as this is supposed to be
   * unidirectional. */
  transport_layer_addr_t tl_addr1 = get_tl_addr(AF_INET, ip1);
  teid_t t2 = newGtpuCreateTunnel(ep2, ue_id, pdu_id, pdu_id, t1, tl_addr1, NULL, NULL);

  EXPECT_NE(t1, t2); // cannot be the same TEIDs

  /* With the TEID at ep2, update ep1 endpoint. */
  in_addr_t addr2 = get_addr(AF_INET, ip2);
  GtpuUpdateTunnelOutgoingAddressAndTeid(ep1, ue_id, pdu_id, addr2, t2);

  /* Send num_send messages, with each message three bytes with a specfic
   * payload structure (each byte increasing within the buffer and across
   * message) checked by the callback recv_basic_conn_qfi(). */
  uint8_t buf[3];
  uint8_t payload_counter = 0;
  for (int i = 0; i < num_send; ++i) {
    for (size_t p = 0; p < sizeof(buf); p++)
      buf[p] = payload_counter++;
    if (qfi >= 0)
      gtpv1uSendDirectWithQFI(ep2, ue_id, pdu_id, qfi, buf, sizeof(buf));
    else
      gtpv1uSendDirect(ep2, ue_id, pdu_id, buf, sizeof(buf), false, false);
  }

  usleep(100 * 1000); // wait 100ms to give time to receive packets

  /* check that three messages have been received. The callback checked that
   * the messages had the expected format. */
  EXPECT_EQ(*rcv_count, num_send);

  int ret;
  ret = newGtpuDeleteAllTunnels(ep1, ue_id);
  EXPECT_EQ(ret, 0);
  ret = newGtpuDeleteAllTunnels(ep2, ue_id);
  EXPECT_EQ(ret, 0);

  ret = gtpv1Term(ep1);
  EXPECT_EQ(ret, 0);
  ret = gtpv1Term(ep2);
  EXPECT_EQ(ret, 0);
}

static int recv_count_qfi = 0;
static bool recv_basic_conn_qfi(protocol_ctxt_t *ctxt,
                                const ue_id_t ue_id,
                                const srb_flag_t flag,
                                const mui_t mui,
                                const confirm_t confirm,
                                const sdu_size_t size,
                                unsigned char *const buf,
                                const pdcp_transmission_mode_t modeP,
                                const uint32_t *sourceL2Id,
                                const uint32_t *destinationL2Id,
                                const uint8_t qfi,
                                const bool rqi,
                                const int pdusession_id)
{
  UNUSED(ctxt);
  UNUSED(flag);
  UNUSED(mui);
  UNUSED(confirm);
  UNUSED(modeP);
  UNUSED(sourceL2Id);
  UNUSED(destinationL2Id);
  UNUSED(rqi);
  UNUSED(pdusession_id);
  EXPECT_EQ(ue_id, 1); // as defined in basic_conn_qfi
  EXPECT_EQ(qfi, 1);
  EXPECT_EQ(size, 3);
  for (int i = 0; i < size; ++i)
    EXPECT_EQ(buf[i], recv_count_qfi * size + i);
  recv_count_qfi++;
  LOG_I(GTPU, "received message %d with size 3, content %d.%d.%d\n", recv_count_qfi, buf[0], buf[1], buf[2]);
  return true;
}

static int recv_count_multi_qfi = 0;
static const uint8_t expected_qfis_multi_qfi[] = {1, 5, 9, 5, 1};
static const int expected_qfis_multi_qfi_count = sizeof(expected_qfis_multi_qfi) / sizeof(expected_qfis_multi_qfi[0]);
static bool recv_multi_qfi_same_pdu(protocol_ctxt_t *ctxt,
                                    const ue_id_t ue_id,
                                    const srb_flag_t flag,
                                    const mui_t mui,
                                    const confirm_t confirm,
                                    const sdu_size_t size,
                                    unsigned char *const buf,
                                    const pdcp_transmission_mode_t modeP,
                                    const uint32_t *sourceL2Id,
                                    const uint32_t *destinationL2Id,
                                    const uint8_t qfi,
                                    const bool rqi,
                                    const int pdusession_id)
{
  UNUSED(ctxt);
  UNUSED(flag);
  UNUSED(mui);
  UNUSED(confirm);
  UNUSED(modeP);
  UNUSED(sourceL2Id);
  UNUSED(destinationL2Id);
  UNUSED(rqi);
  EXPECT_EQ(ue_id, 7U);
  EXPECT_EQ(pdusession_id, 9);
  EXPECT_LT(recv_count_multi_qfi, expected_qfis_multi_qfi_count);
  EXPECT_EQ(qfi, expected_qfis_multi_qfi[recv_count_multi_qfi]);
  EXPECT_EQ(size, 3);
  for (int i = 0; i < size; ++i)
    EXPECT_EQ(buf[i], recv_count_multi_qfi * size + i);
  recv_count_multi_qfi++;
  return true;
}

static void run_multi_qos_flows_test(uint32_t ue_id, long pdu_id, const uint8_t *qfis, int num_send)
{
  /* set up two instances on different IPs */
  const char *ip1 = "127.0.0.1";
  const char *ip2 = "127.0.0.2";
  uint16_t port = 4567;
  instance_t ep1 = init_gtp(ip1, port);
  EXPECT_GE(ep1, 1);
  instance_t ep2 = init_gtp(ip2, port);
  EXPECT_GE(ep2, 1);
  EXPECT_NE(ep1, ep2);

  transport_layer_addr_t null_addr = {.length = 32};
  teid_t t1 = newGtpuCreateTunnel(ep1, ue_id, pdu_id, pdu_id, -1, null_addr, NULL, recv_multi_qfi_same_pdu);

  transport_layer_addr_t tl_addr1 = get_tl_addr(AF_INET, ip1);
  teid_t t2 = newGtpuCreateTunnel(ep2, ue_id, pdu_id, pdu_id, t1, tl_addr1, NULL, NULL);
  EXPECT_NE(t1, t2);

  in_addr_t addr2 = get_addr(AF_INET, ip2);
  GtpuUpdateTunnelOutgoingAddressAndTeid(ep1, ue_id, pdu_id, addr2, t2);

  uint8_t buf[3];
  uint8_t payload_counter = 0;
  for (int i = 0; i < num_send; ++i) {
    for (size_t p = 0; p < sizeof(buf); p++)
      buf[p] = payload_counter++;
    gtpv1uSendDirectWithQFI(ep2, ue_id, pdu_id, qfis[i], buf, sizeof(buf));
  }

  usleep(100 * 1000);
  EXPECT_EQ(recv_count_multi_qfi, num_send);

  int ret = newGtpuDeleteAllTunnels(ep1, ue_id);
  EXPECT_EQ(ret, 0);
  ret = newGtpuDeleteAllTunnels(ep2, ue_id);
  EXPECT_EQ(ret, 0);
  ret = gtpv1Term(ep1);
  EXPECT_EQ(ret, 0);
  ret = gtpv1Term(ep2);
  EXPECT_EQ(ret, 0);
}

/* Test unidirectional GTP message forwarding for a single UE with QFI. */
TEST(gtp, basic_conn_qfi)
{
  /* we consider only a single UE on a specfic bearer (ID=3) */
  uint32_t ue_id = 1;
  long pdu_id = 3;
  long qfi = 1;
  int num_send = 3;
  run_basic_test(ue_id, pdu_id, qfi, num_send, &recv_count_qfi, NULL, recv_basic_conn_qfi);
}

/* Test one PDU session carrying multiple QoS flows (different QFIs). */
TEST(gtp, multi_qos_flows)
{
  uint32_t ue_id = 7;
  long pdu_id = 9;
  static const uint8_t qfis[] = {1, 5, 9, 5, 1};
  recv_count_multi_qfi = 0;
  run_multi_qos_flows_test(ue_id, pdu_id, qfis, sizeof(qfis) / sizeof(qfis[0]));
}

static int recv_count = 0;
static bool recv_basic_conn(protocol_ctxt_t *ctxt,
                            const srb_flag_t srb_flagP,
                            const rb_id_t rb_idP,
                            const mui_t muiP,
                            const confirm_t confirmP,
                            const sdu_size_t size,
                            unsigned char *const buf,
                            const pdcp_transmission_mode_t modeP,
                            const uint32_t *sourceL2Id,
                            const uint32_t *destinationL2Id)
{
  UNUSED(srb_flagP);
  UNUSED(rb_idP);
  UNUSED(muiP);
  UNUSED(confirmP);
  UNUSED(modeP);
  UNUSED(sourceL2Id);
  UNUSED(destinationL2Id);
  EXPECT_EQ(ctxt->rntiMaybeUEid, 12); // as defined in basic_conn
  EXPECT_EQ(size, 3);
  for (int i = 0; i < size; ++i)
    EXPECT_EQ(buf[i], recv_count * size + i);
  recv_count++;
  LOG_I(GTPU, "received message %d with size 3, content %d.%d.%d\n", recv_count, buf[0], buf[1], buf[2]);
  return true;
}

/* Test unidirectional GTP message forwarding for a single UE (without QFI). */
TEST(gtp, basic_conn)
{
  /* we consider only a single UE on a specfic bearer (ID=3) */
  uint32_t ue_id = 12;
  long pdu_id = 5;
  long noqfi = -1;
  int num_send = 12;
  run_basic_test(ue_id, pdu_id, noqfi, num_send, &recv_count, recv_basic_conn, NULL);
}

/* ideas for tests:
 * - share IP addresses among two instances (e.g., F1-U/NG-U on same IP/port)
 * - support of IPv6
 * - test extension headers
 *
 * ideas for cleanup:
 * - simplify callback, harmonize "normal" and "SDAP" callbacks
 * - consistently use either transport_layer_addr_t or in_addr
 */

int main(int argc, char **argv)
{
  //if ((uniqCfg = load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY)) == NULL) {
  //  exit_fun("[SOFTMODEM] Error, configuration module init failed\n");
  //}
  // TODO logInit() without config?
  logInit();
  g_log->log_component[GTPU].level = OAILOG_DEBUG;

  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
