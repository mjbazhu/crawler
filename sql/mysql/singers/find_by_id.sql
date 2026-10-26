SELECT
    id,
    name,
    normalized_name,
    gender,
    created_at,
    updated_at
FROM singers
WHERE id = ?
