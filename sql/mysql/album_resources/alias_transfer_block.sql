INSERT INTO resource_baseline_blocks(owner_type,resource_id,content_generation,baseline_id,reason)
SELECT 'album',canonical.id,canonical.content_generation,COALESCE(blocked.baseline_id,CONCAT('alias:',old.id,':',canonical.id)),COALESCE(blocked.reason,'alias_identity_unverified')
FROM album_resources old JOIN album_resources canonical ON canonical.album_source_ref_id=? AND canonical.resource_kind=old.resource_kind
LEFT JOIN resource_baseline_blocks blocked ON blocked.owner_type='album' AND blocked.resource_id=old.id AND blocked.content_generation=old.content_generation
WHERE old.album_source_ref_id=? AND (blocked.resource_id IS NOT NULL OR
 ((old.local_skip_generation>0 OR EXISTS(SELECT 1 FROM resource_cloud_receipts c WHERE c.owner_type='album' AND c.resource_id=old.id))
 AND NOT (LENGTH(canonical.reference_sha256)=64 AND canonical.reference_sha256=old.reference_sha256 AND canonical.reference_size_bytes=old.reference_size_bytes)))
ON DUPLICATE KEY UPDATE baseline_id=resource_baseline_blocks.baseline_id
