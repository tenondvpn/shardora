# configure
export CC=/usr/local/gcc-8.3.0/bin/gcc
export CXX=/usr/local/gcc-8.3.0/bin/g++
TARGET=Debug
mkdir -p cbuild_$TARGET
cd cbuild_$TARGET
# CMAKE_BUILD_TYPE:
#   None:
#   Debug:              -g
#   Release:            -O3 -DNDEBUG
#   RelWithDebInfo:     -O2 -g -DNDEBUG
#   MinSizeRel:         -Os -DNDEBUG
cmake .. -DCMAKE_BUILD_TYPE=$TARGET -DOPENSSL_ROOT_DIR=./third_party/depends/include/ -DCMAKE_INSTALL_PREFIX=~/zjchain
if [[ $1 == "" ]];
then
    make -j3
    ./tcp_test/tcp_test
    ./http_test/http_test
    ./common_test/common_test
    ./broadcast_test/broadcast_test
    ./security_test/security_test
    ./websocket_test/websocket_test
    ./transport_test/transport_test
    exit 0
fi

make -j3 zjchain
echo $1
if [[ $1 == "test" ]];
then
    make -j3 common_test
    make -j3 tcp_test
    make -j3 http_test
    make -j3 broadcast_test
    make -j3 security_test
    make -j3 websocket_test
    make -j3 transport_test
    ./tcp_test/tcp_test
    ./http_test/http_test
    ./common_test/common_test
    ./broadcast_test/broadcast_test
    ./security_test/security_test
    ./websocket_test/websocket_test
    ./transport_test/transport_test
fi

if [[ $1 == "tcp" ]];
then
    make -j3 tnets
    make -j3 tnetc
fi

if [[ $1 == "http" ]];
then
    make -j3 https
    make -j3 httpc
fi

if [[ $1 == "ws" ]];
then
    make -j3 wss
    make -j3 wsc
fi

