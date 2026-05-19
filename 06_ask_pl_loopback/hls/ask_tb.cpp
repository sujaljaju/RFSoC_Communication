#include <iostream>
#include <iomanip>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

// Redeclare types for the TB
typedef ap_axiu<16, 0, 0, 0> axis16;
typedef ap_axiu<128, 0, 0, 0> axis128;

// Prototype
void ask_modulator(hls::stream<axis16>& dma_in, hls::stream<axis128>& dac_out);

int main() {
    hls::stream<axis16> dma_in;
    hls::stream<axis128> dac_out;

    // Test Pattern: 11 10 01 00 11 10 01 00 (Binary: 11100100 11100100 = 0xE4E4)
    axis16 input_val;
    input_val.data = 0xE4E4;
    input_val.keep = 0x3;
    input_val.last = 1;
    dma_in.write(input_val);

    // Execute the function
    ask_modulator(dma_in, dac_out);

    std::cout << "--- CSIM Results ---" << std::endl;
    int cycle = 0;
    while(!dac_out.empty()){
        axis128 out = dac_out.read();
        cycle++;
        std::cout << "Cycle " << cycle << ":" << std::endl;
        for(int i=0; i<4; i++){
            ap_int<16> I = out.data.range(i*32+15, i*32);
            ap_int<16> Q = out.data.range(i*32+31, i*32+16);
            std::cout << "  Symbol " << (cycle-1)*4 + i << " -> I: " << I << " Q: " << Q << std::endl;
        }
    }

    return 0;
}
