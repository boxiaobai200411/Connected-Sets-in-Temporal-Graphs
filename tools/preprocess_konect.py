"""Convert a KONECT event file to this project's dataset and global SCC format."""
import argparse
from array import array
import hashlib
import json
from pathlib import Path


UINT32_MAX = (1 << 32) - 1


def build_global_sccs(n, sources, targets):
    """Iterative Kosaraju on compact CSR arrays; keep components of size >= 2."""
    outgoing = array('I', [0]) * (n + 1)
    incoming = array('I', [0]) * (n + 1)
    for u, v in zip(sources, targets):
        outgoing[u + 1] += 1
        incoming[v + 1] += 1
    for u in range(n):
        outgoing[u + 1] += outgoing[u]
        incoming[u + 1] += incoming[u]
    forward = array('I', [0]) * len(sources)
    reverse = array('I', [0]) * len(sources)
    out_cursor = outgoing[:-1]
    in_cursor = incoming[:-1]
    for u, v in zip(sources, targets):
        forward[out_cursor[u]] = v
        out_cursor[u] += 1
        reverse[in_cursor[v]] = u
        in_cursor[v] += 1

    visited = bytearray(n)
    cursor = outgoing[:-1]
    order = array('I')
    for root in range(n):
        if visited[root]:
            continue
        visited[root] = 1
        stack = [root]
        while stack:
            u = stack[-1]
            position = cursor[u]
            if position == outgoing[u + 1]:
                order.append(stack.pop())
                continue
            cursor[u] += 1
            v = forward[position]
            if not visited[v]:
                visited[v] = 1
                stack.append(v)

    assigned = bytearray(n)
    components = []
    singleton_count = 0
    for root in reversed(order):
        if assigned[root]:
            continue
        assigned[root] = 1
        stack = [root]
        component = []
        while stack:
            u = stack.pop()
            component.append(u)
            for position in range(incoming[u], incoming[u + 1]):
                v = reverse[position]
                if not assigned[v]:
                    assigned[v] = 1
                    stack.append(v)
        if len(component) >= 2:
            component.sort()
            components.append(component)
        else:
            singleton_count += 1
    components.sort(key=lambda component: (-len(component), component[0]))
    assert sum(map(len, components)) + singleton_count == n
    return components, singleton_count


def verify_data(path, edges, ids, min_time, header):
    """Compare every serialized event with its corresponding original event."""
    observed_nodes = bytearray(header[0])
    previous = None
    with path.open('r', encoding='ascii') as stream:
        if tuple(map(int, next(stream).split())) != header:
            raise ValueError('Incorrect output header')
        for t, raw_u, raw_v in edges:
            fields = tuple(map(int, stream.readline().split()))
            expected = (ids[raw_u], ids[raw_v], t - min_time)
            if fields != expected:
                raise ValueError('Output event does not match original event')
            u, v, time = fields
            if not (0 <= u < header[0] and 0 <= v < header[0] and
                    u != v and 0 <= time < header[2]):
                raise ValueError('Invalid output event')
            order = (time, u, v)
            if previous is not None and order < previous:
                raise ValueError('Output events are not sorted')
            previous = order
            observed_nodes[u] = observed_nodes[v] = 1
        if stream.read().strip():
            raise ValueError('Extra output records')
    if sum(observed_nodes) != header[0]:
        raise ValueError('Output IDs are not dense')
    if edges and (edges[0][0] - min_time != 0 or previous[0] != header[2] - 1):
        raise ValueError('Incorrect shifted time range')


