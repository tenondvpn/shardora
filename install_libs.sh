#sudo apt-get update
#sudo apt-get install -y autoconf automake libtool glibc-headers gcc-c++
yum install -y autoconf automake libtool glibc-headers gcc-c++
git submodule init
git submodule update

work_path=`pwd`

cd ./libs/protobuf && git submodule init && git submodule update && sh ./autogen.sh && ./configure --prefix=`pwd`/libs && make -j$(nproc) && make install

cd $work_path
cd ./libs/evhtp