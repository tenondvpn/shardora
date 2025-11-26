#!/bin/bash
# 修改配置文件
# 确保服务器安装了 sshpass
echo "==== STEP1: START DEPLOY ===="
server0=127.0.0.1
target=$1
no_build=$2

echo "[$server0]"
cd /root/shardora && sh ./build_genesis.sh $target $no_build
cd /root && sh -x fetch.sh 127.0.0.1 ${server0} '' '/root' r1 r2 r3 s3_1 s3_2 s3_3 s3_4 s3_5 s3_6 s3_7 s3_8 s3_9 s3_10 node;

wait
echo "==== STEP2: COPY NODES ===="



nodes=("new_1" "new_2" "new_3" "new_4" "new_5" "new_6" "new_7" "new_8" "new_9" "new_10" "new_11" "new_12" "new_13" "new_14" "new_15" "new_16" "new_17" "new_18" "new_19" "new_20" "new_21" "new_22" "new_23" "new_24" "new_25" "new_26" "new_27" "new_28" "new_29" "new_30" "new_31" "new_32" "new_33" "new_34" "new_35" "new_36" "new_37" "new_38" "new_39" "new_40" "new_41" "new_42" "new_43" "new_44" "new_45" "new_46" "new_47" "new_48" "new_49" "new_50" "new_51" "new_52" "new_53" "new_54" "new_55" "new_56" "new_57" "new_58" "new_59" "new_60" "new_61" "new_62" "new_63" "new_64" "new_65" "new_66" "new_67" "new_68" "new_69" "new_70" "new_71" "new_72" "new_73" "new_74" "new_75" "new_76" "new_77" "new_78" "new_79" "new_80" "new_81" "new_82" "new_83" "new_84" "new_85" "new_86" "new_87" "new_88" "new_89" "new_90" "new_91" "new_92" "new_93" "new_94" "new_95" "new_96" "new_97" "new_98" "new_99" "new_100" "new_101" "new_102" "new_103" "new_104" "new_105" "new_106" "new_107" "new_108" "new_109" "new_110" "new_111" "new_112" "new_113" "new_114" "new_115" "new_116" "new_117" "new_118" "new_119" "new_120" "new_121" "new_122" "new_123" "new_124" "new_125" "new_126" "new_127" "new_128" "new_129" "new_130" "new_131" "new_132" "new_133" "new_134" "new_135" "new_136" "new_137" "new_138" "new_139" "new_140" "new_141" "new_142" "new_143" "new_144" "new_145" "new_146" "new_147" "new_148" "new_149" "new_150" "new_151" "new_152" "new_153" "new_154" "new_155" "new_156" "new_157" "new_158" "new_159" "new_160" "new_161" "new_162" "new_163" "new_164" "new_165" "new_166" "new_167" "new_168" "new_169" "new_170" "new_171" "new_172" "new_173" "new_174" "new_175" "new_176" "new_177" "new_178" "new_179" "new_180" "new_181" "new_182" "new_183" "new_184" "new_185" "new_186" "new_187" "new_188" "new_189" "new_190" "new_191" "new_192" "new_193" "new_194" "new_195" "new_196" "new_197" "new_198" "new_199" "new_200")


rm -rf /root/zjnodes/new*
for n in  "${nodes[@]}"; do
        cd /root/shardora
        mkdir -p "/root/zjnodes/${n}/log"
        mkdir -p "/root/zjnodes/${n}/conf"

        cp -rf ./zjnodes/${n}/conf/shardora.conf /root/zjnodes/${n}/conf/shardora.conf
        echo "cp $n"
done

echo "==== STEP3: COPY NEW NODES ===="



fromip="${1:-"127.0.0.1"}"
newip="${2:-"10.200.48.58"}"
pass="${3:-"Xf4aGbTaf!"}"



sshpass -p $pass scp -o StrictHostKeyChecking=no -r ./deploy/no_net_deploy/remote_deploy.sh root@${newip}:/root/
sshpass -p $pass ssh -o StrictHostKeyChecking=no root@${newip} 'chmod +x /root/remote_deploy.sh'

rm -rf /root/zjnodes.jar
cd /root/ && tar -czf zjnodes.jar zjnodes

sshpass -p $pass ssh -o StrictHostKeyChecking=no root@${newip} "rm -rf /root/zjnodes && mkdir -p /root/zjnodes" 

sshpass -p $pass scp -o StrictHostKeyChecking=no -r /root/zjnodes.jar root@${newip}:/root/
echo "scp zjnodes.jar to ${newip} success"
sshpass -p $pass ssh -o StrictHostKeyChecking=no root@${newip} 'bash -s' << 'EOF'
    cd /root && tar -xvf /root/zjnodes.jar  
EOF

echo "=====STEP 4: unzip zjnodes.jar to ${newip} success ===="

sshpass -p $pass ssh -o StrictHostKeyChecking=no root@${newip} 'bash /root/remote_deploy.sh'

echo "
sshpass -p $pass scp -o StrictHostKeyChecking=no -r deploy/no_net_deploy/remote_deploy.sh root@${newip}:/root/ "