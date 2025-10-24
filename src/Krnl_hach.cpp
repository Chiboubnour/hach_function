// krnl_minimizer_alveo.cpp
#include "ap_int.h"
#include "hls_stream.h"
#include <cstdint>

#define S 28
#define SMER_SIZE (2*S)
#define PARALLEL 8
#define INPUT_WIDTH 512
#define OUTPUT_WIDTH 512
#define UNITS_PER_WORD (INPUT_WIDTH / 64)
#define WINDOW_SIZE 16
#define STREAM_DEPTH 1024

inline ap_uint<2> nucl_encode(ap_uint<8> c) {
    #pragma HLS INLINE
    switch ((char)c) {
        case 'A': return 0;
        case 'C': return 1;
        case 'G': return 2;
        case 'T': return 3;
        default:  return 0;
    }
}

inline ap_uint<64> mask_right_int(int numbits) {
    #pragma HLS INLINE
    return (numbits >= 64) ? ~0ULL : ((1ULL << numbits) - 1ULL);
}

inline ap_uint<64> bfc_hash_64_pipelined(ap_uint<64> k, ap_uint<64> mask) {
    #pragma HLS INLINE
    k = (~k + (k<<21));
    k = k ^ (k>>24);
    k = ((k + (k<<3)) + (k<<8));
    k = k ^ (k>>14);
    k = ((k + (k<<2)) + (k<<4));
    k = k ^ (k>>28);
    k = (k + (k<<31)) & mask;
    return k & mask;
}

void unpack_sequence_stream_512bit(
    const ap_uint<INPUT_WIDTH>* sequence,
    hls::stream<ap_uint<2>>& seq_stream,
    uint64_t n_bases
) {
    #pragma HLS INLINE off
    uint64_t words = (n_bases + 63) / 64;
    for (uint64_t i = 0; i < words; ++i) {
        #pragma HLS PIPELINE II=1
        ap_uint<INPUT_WIDTH> word = sequence[i];
        for (int j = 0; j < 64; ++j) {
            uint64_t idx = i * 64 + j;
            if (idx < n_bases) {
                ap_uint<8> c = (word >> (8 * j)) & 0xFF;
                seq_stream.write(nucl_encode(c));
            }
        }
    }
}

void thread_smer_canonical_8x(
    hls::stream<ap_uint<2>>& stream_i,
    hls::stream<ap_uint<64>>& stream_o,
    uint64_t n_bases
) {
    #pragma HLS INLINE off
    ap_uint<SMER_SIZE + 64> window = 0; // margin to shift
    int loaded = 0;

    while (loaded < S - 1) {
        #pragma HLS PIPELINE II=1
        ap_uint<2> b = stream_i.read();
        window = (window << 2) | b;
        loaded++;
    }

    uint64_t processed = S - 1;
    bool done = false;

    while (!done) {
        #pragma HLS PIPELINE II=1
        ap_uint<2> new_bases[PARALLEL];
        #pragma HLS ARRAY_PARTITION variable=new_bases complete
        bool valids[PARALLEL];
        int valid_count = 0;

        for (int k = 0; k < PARALLEL; ++k) {
            if (processed < n_bases) {
                new_bases[k] = stream_i.read();
                valids[k] = true;
                processed++;
                valid_count++;
            } else {
                new_bases[k] = 0;
                valids[k] = false;
            }
        }

        ap_uint<64> smers[PARALLEL];
        #pragma HLS ARRAY_PARTITION variable=smers complete
        ap_uint<SMER_SIZE + 64> local_window = window;

        for (int k = 0; k < PARALLEL; ++k) {
            #pragma HLS UNROLL
            local_window = (local_window << 2) | new_bases[k];
            smers[k] = (ap_uint<64>)local_window.range(SMER_SIZE - 1, 0);
        }
        window = local_window;

        for (int k = 0; k < PARALLEL; ++k) {
            #pragma HLS UNROLL
            if (valids[k]) {
                // reverse complement
                ap_uint<64> rev = 0;
                for (int b = 0; b < S; ++b) {
                    #pragma HLS UNROLL
                    ap_uint<2> base = smers[k].range(2*b + 1, 2*b);
                    ap_uint<2> cb = (ap_uint<2>)(0x2 ^ base);
                    int hi = SMER_SIZE - 1 - (2*b + 1);
                    int lo = SMER_SIZE - 1 - (2*b);
                    rev.range(hi, lo) = cb;
                }
                ap_uint<64> canon = (smers[k] < rev) ? smers[k] : rev;
                stream_o.write(canon);
            } else {
                done = true;
            }
        }

        if (valid_count == 0) done = true;
    }
}

void thread_hash_8x(
    hls::stream<ap_uint<64>>& stream_i,
    hls::stream<ap_uint<64>>& stream_o,
    uint64_t n_smers
) {
    #pragma HLS INLINE off
    const ap_uint<64> mask = mask_right_int(SMER_SIZE);
    uint64_t processed = 0;

    while (processed < n_smers) {
        #pragma HLS PIPELINE II=1
        ap_uint<64> hashes[PARALLEL];
        #pragma HLS ARRAY_PARTITION variable=hashes complete

        for (int j = 0; j < PARALLEL && (processed + j) < n_smers; ++j) {
            #pragma HLS UNROLL
            ap_uint<64> smer = stream_i.read();
            hashes[j] = bfc_hash_64_pipelined(smer & mask, mask);
        }

        int write_count = (n_smers - processed < PARALLEL) ? (n_smers - processed) : PARALLEL;
        for (int j = 0; j < write_count; ++j) {
            #pragma HLS UNROLL
            stream_o.write(hashes[j]);
        }
        processed += write_count;
    }
}

