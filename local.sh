export LD_LIBRARY_PATH=/usr/local/python3/lib/python3.10/:$LD_LIBRARY_PATH

#文件名
FILE_NAME='./local.sh'
#获取文件做后修改时间戳
LAST_MODIFY_TIMESTAMP=`stat -c %Y  $FILE_NAME`
#格式化时间戳
formart_date=`date '+%Y%m%d%H%M%S' -d @$LAST_MODIFY_TIMESTAMP`
old_tm=`cat modify_time`
echo $old_tm

if [ "$old_tm" -eq "$formart_date" ];then
     echo "eq called!"
     sh deploy_genesis.sh Debug
     exit 0
fi
echo $formart_date > modify_time
echo "new deploy"
python3 gen_nodes_conf.py -n 4 -s 1 -m 127.0.0.1 -r 3 -m0 127.0.0.1
tail -n 261 nodes_conf_n50_s1_m5.yml >> ./nodes_conf_n4_s1_m1.yml
python3 gen_genesis_script.py --config "./nodes_conf_n4_s1_m1.yml"
#pkill -f shardora
sh deploy_genesis.sh Debug || true
sleep 5
exit 0
sh new_nodes_dispatch_coin.sh  || true
python test_accounts.py 
