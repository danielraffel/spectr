#!/usr/bin/env python3
"""Validate exported Spectr delivery/final rows, never infer GPU execution time."""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path

class TraceError(ValueError):
    pass

def integer(row, key, minimum=0):
    value = row.get(key)
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise TraceError(f"invalid_field:{key}")
    return value

def flag(row, key):
    value = row.get(key)
    if type(value) not in (bool, int) or value not in (0, 1):
        raise TraceError(f"invalid_field:{key}")
    return bool(value)

def validate(events, stats, *, expected_pid, expected_run_ids, require_gpu=True):
    if not expected_run_ids or any(type(x) is not int or x <= 0 for x in expected_run_ids):
        raise TraceError("expected_run_ids_required")
    if type(expected_pid) is not int or expected_pid <= 0:
        raise TraceError("expected_pid_required")
    if not isinstance(stats, list) or not stats:
        raise TraceError("processor_stats_required")
    for stat in stats:
        if not isinstance(stat.get('name'), str) or not isinstance(stat.get('severity'), str):
            raise TraceError("invalid_processor_stat")
        value = integer(stat, 'value')
        if value and (stat['severity'] == 'data_loss' or stat['name'] in {
            'traced_buf_write_wrap_count', 'traced_buf_bytes_overwritten',
            'traced_buf_incremental_sequences_dropped',
            'packet_skipped_seq_needs_incremental_state_invalid'}):
            raise TraceError("processor_data_loss")
    groups = defaultdict(list)
    upids = set()
    slice_ids = set()
    for row in events:
        if integer(row, 'pid', 1) != expected_pid:
            raise TraceError("unexpected_process")
        upid = integer(row, 'upid')
        upids.add(upid)
        run = integer(row, 'renderer_run_id', 1)
        if integer(row, 'schema_version') != 1:
            raise TraceError("unsupported_schema")
        sid = integer(row, 'id')
        if sid in slice_ids:
            raise TraceError("duplicate_slice")
        slice_ids.add(sid)
        integer(row, 'ts')
        if row.get('name') not in ('spectr.shared_audio.delivery', 'spectr.shared_audio.final'):
            raise TraceError("unknown_event")
        groups[(upid, run)].append(row)
    if len(upids) != 1 or {run for _, run in groups} != set(expected_run_ids):
        raise TraceError("missing_or_unexpected_run")
    summaries = []
    for (_, run), rows in groups.items():
        finals = [r for r in rows if r['name'].endswith('.final')]
        if len(finals) != 1:
            raise TraceError("final_count")
        final = finals[0]
        if not flag(final, 'physical_release_confirmed'):
            raise TraceError("release_unconfirmed")
        if flag(final, 'counter_overflow'):
            raise TraceError("counter_overflow")
        first, last = integer(final, 'first_epoch', 1), integer(final, 'last_epoch', 1)
        if first != run or last < first or last-first >= 2**32:
            raise TraceError("invalid_epoch_range")
        integer(final, 'quantum_frames', 1)
        if not 1 <= integer(final, 'lead_quanta', 1) <= 8:
            raise TraceError("invalid_lead")
        fields = ('input_quanta', 'ingress_admitted_quanta', 'terminal_attempts',
                  'terminal_enqueued', 'terminal_popped', 'lost_records',
                  'gpu_delivered', 'cpu_fallback', 'cancelled')
        counts = {k: integer(final, k) for k in fields}
        if counts['lost_records']:
            raise TraceError("terminal_ring_loss")
        if len({counts[k] for k in ('input_quanta', 'terminal_attempts', 'terminal_enqueued', 'terminal_popped')}) != 1:
            raise TraceError("source_terminal_conservation")
        deliveries = [r for r in rows if r['name'].endswith('.delivery')]
        if len(deliveries) != counts['terminal_popped']:
            raise TraceError("delivery_count")
        identities = set()
        epochs = defaultdict(list)
        outcomes = Counter()
        admitted = 0
        for row in deliveries:
            if (row['ts'], row['id']) > (final['ts'], final['id']):
                raise TraceError("delivery_after_final")
            epoch, sequence = integer(row, 'stream_epoch', 1), integer(row, 'block_sequence')
            if not first <= epoch <= last:
                raise TraceError("delivery_epoch_range")
            identity = (epoch, sequence)
            if identity in identities:
                raise TraceError("duplicate_delivery")
            identities.add(identity)
            epochs[epoch].append(sequence)
            admitted += flag(row, 'ingress_admitted')
            disposition = integer(row, 'disposition')
            if disposition not in (1, 2, 7):
                raise TraceError("unknown_disposition")
            outcomes[disposition] += 1
            for key in ('callback_fence_reason', 'worker_fence_reason'):
                if integer(row, key) > 11:
                    raise TraceError("unknown_fence_reason")
        if any(sorted(sequences) != list(range(len(sequences))) for sequences in epochs.values()):
            raise TraceError("sequence_gap")
        if admitted != counts['ingress_admitted_quanta']:
            raise TraceError("ingress_admission_count")
        if [outcomes[1], outcomes[2], outcomes[7]] != [counts['gpu_delivered'], counts['cpu_fallback'], counts['cancelled']]:
            raise TraceError("disposition_totals")
        if require_gpu and not outcomes[1]:
            raise TraceError("no_gpu_delivery")
        summaries.append({'renderer_run_id': run, 'input_quanta': counts['input_quanta'],
                          'gpu_delivered': outcomes[1], 'cpu_fallback': outcomes[2], 'cancelled': outcomes[7]})
    return {'status': 'passed', 'scope': 'trace accounting only; not GPU scheduling or performance', 'runs': summaries}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('export', type=Path, help='JSON object with events and complete processor stats arrays')
    p.add_argument('--expected-pid', type=int, required=True)
    p.add_argument('--expected-run-id', type=int, action='append', required=True)
    p.add_argument('--allow-cpu-only', action='store_true', help='Lifecycle control only, not positive GPU-use acceptance')
    args = p.parse_args()
    data = json.loads(args.export.read_text())
    try:
        print(json.dumps(validate(data['events'], data['stats'], expected_pid=args.expected_pid,
            expected_run_ids=args.expected_run_id, require_gpu=not args.allow_cpu_only), indent=2))
    except (TraceError, KeyError, TypeError) as exc:
        p.exit(1, f'trace rejected: {exc}\n')
if __name__ == '__main__':
    main()
