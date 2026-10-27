UPDATE albums canonical
JOIN albums alias ON alias.id = ?
SET canonical.release_date = COALESCE(NULLIF(canonical.release_date, ''), alias.release_date),
    canonical.updated_at = UNIX_TIMESTAMP()
WHERE canonical.id = ?;
