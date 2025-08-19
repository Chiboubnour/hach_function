# HBM Hashing XRT (XRT Native API's)

This is a simple example of **hash computation from DNA sequences** using an HLS kernel with **HBM (High Bandwidth Memory)** on FPGA.  
The goal is to demonstrate how sequences can be packed, unpacked, transformed into **s-mers** (sub-sequences), and hashed efficiently on hardware using XRT Native APIs.

---

## RESULTS

| n (bases) | Temps d'exécution (s) | Consommation (GB) |
|-----------|-----------------------|-------------------|
| 64        | 1.19e-04              | 3.6e-07           |
| 128       | 3.89e-05              | 9.36e-07          |
| 256       | 3.23e-05              | 2.09e-06          |
| 512       | 3.26e-05              | 4.39e-06          |
| 1024      | 3.47e-05              | 9.00e-06          |

