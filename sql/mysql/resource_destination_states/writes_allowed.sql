SELECT COUNT(*)=3
   AND COALESCE(SUM(dispatch_state='maintenance'),0)=0
   AND COALESCE(SUM(dispatch_state='active'),0)>0
   AND COALESCE(SUM(dispatch_state='active' AND (baseline_ready<>1 OR target_fingerprint='' OR target_revision=0)),0)=0 AS allowed
FROM resource_destination_states
