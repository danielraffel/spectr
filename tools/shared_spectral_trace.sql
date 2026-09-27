-- Export both result sets without replacing missing annotations with zero.
-- Bind expected native PID and renderer run IDs from the host receipt.
SELECT s.id,s.ts,s.name,t.upid,p.pid,
 EXTRACT_ARG(s.arg_set_id,'debug.schema_version') AS schema_version,
 EXTRACT_ARG(s.arg_set_id,'debug.renderer_run_id') AS renderer_run_id,
 EXTRACT_ARG(s.arg_set_id,'debug.stream_epoch') AS stream_epoch,
 EXTRACT_ARG(s.arg_set_id,'debug.block_sequence') AS block_sequence,
 EXTRACT_ARG(s.arg_set_id,'debug.ingress_admitted') AS ingress_admitted,
 EXTRACT_ARG(s.arg_set_id,'debug.disposition') AS disposition,
 EXTRACT_ARG(s.arg_set_id,'debug.callback_fence_reason') AS callback_fence_reason,
 EXTRACT_ARG(s.arg_set_id,'debug.worker_fence_reason') AS worker_fence_reason,
 EXTRACT_ARG(s.arg_set_id,'debug.first_epoch') AS first_epoch,
 EXTRACT_ARG(s.arg_set_id,'debug.last_epoch') AS last_epoch,
 EXTRACT_ARG(s.arg_set_id,'debug.input_quanta') AS input_quanta,
 EXTRACT_ARG(s.arg_set_id,'debug.ingress_admitted_quanta') AS ingress_admitted_quanta,
 EXTRACT_ARG(s.arg_set_id,'debug.terminal_attempts') AS terminal_attempts,
 EXTRACT_ARG(s.arg_set_id,'debug.terminal_enqueued') AS terminal_enqueued,
 EXTRACT_ARG(s.arg_set_id,'debug.terminal_popped') AS terminal_popped,
 EXTRACT_ARG(s.arg_set_id,'debug.lost_records') AS lost_records,
 EXTRACT_ARG(s.arg_set_id,'debug.counter_overflow') AS counter_overflow,
 EXTRACT_ARG(s.arg_set_id,'debug.gpu_delivered') AS gpu_delivered,
 EXTRACT_ARG(s.arg_set_id,'debug.cpu_fallback') AS cpu_fallback,
 EXTRACT_ARG(s.arg_set_id,'debug.cancelled') AS cancelled,
 EXTRACT_ARG(s.arg_set_id,'debug.physical_release_confirmed') AS physical_release_confirmed,
 EXTRACT_ARG(s.arg_set_id,'debug.quantum_frames') AS quantum_frames,
 EXTRACT_ARG(s.arg_set_id,'debug.lead_quanta') AS lead_quanta
FROM slice s JOIN thread_track tt ON tt.id=s.track_id
JOIN thread t ON t.utid=tt.utid JOIN process p ON p.upid=t.upid
WHERE s.category='gpu' AND s.name IN ('spectr.shared_audio.delivery','spectr.shared_audio.final')
ORDER BY s.ts,s.id;
SELECT name,severity,value FROM stats;
