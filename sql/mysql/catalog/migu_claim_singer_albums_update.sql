UPDATE singer_source_refs
SET albums_sync_status = 1,
    albums_sync_error = NULL,
    albums_claim_token = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    albums_claimed_at = UNIX_TIMESTAMP(),
    albums_claim_owner = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    updated_at = UNIX_TIMESTAMP()
WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (? = '' OR source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND albums_sync_status IN (?, ?, ?, ?, ?)
  AND (? = '' OR singer_db_id IN (SELECT s.id FROM singers s WHERE s.name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%')) OR source_singer_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
  AND (? = '' OR source_country = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = -1 OR singer_db_id IN (SELECT s.id FROM singers s WHERE s.gender = ?))
ORDER BY updated_at ASC, id ASC
LIMIT ?
