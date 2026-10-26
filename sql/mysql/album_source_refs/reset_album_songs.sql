UPDATE album_source_refs
SET songs_sync_status = 0,
    songs_synced_at = 0,
    songs_sync_count = 0,
    songs_sync_error = NULL,
    songs_claim_token = '',
    songs_claimed_at = 0,
    songs_claim_owner = '',
    updated_at = UNIX_TIMESTAMP()
WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (? = '' OR source_album_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND songs_sync_status IN (?, ?, ?, ?, ?)
  AND (? = '' OR EXISTS (SELECT 1 FROM albums title_row WHERE title_row.id=album_source_refs.album_db_id AND title_row.title LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%')) OR source_album_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
  AND (? = '' OR EXISTS (SELECT 1 FROM album_artist_refs aa JOIN singer_source_refs sr ON sr.id=aa.singer_source_ref_id JOIN singers sn ON sn.id=sr.singer_db_id WHERE aa.album_source_ref_id=album_source_refs.id AND (sn.name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR sr.source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)))
  AND (? = -1 OR source_album_kind = ?)
ORDER BY updated_at ASC, id ASC
LIMIT ?
