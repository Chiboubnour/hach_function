// krnl_hach_alveo.cpp
#include "ap_int.h"
#include "hls_stream.h"

#define S 28
#define SMER_SIZE (2 * S)       // 56
#define DATA_DEPTH 1024
#define MEM_UNIT 64
#define PARALLEL_PIPES 8       // 512 bits / 64 bits = 8
#define INPUT_WIDTH 512
#define OUTPUT_WIDTH 512

// -------------------- utilitaires --------------------
inline ap_uint<2> nucl_encode(ap_uint<8> nucl) {
    #pragma HLS INLINE
    switch ((char)nucl) {
        case 'A': return 0;
        case 'C': return 1;
        case 'G': return 2;
        case 'T': return 3;
        default : return 0;
    }
}

inline ap_uint<64> mask_right_int(int numbits) {
    #pragma HLS INLINE
    return (numbits >= MEM_UNIT) ? ~0ULL : ((1ULL << numbits) - 1ULL);
}

// -------------------- hash pipelined (3 stages) --------------------
inline ap_uint<64> hash_stage1(ap_uint<64> k) {
    #pragma HLS INLINE
    k = (~k + (k << 21));
    k = k ^ (k >> 24);
    return k;
}
inline ap_uint<64> hash_stage2(ap_uint<64> k) {
    #pragma HLS INLINE
    k = ((k + (k << 3)) + (k << 8));
    k = k ^ (k >> 14);
    return k;
}
inline ap_uint<64> hash_stage3(ap_uint<64> k, ap_uint<64> mask) {
    #pragma HLS INLINE
    k = ((k + (k << 2)) + (k << 4));
    k = k ^ (k >> 28);
    k = (k + (k << 31)) & mask;
    return k & mask;
}

inline ap_uint<64> bfc_hash_64_pipelined(ap_uint<64> key, ap_uint<64> mask) {
    #pragma HLS INLINE off
    ap_uint<64> k = key;
    k = hash_stage1(k);
    k = hash_stage2(k);
    k = hash_stage3(k, mask);
    return k;
}

// -------------------- unpack 512-bit -> stream of 2-bit nucleotides --------------------
void unpack_sequence_stream_512bit(
    const ap_uint<INPUT_WIDTH>* packed_sequence,
    hls::stream<ap_uint<2>>& sequence_stream,
    int n_bases
) {
    #pragma HLS INLINE off
    const int bases_per_word = 64; // 512 bits / 8 bits per ASCII base
    int word_count = (n_bases + bases_per_word - 1) / bases_per_word;

    // Read each 512-bit word and push up to 64 bases (as 2-bit codes) into sequence_stream
    for (int i = 0; i < word_count; ++i) {
        #pragma HLS PIPELINE II=1
        ap_uint<INPUT_WIDTH> word = packed_sequence[i];
        // Each byte is an ASCII nucleotide; extract bytes
        for (int j = 0; j < bases_per_word; ++j) {
            #pragma HLS PIPELINE II=1
            int idx = i * bases_per_word + j;
            if (idx < n_bases) {
                ap_uint<8> c = (ap_uint<8>)( (word >> (8 * j)) & (ap_uint<INPUT_WIDTH>)0xFF );
                ap_uint<2> nu = nucl_encode(c);
                sequence_stream.write(nu);
            }
        }
    }
}

