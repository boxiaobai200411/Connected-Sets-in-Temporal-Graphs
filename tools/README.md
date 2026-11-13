# Optional dataset converters

The C++ `ProcessDataset` executable handles integer `source target timestamp` input. The Python tools in this directory support two additional source formats and use only Python's standard library.

## KONECT event files

For four-column `source target weight timestamp` input:

```sh
python3 tools/preprocess_konect.py --input /path/to/events --name prosper-loans --output-dir dataset
```

The converter expects weight 1, creates the prepared dataset and global SCC file, and records the original-to-normalized ID mapping and conversion metadata. Use `--help` for all options. Do not enable `--drop-zero-timestamps` unless excluding those events is intended.

## SNAP Reddit-body

Source: https://snap.stanford.edu/data/soc-RedditHyperlinks.html

The Reddit converter also generates query files. To preserve the supplied experimental queries, write its output to a separate directory, then copy only the graph and SCC files into `dataset/`:

```sh
python3 tools/preprocess_reddit_body.py --input /path/to/soc-redditHyperlinks-body.tsv --output-dir tmp/reddit-converted --metadata-dir tmp/reddit-metadata
cp tmp/reddit-converted/reddit-body.txt dataset/
cp tmp/reddit-converted/reddit-body_SCCs.txt dataset/
```

Existing outputs are not overwritten by either converter.
