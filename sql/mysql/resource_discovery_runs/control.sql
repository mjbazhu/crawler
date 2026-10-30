UPDATE resource_discovery_runs SET state=?,claim_generation=claim_generation+1,lease_until=0,result_code=?,updated_at=UNIX_TIMESTAMP() WHERE run_id=?
