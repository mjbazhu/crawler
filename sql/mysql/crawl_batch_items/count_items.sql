SELECT COUNT(*)
FROM crawl_batch_items
WHERE batch_id = ?
  AND (? = '' OR state = ?)
  AND (? = '' OR error_category = ?)
  AND (? = '' OR result_code = ?)
