SELECT COUNT(*) FROM (
    SELECT producer_id FROM resource_producer_tasks FORCE INDEX(idx_producer_dispatch)
    WHERE destination=? AND state IN('pending','retry','running','waiting_input')
    LIMIT ? FOR UPDATE
) AS queued
