#!/bin/bash

./configure --with-address-sanitizer --with-clang=clang++
make -j4
