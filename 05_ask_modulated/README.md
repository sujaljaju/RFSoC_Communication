# 05 — DMA to DAC with 4-ASK HLS Modulator

**Status: ⚠️ Partial — byte-level TX works, bulk DMA transfer stalls**

---

## Why This Was Needed

Design 04 proved we could transmit data using FSK — but the modulation was happening entirely in Python, which capped throughput at around 51 bits/second. Every symbol required a Python function call, an AXI register write, and a full DMA capture cycle. That's not a communication system — it's a demonstration.

To get real speed, the modulation logic needs to live in hardware, running at the DAC's fabric clock — hundreds of MHz. Python should only need to push raw data bytes into the DMA, and the hardware should handle the symbol mapping automatically.

This design introduces a **custom HLS IP** (`ask_modulator`) written in Vivado HLS C++. It sits between the DMA and the RFDC, receives 8-bit raw data from the DMA, maps every 2-bit pair to one of four amplitude levels (4-ASK), and outputs a 128-bit AXI-Stream directly to the DAC — all in hardware, at fabric clock speed.

**4-ASK symbol mapping:**

| 2-bit Symbol | Amplitude (I value) | Approximate Voltage |
|-------------|--------------------|--------------------|
| `00` | 8191 | Level 1 (lowest) |
| `01` | 16383 | Level 2 |
| `10` | 24575 | Level 3 |
| `11` | 32767 | Level 4 (highest) |

---

## What This Design Does

Python sends raw binary data bytes to the DMA. The DMA streams them to the `ask_modulator` HLS IP. For each byte received, the IP extracts four 2-bit symbols, maps each to an amplitude level, and packs them into a 128-bit word (4 × 32-bit I/Q pairs, Q=0 for real-mode operation) before passing it to the RFDC DAC.

```
Python: binary data bytes
   │
   ▼
DDR Memory
   │  AXI HP
   ▼
AXI DMA (MM2S, 8-bit stream out)
   │  8-bit AXI-Stream
   ▼
ask_modulator HLS IP
   │  reads 8-bit byte → maps 4 symbols → packs 128-bit word
   ▼
RFDC DAC Tile 0
   │  analog output @ 4.9152 GSPS
   ▼
vout00 SMA
```

---

## Files

| File | Description |
|------|-------------|
| `dma_dac_modulated_bd.tcl` | Vivado IP Integrator block design script |
| `dma_dac_modulated_project.tcl` | Full Vivado project restoration script |
| `dma_dac_bd.hwh` | Hardware Handoff file |
| `dma_dac_mod.ipynb` | PYNQ Jupyter notebook |
| `hls/modulator.cpp` | HLS C++ source for `ask_modulator` v1 (8-bit input, 4 symbols/byte) |
| `hls/modulator_tb.cpp` | HLS C++ testbench |

---

## Block Design

**New IP vs Design 01:**

| IP | Role |
|----|------|
| `ask_modulator_0` | **New** — custom HLS IP, sits between DMA and RFDC |
| `axi_dma_0` | DMA — MM2S only, now outputs 8-bit stream to the HLS IP |
| `usp_rf_data_converter_0` | RFDC DAC — now receives 128-bit stream from HLS IP, not directly from DMA |
| `ps8_0_axi_periph` | Extended to 4 masters — adds AXI-Lite control port for the HLS IP |
| Others | Same as Design 01 |

**Data flow change:**
In Design 01: `DMA → RFDC`
In this design: `DMA → ask_modulator → RFDC`

The HLS IP's `s_axi_CTRL` port is connected to the PS control bus, allowing Python to start/stop the IP via a register write (`modulator.write(0x00, 0x01)`).

---

## HLS IP: `ask_modulator` (v1)

```cpp
void ask_modulator(
    hls::stream<axis8>& dma_in,      // 8-bit input from DMA
    hls::stream<axis128>& dac_out    // 128-bit output to RFDC
) {
    #pragma HLS PIPELINE II=1

    const ap_int<16> map[4] = {8191, 16383, 24575, 32767};

    axis8 val_in;
    axis128 val_out;

    if (dma_in.read_nb(val_in)) {
        for (int i = 0; i < 4; i++) {
            #pragma HLS UNROLL
            ap_uint<2> bits = val_in.data.range(i*2+1, i*2);
            ap_int<16> amplitude = map[bits];
            val_out.data.range(i*32+15, i*32) = amplitude;  // I
            val_out.data.range(i*32+31, i*32+16) = 0;       // Q
        }
        val_out.keep = -1;
        val_out.last = val_in.last;
        dac_out.write(val_out);
    }
}
```

Key HLS pragmas:
- `PIPELINE II=1` — process one byte per clock cycle
- `UNROLL` on the symbol loop — all 4 symbols mapped in parallel

---

## How to Run

**Step 1 — Load overlay**
```python
from pynq import Overlay, allocate
import numpy as np

ol = Overlay('dma_dac_bd.bit')
dma = ol.axi_dma_0
modulator = ol.ask_modulator_0
```

**Step 2 — Start the HLS IP**
```python
modulator.write(0x00, 0x01)  # Write to AP_CTRL register to start
```

**Step 3 — Send data (byte-level, works)**
```python
out_buffer = allocate(shape=(1,), dtype=np.uint8)

binary_string = "010101010101010101010101"
data_bytes = [int(binary_string[i:i+8], 2) for i in range(0, len(binary_string), 8)]

for byte_val in data_bytes:
    out_buffer[0] = byte_val
    dma.sendchannel.transfer(out_buffer)
    dma.sendchannel.wait()
```

This sends one byte at a time. The HLS IP processes it, maps the 4 symbols, and outputs a 128-bit word to the DAC. **This works and was verified on the oscilloscope** — the 4-ASK amplitude levels were visible.

---

## Known Issue: Bulk DMA Stall

When attempting to transfer a large buffer (1 million bytes) in a single DMA call:

```python
dma.sendchannel.transfer(test_data)   # starts fine
dma.sendchannel.wait()                # hangs here indefinitely
```

The transfer starts (the DMA prints "Transferring 0.95 MB...") but `wait()` never returns. The DMA interrupt never fires. This was reproducible and never resolved within the internship period.

**Suspected cause:** The HLS IP uses `read_nb` (non-blocking read) with `PIPELINE II=1`. When the DMA pushes a large burst, there may be backpressure or flow control issues between the DMA's TLAST signaling and the HLS IP's consumption rate, causing the DMA to stall waiting for the stream to drain.

The byte-by-byte workaround works but is slow — it defeats the purpose of moving modulation into hardware.

**What's missing:** A fully working bulk pipeline. The hardware modulation concept is sound (the byte-level test confirms the IP logic is correct), but the streaming interface between DMA and the HLS IP needs fixing. Design 06 rearchitects this with a different approach.
