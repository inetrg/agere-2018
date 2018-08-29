# Benchmarking the new brokers in CAF

Some measurement related to a new broker concept for CAF. Based on the [CAF](https://github.com/actor-framework/actor-framework/) branch related to [new broker experiments](https://github.com/actor-framework/actor-framework/tree/topic/new-broker-experiments).

## Requirements

* CAF (commit: e388b2b2)
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

Then configure the loss on the links on each host. To change the loss to new value you have to delete the previous configuration first. Use the same command, with `del` instead of `add`.

```
$ # h1
$ sudo tc qdisc add dev h1-eth0 root netem loss 10%
$ # h2
$ sudo tc qdisc add dev h2-eth0 root netem loss 10%
```

Finally, run the server on one host and the client on the other:

```
$ # h1
$ ../build/bin/pingpong -s
$ #2 
$ ../build/bin/pingpong -m 2000 -H \"10.0.0.1\"
```

The script `pingpong.R` plots the data and expectes it to be in a csv file containing a row with headers, for example `loss, value0, value1, ...value9` followed by rows of measurements, each having the loss in the first column and time values in ms (without the uint suffix) in the following columns.1

