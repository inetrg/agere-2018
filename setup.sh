#!/usr/local/bin/bash

ROOT_DIR=$(pwd)

cores=4
if [ "$(uname)" == "Darwin" ]; then
  cores=$(sysctl -n hw.ncpu)
elif [ "$(uname)" == "FreeBSD" ]; then
  cores=$(sysctl -n hw.ncpu)
elif [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then
  cores=$(nproc --all)
fi

echo "Using $cores cores for compilation."

echo "Building CAF"
# This only works with newer git versions:
# git -C actor-framework pull || git clone https://github.com/actor-framework/actor-framework.git
if cd $ROOT_DIR/actor-framework
then
  git checkout topic/new-broker-experiments
  git pull
else
  cd $ROOT_DIR
  git clone https://github.com/actor-framework/actor-framework.git
  cd actor-framework
  git checkout topic/new-broker-experiments
fi
cd $ROOT_DIR/actor-framework
git apply $ROOT_DIR/caf-poll.diff
./configure --build-type=release --no-opencl --no-tools --no-examples
make -j$cores
cd $ROOT_DIR

echo "Building google benchmark"
if cd $ROOT_DIR/benchmark
then
  git pull
else
  cd $ROOT_DIR
  git clone https://github.com/google/benchmark.git
  cd benchmark
  git clone https://github.com/google/googletest.git
  mkdir build
fi
cd $ROOT_DIR/benchmark/build
cmake .. -DCMAKE_BUILD_TYPE=RELEASE
make -j$cores
cd $ROOT_DIR

