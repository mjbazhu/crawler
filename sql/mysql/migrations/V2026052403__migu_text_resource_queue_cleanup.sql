SET @crawler_now := UNIX_TIMESTAMP();

DELETE sr
FROM song_resources sr
JOIN song_source_refs r ON r.id = sr.song_source_ref_id
WHERE r.source = 'migu'
  AND sr.resource_kind IN ('cover', 'lyric', 'mrc', 'trc')
  AND CASE sr.resource_kind
      WHEN 'cover' THEN COALESCE(r.cover_url, '')
      WHEN 'lyric' THEN COALESCE(r.lyric_url, '')
      WHEN 'mrc' THEN COALESCE(r.mrc_url, '')
      WHEN 'trc' THEN COALESCE(r.trc_url, '')
      ELSE ''
  END = ''
  AND COALESCE(sr.local_path, '') = ''
  AND sr.size_bytes = 0
  AND sr.status IN (0, 3, 4);

INSERT INTO song_resources (
    song_source_ref_id,
    resource_kind,
    format_type,
    remote_url,
    local_path,
    size_bytes,
    checksum,
    status,
    retry_count,
    last_error,
    fetched_at,
    created_at,
    updated_at
)
SELECT r.id, 'cover', '', r.cover_url, NULL, 0, NULL, 0, 0, NULL, 0, @crawler_now, @crawler_now
FROM song_source_refs r
WHERE r.source = 'migu'
  AND COALESCE(r.cover_url, '') <> ''
  AND NOT EXISTS (SELECT 1 FROM song_resources sr WHERE sr.song_source_ref_id = r.id AND sr.resource_kind = 'cover' AND sr.format_type = '')
UNION ALL
SELECT r.id, 'lyric', '', r.lyric_url, NULL, 0, NULL, 0, 0, NULL, 0, @crawler_now, @crawler_now
FROM song_source_refs r
WHERE r.source = 'migu'
  AND COALESCE(r.lyric_url, '') <> ''
  AND NOT EXISTS (SELECT 1 FROM song_resources sr WHERE sr.song_source_ref_id = r.id AND sr.resource_kind = 'lyric' AND sr.format_type = '')
UNION ALL
SELECT r.id, 'mrc', '', r.mrc_url, NULL, 0, NULL, 0, 0, NULL, 0, @crawler_now, @crawler_now
FROM song_source_refs r
WHERE r.source = 'migu'
  AND COALESCE(r.mrc_url, '') <> ''
  AND NOT EXISTS (SELECT 1 FROM song_resources sr WHERE sr.song_source_ref_id = r.id AND sr.resource_kind = 'mrc' AND sr.format_type = '')
UNION ALL
SELECT r.id, 'trc', '', r.trc_url, NULL, 0, NULL, 0, 0, NULL, 0, @crawler_now, @crawler_now
FROM song_source_refs r
WHERE r.source = 'migu'
  AND COALESCE(r.trc_url, '') <> ''
  AND NOT EXISTS (SELECT 1 FROM song_resources sr WHERE sr.song_source_ref_id = r.id AND sr.resource_kind = 'trc' AND sr.format_type = '');

UPDATE song_resources sr
JOIN song_source_refs r ON r.id = sr.song_source_ref_id
SET sr.status = 0,
    sr.retry_count = 0,
    sr.last_error = NULL,
    sr.error_category = NULL,
    sr.result_code = NULL,
    sr.running_batch_id = NULL,
    sr.last_processed_at = 0,
    sr.updated_at = @crawler_now
WHERE r.source = 'migu'
  AND sr.resource_kind IN ('cover', 'lyric', 'mrc', 'trc')
  AND COALESCE(sr.remote_url, '') <> ''
  AND sr.status = 4
  AND sr.result_code IN ('440015', '440016', 'digital_album');

INSERT INTO crawler_schema_versions (
    component,
    schema_version,
    schema_name,
    applied_at,
    updated_at
)
VALUES (
    'crawler_mysql',
    2026052403,
    'migu text resource queue cleanup',
    @crawler_now,
    @crawler_now
)
ON DUPLICATE KEY UPDATE
    schema_version = GREATEST(schema_version, VALUES(schema_version)),
    schema_name = VALUES(schema_name),
    updated_at = VALUES(updated_at);
