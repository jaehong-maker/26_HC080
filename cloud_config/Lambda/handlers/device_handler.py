
# 디퓨저 기기 자체의 상태 조회 및 제어 요청을 담당

import json
import logging
from datetime import datetime, timedelta, timezone
from decimal import Decimal
import time

import db_utils
import api_utils
from .common import _to_decimal_list

logger = logging.getLogger()

def handle_device(action, mode, body, device_id, normalized_weights):
    if action == "POLL":
        is_app_request = True if body.get("email") else False
        
        if not is_app_request:
            current_w = normalized_weights[0]
            current_db = int(body.get("db_level", 0))
            logger.debug(f"[POLL_HARDWARE] ID:{device_id} Weights:{normalized_weights} DB:{current_db}")
            
            update_exp = "set weight_g = :wg, weights = :w, db_level = :db"
            exp_vals = {
                ':wg': Decimal(str(current_w)),
                ':w': _to_decimal_list(normalized_weights),
                ':db': current_db
            }
            
            # 기기가 현재 재생 중인 곡 번호 업데이트
            current_music = body.get("music")
            if current_music is not None:
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
            if active_spray > 0 and music_code > 0 and music_code != last_sent_music:
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
        volume = int(state.get('volume', 5)) 

        mapping_dict = db_utils.get_spray_mapping(device_id)
        active_mode = state.get('active_mode', 'ready')
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
            "weights": final_weights,
            "mapping": mapping_dict,
            "music_tracks": state.get('music_tracks', '1_6_11_16'),
            "result_text": "" if spray_code in [0, -1] else f"명령:{spray_code}"
        }
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
