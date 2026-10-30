UPDATE resource_producer_tasks SET state='pending',claim_generation=claim_generation+1,attempt_count=0,cancel_requested=0,result_code='',error_category='',available_at=UNIX_TIMESTAMP(),updated_at=UNIX_TIMESTAMP()
WHERE producer_id=? AND claim_generation=? AND state IN ('failed','blocked','waiting_input') AND child_operation_id=''
