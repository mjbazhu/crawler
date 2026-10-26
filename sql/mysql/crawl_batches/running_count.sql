SELECT COUNT(*)
FROM crawl_batches
WHERE source = ?
  AND archived_at = 0
  AND state = 'Running'
