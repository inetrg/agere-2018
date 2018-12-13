#!/bin/bash

rm -rf build/
./configure --with-address-sanitizer --with-clang=clang++
make -j4