void thread_dedup_window(
    hls::stream<ap_uint<64>>& stream_i,
    hls::stream<ap_uint<64>>& stream_o,
    uint64_t n_smers
) {
    #pragma HLS INLINE off
    if (n_smers == 0) {
        stream_o.write((ap_uint<64>)0);
        return;
    }

    ap_uint<64> buffer[WINDOW_SIZE];
    #pragma HLS ARRAY_PARTITION variable=buffer complete

    int cnt = 0;
    uint64_t idx = 0;

    int init_fill = (n_smers < WINDOW_SIZE) ? (int)n_smers : WINDOW_SIZE;
    for (int p = 0; p < init_fill; ++p) {
        #pragma HLS PIPELINE II=1
        buffer[p] = stream_i.read();
        idx++;
    }

    if (n_smers < WINDOW_SIZE) {
        ap_uint<64> minv = buffer[0];
        for (int p = 1; p < init_fill; ++p) {
            #pragma HLS UNROLL
            if (buffer[p] < minv) minv = buffer[p];
        }
        stream_o.write(minv);
        stream_o.write((ap_uint<64>)0);
        return;
    }

    ap_uint<64> current_min = buffer[0];
    int min_index = 0;
    for (int p = 1; p < WINDOW_SIZE; ++p) {
        #pragma HLS UNROLL
        if (buffer[p] < current_min) { current_min = buffer[p]; min_index = p; }
    }
    stream_o.write(current_min); 
    ap_uint<64> lastElement = current_min;

    int head = 0; 

    while (idx < n_smers) {
        #pragma HLS PIPELINE II=1
        ap_uint<64> v = stream_i.read();
        buffer[head] = v;

        if (min_index == head) {
            ap_uint<64> new_min = buffer[0];
            int new_min_idx = 0;
            for (int p = 1; p < WINDOW_SIZE; ++p) {
                #pragma HLS UNROLL
                if (buffer[p] < new_min) { new_min = buffer[p]; new_min_idx = p; }
            }
            current_min = new_min;
            min_index = new_min_idx;
        } else if (v < current_min) {
            current_min = v;
            min_index = head;
        }
        head = (head + 1) % WINDOW_SIZE;

        if (current_min != lastElement) {
            stream_o.write(current_min);
            lastElement = current_min;
        }
        idx++;
    }

    stream_o.write((ap_uint<64>)0); 
}

void thread_store_512bit(
    hls::stream<ap_uint<64>>& stream_i,
    ap_uint<OUTPUT_WIDTH>* tab_hash,
    ap_uint<64>* nMinizrs
) {
    #pragma HLS INLINE off
    ap_uint<64> out_count = 0;
    ap_uint<OUTPUT_WIDTH> out_word = 0;
    int slot = 0;
    int word_idx = 0;

    while (true) {
        #pragma HLS PIPELINE II=1
        ap_uint<64> v = stream_i.read();
        if (v == 0) break;
        out_word.range((slot+1)*64 - 1, slot*64) = v;
        slot++;
        out_count++;
        if (slot == 8) {
            tab_hash[word_idx++] = out_word;
            out_word = 0;
            slot = 0;
        }
    }
    if (slot != 0) {
        tab_hash[word_idx++] = out_word;
    }
    *nMinizrs = out_count;
}

extern "C" {
void krnl_minimizer(
    const ap_uint<INPUT_WIDTH>* sequence,
    ap_uint<OUTPUT_WIDTH>* tab_hash,
    ap_uint<64> n_bases,
    ap_uint<64>* nMinizrs
) {
    #pragma HLS INTERFACE m_axi port=sequence offset=slave bundle=gmem_seq max_read_burst_length=256  num_read_outstanding=64
    #pragma HLS INTERFACE m_axi port=tab_hash offset=slave bundle=gmem_out max_write_burst_length=256 num_write_outstanding=64
    #pragma HLS INTERFACE s_axilite port=n_bases
    #pragma HLS INTERFACE s_axilite port=nMinizrs
    #pragma HLS INTERFACE s_axilite port=return
    #pragma HLS DATAFLOW

    //ap_uint<64> n_smers = (n_bases >= (ap_uint<64>)(S - 1)) ? (n_bases - (ap_uint<64>)(S - 1)) : (ap_uint<64>)0;
    ap_uint<64> n_smers = (n_bases >= (ap_uint<64>)(S - 1))
    ? (ap_uint<64>)(n_bases - (ap_uint<64>)(S - 1))
    : (ap_uint<64>)0;

    static hls::stream<ap_uint<2>, STREAM_DEPTH> seq_stream;
    static hls::stream<ap_uint<64>, STREAM_DEPTH> canon_stream;
    static hls::stream<ap_uint<64>, STREAM_DEPTH> hash_stream;
    static hls::stream<ap_uint<64>, STREAM_DEPTH> dedup_stream;
    #pragma HLS STREAM variable=seq_stream depth=STREAM_DEPTH
    #pragma HLS STREAM variable=canon_stream depth=STREAM_DEPTH
    #pragma HLS STREAM variable=hash_stream depth=STREAM_DEPTH
    #pragma HLS STREAM variable=dedup_stream depth=STREAM_DEPTH

    unpack_sequence_stream_512bit(sequence, seq_stream, (uint64_t)n_bases);
    thread_smer_canonical_8x(seq_stream, canon_stream, (uint64_t)n_bases);
    thread_hash_8x(canon_stream, hash_stream, (uint64_t)n_smers);
    thread_dedup_window(hash_stream, dedup_stream, (uint64_t)n_smers);
    thread_store_512bit(dedup_stream, tab_hash, nMinizrs);
}
}
