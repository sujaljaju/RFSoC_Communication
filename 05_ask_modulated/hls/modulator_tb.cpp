#include <iostream>
#include "hls_stream.h"
#include "ap_axi_sdata.h"

typedef ap_axiu<128, 0, 0, 0> axis128;
typedef ap_axiu<8, 0, 0, 0>   axis8;

void ask_modulator(hls::stream<axis8>& dma_in, hls::stream<axis128>& dac_out);

int main() {
    hls::stream<axis8> dma_in;
    hls::stream<axis128> dac_out;
    const short map[4] = {8191, 16383, 24575, 32767};

    // 1. Prepare input
    axis8 test_input;
    test_input.data = 0b11100100; // Symbols: 3, 2, 1, 0
    test_input.keep = 1;
    test_input.last = 1;
    dma_in.write(test_input);

    std::cout << "--- Starting Fast Mode Simulation ---" << std::endl;

    // 2. Run the function
    ask_modulator(dma_in, dac_out);

    // 3. Verify Mapping
    if (!dac_out.empty()) {
        axis128 result = dac_out.read();
        bool match = true;

        for (int i = 0; i < 4; i++) {
            int bit_pair = (test_input.data >> (i * 2)) & 0x03;
            short expected_amp = map[bit_pair];
            short i_val = result.data.range(i*32 + 15, i*32).to_int();

            std::cout << "Sample " << i << ": Got I=" << i_val << " Exp=" << expected_amp << std::endl;
            if (i_val != expected_amp) match = false;
        }

        if (match) {
            std::cout << "TEST PASSED: Mapping is Correct!" << std::endl;
        } else {
            std::cout << "TEST FAILED: Mapping Mismatch!" << std::endl;
            return 1;
        }
    } else {
        std::cout << "TEST FAILED: No Output!" << std::endl;
        return 1;
    }

    std::cout << "--- Simulation Complete ---" << std::endl;
    return 0;
}
