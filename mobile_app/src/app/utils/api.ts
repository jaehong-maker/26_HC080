import { CapacitorHttp, HttpResponse } from "@capacitor/core";

// API 서버 주소 (Lambda URL)
const LAMBDA_URL = "https://tgrwszo3iwurntqeq76s5rro640asnwq.lambda-url.ap-northeast-2.on.aws";

/**
 * 서버 응답 데이터 구조
 */
export interface ApiResponse {
  result?: string;
  message?: string;
  region?: string;
  music_tracks?: string;
  result_text?: string;
  spray?: number;
  temp?: string;
  weather?: string;
  transcript?: string;
  db_level?: number;
  db_avg?: number;
  is_spike?: boolean;
  intensity?: number;
  timer_enabled?: boolean;
  timer_start?: number;
  timer_end?: number;
  last_seen?: string;
  emotion_tag?: string;
  emotion_summary?: string;
  context?: string;
  mapping?: Record<string, number>;
  active_mode?: "weather" | "manual" | "ready" | "off" | string;
  active_scent?: number;
  led_r?: number;
  led_g?: number;
  led_b?: number;
  led_br?: number;
  led_dict?: { led_r: number; led_g: number; led_b: number; led_bright?: number };
}

/**
 * 공통 API 요청 함수 (CapacitorHttp 사용)
 */
async function requestApi(data: any): Promise<ApiResponse> {
  console.log("API Request Payload:", data);
  try {
    const options = {
      url: LAMBDA_URL,
      headers: {
        "Content-Type": "application/json",
      },
      data: data,
    };

    const response: HttpResponse = await CapacitorHttp.post(options);

    if (response.status !== 200) {
      console.error(`HTTP Error: ${response.status}`);
      throw new Error(`서버 에러: ${response.status}`);
    }

    const resultData = response.data;
    console.log("API Response Data:", resultData);
    return resultData;
  } catch (error) {
    console.error("API Request Error Detail:", error);
    return {
      result: "FAIL",
      message: error instanceof Error ? error.message : "통신 오류가 발생했습니다.",
    };
  }
}

/**
 * 음성 데이터 전송 API
 */
export async function apiSendVoiceData(base64Audio: string, deviceId?: string, contentType: string = "audio/wav"): Promise<ApiResponse> {
  console.log(`[apiSendVoiceData] Sending Voice Data... (Type: ${contentType}, Device: ${deviceId || "SS"})`);
  try {
    const options = {
      url: LAMBDA_URL,
      headers: {
        "Content-Type": contentType,
        "x-device-id": deviceId || "SS",
        "x-mime-type": contentType, 
      },
      data: base64Audio,
    };

    const response: HttpResponse = await CapacitorHttp.post(options);
    
    if (response.status !== 200) {
      console.error(`[apiSendVoiceData] Server Error Status: ${response.status}`);
      return { result: "FAIL", message: `서버 오류 (${response.status})` };
    }

    console.log("[apiSendVoiceData] Server Success Response:", response.data);
    return response.data;
  } catch (error) {
    console.error("[apiSendVoiceData] Request Failed:", error);
    return { result: "FAIL", message: "음성 인식 서버 통신 실패" };
  }
}

/**
 * 로그인 API
 */
export async function apiLogin(email: string, pass: string): Promise<ApiResponse> {
  return requestApi({
    action: "LOGIN",
    email,
    password: pass,
  });
}

/**
 * 회원가입 API
 */
export async function apiSignup(email: string, pass: string, region: string, authCode: string, deviceId: string): Promise<ApiResponse> {
  return requestApi({
    action: "SIGNUP",
    email,
    password: pass,
    region,
    auth_code: authCode,
    deviceId,
  });
}

/**
 * 인증번호 전송 API
 */
export async function apiSendAuthCode(email: string): Promise<ApiResponse> {
  return requestApi({
    action: "SEND_AUTH",
    email,
  });
}

/**
 * 사용자 정보 업데이트 API (비밀번호, 지역 등)
 */
export async function apiUpdateUser(email: string, oldPassword?: string, newPassword?: string, region?: string): Promise<ApiResponse> {
  return requestApi({
    action: "UPDATE_USER",
    email,
    old_password: oldPassword,
    new_password: newPassword,
    region,
  });
}

/**
 * 기기 상태 조회 (POLL)
 */
