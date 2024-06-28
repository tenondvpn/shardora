
#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/gcc-8.3.0/lib64/

# $1 = Debug/Release
TARGET=Release
if test $1 = "Debug"
then
    TARGET=Debug
fi

# nobuild: no build & no genesis block
# noblock: build & no genesis block
NO_BUILD=0
if [ -n $2 ] && [ $2 = "nobuild" ]
then
    NO_BUILD="nobuild"
fi

if [ -n $2 ] && [ $2 = "noblock" ]
then
    NO_BUILD="noblock"
fi

if test $NO_BUILD = 0
then
	sh build.sh a $TARGET	
elif test $NO_BUILD = "noblock"
then
	sh build.sh a $TARGET
	sudo mv -f /mnt/zjnodes/zjchain /mnt/
else
	sudo mv -f /mnt/zjnodes/zjchain /mnt/
fi

sudo rm -rf /mnt/zjnodes
sudo cp -rf ./zjnodes /mnt
sudo cp -rf ./deploy /mnt
sudo cp ./fetch.sh /mnt
rm -rf /mnt/zjnodes/*/zjchain /mnt/zjnodes/*/core* /mnt/zjnodes/*/log/* /mnt/zjnodes/*/*db*

if [ $NO_BUILD = "nobuild" -o $NO_BUILD = "noblock" ]
then
	sudo rm -rf /mnt/zjnodes/zjchain
	sudo mv -f /mnt/zjchain /mnt/zjnodes/
