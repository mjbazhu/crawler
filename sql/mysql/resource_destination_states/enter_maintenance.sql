UPDATE resource_destination_states SET dispatch_state='maintenance',baseline_ready=0,baseline_manifest='',updated_at=UNIX_TIMESTAMP() WHERE destination IN('local','baidu','gdrive')
