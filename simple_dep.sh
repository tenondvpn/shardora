TARGET=Debug
sh build.sh a $TARGET
sudo rm -rf /root/zjnodes
sudo cp -rf ./zjnodes_local /root/zjnodes
sudo cp -rf ./deploy /root
sudo cp ./fetch.sh /root
rm -rf /root/zjnodes/*/zjchain /root/zjnodes/*/core* /root/zjnodes/*/log/* /root/zjnodes/*/*db*

cp -rf ./zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/zjchain
cp -rf ./zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/zjchain/conf
mkdir -p /root/zjnodes/zjchain/log


sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/zjchain
sudo cp -f ./conf/genesis.yml /root/zjnodes/zjchain/genesis.yml

sudo cp -rf ./cbuild_$TARGET/zjchain /root/zjnodes/zjchain


cd /root/zjnodes/zjchain && ./zjchain -U
cd /root/zjnodes/zjchain && ./zjchain -S 3

rm -rf /root/zjnodes/r*
rm -rf /root/zjnodes/s*
rm -rf /root/zjnodes/new*
rm -rf /root/zjnodes/node
rm -rf /root/zjnodes/param
node_count=$1
for ((i=0; i<$1;i++)); do
    
    echo $i
done
