export LD_LIBRARY_PATH=/root/Zjchain-Browser/python3.10/lib/:$LD_LIBRARY_PATH

/root/Zjchain-Browser/python3.10/bin/python3 gen_nodes_conf.py -n 10 -s 1 -m 127.0.0.1 -r 3 -m0 127.0.0.1
tail -n 265 nodes_conf_n50_s1_m5.yml >> ./nodes_conf_n10_s1_m1.yml
/root/Zjchain-Browser/python3.10/bin/python3 gen_genesis_script.py --config "./nodes_conf_n10_s1_m1.yml"
sh deploy_genesis.sh Debug
