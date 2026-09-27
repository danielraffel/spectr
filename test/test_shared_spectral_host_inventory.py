import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).parents[1]/'tools'))
from validate_shared_spectral_host_inventory import validate_host_trace,TraceError
from test_shared_spectral_trace_accounting import SpectralTraceControls

class HostInventoryControls(unittest.TestCase):
    def fixture(self):
        events,stats=SpectralTraceControls().fixture()
        more=copy.deepcopy(events)
        for row in more:
            row['id']+=10;row['ts']+=30;row['renderer_run_id']=200
            for field in ('stream_epoch','first_epoch','last_epoch'):
                if field in row:row[field]+=100
        records=[dict(schema='spectr.native-host-trace-inventory.v1',kind='prepared',pid=41,
                 instance_token=7,prepare_ordinal=i+1,renderer_run_id=run,gpu_required=i==0)
                 for i,run in enumerate((100,200))]
        records.append(dict(schema='spectr.native-host-trace-inventory.v1',kind='complete',pid=41,
                            prepared_count=2,module_closed=True))
        return records,events+more,stats
    def run_fixture(self,r,e,s):return validate_host_trace(r,e,s,expected_pid=41)
    def test_positive(self):self.assertEqual(self.run_fixture(*self.fixture())['status'],'passed')
    def test_entire_run_missing(self):
        r,e,s=self.fixture()
        with self.assertRaisesRegex(TraceError,'missing_or_unexpected_run'):self.run_fixture(r,e[:4],s)
    def test_unexpected_run(self):
        r,e,s=self.fixture();r.pop(1);r[-1]['prepared_count']=1
        with self.assertRaisesRegex(TraceError,'missing_or_unexpected_run'):self.run_fixture(r,e,s)
    def test_bad_inventory(self):
        changes=[lambda r:r.pop(),lambda r:r[0].update(pid=42),lambda r:r[0].update(prepare_ordinal=2),
                 lambda r:r[1].update(renderer_run_id=100),lambda r:r[-1].update(prepared_count=3),
                 lambda r:r[-1].update(module_closed=False),lambda r:r[0].update(gpu_required=1),
                 lambda r:r[1].update(instance_token=8)]
        for change in changes:
            with self.subTest(change=change):
                r,e,s=self.fixture();change(r)
                with self.assertRaises(TraceError):self.run_fixture(r,e,s)
    def test_missing_final(self):
        r,e,s=self.fixture();e.pop()
        with self.assertRaisesRegex(TraceError,'final_count'):self.run_fixture(r,e,s)
    def test_cpu_control_is_not_gpu_proof(self):
        r,e,s=self.fixture()
        for row in r[:-1]:row['gpu_required']=False
        with self.assertRaisesRegex(TraceError,'host_gpu_scenario_required'):self.run_fixture(r,e,s)
if __name__=='__main__':unittest.main()
