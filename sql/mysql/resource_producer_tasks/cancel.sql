UPDATE resource_producer_tasks SET cancel_requested=1,state=IF(state IN ('pending','retry','waiting_input'),'cancelled',state),result_code=IF(state='cancelled','cancelled',result_code),updated_at=UNIX_TIMESTAMP()
WHERE parent_operation_id=? AND destination=? AND state IN ('pending','retry','waiting_input','running')
