UPDATE resource_producer_tasks SET state=?,result_code=?,error_category=?,child_operation_id=?,available_at=?,updated_at=UNIX_TIMESTAMP()
WHERE producer_id=? AND claim_generation=? AND state IN ('pending','retry','waiting_input','running')
