#!/bin/bash

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/
ps -ef | grep zjchain | grep new_node | awk -F' ' '{print $2}' | xargs kill -9

nodes=("new_1" "new_2" "new_3" "new_4" "new_5" "new_6" "new_7" "new_8" "new_9" "new_10")


for n in  "${nodes[@]}"; do
    rm -rf /root/zjnodes/${n}/zjchain /root/zjnodes/${n}/core* /root/zjnodes/${n}/log/* /root/zjnodes/${n}/*db*

    mkdir -p "/root/zjnodes/${n}/log"
    cp -rf /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/${n}/conf
    cp -rf /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/${n}/conf
    cp -rf /root/zjnodes/zjchain/zjchain /root/zjnodes/${n}/
    cp -rf /root/zjnodes/${n}/conf/zjchain.conf /root/zjnodes/${n}/conf/zjchain.conf
    echo "cp $n"
done




for node in "${nodes[@]}"; do
  cd /root/zjnodes/$node/ && nohup ./zjchain -f 0 -g 0 $node new_node> /dev/null 2>&1 &
  echo "start $node"
done
``