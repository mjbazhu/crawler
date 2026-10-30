SELECT t.task_id FROM resource_transfer_slots s JOIN resource_transfer_tasks t ON t.task_id=s.task_id
WHERE s.owner_type=? AND s.resource_id=? AND s.destination='local' AND t.state IN ('pending','retry','running','uncertain')
  AND (t.attempt_path=? OR (t.state='running' AND t.native_claim_generation=?)) LIMIT 1
