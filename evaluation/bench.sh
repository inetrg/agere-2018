#!/bin/bash

function mininet_bench() {
	for i in {0..10}
	do
		./mininet_bench.py $1 $2 $i $3 $4 $5 $6 
	done
}

## without delay ##
# quic
mininet_bench() -q -l -r 10 

#tcp
mininet_bench() -t -l -r 10

# reliable udp
mininet_bench() -u -l -r 10

# reliable udp + ordering
mininet_bench() -u -l -r 10 -o

# with 10ms delay ##
# quic
mininet_bench() -q -l -r 10 -d 10

#tcp
mininet_bench() -t -l -r 10 -d 10

# reliable udp
mininet_bench() -u -l -r 10 -d 10

# reliable udp + ordering
mininet_bench() -u -l -r 10 -o -d 10

