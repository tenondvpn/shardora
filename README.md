# Shardora
      A Dynamic Blockchain Sharding System with Resilient and Seamless Shard Reconfiguration

# Quick Start
## Requirements
      centos7
      g++8.3.0
      python3.10+
      cmake3.25.1+

## Run local shardora network
      git clone git@github.com:tenondvpn/shardora.git
      sh local.sh
	  
## Run customized network

### 1. Generate node configuration file

```
python3 gen_nodes_conf.py -n 10 -s 1 -m 10.101.20.29,10.101.20.30 -r 3 -m0 10.101.20.29

-n --node_num_per_shard node numbers of each shard network
-s --shard_num shard numbers
-m --machines ip addresses of target machines, devided by ','
-r --root_node_num node numbers of root network
-m0 --machine0 ip of the machine that will execute deployment script
```	

This will generate `nodes_conf_n10_s1_m2.yml` which means 10 nodes of 1 shard will be deployed on 2 machines.


### 2. Edit the configuration file if needed


```
nodes: # node configs
- http_port: 22001
  name: r1
  net: 2
  server: 10.0.0.201
  tcp_port: 12001
- http_port: 22002
  name: r2
  net: 2
  server: 10.0.0.1
  tcp_port: 12002
- http_port: 22003
  name: r3
  net: 2
  server: 10.0.0.2
  tcp_port: 12003
- http_port: 23001
  name: s3_1
  net: 3
  server: 10.0.0.3
  tcp_port: 13001
- http_port: 23002
  name: s3_2
  net: 3
  server: 10.0.0.4
  tcp_port: 13002
account_sks: # private keys of genesis accounts of different shard numbers
  3:
    - b5039128131f96f6164a33bc7fbc48c2f5cf425e8476b1c4d0f4d186fbd0d708
    - 02b91d27bb1761688be87898c44772e727f5e2f64aaf51a42931a0ca66a8a227
  4:
    - 580bb274af80b8d39b33f25ddbc911b14a1b3a2a6ec8ca376ffe9661cf809d36	
passwords: # machine passwords
  10.0.0.201: 'password'
  10.0.0.1: 'password'
node_sks: # private keys of genesis node accounts
  r1: 076886edab4bac346ec43c209c5438c23376742afa6c1837e145c24b088669a0
  r2: 5067f35bbc7c00f7837ade097141a93955c06e1bf9ec1db0860c58d665de7b31
  r3: 616a3dcadc11b5bce0445dee003356b372b1d69b1fe9156a318520933966e6cd
  s3_1: cefc2c33064ea7691aee3e5e4f7842935d26f3ad790d81cf015e79b78958e848
  s3_11: 66a1271be9d6d5d7b400dea6e3219d9c011caae518b6b9f920a00b2eb8e3aa3b
```

`gen_nodes_conf.py` will not generate the field `account_sks` `passwords` and `node_sks`, these fields will be randomly prepared if you won't add them manually. 

### 3. Generate deployment script file by the nodes config file.

```
python3 gen_genesis_script.py --config "./nodes_conf_n20_s1_m2.yml" --datadir='/mnt'

--config path of nodes config file
--datadir target folder for deployment
```

This will generate all the config files in `./zjnodes` and `deploy_genesis.sh` for deployment. 

### 4. Deploy the nodes.

Log on m0 and execute `deploy_genesis.sh` just generated.

```
sh deploy_genesis.sh Debug
```

## Transaction test
```
      cd ./cbuild_Debug && make txcli
      ./txcli
```

## Throughput(Single Shard，Single Pool) / TPS（300nodes/shard)


```
2024-06-25 16:47:50,051 [WARN] [block_acceptor.h][CalculateTps][193] pool: 63, tps: 6417.99
2024-06-25 16:47:52,110 [WARN] [block_acceptor.h][CalculateTps][193] pool: 63, tps: 5396.62
2024-06-25 16:47:54,765 [WARN] [block_acceptor.h][CalculateTps][193] pool: 63, tps: 4416.25
2024-06-25 16:47:56,808 [WARN] [block_acceptor.h][CalculateTps][193] pool: 63, tps: 7374.50
2024-06-25 16:47:59,095 [WARN] [block_acceptor.h][CalculateTps][193] pool: 63, tps: 6526.97
2024-06-25 16:48:01,208 [WARN] [block_acceptor.h][CalculateTps][193] pool: 63, tps: 7459.51
2024-06-25 16:48:03,729 [WARN] [block_acceptor.h][CalculateTps][193] pool: 63, tps: 3836.93
2024-06-25 16:48:05,829 [WARN] [block_acceptor.h][CalculateTps][193] pool: 63, tps: 9771.72
2024-06-25 16:48:07,839 [WARN] [block_acceptor.h][CalculateTps][193] pool: 63, tps: 4807.10
2024-06-25 16:48:09,851 [WARN] [block_acceptor.h][CalculateTps][193] pool: 63, tps: 6787.10
2024-06-25 16:48:11,904 [WARN] [block_acceptor.h][CalculateTps][193] pool: 63, tps: 6455.18
2024-06-25 16:48:13,941 [WARN] [block_acceptor.h][CalculateTps][193] pool: 63, tps: 6617.09
2024-06-25 16:48:16,044 [WARN] [block_acceptor.h][CalculateTps][193] pool: 63, tps: 6199.01
2024-06-25 16:48:18,079 [WARN] [block_acceptor.h][CalculateTps][193] pool: 63, tps: 6151.73
2024-06-25 16:48:20,440 [WARN] [block_acceptor.h][CalculateTps][193] pool: 63, tps: 4558.40
2024-06-25 16:48:22,612 [WARN] [block_acceptor.h][CalculateTps][193] pool: 63, tps: 6785.39
2024-06-25 16:48:24,756 [WARN] [block_acceptor.h][CalculateTps][193] pool: 63, tps: 6030.45
```
