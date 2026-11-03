
# 디퓨저 기기 자체의 상태 조회 및 제어 요청을 담당

import json
import logging
import math
import statistics
from datetime import datetime, timedelta, timezone
from decimal import Decimal
import time

import db_utils
import api_utils
import config
from .common import _to_decimal_list, _get_ambient_noise_bucket

logger = logging.getLogger()


def _read_microphone_level(body):
    """Accept only the explicit firmware validity flag and a finite sample."""
    if body.get("microphone_valid") is not True or "db_level" not in body:
        return None
    raw_level = body["db_level"]
    if isinstance(raw_level, bool):
        return None
    try:
        level = float(raw_level)
    except (TypeError, ValueError, OverflowError):
        return None
    return level if math.isfinite(level) and level >= 0 else None


def _noise_records(raw_samples, now, cutoff=None):
    records = []
    for sample in raw_samples if isinstance(raw_samples, list) else []:
        try:
            timestamp, value = float(sample["timestamp"]), float(sample["value"])
        except (KeyError, TypeError, ValueError, OverflowError):
            continue
        if (
            math.isfinite(timestamp) and math.isfinite(value) and value >= 0
            and timestamp <= now and (cutoff is None or timestamp >= cutoff)
        ):
            records.append({"timestamp": timestamp, "value": value})
    return records


def _decimal_records(records):
    return [{"timestamp": Decimal(str(sample["timestamp"])), "value": Decimal(str(sample["value"]))}
            for sample in records]


def _collect_ambient_startup(state, body, now_epoch):
    """Collect fresh POLL samples spanning at least the startup interval."""
    if state.get("ambient_startup_awaiting_poll", False):
        # This snapshot predates the device receiving the pump/audio stop.
        # Exclude it so the previous mode cannot choose the initial scent.
        return [], 0, False, 0.0, 0.0

    records = _noise_records(state.get("ambient_startup_samples", []), now_epoch)
    gap = int(config.AMBIENT_SAMPLE_GAP_RESET_SECONDS)
    if records and now_epoch - records[-1]["timestamp"] >= gap:
        records = []
    value = _read_microphone_level(body)
    if value is not None:
        records.append({"timestamp": now_epoch, "value": value})
    started_at = records[0]["timestamp"] if records else 0
    duration = int(config.AMBIENT_STARTUP_MEASUREMENT_SECONDS)
    ready = bool(value is not None and len(records) >= 2 and records[-1]["timestamp"] - started_at >= duration)
    values = [sample["value"] for sample in records]
    average = round(sum(values) / len(values), 2) if values else 0.0
    stddev = round(statistics.pstdev(values), 2) if len(values) > 1 else 0.0
    return records, started_at, ready, average, stddev


