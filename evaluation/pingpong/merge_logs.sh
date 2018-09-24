#!/bin/bash


rm reliable_udp.csv
echo "loss, value0, value1, value2, value3, value4, value5, value6, value7, value8, value9" >> reliable_udp.csv
for i in {0..10}
do
  echo "$i,$(paste -d ',' logs/reliable-udp/ppclient-$i-*.out)" >> reliable_udp.csv
done
sed -i '' 's/ms//g' reliable_udp.csv

rm tcp.csv
echo "loss, value0, value1, value2, value3, value4, value5, value6, value7, value8, value9" >> tcp.csv
for i in {0..10}
do
  echo "$i,$(paste -d ',' logs/tcp/ppclient-$i-*.out)" >> tcp.csv
done

sed -i '' 's/ms//g' tcp.csv
