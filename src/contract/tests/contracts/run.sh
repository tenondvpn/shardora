for ((i=10000000;i<=99999999;i++));
do
    
    echo b5f4bf70ae9afb2649e47488d8cd1574$i
    node run.js 0 a0793c84fb3133c0df1b9a6d5b4bbfe5e7545138 10000
    node run.js 0 b5f4bf70ae9afb2649e47488d8cd1574$i 20000
    sleep 0.1
done
# node c2c.js 0 38d2a932186ba9f9b2aa74c4c1ee8090a51b49a0 190000
#node c2c.js 0 915a76a52ed0838b37b2742342acf922adb5d0ad 190000
