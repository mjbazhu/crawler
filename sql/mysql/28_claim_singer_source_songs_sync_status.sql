UPDATE singer_source_refs
SET
    songs_sync_status = 1,
    songs_sync_error = NULL,
    updated_at = UNIX_TIMESTAMP()
WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (
      songs_sync_status IN (?, ?, ?, ?, ?)
      OR (? = 1 AND songs_sync_status = 1 AND updated_at <= UNIX_TIMESTAMP() - ?)
  )
