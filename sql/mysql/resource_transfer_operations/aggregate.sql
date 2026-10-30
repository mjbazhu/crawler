WITH scope AS (SELECT ? AS id), units AS (
 SELECT IF(t.state='waiting_input','failed',t.state) AS state,t.cancel_requested FROM resource_transfer_tasks t JOIN scope s ON t.operation_id=s.id
 UNION ALL
 SELECT p.state,p.cancel_requested FROM resource_producer_tasks p JOIN scope s ON p.parent_operation_id=s.id
 UNION ALL
 SELECT CASE c.state WHEN 'completed' THEN 'succeeded' WHEN 'partial' THEN 'failed' WHEN 'cancelling' THEN 'running' ELSE c.state END,
        c.state='cancelling'
 FROM resource_transfer_operations c JOIN scope s ON c.parent_operation_id=s.id
), totals AS (
 SELECT COUNT(*) AS total,SUM(state IN ('pending','retry','running','waiting_input')) AS active,
 SUM(state IN ('pending','retry','running','waiting_input') AND cancel_requested=1) AS cancelling,
 SUM(state='cancelled') AS cancelled,
 SUM(state NOT IN ('succeeded','skipped','cancelled','pending','retry','running','waiting_input')) AS errors FROM units
)
UPDATE resource_transfer_operations o JOIN scope s ON o.operation_id=s.id JOIN totals t
SET o.state=CASE WHEN t.active>0 THEN IF(t.cancelling>0,'cancelling','running') WHEN t.cancelled=t.total THEN 'cancelled' WHEN t.errors>0 OR t.cancelled>0 THEN 'partial' ELSE 'completed' END,o.updated_at=UNIX_TIMESTAMP()
