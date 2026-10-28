SELECT
    id,
    name,
    normalized_name,
    created_at,
    updated_at
FROM singers
WHERE id = ?
