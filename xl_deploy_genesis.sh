
#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
server0=127.0.0.1
target=$1
no_build=$2

echo "[$server0]"
sh ./xl_build_genesis.sh $target $no_build
cd /home/xl && sh -x fetch.sh 127.0.0.1 ${server0} '' '/home/xl' r1 r2 r3 s3_1 s3_2 s3_3 s3_4 s3_5 s3_6 s3_7 s3_8 s3_9 s3_10 s3_11 s3_12 s3_13 s3_14 s3_15 s3_16 s3_17 s3_18 s3_19 s3_20 s3_21 s3_22 s3_23 s3_24 s3_25 s3_26 s3_27 s3_28 s3_29 s3_30 s3_31 s3_32 s3_33 s3_34 s3_35 s3_36 s3_37 s3_38 s3_39 s3_40 s3_41 s3_42 s3_43 s3_44 s3_45 s3_46 s3_47 s3_48 s3_49 s3_50
echo "==== 同步中继服务器 ====" 
wait

(
echo "[$server0]"
for n in r1 r2 r3 s3_1 s3_2 s3_3 s3_4 s3_5 s3_6 s3_7 s3_8 s3_9 s3_10 s3_11 s3_12 s3_13 s3_14 s3_15 s3_16 s3_17 s3_18 s3_19 s3_20 s3_21 s3_22 s3_23 s3_24 s3_25 s3_26 s3_27 s3_28 s3_29 s3_30 s3_31 s3_32 s3_33 s3_34 s3_35 s3_36 s3_37 s3_38 s3_39 s3_40 s3_41 s3_42 s3_43 s3_44 s3_45 s3_46 s3_47 s3_48 s3_49 s3_50; do
    ln -s /home/xl/zjnodes/shardora/GeoLite2-City.mmdb /home/xl/zjnodes/${n}/conf
    ln -s /home/xl/zjnodes/shardora/conf/log4cpp.properties /home/xl/zjnodes/${n}/conf
    ln -s /home/xl/zjnodes/shardora/xlchain /home/xl/zjnodes/${n}/xlchain
done
) &

(

for n in r1 r2 r3; do
    cp -rf /home/xl/zjnodes/shardora/root_db /home/xl/zjnodes/${n}/db
done

for n in s3_1 s3_2 s3_3 s3_4 s3_5 s3_6 s3_7 s3_8 s3_9 s3_10 s3_11 s3_12 s3_13 s3_14 s3_15 s3_16 s3_17 s3_18 s3_19 s3_20 s3_21 s3_22 s3_23 s3_24 s3_25 s3_26 s3_27 s3_28 s3_29 s3_30 s3_31 s3_32 s3_33 s3_34 s3_35 s3_36 s3_37 s3_38 s3_39 s3_40 s3_41 s3_42 s3_43 s3_44 s3_45 s3_46 s3_47 s3_48 s3_49 s3_50; do
    cp -rf /home/xl/zjnodes/shardora/shard_db_3 /home/xl/zjnodes/${n}/db
done
) &
wait

echo "==== STEP1: DONE ===="

echo "==== STEP2: CLEAR OLDS ===="

ps -ef | grep xlchain | grep xl | awk -F' ' '{print $2}' | xargs kill -9

echo "==== STEP2: DONE ===="

echo "==== STEP3: EXECUTE ===="

echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/ && cd /home/xl/zjnodes/r1/ && nohup ./xlchain -f 1 -g 0 r1 xl> /dev/null 2>&1 &

sleep 3

echo "[$server0]"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64
for node in r2 r3 s3_1 s3_2 s3_3 s3_4 s3_5 s3_6 s3_7 s3_8 s3_9 s3_10 s3_11 s3_12 s3_13 s3_14 s3_15 s3_16 s3_17 s3_18 s3_19 s3_20 s3_21 s3_22 s3_23 s3_24 s3_25 s3_26 s3_27 s3_28 s3_29 s3_30 s3_31 s3_32 s3_33 s3_34 s3_35 s3_36 s3_37 s3_38 s3_39 s3_40 s3_41 s3_42 s3_43 s3_44 s3_45 s3_46 s3_47 s3_48 s3_49 s3_50; do
cd /home/xl/zjnodes/$node/ && nohup ./xlchain -f 0 -g 0 $node xl> /dev/null 2>&1 &
done


echo "==== STEP3: DONE ===="
