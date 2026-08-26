import React, { createContext, useContext, useState, useEffect, useRef } from "react";
import { apiPollDeviceState, apiSendData } from "../utils/api";
import { useAuth } from "./AuthContext";

export interface ScentSlot {
  id: number;
  name: string;
  color: string;
  remaining: number;
  weightGrams: number;
}

interface DeviceContextType {
  scentSlots: ScentSlot[];
  updateScentSlot: (id: number, slot: Partial<ScentSlot>) => void;
  refreshDeviceState: () => Promise<void>;
  wifiStrength: "strong" | "medium" | "weak" | "disconnected";
  setWifiStrength: (val: "strong" | "medium" | "weak" | "disconnected") => void;
  volume: number;
  handleVolumeChange: (newVolume: number) => void;
  intensity: number;
  handleIntensityChange: (newLevel: number) => void;
  activeTimerMinutes: number | null;
  handleTimerChange: (minutes: number | null) => void;
  timerEnabled: boolean;
  timerStart: number;
  timerEnd: number;
  updateTimerSettings: (enabled: boolean, start: number, end: number) => Promise<void>;
  currentScent: string;
  isDiffuserOn: boolean;
  setIsDiffuserOn: (val: boolean) => void;
  ledColor: string;
  ledBrightness: number;
  isLedOn: boolean;
  activeMode: "manual" | "weather" | "voice" | "ai" | "noise" | null;
  setActiveMode: (mode: "manual" | "weather" | "voice" | "ai" | "noise" | null) => void;
  isDeviceActionLoading: boolean;
  sendDeviceData: (action: string, value: number, region?: string, diaryText?: string, ledData?: { r: number, g: number, b: number, br: number }) => Promise<any>;
  calibrateWeight: () => Promise<boolean>;
  dbLevel: number;
  dbAvg: number;
  dbStdDev: number;
  noiseType: string;
  dbChange: number;
  deviceStatus: string;
  currentMusicCode: number;
  isSpike: boolean;
}

const DeviceContext = createContext<DeviceContextType | undefined>(undefined);

