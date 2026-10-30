UPDATE resource_transfer_tasks SET state='running',claim_generation=claim_generation+1,native_claim_generation=?,claimed_at=UNIX_TIMESTAMP(),attempt_count=attempt_count+1,result_code='',updated_at=UNIX_TIMESTAMP()
WHERE task_id=? AND state IN ('pending','retry') AND cancel_requested=0 AND available_at<=UNIX_TIMESTAMP()
