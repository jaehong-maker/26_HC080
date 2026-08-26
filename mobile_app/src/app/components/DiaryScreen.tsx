import React, { useState, useEffect } from "react";
import { Link } from "react-router";
import { BottomNav } from "./BottomNav";
import { Home as HomeIcon, Droplets, SlidersHorizontal, Settings, BookHeart, Brain, Sparkles, Search, ChevronDown, Quote, ThumbsUp, ThumbsDown, Music, Palette, Lightbulb } from "lucide-react";
import { motion, AnimatePresence } from "motion/react";
import { useDiary } from "../store/DiaryContext";
import { useAuth } from "../store/AuthContext";
import { useDevice } from "../store/DeviceContext";
import { TRACKS } from "../utils/tracks";
import { toast } from "sonner";

export function DiaryScreen() {
  const { diaries, fetchDiaries } = useDiary();
  const { currentUser } = useAuth();
  const { sendDeviceData, scentSlots } = useDevice();
  const [diaryText, setDiaryText] = useState("");
  const [isAnalyzing, setIsAnalyzing] = useState(false);
  const [result, setResult] = useState<{ 
    emotion: string; 
    scent: string; 
    color: string; 
    tagColor: string; 
    hex: string;
    colorName: string;
    recommendedSong: string;
    spray?: number; 
    reason?: string;
    scentName?: string;
  } | null>(null);
  
  const [selectedDate, setSelectedDate] = useState<string | null>(null);
  const [isDropdownOpen, setIsDropdownOpen] = useState(false);
  const [feedback, setFeedback] = useState<"like" | "dislike" | null>(null);

  // 감정에 따른 단일 시각적 테마 & LED 색상 & 추천 음악 반환 함수 (통합 제어)
  const getEmotionTheme = (emotion: string) => {
    const e = (emotion || "편안함").toLowerCase();
    
    // 1. 신남/행복/즐거움 계열 (Orange / Yellow LED)
    if (e.includes("신남") || e.includes("행복") || e.includes("즐거움") || e.includes("설렘") || e.includes("뿌듯") || e.includes("활기") || e.includes("기쁨")) {
      return {
        hex: "#FF7E36",
        colorName: "웜 선셋 오렌지 (#FF7E36)",
        recommendedSong: "신나는 에너제틱 재즈 & 팝",
        color: "from-orange-500 to-amber-400 dark:from-orange-600 dark:to-amber-500 text-white shadow-orange-200/50",
        tagColor: "bg-white/20 text-white backdrop-blur-sm border-white/30",
        historyBadge: "bg-orange-500/15 dark:bg-orange-500/25 text-orange-700 dark:text-orange-300 border border-orange-500/30",
        historyBorder: "border-orange-200/60 dark:border-orange-900/40"
      };
    }
    // 2. 화남/짜증 계열 (Blue / Indigo LED - 진정)
    if (e.includes("화남") || e.includes("짜증") || e.includes("답답") || e.includes("분노") || e.includes("스트레스")) {
       return {
        hex: "#3B82F6",
        colorName: "쿨 캄 블루 (#3B82F6)",
        recommendedSong: "마음을 가라앉히는 로파이 & 빗소리",
        color: "from-blue-600 to-indigo-500 dark:from-blue-700 dark:to-indigo-600 text-white shadow-blue-200/50",
        tagColor: "bg-white/20 text-white backdrop-blur-sm border-white/30",
        historyBadge: "bg-blue-500/15 dark:bg-blue-500/25 text-blue-700 dark:text-blue-300 border border-blue-500/30",
        historyBorder: "border-blue-200/60 dark:border-blue-900/40"
      };
    }
    // 3. 슬픔/우울/피곤 계열 (Pink / Rose LED - 위로)
    if (e.includes("슬픔") || e.includes("우울") || e.includes("외로움") || e.includes("지침") || e.includes("힘듦") || e.includes("피곤") || e.includes("고단")) {
      return {
        hex: "#EC4899",
        colorName: "워밍 로즈 핑크 (#EC4899)",
        recommendedSong: "따뜻하게 위로하는 어쿠스틱 발라드",
        color: "from-rose-500 to-pink-400 dark:from-rose-600 dark:to-pink-500 text-white shadow-rose-200/50",
        tagColor: "bg-white/20 text-white backdrop-blur-sm border-white/30",
        historyBadge: "bg-pink-500/15 dark:bg-pink-500/25 text-pink-700 dark:text-pink-300 border border-pink-500/30",
        historyBorder: "border-pink-200/60 dark:border-pink-900/40"
      };
    }
    // 4. 편안함/평온 계열 (Violet / Purple LED - 휴식)
    return {
      hex: "#8B5CF6",
      colorName: "디프 릴랙싱 바이올렛 (#8B5CF6)",
      recommendedSong: "평온하고 포근한 앰비언트 메디테이션",
      color: "from-violet-600 to-purple-500 dark:from-violet-700 dark:to-purple-600 text-white shadow-purple-200/50",
      tagColor: "bg-white/20 text-white backdrop-blur-sm border-white/30",
      historyBadge: "bg-purple-500/15 dark:bg-purple-500/25 text-purple-700 dark:text-purple-300 border border-purple-500/30",
      historyBorder: "border-purple-200/60 dark:border-purple-900/40"
    };
  };

  // 향기 번호에 따른 실제 이름 찾기
  const getActualScentName = (sprayCode: number | undefined) => {
    if (!sprayCode) return "";
    // 블렌딩 코드인 경우 (예: 12)
    if (sprayCode > 10 && sprayCode < 90) {
      const first = Math.floor(sprayCode / 10);
      const second = sprayCode % 10;
      const name1 = scentSlots.find(s => s.id === first)?.name || "향기1";
      const name2 = scentSlots.find(s => s.id === second)?.name || "향기2";
      return `${name1} & ${name2}`;
    }
    const slot = scentSlots.find(s => s.id === sprayCode);
    return slot ? slot.name : "";
  };

  // 화면 진입 시 서버에서 일기 내역 가져오기
  useEffect(() => {
    if (currentUser?.email) {
      fetchDiaries(currentUser.email);
    }
  }, [currentUser?.email]);

  const history = currentUser && diaries[currentUser.email] ? diaries[currentUser.email] : [];
  const uniqueDates = Array.from(new Set(history.map(item => item.date)));

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!diaryText.trim() || !currentUser) return;

    setIsAnalyzing(true);
    setResult(null);
    setFeedback(null);

    try {
      // 인자: action, value, region(emotion tag), diaryText
      const res = await sendDeviceData("AI_EMOTION", 0, "분석중", diaryText);

      if (res.success) {
        // 서버에서 반환한 emotion_tag를 최우선으로 사용, 없으면 context에서 추출
        let detectedEmotion = res.emotion_tag || (res.context || "").replace("Emotion_", "").trim() || "편안함";
        if (detectedEmotion === "분석중") detectedEmotion = "편안함";
        
        // 동적 테마 적용
        const theme = getEmotionTheme(detectedEmotion);
        const actualScentName = getActualScentName(res.spray);

        // AI/서버가 반환한 실제 LED RGB가 있으면 Hex 변환하여 동기화
        const r = res.led_r ?? res.led_dict?.led_r;
        const g = res.led_g ?? res.led_dict?.led_g;
        const b = res.led_b ?? res.led_dict?.led_b;

        let exactHex = theme.hex;
        if (r !== undefined && g !== undefined && b !== undefined) {
          exactHex = `#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${b.toString(16).padStart(2, '0')}`.toUpperCase();
        }

        // 음악 정보 매칭 (실제 출력되는 곡 이름과 동일하도록 TRACKS에서 파싱)
        let actualSongTitle = "";
        if (res.music && Number(res.music) > 0) {
          const trackInfo = TRACKS.find(t => t.id === `song_${res.music}`);
          if (trackInfo) actualSongTitle = `${trackInfo.name} - ${trackInfo.artist}`;
        }
        if (!actualSongTitle && currentUser?.musicTracks) {
          const userTrackIds = currentUser.musicTracks.split("_");
          const sprayId = res.spray || 1;
          const trackIdx = sprayId - 1;
          if (trackIdx >= 0 && trackIdx < userTrackIds.length) {
            const rawSlotStr = userTrackIds[trackIdx] || "0";
            const firstTrackNum = rawSlotStr.split(",")[0];
            if (firstTrackNum && firstTrackNum !== "0") {
              const trackInfo = TRACKS.find(t => t.id === `song_${firstTrackNum}`);
              if (trackInfo) actualSongTitle = `${trackInfo.name} - ${trackInfo.artist}`;
            }
          }
        }
        if (!actualSongTitle) {
          actualSongTitle = theme.recommendedSong;
        }

        setResult({
          emotion: detectedEmotion,
          scent: res.emotion_summary || "당신만을 위한 추천 향기", // 요약 메시지 활용
          reason: res.result_text || res.message || `${detectedEmotion} 감정에 어울리는 특별한 향기를 준비했어요.`,
          spray: res.spray,
          scentName: actualScentName,
          ...theme,
          hex: exactHex,
          recommendedSong: actualSongTitle,
        });

        // 서버에 저장된 최신 내역을 즉시 다시 불러오기
        if (currentUser?.email) {
          await fetchDiaries(currentUser.email);
        }
        
        setDiaryText(""); 
      } else {
        alert("분석 실패: " + res.message);
      }
    } catch (error) {
      console.error("AI Analysis failed:", error);
      alert("통신 오류가 발생했습니다.");
    } finally {
      setIsAnalyzing(false);
    }
  };

  const getHistoryEmotionColor = (emotion: string) => {
    const e = (emotion || "편안함").toLowerCase();
    if (e.includes("신남") || e.includes("행복") || e.includes("즐거움") || e.includes("설렘") || e.includes("뿌듯") || e.includes("활기") || e.includes("기쁨")) {
      return "bg-orange-100 dark:bg-orange-900/30 text-orange-700 dark:text-orange-400";
    }
    if (e.includes("화남") || e.includes("짜증") || e.includes("답답") || e.includes("분노") || e.includes("스트레스")) {
      return "bg-blue-100 dark:bg-blue-900/30 text-blue-700 dark:text-blue-400";
    }
    if (e.includes("슬픔") || e.includes("우울") || e.includes("외로움") || e.includes("지침") || e.includes("힘듦") || e.includes("피곤") || e.includes("고단")) {
      return "bg-pink-100 dark:bg-pink-900/30 text-pink-700 dark:text-pink-400";
    }
    return "bg-violet-100 dark:bg-violet-900/30 text-violet-700 dark:text-violet-400";
  };

  const handleDiaryFeedback = async (type: "like" | "dislike") => {
    if (!result || !currentUser) return;
    setFeedback(type);

    try {
      if (type === "dislike") {
        // 기존 향기 분사를 확실히 멈추기 위해 정지 명령 전송
        toast("기존 향기를 멈추고 새로운 향기를 준비 중입니다...", { icon: "⏳" });
        await sendDeviceData("MENU_STOP", 0);
        // 기기가 정지 명령을 가져가서 모터를 끌 수 있도록 3초 대기
        await new Promise(resolve => setTimeout(resolve, 3000));
      }

      // 1. 서버에 피드백 전송 (서버에서 자동으로 다른 향기를 찾아 기기에 명령을 내림)
      const val = type === "like" ? 1 : -1;
      const res = await sendDeviceData("FEEDBACK", val, `${result.spray}_AI_DIARY`);

      if (type === "dislike") {
        if (res.success && res.spray) {
          const newSprayCode = res.spray;
          const newScent = scentSlots.find(s => s.id === newSprayCode) || scentSlots[0];
          
          // 2. UI 즉시 업데이트
          setResult(prev => {
            if (!prev) return prev;
            return {
              ...prev,
              spray: newScent.id,
              scentName: newScent.name,
              scent: "다른 향기로 바로 교체해 드렸어요!",
              reason: "추천해 드린 향기가 마음에 들지 않으셨군요. 즉시 다른 향기로 교체하여 기기에 분사 명령을 보냈습니다."
            };
          });
          toast.success(`${newScent.name} 향기로 즉시 변경되었습니다!`);
        } else {
          toast.error("다른 향기로 변경하는 데 실패했습니다.");
        }
      } else {
        toast.success("피드백이 반영되었습니다. 감사합니다!");
      }
    } catch (err) {
      console.error("Feedback failed", err);
    }
  };

  return (
    <div key="diary-screen" className="flex-1 flex flex-col bg-gray-50 dark:bg-gray-950 overflow-y-auto relative pb-32 min-h-screen transition-colors duration-300">
      <header className="px-6 pt-12 pb-4 flex justify-between items-center z-10 sticky top-0 bg-gray-50/90 dark:bg-gray-950/90 backdrop-blur-md transition-colors duration-300">
        <div>
          <h1 className="text-2xl font-bold tracking-tight text-gray-900 dark:text-white">감정 일기장</h1>
          <p className="text-sm font-medium text-gray-500 dark:text-gray-400 mt-1">오늘의 기분을 기록하고 향기를 추천받으세요</p>
        </div>
      </header>

      <div className="px-6 py-4 flex flex-col gap-6">
        <motion.div 
          initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }}
          className="bg-white dark:bg-gray-900 rounded-3xl p-6 shadow-sm border border-gray-200 dark:border-gray-800 transition-colors duration-300"
        >
          <div className="flex items-center gap-2 mb-4">
            <BookHeart className="w-5 h-5 text-gray-700 dark:text-gray-300" />
            <h2 className="text-lg font-bold text-gray-900 dark:text-white">나의 3줄 일기</h2>
          </div>
          
          <form onSubmit={handleSubmit} className="flex flex-col gap-4">
            <textarea
              value={diaryText}
              onChange={(e) => setDiaryText(e.target.value)}
              placeholder="오늘 하루는 어땠나요? 3줄로 솔직한 감정을 적어주시면 AI가 문맥을 파악해 가장 어울리는 향기를 분사해드려요."
              className="w-full h-32 p-4 bg-gray-50 dark:bg-gray-800 border border-gray-200 dark:border-gray-700 rounded-2xl resize-none text-gray-800 dark:text-gray-200 focus:outline-none focus:ring-2 focus:ring-gray-900 dark:focus:ring-gray-100 focus:border-transparent transition-all text-sm placeholder:text-gray-400 dark:placeholder:text-gray-500"
            />
            
            <button
              type="submit"
              disabled={!diaryText.trim() || isAnalyzing}
              className="w-full py-4 bg-gray-900 dark:bg-gray-100 text-white dark:text-gray-900 rounded-2xl font-bold text-sm shadow-sm hover:bg-gray-800 dark:hover:bg-gray-200 transition-colors disabled:bg-gray-300 dark:disabled:bg-gray-800 disabled:text-gray-500 dark:disabled:text-gray-600 flex justify-center items-center gap-2"
            >
              {isAnalyzing ? (
                <>
                  <motion.div
                    animate={{ rotate: 360 }}
                    transition={{ duration: 1, repeat: Infinity, ease: "linear" }}
                  >
                    <Sparkles className="w-5 h-5" />
                  </motion.div>
                  <span>감정 분석 중...</span>
                </>
              ) : (
                <>
                  <Brain className="w-5 h-5" />
                  <span>AI 분석 및 향기 추천받기</span>
                </>
              )}
            </button>
          </form>
        </motion.div>

        <AnimatePresence>
          {result && (
            <motion.div
              initial={{ opacity: 0, scale: 0.95, y: 15 }}
              animate={{ opacity: 1, scale: 1, y: 0 }}
              exit={{ opacity: 0, scale: 0.95, y: -15 }}
              style={{
                background: `linear-gradient(135deg, ${result.hex}E6 0%, ${result.hex} 100%)`,
                boxShadow: `0 20px 40px -15px ${result.hex}80`
              }}
              className="rounded-[2.5rem] p-8 text-white border-none relative overflow-hidden transition-all duration-500"
            >
              {/* 장식용 배경 요소 */}
              <div className="absolute right-[-5%] top-[-10%] w-56 h-56 bg-white/20 rounded-full blur-3xl -z-10" />
              <div className="absolute left-[-5%] bottom-[-10%] w-40 h-40 bg-black/10 rounded-full blur-2xl -z-10" />
              
              <div className="flex flex-col gap-8 relative z-10">
                <div className="flex justify-between items-start">
                  <div className="flex flex-col gap-1.5">
                    <div className="flex items-center gap-2 mb-1">
                      <span className="w-1.5 h-1.5 bg-white rounded-full animate-pulse" />
                      <span className="text-[10px] font-black uppercase tracking-[0.25em] opacity-80">AI HEART REPORT</span>
                    </div>
                    <div className="flex items-center gap-3">
                      <h3 className="text-4xl font-black tracking-tighter text-white">
                        {result.emotion}
                      </h3>
                      <span className={`px-3 py-1 rounded-full text-[10px] font-black border ${result.tagColor}`}>
                        분석 완료
                      </span>
                    </div>
                  </div>
                  <div className="bg-white/20 backdrop-blur-xl p-4 rounded-[1.5rem] border border-white/30 shadow-lg">
                    <Sparkles className="w-7 h-7 text-white" />
                  </div>
                </div>

                <div className="bg-white/10 backdrop-blur-md rounded-3xl p-6 border border-white/20">
                  <div className="flex items-center gap-2 mb-3">
                    <Quote className="w-4 h-4 text-white opacity-60 fill-current" />
                    <span className="text-[10px] font-black uppercase tracking-widest text-white opacity-80">Insight</span>
                  </div>
                  <p className="text-sm font-semibold leading-relaxed text-white opacity-95">
                    {result.reason}
                  </p>
                </div>

                <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
                  {/* 향기 추천 */}
                  <div className="flex flex-col gap-1 bg-white/15 backdrop-blur-md rounded-2xl p-4 border border-white/20">
                    <div className="flex items-center gap-2">
                      <Droplets className="w-4 h-4 text-white opacity-80" />
                      <span className="text-[10px] font-black uppercase tracking-wider text-white opacity-80">Recommend Scent</span>
                    </div>
                    <span className="text-lg font-black tracking-tight text-white mt-1 drop-shadow-sm">
                      {result.scentName || "맞춤 향기"}
                    </span>
                    <span className="text-[10px] font-bold text-white opacity-80">
                      {result.scent}
                    </span>
                  </div>

                  {/* 노래 추천 */}
                  <div className="flex flex-col gap-1 bg-white/15 backdrop-blur-md rounded-2xl p-4 border border-white/20">
                    <div className="flex items-center gap-2">
                      <Music className="w-4 h-4 text-white opacity-80" />
                      <span className="text-[10px] font-black uppercase tracking-wider text-white opacity-80">Recommend Song</span>
                    </div>
                    <span className="text-sm font-black tracking-tight text-white mt-1 drop-shadow-sm">
                      🎵 {result.recommendedSong}
                    </span>
                  </div>
                </div>

                {/* 피드백 버튼 영역 */}
                <div className="flex justify-center gap-3 mt-2 relative z-20">
                  <button
                    onClick={() => handleDiaryFeedback("like")}
                    className={`flex-1 flex items-center justify-center gap-2 py-3 rounded-2xl border shadow-sm transition-all ${feedback === "like" ? "bg-white text-gray-900 border-white scale-105" : "bg-white/10 hover:bg-white/20 text-white border-white/20"}`}
                  >
                    <ThumbsUp className={`w-4 h-4 ${feedback === "like" ? "fill-current" : ""}`} />
                    <span className="text-sm font-bold whitespace-nowrap">좋아요</span>
                  </button>
                  <button
                    onClick={() => handleDiaryFeedback("dislike")}
                    className={`flex-1 flex items-center justify-center gap-2 py-3 rounded-2xl border shadow-sm transition-all bg-white/10 hover:bg-white/20 text-white border-white/20`}
                  >
                    <ThumbsDown className="w-4 h-4" />
                    <span className="text-sm font-bold whitespace-nowrap">별로예요</span>
                  </button>
                </div>
              </div>
            </motion.div>
          )}
        </AnimatePresence>

        <motion.div
          initial={{ opacity: 0, y: 10 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ delay: 0.1 }}
          className="bg-white dark:bg-gray-900 rounded-3xl p-6 shadow-sm border border-gray-200 dark:border-gray-800 mt-2 transition-colors duration-300"
        >
          <div className="flex items-center justify-between mb-6 relative z-20">
            <div className="flex items-center gap-2">
              <BookHeart className="w-5 h-5 text-gray-700 dark:text-gray-300" />
              <h2 className="text-lg font-bold text-gray-900 dark:text-white">지난 일기 기록</h2>
            </div>
            
            <div className="relative">
              <button 
                onClick={() => setIsDropdownOpen(!isDropdownOpen)}
                className="flex items-center gap-2 bg-gray-50 dark:bg-gray-800 hover:bg-gray-100 dark:hover:bg-gray-700 rounded-xl px-4 py-2 border border-gray-200 dark:border-gray-700 transition-colors"
              >
                <span className="text-sm font-bold text-gray-700 dark:text-gray-200">
                  {selectedDate ? selectedDate : "전체 날짜"}
                </span>
                <ChevronDown className={`w-4 h-4 text-gray-500 transition-transform ${isDropdownOpen ? 'rotate-180' : ''}`} />
              </button>

              <AnimatePresence>
                {isDropdownOpen && (
                  <>
                    <div 
                      className="fixed inset-0 z-20"
                      onClick={() => setIsDropdownOpen(false)}
                    />
                    <motion.div
                      initial={{ opacity: 0, y: -5, scale: 0.95 }}
                      animate={{ opacity: 1, y: 0, scale: 1 }}
                      exit={{ opacity: 0, y: -5, scale: 0.95 }}
                      transition={{ duration: 0.15 }}
                      className="absolute right-0 top-full mt-2 w-48 bg-white dark:bg-gray-900 border border-gray-200 dark:border-gray-700 rounded-2xl shadow-xl overflow-hidden z-30 ring-1 ring-black/5 dark:ring-white/10"
                    >
                      <div className="max-h-60 overflow-y-auto no-scrollbar py-1">
                        <button
                          onClick={() => {
                            setSelectedDate(null);
                            setIsDropdownOpen(false);
                          }}
                          className={`w-full text-left px-4 py-3 text-sm transition-colors ${!selectedDate ? 'font-bold text-gray-900 dark:text-white bg-gray-50 dark:bg-gray-800' : 'text-gray-600 dark:text-gray-400 hover:bg-gray-50 dark:hover:bg-gray-800'}`}
                        >
                          전체 날짜
                        </button>
                        {uniqueDates.map(date => (
                          <button
                            key={date}
                            onClick={() => {
                              setSelectedDate(date);
                              setIsDropdownOpen(false);
                            }}
                            className={`w-full text-left px-4 py-3 text-sm transition-colors ${selectedDate === date ? 'font-bold text-gray-900 dark:text-white bg-gray-50 dark:bg-gray-800' : 'text-gray-600 dark:text-gray-400 hover:bg-gray-50 dark:hover:bg-gray-800'}`}
                          >
                            {date}
                          </button>
                        ))}
                      </div>
                    </motion.div>
                  </>
                )}
              </AnimatePresence>
            </div>
          </div>
          
          <div className="flex flex-col gap-5">
            {history
              .filter((item) => !selectedDate || item.date === selectedDate)
              .map((item) => {
                let displayEmotion = item.emotion;
                if (!displayEmotion || displayEmotion === "분석중" || displayEmotion === "분석 완료") {
                  displayEmotion = "편안함";
                }
                
                const itemTheme = getEmotionTheme(displayEmotion);
                
                return (
                  <div 
                    key={item.timestamp || item.id} 
                    className={`group bg-gray-50/70 dark:bg-gray-800/40 rounded-[2rem] p-6 border ${itemTheme.historyBorder} hover:shadow-md transition-all duration-300`}
                  >
                    <div className="flex items-center justify-between mb-4">
                      <div className="flex flex-col gap-1.5">
                        <span className="text-[10px] font-black text-gray-400 dark:text-gray-500 uppercase tracking-[0.15em]">
                          {item.date}
                        </span>
                        <div className="flex items-center flex-wrap gap-2 mt-0.5">
                          {/* 감정 태그 */}
                          <span className={`px-3 py-1 rounded-full text-[10px] font-black shadow-sm ${itemTheme.historyBadge}`}>
                            {displayEmotion}
                          </span>
                          
                          {/* 추천과 100% 일치하는 LED 색상 칩 & 발광 원형 서클 */}
                          <div className="flex items-center gap-1.5 px-2.5 py-1 rounded-full bg-white dark:bg-gray-900 border border-gray-200/80 dark:border-gray-700/80 shadow-2xs">
                            <div 
                              className="w-3 h-3 rounded-full border border-white/60 shadow-xs" 
                              style={{ backgroundColor: itemTheme.hex, boxShadow: `0 0 8px ${itemTheme.hex}88` }}
                            />
                            <span className="text-[10px] font-extrabold text-gray-700 dark:text-gray-200">
                              LED: {itemTheme.hex}
                            </span>
                          </div>
                        </div>
                      </div>
                      
                      <div 
                        className="w-10 h-10 rounded-2xl flex items-center justify-center shadow-xs border border-white dark:border-gray-800 transition-transform duration-300 group-hover:scale-110"
                        style={{ backgroundColor: `${itemTheme.hex}15` }}
                      >
                        <Sparkles className="w-4 h-4" style={{ color: itemTheme.hex }} />
                      </div>
                    </div>

                    <p className="text-gray-900 dark:text-gray-100 text-base font-semibold leading-relaxed tracking-tight mb-4 whitespace-pre-wrap transition-colors">
                      {item.text}
                    </p>

                    <div className="pt-4 border-t border-gray-200/60 dark:border-gray-800/60 flex flex-wrap items-center justify-between gap-2">
                      <div className="flex items-center gap-2">
                        <div className="w-6 h-6 rounded-full bg-orange-100 dark:bg-orange-950/50 flex items-center justify-center border border-orange-200 dark:border-orange-900">
                          <Droplets className="w-3 h-3 text-orange-600 dark:text-orange-400" />
                        </div>
                        <span className="text-[11px] font-extrabold text-gray-700 dark:text-gray-300">
                          {item.scent}
                        </span>
                      </div>

                      <div className="flex items-center gap-1.5 text-[11px] font-medium text-gray-500 dark:text-gray-400">
                        <Music className="w-3.5 h-3.5 text-purple-500" />
                        <span>{itemTheme.recommendedSong}</span>
                      </div>
                    </div>
                  </div>
                );
              })}
          </div>
        </motion.div>
      </div>

      <BottomNav />
    </div>
  );
}
