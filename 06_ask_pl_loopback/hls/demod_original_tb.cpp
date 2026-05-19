#include <iostream>
#include <iomanip>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

// Matches the types in your demodulator
typedef ap_axiu<16, 0, 0, 0> axis16;
typedef ap_axiu<128, 0, 0, 0> axis128;

// Function Prototype
void ask_demodulator(hls::stream<axis128>& adc_in, hls::stream<axis16>& dma_out);

int main() {
    hls::stream<axis128> adc_in("adc_in");
    hls::stream<axis16> dma_out("dma_out");

    // --- STEP 1: PREPARE ADC SAMPLES ---
    // We will send 8 symbols in total (Level 0, 1, 2, 3, 0, 1, 2, 3)
    // Expected Bits: 00 01 10 11 00 01 10 11 -> Hex: 0xE4E4
    
    axis128 adc_word1, adc_word2;
    
    // Cycle 1: Symbols 0, 1, 2, 3
    adc_word1.data.range(31, 0)   = 900;      // Level 0 (I=0, Q=0)
    adc_word1.data.range(63, 32)  = 1800;   // Level 1
    adc_word1.data.range(95, 64)  = 2700;  // Level 2
    adc_word1.data.range(127, 96) = 3600;  // Level 3 (Max)
    adc_word1.keep = 0xFFFF;

    // Cycle 2: Symbols 0, 1, 2, 3 (Repeated)
    adc_word2.data.range(31, 0)   = 900;
    adc_word2.data.range(63, 32)  = 1800;
    adc_word2.data.range(95, 64)  = 2700;
    adc_word2.data.range(127, 96) = 3600;
    adc_word2.keep = 0xFFFF;

    // Write to stream
    adc_in.write(adc_word1);
    adc_in.write(adc_word2);

    // --- STEP 2: RUN DEMODULATOR ---
    std::cout << "--- CSIM START: Demodulating ADC Stream ---" << std::endl;
    ask_demodulator(adc_in, dma_out);

    // --- STEP 3: VERIFY OUTPUT ---
    if (!dma_out.empty()) {
        axis16 result = dma_out.read();
        std::cout << "Received 16-bit word: 0x" << std::hex << std::uppercase << (int)result.data << std::endl;
        
        // Expected 0xE4E4 because 11100100 11100100 (binary)
        if (result.data == 0xE4E4) {
            std::cout << "SUCCESS: Data correctly demodulated!" << std::endl;
        } else {
            std::cout << "FAILURE: Expected 0xE4E4, but got 0x" << std::hex << (int)result.data << std::endl;
        }
    } else {
        std::cout << "ERROR: No output produced by demodulator." << std::endl;
    }

    return 0;
}
