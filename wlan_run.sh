node_ips=$1
sh wlan_remote_clear.sh 1 $node_ips 3 Xf4aGbTaf! Release
node_ips_array=(${node_ips//,/ })
count=0
cp_ips=""
for ip in "${node_ips_array[@]}"; do 
    count=$(($count + 1))
    if (($count == 1)); then
        cp_ips=$ip
        continue
    fi

    cp_ips=$cp_ips","$ip
    if ((count >= 10)); then
        break
    fi
done

echo $cp_ips
sh wlan_remote_cp.sh 1 $cp_ips 3 Xf4aGbTaf! Release
sh wlan_remote_run.sh 1 $node_ips 3 Xf4aGbTaf! Release
