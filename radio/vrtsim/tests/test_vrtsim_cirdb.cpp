/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <yaml-cpp/yaml.h>
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <string>
#include <complex>
#include <filesystem>
#include <fstream>
#include <tuple>

#include "common_lib.h"
#include "common/config/config_userapi.h"

namespace fs = std::filesystem;

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

class CIRDBProducer {
  fs::path root;
  std::vector<YAML::Node> entries;

 public:
  CIRDBProducer(fs::path r) : root(r)
  {
    fs::create_directories(root);
  }
  void add_entry(int model_id, int n_tx, int n_rx, int L, int S, double fs, double dt, double ds, double speed)
  {
    YAML::Node entry;
    entry["model_id"] = model_id;
    entry["n_tx"] = n_tx;
    entry["n_rx"] = n_rx;
    entry["L"] = L;
    entry["S"] = S;
    entry["fs_hz"] = fs;
    entry["snapshot_dt_s"] = dt;
    entry["ds_ns"] = ds;
    entry["speed_mps"] = speed;
    entry["offset_bytes"] = entries.size() * n_tx * n_rx * L * sizeof(float) * 2;
    entry["nbytes"] = n_tx * n_rx * L * sizeof(float) * 2;
    entries.push_back(entry);
  }
  void write_files()
  {
    YAML::Node root_node;
    root_node["entries"] = entries;
    std::ofstream out(root / "vrtsim.yaml");
    out << root_node;

    std::ofstream bin(root / "cir_db.bin", std::ios::binary);
    for (const auto &e : entries) {
      int count = e["n_tx"].as<int>() * e["n_rx"].as<int>() * e["L"].as<int>();
      std::vector<float> data(count * 2, 0.0f);
      for (int tx = 0; tx < e["n_tx"].as<int>(); tx++) {
        for (int rx = 0; rx < e["n_rx"].as<int>(); rx++) {
          int L = e["L"].as<int>();
          data[((rx + tx * e["n_rx"].as<int>()) * L + 7) * 2] = 1.0f;
        }
      }
      bin.write(reinterpret_cast<char *>(data.data()), data.size() * sizeof(float));
    }
  }
};

struct CIRDBAntParams {
  int gnb_tx;
  int gnb_rx;
  int ue_tx;
  int ue_rx;
  std::string ue_ant_str;
  CIRDBAntParams(int gt, int gr, int ut, int ur, std::string s) : gnb_tx(gt), gnb_rx(gr), ue_tx(ut), ue_rx(ur), ue_ant_str(s)
  {
  }
};

class VRTSTapsCIRDBTest : public ::testing::TestWithParam<CIRDBAntParams> {
 protected:
  openair0_device_t server_device = {0};
  openair0_device_t client_device = {0};
  openair0_config_t server_config = {0};
  openair0_config_t client_config = {0};
  fs::path tmp_dir;
  std::string shm_name;
  configmodule_interface_t *cfg1 = nullptr;
  configmodule_interface_t *cfg2 = nullptr;

  void SetUp() override
  {
    const CIRDBAntParams &p = GetParam();
    tmp_dir = fs::temp_directory_path() / ("vrtsim_cirdb_test_" + std::to_string(p.gnb_tx) + "_" + p.ue_ant_str);
    CIRDBProducer producer(tmp_dir);
    // Add entries for both directions to both model IDs to ensure test robustness against unintended configuration state leakage
    producer.add_entry(0, p.gnb_tx, p.ue_rx, 8, 1, 30.72e6, 0.5, 10.0, 1.5);
    producer.add_entry(1, p.gnb_tx, p.ue_rx, 8, 1, 30.72e6, 0.5, 10.0, 1.5);
    producer.add_entry(0, p.ue_tx, p.gnb_rx, 8, 1, 30.72e6, 0.5, 10.0, 1.5);
    producer.add_entry(1, p.ue_tx, p.gnb_rx, 8, 1, 30.72e6, 0.5, 10.0, 1.5);
    producer.write_files();
    shm_name = "shm_cirdb_" + std::to_string(p.gnb_tx) + "_" + p.ue_ant_str;
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
    fs::remove_all(tmp_dir);
    server_device = {0};
    client_device = {0};
    cfg1 = nullptr;
    cfg2 = nullptr;
    uniqCfg = nullptr;
  }
};

