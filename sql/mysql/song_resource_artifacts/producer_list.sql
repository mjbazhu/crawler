SELECT a.id,a.artifact_type,a.source_checksum,a.processor_version,a.schema_version,a.reference_sha256,a.reference_size_bytes,a.status,
 EXISTS(SELECT 1 FROM resource_transfer_slots s WHERE s.owner_type='song_artifact' AND s.resource_id=a.id),
 EXISTS(SELECT 1 FROM resource_cloud_receipts c WHERE c.owner_type='song_artifact' AND c.resource_id=a.id AND c.task_state IN ('running','uncertain'))
FROM song_resource_artifacts a WHERE a.source_resource_id=? ORDER BY a.id FOR UPDATE
