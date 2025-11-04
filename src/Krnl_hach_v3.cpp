#include <ap_int.h>
#include <hls_stream.h>

#define SMER_SIZE 56
#define DATA_DEPTH 1024
#define SMERS_PER_CYCLE 8

<<<<<<< HEAD
inline ap_uint<2> nucl_encode(char nucl) {
    switch (nucl) {
        case 'A': return 0;
        case 'C': return 1;
        case 'G': return 2;
        case 'T': return 3;
        default : return 0;
=======
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
>>>>>>> 149120260dd00eb5aa85cea0a5afcf9f95bb590d
    }
}

inline ap_uint<64> min_v1(const ap_uint<64> a, const ap_uint<64> b) {
    return (a < b) ? a : b;
}

inline ap_uint<64> mask_right(int numbits) {
    return (numbits >= 64) ? ~0ULL : ((1ULL << numbits) - 1ULL);
}

inline ap_uint<64> hash_u64(ap_uint<64> key, ap_uint<64> mask) {
    key = (~key + (key << 21)) & mask;
    key = key ^ (key >> 24);
    key = ((key + (key << 3)) + (key << 8)) & mask;
    key = key ^ (key >> 14);
    key = ((key + (key << 2)) + (key << 4)) & mask;
    key = key ^ (key >> 28);
    key = (key + (key << 31)) & mask;
    return key;
}

void thread_reader_v2(
    const ap_uint<512>* packed_sequence,
=======

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
    hls::stream< ap_uint<24> >& stream_o
) {
    const int n_words = (int)((n_bases + 63) / 64);
    for (int i = 0; i < n_words; ++i) {
    #pragma HLS PIPELINE II=1
        const ap_uint<512> word_64b = packed_sequence[i];
        ap_uint<24> word_3b = 0;
        bool stop = false;
        for (int b = 0; b < 64; ++b) {
        #pragma HLS UNROLL factor=8
            const int global_idx = i * 64 + b;
            ap_uint<1> valid = (global_idx < (int)n_bases) ? 1 : 0;
            ap_uint<8> c = valid ? word_64b.range(8*(b+1)-1, 8*b) : 0;
            const ap_uint<2> enc = (c >> 1) & 0x3;
            const int slot = b & 0x7;
            word_3b.range(3*slot+1, 3*slot) = enc;
            word_3b[3*slot+2] = valid;
            if (slot == 7) {
                stream_o.write(word_3b);
                word_3b = 0;
                bool any_invalid = false;
                for (int k = 0; k < 8; ++k) any_invalid |= (word_3b.range(3*k+2,3*k+2) == 0);
                if (!valid && global_idx < (int)n_bases) { /* nothing */ }
                if (!valid && (global_idx % 8) == 7) { stop = true; break; }
            }
        }
        if (stop) break;
    }
    if ((n_bases & 7) == 0) stream_o.write((ap_uint<24>)0);
}

void thread_reader_pack(
    hls::stream<ap_uint<24>>& stream_i,
    hls::stream<ap_uint<SMERS_PER_CYCLE*3>>& stream_o
) {
    ap_uint<SMERS_PER_CYCLE*3> pack = 0;
    int count = 0;
    while (true) {
    #pragma HLS PIPELINE II=1
        ap_uint<24> val = stream_i.read();
        if (val == 0) { if (count != 0) stream_o.write(pack); stream_o.write(0); break; }
        for (int i = 0; i < 8; i++) {
            pack.range((count+1)*3-1, count*3) = val.range(3*i+1, 3*i);
            count++;
            if (count == SMERS_PER_CYCLE) {
                stream_o.write(pack);
                count = 0;
                pack = 0;
            }
        }
    }
}

void thread_smer_v2(
    hls::stream<ap_uint<SMERS_PER_CYCLE*3>>& stream_i,
    ap_uint<64> n_bases,
    hls::stream<ap_uint<SMER_SIZE>>& stream_o
) {
    constexpr int smer = SMER_SIZE / 2;
    const ap_uint<64> HASH_MASK = mask_right(SMER_SIZE);
    ap_uint<SMER_SIZE> current_smer = 0;
    ap_uint<SMER_SIZE> cur_inv_smer = 0;
    while (true) {
    #pragma HLS PIPELINE II=1
        ap_uint<SMERS_PER_CYCLE*3> word = stream_i.read();
        if (word == 0) { stream_o.write(0); break; }
        for (int i = 0; i < SMERS_PER_CYCLE; i++) {
            const ap_uint<2> c_nucl = word.range(3*i+1, 3*i);
            current_smer <<= 2;
            current_smer(1,0) = c_nucl;
            cur_inv_smer = (cur_inv_smer >> 2) | ((ap_uint<SMER_SIZE>)((0x2 ^ c_nucl)) << (SMER_SIZE-2));
            const ap_uint<SMER_SIZE> vmin = min_v1(current_smer, cur_inv_smer);
            const ap_uint<SMER_SIZE> vhash = (ap_uint<SMER_SIZE>)hash_u64((ap_uint<64>)vmin, HASH_MASK);
            stream_o.write(vhash);
        }
    }
}

