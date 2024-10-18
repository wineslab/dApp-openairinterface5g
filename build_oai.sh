#!/bin/bash

source oaienv

cd ./cmake_targets/

# export UHD_VERSION=4.4.0.0

# ./build_oai -I

./build_oai -w USRP --ninja --gNB --build-e3 --use-e3-uds --cmake-opt "-DCMAKE_C_FLAGS=-march=native -DCMAKE_CXX_FLAGS=-march=native"
# --nrUE
cd ;
