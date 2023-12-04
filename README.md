# zjchain

# for test

grep "new from add new to sharding" ./log/zjchain.log | grep "pool: 0" | awk -F' ' '{ printf("%03d", $19); print " " $15}' | sort > 0

# deploy

Project deployment, which clears all data.

```shell
sh ./deploy.sh # build and deploy and run
```

Start or stop the processes.
```shell
cd /root/deploy && sh start.sh
```
```shell
cd /rootdeploy && sh stop.sh
```

