#include "ap_int.h"
#include "hls_stream.h"
 
#define S 28
#define SMER_SIZE (2 * S)
#define DATA_DEPTH 1024
#define MEM_UNIT 64
#define PARALLEL_PIPES 8
#define INPUT_WIDTH 512
#define OUTPUT_WIDTH 512
 
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
 
void unpack_sequence_stream_512bit(
    const ap_uint<512>* packed_sequence,
    hls::stream<ap_uint<2>>& sequence_stream,
    int n
) {
    int word_count = (n + 63) / 64; 

    for (int i = 0; i < word_count; ++i) {
        ap_uint<512> word = packed_sequence[i];

        for (int j = 0; j < 64; ++j) {
            #pragma HLS PIPELINE II=1
            int idx = i * 64 + j;
            if (idx < n) {
                ap_uint<8> c = (word >> (8 * j)) & 0xFF;
                ap_uint<2> nucl = nucl_encode(c);
                sequence_stream.write(nucl);
            }
        }
    }
}
 
 void thread_smer(
     hls::stream<ap_uint<2>>& stream_i,
     hls::stream<ap_uint<64>>& stream_o,
     int n
 ) {
     ap_uint<64> current_smer = 0;
     const ap_uint<64> mask = mask_right(SMER_SIZE);
 
     for (int i = 0; i < n; i++) {
         #pragma HLS PIPELINE II=1
         ap_uint<2> base = stream_i.read();
         current_smer = ((current_smer << 2) | base) & mask;
         if (i >= S - 1) {
             stream_o.write(current_smer);
         }
     }
 }
 
void thread_hash(
    hls::stream<ap_uint<64>>& stream_i,
    hls::stream<ap_uint<64>>& stream_o,
    int n_smers
) {
    const ap_uint<64> mask = mask_right(SMER_SIZE);

    for (int i = 0; i < n_smers; i++) {
        #pragma HLS PIPELINE II=1
        ap_uint<64> smer = stream_i.read();
        ap_uint<64> hash = bfc_hash_64(smer, mask);
        stream_o.write(hash);
    }
}

void thread_hash_parallel_8x(
    hls::stream<ap_uint<64>>& stream_i,
    hls::stream<ap_uint<64>>& stream_o,
    int n_smers
) {
    const ap_uint<64> mask = mask_right(SMER_SIZE);
    int processed = 0;

    while (processed < n_smers) {
        #pragma HLS PIPELINE II=1
        
        ap_uint<64> hashes[PARALLEL_PIPES];
        #pragma HLS ARRAY_PARTITION variable=hashes complete
        
        for (int j = 0; j < PARALLEL_PIPES && (processed + j) < n_smers; j++) {
            #pragma HLS UNROLL
            ap_uint<64> smer = stream_i.read();
            hashes[j] = bfc_hash_64(smer, mask);
        }
        
        // Écrire les hashs dans l'ordre
        int hashes_to_write = (n_smers - processed < PARALLEL_PIPES) ? 
                              (n_smers - processed) : PARALLEL_PIPES;
                              
        for (int j = 0; j < hashes_to_write; j++) {
            #pragma HLS UNROLL
            stream_o.write(hashes[j]);
        }
        
        processed += hashes_to_write;
    }
}
 
void thread_store_512bit(
    hls::stream<ap_uint<64>>& stream_i,
    ap_uint<512>* tab_hash,
    int n_smers
) {
    int output_words = (n_smers + 7) / 8; 
    
    for (int word_idx = 0; word_idx < output_words; word_idx++) {
        #pragma HLS PIPELINE II=1
        ap_uint<512> output_word = 0;
        
        for (int hash_idx = 0; hash_idx < 8; hash_idx++) {
            #pragma HLS UNROLL
            int global_hash_idx = word_idx * 8 + hash_idx;
            if (global_hash_idx < n_smers) {
                ap_uint<64> hash_value = stream_i.read();
                output_word |= (ap_uint<512>(hash_value) << (hash_idx * 64));
            }
        }
        tab_hash[word_idx] = output_word;
    }
}
 
extern "C" {
void krnl_hach(
    const ap_uint<512>* sequence,
    const int n,
    ap_uint<512>* tab_hash
) {
    #pragma HLS INTERFACE m_axi port=sequence offset=slave bundle=gmem_seq
    #pragma HLS INTERFACE m_axi port=tab_hash offset=slave bundle=gmem_out
    #pragma HLS INTERFACE s_axilite port=n 
    #pragma HLS INTERFACE s_axilite port=return 
    #pragma HLS DATAFLOW

    const int n_smers = n - (S - 1);

    hls::stream<ap_uint<2>, DATA_DEPTH> stream_reader_to_smer;
    hls::stream<ap_uint<64>, DATA_DEPTH> stream_smer_to_hash;
    hls::stream<ap_uint<64>, DATA_DEPTH> stream_hash_to_store;

    unpack_sequence_stream_512bit(sequence, stream_reader_to_smer, n);
    
    thread_smer(stream_reader_to_smer, stream_smer_to_hash, n);
    thread_hash_parallel_8x(stream_smer_to_hash, stream_hash_to_store, n_smers);
    
    thread_store_512bit(stream_hash_to_store, tab_hash, n_smers);
}

}
