SELECT batch_id
FROM crawl_batches
WHERE source = ?
  AND state = 'Running'
  AND updated_at <= UNIX_TIMESTAMP() - ?
ORDER BY updated_at ASC, id ASC
