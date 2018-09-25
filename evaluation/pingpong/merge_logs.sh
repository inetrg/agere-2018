#!/bin/bash

delay=0

i_arg="''"
if [ "$(uname)" == "Darwin" ]; then
  i_arg="''"
elif [ "$(uname)" == "FreeBSD" ]; then
  i_arg="''"
elif [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then
  i_arg=""
fi


file="reliable_udp-${delay}.csv"
rm $file
echo "loss, value0, value1, value2, value3, value4, value5, value6, value7, value8, value9" >> $file
for i in {0..10}
do
echo "$i,$(paste -d ',' udp-client-$i-${delay}-*.out)" >> $file
done
sed -i $i_arg 's/ms//g' "$file" 

file="reliable_ordered_udp-${delay}.csv"
rm $file
echo "loss, value0, value1, value2, value3, value4, value5, value6, value7, value8, value9" >> $file
for i in {0..10}
do
echo "$i,$(paste -d ',' udp-ordered-client-$i-${delay}-*.out)" >> $file
done
sed -i $i_arg 's/ms//g' "$file"

file="tcp-${delay}.csv"
rm $file
echo "loss, value0, value1, value2, value3, value4, value5, value6, value7, value8, value9" >> $file
for i in {0..10}
do
  echo "$i,$(paste -d ',' tcp-client-$i-${delay}-*.out)" >> $file
done

sed -i $i_arg 's/ms//g' "$file"
