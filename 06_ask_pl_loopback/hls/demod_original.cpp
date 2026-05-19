#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

typedef ap_axiu<16, 0, 0, 0> axis16;
typedef ap_axiu<128, 0, 0, 0> axis128;

void ask_demodulator(
    hls::stream<axis128>& adc_in,
    hls::stream<axis16>& dma_out
) {
    #pragma HLS INTERFACE axis port=adc_in
    #pragma HLS INTERFACE axis port=dma_out
    #pragma HLS INTERFACE s_axilite port=return bundle=CTRL

    // Midpoints between levels: 0, 8191, 16383, 24575, 32767
    const int T2 = 1200;
    const int T3 = 2247;
    const int T4 = 3271;


    axis128 val_in;
    axis16 val_out;
    ap_uint<16> packed_data = 0;

    // We must process 2 ADC cycles to produce 1 DMA word (8 symbols)
    for (int cycle = 0; cycle < 2; cycle++) {
        #pragma HLS PIPELINE II=1
        // Blocking read: wait for the FIFO to provide data
        val_in = adc_in.read(); 

        for (int i = 0; i < 4; i++) {
            #pragma HLS UNROLL
            // Extract I component
            ap_int<16> i_sample = val_in.data.range(i * 32 + 15, i * 32);
            ap_uint<16> mag = (i_sample < 0) ? (ap_uint<16>)-i_sample : (ap_uint<16>)i_sample;

            ap_uint<2> symbol;
            if (mag < T2)      symbol = 0;
            else if (mag < T3) symbol = 1;
            else if (mag < T4) symbol = 2;
            else               symbol = 3;

            packed_data.range((cycle * 4 + i) * 2 + 1, (cycle * 4 + i) * 2) = symbol;
        }
    }

    val_out.data = packed_data;
    val_out.keep = 0x3;
    val_out.last = 0; 
    dma_out.write(val_out);
}
