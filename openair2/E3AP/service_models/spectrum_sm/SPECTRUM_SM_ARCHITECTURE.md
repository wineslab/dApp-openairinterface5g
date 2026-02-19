# Spectrum Service Model (SM) Architecture

This document describes the E3 Service Model Spectrum implementation in OpenAirInterface, detailing the IQ data collection mechanism, the PRB control loop, and the xApp-dApp coordination path.

## Overview

The Spectrum SM provides three key functionalities:
1. **Indication (Sensing)**: Collection and forwarding of frequency-domain IQ samples for spectrum monitoring
2. **Control**: Dynamic PRB (Physical Resource Block) blacklisting based on external dApp decisions
3. **xApp-dApp Interaction**: Bidirectional PRB-level information exchange between xApp (via E2) and dApp (via E3), enabling coordinated RAN control from both interfaces

---

## 1. IQ Data Collection (Indication Path)

### 1.1 Data Source Location

The IQ samples are collected in the PHY layer at:
- **File**: `openair1/SCHED_NR/phy_procedures_nr_gNB.c`
- **Function**: `phy_procedures_gNB_uespec_RX()`
- **Data Variable**: `gNB->common_vars.rxdataF[0]`

### 1.2 Signal Processing Stage

The collected IQs are **PRE-EQUALIZATION** samples in the **frequency domain (post-FFT)**.

```
┌─────────────┐    ┌─────────┐    ┌──────────────────────────────────┐    ┌─────────────────┐    ┌─────────────┐
│  RF Frontend │ → │   ADC   │ → │  Time Domain Processing + FFT    │ → │    rxdataF      │ → │ Equalization │
│             │    │         │    │  (CP Removal, Synchronization)   │    │ ◄── E3 COLLECTS │    │             │
└─────────────┘    └─────────┘    └──────────────────────────────────┘    └─────────────────┘    └─────────────┘
                                                                                  │
                                                                                  ▼
                                                                         ┌─────────────────┐
                                                                         │ Channel Est.    │
                                                                         │ rxdataF_comp    │
                                                                         │ (post-equal.)   │
                                                                         └─────────────────┘
```

### 1.3 Position in 5G NR PHY Uplink Processing Chain

| Stage | Description | E3 Agent |
|-------|-------------|----------|
| ADC | Analog-to-Digital Conversion | ✗ Before |
| RF Frontend | Filtering, amplification | ✗ Before |
| Time-domain RX | Sample buffering | ✗ Before |
| CP Removal | Cyclic prefix removal | ✗ Before |
| **FFT** | Time → Frequency transform | ✗ Before |
| **rxdataF** | **Frequency-domain samples** | **✓ COLLECTED HERE** |
| Channel Estimation | Estimate channel response | ✗ After |
| Equalization | Compensate channel effects | ✗ After |
| rxdataF_comp | Equalized samples | ✗ After |
| Demodulation | Symbol → bits | ✗ After |
| LDPC Decoding | Error correction | ✗ After |

### 1.4 Collection Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| **Domain** | Frequency | Post-FFT samples |
| **Stage** | Pre-equalization | Raw channel + signal + noise |
| **Slot filter** | Slot 8 only | `slot_rx == 8` |
| **Symbol** | 12th OFDM symbol | Fixed symbol within slot |
| **Size** | `ofdm_symbol_size` samples | One full OFDM symbol |
| **Antenna** | First antenna (index 0) | Single antenna capture |
| **Data type** | `c16_t` (complex 16-bit) | I/Q interleaved |
| **Sampling control** | `sampling_threshold` | Decimation factor (default: 5) |

### 1.5 Code Reference

```c
// From phy_procedures_nr_gNB.c (lines ~1137-1161)
#ifdef E3_AGENT
  c16_t **rxdataF_sen = gNB->common_vars.rxdataF[0];
  if (nr_slot_select(&gNB->gNB_config, frame_rx, slot_rx) == NR_UPLINK_SLOT && slot_rx == 8) {
    e3_sm_spectrum_control->sampling_counter++;
    if (e3_sm_spectrum_control->sampling_counter > e3_sm_spectrum_control->sampling_threshold) {
      const uint16_t n_symbols = (slot_rx % RU_RX_SLOT_DEPTH) * gNB->frame_parms.symbols_per_slot;
      uint64_t symbol_offset = (n_symbols)*gNB->frame_parms.ofdm_symbol_size + (12) * gNB->frame_parms.ofdm_symbol_size;
      int32_t *rx_signal = (int32_t *)&rxdataF_sen[0][symbol_offset];
      
      T(T_GNB_PHY_UL_FREQ_SENSING_SYMBOL, ...);
      e3_sm_spectrum_control->sampling_counter = 0;
    }
  }
#endif
```

