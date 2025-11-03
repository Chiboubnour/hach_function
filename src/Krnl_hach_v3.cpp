#include "ap_int.h"
#include "hls_stream.h"
#include <cstdint>

#define S 28
#define SMER_SIZE (2 * S)
#define INPUT_WIDTH 512
#define OUTPUT_WIDTH 512
#define WINDOW_SIZE 16
#define STREAM_DEPTH 256 

// =================================================================================
// POINT D'ACTION 3 : Hachage pipeliné pour une fréquence (fmax) plus élevée
// =================================================================================
inline ap_uint<64> bfc_hash_64_pipelined(ap_uint<64> k) {
    #pragma HLS INLINE

    const ap_uint<64> mask = (((ap_uint<64>)1 << SMER_SIZE) - 1);

    k = (~k + (k << 21));
    k = k ^ (k >> 24);
    k = ((k + (k << 3)) + (k << 8));
    k = k ^ (k >> 14);
    k = ((k + (k << 2)) + (k << 4));
    k = k ^ (k >> 28);
    k = (k + (k << 31));
    return k & mask;
}

// =================================================================================
// POINT D'ACTION 1 : Traitement direct depuis la mémoire 512-bit (Élimine le goulot d'étranglement)
// =================================================================================
void thread_smer_from_memory_512(
    const ap_uint<INPUT_WIDTH>* sequence,
    hls::stream<ap_uint<64>>& stream_o,
    uint64_t n_bases
) {
    #pragma HLS INLINE off
    if (n_bases < S) return;

    ap_uint<2 * (S - 1)> overlap_buffer = 0;
    ap_uint<64> n_smers_to_produce = n_bases - S + 1;
    uint64_t words_to_read = (n_bases + 255) / 256; // 256 bases per 512-bit word

    WORD_LOOP:
    for (uint64_t i = 0; i < words_to_read; ++i) {
        #pragma HLS PIPELINE II=1

        ap_uint<INPUT_WIDTH> current_word = sequence[i];

        ap_uint<INPUT_WIDTH + 2 * (S - 1)> processing_window;
        processing_window.range(INPUT_WIDTH - 1, 0) = current_word;
        processing_window.range(INPUT_WIDTH + 2 * (S - 1) - 1, INPUT_WIDTH) = overlap_buffer;

        SMER_GEN_LOOP:
        for (int j = 0; j < 256; ++j) {
            #pragma HLS UNROLL
            
            ap_uint<SMER_SIZE> smer = processing_window.range(SMER_SIZE - 1 + (j * 2), j * 2);
            
            ap_uint<SMER_SIZE> rev_comp = 0;
            for (int b = 0; b < S; ++b) {
                #pragma HLS UNROLL
                ap_uint<2> base = smer.range(2 * b + 1, 2 * b);
                rev_comp.range(SMER_SIZE - 1 - (2 * b), SMER_SIZE - 2 - (2 * b)) = ~base;
            }

            ap_uint<64> canon_smer = (smer < rev_comp) ? (ap_uint<64>)smer : (ap_uint<64>)rev_comp;
            
            if ((i * 256 + j) < n_smers_to_produce) {
                stream_o.write(canon_smer);
            }
        }

        overlap_buffer = current_word.range(INPUT_WIDTH - 1, INPUT_WIDTH - 2 * (S - 1));
    }
}


void thread_hash_stream(
    hls::stream<ap_uint<64>>& stream_i,
    hls::stream<ap_uint<64>>& stream_o,
    uint64_t n_smers
) {
    #pragma HLS INLINE off
    HASH_LOOP:
    for (uint64_t i = 0; i < n_smers; ++i) {
        #pragma HLS PIPELINE II=1
        ap_uint<64> smer = stream_i.read();
        ap_uint<64> hash_val = bfc_hash_64_pipelined(smer);
        stream_o.write(hash_val);
    }
}

// =================================================================================
// POINT D'ACTION 2 : Fenêtre glissante optimisée avec un arbre de comparateurs (II=1 garanti)
// =================================================================================
// Fonction utilitaire pour trouver le minimum dans un tableau via un arbre de réduction
template<int N>
ap_uint<64> tree_reducer(ap_uint<64> win[N]) {
    #pragma HLS INLINE
    ap_uint<64> stage[N];
    #pragma HLS ARRAY_PARTITION variable=stage complete

    // Initialise la première étape
    for(int i=0; i<N; ++i) {
        #pragma HLS UNROLL
        stage[i] = win[i];
    }
    
    // Itère à travers les étapes de l'arbre
    for(int width = N/2; width > 0; width /= 2) {
        #pragma HLS UNROLL
        for(int i=0; i < width; ++i) {
            #pragma HLS UNROLL
            stage[i] = (stage[i] < stage[i+width]) ? stage[i] : stage[i+width];
        }
    }
    return stage[0];
}


