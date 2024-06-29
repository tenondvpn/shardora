# zjchain

## Genesis Nodes Deployment

### 编写配置文件

编写 nodes_conf.yml 文件

```yaml
nodes:
  - name: r1
    net: 2
    server: 10.101.20.35
    http_port: 8201
    tcp_port: 12001
  - name: r2
    net: 2
    server: 10.101.20.35
    http_port: 8202
    tcp_port: 12002
  - name: r3
    net: 2
    server: 10.101.20.33
    http_port: 8203
    tcp_port: 12003
  - name: s3_1
    net: 3
    server: 10.101.20.35
    http_port: 8301
    tcp_port: 13001
  - name: s3_2
    net: 3
    server: 10.101.20.35
    http_port: 8302
    tcp_port: 13002
  - name: s3_3
    net: 3
    server: 10.101.20.35
    port: 8103
    http_port: 8303
    tcp_port: 13003
  - name: s3_4
    net: 3
    server: 10.101.20.33
    http_port: 8304
    tcp_port: 13004
  - name: s3_5
    net: 3
    server: 10.101.20.33
    http_port: 8305
    tcp_port: 13005
  - name: s3_6
    net: 3
    server: 10.101.20.32
    http_port: 8306
    tcp_port: 13006
  - name: s4_1
    net: 4
    server: 10.101.20.35
    http_port: 8401
    tcp_port: 14001
  - name: s4_2
    net: 4
    server: 10.101.20.35
    http_port: 8402
    tcp_port: 14002
  - name: s4_3
    net: 4
    server: 10.101.20.35
    http_port: 8403
    tcp_port: 14003
  - name: s4_4
    net: 4
    server: 10.101.20.35
    http_port: 8404
    tcp_port: 14004
  - name: s4_5
    net: 4
    server: 10.101.20.33
    http_port: 8405
    tcp_port: 14005
  - name: s4_6
    net: 4
    server: 10.101.20.33
    http_port: 8406
    tcp_port: 14006
  - name: s4_7
    net: 4
    server: 10.101.20.32
    http_port: 8407
    tcp_port: 14007
account_sks:
  3: # 分片 3 的创世账号私钥，共 256 个，剩余的随机生成账户
    - b5039128131f96f6164a33bc7fbc48c2f5cf425e8476b1c4d0f4d186fbd0d708
    - 02b91d27bb1761688be87898c44772e727f5e2f64aaf51a42931a0ca66a8a227
    - 580bb274af80b8d39b33f25ddbc911b14a1b3a2a6ec8ca376ffe9661cf809d36
    - 37a286f6e530de788a88ed46e15426812cf69a49bbd53f682db4f8ff5a0c84de
passwords: # 服务器密码
  10.101.20.35: '!@#$%^'
  10.101.20.33: '!@#$%^'
  10.101.20.32: '!@#$%^'
```

也可使用脚本快速生成，会自动负载均衡到服务器
```
python3 gen_nodes_conf.py -n 10 -s 1 -m 10.101.20.29,10.101.20.30 -r 3 -m0 10.101.20.29

-n --node_num_per_shard 每个分配的节点数
-s --shard_num 分片数量，比如 1 时只生成 #3
-m --machines 机器ip，逗号分割
-r --root_node_num root节点个数
-m0 --machine0 部署脚本所在机器
```

### 生成部署脚本

```
python3 gen_genesis_script.py --config "./nodes_conf_n50_s1_m5.yml" --datadir="/root/xf"

--config 节点配置文件
--datadir 节点部署到的文件夹
```

会生成部署脚本 deploy_genesis.sh

执行即可部署
```
sh deploy_genesis.sh Release/Debug
```