### 1.6 Implications

Since the samples are **pre-equalization**:
- They contain the **combined effect** of: transmitted signal + channel response + noise + interference
- They are **NOT** compensated for channel distortion
- Ideal for **spectrum sensing/monitoring** applications
- Can be used to analyze: interference levels, spectral occupancy, raw signal power per subcarrier

---

## 2. PRB Control (Control Path)

### 2.1 Control Flow Overview

```
┌──────────────┐    ┌──────────────┐    ┌─────────────────────────┐    ┌──────────────────┐
│   dApp/xApp  │ →  │  E3 Agent    │ →  │ spectrum_sm_process_    │ →  │ e3_sm_spectrum_  │
│  (external)  │    │  Interface   │    │ dapp_control_action()   │    │ control struct   │
└──────────────┘    └──────────────┘    └─────────────────────────┘    └──────────────────┘
                                                                                │
                                                                                ▼
┌──────────────┐    ┌──────────────┐    ┌─────────────────────────┐    ┌──────────────────┐
│  Scheduling  │ ←  │  gNB->       │ ←  │ nr_update_prb_policy()  │ ←  │  dyn_prbbl[]     │
│  Decisions   │    │  ulprbbl[]   │    │ (every 128 frames)      │    │  (blacklist)     │
└──────────────┘    └──────────────┘    └─────────────────────────┘    └──────────────────┘
```

### 2.2 Control Data Structure

```c
typedef struct e3_sm_spectrum_control {
  char* action_list;           // Temporary buffer for incoming PRB list
  int action_size;             // Number of PRBs in action_list
  uint16_t dyn_prbbl[MAX_BWP_SIZE];  // Dynamic PRB blacklist bitmap
  int ready;                   // Flag: new policy available
  uint32_t sampling_threshold; // IQ sampling decimation factor
  uint32_t sampling_counter;   // Current sample count
  pthread_mutex_t mutex;       // Thread synchronization
} e3_sm_spectrum_control_t;
```

### 2.3 Control Processing

When a control message arrives from the dApp:

1. **Decode**: `spectrum_sm_process_dapp_control_action()` decodes the PRB control message
2. **Parse**: Extract blacklisted PRB indices from the message
3. **Update**: Set `dyn_prbbl[prb_index] = 0x3FFF` for each blacklisted PRB
4. **Signal**: Set `ready = 1` to indicate new policy is available

```c
// From spectrum_sm.c
for (size_t j = 0; j < elems; ++j) {
    uint16_t prb = alist16[j];
    if (prb < MAX_BWP_SIZE)
        e3_sm_spectrum_control->dyn_prbbl[prb] = 0x3FFF;
}
e3_sm_spectrum_control->ready = 1;
```

### 2.4 Slot 8 Reservation for Spectrum Sensing

To ensure clean IQ samples for spectrum sensing (without UE transmissions), slot 8 is reserved by modifying the ULSCH TDA selection in `gNB_scheduler_ulsch.c`:

```c
// From gNB_scheduler_ulsch.c - get_ul_tda()
#ifdef E3_AGENT
  // Empty symbol in slot 8 for spectrum sensing. Symbol 12.
  // TODO handle when SRS is present
  if (slot == 8) {
    return 1;  // Use TDA index 1 which avoids symbol 12
  }
#endif // E3_AGENT
```

This forces the scheduler to use **TDA index 1** for slot 8, which results in:
- **No UE PUSCH transmission** on symbol 12 of slot 8
- The 12th OFDM symbol remains **free of scheduled UE traffic**
- IQ samples captured are purely **noise + external interference** (no OAI-scheduled signals)

### 2.5 PRB Blacklist Application in MAC Scheduler

The PRB blacklist is applied at multiple points in the MAC scheduler:

#### 2.5.1 Policy Update (Periodic)

```c
// Called every 128 frames (slot 0, frame & 127 == 0) in gNB_scheduler.c
#ifdef E3_AGENT
    nr_update_prb_policy(module_idP, frame, slot);
#endif
```

