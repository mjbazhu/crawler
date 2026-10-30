UPDATE singer_resources SET local_skip_generation=?,local_skip_target_revision=?,
    local_skip_baseline=?,local_skip_reason='legacy_both_clouds_present'
WHERE id=? AND content_generation=? AND status=2;
