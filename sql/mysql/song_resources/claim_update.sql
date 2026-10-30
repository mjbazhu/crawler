UPDATE song_resources
SET status = 1,
    claim_generation = claim_generation + 1,
    last_error = NULL,
    error_category = NULL,
    result_code = NULL,
    running_batch_id = NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
    last_processed_at = UNIX_TIMESTAMP(),
    updated_at = UNIX_TIMESTAMP()
WHERE id = ?
  AND status IN (?, ?, ?, ?, ?)
  AND claim_generation = ?
  AND NOT EXISTS (SELECT 1 FROM resource_baseline_blocks blocked WHERE blocked.owner_type='song' AND blocked.resource_id=song_resources.id AND blocked.content_generation=song_resources.content_generation)
  AND NOT (local_skip_generation=content_generation AND local_skip_target_revision=(SELECT target_revision FROM resource_destination_states WHERE destination='local') AND local_skip_baseline<>'' AND local_skip_reason<>'')
  AND (?=0 OR (
       NOT EXISTS (SELECT 1 FROM resource_transfer_slots slot WHERE slot.owner_type='song' AND slot.resource_id=song_resources.id AND slot.destination='local') AND
       EXISTS (SELECT 1 FROM resource_destination_states d WHERE d.destination='local' AND d.dispatch_state='active' AND d.baseline_ready=1 AND d.target_fingerprint<>'' AND d.target_revision>0)
       AND (SELECT COUNT(*) FROM resource_destination_states)=3
       AND NOT EXISTS (SELECT 1 FROM resource_destination_states g WHERE g.dispatch_state='maintenance' OR (g.dispatch_state='active' AND (g.baseline_ready<>1 OR g.target_fingerprint='' OR g.target_revision=0)))))