// -------------------- thread: generate smers and hash in parallel (vectorized) --------------------
void thread_smer_hash_parallel(
    hls::stream<ap_uint<2>>& stream_i,       // input 2-bit bases
    hls::stream<ap_uint<64>>& stream_o,      // output 64-bit hash per smer
    int n_bases                               // number of bases
) {
    #pragma HLS INLINE off
    const ap_uint<64> mask = mask_right_int(SMER_SIZE);
    const int S_local = S;
    const int PAR = PARALLEL_PIPES;

    // window holds the current last SMER_SIZE bits (lsb = most recent base)
    // Use wider container to allow shifting and multiple appends
    ap_uint<128> window = 0;
    int loaded = 0;

    // AMORÇAGE : fill S-1 bases
    while (loaded < (S_local - 1)) {
        #pragma HLS PIPELINE II=1
        ap_uint<2> b = stream_i.read();
        window = ((window << 2) | (ap_uint<128>)b);
        loaded++;
    }

    int processed_bases = S_local - 1;
    bool done = false;

    while (!done) {
        #pragma HLS PIPELINE II=1

        // read up to PAR bases (one group)
        ap_uint<2> new_bases[PAR];
        #pragma HLS ARRAY_PARTITION variable=new_bases complete
        bool valids[PAR];
        int valid_count = 0;

        for (int j = 0; j < PAR; ++j) {
            #pragma HLS UNROLL
            if (processed_bases < n_bases) {
                ap_uint<2> b = stream_i.read();
                new_bases[j] = b;
                valids[j] = true;
                processed_bases++;
                valid_count++;
            } else {
                new_bases[j] = 0;
                valids[j] = false;
            }
        }

        // append PAR bases into window and generate PAR smers
        ap_uint<64> local_smers[PAR];
        #pragma HLS ARRAY_PARTITION variable=local_smers complete

        // local copy to update stepwise
        ap_uint<128> local_window = window;
        for (int k = 0; k < PAR; ++k) {
            #pragma HLS UNROLL
            local_window = ((local_window << 2) | (ap_uint<128>)new_bases[k]);
            // extract SMER_SIZE bits from LSB side (lowest bits are most recent appended)
            // bit range: low=0.. high=SMER_SIZE-1 relative to LSB of local_window
            ap_uint<64> slice = (ap_uint<64>) local_window.range(SMER_SIZE - 1, 0);
            local_smers[k] = slice;
        }
        // update main window (keep enough bits)
        window = local_window;

        // For each local_smers[k], compute reverse complement, canonical, hash and write if valid
        for (int k = 0; k < PAR; ++k) {
            #pragma HLS UNROLL
            if (!valids[k]) {
                // if this batch contained padding only, finish
                done = true;
                break;
            }

            ap_uint<64> slice = local_smers[k];

            // compute reverse complement of slice (SMER_SIZE bits = 2*S bits)
            ap_uint<64> rev = 0;
            // we process base by base (S bits)
            for (int b = 0; b < S_local; ++b) {
                #pragma HLS UNROLL
                ap_uint<2> base = slice.range(2*b+1, 2*b);
                ap_uint<2> cb = (ap_uint<2>)(0x2 ^ base); // complement
                // place complemented base at reversed position
                int hi = SMER_SIZE - 1 - (2*b + 1);
                int lo = SMER_SIZE - 1 - (2*b);
                rev.range(hi, lo) = cb;
            }

            ap_uint<64> vmin = (slice < rev) ? slice : rev;
            ap_uint<64> hash = bfc_hash_64_pipelined(vmin, mask);
            stream_o.write(hash);
        }

        // if we've produced all bases, loop will exit eventually once valids all false
        if (processed_bases >= n_bases && valid_count == 0) done = true;
    }
}

// -------------------- store 64-bit hashes into 512-bit words --------------------
void thread_store_512bit(
    hls::stream<ap_uint<64>>& stream_i,
    ap_uint<OUTPUT_WIDTH>* tab_hash,
    int n_smers
) {
    #pragma HLS INLINE off
    const int hashes_per_word = OUTPUT_WIDTH / 64; // 8
    int out_words = (n_smers + hashes_per_word - 1) / hashes_per_word;

    for (int w = 0; w < out_words; ++w) {
        #pragma HLS PIPELINE II=1
        ap_uint<OUTPUT_WIDTH> out_word = 0;
        for (int h = 0; h < hashes_per_word; ++h) {
            #pragma HLS UNROLL
            int global_idx = w * hashes_per_word + h;
            if (global_idx < n_smers) {
                ap_uint<64> hv = stream_i.read();
                out_word |= ( (ap_uint<OUTPUT_WIDTH>)hv << (h * 64) );
            }
        }
        tab_hash[w] = out_word;
    }
}

// -------------------- top kernel --------------------
extern "C" {
void krnl_hach(
    const ap_uint<INPUT_WIDTH>* sequence,
    const int n,                        
    ap_uint<OUTPUT_WIDTH>* tab_hash        
) {
    #pragma HLS INTERFACE m_axi port=sequence offset=slave bundle=gmem_seq max_read_burst_length=256 num_read_outstanding=16
    #pragma HLS INTERFACE m_axi port=tab_hash offset=slave bundle=gmem_out max_write_burst_length=256 num_write_outstanding=16
    #pragma HLS INTERFACE s_axilite port=n
    #pragma HLS INTERFACE s_axilite port=return
    #pragma HLS DATAFLOW

    const int n_smers = (n >= S) ? (n - (S - 1)) : 0;

    hls::stream<ap_uint<2>, DATA_DEPTH>  seq_stream;
    hls::stream<ap_uint<64>, DATA_DEPTH> hash_stream;

    #pragma HLS STREAM variable=seq_stream depth=2048
    #pragma HLS STREAM variable=hash_stream depth=2048

    unpack_sequence_stream_512bit(sequence, seq_stream, n);

    thread_smer_hash_parallel(seq_stream, hash_stream, n);

    thread_store_512bit(hash_stream, tab_hash, n_smers);
}
}
