#!/bin/bash

OAI_CONFIG_DIR="../../../targets/PROJECTS/GENERIC-NR-5GC/CONF/"

# x310
# OAI_CONFIG_FILE="gnb.band78.sa.fr1.106PRB.usrpx300.conf"

# b210
OAI_CONFIG_FILE="gnb.sa.band78.fr1.106PRB.usrpb210.conf"

source oaienv
cd ./cmake_targets/ran_build/build

# x310
# ./nr-softmodem -O ${OAI_CONFIG_DIR}${OAI_CONFIG_FILE} --gNBs.[0].min_rxtxtime 6 --sa --usrp-tx-thread-config 1  --usrp-args "addr=192.168.10.58" --T_stdout  2

# b210
./nr-softmodem -O ${OAI_CONFIG_DIR}${OAI_CONFIG_FILE} --gNBs.[0].min_rxtxtime 6 --sa --usrp-tx-thread-config 1 -E  --T_stdout  2

# rfsim
# ./nr-softmodem -O  ${OAI_CONFIG_DIR}${OAI_CONFIG_FILE}  --gNBs.[0].min_rxtxtime 6  --sa --rfsim --T_stdout 2