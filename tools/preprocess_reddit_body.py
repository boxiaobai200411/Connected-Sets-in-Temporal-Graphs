"""Convert the complete SNAP Reddit-body network into this project's format."""
import argparse
from array import array
from collections import Counter
from datetime import datetime, timedelta
import hashlib
import json
from pathlib import Path
import random

from preprocess_konect import UINT32_MAX, build_global_sccs, verify_data


SOURCE_URL = 'https://snap.stanford.edu/data/soc-redditHyperlinks-body.tsv'
SOURCE_PAGE = 'https://snap.stanford.edu/data/soc-RedditHyperlinks.html'


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def convert(args):
    source = args.input.resolve()
    output_dir = args.output_dir.resolve()
    metadata_dir = args.metadata_dir.resolve()
    query_dir = output_dir / (args.name + '_query')
    data_path = output_dir / (args.name + '.txt')
    scc_path = output_dir / (args.name + '_SCCs.txt')
    mapping_path = metadata_dir / (args.name + '_id_map.tsv')
    report_path = metadata_dir / (args.name + '_report.json')
    query_paths = [query_dir / (str(percent) + '.txt') for percent in (10, 20, 30, 40)]
    paths = [data_path, scc_path, mapping_path, report_path] + query_paths
    for path in paths:
        if path.exists() or path.with_suffix(path.suffix + '.part').exists():
            raise FileExistsError('Refusing to overwrite: ' + str(path))

    events, nodes = [], set()
    labels = Counter()
    epoch = datetime(1970, 1, 1)
    with source.open('rb') as stream:
        header = next(stream).rstrip(b'\r\n').split(b'\t')
        if header[:4] != [b'SOURCE_SUBREDDIT', b'TARGET_SUBREDDIT', b'POST_ID', b'TIMESTAMP']:
            raise ValueError('Unexpected SNAP Reddit-body header')
        for line_number, line in enumerate(stream, 2):
            fields = line.rstrip(b'\r\n').split(b'\t', 5)
            if len(fields) != 6:
                raise ValueError('Expected six TSV fields at line ' + str(line_number))
            u, v = fields[0].decode('utf-8'), fields[1].decode('utf-8')
            if not u or not v:
                raise ValueError('Empty subreddit name at line ' + str(line_number))
            # The source does not specify a timezone. Use published clock-time
            # differences without local timezone or daylight-saving conversion.
            time = datetime.strptime(fields[3].decode('ascii'), '%Y-%m-%d %H:%M:%S')
            seconds = int((time - epoch).total_seconds())
            labels[int(fields[4])] += 1
            nodes.update((u, v))
            events.append((seconds, u, v))
    if len(events) != 286561 or len(nodes) != 35776:
        raise ValueError('The downloaded file is not the complete verified SNAP body network')
    ids = {name: number for number, name in enumerate(sorted(nodes))}
    events.sort()
    min_time, max_time = events[0][0], events[-1][0]
    time_count = max_time - min_time + 1
    header = (len(ids), len(events), time_count)
    if any(value > UINT32_MAX for value in header):
        raise ValueError('Dataset exceeds uint32 limits')
    self_loops = sum(u == v for _, u, v in events)
    if self_loops:
        raise ValueError('Unexpected self-loop in SNAP Reddit-body')
    print(json.dumps(dict(phase='parsed', nodes=header[0], events=header[1],
                          time_count=time_count)), flush=True)

    output_dir.mkdir(parents=True, exist_ok=True)
    metadata_dir.mkdir(parents=True, exist_ok=True)
    query_dir.mkdir(parents=True, exist_ok=True)
    staging_data = data_path.with_suffix('.txt.part')
    sources, targets = array('I'), array('I')
    with staging_data.open('w', encoding='ascii', newline='\n') as stream:
        stream.write('{} {} {}\n'.format(*header))
        for time, raw_u, raw_v in events:
            u, v = ids[raw_u], ids[raw_v]
            stream.write('{} {} {}\n'.format(u, v, time - min_time))
            sources.append(u)
            targets.append(v)
    verify_data(staging_data, events, ids, min_time, header)

    components, singleton_count = build_global_sccs(header[0], sources, targets)
    staging_scc = scc_path.with_suffix('.txt.part')
    with staging_scc.open('w', encoding='ascii', newline='\n') as stream:
        stream.write(str(len(components)) + '\n')
        for component in components:
            stream.write(str(len(component)) + ' ' + ' '.join(map(str, component)) + '\n')
    with staging_scc.open('r', encoding='ascii') as stream:
        if int(next(stream)) != len(components):
            raise ValueError('Incorrect SCC count')
        for component in components:
            if list(map(int, next(stream).split())) != [len(component)] + component:
                raise ValueError('Incorrect SCC record')
        if stream.read().strip():
            raise ValueError('Extra SCC records')

    with mapping_path.open('w', encoding='utf-8', newline='\n') as stream:
        stream.write('original_subreddit\tnew_id\n')
        for name, number in ids.items():
            stream.write('{}\t{}\n'.format(name, number))
    staging_data.replace(data_path)
    staging_scc.replace(scc_path)

    generator = random.Random(args.seed)
    queries = []
    for percent, path in zip((10, 20, 30, 40), query_paths):
        length = time_count * percent // 100
        starts = generator.sample(range(time_count - length + 1), 10)
        windows = [(start, start + length - 1) for start in starts]
        with path.open('w', encoding='ascii', newline='\n') as stream:
            for left, right in windows:
                stream.write('{} {}\n'.format(left, right))
        queries.append(dict(percent=percent, length_seconds=length,
                            windows=windows, path=str(path), sha256=sha256(path)))

    report = dict(
        dataset=args.name, source=str(source), source_url=SOURCE_URL, source_page=SOURCE_PAGE,
        source_bytes=source.stat().st_size, source_sha256=sha256(source),
        data_file=str(data_path), data_sha256=sha256(data_path),
        scc_file=str(scc_path), scc_sha256=sha256(scc_path),
        id_mapping=str(mapping_path), id_mapping_sha256=sha256(mapping_path),
        nodes=header[0], edges=header[1], self_loops=self_loops, removed_events=0,
        original_labels=dict(labels),
        min_original_time=events[0][0], max_original_time=events[-1][0],
        original_start=(epoch + timedelta(seconds=min_time)).isoformat(sep=' '),
        original_end=(epoch + timedelta(seconds=max_time)).isoformat(sep=' '),
        shifted_min_timestamp=0, shifted_max_timestamp=time_count - 1,
        time_count=time_count, span_days=(time_count - 1) / 86400,
        nontrivial_static_sccs=len(components), largest_static_scc=len(components[0]),
        singleton_static_sccs=singleton_count,
        timestamp_policy='Published timezone-naive timestamps parsed as integer seconds; subtract minimum; no timezone or DST adjustment.',
        event_policy='All body hyperlinks retained with direction and multiplicity; sentiment and text attributes ignored.',
        verification='Every sorted output event checked against transformed source; dense IDs, time ranges, ordering and SCC serialization checked.',
        query_seed=args.seed, query_policy='Ten distinct uniformly sampled start times per fraction, without SCC/answer/runtime selection.',
        queries=queries)
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: report[key] for key in (
        'dataset', 'nodes', 'edges', 'time_count', 'span_days', 'nontrivial_static_sccs',
        'largest_static_scc', 'shifted_max_timestamp')}), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--name', default='reddit-body')
    parser.add_argument('--output-dir', type=Path, default=Path('dataset'))
    parser.add_argument('--metadata-dir', type=Path, default=Path('analysis/reddit_body_preprocess_20261107'))
    parser.add_argument('--seed', type=int, default=20261107)
    convert(parser.parse_args())
