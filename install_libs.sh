#sudo apt-get update
#sudo apt-get install -y autoconf automake libtool
yum install -y autoconf automake libtool
git submodule init
git submodule update
cd ./libs/protobuf and ./autogen.sh