#!/bin/bash

./configure --with-address-sanitizer --with-clang=clang++ --with-log-level=TRACE
make -j4
