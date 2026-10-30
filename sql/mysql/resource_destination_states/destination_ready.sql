SELECT dispatch_state='active' AND baseline_ready=1 AND target_fingerprint<>'' AND target_revision>0
FROM resource_destination_states WHERE destination=?
