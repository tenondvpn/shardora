node proxy_reencyption.js 0 0

for ((i=0;i<1;i++));
do
    node proxy_reencyption.js 1 $i test_content_1_$i
    sleep 3
    node proxy_reencyption.js 2 $i test_content_2_$i
    sleep 2
    node proxy_reencyption.js 3 $i test_content_3_$i
    sleep 2
    node proxy_reencyption.js 4 $i test_content_4_$i
    sleep 2
    node proxy_reencyption.js 6 $i test_content_5_$i
    sleep 2
done
node proxy_reencyption.js 30 0
