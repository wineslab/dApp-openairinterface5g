/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "common_lib.h"
#include <gtest/gtest.h>
#include "common/config/config_userapi.h"
#include <thread>
#include <vector>
#include <string>

configmodule_interface_t *uniqCfg = NULL;

extern "C" {
#include "common/config/config_userapi.h"
#include "openair1/SIMULATION/TOOLS/sim.h"
extern int device_init(openair0_device_t *device, openair0_config_t *openair0_cfg);
extern void vrtsim_produce_samples(openair0_device_t *device, size_t num_samples);
static softmodem_params_t softmodem_params;
softmodem_params_t *get_softmodem_params(void)
{
  return &softmodem_params;
}
void exit_function(const char *file, const char *function, const int line, const char *s, const int assert)
{
  fprintf(stderr, "FATAL: %s at %s:%s:%d\n", s, file, function, line);
  exit(EXIT_FAILURE);
}
}

struct VRTSIMTestCase {
  int server_tx;
  int server_rx;
  int client_tx;
  int client_rx;
  std::string ue_antennas;
};

class VRTSIMTest : public ::testing::TestWithParam<VRTSIMTestCase> {
 protected:
  configmodule_interface_t *cfg1 = nullptr;
  configmodule_interface_t *cfg2 = nullptr;
  openair0_device_t server_device = {0};
  openair0_device_t client_device = {0};
  openair0_config_t server_config = {0};
  openair0_config_t client_config = {0};

  void SetUp() override
  {
    const auto &param = GetParam();

    // Setup server
    std::vector<const char *> server_argv = {"--vrtsim.role",
                                             "server",
                                             "--vrtsim.disable-timing-thread",
                                             "1",
                                             "--vrtsim.ue_config.[0].antennas",
                                             param.ue_antennas.c_str()};
    cfg1 = load_configmodule(server_argv.size(), (char **)server_argv.data(), CONFIG_ENABLECMDLINEONLY);
    uniqCfg = cfg1;
    server_config.tx_num_channels = param.server_tx;
    server_config.rx_num_channels = param.server_rx;
    server_config.sample_rate = 30.72e6;
    ASSERT_EQ(device_init(&server_device, &server_config), 0);
    ASSERT_EQ(server_device.trx_start_func(&server_device), 0);

    // Setup client
    std::vector<const char *> client_argv = {"--vrtsim.role", "client"};
    cfg2 = load_configmodule(client_argv.size(), (char **)client_argv.data(), CONFIG_ENABLECMDLINEONLY);
    uniqCfg = cfg2;
    client_config.tx_num_channels = param.client_tx;
    client_config.rx_num_channels = param.client_rx;
    client_config.sample_rate = 30.72e6;
    ASSERT_EQ(device_init(&client_device, &client_config), 0);
    ASSERT_EQ(client_device.trx_start_func(&client_device), 0);
  }

  void TearDown() override
  {
    if (server_device.trx_end_func)
      server_device.trx_end_func(&server_device);
    if (client_device.trx_end_func)
      client_device.trx_end_func(&client_device);
    if (cfg1)
      end_configmodule(cfg1);
    if (cfg2)
      end_configmodule(cfg2);
  }
};

TEST_P(VRTSIMTest, TransparentChannel)
{
  const auto &param = GetParam();
  const int nsamps = 1024;

  // Prepare TX samples for client (UL)
  std::vector<std::vector<c16_t>> client_tx_samples(param.client_tx, std::vector<c16_t>(nsamps));
  std::vector<void *> client_tx_ptrs(param.client_tx);
  for (int a = 0; a < param.client_tx; a++) {
    for (int i = 0; i < nsamps; i++) {
      client_tx_samples[a][i] = {(int16_t)(i + a * 100), (int16_t) - (i + a * 100)};
    }
    client_tx_ptrs[a] = client_tx_samples[a].data();
  }

  // Prepare TX samples for server (DL)
  std::vector<std::vector<c16_t>> server_tx_samples(param.server_tx, std::vector<c16_t>(nsamps));
  std::vector<void *> server_tx_ptrs(param.server_tx);
  for (int a = 0; a < param.server_tx; a++) {
    for (int i = 0; i < nsamps; i++) {
      server_tx_samples[a][i] = {(int16_t)(i + a * 200), (int16_t)(i + a * 200 + 10)};
    }
    server_tx_ptrs[a] = server_tx_samples[a].data();
  }

  // Both sides write at timestamp 0
  ASSERT_EQ(client_device.trx_write_func(&client_device, 0, client_tx_ptrs.data(), nsamps, param.client_tx, 0), nsamps);
  ASSERT_EQ(server_device.trx_write_func(&server_device, 0, server_tx_ptrs.data(), nsamps, param.server_tx, 0), nsamps);

  // Server produces samples (advances time to nsamps)
  vrtsim_produce_samples(&server_device, nsamps);

  // Server reads from client (UL) at timestamp 0
  std::vector<std::vector<c16_t>> server_rx_samples(param.server_rx, std::vector<c16_t>(nsamps));
  std::vector<void *> server_rx_ptrs(param.server_rx);
  for (int a = 0; a < param.server_rx; a++)
    server_rx_ptrs[a] = server_rx_samples[a].data();

  openair0_timestamp_t rx_ts;
  ASSERT_EQ(server_device.trx_read_func(&server_device, &rx_ts, server_rx_ptrs.data(), nsamps, param.server_rx), nsamps);
  EXPECT_EQ(rx_ts, 0);

  // Verify UL mapping (Order mapping: min(client_tx, server_rx))
  int num_ul_mapped = std::min(param.client_tx, param.server_rx);
  for (int a = 0; a < num_ul_mapped; a++) {
    for (int i = 0; i < nsamps; i++) {
      EXPECT_EQ(server_rx_samples[a][i].r, client_tx_samples[a][i].r) << "UL Mismatch at index " << i << " antenna " << a;
      EXPECT_EQ(server_rx_samples[a][i].i, client_tx_samples[a][i].i) << "UL Mismatch at index " << i << " antenna " << a;
    }
  }

  // Client reads from server (DL) at timestamp 0
  std::vector<std::vector<c16_t>> client_rx_samples(param.client_rx, std::vector<c16_t>(nsamps));
  std::vector<void *> client_rx_ptrs(param.client_rx);
  for (int a = 0; a < param.client_rx; a++)
    client_rx_ptrs[a] = client_rx_samples[a].data();

  ASSERT_EQ(client_device.trx_read_func(&client_device, &rx_ts, client_rx_ptrs.data(), nsamps, param.client_rx), nsamps);
  EXPECT_EQ(rx_ts, 0);

  // Verify DL mapping (Order mapping: min(server_tx, client_rx))
  int num_dl_mapped = std::min(param.server_tx, param.client_rx);
  for (int a = 0; a < num_dl_mapped; a++) {
    for (int i = 0; i < nsamps; i++) {
      EXPECT_EQ(client_rx_samples[a][i].r, server_tx_samples[a][i].r) << "DL Mismatch at index " << i << " antenna " << a;
      EXPECT_EQ(client_rx_samples[a][i].i, server_tx_samples[a][i].i) << "DL Mismatch at index " << i << " antenna " << a;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(AntennaVariations,
                         VRTSIMTest,
                         ::testing::Values(VRTSIMTestCase{2, 2, 1, 1, "1x1"},
                                           VRTSIMTestCase{8, 8, 1, 1, "1x1"},
                                           VRTSIMTestCase{8, 8, 2, 2, "2x2"},
                                           VRTSIMTestCase{1, 1, 2, 2, "2x2"}));

int main(int argc, char **argv)
{
  logInit();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