TEST_P(VRTSTapsCIRDBTest, CIRDBDelayDL)
{
  const CIRDBAntParams &p = GetParam();

  // Setup Server
  {
    const char *s_argv[] = {"test_vrtsim_cirdb",
                            "--vrtsim.role",
                            "server",
                            "--vrtsim.shm_channel_name",
                            shm_name.c_str(),
                            "--vrtsim.disable-timing-thread",
                            "1",
                            "--vrtsim.cirdb",
                            "1",
                            "--vrtsim.cirdb-path",
                            tmp_dir.c_str(),
                            "--vrtsim.cirdb_model_id",
                            "0",
                            "--vrtsim.ue_config.[0].antennas",
                            p.ue_ant_str.c_str()};
    cfg1 = load_configmodule(sizeof(s_argv) / sizeof(char *), (char **)s_argv, CONFIG_ENABLECMDLINEONLY);
    uniqCfg = cfg1;
    server_config.tx_num_channels = p.gnb_tx;
    server_config.rx_num_channels = p.gnb_rx;
    server_config.sample_rate = 30.72e6;
    ASSERT_EQ(device_init(&server_device, &server_config), 0);
    ASSERT_EQ(server_device.trx_start_func(&server_device), 0);
  }

  // Setup Client
  {
    const char *c_argv[] = {"test_vrtsim_cirdb",
                            "--vrtsim.role",
                            "client",
                            "--vrtsim.shm_channel_name",
                            shm_name.c_str(),
                            "--vrtsim.cirdb",
                            "0",
                            "--vrtsim.ue_config.[0].antennas",
                            p.ue_ant_str.c_str()};
    cfg2 = load_configmodule(sizeof(c_argv) / sizeof(char *), (char **)c_argv, CONFIG_ENABLECMDLINEONLY);
    uniqCfg = cfg2;
    client_config.tx_num_channels = p.ue_tx;
    client_config.rx_num_channels = p.ue_rx;
    client_config.sample_rate = 30.72e6;
    ASSERT_EQ(device_init(&client_device, &client_config), 0);
    ASSERT_EQ(client_device.trx_start_func(&client_device), 0);
  }

  const int nsamps = 1024;
  std::vector<std::vector<c16_t>> tx_chan_samples(p.gnb_tx, std::vector<c16_t>(nsamps));
  std::vector<void *> tx_ptrs(p.gnb_tx);
  for (int ch = 0; ch < p.gnb_tx; ch++) {
    for (int i = 0; i < nsamps; i++)
      tx_chan_samples[ch][i] = {(int16_t)(i + 1 + ch * 100), (int16_t) - (i + 1 + ch * 100)};
    tx_ptrs[ch] = tx_chan_samples[ch].data();
  }
  ASSERT_EQ(server_device.trx_write_func(&server_device, 0, tx_ptrs.data(), nsamps, p.gnb_tx, 0), nsamps);
  vrtsim_produce_samples(&server_device, nsamps);

  std::vector<std::vector<c16_t>> rx_chan_samples(p.ue_rx, std::vector<c16_t>(nsamps));
  std::vector<void *> rx_ptrs(p.ue_rx);
  for (int ch = 0; ch < p.ue_rx; ch++)
    rx_ptrs[ch] = rx_chan_samples[ch].data();
  openair0_timestamp_t rx_ts;
  ASSERT_EQ(client_device.trx_read_func(&client_device, &rx_ts, rx_ptrs.data(), nsamps, p.ue_rx), nsamps);

  for (int rx_ch = 0; rx_ch < p.ue_rx; rx_ch++) {
    for (int i = 7; i < nsamps; i++) {
      int32_t expected_r = 0;
      for (int tx_ch = 0; tx_ch < p.gnb_tx; tx_ch++)
        expected_r += tx_chan_samples[tx_ch][i - 7].r;
      EXPECT_EQ(rx_chan_samples[rx_ch][i].r, (int16_t)expected_r);
    }
  }
}

