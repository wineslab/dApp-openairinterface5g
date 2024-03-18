#!/bin/bash

OAI_CONFIG_DIR="../../../targets/PROJECTS/GENERIC-NR-5GC/CONF/"
OAI_CONFIG_FILE="gnb.band78.sa.fr1.106PRB.usrpx300.conf"

cd ~/openairinterface5g
source oaienv
cd ./cmake_targets/ran_build/build

./nr-softmodem -O ${OAI_CONFIG_DIR}${OAI_CONFIG_FILE} --gNBs.[0].min_rxtxtime 6 --sa --usrp-tx-thread-config 1 