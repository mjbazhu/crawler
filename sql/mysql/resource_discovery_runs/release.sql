UPDATE resource_discovery_runs SET state='paused',lease_until=0,claim_generation=claim_generation+1,result_code=?,updated_at=UNIX_TIMESTAMP() WHERE run_id=? AND state='running' AND claim_generation=?