fi
root=("r1" "r2" "r3")
shard3=("s3_1" "s3_2" "s3_3" "s3_4" "s3_5" "s3_6" "s3_7" "s3_8" "s3_9" "s3_10" "s3_11" "s3_12" "s3_13" "s3_14" "s3_15" "s3_16" "s3_17" "s3_18" "s3_19" "s3_20" "s3_21" "s3_22" "s3_23" "s3_24" "s3_25" "s3_26" "s3_27" "s3_28" "s3_29" "s3_30" "s3_31" "s3_32" "s3_33" "s3_34" "s3_35" "s3_36" "s3_37" "s3_38" "s3_39" "s3_40" "s3_41" "s3_42" "s3_43" "s3_44" "s3_45" "s3_46" "s3_47" "s3_48" "s3_49" "s3_50" "s3_51" "s3_52" "s3_53" "s3_54" "s3_55" "s3_56" "s3_57" "s3_58" "s3_59" "s3_60" "s3_61" "s3_62" "s3_63" "s3_64" "s3_65" "s3_66" "s3_67" "s3_68" "s3_69" "s3_70" "s3_71" "s3_72" "s3_73" "s3_74" "s3_75" "s3_76" "s3_77" "s3_78" "s3_79" "s3_80" "s3_81" "s3_82" "s3_83" "s3_84" "s3_85" "s3_86" "s3_87" "s3_88" "s3_89" "s3_90" "s3_91" "s3_92" "s3_93" "s3_94" "s3_95" "s3_96" "s3_97" "s3_98" "s3_99" "s3_100" "s3_101" "s3_102" "s3_103" "s3_104" "s3_105" "s3_106" "s3_107" "s3_108" "s3_109" "s3_110" "s3_111" "s3_112" "s3_113" "s3_114" "s3_115" "s3_116" "s3_117" "s3_118" "s3_119" "s3_120" "s3_121" "s3_122" "s3_123" "s3_124" "s3_125" "s3_126" "s3_127" "s3_128" "s3_129" "s3_130" "s3_131" "s3_132" "s3_133" "s3_134" "s3_135" "s3_136" "s3_137" "s3_138" "s3_139" "s3_140" "s3_141" "s3_142" "s3_143" "s3_144" "s3_145" "s3_146" "s3_147" "s3_148" "s3_149" "s3_150" "s3_151" "s3_152" "s3_153" "s3_154" "s3_155" "s3_156" "s3_157" "s3_158" "s3_159" "s3_160" "s3_161" "s3_162" "s3_163" "s3_164" "s3_165" "s3_166" "s3_167" "s3_168" "s3_169" "s3_170" "s3_171" "s3_172" "s3_173" "s3_174" "s3_175" "s3_176" "s3_177" "s3_178" "s3_179" "s3_180" "s3_181" "s3_182" "s3_183" "s3_184" "s3_185" "s3_186" "s3_187" "s3_188" "s3_189" "s3_190" "s3_191" "s3_192" "s3_193" "s3_194" "s3_195" "s3_196" "s3_197" "s3_198" "s3_199" "s3_200" "s3_201" "s3_202" "s3_203" "s3_204" "s3_205" "s3_206" "s3_207" "s3_208" "s3_209" "s3_210" "s3_211" "s3_212" "s3_213" "s3_214" "s3_215" "s3_216" "s3_217" "s3_218" "s3_219" "s3_220" "s3_221" "s3_222" "s3_223" "s3_224" "s3_225" "s3_226" "s3_227" "s3_228" "s3_229" "s3_230" "s3_231" "s3_232" "s3_233" "s3_234" "s3_235" "s3_236" "s3_237" "s3_238" "s3_239" "s3_240" "s3_241" "s3_242" "s3_243" "s3_244" "s3_245" "s3_246" "s3_247" "s3_248" "s3_249" "s3_250" "s3_251" "s3_252" "s3_253" "s3_254" "s3_255" "s3_256" "s3_257" "s3_258" "s3_259" "s3_260" "s3_261" "s3_262" "s3_263" "s3_264" "s3_265" "s3_266" "s3_267" "s3_268" "s3_269" "s3_270" "s3_271" "s3_272" "s3_273" "s3_274" "s3_275" "s3_276" "s3_277" "s3_278" "s3_279" "s3_280" "s3_281" "s3_282" "s3_283" "s3_284" "s3_285" "s3_286" "s3_287" "s3_288" "s3_289" "s3_290" "s3_291" "s3_292" "s3_293" "s3_294" "s3_295" "s3_296" "s3_297" "s3_298" "s3_299" "s3_300")
nodes=("r1" "r2" "r3" "s3_1" "s3_2" "s3_3" "s3_4" "s3_5" "s3_6" "s3_7" "s3_8" "s3_9" "s3_10" "s3_11" "s3_12" "s3_13" "s3_14" "s3_15" "s3_16" "s3_17" "s3_18" "s3_19" "s3_20" "s3_21" "s3_22" "s3_23" "s3_24" "s3_25" "s3_26" "s3_27" "s3_28" "s3_29" "s3_30" "s3_31" "s3_32" "s3_33" "s3_34" "s3_35" "s3_36" "s3_37" "s3_38" "s3_39" "s3_40" "s3_41" "s3_42" "s3_43" "s3_44" "s3_45" "s3_46" "s3_47" "s3_48" "s3_49" "s3_50" "s3_51" "s3_52" "s3_53" "s3_54" "s3_55" "s3_56" "s3_57" "s3_58" "s3_59" "s3_60" "s3_61" "s3_62" "s3_63" "s3_64" "s3_65" "s3_66" "s3_67" "s3_68" "s3_69" "s3_70" "s3_71" "s3_72" "s3_73" "s3_74" "s3_75" "s3_76" "s3_77" "s3_78" "s3_79" "s3_80" "s3_81" "s3_82" "s3_83" "s3_84" "s3_85" "s3_86" "s3_87" "s3_88" "s3_89" "s3_90" "s3_91" "s3_92" "s3_93" "s3_94" "s3_95" "s3_96" "s3_97" "s3_98" "s3_99" "s3_100" "s3_101" "s3_102" "s3_103" "s3_104" "s3_105" "s3_106" "s3_107" "s3_108" "s3_109" "s3_110" "s3_111" "s3_112" "s3_113" "s3_114" "s3_115" "s3_116" "s3_117" "s3_118" "s3_119" "s3_120" "s3_121" "s3_122" "s3_123" "s3_124" "s3_125" "s3_126" "s3_127" "s3_128" "s3_129" "s3_130" "s3_131" "s3_132" "s3_133" "s3_134" "s3_135" "s3_136" "s3_137" "s3_138" "s3_139" "s3_140" "s3_141" "s3_142" "s3_143" "s3_144" "s3_145" "s3_146" "s3_147" "s3_148" "s3_149" "s3_150" "s3_151" "s3_152" "s3_153" "s3_154" "s3_155" "s3_156" "s3_157" "s3_158" "s3_159" "s3_160" "s3_161" "s3_162" "s3_163" "s3_164" "s3_165" "s3_166" "s3_167" "s3_168" "s3_169" "s3_170" "s3_171" "s3_172" "s3_173" "s3_174" "s3_175" "s3_176" "s3_177" "s3_178" "s3_179" "s3_180" "s3_181" "s3_182" "s3_183" "s3_184" "s3_185" "s3_186" "s3_187" "s3_188" "s3_189" "s3_190" "s3_191" "s3_192" "s3_193" "s3_194" "s3_195" "s3_196" "s3_197" "s3_198" "s3_199" "s3_200" "s3_201" "s3_202" "s3_203" "s3_204" "s3_205" "s3_206" "s3_207" "s3_208" "s3_209" "s3_210" "s3_211" "s3_212" "s3_213" "s3_214" "s3_215" "s3_216" "s3_217" "s3_218" "s3_219" "s3_220" "s3_221" "s3_222" "s3_223" "s3_224" "s3_225" "s3_226" "s3_227" "s3_228" "s3_229" "s3_230" "s3_231" "s3_232" "s3_233" "s3_234" "s3_235" "s3_236" "s3_237" "s3_238" "s3_239" "s3_240" "s3_241" "s3_242" "s3_243" "s3_244" "s3_245" "s3_246" "s3_247" "s3_248" "s3_249" "s3_250" "s3_251" "s3_252" "s3_253" "s3_254" "s3_255" "s3_256" "s3_257" "s3_258" "s3_259" "s3_260" "s3_261" "s3_262" "s3_263" "s3_264" "s3_265" "s3_266" "s3_267" "s3_268" "s3_269" "s3_270" "s3_271" "s3_272" "s3_273" "s3_274" "s3_275" "s3_276" "s3_277" "s3_278" "s3_279" "s3_280" "s3_281" "s3_282" "s3_283" "s3_284" "s3_285" "s3_286" "s3_287" "s3_288" "s3_289" "s3_290" "s3_291" "s3_292" "s3_293" "s3_294" "s3_295" "s3_296" "s3_297" "s3_298" "s3_299" "s3_300")

