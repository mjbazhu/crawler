SET @crawler_migu_resource_prefix := 'https://d.musicapp.migu.cn';
SET @crawler_now := UNIX_TIMESTAMP();

UPDATE song_source_refs
SET cover_url = CASE
        WHEN cover_url LIKE '/data/oss/%' THEN CONCAT(@crawler_migu_resource_prefix, cover_url)
        WHEN cover_url LIKE 'data/oss/%' THEN CONCAT(@crawler_migu_resource_prefix, '/', cover_url)
        ELSE cover_url
    END,
    updated_at = @crawler_now
WHERE source = 'migu'
  AND (
      cover_url LIKE '/data/oss/%'
      OR cover_url LIKE 'data/oss/%'
  );

UPDATE song_resources sr
JOIN song_source_refs r ON r.id = sr.song_source_ref_id
SET sr.remote_url = CASE
        WHEN COALESCE(sr.remote_url, '') LIKE '/data/oss/%' THEN CONCAT(@crawler_migu_resource_prefix, sr.remote_url)
        WHEN COALESCE(sr.remote_url, '') LIKE 'data/oss/%' THEN CONCAT(@crawler_migu_resource_prefix, '/', sr.remote_url)
        WHEN COALESCE(sr.remote_url, '') = '' THEN r.cover_url
        ELSE sr.remote_url
    END,
    sr.updated_at = @crawler_now
WHERE r.source = 'migu'
  AND sr.resource_kind = 'cover'
  AND sr.format_type = ''
  AND COALESCE(r.cover_url, '') <> ''
  AND (
      COALESCE(sr.remote_url, '') LIKE '/data/oss/%'
      OR COALESCE(sr.remote_url, '') LIKE 'data/oss/%'
      OR COALESCE(sr.remote_url, '') = ''
  );

INSERT INTO crawler_schema_versions (
    component,
    schema_version,
    schema_name,
    applied_at,
    updated_at
)
VALUES (
    'crawler_mysql',
    2026052402,
    'migu song cover url prefix',
    @crawler_now,
    @crawler_now
)
ON DUPLICATE KEY UPDATE
    schema_version = GREATEST(schema_version, VALUES(schema_version)),
    schema_name = VALUES(schema_name),
    updated_at = VALUES(updated_at);
