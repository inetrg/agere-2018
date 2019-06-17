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

file="quic-data.csv"
rm $file
echo "file_size, value0, value1, value2, value3, value4, value5, value6, value7, value8, value9" >> $file
for size in 1M 10M 100M 1G
do
	echo "$size,$(paste -d ',' quic-client-$size-*.out)" >> $file
done
sed -i $i_arg 's/ms//g' "$file"


file="tcp-data.csv"
rm $file
echo "file_size, value0, value1, value2, value3, value4, value5, value6, value7, value8, value9" >> $file
for size in 1M 10M 100M 1G
do
        echo "$size,$(paste -d ',' tcp-client-$size-*.out)" >> $file
done
sed -i $i_arg 's/ms//g' "$file"