for node in "${nodes[@]}"; do
    mkdir -p "/mnt/zjnodes/${node}/log"
    # cp -rf ./zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/${node}/conf
    # cp -rf ./zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/${node}/conf
done
cp -rf ./zjnodes/zjchain/GeoLite2-City.mmdb /mnt/zjnodes/zjchain
cp -rf ./zjnodes/zjchain/conf/log4cpp.properties /mnt/zjnodes/zjchain/conf
mkdir -p /mnt/zjnodes/zjchain/log


sudo cp -rf ./cbuild_$TARGET/zjchain /mnt/zjnodes/zjchain
sudo cp -f ./conf/genesis.yml /mnt/zjnodes/zjchain/genesis.yml

# for node in "${nodes[@]}"; do
    # sudo cp -rf ./cbuild_$TARGET/zjchain /mnt/zjnodes/${node}
# done
sudo cp -rf ./cbuild_$TARGET/zjchain /mnt/zjnodes/zjchain


if test $NO_BUILD = 0
then
    cd /mnt/zjnodes/zjchain && ./zjchain -U
    cd /mnt/zjnodes/zjchain && ./zjchain -S 3 &
    wait
fi

#for node in "${root[@]}"; do
#	cp -rf /mnt/zjnodes/zjchain/root_db /mnt/zjnodes/${node}/db
#done


#for node in "${shard3[@]}"; do
#	cp -rf /mnt/zjnodes/zjchain/shard_db_3 /mnt/zjnodes/${node}/db
#done


# 压缩 zjnodes/zjchain，便于网络传输

clickhouse-client -q "drop table zjc_ck_account_key_value_table"
clickhouse-client -q "drop table zjc_ck_account_table"
clickhouse-client -q "drop table zjc_ck_block_table"
clickhouse-client -q "drop table zjc_ck_statistic_table"
clickhouse-client -q "drop table zjc_ck_transaction_table"