def convert(args):
    source = args.input.resolve()
    output_dir = args.output_dir.resolve()
    metadata_dir = args.metadata_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    metadata_dir.mkdir(parents=True, exist_ok=True)
    data_path = output_dir / (args.name + '.txt')
    scc_path = output_dir / (args.name + '_SCCs.txt')
    mapping_path = metadata_dir / (args.name + '_id_map.tsv')
    report_path = metadata_dir / (args.name + '_report.json')
    for path in (data_path, scc_path, mapping_path, report_path):
        if path.exists():
            raise FileExistsError('Refusing to overwrite: ' + str(path))

    edges = []
    nodes = set()
    raw_count = loop_count = removed_zero_count = 0
    digest = hashlib.sha256()
    excluded_zero = []
    with source.open('rb') as stream:
        for line_number, raw in enumerate(stream, 1):
            digest.update(raw)
            line = raw.strip()
            if not line or line.startswith((b'%', b'#')):
                continue
            fields = line.split()
            if len(fields) != 4:
                raise ValueError('Expected u v weight timestamp at line ' + str(line_number))
            u, v, weight, t = map(int, fields)
            if weight != 1:
                raise ValueError('Expected an unweighted event at line ' + str(line_number))
            raw_count += 1
            if u == v:
                loop_count += 1
                continue
            if args.drop_zero_timestamps and t == 0:
                removed_zero_count += 1
                excluded_zero.append(dict(line=line_number, source=u, target=v, timestamp=t))
                continue
            nodes.update((u, v))
            edges.append((t, u, v))
    ids = {old: new for new, old in enumerate(sorted(nodes))}
    edges.sort()
    min_time = edges[0][0] if edges else 0
    max_time = edges[-1][0] if edges else 0
    time_count = max_time - min_time + 1 if edges else 0
    header = (len(ids), len(edges), time_count)
    if any(value > UINT32_MAX for value in header):
        raise ValueError('Dataset exceeds uint32 limits')
    print(json.dumps(dict(dataset=args.name, phase='parsed', nodes=header[0],
                          edges=header[1], time_count=header[2]), ensure_ascii=True), flush=True)

    staging_data = data_path.with_suffix('.txt.part')
    sources, targets = array('I'), array('I')
    with staging_data.open('w', encoding='ascii', newline='\n') as stream:
        stream.write('{} {} {}\n'.format(*header))
        for t, raw_u, raw_v in edges:
            u, v = ids[raw_u], ids[raw_v]
            stream.write('{} {} {}\n'.format(u, v, t - min_time))
            sources.append(u)
            targets.append(v)
    verify_data(staging_data, edges, ids, min_time, header)
    del edges
    print(json.dumps(dict(dataset=args.name, phase='events_verified'), ensure_ascii=True), flush=True)
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
            values = list(map(int, next(stream).split()))
            if values != [len(component)] + component:
                raise ValueError('Incorrect SCC record')
        if stream.read().strip():
            raise ValueError('Extra SCC records')

    with mapping_path.open('w', encoding='ascii', newline='\n') as stream:
        stream.write('original_id\tnew_id\n')
        for original_id, new_id in ids.items():
            stream.write('{}\t{}\n'.format(original_id, new_id))
    staging_data.replace(data_path)
    staging_scc.replace(scc_path)
    report = dict(dataset=args.name, source=str(source), source_sha256=digest.hexdigest(),
                  data_file=str(data_path), scc_file=str(scc_path), id_mapping=str(mapping_path),
                  raw_edges=raw_count, removed_self_loops=loop_count,
                  removed_zero_timestamps=removed_zero_count, excluded_zero_records=excluded_zero,
                  nodes=header[0], edges=header[1], min_original_timestamp=min_time,
                  max_original_timestamp=max_time, time_count=time_count,
                  shifted_min_timestamp=0 if header[1] else None,
                  shifted_max_timestamp=max_time-min_time if header[1] else None,
                  span_days=(max_time-min_time)/86400 if header[1] else 0,
                  nontrivial_static_sccs=len(components),
                  largest_static_scc=max(map(len, components), default=0),
                  singleton_static_sccs=singleton_count,
                  verification='Every output event compared with the corresponding transformed source event; dense IDs, ranges, order, and SCC serialization checked.')
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    print(json.dumps(report, ensure_ascii=True), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--name', required=True)
    parser.add_argument('--output-dir', type=Path, default=Path('dataset'))
    parser.add_argument('--metadata-dir', type=Path, default=Path('tmp/konect_preprocess'))
    parser.add_argument('--drop-zero-timestamps', action='store_true',
                        help='Explicitly exclude source events with timestamp 0 before shifting')
    convert(parser.parse_args())
