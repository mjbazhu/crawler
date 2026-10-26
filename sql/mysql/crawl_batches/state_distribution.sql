SELECT state, COUNT(*)
FROM crawl_batches
WHERE source = ?
  AND archived_at = 0
GROUP BY state
ORDER BY state ASC
