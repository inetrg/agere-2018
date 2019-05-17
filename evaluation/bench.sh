#!/bin/bash

## without delay ##
# quic
#for i in {0..10}
#do
#	./mininet_bench.py -q -l $i -r 10
#done

#tcp
#for i in {0..10}
#do
#	./mininet_bench.py -t -l $i -r 10
#done

# reliable udp
#for i in {0..10}
#do
#	./mininet_bench.py -u -l $i -r 10
#done

# reliable udp + ordering
#for i in {0..10}
#do
#	./mininet_bench.py -u -l $i -r 10 -o
#done
#
#
### with 10ms delay ##
# quic
#for i in {0..10}
#do
#./mininet_bench.py -q -l 10 -r 10 -d 10
#done

#tcp
#for i in {6..10}
#do
	#./mininet_bench.py -t -l $i -r 10 -d 10
#done

# reliable udp
#for i in {5..10}
#do
#	./mininet_bench.py -u -l $i -r 10 -d 10
#done

# reliable udp + ordering
for i in {0..10}
do
        ./mininet_bench.py -u -l $i -r 10 -o -d 10
done

