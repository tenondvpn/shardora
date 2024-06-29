#!/bin/bash

sh -x ci/deploy.sh Release
cd cbuild_Release && make hotstuff
sleep 3
nohup ./hotstuff 2>&1 &

echo "[TESTING] Please wait 30s..."
sleep 30

last_line=$(grep "pool: 63, tps" /root/zjnodes/s3_4/log/zjchain.log | tail -n 1)
tps_value=$(echo "$last_line" | grep -oP 'tps: \K[0-9]+\.[0-9]+')

if (( $(echo "$tps_value > 500" | bc -l) )); then
    echo "[SUCCESS]TPS value is greater than 500: $tps_value"
else
    echo "[FAILED]TPS value is not greater than 500: $tps_value"
fi

ps -ef | grep zjchain | grep root | awk -F' ' '{print $2}' | xargs kill -9
ps -ef | grep hotstuff | grep root | awk -F' ' '{print $2}' | xargs kill -9
