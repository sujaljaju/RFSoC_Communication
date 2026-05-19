# 01 — DMA to DAC

**Status: ✅ Working**

---

## The Starting Point

Before anything else — before modulation, before communication, before any of the more complex goals — we needed to answer one basic question: *can we produce an analog RF signal from Python code running on this board?*

The RFSoC 4x2 has powerful RF DAC tiles built into the chip, but they don't just turn on by themselves. They need to be clocked correctly, configured through AXI registers, and fed a continuous stream of digital samples. This design establishes that entire pipeline from scratch — from Python, through the DMA, through the RFDC, out to a physical SMA connector where we could probe the signal with an oscilloscope.

Getting this working was the prerequisite for everything else in this repository.

---

## What This Design Does

Python allocates a buffer in DDR memory, fills it with waveform samples (sine wave, square wave, etc.), and hands it to the AXI DMA. The DMA reads those samples and streams them over AXI-Stream to the RF Data Converter (RFDC), which converts them to an analog signal at the `vout00` SMA connector.

<p align="center">
  <img src="images/01_dma_dac_flow.svg" width="700" />
</p>


---

## Files

| File | Description |
|------|-------------|
| `dma_dac_bd.tcl` | Vivado IP Integrator block design script |
| `dma_dac_project.tcl` | Full Vivado project restoration script |
| `dma_dac_bd.hwh` | Hardware Handoff file — parsed by PYNQ at runtime |
| `DMA_to_DAC_5.ipynb` | PYNQ Jupyter notebook |

---

## Block Design

**IPs instantiated:**

| IP | Role |
|----|------|
| `zynq_ultra_ps_e_0` | ARM PS — runs Linux/PYNQ, provides clocks and AXI ports |
| `axi_dma_0` | DMA engine — MM2S only, 128-bit wide, no scatter-gather |
| `usp_rf_data_converter_0` | RFDC — DAC Tile 0 enabled at 4.9152 GSPS |
| `axi_intc_0` | Interrupt controller — DMA done interrupt routed to PS |
| `ps8_0_axi_periph` | AXI Interconnect — PS to DMA / RFDC / INTC (control) |
| `axi_interconnect_0` | AXI Interconnect — DMA data path to PS HPC port |
| `rst_ps8_0_99M` | Reset synchronizer for 100 MHz control clock domain |
| `rst_ps8_0_99M1` | Reset synchronizer for DAC fabric clock domain |

**Key RFDC configuration:**

| Parameter | Value |
|-----------|-------|
| DAC Tile 0 | Enabled |
| Sampling Rate | 4.9152 GSPS |
| Fabric Clock Out | 307.2 MHz |
| Reference Clock In | 491.52 MHz (from LMX2594) |
| Interpolation Mode | 3× |
| Mixer Type | Fine (NCO-based upconversion) |
| Default NCO Frequency | 100 MHz |

**Two clock domains:**
- **100 MHz** (`pl_clk0` from PS): Control path — DMA AXI-Lite registers, INTC, RFDC config registers
- **~307 MHz** (DAC fabric clock from RFDC): Data path — DMA MM2S output, AXI-Stream into RFDC

These are kept separate because the RFDC data path runs at its own derived clock. The two `proc_sys_reset` blocks ensure each domain gets a clean, synchronized reset.

**Address map:**

| Peripheral | Base Address |
|-----------|-------------|
| AXI DMA | `0xA000_0000` |
| AXI INTC | `0xA001_0000` |
| RFDC | `0xA004_0000` |

---

## How to Run

**Step 1 — Load the overlay and set clocks**
```python
from pynq import Overlay, allocate, PL
import xrfclk

PL.reset()
ol = Overlay('dma_dac_bd.bit')
xrfclk.set_ref_clks(lmk_freq=245.76, lmx_freq=491.52)
```
`PL.reset()` clears any previous bitstream state. The `xrfclk` call programs the LMK04828 and LMX2594 clock chips on the board via SPI — without this the RFDC PLL has no reference and won't lock.

**Step 2 — Configure the RFDC NCO**
```python
import xrfdc

rfdc = xrfdc.RFdc(ol.ip_dict['usp_rf_data_converter_0'])
target_block = rfdc.dac_tiles[0].blocks[0]

def update_nco(rf_block, nco_freq):
    mixer_cfg = rf_block.MixerSettings
    mixer_cfg['Freq'] = nco_freq
    rf_block.MixerSettings = mixer_cfg
    rf_block.UpdateEvent(xrfdc.EVENT_MIXER)

update_nco(target_block, 150)  # 150 MHz carrier
```
The NCO sets the carrier frequency. The actual analog output = baseband signal frequency + NCO frequency.

**Step 3 — Fill buffer and stream continuously**
```python
import numpy as np

input_buffer = allocate(shape=(1024,), dtype='u2')
dma_send = ol.axi_dma_0.sendchannel

amplitude = 33800
t = np.arange(1024)
sine_data = (amplitude * np.sin(2 * np.pi * t / 1024) + amplitude).astype(np.uint16)
np.copyto(input_buffer, sine_data)

try:
    while True:
        dma_send.transfer(input_buffer)
        dma_send.wait()
except KeyboardInterrupt:
    print("Stopped.")
```
`dma_send.wait()` blocks until the DMA interrupt fires, confirming the transfer completed. The loop immediately re-queues the same buffer, producing a continuous repeating signal.

---

## Known Issues / Notes

- **Scope bandwidth:** The DAC runs at 4.9152 GSPS. A typical oscilloscope (200 MHz or 2.5 GSPS bandwidth) cannot faithfully display the output. Use a spectrum analyzer for accurate frequency measurements. The notebook author noted this observation directly.
- **Zero-frequency sine:** Setting `frequency = 0` produces a DC-offset flat line, not an oscillating sine. Set a non-zero frequency relative to the sample rate to get a visible waveform.
- **What's missing:** This design is TX only — there is no way to verify what the signal looks like on the receiving end. We can see it on a scope, but we can't bring it back into Python for analysis. That's what Design 02 solves.