def _prepare_noise_update(state, body, now):
    """Produce measurement-only writes; invalid POLLs still run device control."""
    current_db = _read_microphone_level(body)
    valid = current_db is not None
    window = int(config.AMBIENT_ANALYSIS_WINDOW_SECONDS)
    gap = int(config.AMBIENT_SAMPLE_GAP_RESET_SECONDS)
    last_valid = float(state.get("db_last_sample_time", 0) or 0)
    # Old records may contain synthetic zeroes from the previous protocol.
    # Discard them once rather than treating them as verified measurements.
    migrating = state.get("noise_sample_validity_version") != 1
    reset = migrating or last_valid <= 0 or now - last_valid >= gap
    records = [] if reset else _noise_records(state.get("db_samples", []), now, now - window)
    started_at = float(state.get("db_window_started_at", now) or now)
    if reset or not records:
        started_at = now
    if valid:
        records.append({"timestamp": now, "value": current_db})
    records = records[-int(config.AMBIENT_MAX_SAMPLES):]
    updates = {
        "microphone_valid": valid, "noise_sample_validity_version": 1,
        "db_samples": _decimal_records(records),
        "db_window_started_at": Decimal(str(started_at)),
        "db_window_ready": bool(valid and now - started_at >= window),
        "ambient_loud_since": 0, "ambient_sustained_loud": False, "is_spike": False,
        "ambient_noise_rule_version": 2,
    }
    remove = []
    average = stddev = effective_db = None
    previous_context = str(state.get("ambient_context", ""))
    previous_bucket = previous_context.rsplit("_", 1)[-1] if previous_context else ""
    average_bucket = ""
    bucket_changed = False
    if valid:
        values = [sample["value"] for sample in records]
        average = round(sum(values) / len(values), 2)
        stddev = round(statistics.pstdev(values), 2) if len(values) > 1 else 0.0
        previous_db = float(state.get("db_level", current_db))
        is_spike = bool(
            not reset and state.get("microphone_valid") is True
            and abs(current_db - previous_db) >= config.AMBIENT_SPIKE_DELTA_DB
        )
        average_bucket, _ = _get_ambient_noise_bucket(average)
        bucket_changed = bool(
            state.get("active_mode") == "ambient" and updates["db_window_ready"]
            and previous_bucket and average_bucket != previous_bucket
        )
        applied_effective_db = float(state.get("ambient_effective_db", average)) if previous_context else average
        effective_db = applied_effective_db
        if bucket_changed:
            effective_db = average
        updates.update({
            "db_level": Decimal(str(current_db)), "db_last_sample_time": Decimal(str(now)),
            "db_avg": Decimal(str(average)), "db_stddev": Decimal(str(stddev)),
            "noise_type": "불규칙적(대화/활동)" if stddev > 8.0 else "안정적(음악/정적)",
            # A candidate bucket is applied only when the command commits.
            "is_spike": is_spike, "ambient_effective_db": Decimal(str(applied_effective_db)),
        })
        if state.get("active_mode") == "ambient" and current_db >= config.AMBIENT_HIGH_THRESHOLD_DB:
            continuous = not reset and state.get("microphone_valid") is True
            loud_since = float(state.get("ambient_loud_since", 0) or 0) if continuous else 0
            if loud_since <= 0:
                loud_since = now
            updates["ambient_loud_since"] = Decimal(str(loud_since))
            updates["ambient_sustained_loud"] = now - loud_since >= window
    elif reset:
        # No mean exists after a long gap. Preserve the last valid reading's
        # value/time for diagnostics, but never advertise a new zero sample.
        remove = ["db_avg", "db_stddev", "noise_type"]

    startup_ready = False
    startup_avg = startup_stddev = 0.0
    if state.get("ambient_startup_pending", False) and state.get("active_mode") == "ambient":
        startup_state = state
        if reset:
            startup_state = {**state, "ambient_startup_samples": []}
        samples, startup_start, startup_ready, startup_avg, startup_stddev = _collect_ambient_startup(
            startup_state, body, now,
        )
        updates.update({
            "ambient_startup_samples": _decimal_records(samples),
            "ambient_startup_started_at": Decimal(str(startup_start)),
            "ambient_startup_awaiting_poll": False,
        })
        if valid and samples:
            updates["ambient_startup_avg"] = Decimal(str(startup_avg))
        elif reset:
            remove.append("ambient_startup_avg")
    return updates, remove, {
        "valid": valid, "current_db": current_db, "db_avg": average, "db_stddev": stddev,
        "effective_db": effective_db, "previous_bucket": previous_bucket, "average_bucket": average_bucket,
        "bucket_changed": bucket_changed, "startup_ready": startup_ready,
        "startup_avg": startup_avg, "startup_stddev": startup_stddev,
    }

