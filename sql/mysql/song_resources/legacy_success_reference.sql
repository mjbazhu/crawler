UPDATE song_resources SET reference_sha256=IF(reference_sha256='',?,reference_sha256),
 reference_size_bytes=IF(reference_size_bytes=0,?,reference_size_bytes),
 local_content_generation=content_generation,local_target_revision=?
WHERE id=? AND status=2 AND claim_generation=?;
