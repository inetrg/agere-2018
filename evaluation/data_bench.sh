#!/bin/bash

out_path=$HOME/code/agere-2018/evaluation/out

function quic_bench {
	echo "quic_bench $1"
	for i in {0..10}
	do
		echo "round $i"
		./quic_big_data -s -b $1 1>$out_path/quic-server-$1-$i.out 2>$out_path/quic-server-$1-$i.err &
		./quic_big_data -b $1 1>$out_path/quic-client-$1-$i.out 2>$out_path/quic-client-$1-$i.err
	done
}


function tcp_bench {
	echo "tcp_bench $1"
	for i in {0..10}
        do
                echo "round $i"
                ./tcp_big_data -s -b $1 1>$out_path/tcp-server-$1-$i.out 2>$out_path/tcp-server-$1-$i.err &
                ./tcp_big_data -b $1 1>$out_path/tcp-client-$1-$i.out 2>$out_path/tcp-client-$1-$i.err
        done
}


function traditional_tcp_bench {
	echo "# traditional_tcp_bench $1"
	for i in {0..10}
        do
                echo "round $i"
                ./tcp_big_data -t -s -b $1 1>$out_path/traditional-tcp-server-$1-$i.out 2>$out_path/traditional-tcp-server-$1-$i.err &
                ./tcp_big_data -t -b $1 1>$out_path/traditional-tcp-client-$1-$i.out 2>$out_path/traditional-tcp-client-$1-$i.err
        done
}


function udp_bench {
        echo "# udp_bench $1"
        for i in {0..10}
        do
                echo "round $i"
                ./udp_big_data -s -b $1 1>$out_path/udp-server-$1-$i.out 2>$out_path/udp-server-$1-$i.err &
                ./udp_big_data -b $1 1>$out_path/udp-client-$1-$i.out 2>$out_path/udp-client-$1-$i.err
        done
}


function ordered_udp_bench {                                       
        echo "# udp_bench $1"            
        for i in {0..10}
        do
                echo "round $i"
                ./udp_big_data -o -s -b $1 1>$out_path/ordered-udp-server-$1-$i.out 2>$out_path/ordered-udp-server-$1-$i.err & 
                ./udp_big_data -o -b $1 1>$out_path/ordered-udp-client-$1-$i.out 2>$out_path/ordered-udp-client-$1-$i.err 
        done
}





mkdir out
#rm out/*.out out/*.err
cd ..
ROOT_DIR=$(pwd)

## run benchmarks with files in range of 1M...1G
echo "generating data"
cd $ROOT_DIR/data
#rm -rf *-file
#./generate_data.sh

export QUICLY_CERTS="$ROOT_DIR/quicly/t/assets/"
export DATA_PATH="$ROOT_DIR/data"


cd $ROOT_DIR/build/bin
#echo "## starting quic benchmark now"
#for size in 1M 10M 100M 1G
#do
#	quic_bench $size
#done

#echo "starting tcp benchmark now"
#for size in 1M 10M 100M 1G
#do
#	tcp_bench $size
#done

#echo "starting traditional tcp benchmark now"
#for size in 1M 10M 100M 1G
#do
#        traditional_tcp_bench $size
#done

echo "starting udp benchmark now"
for size in 1M 10M 100M 1G
do
        udp_bench $size
done

echo "starting ordered udp benchmark now"            
for size in 1M 10M 100M 1G
do
        ordered_udp_bench $size            
done


