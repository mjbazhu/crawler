UPDATE album_resources canonical JOIN album_resources old ON old.album_source_ref_id=? AND old.resource_kind=canonical.resource_kind
SET canonical.reference_sha256=old.reference_sha256,canonical.reference_size_bytes=old.reference_size_bytes,
canonical.local_content_generation=canonical.content_generation,canonical.local_target_revision=old.local_target_revision
WHERE canonical.album_source_ref_id=? AND canonical.reference_sha256='' AND LENGTH(old.reference_sha256)=64
 AND canonical.local_path=old.local_path AND canonical.checksum=old.reference_sha256 AND canonical.size_bytes=old.reference_size_bytes
 AND old.local_content_generation=old.content_generation