void thread_smer_pack(
    hls::stream<ap_uint<SMER_SIZE>>& stream_i,
    hls::stream<ap_uint<SMER_SIZE*SMERS_PER_CYCLE>>& stream_o
) {
    ap_uint<SMER_SIZE*SMERS_PER_CYCLE> pack = 0;
    int count = 0;
    while (true) {
    #pragma HLS PIPELINE II=1
        ap_uint<SMER_SIZE> val = stream_i.read();
        if (val == 0) { if (count != 0) stream_o.write(pack); stream_o.write(0); break; }
        pack.range((count+1)*SMER_SIZE-1, count*SMER_SIZE) = val;
        count++;
        if (count == SMERS_PER_CYCLE) { stream_o.write(pack); count = 0; pack = 0; }
    }
}

void thread_dedup_v2(
    hls::stream<ap_uint<SMER_SIZE*SMERS_PER_CYCLE>>& stream_i,
    hls::stream<ap_uint<SMER_SIZE>>& stream_o
) {
    ap_uint<SMER_SIZE> buffer[8];
    for (int i = 0; i < 8; i++) buffer[i] = stream_i.read().range(SMER_SIZE-1, 0);
    ap_uint<SMER_SIZE> lastElement = (ap_uint<SMER_SIZE>)(-1);
    while (true) {
    #pragma HLS PIPELINE II=1
        ap_uint<SMER_SIZE*SMERS_PER_CYCLE> val = stream_i.read();
        if (val == 0) { stream_o.write(0); break; }
        for (int i = 0; i < SMERS_PER_CYCLE; i++) {
            ap_uint<SMER_SIZE> vhash = val.range((i+1)*SMER_SIZE-1, i*SMER_SIZE);
            ap_uint<SMER_SIZE> minz = vhash;
            for (int p = 0; p < 8; p++) minz = (buffer[p] < minz) ? buffer[p] : minz;
            for (int p = 0; p < 7; p++) buffer[p] = buffer[p+1];
            buffer[7] = vhash;
            if (lastElement != minz) { stream_o.write(minz); lastElement = minz; }
        }
    }
}

void thread_store_v2(
    hls::stream<ap_uint<SMER_SIZE>>& stream_i,
    ap_uint<512>* tab_hash,
    ap_uint<64>* nElements
) {
    ap_uint<512> pack = 0;
    int count = 0;
    int idx = 0;
    int total = 0;
    while (true) {
    #pragma HLS PIPELINE II=1
        ap_uint<SMER_SIZE> val = stream_i.read();
        if (val == 0) break;
        pack.range((count+1)*SMER_SIZE-1, count*SMER_SIZE) = val;
        count++;
        total++;
        if (count == 8) {
            tab_hash[idx++] = pack;
            pack = 0;
            count = 0;
        }
    }
    if (count != 0) {
        tab_hash[idx++] = pack;
    }
    *nElements = total;
}

void minimizer(
    const ap_uint<512>* packed_sequence,
    ap_uint<64> n,
    ap_uint<512>* tab_hash,
    ap_uint<64>* nMinizrs
) {
    #pragma HLS INTERFACE mode=m_axi port=packed_sequence
    #pragma HLS INTERFACE mode=m_axi port=tab_hash
    #pragma HLS INTERFACE mode=s_axilite port=nMinizrs
    #pragma HLS INTERFACE mode=s_axilite port=n
    #pragma HLS INTERFACE mode=s_axilite port=return bundle=control
    #pragma HLS DATAFLOW

    hls::stream< ap_uint<24>, DATA_DEPTH > fifo_1;
    hls::stream< ap_uint<SMERS_PER_CYCLE*3>, DATA_DEPTH > fifo_2;
    hls::stream< ap_uint<SMER_SIZE>, DATA_DEPTH > fifo_3;
    hls::stream< ap_uint<SMER_SIZE*SMERS_PER_CYCLE>, DATA_DEPTH > fifo_4;
    hls::stream< ap_uint<SMER_SIZE>, DATA_DEPTH > fifo_5;
    ap_uint<64> n_smers = (n_bases >= (ap_uint<64>)(S - 1))
    ? (ap_uint<64>)(n_bases - (ap_uint<64>)(S - 1))
    : (ap_uint<64>)0;


    thread_reader_v2(packed_sequence, n, fifo_1);
    thread_reader_pack(fifo_1, fifo_2);
    thread_smer_v2(fifo_2, n, fifo_3);
    thread_smer_pack(fifo_3, fifo_4);
    thread_dedup_v2(fifo_4, fifo_5);
    thread_store_v2(fifo_5, tab_hash, nMinizrs);
}
