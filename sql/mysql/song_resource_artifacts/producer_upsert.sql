INSERT INTO song_resource_artifacts(source_resource_id,artifact_type,source_checksum,processor_version,schema_version,created_at,updated_at)
VALUES(?,?,?,?,?,UNIX_TIMESTAMP(),UNIX_TIMESTAMP())
ON DUPLICATE KEY UPDATE source_checksum=VALUES(source_checksum),processor_version=VALUES(processor_version),schema_version=VALUES(schema_version),updated_at=UNIX_TIMESTAMP()
