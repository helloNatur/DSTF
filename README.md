# DSTF and DSTF+

## Overview

**DSTF** is a hierarchical **Range Searchable Symmetric Encryption (RSSE)** framework designed for secure and efficient **spatiotemporal queries** over encrypted databases.  
It supports range filtering, and lightweight join queries while maintaining sublinear search complexity and a low false positive rate.

**DSTF+** is an optimized variant of DSTF that integrates a lightweight **TDAG-based structure** into the SegmentTree layer, following the *injection-and-rewiring* principle.  
This extension significantly reduces the number of returned tokens while preserving an acceptable false positive rate.


## Build & Installation

### Requirements
- C++17 or later  
- CMake ≥ 3.16  
- OpenSSL (for AES/SHA-256 encryption)  
- GoogleTest (for unit testing)

### Build

```bash
git clone https://github.com/helloNatur/DSTF.git
cd libbf && ./configure --prefix=PREFIX && make && make install
cd TDAG
mkdir build && cd build
cmake .. && make -j

./b_st_bf_test
