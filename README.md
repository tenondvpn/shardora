# Shardora
      A Dynamic Blockchain Sharding System with Resilient and Seamless Shard Reconfiguration
      paper: https://ccs2025a.hotcrp.com/doc/ccs2025a-paper756.pdf?cap=hcav756eNAubdJqApSsXnJDucFgJMXB
      

# Quick Start
## Requirements
      centos7
      g++8.3.0
      python3.10+
      cmake3.25.1+

## Run local shardora network
      git clone git@github.com:tenondvpn/shardora.git
      sh simple_dep.sh $node_count  
      # node_count like 4, mean create 4 nodes shardora network on local machine
	  
## Run customized network
      sh simple_remote.sh $each_machine_node_count $ip_list  
      # each_machine_node_count like 4, mean each machine create 4 nodes. 
      # ip_list like 192.168.0.1,192.168.0.2, mean 2 machine create 2 * 4 nodes shardora network
      # machine user must root
      # machine password must Xf4aGbTaf!(for test), you can change it by edit simple_remote.sh

## Transaction test
```
      cd ./cbuild_Release && make txcli
      ./txcli
```
