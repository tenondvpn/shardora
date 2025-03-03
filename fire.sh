#!/bin/sh
pid=$1
if [ $# -lt 1 ]; then
    pid=`ps -ef | grep 's3_1 root' | grep zjchain | awk -F' ' '{print $2}'`
fi

mkdir perf
rm -f perf/*
perf record -F 99 -p $pid -g -o perf/in-fb.data -- sleep 60
perf script -i perf/in-fb.data &> perf/perf.unfold
./FlameGraph-master/stackcollapse-perf.pl perf/perf.unfold &> perf/perf.folded
./FlameGraph-master/flamegraph.pl perf/perf.folded > perf/perf.svg
