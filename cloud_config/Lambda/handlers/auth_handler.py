# 사용자 인증, PIN 번호 확인 및 기기 등록/초기화를 담당하는 핸들러임


#사용자 인증, 로그인, 회원가입 관련 로직을 처리하는 람다 핸들러


import json
import random
import logging
from datetime import datetime, timedelta, timezone

import db_utils
import api_utils

logger = logging.getLogger()

# 기기 등록 및 인증을 처리함
def handle_auth(action, body, device_id):

    if action == "SEND_AUTH":
        try:
            u_email = body.get("email")
            if not u_email:
                return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"message": "이메일을 입력해주세요.", "result": "FAIL"}, ensure_ascii=False)}

            if 'Item' in db_utils.user_table.get_item(Key={'email': u_email}):
                return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"message": "이미 가입된 이메일입니다.", "result": "FAIL"}, ensure_ascii=False)}

            auth_code = str(random.randint(100000, 999999))
            
            if api_utils.send_verification_email(u_email, auth_code):
                db_utils.auth_table.put_item(Item={
                    'email': u_email,
                    'code': auth_code,
                    'createdAt': datetime.now(timezone(timedelta(hours=9))).isoformat()
                })
                return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"message": "인증번호가 발송되었습니다. 이메일을 확인해주세요.", "result": "SUCCESS"}, ensure_ascii=False)}
            else:
                return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"message": "이메일 발송에 실패했습니다.", "result": "FAIL"}, ensure_ascii=False)}
        except Exception as e:
            return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"message": f"진짜 에러 원인: {str(e)}", "result": "FAIL"}, ensure_ascii=False)} 

    elif action == "SIGNUP":
        try:
            u_email = body.get("email")
            u_password = body.get("password")
            u_region = body.get("region", "서울") 
            u_auth_code = body.get("auth_code")
            u_device_id = body.get("deviceId", "ESP32_Test")

            if not u_email or not u_password or not u_auth_code:
                return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"message": "입력 정보가 부족합니다.", "result": "FAIL"}, ensure_ascii=False)}

            auth_res = db_utils.auth_table.get_item(Key={'email': u_email})
            if 'Item' not in auth_res:
                return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"message": "인증번호 내역이 없습니다. 먼저 인증을 요청하세요.", "result": "FAIL"}, ensure_ascii=False)}
            
            if auth_res['Item']['code'] != u_auth_code:
                return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"message": "인증번호가 일치하지 않습니다.", "result": "FAIL"}, ensure_ascii=False)}

            db_utils.user_table.put_item(Item={
                'email': u_email,
                'password': u_password,
                'region': u_region,
                'music_tracks': '1_6_11_16', 
                'deviceId': u_device_id,
                'createdAt': datetime.now(timezone(timedelta(hours=9))).isoformat()
            })
            
            db_utils.auth_table.delete_item(Key={'email': u_email})
            return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"message": "회원가입이 완료되었습니다!", "result": "SUCCESS"}, ensure_ascii=False)}
        except Exception as e:
           return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"message": f"진짜 에러 원인: {str(e)}", "result": "FAIL"}, ensure_ascii=False)}

    elif action == "GET_DIARIES":
        try:
            u_email = body.get("email")
            if not u_email:
                return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"message": "이메일 정보가 없습니다.", "result": "FAIL"}, ensure_ascii=False)}
            
            diaries = db_utils.get_user_diaries(u_email)
            return {
                "statusCode": 200,
                "headers": {"Content-Type": "application/json"},
                "body": json.dumps({"result": "SUCCESS", "diaries": diaries}, ensure_ascii=False)
            }
        except Exception as e:
            return {"statusCode": 500, "body": str(e)}

    elif action == "UPDATE_USER":
        try:
            u_email = body.get("email")
            old_password = body.get("old_password")
            new_password = body.get("new_password")
            u_region = body.get("region")
            u_device_id = body.get("deviceId")

            if not u_email:
                return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"message": "이메일 정보가 없습니다.", "result": "FAIL"}, ensure_ascii=False)}

            res = db_utils.user_table.get_item(Key={'email': u_email})
            if 'Item' not in res:
                return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"message": "사용자를 찾을 수 없습니다.", "result": "FAIL"}, ensure_ascii=False)}

            user_info = res['Item']
            update_parts = []
            expr_attr_values = {}
            expr_attr_names = {}
            
            if new_password:
                if not old_password:
                    return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"message": "기존 비밀번호를 입력해주세요.", "result": "FAIL"}, ensure_ascii=False)}
                if user_info.get('password') != old_password:
                    return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"message": "기존 비밀번호가 일치하지 않습니다.", "result": "FAIL"}, ensure_ascii=False)}
                
                update_parts.append("#p = :p")
                expr_attr_values[":p"] = new_password
                expr_attr_names["#p"] = "password"

            if u_region:
                update_parts.append("#r = :r")
                expr_attr_values[":r"] = u_region
                expr_attr_names["#r"] = "region"

            if u_device_id:
                update_parts.append("#d = :d")
                expr_attr_values[":d"] = u_device_id
                expr_attr_names["#d"] = "deviceId"

            if not update_parts:
                return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"message": "변경할 내용이 없습니다.", "result": "SUCCESS"}, ensure_ascii=False)}

            update_expr = "SET " + ", ".join(update_parts)

            db_utils.user_table.update_item(
                Key={'email': u_email},
                UpdateExpression=update_expr,
                ExpressionAttributeValues=expr_attr_values,
                ExpressionAttributeNames=expr_attr_names
            )

            return {
                "statusCode": 200, 
                "headers": {"Content-Type": "application/json"}, 
                "body": json.dumps({
                    "message": "개인정보 수정이 완료되었습니다.", 
                    "result": "SUCCESS", 
                    "region": u_region or user_info.get('region'),
                    "deviceId": u_device_id or user_info.get('deviceId')
                }, ensure_ascii=False)
            }
        except Exception as e:
            logger.error(f"[UPDATE_USER] Error: {str(e)}")
            return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"message": f"업데이트 실패: {str(e)}", "result": "FAIL"}, ensure_ascii=False)}

    elif action == "LOGIN":
        try:
            u_email = body.get("email")
            u_password = body.get("password")
            res = db_utils.user_table.get_item(Key={'email': u_email})
            
            if 'Item' in res:
                user_info = res['Item']
                if user_info.get('password') != u_password:
                    return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"message": "비밀번호가 틀렸습니다.", "result": "FAIL"}, ensure_ascii=False)}

                my_music = user_info.get('music_tracks', '1_6_11_16')
                my_device = user_info.get('deviceId', 'ESP32_Test') 
                my_mapping = user_info.get('spray_mapping')

                try:
                    target_id = my_device if my_device else device_id
                    if target_id:
                        db_utils.state_table.update_item(Key={'deviceId': target_id}, UpdateExpression="set music_tracks = :m", ExpressionAttributeValues={':m': str(my_music)})
                        if my_mapping:
                            db_utils.save_spray_mapping(target_id, my_mapping)
                except: pass

                return {
                    "statusCode": 200,
                    "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({
                        "message": "로그인 성공!",
                        "result": "SUCCESS",
                        "region": user_info.get('region', '서울'),
                        "music_tracks": my_music,
                        "deviceId": my_device
                    }, ensure_ascii=False)
                }
            else:
                return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"message": "존재하지 않는 계정입니다.", "result": "FAIL"}, ensure_ascii=False)}
        except Exception as e:
            return {"statusCode": 200, "headers": {"Content-Type": "application/json"}, "body": json.dumps({"message": f"DB 에러: {str(e)}", "result": "FAIL"}, ensure_ascii=False)}
    
    return None
