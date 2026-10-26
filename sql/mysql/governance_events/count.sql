SELECT COUNT(*)
FROM governance_events
WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (? = '' OR target_type = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = '' OR target_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = '' OR action = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = '' OR batch_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
