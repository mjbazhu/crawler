SELECT task_type, COUNT(*)
FROM crawl_batches
WHERE source = ?
  AND archived_at = 0
  AND state = 'Failed'
GROUP BY task_type
ORDER BY COUNT(*) DESC, task_type ASC
LIMIT 10
