import React, { useState } from "react";
import { Link, useNavigate } from "react-router";
import { motion } from "motion/react";
import { ArrowLeft, Mail, Lock, CheckCircle2, MapPin, Loader2, Cpu } from "lucide-react";
import { useAuth } from "../store/AuthContext";
import { apiSendAuthCode } from "../utils/api";

export function SignupScreen() {
  const navigate = useNavigate();
  const { registerUser } = useAuth();

  const [email, setEmail] = useState("");
  const [code, setCode] = useState("");
  const [password, setPassword] = useState("");
  const [region, setRegion] = useState("seoul");
  const [deviceId, setDeviceId] = useState("");

  const [codeSent, setCodeSent] = useState(false);
  const [error, setError] = useState("");
  const [successMsg, setSuccessMsg] = useState("");
  const [loading, setLoading] = useState(false);

  /**
   * 인증번호 발송
   */
  const handleSendCode = async () => {
    const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
    if (!emailRegex.test(email)) {
      setError("올바른 이메일 형식을 입력해주세요.");
      return;
    }

    setLoading(true);
    setError("");
    setSuccessMsg("");

    try {
      const response = await apiSendAuthCode(email);
      if (response.result === "SUCCESS") {
        setCodeSent(true);
        setSuccessMsg(response.message || "인증번호가 발송되었습니다.");
      } else {
        setError(response.message || "인증번호 발송에 실패했습니다.");
      }
    } catch (err) {
      setError("서버 통신 중 오류가 발생했습니다.");
    } finally {
      setLoading(false);
    }
  };

  /**
   * 회원가입 완료
   */
  const handleSignup = async (e: React.FormEvent) => {
    e.preventDefault();
    setError("");
    setSuccessMsg("");

    if (!codeSent) {
      setError("먼저 이메일 인증을 진행해주세요.");
      return;
    }
    if (code.length !== 6 || !/^\d+$/.test(code)) {
      setError("올바른 6자리 인증번호를 입력해주세요.");
      return;
    }
    if (password.length < 4) {
      setError("비밀번호는 최소 4자 이상이어야 합니다.");
      return;
    }
    if (!deviceId.trim()) {
      setError("기기 고유 번호(Device ID)를 입력해주세요.");
      return;
    }

    setLoading(true);

    try {
      const result = await registerUser(email, password, region, code, deviceId.trim());
      if (result.success) {
        alert("회원가입이 완료되었습니다. 로그인 해주세요.");
        navigate("/");
      } else {
        setError(result.message || "회원가입에 실패했습니다.");
      }
    } catch (err) {
      setError("회원가입 처리 중 오류가 발생했습니다.");
    } finally {
      setLoading(false);
    }
  };

  return (
    <div key="signup-screen" className="h-screen w-full bg-white dark:bg-gray-950 flex flex-col relative fixed inset-0 overflow-y-auto transition-colors duration-500">
      <header className="px-6 pt-12 pb-4 flex items-center z-10 sticky top-0 bg-white/80 dark:bg-gray-950/80 backdrop-blur-md border-b border-gray-100 dark:border-gray-800 shrink-0 transition-colors duration-300">
        <button 
          onClick={() => navigate(-1)}
          className="w-10 h-10 -ml-2 rounded-full flex items-center justify-center text-gray-900 dark:text-gray-100 hover:bg-gray-100 dark:hover:bg-gray-800 transition-colors"
        >
          <ArrowLeft className="w-6 h-6" />
        </button>
        <h1 className="text-xl font-bold tracking-tight text-gray-900 dark:text-white ml-2">회원가입</h1>
      </header>

      <form onSubmit={handleSignup} className="flex-1 px-8 py-6 flex flex-col gap-4">
        {error && (
          <motion.div initial={{ opacity: 0, y: -10 }} animate={{ opacity: 1, y: 0 }} className="p-3 bg-red-50 dark:bg-red-900/20 text-red-600 dark:text-red-400 text-sm font-semibold rounded-xl border border-red-100 dark:border-red-900/30">
            {error}
          </motion.div>
        )}
        {successMsg && (
          <motion.div initial={{ opacity: 0, y: -10 }} animate={{ opacity: 1, y: 0 }} className="p-3 bg-green-50 dark:bg-green-900/20 text-green-700 dark:text-green-400 text-sm font-semibold rounded-xl border border-green-100 dark:border-green-900/30">
            {successMsg}
          </motion.div>
        )}

        <motion.div initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} className="flex flex-col gap-1.5">
          <label className="text-sm font-bold text-gray-700 dark:text-gray-300 ml-1">이메일</label>
          <div className="flex gap-2">
            <div className="relative flex-1">
              <div className="absolute left-4 top-0 bottom-0 flex items-center text-gray-400 dark:text-gray-500">
                <Mail className="w-5 h-5" />
              </div>
              <input 
                type="email" 
                value={email}
                onChange={(e) => setEmail(e.target.value)}
                placeholder="example@email.com" 
                disabled={loading || codeSent}
                className="w-full h-12 sm:h-14 bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 shadow-sm text-gray-900 dark:text-gray-100 rounded-2xl pl-12 pr-4 font-medium placeholder:text-gray-400 dark:placeholder:text-gray-500 focus:outline-none focus:ring-2 focus:ring-gray-900/10 dark:focus:ring-white/10 transition-all text-sm sm:text-base disabled:opacity-50"
              />
            </div>
            <button 
              type="button"
              onClick={handleSendCode}
              disabled={loading || codeSent}
              className="px-4 h-12 sm:h-14 bg-gray-100 dark:bg-gray-800 hover:bg-gray-200 dark:hover:bg-gray-700 text-gray-900 dark:text-gray-100 font-bold text-xs sm:text-sm rounded-2xl transition-colors whitespace-nowrap flex items-center justify-center min-w-[80px] disabled:opacity-50 border border-transparent dark:border-gray-700"
            >
              {loading && !codeSent ? <Loader2 className="w-4 h-4 animate-spin" /> : codeSent ? "발송완료" : "인증받기"}
            </button>
          </div>
        </motion.div>

        {codeSent && (
          <motion.div initial={{ opacity: 0, height: 0 }} animate={{ opacity: 1, height: "auto" }} className="flex flex-col gap-1.5 overflow-hidden">
            <label className="text-sm font-bold text-gray-700 dark:text-gray-300 ml-1">인증번호 6자리</label>
            <div className="relative">
              <div className="absolute left-4 top-0 bottom-0 flex items-center text-gray-400 dark:text-gray-500">
                <CheckCircle2 className="w-5 h-5" />
              </div>
              <input 
                type="text" 
                maxLength={6}
                value={code}
                onChange={(e) => setCode(e.target.value)}
                placeholder="000000" 
                disabled={loading}
                className="w-full h-12 sm:h-14 bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 shadow-sm text-gray-900 dark:text-gray-100 rounded-2xl pl-12 pr-4 font-medium placeholder:text-gray-400 dark:placeholder:text-gray-500 focus:outline-none focus:ring-2 focus:ring-gray-900/10 dark:focus:ring-white/10 transition-all text-sm sm:text-base disabled:opacity-50"
              />
            </div>
          </motion.div>
        )}

        <motion.div initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} transition={{ delay: 0.1 }} className="flex flex-col gap-1.5">
          <label className="text-sm font-bold text-gray-700 dark:text-gray-300 ml-1">비밀번호</label>
          <div className="relative">
            <div className="absolute left-4 top-0 bottom-0 flex items-center text-gray-400 dark:text-gray-500">
              <Lock className="w-5 h-5" />
            </div>
            <input 
              type="password" 
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              placeholder="최소 4자 이상" 
              disabled={loading}
              className="w-full h-12 sm:h-14 bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 shadow-sm text-gray-900 dark:text-gray-100 rounded-2xl pl-12 pr-4 font-medium placeholder:text-gray-400 dark:placeholder:text-gray-500 focus:outline-none focus:ring-2 focus:ring-gray-900/10 dark:focus:ring-white/10 transition-all text-sm sm:text-base disabled:opacity-50"
            />
          </div>
        </motion.div>

        <motion.div initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} transition={{ delay: 0.15 }} className="flex flex-col gap-1.5">
          <label className="text-sm font-bold text-gray-700 dark:text-gray-300 ml-1">기기 고유 번호 (Device ID)</label>
          <div className="relative">
            <div className="absolute left-4 top-0 bottom-0 flex items-center text-gray-400 dark:text-gray-500">
              <Cpu className="w-5 h-5" />
            </div>
            <input 
              type="text" 
              value={deviceId}
              onChange={(e) => setDeviceId(e.target.value)}
              placeholder="예: ESP_123456" 
              disabled={loading}
              className="w-full h-12 sm:h-14 bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 shadow-sm text-gray-900 dark:text-gray-100 rounded-2xl pl-12 pr-4 font-medium placeholder:text-gray-400 dark:placeholder:text-gray-500 focus:outline-none focus:ring-2 focus:ring-gray-900/10 dark:focus:ring-white/10 transition-all text-sm sm:text-base disabled:opacity-50"
            />
          </div>
          <p className="text-[10px] text-gray-400 ml-1 font-medium">* 제품에서 제공하는 Device ID를 입력해주세요. (기기 디스플레이에서 확인 가능)</p>
        </motion.div>

        <motion.div initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} transition={{ delay: 0.2 }} className="flex flex-col gap-1.5">
          <label className="text-sm font-bold text-gray-700 dark:text-gray-300 ml-1">거주 지역</label>
          <div className="relative">
            <div className="absolute left-4 top-0 bottom-0 flex items-center text-gray-400 dark:text-gray-500">
              <MapPin className="w-5 h-5" />
            </div>
            <select 
              value={region}
              onChange={(e) => setRegion(e.target.value)}
              disabled={loading}
              className="w-full h-12 sm:h-14 bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 shadow-sm text-gray-900 dark:text-gray-100 rounded-2xl pl-12 pr-10 font-medium appearance-none focus:outline-none focus:ring-2 focus:ring-gray-900/10 dark:focus:ring-white/10 transition-all text-sm sm:text-base disabled:opacity-50"
            >
              <option value="seoul">서울</option>
              <option value="goyang">고양</option>
              <option value="gwacheon">과천</option>
              <option value="gwangmyeong">광명</option>
              <option value="gwangju">광주</option>
              <option value="guri">구리</option>
              <option value="gunpo">군포</option>
              <option value="gimpo">김포</option>
              <option value="namyangju">남양주</option>
              <option value="dongducheon">동두천</option>
              <option value="bucheon">부천</option>
              <option value="seongnam">성남</option>
              <option value="suwon">수원</option>
              <option value="siheung">시흥</option>
              <option value="ansan">안산</option>
              <option value="anyang">안양</option>
              <option value="yangju">양주</option>
              <option value="yangpyeong">양평</option>
              <option value="yongin">용인</option>
              <option value="uijeongbu">의정부</option>
              <option value="icheon">이천</option>
              <option value="paju">파주</option>
              <option value="pyeongtaek">평택</option>
              <option value="pocheon">포천</option>
              <option value="hwaseong">화성</option>
              <option value="gangwon">강원</option>
              <option value="gyeongnam">경남</option>
              <option value="gyeongbuk">경북</option>
              <option value="gwangju_jeolla">광주(전라)</option>
              <option value="daegu">대구</option>
              <option value="daejeon">대전</option>
              <option value="busan">부산</option>
              <option value="sejong">세종</option>
              <option value="ulleungdo">울릉도</option>
              <option value="ulsan">울산</option>
              <option value="incheon">인천</option>
              <option value="jeonnam">전남</option>
              <option value="jeonbuk">전북</option>
              <option value="jeju">제주</option>
              <option value="chungnam">충남</option>
              <option value="chungbuk">충북</option>
            </select>
            <div className="absolute right-4 top-0 bottom-0 flex items-center pointer-events-none">
              <svg className="w-4 h-4 text-gray-400 dark:text-gray-500" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M19 9l-7 7-7-7"></path></svg>
            </div>
          </div>
        </motion.div>

        <motion.div initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} transition={{ delay: 0.4 }} className="mt-4 pb-8 sm:pb-12">
          <button 
            type="submit"
            disabled={loading}
            className="w-full h-12 sm:h-14 bg-gray-900 dark:bg-gray-100 text-white dark:text-gray-900 rounded-2xl flex items-center justify-center font-bold text-base sm:text-lg shadow-md hover:bg-gray-800 dark:hover:bg-white transition-colors cursor-pointer disabled:opacity-70 disabled:cursor-not-allowed"
          >
            {loading ? <Loader2 className="w-6 h-6 animate-spin" /> : "가입 완료하기"}
          </button>
        </motion.div>
      </form>
    </div>
  );
}
