#!/bin/bash

OAI_CONFIG_DIR="../../../targets/PROJECTS/GENERIC-NR-5GC/CONF/"

# x310
OAI_CONFIG_FILE="gnb.band78.sa.fr1.106PRB.usrpx300.conf"

# x410
# OAI_CONFIG_FILE="gnb.band78.sa.fr1.106PRB.usrpx400.conf"

# b210 not tested in a while, may not work
# OAI_CONFIG_FILE="gnb.sa.band78.fr1.106PRB.usrpb210.conf"

rm -rf /tmp/dapps 

source oaienv
cd ./cmake_targets/ran_build/build

umask 0000

rm statsMAC.log
rm statsPRB.log

# x310 and x410
# gdb --args
taskset -ca 0-45 ./nr-softmodem -O ${OAI_CONFIG_DIR}${OAI_CONFIG_FILE} --gNBs.[0].min_rxtxtime 6 --sa --usrp-tx-thread-config 1  -E --T_stdout 2 --gNBs.[0].do_SRS 0

# b210
# ./nr-softmodem -O ${OAI_CONFIG_DIR}${OAI_CONFIG_FILE} --gNBs.[0].min_rxtxtime 6 --sa --usrp-tx-thread-config 1 -E  --T_stdout  2

cd -