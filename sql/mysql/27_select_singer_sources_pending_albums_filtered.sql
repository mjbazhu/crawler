SELECT
    summary.id,
    summary.singer_db_id,
    summary.source,
    summary.source_item_key,
    summary.source_singer_id,
    summary.name,
    summary.source_country,
    summary.gender,
    summary.avatar_url,
    summary.sync_status,
    summary.synced_at,
    summary.sync_count,
    summary.sync_error,
    summary.updated_at
FROM (
    SELECT
        r.id,
        r.singer_db_id,
        r.source,
        r.source_item_key,
        r.source_singer_id,
        s.name,
        COALESCE(r.source_country, '') AS source_country,
        s.gender,
        COALESCE(r.avatar_url, '') AS avatar_url,
        CASE
            WHEN EXISTS (
                SELECT 1
                FROM album_source_refs al
                WHERE al.source = r.source
                  AND al.source_artist_id = r.source_singer_id
                LIMIT 1
            ) AND COALESCE(r.albums_sync_error, '') = '' AND r.albums_sync_status <> 1 THEN 2
            ELSE r.albums_sync_status
        END AS sync_status,
        CASE
            WHEN EXISTS (
                SELECT 1
                FROM album_source_refs al
                WHERE al.source = r.source
                  AND al.source_artist_id = r.source_singer_id
                LIMIT 1
            ) AND COALESCE(r.albums_sync_error, '') = '' AND r.albums_synced_at = 0 THEN r.updated_at
            ELSE r.albums_synced_at
        END AS synced_at,
        (
            SELECT COUNT(DISTINCT al.id)
            FROM album_source_refs al
            WHERE al.source = r.source
              AND al.source_artist_id = r.source_singer_id
        ) AS sync_count,
        COALESCE(r.albums_sync_error, '') AS sync_error,
        r.updated_at
    FROM singer_source_refs r
    JOIN singers s ON s.id = r.singer_db_id
    WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
) summary
WHERE (
      summary.sync_status IN (?, ?, ?, ?, ?)
      OR (? = 1 AND summary.sync_status = 1 AND summary.updated_at <= UNIX_TIMESTAMP() - ?)
  )
  AND (? = '' OR summary.source_country = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = -1 OR summary.gender = ?)
  AND (
      ? = ''
      OR summary.name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%')
      OR summary.source_singer_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%')
  )
ORDER BY summary.updated_at ASC, summary.id ASC
LIMIT ? OFFSET ?
