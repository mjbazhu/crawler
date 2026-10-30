INSERT INTO song_resources (
    song_source_ref_id,
    claim_generation,
    resource_kind,
    format_type,
    local_path,
    size_bytes,
    checksum,
    status,
    retry_count,
    last_error,
    error_category,
    result_code,
    running_batch_id,
    last_processed_at,
    fetched_at,
    created_at,
    updated_at
)
SELECT
    ?,
    1,
    CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    '',
    NULL,
    0,
    NULL,
    1,
    0,
    NULL,
    NULL,
    NULL,
    NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
    UNIX_TIMESTAMP(),
    0,
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
WHERE EXISTS (SELECT 1 FROM song_source_refs source_ref WHERE source_ref.id = ?
    AND NOT EXISTS (SELECT 1 FROM song_resources existing LEFT JOIN resource_transfer_slots slot
                    ON slot.owner_type='song' AND slot.resource_id=existing.id AND slot.destination='local'
                    LEFT JOIN resource_baseline_blocks blocked ON blocked.owner_type='song' AND blocked.resource_id=existing.id AND blocked.content_generation=existing.content_generation
                   WHERE existing.song_source_ref_id=source_ref.id AND existing.resource_kind=CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AND existing.format_type='' AND (slot.task_id IS NOT NULL OR blocked.resource_id IS NOT NULL)))
  AND (?=0 OR (
       EXISTS (SELECT 1 FROM resource_destination_states d WHERE d.destination='local' AND d.dispatch_state='active' AND d.baseline_ready=1 AND d.target_fingerprint<>'' AND d.target_revision>0)
       AND (SELECT COUNT(*) FROM resource_destination_states)=3
       AND NOT EXISTS (SELECT 1 FROM resource_destination_states g WHERE g.dispatch_state='maintenance' OR (g.dispatch_state='active' AND (g.baseline_ready<>1 OR g.target_fingerprint='' OR g.target_revision=0)))))
ON DUPLICATE KEY UPDATE
    claim_generation = IF(status IN (0, 3) AND NOT (local_skip_generation=content_generation AND local_skip_target_revision=(SELECT target_revision FROM resource_destination_states WHERE destination='local') AND local_skip_baseline<>'' AND local_skip_reason<>''), claim_generation + 1, claim_generation),
    last_error = IF(status IN (0, 3) AND NOT (local_skip_generation=content_generation AND local_skip_target_revision=(SELECT target_revision FROM resource_destination_states WHERE destination='local') AND local_skip_baseline<>'' AND local_skip_reason<>''), NULL, last_error),
    error_category = IF(status IN (0, 3) AND NOT (local_skip_generation=content_generation AND local_skip_target_revision=(SELECT target_revision FROM resource_destination_states WHERE destination='local') AND local_skip_baseline<>'' AND local_skip_reason<>''), NULL, error_category),
    result_code = IF(status IN (0, 3) AND NOT (local_skip_generation=content_generation AND local_skip_target_revision=(SELECT target_revision FROM resource_destination_states WHERE destination='local') AND local_skip_baseline<>'' AND local_skip_reason<>''), NULL, result_code),
    running_batch_id = IF(status IN (0, 3) AND NOT (local_skip_generation=content_generation AND local_skip_target_revision=(SELECT target_revision FROM resource_destination_states WHERE destination='local') AND local_skip_baseline<>'' AND local_skip_reason<>''), VALUES(running_batch_id), running_batch_id),
    last_processed_at = IF(status IN (0, 3) AND NOT (local_skip_generation=content_generation AND local_skip_target_revision=(SELECT target_revision FROM resource_destination_states WHERE destination='local') AND local_skip_baseline<>'' AND local_skip_reason<>''), VALUES(last_processed_at), last_processed_at),
    updated_at = IF(status IN (0, 3) AND NOT (local_skip_generation=content_generation AND local_skip_target_revision=(SELECT target_revision FROM resource_destination_states WHERE destination='local') AND local_skip_baseline<>'' AND local_skip_reason<>''), VALUES(updated_at), updated_at),
    status = IF(status IN (0, 3) AND NOT (local_skip_generation=content_generation AND local_skip_target_revision=(SELECT target_revision FROM resource_destination_states WHERE destination='local') AND local_skip_baseline<>'' AND local_skip_reason<>''), VALUES(status), status)
