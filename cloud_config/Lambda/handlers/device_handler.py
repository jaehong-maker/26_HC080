
# 디퓨저 기기 자체의 상태 조회 및 제어 요청을 담당

import json
import logging
import statistics
from datetime import datetime, timedelta, timezone
from decimal import Decimal
import time

import db_utils
import api_utils
import config
from .common import _to_decimal_list

logger = logging.getLogger()

def handle_device(action, mode, body, device_id, normalized_weights):
    if action == "POLL":
        is_app_request = True if body.get("email") else False
        implicit_like_result = db_utils.apply_due_implicit_like(device_id)
        
        if not is_app_request:
            previous_state = db_utils.get_device_state(device_id) or {}
            current_w = normalized_weights[0]
            current_db = int(body.get("db_level", 0))
            logger.debug(f"[POLL_HARDWARE] ID:{device_id} Weights:{normalized_weights} DB:{current_db}")

            now_epoch = int(time.time())
            window_seconds = int(getattr(config, "AMBIENT_ANALYSIS_WINDOW_SECONDS", 180))
            gap_reset_seconds = int(getattr(config, "AMBIENT_SAMPLE_GAP_RESET_SECONDS", 10))
            max_samples = int(getattr(config, "AMBIENT_MAX_SAMPLES", 600))
            last_sample_time = int(previous_state.get("db_last_sample_time", 0))
            window_started_at = int(previous_state.get("db_window_started_at", now_epoch))
            previous_samples = previous_state.get("db_samples", [])
            sample_records = []

            if last_sample_time <= 0 or now_epoch - last_sample_time > gap_reset_seconds:
                window_started_at = now_epoch
            elif isinstance(previous_samples, list):
                cutoff_epoch = now_epoch - window_seconds
                for sample in previous_samples:
                    if not isinstance(sample, dict):
                        continue
                    try:
                        sample_time = int(sample.get("timestamp", 0))
                        sample_value = float(sample.get("value", 0))
                    except Exception:
                        continue
                    if sample_time >= cutoff_epoch:
                        sample_records.append({"timestamp": sample_time, "value": sample_value})

            sample_records.append({"timestamp": now_epoch, "value": float(current_db)})
            sample_records = sample_records[-max_samples:]
            sample_values = [sample["value"] for sample in sample_records]
            db_avg = round(sum(sample_values) / len(sample_values), 2)
            db_stddev = round(statistics.pstdev(sample_values), 2) if len(sample_values) > 1 else 0.0
            window_ready = now_epoch - window_started_at >= window_seconds
            previous_db = int(previous_state.get("db_level", current_db))
            spike_delta = int(getattr(config, "AMBIENT_SPIKE_DELTA_DB", 15))
            is_spike = (
                last_sample_time > 0
                and abs(current_db - previous_db) >= spike_delta
            )
            noise_type = "불규칙적(대화/활동)" if db_stddev > 8.0 else "안정적(음악/정적)"

            high_threshold = float(getattr(config, "AMBIENT_HIGH_THRESHOLD_DB", 40))
            rule_version = int(previous_state.get("ambient_noise_rule_version", 0) or 0)
            previous_sustained_loud = (
                bool(previous_state.get("ambient_sustained_loud", False))
                if rule_version == 2 else False
            )
            loud_since = (
                int(previous_state.get("ambient_loud_since", 0) or 0)
                if rule_version == 2 else 0
            )
            ambient_active = previous_state.get("active_mode") == "ambient"
            continuity_broken = (
                last_sample_time <= 0
                or now_epoch - last_sample_time > gap_reset_seconds
            )

            # A music change is allowed only after the *current* sound level
            # has remained at or above the threshold continuously for the
            # whole window. A short spike starts no recommendation.
            if not ambient_active or current_db < high_threshold:
                loud_since = 0
                sustained_loud = False
            else:
                if continuity_broken or loud_since <= 0:
                    loud_since = now_epoch
                    previous_sustained_loud = False
                sustained_loud = bool(
                    previous_sustained_loud
                    or now_epoch - loud_since >= window_seconds
                )

            became_sustained_loud = sustained_loud and not previous_sustained_loud
            effective_db = float(previous_state.get("ambient_effective_db", 40))
            if became_sustained_loud:
                effective_db = db_avg
            
            update_exp = (
                "set weight_g = :wg, weights = :w, db_level = :db, "
                "db_samples = :samples, db_avg = :avg, db_stddev = :std, "
                "noise_type = :nt, is_spike = :sp, db_window_started_at = :ws, "
                "db_last_sample_time = :ls, db_window_ready = :wr, "
                "ambient_sustained_loud = :sl, ambient_effective_db = :edb, "
                "ambient_loud_since = :als, ambient_noise_rule_version = :arv"
            )
            exp_vals = {
                ':wg': Decimal(str(current_w)),
                ':w': _to_decimal_list(normalized_weights),
                ':db': current_db,
                ':samples': [
                    {
                        "timestamp": Decimal(str(sample["timestamp"])),
                        "value": Decimal(str(sample["value"])),
                    }
                    for sample in sample_records
                ],
                ':avg': Decimal(str(db_avg)),
                ':std': Decimal(str(db_stddev)),
                ':nt': noise_type,
                ':sp': bool(is_spike),
                ':ws': window_started_at,
                ':ls': now_epoch,
                ':wr': bool(window_ready),
                ':sl': bool(sustained_loud),
                ':edb': Decimal(str(effective_db)),
                ':als': int(loud_since),
                ':arv': 2,
            }
            
            # 기기가 현재 재생 중인 곡 번호 업데이트
            current_music = body.get("music")
            # Ambient music is server-owned. Do not let a slightly delayed
            # device POLL overwrite a newly selected server track.
            if current_music is not None and previous_state.get("active_mode") != "ambient":
                update_exp += ", current_music = :cm"
                exp_vals[':cm'] = int(current_music)

            try:
                db_utils.state_table.update_item(
                    Key={'deviceId': device_id},
                    UpdateExpression=update_exp,
                    ExpressionAttributeValues=exp_vals
                )
            except Exception as e:
                logger.error(f"무게 및 소음 DB 업데이트 실패: {e}")

            if previous_state.get("active_mode") == "ambient":
                previous_context = str(previous_state.get("ambient_context", ""))
                should_reevaluate = became_sustained_loud or not previous_context
                if should_reevaluate:
                    try:
                        from .ambient_handler import handle_ambient
                        handle_ambient(
                            "ambient",
                            {
                                "db_level": current_db,
                                "db_avg": db_avg,
                                "db_1hr_avg": db_avg,
                                "db_stddev": db_stddev,
                                "is_spike": is_spike,
                                "effective_db": effective_db,
                                "window_ready": window_ready,
                                "sustained_loud": sustained_loud,
                                "music_only": bool(became_sustained_loud),
                                "weights": normalized_weights,
                                "duration": getattr(config, "AMBIENT_DURATION_SECONDS", 3),
                            },
                            device_id,
                            normalized_weights,
                        )
                    except Exception as e:
                        logger.error(f"[AMBIENT_AUTO_RECOMMEND_ERROR] ID:{device_id} {e}")

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
        
        current_spray = int(state.get('current_spray', 0))
        active_spray = spray_code if spray_code > 0 else current_spray
        active_mode = state.get('active_mode', 'ready')
        if active_mode == "ambient":
            music_code = int(state.get("current_music", 0))
        else:
            music_code = db_utils.get_music_track(device_id, active_spray, is_pump=True) if active_spray > 0 else 0
        
        # [BUG FIX] 음악 무한 재시작 방지 로직
        # 기기는 0이 아닌 music 값을 받으면 무조건 트랙을 처음부터 다시 재생합니다.
        # 따라서 새 명령이거나, 앱에서 음악 설정을 바꿨을 때만 0이 아닌 값을 내려보냅니다.
        last_sent_music = int(state.get('last_sent_music', -1))
        
        if spray_code > 0:
            final_music_code = music_code
            if music_code > 0:
                try:
                    db_utils.state_table.update_item(
                        Key={'deviceId': device_id},
                        UpdateExpression="set last_sent_music = :m",
                        ExpressionAttributeValues={':m': music_code}
                    )
                except: pass
        else:
            can_send_music = active_mode == "ambient" or active_spray > 0
            if can_send_music and music_code > 0 and music_code != last_sent_music:
                final_music_code = music_code
                try:
                    db_utils.state_table.update_item(
                        Key={'deviceId': device_id},
                        UpdateExpression="set last_sent_music = :m",
                        ExpressionAttributeValues={':m': music_code}
                    )
                except: pass
            else:
                final_music_code = 0
        
        intensity = int(state.get('intensity', 2)) 

        led_r = int(state.get('led_r', 255))
        led_g = int(state.get('led_g', 255))
        led_b = int(state.get('led_b', 255))
        led_br = int(state.get('led_br', 150))
        db_level = int(state.get('db_level', 0))
        db_avg = float(state.get('db_avg', db_level))
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
            "db_level": db_level,
            "db_avg": round(db_avg, 2),
            "db_stddev": round(db_stddev, 2),
            "noise_type": noise_type,
            "is_spike": is_spike,
            "db_window_ready": bool(state.get("db_window_ready", False)),
            "ambient_sustained_loud": bool(state.get("ambient_sustained_loud", False)),
            "ambient_effective_db": float(state.get("ambient_effective_db", 40)),
            "ambient_context": state.get("ambient_context", ""),
            "weights": final_weights,
            "mapping": mapping_dict,
            "music_tracks": state.get('music_tracks', '1_6_11_16'),
            "result_text": "" if spray_code in [0, -1] else f"명령:{spray_code}"
        }
        profile_region = db_utils.normalize_weather_region(state.get('last_region', ''))
        if profile_region in config.REGION_COORDS:
            response_body["profile_region"] = profile_region
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
                UpdateExpression="set active_mode = :m, active_scent = :z, current_spray = :z, pending_cmd = :z, last_sent_music = :n, last_mode_change_time = :t",
                ExpressionAttributeValues={':m': 'ready', ':t': int(time.time()), ':z': 0, ':n': -1}
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
            if state.get("active_mode") == "ready" and req_mode != "ready" and (now_ts - last_change < 3):
                return {
                    "statusCode": 200,
                    "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({"result": "IGNORED", "message": "앱에서 이미 기기를 정지했습니다."}, ensure_ascii=False)
                }
            
            expr = "set active_mode = :m, active_scent = :s"
            vals = {':m': req_mode, ':s': req_scent}
            
            if req_scent == 0 or req_mode == "ready":
                expr += ", current_spray = :z, last_sent_music = :n"
                vals[':z'] = 0
                vals[':n'] = -1
                
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
