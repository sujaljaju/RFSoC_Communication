# 03 — DAC + ADC Loopback

**Status: ✅ Working**

---

## Why This Was Needed

Designs 01 and 02 proved that the DAC and ADC each work independently. But switching between them meant reprogramming the FPGA every time — which is impractical for any real communication experiment. More importantly, with TX and RX in separate bitfiles, there was no way to send a signal and receive it in the same Python session, let alone in real time.

This design solves that by enabling both the DAC and ADC in a single block design, with a single DMA handling both TX and RX channels. A coaxial SMA cable physically connects the DAC output (`vout00`) back to the ADC input (`vin2_01`), creating a complete loopback path. Whatever we send out, we can immediately read back in Python.

This is the foundation that all subsequent communication experiments are built on.

---

## What This Design Does

Python sends a waveform out the DAC via the DMA MM2S channel. The signal travels through the SMA loopback cable, enters the ADC, gets digitized, and flows back to Python via the DMA S2MM channel. Both directions share a single AXI DMA and a single connection to PS memory.

```
Python sends buffer ──► AXI DMA MM2S ──► RFDC DAC ──► vout00 SMA
                                                            │
                                                      [SMA cable]
                                                            │
Python reads buffer ◄── AXI DMA S2MM ◄── RFDC ADC ◄── vin2_01 SMA
                              ▲
                   axis_subset_converter
```

**Physical requirement:** An SMA coaxial cable connecting `vout00` to `vin2_01` on the board.

---

## Files

| File | Description |
|------|-------------|
| `dac_adc_loopback_bd.tcl` | Vivado IP Integrator block design script |
| `dac_adc_loopback_project.tcl` | Full Vivado project restoration script |
| `dac_adc.hwh` | Hardware Handoff file — also shared with Designs 04 and 07 |
| `ADC_DAC_LOOPBACK.ipynb` | PYNQ Jupyter notebook |

---

## Block Design

**IPs instantiated:**

| IP | Role |
|----|------|
| `zynq_ultra_ps_e_0` | ARM PS |
| `axi_dma_0` | DMA — both MM2S (TX) and S2MM (RX) enabled |
| `usp_rf_data_converter_0` | RFDC — DAC Tile 0 + ADC Tile 2 both active |
| `axis_subset_converter_0` | Adapts ADC AXI-Stream format for DMA S2MM input |
| `axi_smc` | SmartConnect — merges MM2S and S2MM data paths to single HP port |
| `axi_intc_0` | Interrupt controller |
| `xlconcat_0` | Concatenates DMA TX + RX interrupts into one IRQ line to PS |
| `ps8_0_axi_periph` | AXI Interconnect — PS control path |
| `proc_sys_reset_0/1/2` | Resets for three clock domains: PS ctrl, DAC fabric, ADC fabric |

**Key changes from Design 02:**
- DMA now has `c_include_mm2s = 1` and `c_include_s2mm = 1` — both directions active
- A `SmartConnect` (`axi_smc`) replaces the simpler `axi_interconnect` to handle two simultaneous DMA data streams going to the same PS memory port
- `xlconcat_0` merges both DMA interrupts so the PS sees a single IRQ vector
- Three reset domains now — the ADC and DAC run on separate fabric clocks, each needing its own synchronized reset

**ADC fabric clock:** The ADC Tile 2 generates its own fabric clock (`clk_adc2`), separate from the DAC's `clk_dac0`. The S2MM DMA path and its reset must be clocked from `clk_adc2`, not the DAC clock.

---

## How to Run

**Step 1 — Load overlay and set clocks**
```python
from pynq import Overlay, allocate
import xrfclk

ol = Overlay("dac_adc.bit")
xrfclk.set_ref_clks(lmk_freq=245.76, lmx_freq=491.52)
```

**Step 2 — Get DMA handles**
```python
dma_send = ol.axi_dma_0.sendchannel
dma_recv = ol.axi_dma_0.recvchannel
```

**Step 3 — Allocate buffers**
```python
import numpy as np

input_buffer = allocate(shape=(1024,), dtype=np.uint16)
output_buffer = allocate(shape=(8192,), dtype=np.int16)

# Fill send buffer with a signal
input_buffer[:] = 0x7FFF  # DC level — constant amplitude
```

**Step 4 — Send and receive**
```python
dma_send.transfer(input_buffer)
dma_recv.transfer(output_buffer)
dma_recv.wait()

data = np.array(output_buffer)
print(f"Received {len(data)} samples")
```

---

## Known Issues / Notes

- **SMA cable required:** Without the physical loopback cable, `dma_recv.wait()` will block indefinitely — the ADC sees no signal so the DMA never fills its buffer.
- **Clock domain separation:** The ADC and DAC run on different fabric clocks. Any signal path between them in the PL must be carefully handled — this becomes a significant issue in Design 08.
- **What's missing:** This design proves the physical round-trip works, but it carries no *information*. We're just sending a waveform and getting a waveform back — there's no encoding, no decoding, no data. To turn this into a communication system, we need modulation. Design 04 introduces FSK — the simplest form of digital modulation.
