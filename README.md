
# paper

[Akaverse Boosting Sharded Blockchain via Multi-Leader Parallel Pipelines(1).pdf](https://github.com/user-attachments/files/24379972/Akaverse.Boosting.Sharded.Blockchain.via.Multi-Leader.Parallel.Pipelines.1.pdf)
[Shardora_TNSE.pdf](https://github.com/user-attachments/files/24380027/Shardora_TNSE.pdf)

# Shardora/SETH/Akaverse
https://github.com/iPoW-Stack/SethPub

Scaling Blockchain Sharding via 2D Parallelism

<img width="3156" height="1671" alt="image" src="https://github.com/user-attachments/assets/62383f1f-0f19-43d5-8d14-0e8bdea51a7e" />

# Quick Start
## Requirements
      g++13.0 + 
      cmake3.25.1+

## Run local shardora network
      git clone git@github.com:tenondvpn/shardora.git
	  bash build_third.sh
      bash simple_dep.sh $node_count  
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





