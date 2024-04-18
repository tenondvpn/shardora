#!/bin/bash

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/
ps -ef | grep zjchain | grep new_node | awk -F' ' '{print $2}' | xargs kill -9

nodes=("new_1" "new_2" "new_3" "new_4" "new_5" "new_6" "new_7" "new_8" "new_9" "new_10" "new_11" "new_12" "new_13" "new_14" "new_15" "new_16" "new_17" "new_18" "new_19" "new_20" "new_21" "new_22" "new_23" "new_24" "new_25" "new_26" "new_27" "new_28" "new_29" "new_30" "new_31" "new_32" "new_33" "new_34" "new_35" "new_36" "new_37" "new_38" "new_39" "new_40" "new_41" "new_42" "new_43" "new_44" "new_45" "new_46" "new_47" "new_48" "new_49" "new_50")


rm -rf /root/zjnodes/new*

for n in  "${nodes[@]}"; do


    mkdir -p "/root/zjnodes/${n}/log"
    mkdir -p "/root/zjnodes/${n}/conf"
    cp -rf /root/zjnodes/zjchain/GeoLite2-City.mmdb /root/zjnodes/${n}/conf/
    cp -rf /root/zjnodes/zjchain/conf/log4cpp.properties /root/zjnodes/${n}/conf/
    cp -rf /root/zjnodes/zjchain/zjchain /root/zjnodes/${n}/
    cp -rf ./zjnodes/${n}/conf/zjchain.conf /root/zjnodes/${n}/conf/zjchain.conf
    echo "cp $n"
done


ulimit -c unlimited

for node in "${nodes[@]}"; do
  cd /root/zjnodes/$node/ && nohup ./zjchain -f 0 -g 0 $node new_node> /dev/null 2>&1 &
  echo "start $node"

done

sh ./new_nodes_dispatch_coin.sh
