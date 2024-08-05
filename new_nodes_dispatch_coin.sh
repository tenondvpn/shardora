#!/bin/bash

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/
cd ./cbuild_Debug
make txcli
./txcli 5 38d2a932186ba9f9b2aa74c4c1ee8090a51b49a0 7e4c90aafcb19f49a4cdbebfe3a22b81a6024609 72ca38a1961fc752cdcb4d8f5e093429b5b209dc



