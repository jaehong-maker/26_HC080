import React, { useState, useEffect } from "react";
import { useNavigate } from "react-router";
import { motion, AnimatePresence } from "motion/react";
import { 
  ChevronLeft, 
  BarChart3, 
  Sparkles, 
  Trophy, 
  CloudSun,
  RefreshCw,
  AlertCircle,
  ThumbsUp,
  CloudRain,
  Sun,
  Cloud,
  Snowflake,
  Heart,
  Clock,
  Zap
} from "lucide-react";
import { useAuth } from "../store/AuthContext";
import { apiSendData } from "../utils/api";

interface PreferenceItem {
  label: string;
  scent: string;
  score: number;
}

interface ReportData {
  result: string;
  summary: string;
  total_analyzed: number;
  top_scent: string;
  top_context: string;
  weather_prefs?: PreferenceItem[];
  emotion_prefs?: PreferenceItem[];
  time_prefs?: PreferenceItem[];
}

export function AiTasteReportScreen() {
  const navigate = useNavigate();
  const { currentUser } = useAuth();
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [data, setData] = useState<ReportData | null>(null);

  const fetchReport = async () => {
    if (!currentUser) return;
    setLoading(true);
    setError(null);
    try {
      const response = await apiSendData({
        action: "REPORT",
        email: currentUser.email
      });
      
      if (response && response.result === "SUCCESS") {
        setData(response);
      } else {
        setError(response.message || "리포트를 불러오는 데 실패했습니다.");
      }
    } catch (err) {
      console.error("Fetch Report Error:", err);
      setError("서버와의 통신 중 오류가 발생했습니다.");
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchReport();
  }, [currentUser]);

  const getContextIcon = (label: string) => {
    if (label.includes("맑은")) return <Sun className="w-5 h-5 text-orange-500" />;
    if (label.includes("비오는")) return <CloudRain className="w-5 h-5 text-blue-500" />;
    if (label.includes("흐린")) return <Cloud className="w-5 h-5 text-gray-500" />;
    if (label.includes("눈오는")) return <Snowflake className="w-5 h-5 text-cyan-400" />;
    if (label.includes("기분") || label.includes("행복") || label.includes("설렐")) return <Heart className="w-5 h-5 text-rose-500" />;
    if (label.includes("우울") || label.includes("슬플") || label.includes("지칠")) return <Zap className="w-5 h-5 text-purple-500" />;
    return <Clock className="w-5 h-5 text-indigo-500" />;
  };

  return (
    <div key="ai-report-screen" className="flex-1 flex flex-col bg-gray-50 dark:bg-gray-950 overflow-y-auto relative pb-32 min-h-screen transition-colors duration-500">
      {/* 프리미엄 배경 글로우 */}
      <div className="absolute top-0 left-0 right-0 h-[60vh] overflow-hidden pointer-events-none">
        <div className="absolute inset-0 bg-gradient-to-b from-blue-100/40 dark:from-blue-900/10 to-transparent" />
        <motion.div 
          animate={{ 
            scale: [1, 1.2, 1],
            opacity: [0.1, 0.2, 0.1]
          }}
          transition={{ duration: 10, repeat: Infinity, ease: "easeInOut" }}
          className="absolute -top-32 -right-32 w-[500px] h-[500px] bg-blue-400 blur-[120px] rounded-full"
        />
      </div>

      <header className="px-6 pt-12 pb-6 flex items-center gap-4 z-20 relative sticky top-0 bg-gray-50/80 dark:bg-gray-950/80 backdrop-blur-md">
        <button 
          onClick={() => navigate(-1)}
          className="w-10 h-10 rounded-full flex items-center justify-center bg-white dark:bg-gray-900 shadow-sm border border-gray-200 dark:border-gray-800 transition-transform active:scale-95"
        >
          <ChevronLeft className="w-5 h-5 text-gray-700 dark:text-gray-300" />
        </button>
        <div>
          <h1 className="text-xl font-black tracking-tighter text-gray-900 dark:text-white uppercase">Scent Report</h1>
          <div className="flex items-center gap-1.5 mt-0.5">
            <div className="w-1.5 h-1.5 rounded-full bg-blue-500 animate-pulse" />
            <p className="text-[10px] font-black text-blue-500 dark:text-blue-400 tracking-[0.2em] uppercase">AI Preference Analysis</p>
          </div>
        </div>
      </header>

      <main className="px-6 py-4 flex flex-col gap-8 z-10 relative">
        <AnimatePresence mode="wait">
          {loading ? (
            <motion.div 
              key="loading"
              initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}
              className="flex flex-col items-center justify-center py-32 gap-6"
            >
              <div className="relative">
                <RefreshCw className="w-12 h-12 text-blue-500 animate-spin" />
                <div className="absolute inset-0 blur-xl bg-blue-500/20 animate-pulse" />
              </div>
              <p className="text-xs font-black text-gray-400 uppercase tracking-[0.3em] animate-pulse">Deep Learning Preference...</p>
            </motion.div>
          ) : error ? (
            <motion.div 
              key="error"
              initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0 }}
              className="bg-white dark:bg-gray-900 border border-red-100 dark:border-red-900/30 rounded-[2.5rem] p-10 flex flex-col items-center gap-6 text-center shadow-xl shadow-red-500/5"
            >
              <div className="w-16 h-16 bg-red-50 dark:bg-red-900/20 rounded-full flex items-center justify-center">
                <AlertCircle className="w-8 h-8 text-red-500" />
              </div>
              <div className="flex flex-col gap-2">
                <h3 className="text-lg font-black text-gray-900 dark:text-white uppercase tracking-tight">Analysis Interrupted</h3>
                <p className="text-sm font-medium text-gray-500 dark:text-gray-400 break-keep">{error}</p>
              </div>
              <button 
                onClick={fetchReport}
                className="w-full py-4 bg-gray-900 dark:bg-gray-100 text-white dark:text-gray-900 rounded-2xl text-xs font-black tracking-widest active:scale-[0.98] transition-all uppercase shadow-lg"
              >
                Reconnect Server
              </button>
            </motion.div>
          ) : data ? (
            <motion.div 
              key="content"
              initial={{ opacity: 0, y: 20 }} animate={{ opacity: 1, y: 0 }}
              className="flex flex-col gap-8"
            >
              {/* Main Summary Card */}
              <div className="bg-white/80 dark:bg-gray-900/80 backdrop-blur-3xl border border-white dark:border-gray-800 rounded-[2.5rem] p-8 shadow-2xl shadow-blue-500/5 overflow-hidden relative group">
                <div className="absolute -top-10 -right-10 w-40 h-40 bg-blue-500/10 rounded-full blur-3xl group-hover:scale-125 transition-transform duration-1000" />
                
                <div className="flex items-center gap-3 mb-10">
                  <div className="w-10 h-10 bg-blue-500 text-white rounded-2xl flex items-center justify-center shadow-xl shadow-blue-500/30">
                    <Sparkles className="w-5 h-5" />
                  </div>
                  <h3 className="font-black text-gray-900 dark:text-white uppercase tracking-[0.25em] text-[10px]">Prime Insight</h3>
                </div>

                <div className="min-h-[100px] flex items-center relative z-10">
                  <p className="text-2xl font-black leading-tight text-gray-900 dark:text-white break-keep tracking-tight">
                    {data.summary}
                  </p>
                </div>

                <div className="mt-12 pt-8 border-t border-gray-100 dark:border-gray-800 flex justify-between items-center">
                  <div className="flex flex-col gap-1.5">
                    <span className="text-[9px] font-black text-gray-400 uppercase tracking-[0.2em]">Data Points</span>
                    <div className="flex items-center gap-2">
                      <div className="px-2 py-0.5 bg-blue-500 text-[10px] font-black text-white rounded-md">AI</div>
                      <span className="text-lg font-black text-gray-900 dark:text-white tracking-tighter">
                        {data.total_analyzed} <span className="text-xs font-bold text-gray-400 ml-0.5 uppercase tracking-normal italic">Captured</span>
                      </span>
                    </div>
                  </div>
                  <div className="flex -space-x-2">
                    {[1,2,3].map(i => (
                      <div key={i} className="w-8 h-8 rounded-full bg-gray-100 dark:bg-gray-800 border-2 border-white dark:border-gray-900 flex items-center justify-center overflow-hidden">
                        <div className="w-full h-full bg-gradient-to-br from-blue-400 to-indigo-500 opacity-80" />
                      </div>
                    ))}
                  </div>
                </div>
              </div>

              {/* Status Section: Weather-based Preferences */}
              {data.weather_prefs && data.weather_prefs.length > 0 && (
                <div className="flex flex-col gap-5">
                  <div className="flex items-center justify-between px-2">
                    <h4 className="text-[11px] font-black text-gray-400 uppercase tracking-[0.3em] flex items-center gap-2">
                      <Sun className="w-3 h-3" /> Weather Harmony
                    </h4>
                  </div>
                  <div className="grid grid-cols-1 gap-4">
                    {data.weather_prefs.map((pref, idx) => (
                      <motion.div 
                        initial={{ opacity: 0, x: -10 }} animate={{ opacity: 1, x: 0 }} transition={{ delay: idx * 0.1 }}
                        key={pref.label} 
                        className="bg-white/70 dark:bg-gray-900/70 backdrop-blur-xl border border-white dark:border-gray-800 rounded-3xl p-5 flex items-center justify-between shadow-sm"
                      >
                        <div className="flex items-center gap-4">
                          <div className="w-12 h-12 bg-gray-50 dark:bg-gray-800 rounded-2xl flex items-center justify-center shadow-inner">
                            {getContextIcon(pref.label)}
                          </div>
                          <div className="flex flex-col">
                            <span className="text-[10px] font-bold text-gray-400 dark:text-gray-500">{pref.label}</span>
                            <span className="text-base font-black text-gray-900 dark:text-white tracking-tight">{pref.scent}</span>
                          </div>
                        </div>
                        <div className="flex flex-col items-end gap-1">
                          <div className="flex gap-0.5">
                            {[1, 2, 3].map(star => (
                              <div key={star} className={`w-1 h-1 rounded-full ${star <= Math.min(3, pref.score) ? 'bg-blue-500' : 'bg-gray-200 dark:bg-gray-700'}`} />
                            ))}
                          </div>
                          <span className="text-[10px] font-black text-blue-500 uppercase tracking-widest">Matched</span>
                        </div>
                      </motion.div>
                    ))}
                  </div>
                </div>
              )}

              {/* Emotion-based Preferences */}
              {data.emotion_prefs && data.emotion_prefs.length > 0 && (
                <div className="flex flex-col gap-5">
                  <div className="flex items-center justify-between px-2">
                    <h4 className="text-[11px] font-black text-gray-400 uppercase tracking-[0.3em] flex items-center gap-2">
                      <Heart className="w-3 h-3" /> Emotional Response
                    </h4>
                  </div>
                  <div className="grid grid-cols-2 gap-4">
                    {data.emotion_prefs.map((pref, idx) => (
                      <motion.div 
                        initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} transition={{ delay: 0.3 + idx * 0.1 }}
                        key={pref.label} 
                        className="bg-white/70 dark:bg-gray-900/70 backdrop-blur-xl border border-white dark:border-gray-800 rounded-[2rem] p-6 flex flex-col gap-4 shadow-sm"
                      >
                        <div className="w-10 h-10 bg-rose-500/10 text-rose-500 rounded-xl flex items-center justify-center shadow-inner">
                          {getContextIcon(pref.label)}
                        </div>
                        <div>
                          <p className="text-[9px] font-black text-gray-400 uppercase tracking-[0.15em] mb-1">{pref.label}</p>
                          <h4 className="text-sm font-black text-gray-900 dark:text-white leading-tight">
                            {pref.scent}
                          </h4>
                        </div>
                      </motion.div>
                    ))}
                  </div>
                </div>
              )}

              {/* Grid Stats: Best Context & Top Scent */}
              <div className="grid grid-cols-2 gap-4">
                <div className="bg-gray-900 text-white rounded-[2.2rem] p-7 flex flex-col gap-6 shadow-2xl relative overflow-hidden active:scale-[0.98] transition-all">
                  <div className="absolute top-0 right-0 w-24 h-24 bg-white/5 rounded-full -mr-10 -mt-10" />
                  <Trophy className="w-8 h-8 text-amber-400" />
                  <div>
                    <p className="text-[9px] font-black text-white/50 uppercase tracking-[0.25em] mb-1.5">Hall of Fame</p>
                    <h4 className="text-lg font-black tracking-tight leading-tight">
                      {data.top_scent}
                    </h4>
                  </div>
                </div>
                
                <div className="bg-blue-600 text-white rounded-[2.2rem] p-7 flex flex-col gap-6 shadow-2xl relative overflow-hidden active:scale-[0.98] transition-all">
                  <div className="absolute bottom-0 right-0 w-24 h-24 bg-black/10 rounded-full -mr-12 -mb-12" />
                  <BarChart3 className="w-8 h-8 text-white/90" />
                  <div>
                    <p className="text-[9px] font-black text-white/50 uppercase tracking-[0.25em] mb-1.5">Dominant Vibe</p>
                    <h4 className="text-lg font-black tracking-tight leading-tight">
                      {data.top_context}
                    </h4>
                  </div>
                </div>
              </div>

              {/* Tip Section */}
              <div className="bg-white dark:bg-gray-900/50 backdrop-blur-sm rounded-[2.5rem] p-8 border border-gray-100 dark:border-gray-800 shadow-sm">
                <div className="flex gap-6 items-start">
                  <div className="w-12 h-12 bg-blue-50 dark:bg-blue-900/20 rounded-2xl flex items-center justify-center shadow-inner shrink-0">
                    <Zap className="w-6 h-6 text-blue-500" />
                  </div>
                  <div className="flex flex-col gap-2">
                    <h5 className="text-sm font-black text-gray-900 dark:text-gray-100 uppercase tracking-wider">Smart Learning Tip</h5>
                    <p className="text-xs font-medium text-gray-500 dark:text-gray-400 leading-relaxed break-keep">
                      디퓨저가 분사될 때 '좋아요' 피드백을 더 많이 남겨주세요. AI가 {currentUser?.name || "사용자"}님의 취향을 정밀하게 분석하여 상황별 골든 믹스를 찾아냅니다.
                    </p>
                  </div>
                </div>
              </div>
            </motion.div>
          ) : null}
        </AnimatePresence>
      </main>
    </div>
  );
}
