#include "ap_int.h"
#include "hls_stream.h"

#define S 28
#define SMER_SIZE (2*S)
#define PARALLEL 8
#define INPUT_WIDTH 512
#define OUTPUT_WIDTH 512

inline ap_uint<2> nucl_encode(ap_uint<8> nucl) {
    #pragma HLS INLINE
    switch(nucl){
        case 'A': return 0;
        case 'C': return 1;
        case 'G': return 2;
        case 'T': return 3;
        default : return 0;
    }
}

inline ap_uint<64> mask_right_int(int numbits){
    #pragma HLS INLINE
    return (numbits>=64)? ~0ULL : ((1ULL<<numbits)-1ULL);
}

inline ap_uint<64> bfc_hash_64_pipelined(ap_uint<64> k, ap_uint<64> mask){
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

ap_uint<64> reverse_complement(ap_uint<64> smer){
    #pragma HLS INLINE
    ap_uint<64> rev = 0;
    for(int b=0;b<S;b++){
        #pragma HLS UNROLL
        ap_uint<2> base = smer.range(2*b+1, 2*b);
        ap_uint<2> cb = 0x2 ^ base;
        int hi = SMER_SIZE-1-(2*b+1);
        int lo = SMER_SIZE-1-(2*b);
        rev.range(hi,lo)=cb;
    }
    return rev;
}


void unpack_sequence_stream_512bit(const ap_uint<INPUT_WIDTH>* sequence,
                                   hls::stream<ap_uint<2>>& seq_stream,
                                   int n_bases){
    #pragma HLS INLINE off
    int words = (n_bases + 63)/64;
    for(int i=0;i<words;i++){
        #pragma HLS PIPELINE II=1
        ap_uint<INPUT_WIDTH> word = sequence[i];
        for(int j=0;j<64;j++){
            int idx = i*64 + j;
            if(idx<n_bases){
                ap_uint<8> c = (word >> (8*j)) & 0xFF;
                seq_stream.write(nucl_encode(c));
            }
        }
    }
}

void thread_smer_canonical_8x(hls::stream<ap_uint<2>>& stream_i,
                              hls::stream<ap_uint<64>>& stream_o,
                              int n_bases){
    #pragma HLS INLINE off
    ap_uint<128> window = 0;
    int loaded=0;

    while(loaded<S-1){
        #pragma HLS PIPELINE II=1
        window = (window<<2) | stream_i.read();
        loaded++;
    }

    int processed=S-1;
    bool done=false;

    while(!done){
        #pragma HLS PIPELINE II=1
        ap_uint<2> new_bases[PARALLEL];
        #pragma HLS ARRAY_PARTITION variable=new_bases complete
        bool valids[PARALLEL];
        int valid_count=0;

        for(int k=0;k<PARALLEL;k++){
            if(processed<n_bases){
                new_bases[k] = stream_i.read();
                valids[k]=true;
                processed++;
                valid_count++;
            } else{
                new_bases[k]=0;
                valids[k]=false;
            }
        }

        ap_uint<64> smers[PARALLEL];
        #pragma HLS ARRAY_PARTITION variable=smers complete
        ap_uint<128> local_window = window;

        for(int k=0;k<PARALLEL;k++){
            #pragma HLS UNROLL
            local_window = (local_window<<2) | new_bases[k];
            smers[k] = (ap_uint<64>)local_window.range(SMER_SIZE-1,0);
        }
        window = local_window;

        for(int k=0;k<PARALLEL;k++){
            #pragma HLS UNROLL
            if(valids[k]){
                ap_uint<64> rev = reverse_complement(smers[k]);
                ap_uint<64> canon = (smers[k]<rev)? smers[k]: rev;
                stream_o.write(canon);
            } else done=true;
        }

        if(valid_count==0) done=true;
    }
}

void thread_hash_8x(hls::stream<ap_uint<64>>& stream_i,
                    hls::stream<ap_uint<64>>& stream_o,
                    int n_smers){
    #pragma HLS INLINE off
    const ap_uint<64> mask = mask_right_int(SMER_SIZE);
    int processed=0;

    while(processed<n_smers){
        #pragma HLS PIPELINE II=1
        ap_uint<64> hashes[PARALLEL];
        #pragma HLS ARRAY_PARTITION variable=hashes complete

        for(int j=0;j<PARALLEL && (processed+j)<n_smers;j++){
            #pragma HLS UNROLL
            ap_uint<64> smer = stream_i.read();
            hashes[j] = bfc_hash_64_pipelined(smer, mask);
        }

        int write_count = (n_smers-processed<PARALLEL)? n_smers-processed: PARALLEL;
        for(int j=0;j<write_count;j++){
            #pragma HLS UNROLL
            stream_o.write(hashes[j]);
        }
        processed+=write_count;
    }
}

void thread_store_512bit(hls::stream<ap_uint<64>>& stream_i,
                         ap_uint<OUTPUT_WIDTH>* tab_hash,
                         int n_smers){
    #pragma HLS INLINE off
    int words = (n_smers+7)/8;
    for(int w=0;w<words;w++){
        #pragma HLS PIPELINE II=1
        ap_uint<OUTPUT_WIDTH> out=0;
        for(int k=0;k<8;k++){
            #pragma HLS UNROLL
            int idx=w*8+k;
            if(idx<n_smers) out |= (ap_uint<OUTPUT_WIDTH>)stream_i.read() << (k*64);
        }
        tab_hash[w]=out;
    }
}

extern "C" {
void krnl_hach(const ap_uint<INPUT_WIDTH>* sequence,int n,
               ap_uint<OUTPUT_WIDTH>* tab_hash){
    #pragma HLS INTERFACE m_axi port=sequence offset=slave bundle=gmem_seq max_read_burst_length=1024 num_read_outstanding=32
    #pragma HLS INTERFACE m_axi port=tab_hash offset=slave bundle=gmem_out max_write_burst_length=1024 num_write_outstanding=32
    #pragma HLS INTERFACE s_axilite port=n
    #pragma HLS INTERFACE s_axilite port=return
    #pragma HLS DATAFLOW

    const int n_smers = n-(S-1);

    hls::stream<ap_uint<2>,1024> seq_stream;
    hls::stream<ap_uint<64>,1024> canon_stream;
    hls::stream<ap_uint<64>,1024> hash_stream;
    #pragma HLS STREAM variable=seq_stream depth=1024
    #pragma HLS STREAM variable=canon_stream depth=1024
    #pragma HLS STREAM variable=hash_stream depth=1024

    unpack_sequence_stream_512bit(sequence, seq_stream, n);
    thread_smer_canonical_8x(seq_stream, canon_stream, n);
    thread_hash_8x(canon_stream, hash_stream, n_smers);
    thread_store_512bit(hash_stream, tab_hash, n_smers);
}
}