export const DeviceProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const { currentUser, updateUser, updateLocalUser } = useAuth();
  const updateUserRef = useRef(updateUser);

  useEffect(() => {
    updateUserRef.current = updateUser;
  }, [updateUser]);
  
  const [scentSlots, setScentSlots] = useState<ScentSlot[]>(() => {
    const DEFAULT_SLOTS: ScentSlot[] = [
      { id: 1, name: "시트러스", color: "bg-orange-500", remaining: 0, weightGrams: 0 },
      { id: 2, name: "센달우드", color: "bg-amber-700", remaining: 0, weightGrams: 0 },
      { id: 3, name: "패츌리", color: "bg-green-700", remaining: 0, weightGrams: 0 },
      { id: 4, name: "페퍼민트", color: "bg-teal-500", remaining: 0, weightGrams: 0 },
    ];
    try {
      const saved = localStorage.getItem("smart_diffuser_scent_slots");
      if (saved) {
        const parsed = JSON.parse(saved);
        if (Array.isArray(parsed) && parsed.length === 4) {
          return parsed;
        }
      }
    } catch (e) {}
    return DEFAULT_SLOTS;
  });

  const [wifiStrength, setWifiStrength] = useState<"strong" | "medium" | "weak" | "disconnected">("disconnected");
  const [volume, setVolume] = useState<number>(5);
  const [intensity, setIntensity] = useState<number>(50);
  const [activeTimerMinutes, setActiveTimerMinutes] = useState<number | null>(null);
  
  const [timerEnabled, setTimerEnabled] = useState<boolean>(false);
  const [timerStart, setTimerStart] = useState<number>(9);
  const [timerEnd, setTimerEnd] = useState<number>(22);
  
  const [currentScent, setCurrentScent] = useState<string>("1");
  const [currentMusicCode, setCurrentMusicCode] = useState<number>(0);
  const [isDiffuserOn, setIsDiffuserOn] = useState<boolean>(false);
  const [ledColor, setLedColor] = useState<string>("#FBBF24");
  const [ledBrightness, setLedBrightness] = useState<number>(80);
  const [isLedOn, setIsLedOn] = useState<boolean>(false);
  const [activeMode, setActiveMode] = useState<"manual" | "weather" | "voice" | "ai" | "noise" | null>(null);

  const [isDeviceActionLoading, setIsDeviceActionLoading] = useState(false);
  const [dbLevel, setDbLevel] = useState<number>(0);
  const [dbAvg, setDbAvg] = useState<number>(0);
  const [dbStdDev, setDbStdDev] = useState<number>(0);
  const [noiseType, setNoiseType] = useState<string>("분석 전");
  const [dbChange, setDbChange] = useState<number>(0);
  const [deviceStatus, setDeviceStatus] = useState<string>("대기 중");
  const [isSpike, setIsSpike] = useState<boolean>(false);
  
  const lastIntensityUpdateTimeRef = useRef<number>(0);
  const lastVolumeUpdateTimeRef = useRef<number>(0);
  const lastLedUpdateTimeRef = useRef<number>(0);
  const lastDbLevelRef = useRef<number>(0);
  const lastModeChangeTimeRef = useRef<number>(0);

  const handleVolumeChange = (newVol: number) => {
    const rounded = Math.max(0, Math.min(10, Math.round(newVol)));
    setVolume(rounded);
    lastVolumeUpdateTimeRef.current = Date.now();

    if (currentUser) {
        const dId = currentUser.deviceId || "SS";
        apiSendData({
          email: currentUser.email,
          action: "SET_VOLUME",
          value: rounded,
          region: currentUser.region || "서울",
          deviceId: dId
        });
    }
  };

  const handleIntensityChange = async (newLevel: number) => {
    setIntensity(newLevel);
    lastIntensityUpdateTimeRef.current = Date.now();
    if (currentUser) {
      setIsDeviceActionLoading(true);
      try {
        const dId = currentUser.deviceId || "SS";
        await apiSendData({
          email: currentUser.email,
          action: "SET_INTENSITY",
          value: newLevel, 
          region: currentUser.region || "서울",
          deviceId: dId
        });
      } finally {
        setIsDeviceActionLoading(false);
      }
    }
  };

  const updateTimerSettings = async (enabled: boolean, start: number, end: number) => {
    setTimerEnabled(enabled);
    setTimerStart(start);
    setTimerEnd(end);
    
    if (currentUser) {
      setIsDeviceActionLoading(true);
      try {
        const dId = currentUser.deviceId || "SS";
        await apiSendData({
          email: currentUser.email,
          action: "SET_INTENSITY",
          value: intensity,
          region: currentUser.region || "서울",
          deviceId: dId,
          timer_enabled: enabled,
          timer_start: start,
          timer_end: end
        });
      } finally {
        setIsDeviceActionLoading(false);
        refreshDeviceState();
      }
    }
  };

  const handleTimerChange = async (minutes: number | null) => {
    setActiveTimerMinutes(minutes);

    if (currentUser) {
      setIsDeviceActionLoading(true);
      try {
        const dId = currentUser.deviceId || "SS";
        if (minutes === null) {
          await apiSendData({ email: currentUser.email, action: "MENU_STOP", value: 0, region: "STOP", deviceId: dId });
        } else {
          await apiSendData({ email: currentUser.email, action: "TIMER_START", value: minutes, region: currentScent, deviceId: dId });
        }
      } finally {
        setIsDeviceActionLoading(false);
        refreshDeviceState();
      }
    }
  };

  const refreshDeviceState = React.useCallback(async () => {
    if (!currentUser) return;
    try {
      const dId = currentUser.deviceId || "SS";
      const response = await apiPollDeviceState(currentUser.email, dId) as any;
      
      if (response.last_seen) {
        const lastSeen = new Date(response.last_seen);
        const now = new Date();
        const diffInSeconds = (now.getTime() - lastSeen.getTime()) / 1000;
        
        if (diffInSeconds < 20) {
          setWifiStrength("strong");
        } else if (diffInSeconds < 60) {
          setWifiStrength("medium");
        } else if (diffInSeconds < 180) {
          setWifiStrength("weak");
        } else {
          setWifiStrength("disconnected");
        }
      } else {
        setWifiStrength("disconnected");
      }

      if (response.weights && Array.isArray(response.weights)) {
        const isAllZero = response.weights.length >= 4 && response.weights.slice(0, 4).every(w => Number(w) === 0);
        if (!isAllZero) {
          const rawWeights = response.weights_raw || [];
          setScentSlots(prev => prev.map((slot, index) => {
            const weightPercent = response.weights![index];
            let estimatedGrams = (weightPercent / 100) * 75.6 + 19.8;
            const weightGrams = rawWeights[index] !== undefined ? rawWeights[index] : estimatedGrams;
            return { 
              ...slot, 
              remaining: weightPercent !== undefined ? Math.min(100, Math.max(0, Math.round(weightPercent))) : slot.remaining,
              weightGrams: weightGrams
            };
          }));
        }
      }

      if (response.mapping && typeof response.mapping === "object") {
        const SCENT_KIND_MAP: Record<number, { name: string; color: string }> = {
          1: { name: "시트러스", color: "bg-orange-500" },
          2: { name: "센달우드", color: "bg-amber-700" },
          3: { name: "패츌리", color: "bg-green-700" },
          4: { name: "페퍼민트", color: "bg-teal-500" },
          5: { name: "라벤더", color: "bg-violet-500" },
          6: { name: "바닐라", color: "bg-orange-300" },
          7: { name: "무향(물)", color: "bg-cyan-500" },
        };

        setScentSlots(prev => {
          let hasChange = false;
          const next = prev.map(slot => {
            const rawKind = response.mapping![slot.id.toString()] ?? response.mapping![slot.id];
            const kindCode = Number(rawKind);
            if (kindCode && SCENT_KIND_MAP[kindCode]) {
              const target = SCENT_KIND_MAP[kindCode];
              if (slot.name !== target.name || slot.color !== target.color) {
                hasChange = true;
                return { ...slot, name: target.name, color: target.color };
              }
            }
            return slot;
          });

          if (hasChange) {
            try {
              localStorage.setItem("smart_diffuser_scent_slots", JSON.stringify(next));
            } catch (e) {}
            return next;
          }
          return prev;
        });
      }

      if (response.intensity !== undefined && (Date.now() - lastIntensityUpdateTimeRef.current > 1500)) {
        setIntensity(response.intensity);
      }

      if (response.volume !== undefined && (Date.now() - lastVolumeUpdateTimeRef.current > 1500)) {
        setVolume(response.volume);
      }

      if (response.timer_enabled !== undefined) setTimerEnabled(response.timer_enabled);
      if (response.timer_start !== undefined) setTimerStart(response.timer_start);
      if (response.timer_end !== undefined) setTimerEnd(response.timer_end);

      if (response.db_level !== undefined) {
        setDbChange(response.db_level - lastDbLevelRef.current);
        setDbLevel(response.db_level);
        lastDbLevelRef.current = response.db_level;
      }
      if (response.db_avg !== undefined) setDbAvg(response.db_avg);
      if (response.db_stddev !== undefined) setDbStdDev(response.db_stddev);
      if (response.noise_type !== undefined) setNoiseType(response.noise_type);
      else if (response.db_stddev !== undefined) {
        setNoiseType(response.db_stddev > 8.0 ? "불규칙적(대화/활동)" : "안정적(음악/정적)");
      }

      if (response.is_spike !== undefined) {
        setIsSpike(Boolean(response.is_spike));
      } else if (response.db_level !== undefined) {
        setIsSpike(Math.abs(response.db_level - lastDbLevelRef.current) >= 15 || response.db_level >= 75);
      }

      if (response.status !== undefined) {
        setDeviceStatus(response.status);
      }

      const isRecentlyModeChanged = (Date.now() - lastModeChangeTimeRef.current) < 15000;

      if (response.active_mode === "weather") {
        setActiveMode("weather");
        setIsDiffuserOn(true);
      } else if (response.active_mode === "manual") {
        setActiveMode(prevMode => {
          if (prevMode === "voice" || prevMode === "ai" || prevMode === "noise" || (prevMode === "weather" && isRecentlyModeChanged)) {
            return prevMode;
          }
          setIsDiffuserOn(true);
          return "manual";
        });
      } else if (response.active_mode === "ready" || response.active_mode === "off") {
        setActiveMode(prevMode => {
          if (isRecentlyModeChanged && (prevMode === "weather" || prevMode === "ai" || prevMode === "manual" || prevMode === "noise")) {
            return prevMode;
          }
          if (prevMode === "weather" || prevMode === "manual") {
            setIsDiffuserOn(false);
            return null;
          }
          return prevMode;
        });
      }

      if (response.music !== undefined && response.music > 0) {
        setCurrentMusicCode(Number(response.music));
      }

      if (response.active_scent !== undefined && response.active_scent > 0) {
        setCurrentScent(String(response.active_scent));
      } else if (response.spray !== undefined && response.spray > 0 && response.spray !== 90) {
        setCurrentScent(String(response.spray));
      }

      if (response.led_br !== undefined && (Date.now() - lastLedUpdateTimeRef.current > 5000)) {
        const brValueRaw = response.led_br;
        const brPercent = Math.round((brValueRaw / 255) * 100);
        
        setIsLedOn(brValueRaw > 0);
        
        if (brValueRaw > 0) {
            setLedBrightness(brPercent);
            
            if (response.led_r !== undefined && response.led_g !== undefined && response.led_b !== undefined) {
              const hex = `#${response.led_r.toString(16).padStart(2, '0')}${response.led_g.toString(16).padStart(2, '0')}${response.led_b.toString(16).padStart(2, '0')}`;
              if (hex !== "#000000") {
                setLedColor(hex.toUpperCase());
              }
            }
        }
      }

      if (response.spray !== undefined && response.spray > 0 && response.spray !== 90) {
        setCurrentScent(String(response.spray));
        setIsDiffuserOn(true);
      }
      
      if (response.music !== undefined) {
        setCurrentMusicCode(response.music);
      }

      if (
        response.music_tracks &&
        response.music_tracks.includes("_") &&
        response.music_tracks !== currentUser.musicTracks
      ) {
        updateLocalUser({ musicTracks: response.music_tracks });
      }
    } catch (err) {
      console.error("Failed to refresh device state", err);
    }
  }, [currentUser, updateLocalUser]);

  const sendDeviceData = React.useCallback(async (action: string, value: number, region?: string, diaryText?: string, ledData?: { r: number, g: number, b: number, br: number }) => {
    if (!currentUser) return { success: false, message: "로그인이 필요합니다." };
    
    if (action === "SET_LED") lastLedUpdateTimeRef.current = Date.now();
    if (action === "SET_VOLUME") lastVolumeUpdateTimeRef.current = Date.now();

    const dId = currentUser.deviceId || "SS";
    const response = await apiSendData({
      email: currentUser.email,
      action,
      value,
      region: region || currentUser.region || "서울",
      deviceId: dId,
      diaryText,
      ledData
    });

    if (response.result === "SUCCESS" || response.result_text || response.spray !== undefined) {
      if (action === "AI_WEATHER" || action === "AI_EMOTION" || action === "AI_REPLAY" || action === "noise" || action === "MANUAL") {
        lastModeChangeTimeRef.current = Date.now();
        setIsDiffuserOn(true);
        if (action === "AI_WEATHER") setActiveMode("weather");
        if (action === "AI_EMOTION" || action === "AI_REPLAY") setActiveMode("ai");
        if (action === "noise") setActiveMode("noise");
        if (action === "MANUAL") setActiveMode("manual");
      }

      if (action === "MENU_STOP" || response.spray === 0) {
        setIsDiffuserOn(false);
        setActiveMode(null);
        setCurrentScent("0");
        setCurrentMusicCode(0);
      } else if (response.spray !== undefined && response.spray > 0 && response.spray !== 90) {
        setCurrentScent(String(response.spray));
        setIsDiffuserOn(true);
      } else if (action === "AI_WEATHER") {
        setCurrentScent("1");
      }

      if (response.music !== undefined && response.music > 0) {
        setCurrentMusicCode(Number(response.music));
      }
      
      const r = response.led_r ?? response.led_dict?.led_r;
      const g = response.led_g ?? response.led_dict?.led_g;
      const b = response.led_b ?? response.led_dict?.led_b;

      if (r !== undefined && g !== undefined && b !== undefined) {
        const hex = `#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${b.toString(16).padStart(2, '0')}`.toUpperCase();
        if (hex !== "#000000") {
          setLedColor(hex);
          setIsLedOn(true);
        }
      }

      setTimeout(() => refreshDeviceState(), 500);
      return { 
        ...response,
        success: true, 
        message: response.result_text || response.message || "명령 전송 완료",
      };
    }
    return { ...response, success: false, message: response.message || "명령 전송 실패" };
  }, [currentUser, refreshDeviceState]);

  const updateScentSlot = (id: number, slot: Partial<ScentSlot>) => {
    setScentSlots(prev => {
      const next = prev.map(s => s.id === id ? { ...s, ...slot } : s);
      try {
        localStorage.setItem("smart_diffuser_scent_slots", JSON.stringify(next));
      } catch (e) {}
      return next;
    });
  };

  const calibrateWeight = async () => {
    if (!currentUser) return false;
    try {
      const response = await apiSendData({
        email: currentUser.email,
        action: "CALIBRATE",
        value: 0,
        region: "CALIBRATE",
        deviceId: currentUser.deviceId || "SS"
      });
      return response.result === "SUCCESS";
    } catch (e) {
      return false;
    }
  };

  useEffect(() => {
    if (currentUser) {
      refreshDeviceState();
      // 실시간 기기 디스플레이 제어 변경 사항 동기화를 위해 5초 빠른 폴링(Fast Polling) 적용
      const interval = setInterval(refreshDeviceState, 5000);
      return () => clearInterval(interval);
    }
  }, [currentUser, refreshDeviceState]);

  return (
    <DeviceContext.Provider value={{
      scentSlots, updateScentSlot, refreshDeviceState,
      wifiStrength, setWifiStrength,
      volume, handleVolumeChange,
      intensity, handleIntensityChange,
      activeTimerMinutes, handleTimerChange,
      timerEnabled, timerStart, timerEnd, updateTimerSettings,
      currentScent, isDiffuserOn, setIsDiffuserOn, 
      ledColor, ledBrightness, isLedOn,
      activeMode, setActiveMode,
      isDeviceActionLoading,
      sendDeviceData,
      calibrateWeight,
      dbLevel,
      dbAvg,
      dbStdDev,
      noiseType,
      dbChange,
      deviceStatus,
      currentMusicCode,
      isSpike
    }}>
      {children}
    </DeviceContext.Provider>
  );
};

export const useDevice = () => {
  const context = useContext(DeviceContext);
  if (context === undefined) {
    throw new Error("useDevice must be used within a DeviceProvider");
  }
  return context;
};
