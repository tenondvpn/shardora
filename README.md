# zjchain

# for test

grep "new from add new to sharding" ./log/zjchain.log | grep "pool: 0" | awk -F' ' '{ printf("%03d", $19); print " " $15}' | sort > 0

# Genesis nodes deployment

## 1. write zjchain.conf for node

```
[db]
path=./db
[log]
path=log/zjchain.log
[zjchain]
bootstrap=031d29587f946b7e57533725856e3b2fc840ac8395311fea149642334629cd5757:10.101.20.35:11001,03a6f3b7a4a3b546d515bfa643fc4153b86464543a13ab5dd05ce6f095efb98d87:10.101.20.35:12001,031e886027cdf3e7c58b9e47e8aac3fe67c393a155d79a96a0572dd2163b4186f0:10.101.20.35:13001
ck_ip=10.101.20.35
ck_passworkd=""
ck_user=""
country=NL
data_service_node_for_net_id=-1
first_node=0
http_port=8781
id=c513ba24d79f6adc5060caebc9267663457f74c2
local_ip=10.101.20.35
local_port=21001
net_id=4
prikey=e154d5e5fc28b7f715c01ca64058be7466141dc6744c89cbcc5284e228c01269
show_cmd=0
statistic_ck=true
tcp_spec=0.0.0.0:21002
local_member_idx=0
[tx_block]
network_id=4
```

The key fields are:

- bootstrap: the node will connect bootstrap node to establish network after startup.
- local_ip: the ip of node
- local_port: the port of node
- prikey: private key of node account
- http_port: start a http server if http_port is not 0

## 2. create genesis data

```
sh ./genesis.sh
```

This will create genesis data into db folders. The distribution of shards is in the genesis.sh file, edit it if you want to change the mapping of different nodes to shards. 

## 3. copy genesis data to other servers

```
sh -x fetch.sh 10.101.20.36 r1 r2 r3 s1 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11
```

The command will scp genesis data from the initial server, while changing the ips in the config files automatically. For example, if your genesis data is build in 10.101.20.35, the command will copy the data from it and replace "10.101.20.35" to your local ip for all config files.
Of course, you can also upload the genesis data and edit the ips in the config files manually.

## 4. start or stop nodes

After the preparation of genesis data for all nodes on all servers, the start command can be executed as follows.

```
cd /root/deploy && sh start.sh r1

sleep 3

cd /root/deploy && sh start.sh r2 r3
```

```shell
cd /root/deploy && sh stop.sh r1 r2 r3
```

if the node you are starting is the first node, please sleep for at least 3 seconds before starting up the others
