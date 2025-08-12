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
 
 void unpack_sequence_stream_v2(
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
 
 void thread_store(
     hls::stream<ap_uint<64>>& stream_i,
     ap_uint<64>* tab_hash,
     int n_smers
 ) {
     for (int i = 0; i < n_smers; i++) {
         #pragma HLS PIPELINE II=1
         tab_hash[i] = stream_i.read();
     }
 }
 
 extern "C" {
 void krnl_hach(
     const ap_uint<64>* sequence,
     const int n,
     ap_uint<64>* tab_hash
 ) {
     #pragma HLS INTERFACE m_axi port=sequence offset=slave bundle=gmem_seq
     #pragma HLS INTERFACE m_axi port=tab_hash offset=slave bundle=gmem_out
     #pragma HLS INTERFACE s_axilite port=n bundle=control
     #pragma HLS INTERFACE s_axilite port=return bundle=control
     #pragma HLS DATAFLOW
 
     const int n_smers = n - (S - 1);
 
     hls::stream<ap_uint<2>, DATA_DEPTH> stream_reader_to_smer;
     hls::stream<ap_uint<64>, DATA_DEPTH> stream_smer_to_hash;
     hls::stream<ap_uint<64>, DATA_DEPTH> stream_hash_to_store;
 
     unpack_sequence_stream_v2(sequence, stream_reader_to_smer, n);
     thread_smer(stream_reader_to_smer, stream_smer_to_hash, n);
     thread_hash(stream_smer_to_hash, stream_hash_to_store, n_smers);
     thread_store(stream_hash_to_store, tab_hash, n_smers);
 }
 }
 