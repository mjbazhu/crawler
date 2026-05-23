SET @album_normalized_title_value := COALESCE(NULLIF(@normalized_title, ''), @title);
SET @album_artist_name_value := COALESCE(NULLIF(@artist_name, ''), NULLIF(@source_artist_name, ''), '');
SET @album_release_date_value := COALESCE(NULLIF(@release_date, ''), NULLIF(@publish_date, ''), NULLIF(@issue_date, ''), '');
SET @album_identity_hash_value := SHA2(CONCAT_WS('|', @album_normalized_title_value, @album_artist_name_value, @album_release_date_value), 256);
SET @existing_album_db_id := (
    SELECT album_db_id
    FROM album_source_refs
    WHERE source = @source
      AND source_album_id = @source_album_id
    LIMIT 1
);

INSERT INTO albums (
    title,
    normalized_title,
    primary_artist_name,
    release_date,
    identity_hash,
    created_at,
    updated_at
)
SELECT
    @title,
    @album_normalized_title_value,
    @album_artist_name_value,
    @album_release_date_value,
    @album_identity_hash_value,
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
WHERE @existing_album_db_id IS NULL
ON DUPLICATE KEY UPDATE
    id = LAST_INSERT_ID(id),
    title = VALUES(title),
    primary_artist_name = CASE WHEN VALUES(primary_artist_name) <> '' THEN VALUES(primary_artist_name) ELSE primary_artist_name END,
    release_date = CASE WHEN VALUES(release_date) <> '' THEN VALUES(release_date) ELSE release_date END,
    identity_hash = VALUES(identity_hash),
    updated_at = VALUES(updated_at);

SET @album_db_id := COALESCE(@existing_album_db_id, LAST_INSERT_ID());

UPDATE albums
SET
    title = @title,
    normalized_title = @album_normalized_title_value,
    primary_artist_name = CASE WHEN @album_artist_name_value <> '' THEN @album_artist_name_value ELSE primary_artist_name END,
    release_date = CASE WHEN @album_release_date_value <> '' THEN @album_release_date_value ELSE release_date END,
    identity_hash = @album_identity_hash_value,
    updated_at = UNIX_TIMESTAMP()
WHERE id = @album_db_id;

INSERT INTO album_source_refs (
    album_db_id,
    source,
    source_item_key,
    source_album_id,
    source_resource_type,
    source_res_type,
    source_action,
    source_album_kind,
    source_title,
    source_artist_id,
    source_artist_name,
    cover_url,
    summary,
    total_count,
    release_date,
    publish_date,
    play_num,
    play_num_desc,
    detail_sync_status,
    detail_synced_at,
    detail_sync_error,
    created_at,
    updated_at
) VALUES (
    @album_db_id,
    @source,
    @source_item_key,
    @source_album_id,
    NULLIF(@source_resource_type, ''),
    NULLIF(@source_res_type, ''),
    NULLIF(@source_action, ''),
    @source_album_kind,
    @source_title,
    NULLIF(@source_artist_id, ''),
    NULLIF(@source_artist_name, ''),
    NULLIF(@cover_url, ''),
    NULLIF(@summary, ''),
    NULLIF(@total_count, ''),
    NULLIF(@release_date, ''),
    NULLIF(@publish_date, ''),
    @play_num,
    NULLIF(@play_num_desc, ''),
    @detail_sync_status,
    CASE WHEN @detail_sync_status = 2 THEN UNIX_TIMESTAMP() ELSE 0 END,
    NULL,
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
) ON DUPLICATE KEY UPDATE
    id = LAST_INSERT_ID(id),
    album_db_id = VALUES(album_db_id),
    source_resource_type = COALESCE(VALUES(source_resource_type), source_resource_type),
    source_res_type = COALESCE(VALUES(source_res_type), source_res_type),
    source_action = COALESCE(VALUES(source_action), source_action),
    source_album_kind = CASE WHEN VALUES(source_album_kind) <> 0 THEN VALUES(source_album_kind) ELSE source_album_kind END,
    source_title = VALUES(source_title),
    source_artist_id = COALESCE(VALUES(source_artist_id), source_artist_id),
    source_artist_name = COALESCE(VALUES(source_artist_name), source_artist_name),
    cover_url = COALESCE(VALUES(cover_url), cover_url),
    summary = COALESCE(VALUES(summary), summary),
    total_count = COALESCE(VALUES(total_count), total_count),
    release_date = COALESCE(VALUES(release_date), release_date),
    publish_date = COALESCE(VALUES(publish_date), publish_date),
    play_num = CASE WHEN VALUES(play_num) <> 0 THEN VALUES(play_num) ELSE play_num END,
    play_num_desc = COALESCE(VALUES(play_num_desc), play_num_desc),
    detail_sync_status = CASE WHEN VALUES(detail_sync_status) <> 0 THEN VALUES(detail_sync_status) ELSE detail_sync_status END,
    detail_synced_at = CASE WHEN VALUES(detail_sync_status) = 2 THEN VALUES(detail_synced_at) ELSE detail_synced_at END,
    detail_sync_error = CASE WHEN VALUES(detail_sync_status) = 2 THEN NULL ELSE detail_sync_error END,
    updated_at = VALUES(updated_at);

SET @album_source_ref_id := LAST_INSERT_ID();

