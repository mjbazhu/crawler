UPDATE album_resources canonical JOIN album_resources old ON old.album_source_ref_id=? AND old.resource_kind=canonical.resource_kind
SET canonical.local_skip_generation=canonical.content_generation,canonical.local_skip_target_revision=old.local_skip_target_revision,
 canonical.local_skip_baseline=old.local_skip_baseline,canonical.local_skip_reason=old.local_skip_reason
WHERE canonical.album_source_ref_id=? AND canonical.local_skip_generation=0 AND old.local_skip_generation=old.content_generation
 AND old.local_skip_baseline<>'' AND old.local_skip_reason<>'' AND LENGTH(canonical.reference_sha256)=64
 AND canonical.reference_sha256=old.reference_sha256 AND canonical.reference_size_bytes=old.reference_size_bytes
