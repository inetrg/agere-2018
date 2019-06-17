#!/bin/bash

export QUICLY_CERTS=$HOME/agere-2018/quicly/t/assets/
export DATA_PATH=/home/otto/agere-2018/data

# without delay
# TCP
#for i in {0..10}
#do
#	sudo ./mininet_bench.py -t -l $i -r 10
#done


#for i in {0..10}
#do
#	sudo ./mininet_bench.py -u -l $i -r 10
#done # reliable UDP


#for i in {0..10}
#do 
#	sudo ./minineti_bench.py -u -l $i -r 10 -o
#done # reliable UDP + ordering


#for i in {0..10}
#do
#	sudo ./mininet_bench.py -q -l $i -r 10
#done

for i in {0..10}
do
        ./mininet_bench.py -t -f -m 10M -l $i -r 10
done

for i in {0..10}
do
        ./mininet_bench.py -u -f -m 10M -l $i -r 10
done

for i in {0..10}
do
        ./mininet_bench.py -q -f -m 10M -l $i -r 10
done



# with 10ms delayi
for i in {0..10}
do
        ./mininet_bench.py -t -f -m 10M -l $i -r 10 -d 10
done

for i in {0..10}
do
        ./mininet_bench.py -u -f -m 10M -l $i -r 10 -d 10
done

for i in {0..10}
do
        ./mininet_bench.py -q -f -m 10M -l $i -r 10 -d 10
done


#for i in {0..10}
#do
#	sudo ./mininet.py -t -l $i -r 10 -d 10
#done

#for i in {0..10}
#do
#	sudo ./mininet.py -u -l $i -r 10 -d 10
#done

#for i in {0..10}
#do
#	sudo ./mininet.py -u -l $i -r 10 -o -d 10
##done