TEST_P(VRTSTapsCIRDBTest, CIRDBDelayUL)
{
  const CIRDBAntParams &p = GetParam();

  // Setup Server
  {
    const char *s_argv[] = {"test_vrtsim_cirdb",
                            "--vrtsim.role",
                            "server",
                            "--vrtsim.shm_channel_name",
                            shm_name.c_str(),
                            "--vrtsim.disable-timing-thread",
                            "1",
                            "--vrtsim.cirdb",
                            "0",
                            "--vrtsim.ue_config.[0].antennas",
                            p.ue_ant_str.c_str()};
    cfg1 = load_configmodule(sizeof(s_argv) / sizeof(char *), (char **)s_argv, CONFIG_ENABLECMDLINEONLY);
    uniqCfg = cfg1;
    server_config.tx_num_channels = p.gnb_tx;
    server_config.rx_num_channels = p.gnb_rx;
    server_config.sample_rate = 30.72e6;
    ASSERT_EQ(device_init(&server_device, &server_config), 0);
    ASSERT_EQ(server_device.trx_start_func(&server_device), 0);
  }

  // Setup Client
  {
    const char *c_argv[] = {"test_vrtsim_cirdb",
                            "--vrtsim.role",
                            "client",
                            "--vrtsim.shm_channel_name",
                            shm_name.c_str(),
                            "--vrtsim.cirdb",
                            "1",
                            "--vrtsim.cirdb-path",
                            tmp_dir.c_str(),
                            "--vrtsim.cirdb_model_id",
                            "1",
                            "--vrtsim.ue_config.[0].antennas",
                            p.ue_ant_str.c_str()};
    cfg2 = load_configmodule(sizeof(c_argv) / sizeof(char *), (char **)c_argv, CONFIG_ENABLECMDLINEONLY);
    uniqCfg = cfg2;
    client_config.tx_num_channels = p.ue_tx;
    client_config.rx_num_channels = p.ue_rx;
    client_config.sample_rate = 30.72e6;
    ASSERT_EQ(device_init(&client_device, &client_config), 0);
    ASSERT_EQ(client_device.trx_start_func(&client_device), 0);
  }

  const int nsamps = 1024;
  std::vector<std::vector<c16_t>> tx_chan_samples(p.ue_tx, std::vector<c16_t>(nsamps));
  std::vector<void *> tx_ptrs(p.ue_tx);
  for (int ch = 0; ch < p.ue_tx; ch++) {
    for (int i = 0; i < nsamps; i++)
      tx_chan_samples[ch][i] = {(int16_t)(i + 1 + ch * 100), (int16_t) - (i + 1 + ch * 100)};
    tx_ptrs[ch] = tx_chan_samples[ch].data();
  }
  ASSERT_EQ(client_device.trx_write_func(&client_device, 0, tx_ptrs.data(), nsamps, p.ue_tx, 0), nsamps);
  vrtsim_produce_samples(&server_device, nsamps);

  std::vector<std::vector<c16_t>> rx_chan_samples(p.gnb_rx, std::vector<c16_t>(nsamps));
  std::vector<void *> rx_ptrs(p.gnb_rx);
  for (int ch = 0; ch < p.gnb_rx; ch++)
    rx_ptrs[ch] = rx_chan_samples[ch].data();
  openair0_timestamp_t rx_ts;
  ASSERT_EQ(server_device.trx_read_func(&server_device, &rx_ts, rx_ptrs.data(), nsamps, p.gnb_rx), nsamps);

  for (int rx_ch = 0; rx_ch < p.gnb_rx; rx_ch++) {
    for (int i = 7; i < nsamps; i++) {
      int32_t expected_r = 0;
      for (int tx_ch = 0; tx_ch < p.ue_tx; tx_ch++)
        expected_r += tx_chan_samples[tx_ch][i - 7].r;
      EXPECT_EQ(rx_chan_samples[rx_ch][i].r, (int16_t)expected_r);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(AntennaVariations,
                         VRTSTapsCIRDBTest,
                         ::testing::Values(CIRDBAntParams(1, 1, 1, 1, "1x1"),
                                           CIRDBAntParams(2, 2, 1, 1, "1x1"),
                                           CIRDBAntParams(4, 4, 2, 2, "2x2"),
                                           CIRDBAntParams(8, 8, 2, 2, "2x2")));

int main(int argc, char **argv)
{
  logInit();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
