SELECT s.task_id,t.state FROM resource_transfer_slots s JOIN resource_transfer_tasks t ON t.task_id=s.task_id WHERE s.owner_type=? AND s.resource_id=? AND s.destination=? FOR UPDATE
