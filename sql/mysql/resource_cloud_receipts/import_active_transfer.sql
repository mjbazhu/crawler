WITH scope AS (SELECT ? AS owner_type,? AS resource_id)
SELECT s.task_id FROM resource_transfer_slots s JOIN scope x ON s.owner_type=x.owner_type AND s.resource_id=x.resource_id
UNION ALL
SELECT p.parent_task_id FROM resource_producer_tasks p JOIN resource_transfer_tasks t ON t.task_id=p.parent_task_id
JOIN scope x ON t.owner_type=x.owner_type AND t.resource_id=x.resource_id
WHERE p.state IN ('pending','retry','running','waiting_input')