The `nr_update_prb_policy()` function (in `gNB_scheduler_dlsch.c`):
- Acquires the mutex lock
- Copies `dyn_prbbl` to `gNB->ulprbbl` if `ready == 1`
- Resets `ready = 0`
- Logs the barred PRBs

#### 2.5.2 VRB Map Application (Every Slot)

```c
// Applied every slot during vrb_map clearing in gNB_scheduler.c
#ifdef E3_AGENT
      uint16_t *vrb_map = cc[CC_id].vrb_map[i];
      memcpy(vrb_map, &gNB->ulprbbl, sizeof(uint16_t) * MAX_BWP_SIZE);
#endif
```

This ensures:
- **Uplink**: `vrb_map_UL` is initialized with the blacklist
- **Downlink**: `vrb_map` is initialized with the blacklist (when E3_AGENT enabled)

### 2.6 Effect on Scheduling

When a PRB is blacklisted (`ulprbbl[prb] = 0x3FFF`):
- The scheduler treats those PRBs as **already occupied**
- No UE transmissions (PUSCH/PUCCH) will be scheduled on those PRBs
- No gNB transmissions (PDSCH/PDCCH) will be scheduled on those PRBs

### 2.7 Control Timing

| Event | Timing |
|-------|--------|
| Control message reception | Asynchronous (from dApp) |
| Policy update check | Every 128 frames (~1.28s at 10ms frame) |
| VRB map application | Every slot |

---

## 3. Complete Closed-Loop Architecture

```
                                    ┌─────────────────────────────────────────┐
                                    │              dApp                       │
                                    │  (Spectrum analysis, ML inference, etc.)│
                                    └────────────────┬────────────────────────┘
                                                     │
                                         ┌───────────┴───────────┐
                                         │    E3 Interface       │
                                         │  (encode/decode)      │
                                         └───────────┬───────────┘
                                                     │
                    ┌────────────────────────────────┼────────────────────────────────┐
                    │                                │                                │
                    ▼                                │                                ▼
    ┌───────────────────────────┐                   │           ┌───────────────────────────┐
    │   INDICATION PATH         │                   │           │   CONTROL PATH            │
    │   (PHY Layer)             │                   │           │   (MAC Layer)             │
    │                           │                   │           │                           │
    │   rxdataF (freq domain)   │                   │           │   dyn_prbbl[] → ulprbbl[] │
    │         │                 │                   │           │         │                 │
    │         ▼                 │                   │           │         ▼                 │
    │   T-Tracer event          │                   │           │   vrb_map initialization  │
    │         │                 │                   │           │         │                 │
    │         ▼                 │                   │           │         ▼                 │
    │   spectrum_sm encode      │───────────────────┘           │   Scheduler decisions     │
    │         │                 │                               │                           │
    │         ▼                 │                               └───────────────────────────┘
    │   sm_indication_data_set  │
    └───────────────────────────┘
```

| Parameter | Default | Location | Description |
|-----------|---------|----------|-------------|
| `sampling_threshold` | 5 | spectrum_sm.c | IQ capture every N×10ms |
| `slot_rx` filter | 8 | phy_procedures_nr_gNB.c | Only capture from slot 8 |
| Symbol index | 12 | phy_procedures_nr_gNB.c | 12th symbol of the slot |
| Policy update period | 128 frames | gNB_scheduler.c | ~1.28s between updates |
| `MAX_BWP_SIZE` | 275 | nr_common.h | Maximum PRBs supported |

---

## 4. xApp-dApp Interaction (E2-E3 Bridge)

When the E3 Agent is compiled with `E2_AGENT` support, the Spectrum SM participates in a closed loop between an external xApp (via E2) and the local dApp (via E3). Two additional ASN.1 structures support this interaction.

### 4.1 xApp → dApp Control (`Spectrum-PRBBlockedControl`)

The xApp sends a `Spectrum-PRBBlockedControl` message to instruct the dApp about which PRBs should be blocked. This message travels through the E2 interface into the E3 Agent, which forwards it to the connected dApp.

```asn1
Spectrum-PRBBlockedControl ::= SEQUENCE {
    blockedPRBs   SEQUENCE (SIZE (0..maxPRBs)) OF INTEGER (0..maxPRBIndex)
}
```

**Flow**:

```
┌──────────┐    ┌──────────┐    ┌─────────────────────────┐    ┌──────────┐
│   xApp   │ →  │ E2 Agent │ →  │        E3 Agent         │ →  │   dApp   │
│          │    │          │    │ (ran_to_e3_agent_queue) │    │          │
└──────────┘    └──────────┘    └─────────────────────────┘    └──────────┘
```

- The E2 Agent receives the xApp control action and pushes it into `ran_to_e3_agent_queue` (defined in `e3_agent.h`)
- The E3 Agent publisher thread dequeues the message and forwards it to the dApp over the outbound E3 connection
- The dApp can then use this information alongside its own spectrum analysis to decide the final PRB blacklist

### 4.2 dApp → xApp Report (`Spectrum-PRBBlacklistReport`)

The dApp sends a `Spectrum-PRBBlacklistReport` message back to the xApp to communicate the current PRB blacklist state. This message arrives at the E3 Agent as a `E3AP_PDU_TYPE_DAPP_REPORT` and is forwarded to the E2 interface.

```asn1
Spectrum-PRBBlacklistReport ::= SEQUENCE {
    blacklistedPRBs   SEQUENCE (SIZE (0..maxPRBs)) OF INTEGER (0..maxPRBIndex)
}
```

**Flow**:

```
┌──────────┐    ┌──────────────────────────────────────┐    ┌──────────┐    ┌──────────┐
│   dApp   │ →  │    E3 Agent (subscriber_thread)      │ →  │ E2 Agent │ →  │   xApp   │
│          │    │ generate_e2_indication_from_e3_dapp_ │    │          │    │          │
│          │    │ report()                             │    │          │    │          │
└──────────┘    └──────────────────────────────────────┘    └──────────┘    └──────────┘
```

- The E3 Agent subscriber thread receives the dApp report (`e3_agent.c`)
- When `E2_AGENT` is enabled, it calls `generate_e2_indication_from_e3_dapp_report()` to translate the E3 report into an E2 indication
- The E2 Agent delivers the indication to the subscribed xApps

### 4.3 Complete xApp-dApp Closed Loop

```
┌──────────┐                                                          ┌──────────┐
│          │  ──► Spectrum-PRBBlockedControl ──  E2 Agent  ──►  E3 ──►│          │
│   xApp   │                                                          │   dApp   │
│          │  ◄── Spectrum-PRBBlacklistReport ── E2 Agent  ◄──  E3 ◄──│          │
└──────────┘                                                          └──────────┘
     │                                                                     │
     │ E2 Control                                                          │ E3 Control
     ▼                                                                     ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│                                  gNB (RAN)                                      │
└─────────────────────────────────────────────────────────────────────────────────┘
```

Both the xApp and the dApp can independently issue control actions to the RAN. The xApp sends controls via the E2 interface, while the dApp sends controls via the E3 interface. The bidirectional xApp-dApp channel (`Spectrum-PRBBlockedControl` / `Spectrum-PRBBlacklistReport`) allows them to exchange PRB-level information, enabling coordinated decision-making before either entity acts on the RAN.

---

## 5. Files Reference

RAN

| File | Purpose |
|------|---------|
| `openair1/SCHED_NR/phy_procedures_nr_gNB.c` | IQ sample collection point |
| `openair2/E3AP/service_models/spectrum_sm/spectrum_sm.c` | SM main logic, T-tracer interface |
| `openair2/E3AP/service_models/spectrum_sm/spectrum_sm.h` | Data structures, function declarations |
| `openair2/E3AP/service_models/spectrum_sm/spectrum_enc.c` | Indication message encoding |
| `openair2/E3AP/service_models/spectrum_sm/spectrum_dec.c` | Control message decoding |
| `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler.c` | Policy application in scheduler |
| `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_dlsch.c` | `nr_update_prb_policy()` implementation |
| `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_ulsch.c` | Slot 8 TDA reservation for sensing |

xApp-dApp Bridge

| File | Purpose |
|------|---------|
| `openair2/E3AP/e3_agent.h` | Declares `ran_to_e3_agent_queue` (E2→E3 bridge queue) |
| `openair2/E3AP/e3_agent.c` | `subscriber_thread()` handles dApp reports; publisher thread forwards xApp controls |
| `openair2/E3AP/ran_func_dapp_extern.h` | `generate_e2_indication_from_e3_dapp_report()` declaration |


---