INSERT INTO migu_dalbum_details (
    album_source_ref_id,
    source_album_id,
    source_material_id,
    copyright_id,
    price,
    online_date,
    source_item_id,
    source_campaign_id,
    presale_date,
    first_start_date,
    first_end_date,
    sale_end_date,
    invalidate_date,
    original_first_start_date,
    original_first_end_date,
    digital_period_status,
    vip_type,
    vip_flag,
    vip_start_time,
    vip_end_time,
    effective_status,
    convert_status,
    forever_listen_flag,
    forever_listen,
    is_before_sale_end,
    is_in_first_date,
    is_call_payment,
    is_call_payment_for_present,
    is_call_payment_for_purchase,
    is_knowledge_paid,
    have_filter_tag,
    is_convert,
    is_appointment,
    created_at,
    updated_at
)
SELECT
    @album_source_ref_id,
    @source_album_id,
    NULLIF(@source_material_id, ''),
    NULLIF(@copyright_id, ''),
    NULLIF(@price, ''),
    NULLIF(@online_date, ''),
    NULLIF(@source_item_id, ''),
    NULLIF(@source_campaign_id, ''),
    NULLIF(@presale_date, ''),
    NULLIF(@first_start_date, ''),
    NULLIF(@first_end_date, ''),
    NULLIF(@sale_end_date, ''),
    NULLIF(@invalidate_date, ''),
    NULLIF(@original_first_start_date, ''),
    NULLIF(@original_first_end_date, ''),
    NULLIF(@digital_period_status, ''),
    NULLIF(@vip_type, ''),
    NULLIF(@vip_flag, ''),
    NULLIF(@vip_start_time, ''),
    NULLIF(@vip_end_time, ''),
    NULLIF(@effective_status, ''),
    NULLIF(@convert_status, ''),
    NULLIF(@forever_listen_flag, ''),
    @forever_listen,
    @is_before_sale_end,
    @is_in_first_date,
    @is_call_payment,
    @is_call_payment_for_present,
    @is_call_payment_for_purchase,
    @is_knowledge_paid,
    @have_filter_tag,
    @is_convert,
    @is_appointment,
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
WHERE @source_album_kind = 2
  AND @detail_sync_status = 2
ON DUPLICATE KEY UPDATE
    source_material_id = COALESCE(VALUES(source_material_id), source_material_id),
    copyright_id = COALESCE(VALUES(copyright_id), copyright_id),
    price = COALESCE(VALUES(price), price),
    online_date = COALESCE(VALUES(online_date), online_date),
    source_item_id = COALESCE(VALUES(source_item_id), source_item_id),
    source_campaign_id = COALESCE(VALUES(source_campaign_id), source_campaign_id),
    presale_date = COALESCE(VALUES(presale_date), presale_date),
    first_start_date = COALESCE(VALUES(first_start_date), first_start_date),
    first_end_date = COALESCE(VALUES(first_end_date), first_end_date),
    sale_end_date = COALESCE(VALUES(sale_end_date), sale_end_date),
    invalidate_date = COALESCE(VALUES(invalidate_date), invalidate_date),
    original_first_start_date = COALESCE(VALUES(original_first_start_date), original_first_start_date),
    original_first_end_date = COALESCE(VALUES(original_first_end_date), original_first_end_date),
    digital_period_status = COALESCE(VALUES(digital_period_status), digital_period_status),
    vip_type = COALESCE(VALUES(vip_type), vip_type),
    vip_flag = COALESCE(VALUES(vip_flag), vip_flag),
    vip_start_time = COALESCE(VALUES(vip_start_time), vip_start_time),
    vip_end_time = COALESCE(VALUES(vip_end_time), vip_end_time),
    effective_status = COALESCE(VALUES(effective_status), effective_status),
    convert_status = COALESCE(VALUES(convert_status), convert_status),
    forever_listen_flag = COALESCE(VALUES(forever_listen_flag), forever_listen_flag),
    forever_listen = CASE WHEN VALUES(forever_listen) <> -1 THEN VALUES(forever_listen) ELSE forever_listen END,
    is_before_sale_end = CASE WHEN VALUES(is_before_sale_end) <> -1 THEN VALUES(is_before_sale_end) ELSE is_before_sale_end END,
    is_in_first_date = CASE WHEN VALUES(is_in_first_date) <> -1 THEN VALUES(is_in_first_date) ELSE is_in_first_date END,
    is_call_payment = CASE WHEN VALUES(is_call_payment) <> -1 THEN VALUES(is_call_payment) ELSE is_call_payment END,
    is_call_payment_for_present = CASE WHEN VALUES(is_call_payment_for_present) <> -1 THEN VALUES(is_call_payment_for_present) ELSE is_call_payment_for_present END,
    is_call_payment_for_purchase = CASE WHEN VALUES(is_call_payment_for_purchase) <> -1 THEN VALUES(is_call_payment_for_purchase) ELSE is_call_payment_for_purchase END,
    is_knowledge_paid = CASE WHEN VALUES(is_knowledge_paid) <> -1 THEN VALUES(is_knowledge_paid) ELSE is_knowledge_paid END,
    have_filter_tag = CASE WHEN VALUES(have_filter_tag) <> -1 THEN VALUES(have_filter_tag) ELSE have_filter_tag END,
    is_convert = CASE WHEN VALUES(is_convert) <> -1 THEN VALUES(is_convert) ELSE is_convert END,
    is_appointment = CASE WHEN VALUES(is_appointment) <> -1 THEN VALUES(is_appointment) ELSE is_appointment END,
    updated_at = VALUES(updated_at);
