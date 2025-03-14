#!/bin/bash

source oaienv
cd ./cmake_targets/ran_build/build

sudo ./nr-uesoftmodem --rfsim --rfsimulator.serveraddr 127.0.0.1

cd -