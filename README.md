# Benchmarking the new brokers in CAF

Some measurement for the new broker concept in [CAF](https://github.com/actor-framework/actor-framework/). These measurements are based on the branch [new broker experiments](https://github.com/actor-framework/actor-framework/tree/topic/new-broker-experiments). Since the branch will likely be merged into master at some point, there is a archive of the 


## Requirements

* [CAF](https://github.com/actor-framework/actor-framework) (caf.tar.gz)
* [Google Benchmark](https://github.com/google/benchmark)
* Cmake
* C++ 11 compiler
* [Mininet](http://mininet.org)

We used the VM available on the [mininet website](http://mininet.org/download/) to run the benchmarks. CAF and google benchmark can be be build with the script `setup.sh` in the repository. *The VM was configured to use 4 processors and 4096 MB base memory.*

The plots were created with R and exported as tikz for the paper (found in the `evaluation` folder). When using RStudio, set the working directory to the root folder of this repository and the scripts should find the files to read. (They will also produce PDFs, but the scripts were adjusted to the tikz output and some of the legends might look a little bit misplaced.)


## Layers Benchmark

This benchmark uses google benchmark for the measurements. It evaluates the costs of a few layers we implemented for UDP and/or TCP. Here's the command to take the measurements:

```
$ ./build/bin/layers --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out_format=csv --benchmark_out=evaluation/layers.csv
```

The resulting csv file can be plotted with the script `layers.R` found in the evaluation folder.


## Ping Pong Benchmark

This benchmark lets two brokers exchange messages over a link with increasing loss. There is a python script to run the mininet benchmarks, `evaluation/mininet.py`. It has several options to determine which benchmarks to run:

```
$ ./evaluation/mininet.py -h
usage: mininet.py [-h] [-l LOSS] [-d DELAY] [-r RUNS] [-T THREADS] [-R RTO]
                  [-o] (-t | -u | -q)

CAF newbs on Mininet.

optional arguments:
  -h, --help            show this help message and exit
  -l LOSS, --loss LOSS  set packet loss in percent (0)
  -d DELAY, --delay DELAY
                        set link delay in ms (0)
  -r RUNS, --runs RUNS  set number of runs (1)
  -T THREADS, --threads THREADS
                        set number of threads (1)
  -R RTO, --rto RTO     set min rto for TCP (40)
  -o, --ordered         enable ordering for UDP
  -t, --tcp             use TCP
  -u, --udp             use UDP
  -q, --quic            use QUIC
```

A batch of results could be created as follows:

```
$ # without delay
$ for i in {0..10}; sudo ./mininet.py -t -l $i -r 10    ; done # TCP
$ for i in {0..10}; sudo ./mininet.py -u -l $i -r 10    ; done # reliable UDP
$ for i in {0..10}; sudo ./mininet.py -u -l $i -r 10 -o ; done # reliable UDP + ordering
$ # with 10ms delay
$ for i in {0..10}; sudo ./mininet.py -t -l $i -r 10 -d 10
$ for i in {0..10}; sudo ./mininet.py -u -l $i -r 10 -d 10
$ for i in {0..10}; sudo ./mininet.py -u -l $i -r 10 -o -d 10
```

The script stores benchmark output in `./pingpong/` using the nameing scheme `$PROTOCOL-{client,server}-$LOSS-$DELAY-$RUN.{out,err}`. Results will be stored in the `.out` file of the client. Aggregateing the results into `.csv` files can be done with the script `evaluation/pingpong/merge_logs.sh` (there is a variable in it to determine which delay measurements to aggregate). It outputs a number of csv files that should contain one colums for the loss percentage and 10 columns for measurements from 0% loss to 10% loss. It expects the requires files, i.e., the `.out` logs for loss 0% to 10%, in its folder.

Plots in tikz format can be created with the script `pingpong-{0,10}.R` using the previously created csv data.

