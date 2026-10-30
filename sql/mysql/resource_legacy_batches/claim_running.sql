WITH RECURSIVE scope(operation_id,depth) AS (
 SELECT operation_id,0 FROM resource_legacy_operations WHERE batch_id=?
 UNION ALL
 SELECT c.operation_id,s.depth+1 FROM resource_transfer_operations c JOIN scope s ON c.parent_operation_id=s.operation_id WHERE s.depth<2
), active_claims AS (
 SELECT t.task_id FROM resource_transfer_tasks t JOIN scope s ON t.operation_id=s.operation_id WHERE t.state='running' AND (t.destination='local' OR (?<>'song' AND t.destination=?))
 UNION ALL
 SELECT p.producer_id FROM resource_producer_tasks p JOIN scope s ON p.parent_operation_id=s.operation_id WHERE p.state='running' AND p.destination='local'
)
SELECT COUNT(*) FROM active_claims
