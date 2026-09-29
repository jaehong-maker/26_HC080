# 디퓨저 스케줄 설정, 알람 시간 등 기기 세팅 변경을 담당하는 핸들러임

import json
import logging
from datetime import datetime, timedelta, timezone

import db_utils

logger = logging.getLogger()

# handle_settings 처리를 담당함
def handle_settings(action, body, device_id):
    if action == "SAVE_MUSIC":
        try:
            u_email = body.get("email") 
            music_data = body.get("music")
            
            if music_data and "_" in str(music_data):
                db_utils.state_table.update_item(
                    Key={'deviceId': device_id},
                    UpdateExpression="set music_tracks = :m",
                    ExpressionAttributeValues={':m': str(music_data)}
                )
                if u_email:
                    db_utils.user_table.update_item(
                        Key={'email': u_email},
                        UpdateExpression="set music_tracks = :m",
                        ExpressionAttributeValues={':m': str(music_data)}
                    )
                return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"spray": 0, "message": "개인 음악 설정 완료", "result_text": "OK"}, ensure_ascii=False)}
            else:
                return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"spray": 0, "message": "포맷 오류", "result_text": "FAIL"}, ensure_ascii=False)}
        except Exception as e:
            return {"statusCode": 500, "body": str(e)}

    elif action == "SET_MAPPING":
        try:
            u_email = body.get("email")
            mapping_data = body.get("mapping")
            
            if mapping_data:
                normalized = db_utils.save_spray_mapping(device_id, mapping_data, u_email)
                if normalized:
                    return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"spray": 0, "message": "향기 배치 설정 완료", "result_text": "OK"}, ensure_ascii=False)}
            
            return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"spray": 0, "message": "포맷 오류", "result_text": "FAIL"}, ensure_ascii=False)}
        except Exception as e:
            return {"statusCode": 500, "body": str(e)}

    elif action == "SET_INTENSITY":
        try:
            intensity_val = int(body.get("intensity", 2))
            timer_enabled = body.get("timer_enabled")
            timer_start = body.get("timer_start")
            timer_end = body.get("timer_end")
            
            update_exp = "set intensity = :i"
            exp_values = {':i': intensity_val}
            
            if timer_enabled is not None:
                update_exp += ", timer_enabled = :te"
                exp_values[':te'] = bool(timer_enabled)
            if timer_start is not None:
                update_exp += ", timer_start = :ts"
                exp_values[':ts'] = int(timer_start)
            if timer_end is not None:
                update_exp += ", timer_end = :teh"
                exp_values[':teh'] = int(timer_end)

            db_utils.state_table.update_item(
                Key={'deviceId': device_id},
                UpdateExpression=update_exp,
                ExpressionAttributeValues=exp_values
            )
            return {
                "statusCode": 200,
                "headers": {"Content-Type": "application/json"},
                "body": json.dumps({
                    "spray": 0, 
                    "intensity": intensity_val, 
                    "timer_enabled": timer_enabled,
                    "message": "기기 설정이 업데이트되었습니다.", 
                    "result_text": "OK"
                }, ensure_ascii=False)
            }
        except Exception as e:
            return {"statusCode": 500, "body": str(e)}

    elif action == "SET_VOLUME":
        try:
            # 앱은 'value' 키로 볼륨을 보내고, 기기는 'volume' 키로 보낼 수 있으므로 둘 다 체크
            target_vol = int(body.get("volume", body.get("value", 15)))
            db_utils.state_table.update_item(
                Key={'deviceId': device_id},
                UpdateExpression="set volume = :v",
                ExpressionAttributeValues={':v': target_vol}
            )
            return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"result": "SUCCESS", "message": f"볼륨이 {target_vol}으로 변경되었습니다.", "volume": target_vol}, ensure_ascii=False)}
        except Exception as e:
            return {"statusCode": 500, "body": str(e)}

    elif action == "SET_LED":
        try:
            # 앱은 'ledData' 객체 내부에 r, g, b, br을 담아서 보냄
            led_data = body.get("ledData", {})
            r = int(body.get("led_r", led_data.get("r", 255)))
            g = int(body.get("led_g", led_data.get("g", 255)))
            b = int(body.get("led_b", led_data.get("b", 255)))
            br = int(body.get("led_br", body.get("led_bright", led_data.get("br", 150))))
            
            db_utils.state_table.update_item(
                Key={'deviceId': device_id},
                UpdateExpression="set led_r = :r, led_g = :g, led_b = :b, led_br = :br",
                ExpressionAttributeValues={
                    ':r': r, ':g': g, ':b': b, ':br': br
                }
            )
            return {
                "statusCode": 200,
                "headers": {"Content-Type": "application/json"},
                "body": json.dumps({
                    "spray": 0, 
                    "message": "조명 설정이 서버에 저장되었습니다.", 
                    "result_text": "OK",
                    "led_r": r, "led_g": g, "led_b": b, "led_br": br
                }, ensure_ascii=False)
            }
        except Exception as e:
            return {"statusCode": 500, "body": str(e)}

    elif action == "TIMER_START":
        try:
            spray_code = int(body.get("spray", 1))
            run_minutes = int(body.get("run_minutes", 1))
            
            db_utils.manage_mailbox(device_id, spray_code)
            
            now_kst = datetime.now(timezone(timedelta(hours=9)))
            stop_time = now_kst + timedelta(minutes=run_minutes)
            db_utils.state_table.update_item(
                Key={'deviceId': device_id},
                UpdateExpression="set auto_stop_time = :t",
                ExpressionAttributeValues={':t': stop_time.isoformat()}
            )
            return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"spray": spray_code, "message": f"{run_minutes}분 작동 후 서버가 정지합니다.", "result_text": "OK"}, ensure_ascii=False)}
        except Exception as e: 
            return {"statusCode": 500, "body": str(e)}
            
    return None
