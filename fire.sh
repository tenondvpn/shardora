#!/bin/sh

if [ $# -lt 1 ]; then
    echo 'input pid'
    exit 1
fi

mkdir perf
rm -f perf/*
perf record -F 99 -p $1 -g -o perf/in-fb.data -- sleep 60
perf script -i perf/in-fb.data &> perf/perf.unfold
./FlameGraph-master/stackcollapse-perf.pl perf/perf.unfold &> perf/perf.folded
./FlameGraph-master/flamegraph.pl perf/perf.folded > perf/perf.svg
