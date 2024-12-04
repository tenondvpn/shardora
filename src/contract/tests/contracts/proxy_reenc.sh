node proxy_reencyption.js 0 0

for ((i=0;i<=10;i++));
do
    node proxy_reencyption.js 1 $i
    sleep 1
    node proxy_reencyption.js 2 $i
    sleep 1
    node proxy_reencyption.js 3 $i
    sleep 1
    node proxy_reencyption.js 4 $i
    sleep 1
    node proxy_reencyption.js 6 $i
    sleep 1
    node proxy_reencyption.js 30 $i
    sleep 1
done