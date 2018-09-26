# Benchmarking the new brokers in CAF

Some measurement related to a new broker concept for CAF. Based on the [CAF](https://github.com/actor-framework/actor-framework/) branch related to [new broker experiments](https://github.com/actor-framework/actor-framework/tree/topic/new-broker-experiments).


## Requirements

* [CAF](https://github.com/actor-framework/actor-framework) (with the `caf-poll.diff` patch applied)
* [Google Benchmark](https://github.com/google/benchmark)
* Cmake
* C++ 11 compiler
* [Mininet](http://mininet.org)

We used the VM available on the [mininet website](http://mininet.org/download/) to run the benchmarks. CAF and google benchmark can be be build with the script `setup.sh` in the repository


## Layers Benchmark

To run the benchmarks related the layering test run the following command. The resulting csv file can be plotted with the script `layers.R` in the evaluation folder.

```
$ ./build/bin/layers --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out_format=csv --benchmark_out=evaluation/layers.csv
```


## Ping Pong Benchmark

There is a python script to run the mininet benchmarks, `evaluation/mininet.py`. It has several options to determine which benchmarks to run:

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
$ for i in {0..10}; sudo ./mininet.py -t -l $i -r 10    ; done # TCP
$ for i in {0..10}; sudo ./mininet.py -u -l $i -r 10    ; done # reliable UDP
$ for i in {0..10}; sudo ./mininet.py -u -l $i -r 10 -o ; done # reliable UDP + ordering
```

The script stores benchmark output in `./pingpong/` using the nameing scheme `$PROTOCOL-{client,server}-$LOSS-$DELAY-$RUN.{out,err}`. Results will be stored in the `.out` file of the client. Aggregateing the results into `.csv` files is done by the `evaluation/pingpong/merge_logs.sh`. It creates a number of csv files that should contain one colums for the loss percentage and 10 columns for measurements from 0% loss to 10% loss. It expects the requires files, i.e., the `.out` logs for loss 0% to 10%, in its folder.

Plots in tikz format can be created with the script `pingpong.R` using the previously created csv data.

