SELECT
    id,
    name,
    normalized_name,
    gender,
    created_at,
    updated_at
FROM singers
ORDER BY id
LIMIT ? OFFSET ?
