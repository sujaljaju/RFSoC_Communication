# 07 — ASK DAC + ADC (Software Demodulation)

**Status: ⚠️ Experimental**

---

## Why This Was Needed

Design 06 had both modulation and demodulation running in hardware, and it worked — but the amplitude thresholds in the HLS demodulator were fixed constants. In practice, the received signal amplitude varies depending on cable length, connector quality, ADC gain settings, and carrier frequency. Fixed thresholds that work at 100 MHz might fail at 400 MHz.

This design takes a different approach: keep the hardware modulator (`ask_modulator`) for fast symbol output from the DAC, but bring the demodulation back into Python. Python is flexible — it can calibrate thresholds, adapt to signal conditions, and run more sophisticated decoding logic. The trade-off is throughput, but the goal here was correctness and exploration rather than speed.

This design also uses the `dac_adc.bit` hardware (same as Designs 03 and 04) — no new HLS demodulator IP, no hardware RX processing.

---

## What This Design Does

Python sends waveform data through the DMA to the RFDC DAC. The DAC output goes through the SMA loopback cable to the ADC. Python reads the ADC samples back and performs amplitude-based decoding in software — measuring the RMS of the received signal and comparing it to calibrated thresholds to recover the transmitted symbols.

```
Python: binary data
   │
   ▼
AXI DMA (MM2S) ──► RFDC DAC ──► vout00 SMA
                                      │
                                [SMA cable]
                                      │
RFDC ADC ◄── vin2_01 SMA
   │
   ▼
AXI DMA (S2MM) ──► DDR
   │
   ▼
Python: RMS measurement → threshold comparison → decoded bits
```

---

## Files

| File | Description |
|------|-------------|
| `ask_dac_adc_bd.tcl` | Vivado IP Integrator block design script |
| `ask_dac_adc_project.tcl` | Full Vivado project restoration script |
| `dac_adc.hwh` | Hardware Handoff — shared with Designs 03 and 04 |
| `ask_loopback.ipynb` | PYNQ Jupyter notebook |

---

## Block Design

Hardware is identical to Design 03 (`dac_adc_loopback`). Both MM2S and S2MM DMA channels are active, both DAC Tile 0 and ADC Tile 2 are enabled, and the `axis_subset_converter` adapts the ADC stream for DMA. The TCL files are preserved here for independent rebuild.

---

## Software Approach: RMS-Based Decoding

Instead of FFT (used in Design 04 for FSK), this design uses **RMS amplitude** to distinguish between the four ASK levels. The idea: each 2-bit symbol produces a different amplitude at the DAC output. After travelling through the cable and being digitized by the ADC, that amplitude difference should still be detectable in the received samples.

**Calibration step** (run before transmitting):
```python
CARRIER_FREQ = 800  # MHz — set the DAC NCO

# Send each known level, measure received RMS
# Build a lookup table: level → expected RMS range
```

**Decoding step:**
```python
# For each received symbol block:
rms = np.sqrt(np.mean(data.astype(np.float32)**2))
# Map RMS to nearest calibrated level → recover 2-bit symbol
```

---

## Results

From `ask_loopback.ipynb`, a calibration run at 800 MHz carrier:

```
--- Calibrating Channel at 800 MHz ---
Calibration Saved. Max RMS at this freq: [value]
```

Sequence transmission was attempted, with mixed results. The decoded accuracy was inconsistent across runs — the RMS thresholds were sensitive to small changes in cable connection quality and the exact NCO frequency setting.

---

## Known Issues / Notes

- **Carrier frequency sensitivity:** At higher carrier frequencies (800 MHz), the received amplitude was lower and the four ASK levels were harder to distinguish. The RMS spread between adjacent levels narrowed, making threshold-based decoding unreliable.
- **No hardware modulator here:** Unlike Design 05/06, this design uses Python-controlled waveforms directly — no HLS modulator IP. The amplitude levels are set by writing different sample values to the DMA buffer.
- **Fundamental limitation:** Software demodulation via RMS works in ideal conditions but is fragile over a real (even cable) channel. Hardware demodulation as in Design 06 is more robust.
- **Status:** This design was left in an experimental state. The concept is sound, but consistent results were not achieved within the internship period. It is preserved here as a documented exploration.
