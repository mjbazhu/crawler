SELECT r.content_generation,d.target_revision,r.source_revision FROM singer_resources r JOIN resource_destination_states d ON d.destination=? WHERE r.id=? FOR UPDATE;
