UPDATE resource_producer_tasks SET claim_generation=claim_generation+1,updated_at=UNIX_TIMESTAMP() WHERE producer_id=? AND claim_generation=?
