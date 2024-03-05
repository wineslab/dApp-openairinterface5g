#!/bin/bash

source oaienv

cd ./cmake_targets/

# export UHD_VERSION=4.4.0.0

./build_oai -w USRP --ninja --gNB --cmake-opt "-DCMAKE_C_FLAGS=-march=native -DCMAKE_CXX_FLAGS=-march=native" 


cd -