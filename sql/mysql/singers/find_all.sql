SELECT
    id,
    name,
    normalized_name,
    created_at,
    updated_at
FROM singers
ORDER BY id
LIMIT ? OFFSET ?
