import copy
import importlib.util
from pathlib import Path
import unittest
import sqlite3
SPEC = importlib.util.spec_from_file_location('trace_validator', Path(__file__).parents[1]/'tools/validate_shared_spectral_trace.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)

class SpectralTraceControls(unittest.TestCase):
    def fixture(self):
        rows=[]
        for i, disposition in enumerate((1,2,7)):
            rows.append(dict(id=i+1, ts=i+10, pid=41, upid=7, schema_version=1, renderer_run_id=100,
                name='spectr.shared_audio.delivery', stream_epoch=100, block_sequence=i,
                ingress_admitted=1, disposition=disposition, callback_fence_reason=0,worker_fence_reason=0))
        rows.append(dict(id=4,ts=20,pid=41,upid=7,schema_version=1,renderer_run_id=100,name='spectr.shared_audio.final',
            first_epoch=100,last_epoch=102,input_quanta=3,ingress_admitted_quanta=3,terminal_attempts=3,
            terminal_enqueued=3,terminal_popped=3,lost_records=0,counter_overflow=0,gpu_delivered=1,
            cpu_fallback=1,cancelled=1,physical_release_confirmed=1,quantum_frames=32,lead_quanta=4))
        return rows,[dict(name='trace_size_bytes',severity='info',value=100)]
    def check_rows(self,rows,stats,**kw):
        return MODULE.validate(rows,stats,expected_pid=41,expected_run_ids=[100],**kw)
    def rejects(self,change,error):
        rows,stats=self.fixture();change(rows,stats)
        with self.assertRaisesRegex(MODULE.TraceError,error):self.check_rows(rows,stats)
    def test_actual_sql_exports_missing_values_without_zero_substitution(self):
        events,stats=self.fixture()
        db=sqlite3.connect(':memory:');db.row_factory=sqlite3.Row
        db.executescript("CREATE TABLE slice(id,ts,name,track_id,arg_set_id,category);"
            "CREATE TABLE thread_track(id,utid);CREATE TABLE thread(utid,upid);"
            "CREATE TABLE process(upid,pid);CREATE TABLE stats(name,severity,value);"
            "INSERT INTO thread_track VALUES(1,1);INSERT INTO thread VALUES(1,7);"
            "INSERT INTO process VALUES(7,41);")
        args={r['id']:{'debug.'+k:v for k,v in r.items()} for r in events}
        db.create_function('EXTRACT_ARG',2,lambda i,k:args[i].get(k))
        for row in events:db.execute('INSERT INTO slice VALUES(?,?,?,?,?,?)',
            (row['id'],row['ts'],row['name'],1,row['id'],'gpu'))
        for row in stats:db.execute('INSERT INTO stats VALUES(?,?,?)',(row['name'],row['severity'],row['value']))
        sql=(Path(__file__).parents[1]/'tools/shared_spectral_trace.sql').read_text().split(';')
        exported=[dict(r) for r in db.execute(sql[0])]
        exported_stats=[dict(r) for r in db.execute(sql[1])]
        self.assertEqual(self.check_rows(exported,exported_stats)['status'],'passed')
        for field,value in [('block_sequence','bad'),('block_sequence',0.5),
                            ('callback_fence_reason','bad'),('disposition',1.5)]:
            old=args[1]['debug.'+field];args[1]['debug.'+field]=value
            exported=[dict(r) for r in db.execute(sql[0])]
            with self.assertRaisesRegex(MODULE.TraceError,'invalid_field:'+field):
                self.check_rows(exported,exported_stats)
            args[1]['debug.'+field]=old
        del args[4]['debug.physical_release_confirmed']
        exported=[dict(r) for r in db.execute(sql[0])]
        with self.assertRaisesRegex(MODULE.TraceError,'invalid_field:physical_release_confirmed'):
            self.check_rows(exported,exported_stats)
    def test_positive_control(self):
        rows,stats=self.fixture();self.assertEqual(self.check_rows(rows,stats)['status'],'passed')
    def test_zero_based_perfetto_ids_are_valid(self):
        rows,stats=self.fixture()
        for row in rows:row['id']-=1;row['upid']=0
        self.assertEqual(self.check_rows(rows,stats)['status'],'passed')
    def test_missing_first_middle_last(self):
        for i in range(3):
            with self.subTest(i=i):self.rejects(lambda r,s:r.pop(i),'delivery_count')
    def test_missing_final(self):self.rejects(lambda r,s:r.pop(),'final_count')
    def test_duplicate_final(self):
        def change(r,s):r.append(dict(r[-1],id=5))
        self.rejects(change,'final_count')
    def test_duplicate_delivery(self):
        def change(r,s):r[1]=dict(r[0],id=2)
        self.rejects(change,'duplicate_delivery')
    def test_run_and_pid(self):
        self.rejects(lambda r,s:r[0].update(renderer_run_id=101),'missing_or_unexpected_run')
        self.rejects(lambda r,s:r[0].update(pid=42),'unexpected_process')
        self.rejects(lambda r,s:r[0].update(upid=8),'missing_or_unexpected_run')
    def test_loss_and_release(self):
        self.rejects(lambda r,s:r[-1].update(lost_records=1),'terminal_ring_loss')
        self.rejects(lambda r,s:r[-1].update(counter_overflow=1),'counter_overflow')
        self.rejects(lambda r,s:r[-1].update(physical_release_confirmed=0),'release_unconfirmed')
    def test_independent_source_counters(self):
        self.rejects(lambda r,s:r[-1].update(input_quanta=4),'source_terminal_conservation')
        self.rejects(lambda r,s:r[0].update(ingress_admitted=0),'ingress_admission_count')
        self.rejects(lambda r,s:r[-1].update(ingress_admitted_quanta=2),'ingress_admission_count')
    def test_disposition_totals(self):self.rejects(lambda r,s:r[-1].update(gpu_delivered=2),'disposition_totals')
    def test_unknown_or_missing_fields(self):
        self.rejects(lambda r,s:r[0].update(disposition=99),'unknown_disposition')
        self.rejects(lambda r,s:r[0].pop('ingress_admitted'),'invalid_field:ingress_admitted')
        self.rejects(lambda r,s:r[-1].pop('lost_records'),'invalid_field:lost_records')
        self.rejects(lambda r,s:r[0].update(callback_fence_reason=12),'unknown_fence_reason')
        self.rejects(lambda r,s:r[-1].update(counter_overflow=None),'invalid_field:counter_overflow')
    def test_epoch_and_sequence(self):
        self.rejects(lambda r,s:r[0].update(stream_epoch=99),'delivery_epoch_range')
        self.rejects(lambda r,s:r[2].update(block_sequence=3),'sequence_gap')
    def test_delivery_after_final(self):self.rejects(lambda r,s:r[0].update(ts=30),'delivery_after_final')
    def test_processor_loss_is_independent(self):
        self.rejects(lambda r,s:s.append(dict(name='buffer_loss',severity='data_loss',value=1)),'processor_data_loss')
        self.rejects(lambda r,s:s.clear(),'processor_stats_required')
    def test_cpu_only_complete_is_not_positive_gpu_use(self):
        rows,stats=self.fixture();rows[0]['disposition']=2;rows[-1].update(gpu_delivered=0,cpu_fallback=2)
        with self.assertRaisesRegex(MODULE.TraceError,'no_gpu_delivery'):self.check_rows(rows,stats)
        self.assertEqual(self.check_rows(rows,stats,require_gpu=False)['status'],'passed')
    def test_missing_whole_run_and_empty_capture(self):
        rows,stats=self.fixture()
        with self.assertRaisesRegex(MODULE.TraceError,'missing_or_unexpected_run'):
            MODULE.validate(rows,stats,expected_pid=41,expected_run_ids=[100,200])
        with self.assertRaisesRegex(MODULE.TraceError,'missing_or_unexpected_run'):self.check_rows([],stats)
    def test_resets_and_empty_epochs(self):
        rows,stats=self.fixture();rows[2].update(stream_epoch=102,block_sequence=0)
        self.assertEqual(self.check_rows(rows,stats)['status'],'passed')
if __name__=='__main__':unittest.main(verbosity=2)
