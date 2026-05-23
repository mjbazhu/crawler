SELECT
    id,
    source,
    action,
    target_type,
    target_id,
    COALESCE(batch_id, ''),
    COALESCE(before_status, ''),
    COALESCE(after_status, ''),
    COALESCE(result_code, ''),
    COALESCE(error_category, ''),
    COALESCE(reason, ''),
    COALESCE(CAST(payload AS CHAR), '{}'),
    created_at
FROM governance_events
WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (? = '' OR target_type = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = '' OR target_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = '' OR action = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = '' OR batch_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
ORDER BY id DESC
LIMIT ? OFFSET ?
