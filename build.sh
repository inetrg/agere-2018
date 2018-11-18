#!/bin/bash

./configure --with-address-sanitizer --with-clang=clang++ --with-log-level=DEBUG
make -j4
