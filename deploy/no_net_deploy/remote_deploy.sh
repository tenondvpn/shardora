#!/bin/bash
export CC=/usr/local/gcc-8.3.0/bin/gcc
export CXX=/usr/local/gcc-8.3.0/bin/g++
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/ && cd /root/zjnodes/r1/ && nohup ./shardora -f 1 -g 0 r1 root> /dev/null 2>&1 &
ulimit -c unlimited
# 参数
fromip="${1:-"127.0.0.1"}"
newip="${2:-"10.200.48.58"}"
pass="${3:-"Xf4aGbTaf!"}"

# 脚本内容
rm -rf /root/zjnodes && mkdir -p /root/zjnodes
cd /root && tar -xvf /root/zjnodes.jar

for n in r1 r2 r3 s3_1 s3_2 s3_3 s3_4 s3_5 s3_6 s3_7 s3_8 s3_9 s3_10 node; do
    cp -rf /root/zjnodes/shardora/GeoLite2-City.mmdb /root/zjnodes/${n}/conf
    cp -rf /root/zjnodes/shardora/conf/log4cpp.properties /root/zjnodes/${n}/conf
    cp -rf /root/zjnodes/shardora/shardora /root/zjnodes/${n}
done

for n in r1 r2 r3; do
    cp -rf /root/zjnodes/shardora/root_db /root/zjnodes/${n}/db
done

for n in s3_1 s3_2 s3_3 s3_4 s3_5 s3_6 s3_7 s3_8 s3_9 s3_10; do
    cp -rf /root/zjnodes/shardora/shard_db_3 /root/zjnodes/${n}/db
done


sleep 3

for node in r2 r3 s3_1 s3_2 s3_3 s3_4 s3_5 s3_6 s3_7 s3_8 s3_9 s3_10 node; do
    cd /root/zjnodes/$node/ && nohup ./shardora -f 0 -g 0 $node root> /dev/null 2>&1 &
done

ps -ef | grep shardora | grep new_node | awk -F' ' '{print $2}' | xargs kill -9

nodes=("new_1" "new_2" "new_3" "new_4" "new_5" "new_6" "new_7" "new_8" "new_9" "new_10" "new_11" "new_12" "new_13" "new_14" "new_15" "new_16" "new_17" "new_18" "new_19" "new_20" "new_21" "new_22" "new_23" "new_24" "new_25" "new_26" "new_27" "new_28" "new_29" "new_30" "new_31" "new_32" "new_33" "new_34" "new_35" "new_36" "new_37" "new_38" "new_39" "new_40" "new_41" "new_42" "new_43" "new_44" "new_45" "new_46" "new_47" "new_48" "new_49" "new_50" "new_51" "new_52" "new_53" "new_54" "new_55" "new_56" "new_57" "new_58" "new_59" "new_60" "new_61" "new_62" "new_63" "new_64" "new_65" "new_66" "new_67" "new_68" "new_69" "new_70" "new_71" "new_72" "new_73" "new_74" "new_75" "new_76" "new_77" "new_78" "new_79" "new_80" "new_81" "new_82" "new_83" "new_84" "new_85" "new_86" "new_87" "new_88" "new_89" "new_90" "new_91" "new_92" "new_93" "new_94" "new_95" "new_96" "new_97" "new_98" "new_99" "new_100" "new_101" "new_102" "new_103" "new_104" "new_105" "new_106" "new_107" "new_108" "new_109" "new_110" "new_111" "new_112" "new_113" "new_114" "new_115" "new_116" "new_117" "new_118" "new_119" "new_120" "new_121" "new_122" "new_123" "new_124" "new_125" "new_126" "new_127" "new_128" "new_129" "new_130" "new_131" "new_132" "new_133" "new_134" "new_135" "new_136" "new_137" "new_138" "new_139" "new_140" "new_141" "new_142" "new_143" "new_144" "new_145" "new_146" "new_147" "new_148" "new_149" "new_150" "new_151" "new_152" "new_153" "new_154" "new_155" "new_156" "new_157" "new_158" "new_159" "new_160" "new_161" "new_162" "new_163" "new_164" "new_165" "new_166" "new_167" "new_168" "new_169" "new_170" "new_171" "new_172" "new_173" "new_174" "new_175" "new_176" "new_177" "new_178" "new_179" "new_180" "new_181" "new_182" "new_183" "new_184" "new_185" "new_186" "new_187" "new_188" "new_189" "new_190" "new_191" "new_192" "new_193" "new_194" "new_195" "new_196" "new_197" "new_198" "new_199" "new_200")


for node in "${nodes[@]}"; do
    cp -rf /root/zjnodes/shardora/GeoLite2-City.mmdb /root/zjnodes/${n}/conf/
    cp -rf /root/zjnodes/shardora/conf/log4cpp.properties /root/zjnodes/${n}/conf/
    cp -rf /root/zjnodes/shardora/shardora /root/zjnodes/${n}/
    echo "cp $node"
done
for node in "${nodes[@]}"; do
    cd /root/zjnodes/$node/ && nohup ./shardora -f 0 -g 0 $node new_node> /dev/null 2>&1 &
    echo "start $node"
done