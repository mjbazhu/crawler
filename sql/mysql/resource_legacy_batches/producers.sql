WITH RECURSIVE scope(operation_id,depth) AS (
 SELECT operation_id,0 FROM resource_legacy_operations WHERE batch_id=?
 UNION ALL
 SELECT c.operation_id,s.depth+1 FROM resource_transfer_operations c JOIN scope s ON c.parent_operation_id=s.operation_id WHERE s.depth<1
)
SELECT p.state,p.result_code FROM resource_producer_tasks p JOIN scope s ON s.operation_id=p.parent_operation_id WHERE p.destination='local'
