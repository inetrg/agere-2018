# Measuring CAF NEWBs

Some measurement related to a new broker concept for CAF. Based on the [CAF](https://github.com/actor-framework/actor-framework/) branch related to [new broker experiments](https://github.com/actor-framework/actor-framework/tree/topic/new-broker-experiments).

## REQUIREMENTS

* CAF with related branch
* google benchmark
* cmake
* C++ compiler (17)


## Run Layers Benchmark

```
$ ./build/bin/four_udp --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out_format=csv --benchmark_out=evaluation/layers.csv
```
