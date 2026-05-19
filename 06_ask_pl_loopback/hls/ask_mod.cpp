#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

typedef ap_axiu<16, 0, 0, 0> axis16;
typedef ap_axiu<128, 0, 0, 0> axis128;

void ask_modulator(
    hls::stream<axis16>& dma_in,
    hls::stream<axis128>& dac_out
) {
    #pragma HLS INTERFACE axis port=dma_in
    #pragma HLS INTERFACE axis port=dac_out
    #pragma HLS INTERFACE s_axilite port=return bundle=CTRL

    static ap_uint<16> latched_data = 0;
    axis16 val_in;
    axis128 val_out;

    // 1. Read new data if it arrives, otherwise use the last value
    if (dma_in.read_nb(val_in)) {
        latched_data = val_in.data;
    }

    // 2. Output First 128-bit Word (Symbols 0-3)
    #pragma HLS PIPELINE II=1
    for (int i = 0; i < 4; i++) {
        #pragma HLS UNROLL
        ap_uint<2> bits = latched_data.range(i * 2 + 1, i * 2);
        ap_int<16> amp = (bits == 0) ? 8191 : (bits == 1) ? 16383 : (bits == 2) ? 24575 : 32767;

        val_out.data.range(i * 32 + 15, i * 32) = amp;    // I
        val_out.data.range(i * 32 + 31, i * 32 + 16) = 0; // Q
    }
    val_out.keep = 0xFFFF;
    val_out.last = 0;
    dac_out.write(val_out);

    // 3. Output Second 128-bit Word (Symbols 4-7)
    for (int i = 0; i < 4; i++) {
        #pragma HLS UNROLL
        ap_uint<2> bits = latched_data.range((i + 4) * 2 + 1, (i + 4) * 2);
        ap_int<16> amp = (bits == 0) ? 8191 : (bits == 1) ? 16383 : (bits == 2) ? 24575 : 32767;

        val_out.data.range(i * 32 + 15, i * 32) = amp;    // I
        val_out.data.range(i * 32 + 31, i * 32 + 16) = 0; // Q
    }
    val_out.keep = 0xFFFF;
    val_out.last = 0;
    dac_out.write(val_out);
}
