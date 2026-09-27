#!/usr/bin/env python3
"""Bind a complete trace to stopped native-host queries made before processing."""
import argparse
import json
from pathlib import Path
from validate_shared_spectral_trace import TraceError, integer, validate

def expected_runs(records, expected_pid):
    if not records or records[-1].get('kind')!='complete':
        raise TraceError('host_inventory_incomplete')
    seen=set();gpu=set();token=None
    for ordinal,row in enumerate(records[:-1],1):
        if row.get('schema')!='spectr.native-host-trace-inventory.v1' or row.get('kind')!='prepared':
            raise TraceError('invalid_host_inventory_record')
        if integer(row,'pid',1)!=expected_pid or integer(row,'prepare_ordinal',1)!=ordinal:
            raise TraceError('host_inventory_identity')
        actual_token=integer(row,'instance_token',1)
        if token is None:token=actual_token
        if token!=actual_token:raise TraceError('host_instance_changed')
        run=integer(row,'renderer_run_id',1)
        if run in seen:raise TraceError('duplicate_host_run')
        seen.add(run)
        if type(row.get('gpu_required')) is not bool:raise TraceError('invalid_gpu_requirement')
        if row['gpu_required']:gpu.add(run)
    final=records[-1]
    if (final.get('schema')!='spectr.native-host-trace-inventory.v1' or
        integer(final,'pid',1)!=expected_pid or final.get('module_closed') is not True or
        integer(final,'prepared_count',1)!=len(seen)):
        raise TraceError('host_inventory_final')
    return seen,gpu

def validate_host_trace(records,events,stats,*,expected_pid,allow_cpu_only=False):
    runs,gpu=expected_runs(records,expected_pid)
    if not gpu and not allow_cpu_only:raise TraceError('host_gpu_scenario_required')
    result=validate(events,stats,expected_pid=expected_pid,expected_run_ids=sorted(runs),require_gpu=False)
    for row in result['runs']:
        if row['renderer_run_id'] in gpu and row['gpu_delivered']==0:
            raise TraceError('host_gpu_scenario_not_delivered')
    result['identity_source']='native-host-stopped-v2-probe-before-processing'
    result['gpu_required_run_ids']=sorted(gpu)
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('export',type=Path);p.add_argument('--host-inventory',type=Path,required=True)
    p.add_argument('--expected-pid',type=int,required=True)
    p.add_argument('--allow-cpu-only',action='store_true')
    a=p.parse_args();data=json.loads(a.export.read_text())
    rows=[json.loads(line) for line in a.host_inventory.read_text().splitlines()]
    print(json.dumps(validate_host_trace(rows,data['events'],data['stats'],expected_pid=a.expected_pid,
                                        allow_cpu_only=a.allow_cpu_only),indent=2))
if __name__=='__main__':main()
