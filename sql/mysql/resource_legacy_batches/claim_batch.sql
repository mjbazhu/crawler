WITH RECURSIVE ancestors(operation_id,parent_operation_id,depth) AS (
 SELECT operation_id,parent_operation_id,0 FROM resource_transfer_operations WHERE operation_id=?
 UNION ALL
 SELECT p.operation_id,p.parent_operation_id,a.depth+1 FROM resource_transfer_operations p JOIN ancestors a ON a.parent_operation_id=p.operation_id WHERE a.depth<2
)
SELECT l.batch_id,l.owner_type,l.cloud_provider FROM ancestors a JOIN resource_legacy_operations m ON m.operation_id=a.operation_id JOIN resource_legacy_batches l ON l.batch_id=m.batch_id LIMIT 1
