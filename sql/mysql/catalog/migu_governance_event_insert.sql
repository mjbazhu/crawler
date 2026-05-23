INSERT INTO governance_events (
    source,
    action,
    target_type,
    target_id,
    batch_id,
    before_status,
    after_status,
    result_code,
    error_category,
    reason,
    payload,
    created_at
) VALUES (
    CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
    NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
    NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
    NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
    NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
    NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
    CONVERT(? USING utf8mb4),
    ?
)
