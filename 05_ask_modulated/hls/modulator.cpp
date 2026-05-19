#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

// 128-bit AXI-Stream for RFDC (4 Complex I/Q Pairs)
typedef ap_axiu<128, 0, 0, 0> axis128;
// 8-bit AXI-Stream from DMA
typedef ap_axiu<8, 0, 0, 0>   axis8;

void ask_modulator(
    hls::stream<axis8>& dma_in,
    hls::stream<axis128>& dac_out
) {
    // Function-level pipeline ensures a new byte can be read every clock cycle
    #pragma HLS PIPELINE II=1
    #pragma HLS INTERFACE axis port=dma_in
    #pragma HLS INTERFACE axis port=dac_out
    #pragma HLS INTERFACE s_axilite port=return bundle=CTRL

    // 4-ASK Mapping
    const ap_int<16> map[4] = {8191, 16383, 24575, 32767};

    axis8 val_in;
    axis128 val_out;
    
    // Read the 8-bit sequence from DMA as fast as possible
    if (dma_in.read_nb(val_in)) {
        
        // Map 4 symbols in parallel using UNROLL
        for (int i = 0; i < 4; i++) {
            #pragma HLS UNROLL

            // Extract 2-bit symbol
            ap_uint<2> bits = val_in.data.range(i*2+1, i*2);
            ap_int<16> amplitude = map[bits];

            // Pack into 128-bit word for I/Q -> Real Mode
            int base_bit = i * 32;
            val_out.data.range(base_bit + 15, base_bit)      = amplitude; // I
            val_out.data.range(base_bit + 31, base_bit + 16) = 0;         // Q
        }

        val_out.keep = -1; // All 16 bytes valid
        val_out.last = val_in.last; // Propagate TLAST to the DAC
        
        // Write the 128-bit frame to the DAC immediately
        dac_out.write(val_out);
    }
}
