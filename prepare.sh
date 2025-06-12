
FIRST_IP=$1
PASSWORD=$2
TARGET=$3
if [ "$TARGET" == "" ]; then
    TARGET=Release
fi

if [ "$PASSWORD" == "" ]; then
    PASSWORD="Xf4aGbTaf!"
fi

killall -9 zjchain
killall -9 txcli

sh build.sh a $TARGET

rm -rf shardora && mkdir shardora
mkdir shardora
cp -rf ./*.sh ./shardora/
mkdir -p ./shardora/cbuild_$TARGET
cp -rf cbuild_$TARGET/zjchain ./shardora/cbuild_$TARGET
cp -rf zjnodes_* ./shardora/
cp -rf ./init_accounts* ./shardora/
cp -rf ./root_nodes ./shardora/
tar -zcvf shardora.tar.gz ./shardora
        
sshpass -p $PASSWORD scp -o StrictHostKeyChecking=no ./shardora.tar.gz root@$FIRST_IP:/root 
sshpass -p $PASSWORD ssh -o ConnectTimeout=3 -o "StrictHostKeyChecking no" -o ServerAliveInterval=5 root@$FIRST_IP "rm -rf /root/shardora && cd /root && tar -zxvf ./shardora.tar.gz > /dev/null 2>&1 && cd /root/shardora && sh prepare_remote.sh 10 192.168.0.21,192.168.0.198 3 > /dev/null 2>&1" 
