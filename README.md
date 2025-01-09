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
    - a private key
    - a private key
  4:
    - a private key
passwords: # machine passwords
  10.0.0.1: 'password'
node_sks: # private keys of genesis node accounts
  s3_1: a private key
  s3_2: a private key
```

`gen_nodes_conf.py` will not generate the field `account_sks` `passwords` and `node_sks`, these fields will be randomly prepared if you won't add them manually. 

### 3. Generate deployment script file by the nodes config file.

```
python3 gen_genesis_script.py --config "./nodes_conf_n20_s1_m2.yml" --datadir='/root'

--config path of nodes config file
--datadir target folder for deployment
```

This will generate all the config files in `./zjnodes` and `deploy_genesis.sh` for deployment. 

### 4. Deploy the nodes.

Log on m0 and execute `deploy_genesis.sh` just generated.

```
sh deploy_genesis.sh Debug
```

## Run docker

### 1. Build docker image

```
sh build_container.sh Debug/Release
```

A image will be generated: shardora-image-debug or shardora-image-release

### 2. Start a container with config file

```
sh start_container.sh ${image_name} ${config_file} ${container_name}
```

- image_name: thie image name that has been build, shardora-image-debug or shardora-image-release 
- config_file: the configuration file need to start the node process
- container_name: a tag to identify container name, eg. r1 r2 s3_1 s4_1, balabala

eg.

```
sh start_container.sh shardora-image-debug ./zjnodes/s3_1/conf/zjchain.conf s3_1  
```


## Transaction test
```
      cd ./cbuild_Debug && make txcli
      ./txcli
```

