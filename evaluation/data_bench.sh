#!/bin/bash

function bench {
	for i in {0..10}
	do
		echo "round $i"
		./quic_big_data -s -b $1 1>../../evaluation/out/server-$1-$i.out 2>../../evaluation/out/server-$1-$i.err &
		./quic_big_data -b $1 1>../../evaluation/out/client-$1-$i.out 2>../../evaluation/out/client-$1-$i.err
	done
}


mkdir out
cd ..
ROOT_DIR=$(pwd)

## run benchmarks with files in range of 1M...1G
echo "generating data"
cd $ROOT_DIR/data
#./generate_data.sh

echo starting benchmark now
cd $ROOT_DIR/build/bin
bench 1M
bench 10M
bench 100M
bench 1G
