# 08 — FIFO-Buffered DAC + ADC

**Status: ❌ Did not produce results**

---

## Why This Was Attempted

By the time we reached this point, a recurring problem had appeared across several designs: the DAC and ADC run on **separate, asynchronous fabric clocks**. The DMA data path for TX runs at the DAC's clock, and the RX path runs at the ADC's clock. When both are active simultaneously and trying to share the same PS memory port, timing issues and DMA stalls were observed.

The hypothesis was that inserting **AXI-Stream data FIFOs** between the DMA and the RFDC on both the TX and RX paths would act as elastic buffers — absorbing the rate difference between the DMA clock and the RFDC fabric clocks, and smoothing out the flow enough to prevent stalls.

This design adds a dedicated FIFO on each path: `axis_data_fifo_0` on the TX side (DMA → FIFO → DAC) and `axis_data_fifo_1` on the RX side (ADC → FIFO → DMA).

---

## What This Design Attempts

```
AXI DMA MM2S ──► axis_data_fifo_0 ──► RFDC DAC ──► vout00 SMA
                                                         │
                                                   [SMA cable]
                                                         │
AXI DMA S2MM ◄── axis_data_fifo_1 ◄── RFDC ADC ◄── vin2_01 SMA
```

The FIFOs were intended to:
1. Decouple the DMA burst transfers from the continuous RFDC stream
2. Prevent backpressure from the RFDC causing the DMA to stall
3. Allow the two clock domains to operate independently without tight synchronization

---

## Files

| File | Description |
|------|-------------|
| `fifo_bd.tcl` | Vivado IP Integrator block design script |
| `fifo_project.tcl` | Full Vivado project restoration script |
| `dac_adc.hwh` | Hardware Handoff — shared with Designs 03, 04, 07 |
| `initial_sine_cose.ipynb` | First notebook — basic signal generation attempts |
| `stage2_sine_cos.ipynb` | Second notebook — loopback and DMA diagnostic attempts |

---

## Block Design

**IPs instantiated:**

| IP | Role |
|----|------|
| `zynq_ultra_ps_e_0` | ARM PS |
| `axi_dma_0` | DMA — both MM2S and S2MM active |
| `usp_rf_data_converter_0` | RFDC — DAC Tile 0 + ADC Tile 2 |
| `axis_data_fifo_0` | TX FIFO — between DMA MM2S output and RFDC DAC input |
| `axis_data_fifo_1` | RX FIFO — between RFDC ADC output and DMA S2MM input |
| `axi_interconnect_0` | AXI Interconnect — PS control path |
| `axi_interconnect_1` | AXI Interconnect — DMA data paths to PS HPC port |
| `xlconcat_0` | Concatenates TX + RX DMA interrupts |
| `proc_sys_reset_0/1/2` | Resets for three clock domains |

**Notable structural difference from Design 03:**
Design 03 used a `SmartConnect` (`axi_smc`) to merge the two DMA data streams. This design uses two separate `axi_interconnect` instances (`axi_interconnect_0` for control, `axi_interconnect_1` for data). This was part of an attempt to isolate the two DMA channels more completely.

---

## What Happened

The notebooks (`initial_sine_cose.ipynb` and `stage2_sine_cos.ipynb`) document a series of increasingly desperate debugging attempts:

1. **Basic DMA send failed immediately** with `RuntimeError: DMA channel not started` — the DMA channel was not initializing correctly with the FIFO in the path.

2. **Manual register writes attempted** — the team tried writing directly to DMA control registers to force-start the channel, bypassing PYNQ's driver. This produced some activity but the DMA never completed a transfer cleanly.

3. **Nuclear hardware reset attempted** — a cell in `stage2_sine_cos.ipynb` is literally labelled "Executing Nuclear Hardware Reset..." and attempts to reset every register manually. The DMA still stalled.

4. **Status report shows S2MM channel stuck** — DMA status register reads confirmed the S2MM channel was in an error state and would not clear.

From the notebook output:
```
DMA Error: DMA channel not started
--- DMA Status Report ---
Send Channel Status: ...
```

---

## Suspected Root Cause

The FIFO approach, while conceptually reasonable, introduced a new problem: **the FIFOs need to be clocked correctly to act as proper clock-domain crossing buffers.** An `axis_data_fifo` in Vivado has separate slave and master clocks for async mode — but if configured incorrectly (both sides on the same clock, or wrong clock connected), it does not perform clock-domain crossing and the backpressure problem remains or worsens.

It is likely the FIFOs were not configured in asynchronous mode, meaning they didn't actually solve the clock crossing issue and may have introduced additional handshaking deadlocks.

---

## Known Issues / Notes

- This design never produced a working signal. It is included in the repository because the attempt itself is informative — the FIFO approach for clock-domain crossing is valid in principle and worth revisiting with proper async FIFO configuration.
- For async FIFOs in Vivado, the `axis_data_fifo` IP must have independent slave/master clocks enabled, and both clocks must be correctly connected from their respective RFDC clock outputs.
- The correct solution for this clock-domain issue was eventually implemented in Design 06, where the `axis_data_fifo` is configured with the DAC and ADC clocks as independent ports.
