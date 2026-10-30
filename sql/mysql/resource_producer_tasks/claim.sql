UPDATE resource_producer_tasks SET state='running',claim_generation=claim_generation+1,claimed_at=UNIX_TIMESTAMP(),attempt_count=attempt_count+1,result_code='',error_category='',updated_at=UNIX_TIMESTAMP()
WHERE producer_id=? AND claim_generation=? AND state IN ('pending','retry','waiting_input') AND cancel_requested=0 AND available_at<=UNIX_TIMESTAMP()
