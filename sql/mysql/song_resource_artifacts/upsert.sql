INSERT INTO song_resource_artifacts
    (source_resource_id, artifact_type, source_checksum, processor_version, schema_version,
     local_path, size_bytes, checksum, status, result_code, created_at, updated_at)
VALUES (?, ?, ?, ?, ?, NULLIF(?, ''), ?, ?, ?, ?, UNIX_TIMESTAMP(), UNIX_TIMESTAMP())
ON DUPLICATE KEY UPDATE
    local_path = IF(VALUES(status) = 3 AND status = 2 AND source_checksum = VALUES(source_checksum) AND processor_version = VALUES(processor_version), local_path, VALUES(local_path)),
    size_bytes = IF(VALUES(status) = 3 AND status = 2 AND source_checksum = VALUES(source_checksum) AND processor_version = VALUES(processor_version), size_bytes, VALUES(size_bytes)),
    checksum = IF(VALUES(status) = 3 AND status = 2 AND source_checksum = VALUES(source_checksum) AND processor_version = VALUES(processor_version), checksum, VALUES(checksum)),
    cloud_path = IF(source_checksum = VALUES(source_checksum) AND processor_version = VALUES(processor_version), cloud_path, NULL),
    cloud_backend = IF(source_checksum = VALUES(source_checksum) AND processor_version = VALUES(processor_version), cloud_backend, ''),
    result_code = IF(VALUES(status) = 3 AND status = 2 AND source_checksum = VALUES(source_checksum) AND processor_version = VALUES(processor_version), result_code, VALUES(result_code)),
    status = IF(VALUES(status) = 3 AND status = 2 AND source_checksum = VALUES(source_checksum) AND processor_version = VALUES(processor_version), status, VALUES(status)),
    source_checksum = VALUES(source_checksum),
    processor_version = VALUES(processor_version),
    schema_version = VALUES(schema_version),
    updated_at = UNIX_TIMESTAMP()