def handle_device(action, mode, body, device_id, normalized_weights):
    if action == "POLL":
        is_app_request = True if body.get("email") else False
        implicit_like_result = db_utils.apply_due_implicit_like(device_id)
        
        if not is_app_request:
            previous_state = db_utils.get_device_state(device_id) or {}
            now_timestamp = time.time()
            now_epoch = int(now_timestamp)
            startup_pending = bool(previous_state.get("ambient_startup_pending", False))
            noise_updates, noise_removals, noise = _prepare_noise_update(previous_state, body, now_timestamp)
            sample_valid = noise["valid"]
            current_db, db_avg, db_stddev = noise["current_db"], noise["db_avg"], noise["db_stddev"]
            effective_db = noise["effective_db"]
            previous_context = str(previous_state.get("ambient_context", ""))
            previous_noise_bucket, average_noise_bucket = noise["previous_bucket"], noise["average_bucket"]
            noise_bucket_changed = noise["bucket_changed"]
            startup_ready = noise["startup_ready"]
            startup_avg, startup_stddev = noise["startup_avg"], noise["startup_stddev"]
            window_ready = noise_updates["db_window_ready"]
            sustained_loud, is_spike = noise_updates["ambient_sustained_loud"], noise_updates["is_spike"]
            logger.debug("[POLL_HARDWARE] ID:%s Weights:%s MicrophoneValid:%s DB:%s",
                         device_id, normalized_weights, sample_valid, current_db)

            updates = {
                "weight_g": Decimal(str(normalized_weights[0])),
                "weights": _to_decimal_list(normalized_weights),
                **noise_updates,
            }
            current_music = body.get("music")
            updates["device_last_poll_time"] = Decimal(str(now_timestamp))
            if current_music is not None:
                updates["device_reported_music"] = int(current_music)
            updates["device_music_finish_reporting"] = isinstance(body.get("music_finished"), bool)
            if current_music is not None and previous_state.get("active_mode") != "ambient":
                updates["current_music"] = int(current_music)
            exp_vals = {}
            assignments = []
            for index, (field, value) in enumerate(updates.items()):
                token = ":poll_" + str(index)
                assignments.append(field + " = " + token)
                exp_vals[token] = value
            update_exp = "set " + ", ".join(assignments)
            if noise_removals:
                update_exp += " REMOVE " + ", ".join(noise_removals)
            measurement_updated = False
            try:
                probe_write_options = {}
                if startup_pending and previous_state.get("active_mode") == "ambient":
                    exp_vals.update({
                        ":probe_mode": "ambient",
                        ":probe_token": previous_state.get("ambient_startup_token"),
                        ":probe_pending": True,
                    })
                    probe_write_options["ConditionExpression"] = (
                        "active_mode = :probe_mode AND ambient_startup_token = :probe_token "
                        "AND ambient_startup_pending = :probe_pending"
                    )
                db_utils.state_table.update_item(
                    Key={"deviceId": device_id}, UpdateExpression=update_exp,
                    ExpressionAttributeValues=exp_vals, **probe_write_options,
                )
                measurement_updated = True
            except Exception as error:
                startup_ready = False
                logger.error("Weight/noise update failed: %s", error)

            if previous_state.get("diary_restart_pending", False):
                db_utils.activate_diary_playback(device_id, current_music)

            if previous_state.get("active_mode") == "ambient":
                # Only a hardware POLL may request a due finite extension.
                # Renew without AI so an unchanged environment keeps running.
                try:
                    db_utils.renew_ambient_lease(device_id, now_epoch)
                except Exception as error:
                    logger.error("Ambient renewal failed: %s", error)
                should_reevaluate = sample_valid and measurement_updated and (
                    startup_ready if startup_pending
                    else noise_bucket_changed or not previous_context
                )
                if should_reevaluate:
                    try:
                        logger.info(
                            f"[AMBIENT_BUCKET_CHANGE] ID:{device_id} "
                            f"{previous_noise_bucket or 'None'}->{average_noise_bucket} "
                            f"Avg:{db_avg} WindowReady:{window_ready}"
                        )
                        from .ambient_handler import handle_ambient
                        handle_ambient(
                            "ambient",
                            {
                                "db_level": current_db,
                                "db_avg": startup_avg if startup_pending else db_avg,
                                "db_1hr_avg": db_avg,
                                "db_stddev": startup_stddev if startup_pending else db_stddev,
                                "is_spike": is_spike,
                                "effective_db": startup_avg if startup_pending else effective_db,
                                "window_ready": window_ready,
                                "sustained_loud": sustained_loud,
                                "music_only": False,
                                "force_spray": bool(noise_bucket_changed),
                                "weights": normalized_weights,
                                "duration": getattr(config, "AMBIENT_DURATION_SECONDS", 60),
                                "_ambient_startup_token": previous_state.get("ambient_startup_token") if startup_pending else None,
                            },
                            device_id,
                            normalized_weights,
                        )
                    except Exception as e:
                        logger.error(f"[AMBIENT_AUTO_RECOMMEND_ERROR] ID:{device_id} {e}")
                elif not startup_pending:
                    # Track rotation neither queues a spray command nor renews
                    # its lease. The normal POLL response delivers music only.
                    db_utils.rotate_ambient_music(
                        device_id, now_timestamp, body.get("music"), body.get("music_finished"),
                    )

        state = db_utils.get_device_state(device_id) or {}
        
        db_weights = state.get('weights', [Decimal("0")] * 4)
        if isinstance(db_weights, list):
            final_weights = [float(w) for w in db_weights]
        else:
            final_weights = normalized_weights

        auto_stop_str = state.get('auto_stop_time', "")
        if auto_stop_str:
            try:
                auto_stop_time = datetime.fromisoformat(auto_stop_str)
                now_kst = datetime.now(timezone(timedelta(hours=9)))
                if now_kst >= auto_stop_time:
                    db_utils.manage_mailbox(device_id, 0)
                    try:
                        db_utils.state_table.update_item(
                            Key={'deviceId': device_id},
                            UpdateExpression="set active_mode = :m, active_scent = :s REMOVE auto_stop_time",
                            ExpressionAttributeValues={':m': 'ready', ':s': 0}
                        )
                    except: pass
            except: pass

        spray_code, target_region, duration = db_utils.manage_mailbox(device_id, is_peek=is_app_request)
        # A stop may have won while the mailbox was being consumed. Build
        # the response from the latest state and never return an old start.
        state = db_utils.state_table.get_item(
            Key={"deviceId": device_id}, ConsistentRead=True,
        ).get("Item", state)
        if state.get("active_mode") in ("ready", "off") and spray_code > 0:
            spray_code, duration = -1, 0
        
        current_spray = int(state.get('current_spray', 0))
        active_spray = spray_code if spray_code > 0 else current_spray
        active_mode = state.get('active_mode', 'ready')
        fixed_diary_music = int(state.get("diary_music_track", 0) or 0)
        is_fixed_diary = (
            active_mode == "manual" and fixed_diary_music > 0
            and int(state.get("diary_music_spray", 0) or 0) == active_spray
        )
        if active_mode == "ambient":
            music_code = int(state.get("current_music", 0))
        elif is_fixed_diary:
            music_code = fixed_diary_music
        else:
            music_code = db_utils.get_music_track(device_id, active_spray, is_pump=True) if active_spray > 0 else 0
        
        # A lease extension must not restart the same song. Prefer the track
        # actually reported by the hardware; app peeks never consume music.
        last_sent_music = int(state.get('last_sent_music', -1))
        reported_music = last_sent_music
        if active_mode == "ambient" and not is_app_request and "music" in body:
            reported_music = int(body.get("music", 0) or 0)
        can_send_music = active_mode == "ambient" or active_spray > 0
        final_music_code = 0
        if not is_app_request and can_send_music and music_code > 0:
            if music_code != reported_music or (active_mode != "ambient" and spray_code > 0):
                final_music_code = music_code
                try:
                    db_utils.state_table.update_item(
                        Key={'deviceId': device_id},
                        UpdateExpression="set last_sent_music = :m",
                        ExpressionAttributeValues={':m': music_code}
                    )
                except: pass
        
        intensity = int(state.get('intensity', 2)) 

        led_r = int(state.get('led_r', 255))
        led_g = int(state.get('led_g', 255))
        led_b = int(state.get('led_b', 255))
        led_br = int(state.get('led_br', 150))
        last_level = _read_microphone_level({"microphone_valid": True, "db_level": state.get("db_level")})
        last_valid_time = float(state.get("db_last_sample_time", 0) or 0)
        microphone_valid = bool(
            state.get("microphone_valid") is True and last_level is not None and last_valid_time > 0
            and 0 <= time.time() - last_valid_time < config.AMBIENT_SAMPLE_GAP_RESET_SECONDS
        )
        db_avg = float(state.get('db_avg', 0))
        db_stddev = float(state.get('db_stddev', 0))
        noise_type = str(state.get('noise_type', '분석 전'))
        is_spike = bool(state.get('is_spike', False))
        volume = int(state.get('volume', 5)) 

        mapping_dict = db_utils.get_spray_mapping(device_id)
        is_weather = (active_mode == 'weather')
        
        response_body = {
            "active_mode": active_mode,
            "active_scent": int(state.get('active_scent', 0)),
            "current_spray": int(state.get('current_spray', 0)),
            "spray": int(spray_code),
            "app_command": True if spray_code != -1 else False,
            "music": int(state.get('current_music', 0)) if is_app_request else int(final_music_code),
            "duration": int(duration),
            "intensity": intensity,
            "volume": volume,
            "timer_enabled": bool(state.get('timer_enabled', False)),
            "timer_start": int(state.get('timer_start', 9)),
            "timer_end": int(state.get('timer_end', 22)),
            "led_r": led_r,
            "led_g": led_g,
            "led_b": led_b,
            "led_br": led_br,
            "led_bright": led_br,
            "microphone_valid": microphone_valid,
            "db_avg": round(db_avg, 2),
            "db_stddev": round(db_stddev, 2),
            "noise_type": noise_type,
            "is_spike": is_spike,
            "db_window_ready": bool(microphone_valid and state.get("db_window_ready", False)),
            "ambient_sustained_loud": bool(microphone_valid and state.get("ambient_sustained_loud", False)),
            "ambient_effective_db": float(state.get("ambient_effective_db", 40)),
            "ambient_context": state.get("ambient_context", ""),
            "ambient_startup_pending": bool(state.get("ambient_startup_pending", False)),
            "ambient_music_policy": "finished-or-60s-v2",
            "ambient_bucket_music_policy": "immediate-bucket-v1",
            "ambient_music_changed_at": int(state.get("ambient_music_changed_at", 0)),
            "ambient_music_rotation_seconds": int(config.AMBIENT_MUSIC_ROTATION_SECONDS),
            "ambient_renewal_enabled": bool(state.get("ambient_renewal_enabled", False)),
            "device_reported_music": int(state.get("device_reported_music", 0)),
            "device_last_poll_time": float(state.get("device_last_poll_time", 0)),
            "device_music_finish_reporting": bool(state.get("device_music_finish_reporting", False)),
            "ambient_startup_avg": float(state.get("ambient_startup_avg", 0)),
            "weights": final_weights,
            "mapping": mapping_dict,
            "music_tracks": state.get('music_tracks', '1_6_11_16'),
            "result_text": "" if spray_code in [0, -1] else f"명령:{spray_code}"
        }
        if microphone_valid:
            response_body["db_level"] = last_level
        if is_fixed_diary and not is_app_request:
            # The device builds its playlist from music_tracks, not music.
            # Give it one song for this session without changing saved settings.
            slots = [int(digit) for digit in str(active_spray) if digit in "1234"]
            playback_tracks = ["0"] * 4
            playback_tracks[slots[0] - 1] = str(fixed_diary_music)
            response_body["music_tracks"] = "_".join(playback_tracks)
        for field in ("db_avg", "db_stddev", "ambient_startup_avg", "ambient_effective_db"):
            if field not in state:
                response_body.pop(field, None)
        if last_valid_time > 0:
            response_body["db_last_sample_time"] = last_valid_time
        if implicit_like_result:
            response_body["implicit_like_applied"] = True
            response_body["implicit_like_context"] = implicit_like_result["context"]
            response_body["implicit_like_kind"] = implicit_like_result["kind"]
        if is_weather:
            response_body["weather"] = state.get('weather', '')
            response_body["temp"] = state.get('temp', '')
            response_body["temperature"] = state.get('temp', '')
            response_body["humidity"] = state.get('humidity', '')
            response_body["humid"] = state.get('humidity', '')
            response_body["target_region"] = target_region

        return {
            "statusCode": 200,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps(response_body, ensure_ascii=False)
        }
        
    elif action in ["MENU_STOP", "STOP_ALL"] or mode == "menu":
        db_utils.manage_mailbox(device_id, 0)
        try: db_utils.state_table.update_item(Key={'deviceId': device_id}, UpdateExpression="remove auto_stop_time")
        except: pass
        try:
            db_utils.state_table.update_item(
                Key={'deviceId': device_id},
                UpdateExpression=(
                    "set active_mode = :m, active_scent = :z, current_spray = :z, "
                    "pending_cmd = :z, last_sent_music = :n, last_mode_change_time = :t, "
                    "ambient_startup_pending = :pending, ambient_startup_awaiting_poll = :pending"
                ),
                ExpressionAttributeValues={':m': 'ready', ':t': int(time.time()), ':z': 0, ':n': -1, ':pending': False}
            )
        except: pass
        return {
            "statusCode": 200,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps({
                "spray": 0,
                "active_mode": "ready",
                "active_scent": 0,
                "message": "정지 명령",
                "result_text": "STOP"
            }, ensure_ascii=False)
        }

    elif action == "MANUAL":
        try:
            spray_code = int(body.get("spray", 1))
            region_req = body.get("region", "")
            duration = int(body.get("duration", 3)) 

            db_utils.manage_mailbox(device_id, spray_code, region_req, duration)

            try: 
                db_utils.state_table.update_item(
                    Key={'deviceId': device_id},
                    UpdateExpression="set active_mode = :m, active_scent = :s, current_spray = :s, pending_cmd = :s, last_mode_change_time = :t REMOVE auto_stop_time",
                    ExpressionAttributeValues={':m': 'manual', ':s': int(spray_code), ':t': int(time.time())}
                )
            except Exception as e:
                logger.error(f"Manual DB Update Error: {e}")
                
            music_code = spray_code
            if spray_code in [1, 2, 3, 4]:
                music_code = db_utils.get_music_track(device_id, spray_code, is_pump=True)
                
            result_text = f"수동 명령 저장: {spray_code}번 (DeviceId: {device_id})"
            
            state = db_utils.get_device_state(device_id) or {}
            intensity = int(state.get('intensity', 2))
            
            return {
                "statusCode": 200,
                "headers": {"Content-Type": "application/json"},
                "body": json.dumps({
                    "result": "SUCCESS",
                    "spray": spray_code,
                    "duration": duration, 
                    "intensity": intensity,  
                    "result_text": result_text,
                    "message": result_text
                }, ensure_ascii=False)
            }
        except Exception as e:
            return {"statusCode": 500, "body": json.dumps({"result": "FAIL", "message": str(e)}, ensure_ascii=False)}

    elif action == "START":
        try:
            start_code = int(body.get("spray", 1))
            duration = int(body.get("duration", 3))
            region_req = body.get("region", "Manual")
            db_utils.manage_mailbox(device_id, start_code, region_req, duration)
            return {
                "statusCode": 200,
                "headers": {"Content-Type": "application/json"},
                "body": json.dumps({
                    "result": "SUCCESS", 
                    "spray": start_code, 
                    "message": f"START 명령 수신 (ID: {device_id})",
                    "duration": duration
                }, ensure_ascii=False)
            }
        except Exception as e:
            return {"statusCode": 500, "body": str(e)}

    elif action == "TEST_VOICE":
        test_text = body.get("text", "")
        kind_code, duration, result_text, led_dict = api_utils.ask_gemini_voice_analysis(test_text)
        mapped_pump, fb_msg = db_utils.get_smart_spray_mapping(device_id, kind_code)
        result_text += fb_msg
        return {
            "statusCode": 200,
            "body": json.dumps({
                "kind_code": kind_code,
                "spray_code": mapped_pump,
                "duration": duration,
                "result_text": result_text,
                "led_r": led_dict.get("led_r", 255) if led_dict else 255,
                "led_g": led_dict.get("led_g", 255) if led_dict else 255,
                "led_b": led_dict.get("led_b", 255) if led_dict else 255,
                "led_bright": led_dict.get("led_bright", 150) if led_dict else 150
            }, ensure_ascii=False)
        }

    elif action == "DISPLAY_MODE":
        try:
            req_mode = body.get("active_mode", "ready")
            req_scent = int(body.get("active_scent", 0))
            req_epoch = int(body.get("epoch", 0))
            if req_epoch == 0:
                req_epoch = int(body.get("timestamp", 0))
            
            state = db_utils.get_device_state(device_id) or {}
            last_change = int(state.get("last_mode_change_time", 0))
            now_ts = int(time.time())
            
            # 1. 타임스탬프(epoch) 기반 과거 데이터 차단
            if req_epoch > 0 and last_change > req_epoch:
                return {
                    "statusCode": 200,
                    "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({"result": "IGNORED", "message": "과거 상태 데이터이므로 무시됩니다."}, ensure_ascii=False)
                }
            
            # 2. 구버전 펌웨어용 3초 방어 로직
            if state.get("active_mode") == "ready" and req_mode not in ("ready", "off") and (now_ts - last_change < 3):
                return {
                    "statusCode": 200,
                    "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({"result": "IGNORED", "message": "앱에서 이미 기기를 정지했습니다."}, ensure_ascii=False)
                }
            
            if req_mode in ("ready", "off"):
                req_scent = 0
            expr = "set active_mode = :m, active_scent = :s"
            vals = {':m': req_mode, ':s': req_scent}
            
            if req_scent == 0 or req_mode == "ready":
                expr += ", current_spray = :z, last_sent_music = :n"
                vals[':z'] = 0
                vals[':n'] = -1

            if req_mode in ("ready", "off"):
                expr += (
                    ", pending_cmd = :z, ambient_renewal_enabled = :renewal_off, "
                    "ambient_startup_pending = :renewal_off, "
                    "ambient_startup_awaiting_poll = :renewal_off, last_mode_change_time = :stop_time "
                    "REMOVE ambient_lease_expires_at, ambient_renew_at, ambient_lease_token, "
                    "diary_restart_pending, diary_start_token, diary_pending_spray, diary_pending_music, "
                    "diary_pending_duration, diary_music_track, diary_music_spray"
                )
                vals.update({":z": 0, ":renewal_off": False, ":stop_time": now_ts})
                
            db_utils.state_table.update_item(
                Key={'deviceId': device_id},
                UpdateExpression=expr,
                ExpressionAttributeValues=vals
            )
            
            return {
                "statusCode": 200,
                "headers": {"Content-Type": "application/json"},
                "body": json.dumps({
                    "result": "SUCCESS",
                    "active_mode": req_mode,
                    "active_scent": req_scent,
                    "message": "디스플레이 모드 업데이트 완료"
                }, ensure_ascii=False)
            }
        except Exception as e:
            return {"statusCode": 500, "body": json.dumps({"result": "FAIL", "message": str(e)}, ensure_ascii=False)}

    return None
