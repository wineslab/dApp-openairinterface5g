/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef __OAIORAN_RU_H__
#define __OAIORAN_RU_H__

#include "oru_io.h"
#include <stdint.h>

typedef struct {
  char *dpdk_devices[MAX_RU_PORTS];
  int num_dpdk_devices;
  char **extra_eal_args;
  int num_extra_eal_args;
} oru_fh_dpdk_config_t;

typedef struct {
  uint32_t num_ul_slots;
  uint32_t num_dl_slots;
  uint32_t num_ul_symbols;
  uint32_t num_dl_symbols;
  uint32_t tdd_pattern_length_slots;
} oru_fh_tdd_pattern_t;

typedef struct {
  // Compression configuration
  bool enable_compression;
  int numerology;
  uint16_t num_prbs;
  // MTU configuration
  uint16_t mtu;
  // DU mac addreses, used to prepare Ethernet headers
  char *du_mac_addrs[MAX_RU_PORTS];
  int num_du_mac_addrs;
  // DPDK configuration
  oru_fh_dpdk_config_t dpdk_conf;
  // The main core for RX processing
  int rx_core;
  uint32_t T2a_up_min_uS;
  uint32_t T2a_up_max_uS;
  uint32_t T2a_cp_min_uS;
  uint32_t T2a_cp_max_uS;
  oru_fh_tdd_pattern_t tdd_pattern;
  int prach_eaxc_offset;
} oru_fh_config_t;

/**
 * @brief Initialize the O-RU Fronthaul interface.
 *
 * @param cfg Pointer to the fronthaul configuration structure.
 * @return void* Pointer to the initialized fronthaul handle, or NULL on failure.
 */
void *oru_fh_init(oru_fh_config_t *cfg);

/**
 * @brief Clean up and release resources used by the O-RU Fronthaul interface.
 *
 * @param handle Pointer to the fronthaul handle.
 */
void oru_fh_cleanup(void *handle);

/**
 * @brief Get the number of DL jobs ready to be processed.
 *
 * @param handle Pointer to the fronthaul handle.
 * @return int Number of ready jobs.
 */
int oru_fh_get_ready_jobs(void *handle);

/**
 * @brief Read downlink symbol data (IQ samples) from the Fronthaul interface.
 *
 * @param handle Pointer to the fronthaul handle.
 * @param txdataF Array of pointers to buffers to store the received frequency-domain IQ samples (per TX antenna).
 * @param nb_tx Number of TX antennas.
 * @param frame Pointer to store the frame number of the read symbol.
 * @param slot Pointer to store the slot number of the read symbol.
 * @param symbol Pointer to store the symbol number.
 * @return 0 on success or -1 on failure
 */
int oru_fh_tx_read_symbol(void *handle, uint32_t **txdataF, int nb_tx, int *frame, int *slot, int *symbol);

/**
 * @brief Get the UTC anchor point mapping between 5G time and system time.
 *
 * @param handle Pointer to the fronthaul handle.
 * @param frame Pointer to store the reference frame number.
 * @param slot Pointer to store the reference slot number.
 * @param ts Pointer to a timespec structure to store the corresponding system time.
 * @return 0 on success, negative on error.
 */
int oru_fh_get_utc_anchor_point(void *handle, uint32_t* frame, uint32_t* slot, struct timespec *ts);

/**
 * @brief Send PRACH symbol data (U-Plane) over the Fronthaul interface.
 *
 * @param handle Pointer to the fronthaul handle.
 * @param prachF Array of pointers to buffers containing the frequency-domain PRACH IQ samples.
 * @param nb_rx Number of RX antennas.
 * @param frame Target frame number.
 * @param slot Target slot number.
 * @param symbol Target symbol number.
 */
void oru_fh_rx_send_prach(void *handle, uint32_t **prachF, int nb_rx, int frame, int slot, int symbol);

/**
 * @brief Send PUSCH symbol data (U-Plane) over the Fronthaul interface.
 *
 * @param handle Pointer to the fronthaul handle.
 * @param puschF Array of pointers to buffers containing the frequency-domain PUSCH IQ samples.
 * @param nb_rx Number of RX antennas.
 * @param frame Target frame number.
 * @param slot Target slot number.
 * @param symbol Target symbol number.
 */
void oru_fh_rx_send_pusch(void *handle, uint32_t **puschF, int nb_rx, int frame, int slot, int symbol);

/**
 * @brief Start the O-RU Fronthaul processing threads and loops.
 *
 * @param handle Pointer to the fronthaul handle.
 * @return int 0 on success, negative on error.
 */
int oru_fh_start(void *handle);

/**
 * @brief Stop the O-RU Fronthaul processing.
 *
 * @param handle Pointer to the fronthaul handle.
 */
void oru_fh_stop(void *handle);
void oru_fh_print_stats(void *handle);
#endif
