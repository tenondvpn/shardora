git submodule init
git submodule update

export nproc=8
#centos
dnf install -y gnutls-devel
dnf install -y perl
dnf install -y procps-ng-devel
dnf install -y texinfo
dnf install -y xz-devel
#ubuntu
apt update
apt install -y libprocps-dev texinfo libgnutls28-dev liblzma-dev

SRC_PATH=`pwd`
cd $SRC_PATH
cd third_party/json &&  git submodule update --init && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DJSON_BuildTests=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && make install

exit 0
cd $SRC_PATH
cd third_party/cpppbc && git checkout . && sed -i 's/CXXFLAGS=/CXXFLAGS=-I\.\.\/include -L\.\.\/lib /g' ./Makefile && make -j8 libPBC.a
mkdir -p $SRC_PATH/third_party/include/cpppbc && cp -rnf ./*.h $SRC_PATH/third_party/include/cpppbc
cp -rnf ./lib*.a $SRC_PATH/third_party/lib
cd $SRC_PATH
cd third_party/clickhouse &&  git submodule update --init && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && make install

cp -rnf ../contrib/absl/absl $SRC_PATH/third_party/include/
cp -rnf ../contrib/lz4/lz4 $SRC_PATH/third_party/include/
cp -rnf ../contrib/zstd/zstd $SRC_PATH/third_party/include/

cd $SRC_PATH
cd third_party/evmone &&  git submodule update --init && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && make install

cd $SRC_PATH
cd third_party/evmone/evmc &&  git submodule update --init && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && make install
cd third_party/log4cpp && git checkout . &&  sed -i '14i\#include <ctime>' ./include/log4cpp/DailyRollingFileAppender.hh  && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && make install

cd $SRC_PATH
cd third_party/maxmind/ && git submodule init && git submodule update && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc}
cp -rnf libmaxminddb.a $SRC_PATH/third_party/lib/
mkdir -p $SRC_PATH/third_party/include/maxmind && cd .. && cp -rnf ./include/* $SRC_PATH/third_party/include/maxmind && cp -rnf build_release/generated/maxminddb_config.h $SRC_PATH/third_party/include/maxmind/
mkdir -p $SRC_PATH/third_party/include/maxmind/include && cp -rnf ./include/* $SRC_PATH/third_party/include/maxmind/include && cp -rnf build_release/generated/maxminddb_config.h $SRC_PATH/third_party/include/maxmind/include

cd $SRC_PATH
cd third_party/geolite2pp && git checkout . && sed -i 's/const auto iter/const auto\& iter/g' ./src-main/main.cpp &&  sed -i '11i\include_directories(SYSTEM '$SRC_PATH'/third_party/include/maxmind/)' CMakeLists.txt && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/  -DCMAKE_PREFIX_PATH=$SRC_PATH/third_party/ -DCMAKE_INCLUDE_PATH=$SRC_PATH/third_party/include/maxmind/ && cd build_release && make -j${nproc} && make install

cd $SRC_PATH
cd third_party/protobuf/ && git checkout 48cb18e && ./autogen.sh && ./configure --prefix=$SRC_PATH/third_party/ && make -j${nproc} && make install

cd $SRC_PATH
cd third_party/xxHash/ && make -j${nproc} && mkdir -p $SRC_PATH/third_party/include/xxHash && cp -rnf ./*.h $SRC_PATH/third_party/include/xxHash && cp -rnf ./lib*.so ./lib*.a $SRC_PATH/third_party/lib 

cd $SRC_PATH
cd third_party/ethash && git checkout 83bd5ad && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && mkdir -p $SRC_PATH/third_party/include/ethash && cp -rnf ../include/ethash/* $SRC_PATH/third_party/include/ethash && cp -rnf ./lib/keccak/libkeccak.a ./lib/ethash/libethash.a ./lib/global_context/libethash-global-context.a $SRC_PATH/third_party/lib

cd $SRC_PATH
cd third_party/gperftools/ && git checkout d9a5d38 && ./autogen.sh && ./configure --prefix=$SRC_PATH/third_party/ && make -j${nproc} && make install

cd $SRC_PATH
cd third_party/gmssl && git checkout d655c06 && sed -i '19i\#include <gmssl/sm2.h>' ./include/gmssl/sm2_recover.h && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DENABLE_SM2_EXTS=on -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && make install

cd $SRC_PATH
cd third_party/openssl/ && git checkout 7b371d8 && ./Configure --prefix=$SRC_PATH/third_party/ && make -j${nproc} && make install

cd $SRC_PATH
cd third_party/readerwriterqueue && git checkout 8b21766 && mkdir -p $SRC_PATH/third_party/include/readerwriterqueue && cp -rnf ./*.h $SRC_PATH/third_party/include/readerwriterqueue

cd $SRC_PATH
cd third_party/secp256k1 && git checkout a660a49 && cmake -S . -B build_release -DSECP256K1_ENABLE_MODULE_RECOVERY=on -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && make install

cd $SRC_PATH
mkdir -p $SRC_PATH/third_party/include/boost
cd third_party/boost/multiprecision && git checkout c48ae18 && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && make install 

cd $SRC_PATH
cd third_party/oqs && git checkout 94b421e && cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j8 && make install

cd $SRC_PATH
cd third_party/leveldb && git checkout 99b3c03 && git submodule init && git submodule update && cmake -S . -B build_release -DLEVELDB_BUILD_TESTS=OFF -DLEVELDB_BUILD_BENCHMARKS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j8 && make install

cd $SRC_PATH
cd third_party/libsodium && ./autogen.sh -s && ./configure --prefix=$SRC_PATH/third_party/ && make -j${nproc} && make install

cd $SRC_PATH
export SODIUM_INCLUDE_DIR=$SRC_PATH/third_party/include
export SODIUM_LIBRARY=$SRC_PATH/third_party/lib
cd third_party/libff && rm -rf ./depends/* && git checkout . && git submodule init && git submodule update && sed -i '3594i\#ifdef MIE_USE_X64ASM' ./depends/ate-pairing/src/zm2.cpp && sed -i '3690i\#endif' ./depends/ate-pairing/src/zm2.cpp &&  sed -i 's/private:/\/\/private:/g' ./libff/algebra/fields/prime_base/fp.hpp  && cmake -S . -B build_release -DWITH_PROCPS=OFF -DUSE_ASM=False -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j8 && make install

cd $SRC_PATH
cd third_party/libbls && cd ./deps && PARALLEL_COUNT=1 sh build.sh && cp deps_inst/x86_or_x64/lib64/lib* deps_inst/x86_or_x64/lib/ && cd .. && cmake -S . -B build_release  -DUSE_ASM=False  -DWITH_PROCPS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DLIBBLS_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j8 && make install
mkdir -p $SRC_PATH/third_party/include/libbls && cp -rnf ../third_party ../tools ../dkg ../bls $SRC_PATH/third_party/include/libbls 
cp -rnf ../deps/deps_inst/x86_or_x64/include/boost/* $SRC_PATH/third_party/include/boost/

cd $SRC_PATH
cd third_party/pbc && make -f simple.make
mkdir -p $SRC_PATH/third_party/include/pbc && cp -rnf ./include/* $SRC_PATH/third_party/include/pbc && cp -rnf ./lib*.a  $SRC_PATH/third_party/lib

cd $SRC_PATH
cd third_party/fmt && cmake -S . -B build_release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$SRC_PATH/third_party/ && cd build_release && make -j${nproc} && make install

cd $SRC_PATH
cd third_party/httplib && cp ./httplib.h $SRC_PATH/third_party/include/ 



