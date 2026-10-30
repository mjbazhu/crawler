SELECT destination,target_revision,target_fingerprint,dispatch_state,baseline_ready FROM resource_destination_states ORDER BY FIELD(destination,'local','baidu','gdrive') FOR UPDATE
