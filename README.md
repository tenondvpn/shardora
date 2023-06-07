# zjchain

# for test

grep "new from add new to sharding" ./log/zjchain.log | grep "pool: 0" | awk -F' ' '{ printf("%03d", $19); print " " $15}' | sort > 0
