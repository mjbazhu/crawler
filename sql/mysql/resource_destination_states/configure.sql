UPDATE resource_destination_states SET target_revision=?,target_fingerprint=?,baseline_ready=0,
    baseline_manifest='',updated_at=UNIX_TIMESTAMP()
WHERE destination=? AND target_revision=? AND target_fingerprint=? AND dispatch_state='maintenance'
 AND NOT EXISTS(SELECT 1 FROM resource_transfer_slots s WHERE s.destination=resource_destination_states.destination)
 AND NOT EXISTS(SELECT 1 FROM resource_producer_tasks p WHERE p.destination=resource_destination_states.destination AND p.state IN ('pending','retry','running','waiting_input'))
 AND NOT EXISTS(SELECT 1 FROM resource_cloud_receipts c WHERE c.cloud_provider=resource_destination_states.destination AND c.task_state IN ('running','uncertain'));