export async function apiPollDeviceState(email: string, deviceId?: string): Promise<ApiResponse & { weights?: number[], weights_raw?: number[], intensity?: number, volume?: number, db_level?: number }> {
  return requestApi({
    action: "POLL",
    email,
    deviceId,
  });
}

/**
 * 데이터 전송 옵션 인터페이스
 */
export interface SendDataOptions {
  email: string;
  action: string;
  value: number;
  region: string;
  deviceId?: string;
  ledData?: { r: number, g: number, b: number, br: number };
  diaryText?: string;
  dataPayload?: string; 
  timer_enabled?: boolean;
  timer_start?: number;
  timer_end?: number;
  mapping?: Record<string, number>;
}

/**
 * 데이터 전송 (날씨, 감정, 수동 제어 등)
 */
export async function apiSendData(options: SendDataOptions): Promise<ApiResponse> {
  const { email, action, value, region, deviceId, ledData, diaryText, dataPayload, timer_enabled, timer_start, timer_end } = options;
  const sprayValue = Number(value);
  const dId = deviceId || "SS";

  const data: any = { 
    email, 
    deviceId: dId,
    device: dId,
    action: action,
    region: region 
  };

  if (diaryText) data.diary_text = diaryText;

  if (action === "AI_WEATHER") {
    data.mode = "weather";
    data.region = region;
    data.duration = 3;
  } else if (action === "AI_EMOTION") {
    data.mode = "emotion";
    data.user_emotion = region; 
  } else if (action === "AI_REPLAY") {
    data.mode = "ai_replay";
  } else if (action === "noise") {
    data.mode = "ambient";
  } else if (action === "MENU_STOP" || action === "STOP_ALL") {
    data.mode = "menu";
    data.action = "STOP_ALL";
    data.spray = 0;
  } else if (action === "SET_INTENSITY") {
    data.mode = "manual";
    data.action = "SET_INTENSITY";
    data.intensity = sprayValue;
    // 타이머 정보가 있으면 추가
    if (timer_enabled !== undefined) data.timer_enabled = timer_enabled;
    if (timer_start !== undefined) data.timer_start = timer_start;
    if (timer_end !== undefined) data.timer_end = timer_end;
    // JSON 형태의 dataPayload가 있으면 파싱해서 합치기
    if (dataPayload) {
      try {
        const extra = JSON.parse(dataPayload);
        Object.assign(data, extra);
      } catch(e) {}
    }
  } else if (action === "SET_VOLUME") {
    data.mode = "manual";
    data.action = "SET_VOLUME";
    data.volume = sprayValue;
  } else if (action === "SAVE_MUSIC") {
    data.mode = "manual";
    data.action = "SAVE_MUSIC";
    data.music = region; // 곡 목록 (1_6_11_16 형식)
  } else if (action === "SET_MAPPING") {
    data.mode = "manual";
    data.action = "SET_MAPPING";
    data.mapping = options.mapping;
  } else if (action === "SET_LED") {
    data.mode = "manual";
    data.action = "SET_LED";
    if (ledData) {
      data.led_r = ledData.r;
      data.led_g = ledData.g;
      data.led_b = ledData.b;
      data.led_br = ledData.br;
    }
  } else if (action === "TIMER_START") {
    data.mode = "manual";
    data.action = "TIMER_START";
    data.run_minutes = sprayValue; 
    data.spray = region === "STOP" ? 90 : (Number(region) || 1);
  } else if (action === "MANUAL") {
    data.mode = "manual";
    data.action = "MANUAL"; 
    data.spray = sprayValue;
    data.region = region;
    data.duration = 3; 
  } else if (action === "FEEDBACK" || action === "LIKE" || action === "DISLIKE") {
    const actType = action === "LIKE" ? "LIKE" : action === "DISLIKE" ? "DISLIKE" : (region === "like" ? "LIKE" : "DISLIKE");
    data.mode = "manual";
    data.event_type = "FEEDBACK";
    data.action = actType;
    data.value = actType === "LIKE" ? 2 : -5;
    data.current_scent_id = sprayValue || 1;
    data.data = dataPayload || region;
    data.region = region;
  } else {
    data.mode = "manual";
    data.action = action;
    data.spray = sprayValue;
    data.region = region;
  }

  return requestApi(data);
}

export async function apiFetchDiaries(email: string): Promise<ApiResponse> {
  return requestApi({
    email,
    action: "GET_DIARIES"
  });
}
