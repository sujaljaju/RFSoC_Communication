# 02 — ADC to DMA

**Status: ✅ Working**

---

## Why This Was Needed

Design 01 established that we could generate an analog signal from Python and push it out the DAC SMA. The signal was visible on the oscilloscope — but that was the problem. We could *see* it, but we couldn't *process* it. There was no way to bring the received signal back into Python, run analysis on it, or verify its content programmatically.

To build a communication system, we need both ends. This design builds the receive side: it activates the ADC tile, captures whatever is present at the `vin2_01` SMA connector, and streams those samples back into DDR memory via DMA — where Python can read them, run FFT analysis, and measure frequency content.

With this design alone, you can point an RF signal at the ADC input and see it in Python. But TX and RX are still in two separate bitfiles. The next step will combine them.

---

## What This Design Does

The RFDC ADC Tile 2 continuously digitizes the analog signal at `vin2_01` at 4.9152 GSPS. Those samples flow over AXI-Stream through a subset converter (which adapts the stream width) into the DMA's S2MM channel, which writes them to a DDR buffer. Python then reads the buffer and performs frequency analysis.

```
vin2_01 SMA ◄── RF input (signal source or antenna)
   │
   ▼
RFDC ADC Tile 2, Slice 1  (4.9152 GSPS)
   │  AXI-Stream (128-bit @ ADC fabric clock)
   ▼
axis_subset_converter  (adapts stream format for DMA)
   │
   ▼
AXI DMA (S2MM — stream to memory)
   │  AXI HP — S_AXI_HP0_FPD
   ▼
DDR Memory
   │
   ▼
Python / NumPy / FFT
```

---

## Files

| File | Description |
|------|-------------|
| `adc_dma_bd.tcl` | Vivado IP Integrator block design script |
| `adc_dma_project.tcl` | Full Vivado project restoration script |
| `adc_dma.hwh` | Hardware Handoff file — parsed by PYNQ at runtime |
| `adc_example.ipynb` | PYNQ Jupyter notebook |

---

## Block Design

**IPs instantiated:**

| IP | Role |
|----|------|
| `zynq_ultra_ps_e_0` | ARM PS |
| `axi_dma_0` | DMA — S2MM only (`c_include_mm2s = 0`), receive path only |
| `usp_rf_data_converter_0` | RFDC — ADC Tile 2 enabled, DAC disabled |
| `axis_subset_converter_0` | Adapts the RFDC ADC output stream to the DMA's expected format |
| `axi_intc_0` | Interrupt controller |
| `ps8_0_axi_periph` | AXI Interconnect — PS control path |
| `axi_interconnect_0` | AXI Interconnect — DMA data path to PS HP port |
| `proc_sys_reset_0/1` | Resets for control and ADC clock domains |

**Key RFDC configuration:**

| Parameter | Value |
|-----------|-------|
| ADC Tile 2 | Enabled |
| ADC Sampling Rate | 4.9152 GSPS |
| ADC Mixer Type (Tile 2) | Fine (NCO) |
| DAC | Disabled |

**Why the `axis_subset_converter`?**
The RFDC ADC outputs a 128-bit AXI-Stream that includes extra sideband signals and a specific data packing format that the DMA doesn't natively accept. The `axis_subset_converter` strips or remaps those fields so the DMA sees a clean, compatible stream. Getting this configuration right was one of the key debugging steps — a mismatch here produces garbled data silently, with no error.

**Effective ADC sample rate:**
The RFDC runs at 4.9152 GSPS internally, but applies 3× decimation before sending data to the PL fabric. The effective sample rate seen by Python is **1638.4 MSPS**.

---

## How to Run

**Step 1 — Load overlay**
```python
from pynq.overlay import Overlay
import xrfclk

ol = Overlay("adc_dma.bit")
xrfclk.set_ref_clks(lmk_freq=245.76, lmx_freq=491.52)
```

**Step 2 — Capture samples**
```python
from pynq import allocate
import numpy as np

recv_buffer = allocate(shape=(8192,), dtype='u2')
dma_recv = ol.axi_dma_0.recvchannel

dma_recv.transfer(recv_buffer)
dma_recv.wait()

data = np.array(recv_buffer)
```

**Step 3 — FFT analysis**
```python
from scipy.signal import windows

ADC_SAMPLE_RATE_MSPS = 1638.4

centered = data.astype(np.float32) - np.mean(data)
win = windows.blackmanharris(len(centered))
fft_result = np.fft.fft(centered * win)
freqs = np.fft.fftfreq(len(centered), 1 / (ADC_SAMPLE_RATE_MSPS * 1e6))

pos_freqs = freqs[:len(freqs)//2] / 1e6
magnitudes = np.abs(fft_result[:len(fft_result)//2])
peak_freq = pos_freqs[np.argmax(magnitudes[20:])]  # skip DC bins
print(f"Dominant frequency: {peak_freq:.2f} MHz")
```
A Blackman-Harris window is used before FFT to reduce spectral leakage — important when measuring closely-spaced tones.

---

## Known Issues / Notes

- **S2MM only:** This bitfile has no TX capability. You cannot send signals from this design — use Design 01 for that.
- **`axis_subset_converter` sensitivity:** The width and format configuration of this IP must exactly match the RFDC output. This was a source of debugging pain in subsequent designs when ADC tile configurations changed.
- **ADC input port:** ADC Tile 2 uses `vin2_01` — a specific SMA pair on the RFSoC 4x2 board. Make sure you're connected to the right port.
- **What's missing:** We now have TX (Design 01) and RX (Design 02) working separately — but in two different bitfiles. Every time you switch between sending and receiving, you have to reprogram the FPGA. The obvious next step is to put both in the same design, which is what Design 03 does.
