/********************************************************************************************
_______________________
|                       |<----- Sequence Input (packed 64 bits words)
|     unpack_sequence    |       (N * 64 bits, N = (sequence_length+7)/8)
|_______________________|----->|  |
 ______________________       |  | stream1: 2 bits/base, débit de N*8 bases
Sequence Packed -------->|                     |      |__|
   |   generate_smers    |        |
   |_____________________|------->|  |
 ______________________       __|  | stream2: 64 bits/s-mer (s=28 bases, 56 bits utiles)
|                      |<-----|   |
|    compute_hashes    |         |
|______________________|------->|  | stream3: 64 bits/hash
 ______________________       __|  |
|                      |<-----|   |
|    store_hashes      |         |
|______________________|-------> Output Hash Array (64 bits * (N - s + 1))

 *  *****************************************************************************************/

#include "ap_int.h"
#include "hls_stream.h"

#define S 28
#define SMER_SIZE (2 * S)
#define DATA_DEPTH 1024
#define MEM_UNIT 64

inline ap_uint<2> nucl_encode(ap_uint<8> nucl) {
    #pragma HLS INLINE
    switch (nucl) {
        case 'A': return 0;
        case 'C': return 1;
        case 'G': return 2;
        case 'T': return 3;
        default : return 0;
    }
}

inline ap_uint<64> mask_right(int numbits) {
    #pragma HLS INLINE
    return (numbits >= MEM_UNIT) ? ~0ULL : ((1ULL << numbits) - 1ULL);
}

inline ap_uint<64> bfc_hash_64(ap_uint<64> key, ap_uint<64> mask) {
    #pragma HLS INLINE
    key = (~key + (key << 21)) & mask;
    key = key ^ (key >> 24);
    key = ((key + (key << 3)) + (key << 8)) & mask;
    key = key ^ (key >> 14);
    key = ((key + (key << 2)) + (key << 4)) & mask;
    key = key ^ (key >> 28);
    key = (key + (key << 31)) & mask;
    return key;
}

void unpack_sequence(
    const ap_uint<64>* packed_sequence,
    hls::stream<ap_uint<2>>& sequence_stream,
    int n
) {
    int word_count = (n + 7) / 8;
    for (int i = 0; i < word_count; ++i) {
        ap_uint<64> word = packed_sequence[i];
        for (int j = 0; j < 8; ++j) {
            #pragma HLS PIPELINE II=1
            int idx = i * 8 + j;
            if (idx < n) {
                ap_uint<8> c = (word >> (8 * j));
                ap_uint<2> nucl = nucl_encode(c);
                sequence_stream.write(nucl);
            }
        }
    }
}

void generate_smers(
    hls::stream<ap_uint<2>>& in,
    hls::stream<ap_uint<64>>& out,
    int n
) {
    ap_uint<64> smer = 0;
    const ap_uint<64> mask = mask_right(SMER_SIZE);
    for (int i = 0; i < n; i++) {
        #pragma HLS PIPELINE II=1
        ap_uint<2> base = in.read();
        smer = ((smer << 2) | base) & mask;
        if (i >= S - 1) {
            out.write(smer);
        }
    }
}

void compute_hashes(
    hls::stream<ap_uint<64>>& in,
    hls::stream<ap_uint<64>>& out,
    int n_smers
) {
    const ap_uint<64> mask = mask_right(SMER_SIZE);
    for (int i = 0; i < n_smers; i++) {
        #pragma HLS PIPELINE II=1
        ap_uint<64> smer = in.read();
        out.write(bfc_hash_64(smer, mask));
    }
}

void store_hashes(
    hls::stream<ap_uint<64>>& in,
    ap_uint<64>* out,
    int n_smers
) {
    for (int i = 0; i < n_smers; i++) {
        #pragma HLS PIPELINE II=1
        out[i] = in.read();
    }
}

extern "C" {
void krnl_hash_dna(
    const ap_uint<64>* sequence,
    ap_uint<64>* tab_hash,
    int n
) {
    #pragma HLS INTERFACE m_axi port=sequence offset=slave bundle=gmem0
    #pragma HLS INTERFACE m_axi port=tab_hash offset=slave bundle=gmem1
    #pragma HLS INTERFACE s_axilite port=n
    #pragma HLS INTERFACE s_axilite port=return
    #pragma HLS DATAFLOW

    const int n_smers = n - (S - 1);

    hls::stream<ap_uint<2>, DATA_DEPTH> stream1;
    hls::stream<ap_uint<64>, DATA_DEPTH> stream2;
    hls::stream<ap_uint<64>, DATA_DEPTH> stream3;

    unpack_sequence(sequence, stream1, n);
    generate_smers(stream1, stream2, n);
    compute_hashes(stream2, stream3, n_smers);
    store_hashes(stream3, tab_hash, n_smers);
}
}
