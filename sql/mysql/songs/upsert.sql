INSERT INTO songs
    (title, normalized_title, isrc, created_at, updated_at)
VALUES
    (?, ?, NULL, ?, ?)
ON DUPLICATE KEY UPDATE
    title = VALUES(title),
    updated_at = VALUES(updated_at)
