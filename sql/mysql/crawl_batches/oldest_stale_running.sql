SELECT COALESCE(MIN(updated_at), 0)
FROM crawl_batches
WHERE source = ?
  AND archived_at = 0
  AND state = 'Running'
  AND updated_at <= UNIX_TIMESTAMP() - ?
