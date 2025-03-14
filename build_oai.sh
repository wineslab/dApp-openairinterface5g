#!/bin/bash

source oaienv

cd ./cmake_targets/

# export UHD_VERSION=4.4.0.0

# ./build_oai -I

./build_oai -w USRP --ninja --gNB --build-e2 --build-e3 --cmake-opt "-DCMAKE_C_FLAGS=-march=native -DCMAKE_CXX_FLAGS=-march=native"

# --sanitize
# --nrUE
# -g for gdb
# e2_agent = {
#   near_ric_ip_addr = "127.0.0.1";
#   sm_dir = "/usr/local/lib/flexric/"
# }

cd ;
