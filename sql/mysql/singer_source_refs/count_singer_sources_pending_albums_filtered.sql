SELECT
    COUNT(*) AS matched_count,
    COUNT(CASE
        WHEN (
            (summary.sync_status <> 1 AND summary.sync_status IN (?, ?, ?, ?, ?))
            OR (? = 1 AND summary.sync_status = 1 AND summary.updated_at <= UNIX_TIMESTAMP() - ?)
        ) THEN 1
    END) AS eligible_count
FROM (
    SELECT
        r.source_singer_id,
        s.name,
        COALESCE(r.source_country, '') AS source_country,
        r.source_gender AS gender,
        CASE
            WHEN r.source_album_total IS NOT NULL AND (
                SELECT COUNT(DISTINCT al.id)
                FROM album_source_refs al
                WHERE al.source = r.source
                  AND EXISTS (SELECT 1 FROM album_artist_refs aa WHERE aa.album_source_ref_id=al.id AND aa.singer_source_ref_id=r.id)
            ) >= r.source_album_total AND COALESCE(r.albums_sync_error, '') = '' AND r.albums_sync_status <> 1 THEN 2
            WHEN r.source_album_total IS NOT NULL AND (
                SELECT COUNT(DISTINCT al.id)
                FROM album_source_refs al
                WHERE al.source = r.source
                  AND EXISTS (SELECT 1 FROM album_artist_refs aa WHERE aa.album_source_ref_id=al.id AND aa.singer_source_ref_id=r.id)
            ) < r.source_album_total AND COALESCE(r.albums_sync_error, '') = '' AND r.albums_sync_status <> 1 THEN 4
            ELSE r.albums_sync_status
        END AS sync_status,
        r.updated_at
    FROM singer_source_refs r
    JOIN singers s ON s.id = r.singer_db_id
    WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
) summary
WHERE (? = '' OR summary.source_country = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = -1 OR summary.gender = ?)
  AND (
      ? = ''
      OR summary.name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%')
      OR summary.source_singer_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%')
  )
