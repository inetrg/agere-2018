# Benchmarking the new brokers in CAF

Some measurement related to a new broker concept for CAF. Based on the [CAF](https://github.com/actor-framework/actor-framework/) branch related to [new broker experiments](https://github.com/actor-framework/actor-framework/tree/topic/new-broker-experiments).

## Requirements

* CAF (commit: bd1db445)
* [Google Benchmark](https://github.com/google/benchmark)
* Cmake
* C++ compiler
* [Mininet](http://mininet.org)

## Layers Benchmark

To run the benchmarks related the layering test run the following command. The resulting csv file can be plotted with the script `layers.R` in the evaluation folder.

```
$ ./build/bin/layers --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out_format=csv --benchmark_out=evaluation/layers.csv
```

## Ping Pong Benchmark

This benchmark requires setting up a virtual network with Mininet. The mininet.py file includes a simple peer to peer topology. First, start mininet:

```
`$ mn --custom=mininet-py --topo=p2p -x
```

Then configure the loss on the links on each host:

```
$ tc ...
$ tc ...
```

Finally, run the server on one host and the client on the other:

```
$ # h1
$ ../build/bin/pingpong -s
$ #2 
$ ../build/bin/pingpong -m 2000 -H \"10.0.0.1\"
```