void thread_dedup_window_optimized(
    hls::stream<ap_uint<64>>& stream_i,
    hls::stream<ap_uint<64>>& stream_o,
    uint64_t n_smers
) {
    #pragma HLS INLINE off
    if (n_smers == 0) {
        stream_o.write((ap_uint<64>)0);
        return;
    }

    ap_uint<64> window_reg[WINDOW_SIZE];
    #pragma HLS ARRAY_PARTITION variable=window_reg complete

    for (int i = 0; i < WINDOW_SIZE; ++i) {
        #pragma HLS PIPELINE II=1
        if (i < n_smers) {
            window_reg[i] = stream_i.read();
        } else {
            window_reg[i] = (ap_uint<64>)-1; 
        }
    }

    ap_uint<64> last_min = tree_reducer<WINDOW_SIZE>(window_reg);
    stream_o.write(last_min);
    
    DEDUP_LOOP:
    for (uint64_t i = WINDOW_SIZE; i < n_smers; ++i) {
        #pragma HLS PIPELINE II=1
        
        for (int j = 0; j < WINDOW_SIZE - 1; ++j) {
            #pragma HLS UNROLL
            window_reg[j] = window_reg[j + 1];
        }
        window_reg[WINDOW_SIZE - 1] = stream_i.read();

        ap_uint<64> current_min = tree_reducer<WINDOW_SIZE>(window_reg);
        
        if (current_min != last_min) {
            stream_o.write(current_min);
            last_min = current_min;
        }
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
    uint64_t word_idx = 0;

    STORE_LOOP:
    while (true) {
        #pragma HLS PIPELINE II=1
        ap_uint<64> v = stream_i.read();
        if (v == 0) break;

        out_word.range((slot + 1) * 64 - 1, slot * 64) = v;
        out_count++;
        
        if (slot == (OUTPUT_WIDTH / 64 - 1)) {
            tab_hash[word_idx++] = out_word;
            out_word = 0;
            slot = 0;
        } else {
            slot++;
        }
    }
    if (slot != 0) {
        tab_hash[word_idx] = out_word;
    }
    *nMinizrs = out_count;
}


extern "C" {
void Krnl_hach_v3(
    const ap_uint<INPUT_WIDTH>* sequence,
    ap_uint<OUTPUT_WIDTH>* tab_hash,
    ap_uint<64> n_bases,
    ap_uint<64>* nMinizrs
) {
    #pragma HLS INTERFACE m_axi port=sequence offset=slave bundle=gmem_seq max_read_burst_length=256 num_read_outstanding=64
    #pragma HLS INTERFACE m_axi port=tab_hash offset=slave bundle=gmem_out max_write_burst_length=256 num_write_outstanding=64
    #pragma HLS INTERFACE s_axilite port=n_bases
    #pragma HLS INTERFACE s_axilite port=nMinizrs
    #pragma HLS INTERFACE s_axilite port=return
    #pragma HLS DATAFLOW

    ap_uint<64> n_smers = (n_bases >= (ap_uint<64>)(S - 1))
    ? (ap_uint<64>)(n_bases - (ap_uint<64>)(S - 1))
    : (ap_uint<64>)0;


    static hls::stream<ap_uint<64>, STREAM_DEPTH> canon_stream;
    static hls::stream<ap_uint<64>, STREAM_DEPTH> hash_stream;
    static hls::stream<ap_uint<64>, STREAM_DEPTH> dedup_stream;

    #pragma HLS STREAM variable=canon_stream depth=STREAM_DEPTH
    #pragma HLS STREAM variable=hash_stream depth=STREAM_DEPTH
    #pragma HLS STREAM variable=dedup_stream depth=STREAM_DEPTH

    thread_smer_from_memory_512(sequence, canon_stream, n_bases);
    thread_hash_stream(canon_stream, hash_stream, n_smers);
    thread_dedup_window_optimized(hash_stream, dedup_stream, n_smers);
    thread_store_512bit(dedup_stream, tab_hash, nMinizrs);
}
}