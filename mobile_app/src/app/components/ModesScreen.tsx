import React, { useState, useEffect } from "react";
import { Link } from "react-router"; 
import {
  CloudRain,
  Wind,
  Brain,
  Power,
  SlidersHorizontal,
  Timer,
  Home,
  Droplets,
  Heart,
  Settings,
  Mic,
  BookHeart,
  X,
  ThumbsUp,
  ThumbsDown,
  Activity,
  Music,
  MapPin,
  Sun,
  Volume2,
  Plus,
  Minus,
  Wifi,
  WifiOff,
  Loader2,
  AlertTriangle,
  Zap,
  Sparkles,
  ShieldAlert,
} from "lucide-react";
import { BottomNav } from "./BottomNav";
import { motion, AnimatePresence } from "motion/react"; 
import { useAuth } from "../store/AuthContext";
import { useDevice } from "../store/DeviceContext";
import { apiSendVoiceData } from "../utils/api";

import { ModeButton } from "./ModeButton";
import { TRACKS } from "../utils/tracks";
import { toast } from "sonner";

const REGION_MAP: Record<string, string> = {
  seoul: "서울",
  goyang: "고양",
  gwacheon: "과천",
  gwangmyeong: "광명",
  gwangju: "광주",
  guri: "구리",
  gunpo: "군포",
  gimpo: "김포",
  namyangju: "남양주",
  dongducheon: "동두천",
  bucheon: "부천",
  seongnam: "성남",
  suwon: "수원",
  siheung: "시흥",
  ansan: "안산",
  anyang: "안양",
  yangju: "양주",
  yangpyeong: "양평",
  yongin: "용인",
  uijeongbu: "의정부",
  icheon: "이천",
  paju: "파주",
  pyeongtaek: "평택",
  pocheon: "포천",
  hwaseong: "화성",
  gangwon: "강원",
  gyeongnam: "경남",
  gyeongbuk: "경북",
  gwangju_jeolla: "광주(전라)",
  daegu: "대구",
  daejeon: "대전",
  busan: "부산",
  sejong: "세종",
  ulleungdo: "울릉도",
  ulsan: "울산",
  incheon: "인천",
  jeonnam: "전남",
  jeonbuk: "전북",
  jeju: "제주",
  chungnam: "충남",
  chungbuk: "충북",
};

