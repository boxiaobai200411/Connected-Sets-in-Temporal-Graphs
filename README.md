# Connected Sets in Temporal Graphs

Code for the paper "Connected Sets in Temporal Graphs".

C++17 implementation of temporal connected set (TCS) enumeration. A temporal path must use strictly increasing timestamps, and verification only uses vertices in the selected set.

The comparison executable runs three methods for each query:

| Method / output label | C++ entry point              |
| --------------------- | ---------------------------- |
| Enumerate-Com         | `TemporalSCCs::enumerateCom` |
| Enumerate-RG          | `TemporalSCCs::enumerateRG`  |
| Enumerate-Dec         | `TemporalSCCs::enumerateDec` |

## Build

Requirements: Linux, CMake 3.16 or newer, and a C++17 compiler such as GCC or Clang.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake -E chdir build ctest --output-on-failure
```

The mutual-reachability method uses the bundled `coreCliqueRemoval-main` implementation. Only the source files required by this repository are included. Build from the repository root; the upstream standalone executables are not included. The upstream documentation and source notices are preserved.

## Quick start

A small dataset is included so the compiled program can be run immediately:

```sh
./build/BlackHoleSCC -d demo -k 3 -p 20
```

All three methods should report one answer for each of the two demo queries. The first run creates `dataset/demo_SCCs.txt` automatically.

## Dataset format

Place each prepared dataset in `dataset/<dataid>.txt`:

```text
vertex_count edge_count time_count
source target timestamp
source target timestamp
```

Vertex IDs must be in `[0, vertex_count)`. Timestamps must be in `[0, time_count)`, and events must be sorted by nondecreasing timestamp. Equal timestamps cannot be chained into a temporal path. Repeated events are allowed.

The loader reads `dataset/<dataid>_SCCs.txt`. If this file is missing, it computes the static SCCs of the entire directed graph, ignoring timestamps, and saves the file automatically. Only components containing at least two vertices are retained. Existing SCC files are checked for malformed records and reused. These full-graph static SCCs are a preprocessing filter; they are not temporal connected sets.

The SCC file contains the number of components followed by one record per component:

```text
component_count
component_size vertex_id vertex_id ...
```

The public `TemporalSCCs::createGlobalSCCs()` function computes these components using an iterative Tarjan traversal of the loaded graph. Dataset loading and SCC generation occur before query timing.

## Prepare a raw dataset

For an unprepared file containing three integer columns `source target timestamp`, with no header:

```sh
./build/ProcessDataset -i /path/to/raw.txt -d my-dataset
```

This creates `dataset/my-dataset.txt` and `dataset/my-dataset_SCCs.txt`. Use `-o /path/to/existing-directory` to select another output directory. Existing output files are not overwritten.

Preprocessing maps vertex IDs to consecutive IDs starting at zero, shifts the minimum timestamp to zero while preserving time gaps, sorts events, and removes self-loops. Repeated events are preserved. Empty lines and comments starting with `#` or `%` are accepted. Query timestamps must use the normalized time axis.

## Run queries

Store inclusive query intervals, one `left right` pair per line, in:

```text
dataset/<dataid>_query/5.txt
dataset/<dataid>_query/10.txt
dataset/<dataid>_query/20.txt
dataset/<dataid>_query/30.txt
dataset/<dataid>_query/40.txt
```

For example:

```sh
./build/BlackHoleSCC -d CollegeMsg -k 5 -p 20
```

This reads all intervals from `dataset/CollegeMsg_query/20.txt` and runs all three methods with `k=5`. Each method prints elapsed time in milliseconds and the answer count for every interval. The executable checks that the three counts agree. The decomposition size threshold is fixed at `alpha=10`.

The number of queries is determined by the query file. Linux also reports the process-wide `VmPeak` after all queries; this includes the loaded dataset and is not an individual method's auxiliary memory usage. The executable counts answers without storing every answer set in memory.

The `dataset/` directory includes the original query files for eight experimental datasets and a small demo graph. Large dataset files and generated SCC files are excluded by `.gitignore`. See [dataset/README.md](dataset/README.md) for the available query files and [tools/README.md](tools/README.md) for optional format-specific converters.
