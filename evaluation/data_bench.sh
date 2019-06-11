#!/bin/bash

out_path=/home/jakob/code/agere-2018/evaluation/out

function quic_bench {
	for i in {0..10}
	do
		echo "round $i"
		./quic_big_data -s -b $1 1>$out_path/quic-server-$1-$i.out 2>$out_path/quic-server-$1-$i.err &
		./quic_big_data -b $1 1>$out_path/quic-client-$1-$i.out 2>$out_path/quic-client-$1-$i.err
	done
}


function tcp_bench {
	for i in {0..10}
        do
                echo "round $i"
                ./tcp_big_data -s -b $1 1>$out_path/tcp-server-$1-$i.out 2>$out_path/tcp-server-$1-$i.err &
                ./tcp_big_data -b $1 1>$out_path/tcp-client-$1-$i.out 2>$out_path/tcp-client-$1-$i.err
        done
}


mkdir out
rm out/*.out out/*.err
cd ..
ROOT_DIR=$(pwd)

## run benchmarks with files in range of 1M...1G
echo "generating data"
cd $ROOT_DIR/data
#./generate_data.sh

export QUICLY_CERTS="$ROOT_DIR/quicly/t/assets/"
export DATA_PATH="$ROOT_DIR/data"


cd $ROOT_DIR/build/bin
echo "starting quic benchmark now"
for size in 1M 10M 100M 1G
do
	quic_bench $size
done

echo "starting tcp benchmark now"
for size in 1M 10M 100M 1G
do
	tcp_bench $size
done
