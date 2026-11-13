# Datasets and query intervals

This directory contains the prepared CollegeMsg dataset and its existing query files. CollegeMsg is the default dataset: running `TCS-Enum` without arguments uses `k=4` and all ten `20%` query intervals.

| Dataset ID | Query percentages included | Queries per percentage |
| --- | --- | --- |
| `CollegeMsg` | 10, 20, 30, 40 | 10 |

Dataset IDs are case-sensitive. The package preserves the available query files exactly; it does not generate missing percentages or replace intervals.

For a dataset ID `name`, place the prepared graph in `dataset/name.txt`. The first line is `vertex_count edge_count time_count`, followed by time-sorted `source target timestamp` records. Vertex IDs and timestamps start at zero. Query intervals use the same normalized time axis and include both endpoints.

Each `name_query/percent.txt` file contains one `left right` query per line. Run:

```sh
./build/TCS-Enum -d name -k 3 -p 20
```

The full-graph static SCC file `name_SCCs.txt` is generated automatically when missing. Existing SCC files are reused, so remove the corresponding generated SCC file if you replace a graph with another graph under the same dataset ID.

See the repository README for data preparation instructions. Format-specific Python converters are also provided in `tools/`.
