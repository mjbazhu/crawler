UPDATE crawl_batches SET state=?,target_count=?,processed_count=?,success_count=?,failure_count=?,
 cancel_requested=IF(?='Cancelled',1,cancel_requested),finished_at=IF(?=1,UNIX_TIMESTAMP(),0),updated_at=UNIX_TIMESTAMP()
WHERE batch_id=? AND state IN ('Pending','Running')
