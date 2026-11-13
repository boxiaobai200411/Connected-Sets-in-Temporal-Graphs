# Connected Sets in Temporal Graphs

Code for the paper "Connected Sets in Temporal Graphs".

C++17 implementation of temporal connected set (TCS) enumeration. A temporal path must use strictly increasing timestamps, and verification only uses vertices in the selected set.

`TCS-Enum` (Temporal Connected Set Enumeration) runs three methods for each query:

| Method / output label | C++ entry point (returns the answer count) |
| --- | --- |
| Enumerate-Com | `TemporalSCCs::enumerateCom` |
| Enumerate-RG | `TemporalSCCs::enumerateRG` |
| Enumerate-Dec | `TemporalSCCs::enumerateDec` |

## Build

Requirements: Linux, CMake 3.16 or newer, and a C++17 compiler such as GCC or Clang.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake -E chdir build ctest --output-on-failure
```

The mutual-reachability method uses the bundled `coreCliqueRemoval-main` implementation. Only the source files required by this repository are included. Build from the repository root; the upstream standalone executables are not included. The upstream documentation and source notices are preserved.

## Quick start

CollegeMsg and its query files are included. With no arguments, the program runs all ten CollegeMsg queries at `k=4` and `20%` intervals:

```sh
./build/TCS-Enum
```

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
./build/TCS-Enum -d CollegeMsg -k 5 -p 20
```

This reads all intervals from `dataset/CollegeMsg_query/20.txt` and runs all three methods with `k=5`. The `-p` option accepts any nonnegative integer with a corresponding `dataset/<dataid>_query/<percent>.txt` file; it is not limited to the percentages listed above. The `-k` option accepts any integer of at least 2 within the supported integer range. Each method prints elapsed time in milliseconds and the answer count for every interval. The executable checks that the three counts agree. The decomposition size threshold is fixed at `alpha=10`.

The number of queries is determined by the query file. All three C++ entry points return the answer count and update `getAnswerCount()`. The executable counts answers without storing every answer set in memory. Timing and output are handled in `main.cpp`; enumeration indexes and helper functions are private implementation details.

The `dataset/` directory includes CollegeMsg and its original query files. Other dataset files and generated SCC files are excluded by `.gitignore`. See [dataset/README.md](dataset/README.md) for the available query files and [tools/README.md](tools/README.md) for optional format-specific converters.
