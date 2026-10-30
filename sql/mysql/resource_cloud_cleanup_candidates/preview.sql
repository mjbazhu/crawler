WITH page AS (
 SELECT * FROM resource_cloud_cleanup_candidates WHERE id>? AND (?='' OR cloud_provider=?) ORDER BY id LIMIT ?
)
SELECT c.id,c.receipt_id,c.owner_type,c.resource_id,c.content_generation,c.cloud_provider,c.target_revision,c.target_fingerprint,c.confirmed_content_generation,c.confirmed_target_revision,c.cloud_path,c.path_hash,c.object_key,c.size_bytes,c.checksum,c.verification,c.receipt_state,c.captured_at,
 d.target_revision,d.target_fingerprint,
 (s.id IS NOT NULL OR si.id IS NOT NULL OR a.id IS NOT NULL OR ar.id IS NOT NULL),
 (SELECT COUNT(*) FROM resource_destination_states WHERE dispatch_state='maintenance')=3,
 EXISTS(SELECT 1 FROM resource_cloud_receipts r WHERE r.cloud_provider=c.cloud_provider AND (BINARY r.cloud_path=BINARY c.cloud_path OR (c.object_key<>'' AND BINARY RIGHT(r.cloud_path,CHAR_LENGTH(c.object_key))=BINARY c.object_key))),
 (EXISTS(SELECT 1 FROM resource_transfer_slots sl WHERE sl.owner_type=c.owner_type AND sl.resource_id=c.resource_id AND sl.destination=c.cloud_provider)
  OR EXISTS(SELECT 1 FROM resource_transfer_tasks t WHERE t.destination=c.cloud_provider AND t.state IN('pending','running','retry','waiting_input','uncertain') AND ((t.owner_type=c.owner_type AND t.resource_id=c.resource_id) OR BINARY t.attempt_path=BINARY c.cloud_path OR (c.object_key<>'' AND BINARY RIGHT(t.attempt_path,CHAR_LENGTH(c.object_key))=BINARY c.object_key)))
  OR EXISTS(SELECT 1 FROM resource_producer_tasks pr JOIN resource_transfer_tasks t ON t.task_id=pr.parent_task_id WHERE pr.destination=c.cloud_provider AND pr.state IN('pending','running','retry','waiting_input') AND t.owner_type=c.owner_type AND t.resource_id=c.resource_id)),
 EXISTS(SELECT 1 FROM resource_cloud_migration_audit au WHERE au.disposition='alias_retired' AND c.object_key<>'' AND LOCATE(c.object_key,CAST(au.old_values AS CHAR))>0)
FROM page c LEFT JOIN resource_destination_states d ON d.destination=c.cloud_provider
LEFT JOIN song_resources s ON c.owner_type='song' AND s.id=c.resource_id
LEFT JOIN singer_resources si ON c.owner_type='singer' AND si.id=c.resource_id
LEFT JOIN album_resources a ON c.owner_type='album' AND a.id=c.resource_id
LEFT JOIN song_resource_artifacts ar ON c.owner_type='song_artifact' AND ar.id=c.resource_id
ORDER BY c.id
