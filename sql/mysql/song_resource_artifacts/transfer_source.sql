SELECT COALESCE(r.local_path,''),COALESCE(NULLIF(r.reference_sha256,''),r.checksum),
       IF(r.reference_sha256='',r.size_bytes,r.reference_size_bytes),r.artifact_type,
       r.source_resource_id,r.source_checksum,r.processor_version,r.schema_version
FROM song_resource_artifacts r WHERE r.id=?
