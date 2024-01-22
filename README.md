# zjchain

## For Testing

Use the following command to filter and sort specific log entries from `zjchain.log`:

```bash
grep "new from add new to sharding" ./log/zjchain.log | grep "pool: 0" | awk -F' ' '{ printf("%03d", $19); print " " $15}' | sort > 0
```

## Genesis Nodes Deployment

### Deploy on Multiple Servers

#### 1. Create Genesis Data

Execute the script below to generate genesis data in the db folders:

```bash
sh ./genesis.sh Release
```

The shard distribution is specified in the `genesis.sh` file. Edit this file if you need to modify the node-to-shard mapping.

#### 2. Configure `zjchain.conf` for Node Deployment

Here is a sample configuration:

```ini
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

Key fields to note:

- `bootstrap`: Nodes to connect to for network establishment.
- `local_ip`: IP address of the node.
- `local_port`: Port number of the node.
- `prikey`: Private key of the node account.
- `http_port`: If non-zero, a HTTP server starts.

Replace `127.0.0.1` with the actual IP in `zjchain.conf`. Use the provided tool for this:

```bash
sh -x fetch.sh 127.0.0.1 10.101.20.35 r1 r2 r3 s1 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11
```

Arguments:

- `arg1`: Source IP to be replaced.
- `arg2`: New IP address.
- `arg3..`: Node config files to update.

Manual editing is also an option.

#### 3. Copy Genesis Data to Other Servers and Edit `zjchain.conf`

Use `fetch.sh` to transfer genesis data and update IP addresses:

```bash
sh -x fetch.sh 10.101.20.35 10.101.20.36 r1 r2 r3 s1 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11
```

This script:

1. Copies genesis data from the source server.
2. Updates IPs in `zjchain.conf`.

Alternatively, manually upload genesis data and edit config files.

#### 4. Start or Stop Nodes

Execute the start commands as follows:

```bash
# server1
cd /root/deploy && sh start.sh r1
sleep 3

# server2
cd /root/deploy && sh start.sh r2 r3
```

To stop nodes:

```shell
cd /root/deploy && sh stop.sh r1 r2 r3
```

If starting the first node, wait at least 3 seconds before launching others.

## Start A New Node

Follow these steps to start a new node and join the chain:

#### 1. Prepare `zjchain.conf` for the Node

Example configuration:

```ini
[db]
path=./db
[log]
path=log/zjchain.log
[zjchain]
bootstrap=031d29587f946b7e57533725856e3b2fc840ac8395311fea149642334629cd5757:10.101.20.35:11001,03a6f3b7a4a3b546d515bfa643fc4153b86464543a13ab5dd05ce6f095efb98d87:10.101.20.35:12001,031e886027cdf3e7c58b9e47e8aac3fe67c393a155d79a96a0572dd2163b4186f0:10.101.20.35:13001
country=NL
first_node=0
http_port=8792
local_ip=127.0.0.1
local_port=32001
prikey=0cbc2bc8f999aa16392d3f8c1c271c522d3a92a4b7074520b37d37a4b38db999
show_cmd=0
for_ck=false
local_member_idx=2
```

#### 2. Send a Transaction to the Node Account

Send a transaction to allocate a shard for the node. A Node.js script is provided for this:

```bash
cd ./src/contract/tests/contracts

node test_transaction.js 1 {node private key}

shell > 
{
  balance: '0',
  shardingId: 3, // the node is created in shard3 by root 
  poolIndex: 146,
  addr: 'NIDjSIlmODPBAs7mULVnd+EK0/s=',
  type: 'kNormal',
  latestHeight: '1'
}
```

#### 3. Run the Node

Start the node with:

```bash
sh /root/deploy/start.sh
```
