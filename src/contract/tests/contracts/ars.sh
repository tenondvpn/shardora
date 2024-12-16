
node ars.js 0 0

for ((i=30;i<=100;i++));
do
    node ars.js 1 $i
    sleep 1
    node ars.js 2 $i 0,00,27e5ab858583f1d19ef272856859658246cd388f
    node ars.js 2 $i 1,01,1a31f75df2fba7607ae8566646a553451a1b8c14
    sleep 1
    node ars.js 3 $i
    sleep 1
done
