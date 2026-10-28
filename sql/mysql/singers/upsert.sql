INSERT INTO singers
    (name, normalized_name, gender, created_at, updated_at)
VALUES
    (
        CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
        CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
        0,
        ?,
        ?
    )
ON DUPLICATE KEY UPDATE
    name = CASE
        WHEN name = '' THEN VALUES(name)
        ELSE name
    END,
    gender = 0,
    updated_at = VALUES(updated_at)