export function ModesScreen() {
  const { currentUser } = useAuth();
  const { 
    scentSlots, 
    wifiStrength, 
    sendDeviceData, 
    volume, 
    handleVolumeChange, 
    intensity,
    handleIntensityChange,
    activeTimerMinutes,
    handleTimerChange,
    isDeviceActionLoading,
    refreshDeviceState,
    dbLevel,
    dbAvg,
    dbStdDev,
    noiseType,
    dbChange,
    deviceStatus,
    currentScent,
    isDiffuserOn: isOn,
    setIsDiffuserOn: setIsOn,
    activeMode,
    setActiveMode,
    currentMusicCode,
    isSpike
  } = useDevice();

  const regionKorean = currentUser?.region ? (REGION_MAP[currentUser.region] || currentUser.region) : "서울";
  const containerRef = React.useRef<HTMLDivElement>(null);
  
  // 타이머 모달 상태
  const [isTimerModalOpen, setIsTimerModalOpen] = useState(false);
  const [selectedHours, setSelectedHours] = useState(0);
  const [selectedMinutes, setSelectedMinutes] = useState(30);

  const hourScrollRef = React.useRef<HTMLDivElement>(null);
  const minScrollRef = React.useRef<HTMLDivElement>(null);

  React.useEffect(() => {
    if (isTimerModalOpen) {
      setTimeout(() => {
        if (hourScrollRef.current) hourScrollRef.current.scrollTop = selectedHours * 48;
        if (minScrollRef.current) minScrollRef.current.scrollTop = selectedMinutes * 48;
      }, 50);
    }
  }, [isTimerModalOpen, selectedHours, selectedMinutes]);

  const [manualScents, setManualScents] = useState<string[]>([]);
  const [appliedManualScents, setAppliedManualScents] = useState<string[]>([]);
  const [feedback, setFeedback] = useState<"like" | "dislike" | null>(null);
  const [pendingMode, setPendingMode] = useState<
    "manual" | "weather" | "voice" | "ai" | "noise" | null
  >(null);
  const [isVoiceListening, setIsVoiceListening] = useState(false);
  const [loading, setLoading] = useState(false);
  
  const [weatherData, setWeatherData] = useState<{ temp?: string; weather?: string } | null>(null);
  const [voiceResult, setVoiceResult] = useState<string | null>(null);

  const [mediaRecorder, setMediaRecorder] = useState<MediaRecorder | null>(null);

  // 소음 분석 카운트다운 (펌웨어 주기 15초와 대략적 동기화)
  const [analysisCountdown, setAnalysisCountdown] = useState(15);
  
  useEffect(() => {
    let timer: any;
    let pollTimer: any;
    if (isOn && activeMode === "noise") {
      // 5초마다 실시간 dbLevel 및 dbAvg 최신 상태 폴링 (기존 30초 -> 5초 딜레이로 단축)
      refreshDeviceState();
      pollTimer = setInterval(() => {
        refreshDeviceState();
      }, 5000);

      timer = setInterval(() => {
        setAnalysisCountdown(prev => (prev <= 1 ? 15 : prev - 1));
      }, 1000);
    } else {
      setAnalysisCountdown(15);
    }
    return () => {
      clearInterval(timer);
      clearInterval(pollTimer);
    };
  }, [isOn, activeMode, refreshDeviceState]);

  // 현재 분사 중인 향기 이름 및 음악 정보 가져오기 (실시간 동기화)
  const { activeScentName, activeMusicName } = React.useMemo(() => {
    if (!isOn) {
      return { activeScentName: "", activeMusicName: "" };
    }

    const effectiveScent = (!currentScent || currentScent === "0" || currentScent === "90") ? "1" : currentScent;

    // 1. 향기 이름 찾기 (12, 23 등 블렌딩 포함)
    const scentIds = effectiveScent.split("").map(Number);
    const scentNames = scentIds
      .map(id => scentSlots.find(s => s.id === id)?.name)
      .filter(Boolean);
    const finalScentName = scentNames.length > 0 ? scentNames.join(" + ") : "시트러스";

    // 2. 음악 이름 찾기 (기기에서 실시간 재생 중인 곡 또는 매핑 곡)
    let finalMusicName = "재생 안 함";
    if (currentUser?.musicTracks) {
      const userTrackIds = currentUser.musicTracks.split("_");
      
      // 선택된 모든 향기 슬롯의 트랙 목록 수집
      const allSelectedTracks: { trackNum: string; slotId: number; slotName: string }[] = [];
      scentIds.forEach(slotId => {
        const trackIdx = slotId - 1;
        if (trackIdx >= 0 && trackIdx < userTrackIds.length) {
          const rawSlotStr = userTrackIds[trackIdx] || "0";
          const nums = rawSlotStr.split(",").map(n => n.trim()).filter(Boolean);
          const slotName = scentSlots.find(s => s.id === slotId)?.name || `${slotId}번 향`;
          nums.forEach(n => {
            if (n !== "0") allSelectedTracks.push({ trackNum: n, slotId, slotName });
          });
        }
      });

      // 1) 기기에서 실시간 재생 중인 곡(currentMusicCode) 정보가 파싱된 경우 우선 표시
      if (currentMusicCode > 0) {
        const currentTrack = TRACKS.find(t => t.id === `song_${currentMusicCode}`);
        if (currentTrack) {
          const trackIndexInPlaylist = allSelectedTracks.findIndex(t => t.trackNum === String(currentMusicCode));
          if (trackIndexInPlaylist >= 0) {
            finalMusicName = `🎵 ${currentTrack.name} (${trackIndexInPlaylist + 1}/${allSelectedTracks.length}번째 곡)`;
          } else {
            finalMusicName = `🎵 ${currentTrack.name}`;
          }
        }
      }

      // 2) currentMusicCode가 0이거나 미수신인 경우, 선택된 슬롯 중 첫 번째 트랙 표시
      if (finalMusicName === "재생 안 함" && allSelectedTracks.length > 0) {
        const first = allSelectedTracks[0];
        const firstTrack = TRACKS.find(t => t.id === `song_${first.trackNum}`);
        if (firstTrack) {
          finalMusicName = `🎵 ${firstTrack.name} (1/${allSelectedTracks.length}번째 곡)`;
        }
      }
    }

    return { activeScentName: finalScentName, activeMusicName: finalMusicName };
  }, [currentScent, scentSlots, currentUser?.musicTracks, currentMusicCode]);

  // 화면 마운트 및 실시간 기기 음량/상태 3초 간격 빠른 동기화
  React.useEffect(() => {
    refreshDeviceState();
    const interval = setInterval(() => {
      refreshDeviceState();
    }, 3000);
    return () => clearInterval(interval);
  }, [refreshDeviceState]);

  React.useEffect(() => {
    if (pendingMode === "weather") {
      const fetchWeatherPreview = async () => {
        if (!currentUser) return;
        try {
          const result = await sendDeviceData("AI_WEATHER_PREVIEW", 0, regionKorean);
          if (result.success) {
            setWeatherData({ temp: result.temp, weather: result.weather });
          }
        } catch (err) {
          console.error("Failed to fetch weather preview", err);
        }
      };
      fetchWeatherPreview();
    } else if (pendingMode !== null) {
      setWeatherData(null);
    }
  }, [pendingMode, currentUser, sendDeviceData, regionKorean]);

  const isInitialized = React.useRef(false);

  React.useEffect(() => {
    if (!isInitialized.current && scentSlots.length > 0) {
      if (currentScent && currentScent !== "0" && currentScent !== "90") {
        const ids = currentScent.split("").map(Number);
        const names = ids.map(id => scentSlots.find(s => s.id === id)?.name).filter(Boolean) as string[];
        if (names.length > 0) {
          setManualScents(names);
          setAppliedManualScents(names);
          isInitialized.current = true;
          return;
        }
      }
      setManualScents([scentSlots[0].name]);
      setAppliedManualScents([scentSlots[0].name]);
      isInitialized.current = true;
    }
  }, [scentSlots, currentScent]);

  const handlePowerOff = async () => {
    setLoading(true);
    setIsOn(false);
    setActiveMode(null);
    setPendingMode(null);
    setIsVoiceListening(false);
    try {
      await sendDeviceData("MENU_STOP", 0);
    } catch (err) {
      console.error("Power off failed", err);
    } finally {
      setLoading(false);
    }
  };

  const toggleVoiceRecording = async () => {
    if (loading) return; // 전송 중일 때는 클릭 방지

    if (!isVoiceListening) {
      try {
        if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
          throw new Error("마이크를 지원하지 않는 환경입니다.");
        }

        const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
        
        if (typeof MediaRecorder === 'undefined') {
          alert("MediaRecorder를 지원하지 않는 브라우저입니다.");
          stream.getTracks().forEach(track => track.stop());
          return;
        }

        // 지원되는 MIME 타입 확인 (iOS Safari는 audio/mp4, 나머지는 audio/webm 우선)
        let mimeType = "audio/webm";
        const types = ["audio/webm", "audio/mp4", "audio/ogg", "audio/wav"];
        for (const type of types) {
          if (MediaRecorder.isTypeSupported(type)) {
            mimeType = type;
            break;
          }
        }

        console.log(`[Voice] Selected MimeType: ${mimeType}`);
        const recorder = new MediaRecorder(stream, { mimeType });
        const chunks: Blob[] = [];

        recorder.ondataavailable = (e) => {
          if (e.data.size > 0) chunks.push(e.data);
        };

        recorder.onstop = async () => {
          const actualMimeType = recorder.mimeType || mimeType;
          const audioBlob = new Blob(chunks, { type: actualMimeType });
          
          if (audioBlob.size < 1000) {
             alert("음성이 너무 짧거나 녹음되지 않았습니다.");
             stream.getTracks().forEach(track => track.stop());
             setIsVoiceListening(false);
             return;
          }

          const reader = new FileReader();
          reader.readAsDataURL(audioBlob);
          
          reader.onloadend = async () => {
            const base64Audio = (reader.result as string).split(',')[1];
            setLoading(true);
            setVoiceResult("분석 중...");
            try {
              const res = await apiSendVoiceData(base64Audio, currentUser?.deviceId, actualMimeType);
              
              if (res.transcript) {
                // 음성 인식 결과(transcript)와 AI 분석 결과(message)를 함께 보여줍니다.
                const messageText = res.message ? res.message.replace("음성 인식 성공: ", "") : "";
                setVoiceResult(`인식 내용: ${res.transcript}\n${messageText}`);
                
                if (res.spray && res.spray > 0) {
                  if (!isOn) setIsOn(true);
                  setActiveMode("voice");
                  refreshDeviceState();
                }
              } else {
                setVoiceResult("인식을 실패했습니다.");
                alert(res.message || "음성을 인식하지 못했습니다.");
              }
            } catch (err) {
              console.error("[Voice] Processing failed", err);
              setVoiceResult("서버 통신 중 오류");
            } finally {
              setLoading(false);
            }
          };
          
          stream.getTracks().forEach(track => track.stop());
        };

        recorder.start();
        setMediaRecorder(recorder);
        setIsVoiceListening(true);
        setVoiceResult("듣고 있어요...");
      } catch (err) {
        console.error("[Voice] Microphone access error:", err);
        alert(`마이크 연결 실패: ${err instanceof Error ? err.message : "권한을 확인해 주세요."}`);
      }
    } else {
      if (mediaRecorder && mediaRecorder.state !== "inactive") {
        mediaRecorder.stop();
        setIsVoiceListening(false);
      }
    }
  };

  const handleStartMode = async () => {
    const modeToStart = pendingMode || activeMode;
    if (!modeToStart) return;

    if (modeToStart === "voice") {
      setActiveMode("voice");
      setPendingMode(null);
      if (!isOn) setIsOn(true);
      containerRef.current?.scrollTo({ top: 0, behavior: 'smooth' });
      return;
    }

    setLoading(true);
    let action = "";
    let value = 0;
    let region = currentUser?.region || "";

    if (modeToStart === "weather") {
      action = "AI_WEATHER";
      region = regionKorean;
    } else if (modeToStart === "ai") {
      action = "AI_REPLAY";
      region = "편안함";
    } else if (modeToStart === "noise") {
      action = "noise";
      value = 0; // 이제 하드웨어 마이크 값을 서버 DB에서 직접 읽으므로 0 전달
    } else if (modeToStart === "manual") {
      const selectedIds = scentSlots
        .filter(s => manualScents.includes(s.name))
        .map(s => s.id)
        .sort((a, b) => a - b);
      
      action = "MANUAL";
      value = selectedIds.length > 0 ? parseInt(selectedIds.join("")) : 1;
    }

    try {
      const result = await sendDeviceData(action, value, region);
      if (result.success) {
        if (pendingMode) setActiveMode(pendingMode);
        if (result.temp || result.weather) {
          setWeatherData({ temp: result.temp, weather: result.weather });
        }
        setAppliedManualScents(manualScents);
        setPendingMode(null);
        setIsVoiceListening(false);
        if (!isOn) setIsOn(true);
        
        // alert("명령이 전달되었습니다. 잠시만 기다려주세요.");
      } else {
        alert(result.message || "모드 시작 실패");
      }
    } catch (err) {
      alert("서버 통신 중 오류가 발생했습니다.");
    } finally {
      setLoading(false);
      containerRef.current?.scrollTo({ top: 0, behavior: 'smooth' });
    }
  };

  const handleManualScentSelect = (scentName: string) => {
    setManualScents((prev) => {
      if (prev.includes(scentName)) {
        if (prev.length === 1) return prev;
        return prev.filter((s) => s !== scentName);
      }
      if (prev.length >= 2) {
        return [prev[1], scentName];
      }
      return [...prev, scentName];
    });
  };

  const handleFeedback = async (type: "like" | "dislike") => {
    console.log("handleFeedback called with:", type);
    setFeedback(type);
    if (!currentUser) return;
    
    // 백엔드 FEEDBACK 액션 형식에 맞게 데이터 준비
    // value: 1 (좋아요), -1 (별로예요)
    const val = type === "like" ? 1 : -1;
    
    // 상황(Context) 구성
    let context = "";
    if (activeMode === "ai") {
      context = "AI_EMOTION"; // 백엔드에서 감정 태그와 결합됨
    } else if (activeMode === "weather") {
      context = "AI_WEATHER"; // 백엔드에서 날씨/시간대와 결합됨
    } else if (activeMode === "voice") {
      context = "AI_VOICE";
    } else {
      context = "NOISE_ANALYSIS";
    }

    // 현재 분사 중인 향기 ID (기본값은 모드별 기본값)
    const scentId = currentScent || (activeMode === "weather" ? "1" : activeMode === "ai" ? "1" : activeMode === "voice" ? "1" : "4");
    
    try {
      if (type === "dislike") {
        toast("새로운 향 교체 대기 중 (10초 잔향 소거 진행 중)", { icon: "🌬️" });
        await sendDeviceData("DISLIKE", Number(scentId) || 1, `${scentId}_${context}`);
      } else {
        toast.success("현재 향기 만족도가 반영되었습니다! (+2)");
        await sendDeviceData("LIKE", Number(scentId) || 1, `${scentId}_${context}`);
      }
    } catch (err) {
      console.error("Feedback failed", err);
    }
  };

  const modes = [
    {
      id: "manual",
      title: "수동 모드",
      description: "원하는 향기를 직접 설정",
      icon: <Wind className="w-6 h-6" />,
      color: "bg-orange-100 dark:bg-orange-900/30 text-orange-700 dark:text-orange-400",
      activeColor: "bg-orange-500 text-white",
      bgGradient: "from-orange-100 to-amber-50 dark:from-orange-900/40 dark:to-amber-900/20",
      accentColor: "text-orange-500",
      borderColor: "border-orange-500",
      dotColor: "bg-orange-500",
    },
    {
      id: "weather",
      title: "날씨 모드",
      description: "현재 날씨에 맞춘 향기 추천",
      icon: <CloudRain className="w-6 h-6" />,
      color: "bg-blue-100 dark:bg-blue-900/30 text-blue-700 dark:text-blue-400",
      activeColor: "bg-blue-500 text-white",
      bgGradient: "from-blue-100 to-indigo-50 dark:from-blue-900/40 dark:to-indigo-900/20",
      accentColor: "text-blue-500",
      borderColor: "border-blue-500",
      dotColor: "bg-blue-500",
    },
    {
      id: "voice",
      title: "음성 모드",
      description: "음성 명령으로 간편하게 제어",
      icon: <Mic className="w-6 h-6" />,
      color: "bg-emerald-100 dark:bg-emerald-900/30 text-emerald-700 dark:text-emerald-400",
      activeColor: "bg-emerald-500 text-white",
      bgGradient: "from-emerald-100 to-teal-50 dark:from-emerald-900/40 dark:to-teal-900/20",
      accentColor: "text-emerald-500",
      borderColor: "border-emerald-500",
      dotColor: "bg-emerald-500",
    },
    {
      id: "ai",
      title: "AI 추천",
      description: "나의 감정에 맞춘 향기 추천",
      icon: <Brain className="w-6 h-6" />,
      color: "bg-purple-100 dark:bg-purple-900/30 text-purple-700 dark:text-purple-400",
      activeColor: "bg-purple-500 text-white",
      bgGradient: "from-purple-100 to-fuchsia-50 dark:from-purple-900/40 dark:to-fuchsia-900/20",
      accentColor: "text-purple-500",
      borderColor: "border-purple-500",
      dotColor: "bg-purple-500",
    },
    {
      id: "noise",
      title: "소음 분석 모드",
      description: "주변 소음을 분석하여 맞춤형 휴식 향기 제공",
      icon: <Activity className="w-6 h-6" />,
      color: "bg-rose-100 dark:bg-rose-900/30 text-rose-700 dark:text-rose-400",
      activeColor: "bg-rose-500 text-white",
      bgGradient: "from-rose-100 to-orange-50 dark:from-rose-900/40 dark:to-orange-900/20",
      accentColor: "text-rose-500",
      borderColor: "border-rose-500",
      dotColor: "bg-rose-500",
    },
  ];

  const currentModeData =
    modes.find((m) => m.id === activeMode) || {
      id: "none",
      title: "모드 미선택",
      description: "모드를 선택해주세요",
      icon: <Wind className="w-6 h-6" />,
      color: "bg-gray-100 dark:bg-gray-800 text-gray-400",
      activeColor: "bg-gray-500 text-white",
      bgGradient: "from-gray-100 to-gray-50 dark:from-gray-900 dark:to-gray-800",
      accentColor: "text-gray-500",
      borderColor: "border-gray-500",
      dotColor: "bg-gray-500",
    };

  return (
    <div key="modes-screen" ref={containerRef} className="flex-1 flex flex-col bg-white dark:bg-gray-950 overflow-y-auto relative pb-32 min-h-screen transition-colors duration-300">
      <header className="px-6 pt-12 pb-4 flex justify-between items-center z-10 sticky top-0 bg-white/80 dark:bg-gray-950/80 backdrop-blur-md transition-colors duration-300">
        <div>
          <div className="flex items-center gap-2">
            <h1 className="text-2xl font-bold tracking-tight text-gray-900 dark:text-white">
              Smart Diffuser
            </h1>
          </div>
          <p className="text-sm font-medium text-gray-500 dark:text-gray-400 flex items-center gap-1.5 mt-1">
            <span
              className={`w-2 h-2 rounded-full ${isOn && activeMode ? "bg-green-500" : "bg-gray-300 dark:bg-gray-600"}`}
            ></span>
            {isOn && activeMode ? "연결됨 - 발향 중" : "대기 중"}
          </p>
        </div>
        <button
          onClick={() => {
            handlePowerOff();
          }}
          disabled={loading}
          className={`w-12 h-12 rounded-full flex items-center justify-center transition-all shadow-sm border ${isOn || activeMode !== null ? "bg-gray-900 dark:bg-gray-100 text-white dark:text-gray-900 border-gray-900 dark:border-gray-100" : "bg-white dark:bg-gray-800 text-gray-600 dark:text-gray-300 border-gray-200 dark:border-gray-700"} disabled:opacity-50`}
        >
          {loading ? <Loader2 className="w-5 h-5 animate-spin" /> : <Power className="w-5 h-5" />}
        </button>
      </header>

      <AnimatePresence>
        {(pendingMode === "weather" || (activeMode === "weather" && pendingMode === null)) && (
          <motion.div
            initial={{ opacity: 0, height: 0, y: -20 }}
            animate={{ opacity: 1, height: "auto", y: 0 }}
            exit={{ opacity: 0, height: 0, y: -20 }}
            className="px-6 pb-2 overflow-hidden z-10"
          >
            <div className="bg-gradient-to-br from-blue-50 to-indigo-50 dark:from-blue-900/20 dark:to-indigo-900/20 border border-blue-100 dark:border-blue-800/50 shadow-sm rounded-2xl p-5 flex items-center justify-between transition-colors duration-300">
              <div className="flex flex-col gap-1">
                <div className="flex items-center gap-1.5 text-blue-600 dark:text-blue-400 mb-1">
                  <MapPin className="w-4 h-4" />
                  <span className="text-xs font-bold uppercase tracking-wider">현재 위치</span>
                </div>
                <h3 className="text-lg font-extrabold text-gray-900 dark:text-white">{regionKorean}</h3>
                <p className="text-sm font-medium text-gray-600 dark:text-gray-300 mt-1">
                  {weatherData?.temp && weatherData?.weather 
                    ? `${weatherData.temp}°C · ${weatherData.weather}` 
                    : "날씨 정보를 가져오는 중..."}
                </p>
              </div>
              <div className="flex flex-col items-center">
                <div className="relative">
                  <Sun className="w-12 h-12 text-orange-400 absolute -top-2 -right-2 opacity-80" />
                  <CloudRain className="w-14 h-14 text-blue-500 relative z-10" />
                </div>
              </div>
            </div>
          </motion.div>
        )}
      </AnimatePresence>

      <div className="px-6 py-4">
        <motion.div
          layout
          className={`w-full min-h-[400px] rounded-[2rem] p-5 sm:p-6 relative overflow-hidden bg-gradient-to-br ${isOn && activeMode ? currentModeData.bgGradient : "from-gray-50 to-gray-100 dark:from-gray-900 dark:to-gray-800"} transition-colors duration-700 ease-in-out border border-gray-200 dark:border-gray-800 shadow-md`}
        >
          {isOn && activeMode && (
            <div className="absolute inset-0 flex items-center justify-center pointer-events-none">
              <motion.div
                animate={{
                  scale: [1, 1.2, 1],
                  opacity: [0.3, 0.6, 0.3],
                  rotate: [0, 90, 180],
                }}
                transition={{
                  duration: 8,
                  repeat: Infinity,
                  ease: "easeInOut",
                }}
                className={`w-48 h-48 rounded-full blur-3xl ${activeMode === "weather" ? "bg-blue-300 dark:bg-blue-500/30" : activeMode === "voice" ? "bg-emerald-300 dark:bg-emerald-500/30" : activeMode === "ai" ? "bg-purple-300 dark:bg-purple-500/30" : activeMode === "noise" ? "bg-rose-300 dark:bg-rose-500/30" : "bg-orange-300 dark:bg-orange-500/30"}`}
              />
            </div>
          )}

          <div className="relative z-10 flex flex-col h-full justify-between">
            <div className="flex justify-between items-start">
              <div className="bg-white/80 dark:bg-gray-900/80 backdrop-blur-md px-4 py-2 rounded-full inline-flex items-center gap-2 shadow-sm border border-gray-200/50 dark:border-gray-700/50 transition-colors duration-300">
                {isOn && activeMode ? (
                  <>
                    <div
                      className={`w-6 h-6 rounded-full flex items-center justify-center ${currentModeData.color} dark:bg-opacity-20`}
                    >
                      {currentModeData.icon}
                    </div>
                    <span className="text-sm font-semibold text-gray-800 dark:text-gray-200">
                      {activeMode === "noise" ? `${noiseType} (${dbLevel}dB)` : currentModeData.title}
                    </span>
                  </>
                ) : isOn && !activeMode ? (
                  <>
                    <div className="w-6 h-6 rounded-full flex items-center justify-center bg-gray-200 dark:bg-gray-700 text-gray-600 dark:text-gray-400">
                      <Wind className="w-4 h-4" />
                    </div>
                    <span className="text-sm font-semibold text-gray-800 dark:text-gray-300">
                      모드 대기 중
                    </span>
                  </>
                ) : (
                  <>
                    <div className="w-6 h-6 rounded-full flex items-center justify-center bg-gray-200 dark:bg-gray-700 text-gray-600 dark:text-gray-400">
                      <Power className="w-4 h-4" />
                    </div>
                    <span className="text-sm font-semibold text-gray-800 dark:text-gray-300">
                      디퓨저 꺼짐
                    </span>
                  </>
                )}
              </div>
            </div>

            <div className="flex-1 flex flex-col items-center justify-center pt-6 sm:pt-8">
              <div className="w-32 h-32 sm:w-40 sm:h-40 bg-white dark:bg-gray-900 rounded-full mx-auto shadow-md border border-gray-200 dark:border-gray-800 mb-4 sm:mb-6 flex items-center justify-center transition-colors duration-300">
                <Droplets className="w-10 h-10 sm:w-12 sm:h-12 text-gray-300 dark:text-gray-600" />
              </div>
              <div className="h-auto min-h-[120px] flex flex-col items-center justify-start w-full">
                <h2 className="text-xl sm:text-2xl font-bold text-gray-900 dark:text-white tracking-tight drop-shadow-sm transition-colors mb-2 text-center">
                  {isOn && activeMode
                    ? activeMode === "weather"
                      ? "날씨 모드"
                      : activeMode === "voice"
                        ? (isVoiceListening ? "듣고 있어요..." : "음성 모드")
                        : activeMode === "ai"
                          ? "AI 추천"
                          : activeMode === "noise"
                            ? "소음 분석 모드"
                            : `수동 발향 모드`
                    : (currentScent && currentScent !== "0" && currentScent !== "90") ? "디퓨저 발향 중" : "디퓨저 꺼짐"}
                </h2>

                {isOn && activeMode && activeMode === "voice" && (
                  <div className="flex flex-col items-center gap-4 mt-2 sm:mt-4 w-full max-w-xs mx-auto">
                    <div className="relative w-full flex justify-center py-4">
                      {isVoiceListening && (
                        <div className="absolute inset-0 flex items-center justify-center">
                          <motion.div
                            animate={{
                              scale: [1, 1.5, 1],
                              opacity: [0.3, 0.1, 0.3],
                            }}
                            transition={{
                              duration: 2,
                              repeat: Infinity,
                              ease: "easeInOut",
                            }}
                            className="w-32 h-32 bg-red-400 rounded-full blur-2xl"
                          />
                        </div>
                      )}
                      <button
                        onClick={toggleVoiceRecording}
                        disabled={loading}
                        className={`relative z-10 flex flex-col items-center justify-center w-24 h-24 rounded-full border-4 shadow-xl transition-all duration-300 ${
                          isVoiceListening 
                            ? "bg-red-500 text-white border-red-200 scale-110" 
                            : "bg-gray-900 dark:bg-white text-white dark:text-gray-900 border-gray-700 dark:border-gray-200 hover:scale-105"
                        } disabled:opacity-50`}
                      >
                        <Mic className={`w-8 h-8 mb-1 ${isVoiceListening ? "animate-pulse" : ""}`} />
                        <span className="text-[10px] font-black uppercase tracking-tighter">
                          {isVoiceListening ? "말씀해 주세요" : "음성 명령 시작"}
                        </span>
                      </button>
                    </div>
                    
                    <div className="w-full text-center min-h-[40px]">
                      {voiceResult ? (
                        <motion.p 
                          initial={{ opacity: 0, y: 10 }}
                          animate={{ opacity: 1, y: 0 }}
                          className="text-sm font-bold text-blue-600 dark:text-blue-400 bg-blue-50 dark:bg-blue-900/30 px-4 py-2 rounded-2xl border border-blue-100 dark:border-blue-800/50 inline-block whitespace-pre-wrap text-left"
                        >
                          {voiceResult}
                        </motion.p>
                      ) : (
                        <p className="text-xs font-medium text-gray-500 dark:text-gray-400">
                          {isVoiceListening ? "원하시는 향기나 기분을 말씀해 보세요" : "버튼을 누르고 말씀해 주세요"}
                        </p>
                      )}
                    </div>
                  </div>
                )}

                {isOn && activeMode && (
                  <div className="flex flex-col gap-3 w-full max-w-xs mx-auto">
                    {activeMode === "noise" && (
                      <div className="flex flex-col items-center w-full mb-2">
                        <div className="bg-white/50 dark:bg-gray-900/50 backdrop-blur-md rounded-2xl p-4 border border-white/30 dark:border-gray-800/30 w-full mb-3 shadow-xs">
                          <div className="flex justify-between items-center mb-3 relative">
                            <span className="text-[10px] font-black text-gray-400 dark:text-gray-500 uppercase tracking-widest">실시간 소음 분석 지표</span>
                            {dbChange !== 0 && (
                              <span className={`text-[10px] font-extrabold px-2 py-0.5 rounded-full ${dbChange > 0 ? "bg-rose-100 text-rose-600 dark:bg-rose-950/80 dark:text-rose-400" : "bg-blue-100 text-blue-600 dark:bg-blue-950/80 dark:text-blue-400"}`}>
                                {dbChange > 0 ? "↑" : "↓"} {Math.abs(dbChange)}dB
                              </span>
                            )}
                          </div>
                          
                          <div className="grid grid-cols-2 gap-2 text-center border-b border-gray-100 dark:border-gray-800/60 pb-3 mb-2">
                            <div className="flex flex-col items-center border-r border-gray-100 dark:border-gray-800/60 pr-2">
                              <span className="text-[10px] text-gray-500 font-bold uppercase tracking-tighter">실시간 소음</span>
                              <span className="text-2xl font-black text-rose-600 dark:text-rose-400">{dbLevel > 0 ? dbLevel : 45} <span className="text-xs font-bold text-gray-400">dB</span></span>
                            </div>
                            <div className="flex flex-col items-center pl-2">
                              <span className="text-[10px] text-gray-500 font-bold uppercase tracking-tighter">평균 소음</span>
                              <span className="text-2xl font-black text-gray-900 dark:text-white">{dbAvg > 0 ? dbAvg.toFixed(1) : 42.0} <span className="text-xs font-bold text-gray-400">dB</span></span>
                            </div>
                          </div>

                          {/* 소음 스파이크 (Fast-track) 발생 시 B2B 알림 팝업/태그 */}
                          {(isSpike || Math.abs(dbChange) >= 15 || dbLevel >= 75 || deviceStatus.includes("Fast-track")) && (
                            <div className="mt-2 p-2.5 bg-amber-500/15 border border-amber-500/30 rounded-xl flex items-center justify-center gap-2 text-amber-800 dark:text-amber-200 font-black text-xs animate-pulse">
                              <Zap className="w-4 h-4 text-amber-500" />
                              <span>⚡ 공간 소음 급변 감지 - Fast-track 분위기 전환 중</span>
                            </div>
                          )}
                        </div>

                        <motion.div 
                          animate={{ opacity: [0.7, 1, 0.7] }}
                          transition={{ duration: 3, repeat: Infinity }}
                          className="text-center px-2 mb-3"
                        >
                          <p className="text-[13px] font-bold text-rose-600 dark:text-rose-400 break-keep leading-relaxed">
                            {deviceStatus === "소음 분석 중..." 
                              ? "현재 매장 주변 소음을 정밀 분석 중입니다." 
                              : Math.abs(dbChange) > 5 
                                ? `주변 소음이 ${dbChange > 0 ? '커졌습니다' : '작아졌습니다'}. 분석 후 곧 향기 및 BGM을 조절합니다.` 
                                : <>{noiseType} 환경에 맞춘 최적의 향기를 유지 중입니다.</>}
                          </p>
                        </motion.div>
                      </div>
                    )}

                    {/* 10초 잔향 소거 중 (공기 정화) UI 안내 */}
                    {(currentScent === "90" || deviceStatus.includes("소거") || deviceStatus.includes("정지")) ? (
                      <div className="p-3 bg-blue-500/15 border border-blue-500/30 rounded-2xl flex flex-col items-center gap-1">
                        <div className="flex items-center gap-2 text-blue-700 dark:text-blue-300 font-black text-xs">
                          <Wind className="w-4 h-4 text-blue-500 animate-spin" />
                          <span>새로운 향 교체 대기 중 (10초 잔향 소거 진행 중)</span>
                        </div>
                        <p className="text-[10px] font-semibold text-blue-600/80 dark:text-blue-300/80 text-center">
                          이전 향기를 깨끗하게 소거하는 쿨타임 단계입니다 (정상 작동).
                        </p>
                      </div>
                    ) : activeScentName ? (
                      <div className="flex flex-wrap justify-center items-center gap-2 sm:gap-3 bg-white/60 dark:bg-gray-900/60 backdrop-blur-md px-4 py-2.5 rounded-full border border-gray-200/50 dark:border-gray-700/50 shadow-sm">
                        <div className="flex items-center gap-1.5 text-xs sm:text-sm font-bold text-gray-800 dark:text-gray-200">
                          <Droplets className="w-3.5 h-3.5 sm:w-4 sm:h-4 text-blue-500" />
                          <span>
                            {activeScentName} 향 분사 중
                          </span>
                        </div>
                        {activeMusicName && activeMusicName !== "재생 안 함" && (
                          <>
                            <div className="hidden sm:block w-1 h-1 rounded-full bg-gray-300 dark:bg-gray-600"></div>
                            <div className="flex items-center gap-1.5 text-xs sm:text-sm font-bold text-gray-800 dark:text-gray-200">
                              <Music className="w-3.5 h-3.5 sm:w-4 sm:h-4 text-purple-500" />
                              <span>
                                {activeMusicName}
                              </span>
                            </div>
                          </>
                        )}
                      </div>
                    ) : null}
                  </div>
                )}

                {(!isOn || !activeMode) && (
                  <p className="text-sm sm:text-base text-gray-600 dark:text-gray-400 mt-2 font-medium transition-colors text-center">
                    {isOn ? "모드를 선택해서 발향을 시작하세요" : "전원을 켜서 향기를 즐겨보세요"}
                  </p>
                )}

                {isOn && activeMode && (activeMode === "ai" || activeMode === "noise" || activeMode === "weather" || activeMode === "voice") && (
                  <div className="flex flex-col gap-1.5 mt-4 relative z-20">
                    <div className="flex justify-center gap-3">
                      <button
                        onClick={(e) => {
                          e.stopPropagation();
                          handleFeedback("like");
                        }}
                        className="flex-1 flex items-center justify-center gap-2 py-3 rounded-2xl border shadow-sm transition-all bg-white dark:bg-gray-800 text-gray-800 dark:text-gray-200 border-gray-200 dark:border-gray-700 hover:bg-gray-50 dark:hover:bg-gray-700 font-bold"
                      >
                        <ThumbsUp className="w-4 h-4 text-blue-500" />
                        <span className="text-sm whitespace-nowrap">좋아요 👍</span>
                      </button>
                      <button
                        onClick={(e) => {
                          e.stopPropagation();
                          handleFeedback("dislike");
                        }}
                        className="flex-1 flex items-center justify-center gap-2 py-3 rounded-2xl border shadow-sm transition-all bg-white dark:bg-gray-800 text-gray-800 dark:text-gray-200 border-gray-200 dark:border-gray-700 hover:bg-gray-50 dark:hover:bg-gray-700 font-bold"
                      >
                        <ThumbsDown className="w-4 h-4 text-rose-500" />
                        <span className="text-sm whitespace-nowrap">별로예요 👎</span>
                      </button>
                    </div>
                    <span className="text-[10px] font-bold text-gray-400 dark:text-gray-500 text-center block">
                      ✨ Zero-Touch 무개입 자동화 동작 중 (묵시적 만족 유지)
                    </span>
                  </div>
                )}
              </div>
            </div>
          </div>
        </motion.div>
      </div>
      {/* 4개 카트리지 잔량 (%) 및 15% 미만 교체 경고 현황 */}
      <div className="px-6 py-2">
        <div className="bg-white dark:bg-gray-900 border border-gray-200 dark:border-gray-800 shadow-sm rounded-2xl p-4 transition-colors duration-300">
          <div className="flex items-center justify-between mb-3">
            <div className="flex items-center gap-2 text-gray-700 dark:text-gray-300">
              <Droplets className="w-4 h-4 text-blue-500" />
              <span className="text-xs font-bold uppercase tracking-wider">4개 향 카트리지 잔량 현황</span>
            </div>
            {scentSlots.some(s => s.remaining < 15) && (
              <span className="text-[10px] font-black px-2.5 py-1 rounded-full bg-red-100 dark:bg-red-950/80 text-red-600 dark:text-red-400 border border-red-300 dark:border-red-800 flex items-center gap-1 animate-pulse">
                <AlertTriangle className="w-3 h-3 text-red-500" />
                카트리지 교체 필요 (&lt;15%)
              </span>
            )}
          </div>
          <div className="grid grid-cols-2 sm:grid-cols-4 gap-2.5">
            {scentSlots.map((slot) => {
              const isLow = slot.remaining < 15;
              return (
                <div 
                  key={slot.id} 
                  className={`p-2.5 rounded-xl border transition-all flex flex-col gap-1.5 ${
                    isLow 
                      ? "bg-red-50 dark:bg-red-950/40 border-red-300 dark:border-red-800 text-red-700 dark:text-red-300 shadow-xs" 
                      : "bg-gray-50 dark:bg-gray-800/60 border-gray-200/60 dark:border-gray-700/60 text-gray-800 dark:text-gray-200"
                  }`}
                >
                  <div className="flex items-center justify-between">
                    <div className="flex items-center gap-1.5 min-w-0">
                      <div className={`w-2.5 h-2.5 rounded-full ${slot.color} shrink-0`} />
                      <span className="text-[11px] font-bold truncate">{slot.name}</span>
                    </div>
                    <span className={`text-[11px] font-black ${isLow ? "text-red-600 dark:text-red-400" : "text-gray-900 dark:text-white"}`}>
                      {slot.remaining}%
                    </span>
                  </div>
                  <div className="w-full h-1.5 bg-gray-200 dark:bg-gray-700 rounded-full overflow-hidden">
                    <div 
                      className={`h-full rounded-full transition-all duration-500 ${isLow ? "bg-red-500" : "bg-blue-500"}`}
                      style={{ width: `${slot.remaining}%` }}
                    />
                  </div>
                  {isLow && (
                    <span className="text-[9px] font-black text-red-500 dark:text-red-400 text-center tracking-tighter mt-0.5">
                      ⚠️ 교체 필요
                    </span>
                  )}
                </div>
              );
            })}
          </div>
        </div>
      </div>

      <div className="px-6 py-2 grid grid-cols-2 gap-2 sm:gap-3">
        <div className="bg-white dark:bg-gray-900 border border-gray-200 dark:border-gray-800 shadow-sm rounded-2xl p-3 sm:p-4 flex flex-col justify-between transition-colors duration-300">
          <div className="flex items-center justify-center sm:justify-start gap-1.5 sm:gap-2 mb-3 text-gray-600 dark:text-gray-400">
            <SlidersHorizontal className="w-3.5 h-3.5 sm:w-4 sm:h-4" />
            <span className="text-[10px] sm:text-xs font-semibold">
              발향 강도
            </span>
          </div>
          <div className="flex justify-between items-center bg-gray-50 dark:bg-gray-800 border border-gray-100 dark:border-gray-700 p-1 rounded-xl shadow-inner transition-colors duration-300">
            {[1, 2, 3].map((level) => (
              <button
                key={level}
                onClick={() => handleIntensityChange(level)}
                disabled={loading}
                className={`flex-1 py-1.5 rounded-lg text-xs sm:text-sm font-bold transition-all ${intensity === level ? "bg-white dark:bg-gray-700 text-gray-900 dark:text-white shadow-sm border border-gray-200 dark:border-gray-600" : "text-gray-400 dark:text-gray-500 hover:text-gray-600 dark:hover:text-gray-300 border border-transparent"} disabled:opacity-50`}
              >
                {level}
              </button>
            ))}
          </div>
        </div>

        <div className="bg-white dark:bg-gray-900 border border-gray-200 dark:border-gray-800 shadow-sm rounded-2xl p-3 sm:p-4 flex flex-col justify-between transition-colors duration-300">
          <div className="flex items-center justify-center sm:justify-start gap-1.5 sm:gap-2 mb-3 text-gray-600 dark:text-gray-400">
            <Timer className="w-3.5 h-3.5 sm:w-4 sm:h-4" />
            <span className="text-[10px] sm:text-xs font-semibold">
              타이머
            </span>
          </div>
          <button 
            onClick={() => setIsTimerModalOpen(true)}
            className="w-full py-[7px] sm:py-2 bg-gray-50 dark:bg-gray-800 border border-gray-200 dark:border-gray-700 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors rounded-xl shadow-sm text-xs sm:text-sm font-bold text-gray-800 dark:text-gray-200"
          >
            {activeTimerMinutes 
              ? `${activeTimerMinutes >= 60 ? `${Math.floor(activeTimerMinutes / 60)}시간 ` : ""}${activeTimerMinutes % 60 > 0 ? `${activeTimerMinutes % 60}분` : ""}`
              : "끄기"}
          </button>
        </div>
      </div>

      <div className="px-6 py-2">
        <div className="bg-white dark:bg-gray-900 border border-gray-200 dark:border-gray-800 shadow-sm rounded-2xl p-4 transition-colors duration-300">
          <div className="flex items-center justify-between mb-4">
            <div className="flex items-center gap-2 text-gray-600 dark:text-gray-400">
              <Volume2 className="w-4 h-4" />
              <span className="text-xs font-bold uppercase tracking-wider">스피커 음량</span>
            </div>
            <span className="text-sm font-black text-blue-600 dark:text-blue-400 bg-blue-50 dark:bg-blue-900/30 px-3 py-1 rounded-full border border-blue-100 dark:border-blue-800/50">
              {volume} <span className="text-[10px] font-bold text-gray-400">/ 10</span>
            </span>
          </div>
          
          <div className="flex items-center gap-4">
            <button
              onClick={() => handleVolumeChange(volume - 1)}
              disabled={volume <= 0}
              className="w-12 h-12 rounded-2xl bg-gray-100 dark:bg-gray-800 text-gray-600 dark:text-gray-300 flex items-center justify-center hover:bg-gray-200 dark:hover:bg-gray-700 active:scale-90 transition-all disabled:opacity-30 shadow-sm border border-gray-200 dark:border-gray-700"
            >
              <Minus className="w-6 h-6" />
            </button>
            
            <div className="flex-1 h-12 bg-gray-100 dark:bg-gray-800 rounded-2xl relative overflow-hidden flex items-center px-1 shadow-inner border border-gray-200 dark:border-gray-700">
              <div className="absolute inset-0 flex justify-between px-3 items-center pointer-events-none opacity-20">
                {[...Array(11)].map((_, i) => (
                  <div key={i} className={`w-0.5 h-2 rounded-full ${i % 5 === 0 ? "h-4 bg-gray-400" : "bg-gray-300"}`} />
                ))}
              </div>
              <motion.div 
                initial={false}
                animate={{ width: `${(volume / 10) * 100}%` }}
                transition={{ type: "spring", stiffness: 200, damping: 25 }}
                className="h-10 bg-gradient-to-r from-blue-400 to-indigo-500 rounded-xl shadow-lg relative z-10"
              />
            </div>
            
            <button
              onClick={() => handleVolumeChange(volume + 1)}
              disabled={volume >= 10}
              className="w-12 h-12 rounded-2xl bg-gray-900 dark:bg-white text-white dark:text-gray-900 flex items-center justify-center hover:opacity-90 active:scale-90 transition-all disabled:opacity-30 shadow-md"
            >
              <Plus className="w-6 h-6" />
            </button>
          </div>
        </div>
      </div>

      <AnimatePresence>
        {(pendingMode === "manual" || (activeMode === "manual" && pendingMode === null)) && (
          <motion.div
            initial={{ opacity: 0, height: 0, scale: 0.95 }}
            animate={{ opacity: 1, height: "auto", scale: 1 }}
            exit={{ opacity: 0, height: 0, scale: 0.95 }}
            className="px-6 py-2 overflow-hidden"
          >
            <div className="bg-white dark:bg-gray-900 border border-gray-200 dark:border-gray-800 shadow-sm rounded-2xl p-4 transition-colors duration-300">
              <div className="flex items-center justify-between mb-3">
                <div className="flex items-center gap-2 text-gray-600 dark:text-gray-400">
                  <Droplets className="w-4 h-4" />
                  <span className="text-xs font-semibold">
                    수동 향기 선택{" "}
                    <span className="text-[10px] text-gray-400 dark:text-gray-500 font-normal ml-1">
                      (최대 2개)
                    </span>
                  </span>
                </div>
                <span className="text-[10px] font-bold text-gray-400 dark:text-gray-500">
                  {manualScents.length}/2 선택됨
                </span>
              </div>
              <div className="grid grid-cols-4 gap-2">
                {scentSlots.map((slot) => {
                  const isSelected = manualScents.includes(slot.name);
                  return (
                    <button
                      key={slot.id}
                      onClick={() => handleManualScentSelect(slot.name)}
                      className={`py-2 px-1 rounded-xl text-[10px] font-bold border transition-all duration-300 flex flex-col items-center justify-center gap-1.5 ${
                        isSelected
                          ? `bg-gray-900 text-white border-gray-900 dark:bg-white dark:text-gray-900 dark:border-white shadow-md`
                          : `bg-gray-50 text-gray-600 border-gray-200 hover:bg-gray-100 dark:bg-gray-800 dark:text-gray-400 dark:border-gray-700 opacity-80 hover:opacity-100`
                      }`}
                    >
                      <div
                        className={`w-3 h-3 rounded-full ${slot.color} shadow-inner`}
                        style={{ opacity: isSelected ? 1 : 0.6 }}
                      ></div>
                      <span className="truncate w-full text-center px-0.5">{slot.name}</span>
                    </button>
                  );
                })}
              </div>
            </div>
          </motion.div>
        )}
      </AnimatePresence>

      <div className="px-6 py-4 flex-1">
        <div className="flex items-center justify-between mb-4">
          <h3 className="text-lg font-bold text-gray-900 dark:text-white transition-colors">
            발향 모드
          </h3>
        </div>
        <div className="space-y-3">
          {modes.map((mode) => {
            const isSelected = pendingMode
              ? pendingMode === mode.id
              : activeMode === mode.id;

            return (
              <button
                key={mode.id}
                onClick={() => {
                  if (mode.id === "manual") {
                    setPendingMode("manual");
                  } else if (mode.id !== activeMode) {
                    setPendingMode(mode.id as any);
                    setVoiceResult(null);
                  } else {
                    setPendingMode(null);
                  }
                  containerRef.current?.scrollTo({ top: 0, behavior: 'smooth' });
                }}
                disabled={loading}
                className={`w-full flex items-center p-4 rounded-2xl transition-all border-2 ${isSelected ? `${mode.borderColor} bg-white dark:bg-gray-900 shadow-md` : "border-gray-200 dark:border-gray-800 bg-white dark:bg-gray-900 hover:bg-gray-50 dark:hover:bg-gray-800 shadow-sm"} disabled:opacity-50`}
              >
                <div
                  className={`w-12 h-12 rounded-xl flex items-center justify-center mr-4 transition-colors ${isSelected ? mode.activeColor : mode.color}`}
                >
                  {mode.icon}
                </div>
                <div className="text-left flex-1">
                  <h4 className="text-base font-bold text-gray-900 dark:text-gray-100 transition-colors">
                    {mode.title}
                  </h4>
                  <p className="text-xs text-gray-500 dark:text-gray-400 font-medium mt-0.5 transition-colors">
                    {mode.description}
                  </p>
                </div>
                <div
                  className={`w-6 h-6 rounded-full border-2 flex items-center justify-center transition-colors ${isSelected ? mode.borderColor : "border-gray-300 dark:border-gray-700"}`}
                >
                  {isSelected && (
                    <div
                      className={`w-3 h-3 rounded-full ${mode.dotColor}`}
                    />
                  )}
                </div>
              </button>
            );
          })}
        </div>
      </div>

      <AnimatePresence>
        {((pendingMode && pendingMode !== activeMode) || (activeMode === "manual" && !pendingMode && (manualScents.length !== appliedManualScents.length || !manualScents.every(s => appliedManualScents.includes(s))))) && (
          <motion.div
            initial={{ y: 50, opacity: 0 }}
            animate={{ y: 0, opacity: 1 }}
            exit={{ y: 50, opacity: 0 }}
            className="fixed bottom-[84px] left-0 right-0 px-6 z-30"
          >
            <button
              onClick={handleStartMode}
              disabled={loading}
              className="w-full bg-gray-900 dark:bg-gray-100 text-white dark:text-gray-900 py-4 rounded-2xl font-bold shadow-2xl flex items-center justify-center gap-2 transition-transform hover:scale-[1.02] active:scale-[0.98] disabled:opacity-70"
            >
              {loading ? <Loader2 className="w-6 h-6 animate-spin" /> : pendingMode ? "새로운 모드로 발향 시작" : "변경된 향기로 발향 시작"}
            </button>
          </motion.div>
        )}
      </AnimatePresence>

      <AnimatePresence>
        {isTimerModalOpen && (
          <>
            <motion.div 
              initial={{ opacity: 0 }} 
              animate={{ opacity: 1 }} 
              exit={{ opacity: 0 }} 
              onClick={() => setIsTimerModalOpen(false)} 
              className="fixed inset-0 bg-black/60 backdrop-blur-sm z-40" 
            />
            
            <motion.div 
              initial={{ y: "100%" }} 
              animate={{ y: 0 }} 
              exit={{ y: "100%" }} 
              transition={{ type: "spring", damping: 25, stiffness: 200 }} 
              className="fixed inset-x-0 bottom-0 z-50 bg-white dark:bg-gray-900 rounded-t-[3rem] shadow-2xl flex flex-col p-8 pb-12"
            >
              <div className="w-12 h-1.5 bg-gray-200 dark:bg-gray-700 rounded-full mx-auto mb-8" />
              
              <h2 className="text-2xl font-bold text-gray-900 dark:text-white mb-2 text-center">타이머 설정</h2>
              <p className="text-sm text-gray-500 dark:text-gray-400 mb-8 text-center">원하시는 작동 시간을 선택해주세요.</p>

              <div className="flex items-center justify-center gap-4 mb-10">
                <div className="flex flex-col items-center">
                  <span className="text-[10px] font-bold text-gray-400 uppercase tracking-widest mb-2">시간 (Hours)</span>
                  <select 
                    value={selectedHours}
                    onChange={(e) => setSelectedHours(Number(e.target.value))}
                    className="w-24 sm:w-32 h-20 text-4xl sm:text-5xl font-black text-center text-gray-900 dark:text-white bg-gray-50 dark:bg-gray-800 border border-gray-200 dark:border-gray-700 rounded-2xl appearance-none cursor-pointer outline-none focus:ring-2 focus:ring-blue-500"
                    style={{ textAlignLast: 'center' }}
                  >
                    {[...Array(13)].map((_, i) => (
                      <option key={`opt-h-${i}`} value={i}>{i}</option>
                    ))}
                  </select>
                </div>

                <div className="text-4xl font-black text-gray-300 dark:text-gray-700 mt-6">:</div>

                <div className="flex flex-col items-center">
                  <span className="text-[10px] font-bold text-gray-400 uppercase tracking-widest mb-2">분 (Minutes)</span>
                  <select 
                    value={selectedMinutes}
                    onChange={(e) => setSelectedMinutes(Number(e.target.value))}
                    className="w-24 sm:w-32 h-20 text-4xl sm:text-5xl font-black text-center text-gray-900 dark:text-white bg-gray-50 dark:bg-gray-800 border border-gray-200 dark:border-gray-700 rounded-2xl appearance-none cursor-pointer outline-none focus:ring-2 focus:ring-blue-500"
                    style={{ textAlignLast: 'center' }}
                  >
                    {[...Array(60)].map((_, i) => (
                      <option key={`opt-m-${i}`} value={i}>{i.toString().padStart(2, '0')}</option>
                    ))}
                  </select>
                </div>
              </div>

              <div className="flex flex-col gap-3">
                <button
                  onClick={() => {
                    const totalMinutes = selectedHours * 60 + selectedMinutes;
                    if (totalMinutes > 0) {
                      handleTimerChange(totalMinutes);
                      setIsTimerModalOpen(false);
                    } else {
                      alert("시간을 설정해주세요.");
                    }
                  }}
                  className="w-full bg-gray-900 dark:bg-white text-white dark:text-gray-900 py-4 rounded-2xl font-bold shadow-lg"
                >
                  타이머 시작
                </button>
                <button
                  onClick={() => {
                    handleTimerChange(null);
                    setIsTimerModalOpen(false);
                  }}
                  className="w-full bg-gray-100 dark:bg-gray-800 text-gray-600 dark:text-gray-300 py-4 rounded-2xl font-bold"
                >
                  타이머 종료
                </button>
              </div>
            </motion.div>

          </>
        )}
      </AnimatePresence>

      <BottomNav />
    </div>
  );
}