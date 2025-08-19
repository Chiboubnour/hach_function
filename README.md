# Résultats d'exécution

## 1. Exécution sur matériel réel (FPGA U280)

```bash
make run TARGET=hw PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1
===> Premiers 10 hash générés:
943af65c920be3
a16b3488e8b1bd
ce0e5cea32e20f
83118c2d6033ec
943af65c920be3
a16b3488e8b1bd
ce0e5cea32e20f
83118c2d6033ec
943af65c920be3
a16b3488e8b1bd
Temps d'exécution du kernel : 0.000110065 s
Débit : 0.00625085 GB/s
Test terminé avec succès.

make run TARGET=hw_emu PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1
===> Premiers 10 hash générés:
943af65c920be3
a16b3488e8b1bd
ce0e5cea32e20f
83118c2d6033ec
943af65c920be3
a16b3488e8b1bd
ce0e5cea32e20f
83118c2d6033ec
943af65c920be3
a16b3488e8b1bd
Temps d'exécution du kernel : 1.00021 s
Débit : 6.87857e-07 GB/s
Test terminé avec succès.




