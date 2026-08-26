import React, { useState, useEffect } from "react";
import { Link, useNavigate } from "react-router";
import { BottomNav } from "./BottomNav";
import { motion } from "motion/react";
import { 
  Home, 
  Droplets, 
  SlidersHorizontal, 
  Settings as SettingsIcon,
  Bell,
  Moon,
  Shield,
  CircleHelp,
  LogOut,
  ChevronRight,
  Scale,
  Music,
  BookHeart,
  Timer,
  Zap
} from "lucide-react";
import { useAuth } from "../store/AuthContext";
import { useDevice } from "../store/DeviceContext";

export function SettingsScreen() {
  const navigate = useNavigate();
  const { logoutUser } = useAuth();
  const { 
    intensity, 
    handleIntensityChange,
    timerEnabled,
    timerStart,
    timerEnd,
    updateTimerSettings
  } = useDevice();

  const [darkMode, setDarkMode] = useState(() => document.documentElement.classList.contains("dark"));

  useEffect(() => {
    if (darkMode) {
      document.documentElement.classList.add("dark");
    } else {
      document.documentElement.classList.remove("dark");
    }
  }, [darkMode]);

  const handleLogout = () => {
    logoutUser();
    navigate("/");
  };

  return (
    <div key="settings-screen" className="flex-1 flex flex-col bg-gray-50 dark:bg-gray-950 overflow-y-auto relative pb-32 min-h-screen transition-colors duration-300">
      <header className="px-6 pt-12 pb-4 flex justify-between items-center z-10 sticky top-0 bg-gray-50/90 dark:bg-gray-950/90 backdrop-blur-md transition-colors duration-300">
        <div>
          <h1 className="text-2xl font-bold tracking-tight text-gray-900 dark:text-white">설정</h1>
          <p className="text-sm font-medium text-gray-500 dark:text-gray-400 mt-1">기기 관리 및 앱 설정</p>
        </div>
      </header>

      <div className="px-6 py-4 flex flex-col gap-6">
        {/* Device Info */}
        <motion.div 
          initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }}
          className="bg-white dark:bg-gray-900 rounded-3xl p-5 shadow-sm border border-gray-200 dark:border-gray-800 flex items-center gap-4 transition-colors duration-300"
        >
          <div className="w-16 h-16 bg-gray-100 dark:bg-gray-800 rounded-full flex items-center justify-center border border-gray-200 dark:border-gray-700 transition-colors duration-300">
            <SlidersHorizontal className="text-gray-600 dark:text-gray-300 w-8 h-8" />
          </div>
          <div className="flex-1">
            <h2 className="text-lg font-bold text-gray-900 dark:text-white">Smart Diffuser</h2>
          </div>
          <Link to="/device" className="px-3 py-2 bg-gray-100 dark:bg-gray-800 hover:bg-gray-200 dark:hover:bg-gray-700 text-gray-700 dark:text-gray-200 rounded-xl text-xs font-bold transition-colors">
            기기 관리
          </Link>
        </motion.div>

        {/* Timer Schedule */}
        <div className="bg-white dark:bg-gray-900 rounded-3xl p-5 border border-gray-200 dark:border-gray-800 shadow-sm flex flex-col gap-4">
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-3">
              <Timer className="w-5 h-5 text-blue-500" />
              <span className="font-bold text-gray-800 dark:text-gray-200">자동 스케줄러</span>
            </div>
            <button 
              onClick={() => updateTimerSettings(!timerEnabled, timerStart, timerEnd)}
              className={`w-12 h-6 rounded-full p-1 transition-colors flex ${timerEnabled ? 'bg-blue-600 justify-end' : 'bg-gray-200 dark:bg-gray-700 justify-start'}`}
            >
              <motion.div layout className="w-4 h-4 bg-white rounded-full shadow-sm" />
            </button>
          </div>

          <div className={`flex items-center gap-4 transition-opacity ${timerEnabled ? "opacity-100" : "opacity-40 pointer-events-none"}`}>
            <div className="flex-1 flex flex-col gap-1">
              <span className="text-[10px] font-bold text-gray-400 uppercase">시작 시간</span>
              <select 
                value={timerStart}
                onChange={(e) => updateTimerSettings(timerEnabled, parseInt(e.target.value), timerEnd)}
                className="bg-gray-50 dark:bg-gray-800 border-none rounded-xl px-3 py-2 text-sm font-bold outline-none text-gray-800 dark:text-gray-200"
              >
                {Array.from({length: 24}, (_, i) => (
                  <option key={i} value={i}>{i < 10 ? `0${i}` : i}:00</option>
                ))}
              </select>
            </div>
            <ChevronRight className="w-4 h-4 text-gray-300 mt-4" />
            <div className="flex-1 flex flex-col gap-1">
              <span className="text-[10px] font-bold text-gray-400 uppercase">종료 시간</span>
              <select 
                value={timerEnd}
                onChange={(e) => updateTimerSettings(timerEnabled, timerStart, parseInt(e.target.value))}
                className="bg-gray-50 dark:bg-gray-800 border-none rounded-xl px-3 py-2 text-sm font-bold outline-none text-gray-800 dark:text-gray-200"
              >
                {Array.from({length: 24}, (_, i) => (
                  <option key={i} value={i}>{i < 10 ? `0${i}` : i}:00</option>
                ))}
              </select>
            </div>
          </div>
          <p className="text-[11px] font-medium text-gray-500 leading-tight">
            설정된 시간 동안만 디퓨저가 활성화됩니다. 야간에는 자동으로 절전 모드로 진입합니다.
          </p>
        </div>

        {/* Preferences */}
        <div>
          <h3 className="text-sm font-bold text-gray-500 dark:text-gray-400 mb-3 px-1">기본 설정</h3>
          <div className="bg-white dark:bg-gray-900 rounded-3xl border border-gray-200 dark:border-gray-800 shadow-sm overflow-hidden transition-colors duration-300">
            <div className="flex items-center justify-between p-4 border-b border-gray-100 dark:border-gray-800">
              <div className="flex items-center gap-3">
                <div className="w-10 h-10 bg-gray-50 dark:bg-gray-800 rounded-xl flex items-center justify-center text-gray-600 dark:text-gray-300 transition-colors duration-300">
                  <Moon className="w-5 h-5" />
                </div>
                <span className="font-bold text-gray-800 dark:text-gray-200">다크 모드</span>
              </div>
              <button 
                onClick={() => setDarkMode(!darkMode)}
                className={`w-12 h-6 rounded-full p-1 transition-colors flex ${darkMode ? 'bg-gray-900 dark:bg-blue-600 justify-end' : 'bg-gray-200 dark:bg-gray-700 justify-start'}`}
              >
                <motion.div layout className="w-4 h-4 bg-white rounded-full shadow-sm" />
              </button>
            </div>

            <Link to="/privacy" className="w-full flex items-center justify-between p-4 border-b border-gray-100 dark:border-gray-800 hover:bg-gray-50 dark:hover:bg-gray-800 transition-colors text-left">
              <div className="flex items-center gap-3">
                <div className="w-10 h-10 bg-gray-50 dark:bg-gray-800 rounded-xl flex items-center justify-center text-gray-600 dark:text-gray-300 transition-colors duration-300">
                  <Shield className="w-5 h-5" />
                </div>
                <span className="font-bold text-gray-800 dark:text-gray-200">개인정보 보호</span>
              </div>
              <ChevronRight className="w-5 h-5 text-gray-400 dark:text-gray-500" />
            </Link>

            <Link to="/ai-report" className="w-full flex items-center justify-between p-4 hover:bg-gray-50 dark:hover:bg-gray-800 transition-colors text-left">
              <div className="flex items-center gap-3">
                <div className="w-10 h-10 bg-blue-50 dark:bg-blue-900/20 rounded-xl flex items-center justify-center text-blue-600 dark:text-blue-400 transition-colors duration-300">
                  <BookHeart className="w-5 h-5" />
                </div>
                <span className="font-bold text-gray-800 dark:text-gray-200">AI 취향 분석 리포트</span>
              </div>
              <ChevronRight className="w-5 h-5 text-gray-400 dark:text-gray-500" />
            </Link>
          </div>
        </div>

        {/* Diffuser Settings */}
        <div>
          <h3 className="text-sm font-bold text-gray-500 dark:text-gray-400 mb-3 px-1">디퓨저 설정</h3>
          <div className="bg-white dark:bg-gray-900 rounded-3xl border border-gray-200 dark:border-gray-800 shadow-sm overflow-hidden transition-colors duration-300">
            <Link to="/music" className="w-full flex items-center justify-between p-4 hover:bg-gray-50 dark:hover:bg-gray-800 transition-colors text-left">
              <div className="flex items-center gap-3">
                <div className="w-10 h-10 bg-gray-50 dark:bg-gray-800 rounded-xl flex items-center justify-center text-gray-600 dark:text-gray-300 transition-colors duration-300">
                  <Music className="w-5 h-5" />
                </div>
                <span className="font-bold text-gray-800 dark:text-gray-200">음악 설정</span>
              </div>
              <ChevronRight className="w-5 h-5 text-gray-400 dark:text-gray-500" />
            </Link>
          </div>
        </div>

        {/* Support */}
        <div>
          <h3 className="text-sm font-bold text-gray-500 dark:text-gray-400 mb-3 px-1">고객 지원</h3>
          <div className="bg-white dark:bg-gray-900 rounded-3xl border border-gray-200 dark:border-gray-800 shadow-sm overflow-hidden transition-colors duration-300">
            <button 
              onClick={() => window.open("http://pf.kakao.com/_fhxhxjX/chat", "_blank")}
              className="w-full flex items-center justify-between p-4 border-b border-gray-100 dark:border-gray-800 hover:bg-gray-50 dark:hover:bg-gray-800 transition-colors text-left"
            >
              <div className="flex items-center gap-3">
                <div className="w-10 h-10 bg-yellow-50 dark:bg-yellow-900/20 rounded-xl flex items-center justify-center text-yellow-600 dark:text-yellow-400 transition-colors duration-300">
                  <CircleHelp className="w-5 h-5" />
                </div>
                <span className="font-bold text-gray-800 dark:text-gray-200">카카오톡 챗봇 문의</span>
              </div>
              <ChevronRight className="w-5 h-5 text-gray-400 dark:text-gray-500" />
            </button>

            <button 
              onClick={handleLogout}
              className="w-full flex items-center justify-between p-4 hover:bg-red-50 dark:hover:bg-red-900/10 transition-colors text-left"
            >
              <div className="flex items-center gap-3">
                <div className="w-10 h-10 bg-red-50 dark:bg-red-500/10 rounded-xl flex items-center justify-center text-red-500 transition-colors duration-300">
                  <LogOut className="w-5 h-5" />
                </div>
                <span className="font-bold text-red-500">로그아웃</span>
              </div>
              <ChevronRight className="w-5 h-5 text-gray-400 dark:text-gray-500" />
            </button>
          </div>
        </div>
      </div>

      <BottomNav />
    </div>
  );
}