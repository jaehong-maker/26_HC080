import React, { useState } from "react";
import { useNavigate } from "react-router";
import { motion } from "motion/react";
import { ArrowLeft, Mail, Lock, MapPin, CheckCircle2, Shield, Loader2 } from "lucide-react";
import { useAuth } from "../store/AuthContext";

export function PrivacyScreen() {
  const navigate = useNavigate();
  const { currentUser, updateUser } = useAuth();
  
  const [isEditing, setIsEditing] = useState(false);
  const [loading, setLoading] = useState(false);
  const [successMsg, setSuccessMsg] = useState("");
  const [errorMsg, setErrorMsg] = useState("");
  
  const [oldPassword, setOldPassword] = useState("");
  const [newPassword, setNewPassword] = useState("");
  const [region, setRegion] = useState(currentUser?.region || "seoul");

  const handleSave = async () => {
    if (!currentUser) return;
    
    setLoading(true);
    setErrorMsg("");
    setSuccessMsg("");

    const updates: any = { region };
    if (newPassword) updates.password = newPassword;
    
    try {
      // 서버 API 호출 (oldPassword를 함께 전달하여 서버에서 검증)
      const result = await updateUser(currentUser.email, updates, oldPassword);
      
      if (result.success) {
        setIsEditing(false);
        setSuccessMsg(result.message);
        setOldPassword("");
        setNewPassword("");
        setTimeout(() => setSuccessMsg(""), 3000);
      } else {
        setErrorMsg(result.message);
      }
    } catch (err) {
      setErrorMsg("서버 통신 중 오류가 발생했습니다.");
    } finally {
      setLoading(false);
    }
  };

  return (
    <div key="privacy-screen" className="h-screen w-full bg-gray-50 dark:bg-gray-950 flex flex-col fixed inset-0 overflow-hidden transition-colors duration-300">
      <header className="px-6 pt-12 pb-4 flex items-center justify-between z-10 bg-gray-50/90 dark:bg-gray-950/90 backdrop-blur-md border-b border-gray-200/50 dark:border-gray-800/50 shrink-0 transition-colors duration-300">
        <div className="flex items-center">
          <button 
            onClick={() => navigate(-1)}
            className="w-10 h-10 -ml-2 rounded-full flex items-center justify-center text-gray-900 dark:text-white hover:bg-gray-200 dark:hover:bg-gray-800 transition-colors"
          >
            <ArrowLeft className="w-6 h-6" />
          </button>
          <h1 className="text-xl font-bold tracking-tight text-gray-900 dark:text-white ml-2">개인정보 보호</h1>
        </div>
        <div className="w-10 h-10 bg-gray-100 dark:bg-gray-800 rounded-full flex items-center justify-center text-gray-400 dark:text-gray-500">
          <Shield className="w-5 h-5" />
        </div>
      </header>

      <div className="flex-1 px-6 py-6 flex flex-col gap-5 overflow-y-auto custom-scrollbar pb-12">
        <div className="mb-1">
          <h2 className="text-2xl font-bold text-gray-900 dark:text-white">내 계정 정보</h2>
          <p className="text-sm font-medium text-gray-500 dark:text-gray-400 mt-1">
            가입된 정보를 확인하고 비밀번호와 지역을 변경할 수 있습니다.
          </p>
        </div>

        {successMsg && (
          <motion.div initial={{ opacity: 0, y: -10 }} animate={{ opacity: 1, y: 0 }} className="p-4 bg-green-50 dark:bg-green-900/30 border border-green-200 dark:border-green-800 text-green-700 dark:text-green-400 rounded-2xl flex items-center gap-2 font-bold text-sm">
            <CheckCircle2 className="w-5 h-5" />
            {successMsg}
          </motion.div>
        )}
        
        {errorMsg && (
          <motion.div initial={{ opacity: 0, y: -10 }} animate={{ opacity: 1, y: 0 }} className="p-4 bg-red-50 dark:bg-red-900/30 border border-red-200 dark:border-red-800 text-red-700 dark:text-red-400 rounded-2xl flex items-center gap-2 font-bold text-sm">
            {errorMsg}
          </motion.div>
        )}

        <motion.div initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} className="flex flex-col gap-2">
          <label className="text-sm font-bold text-gray-700 dark:text-gray-300 ml-1">이메일 (가입 계정)</label>
          <div className="relative">
            <div className="absolute left-4 top-0 bottom-0 flex items-center text-gray-400 dark:text-gray-500">
              <Mail className="w-5 h-5" />
            </div>
            <input 
              type="email" 
              value={currentUser?.email || "user@example.com"}
              disabled
              className="w-full h-14 bg-gray-200/50 dark:bg-gray-800 border border-gray-200/50 dark:border-gray-700/50 text-gray-500 dark:text-gray-500 rounded-2xl pl-12 pr-4 font-medium focus:outline-none transition-colors"
            />
          </div>
          <p className="text-[11px] text-gray-400 dark:text-gray-500 ml-2 mt-1">* 이메일은 가입 후 변경할 수 없습니다.</p>
        </motion.div>

        <motion.div initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} transition={{ delay: 0.1 }} className="flex flex-col gap-2">
          <label className="text-sm font-bold text-gray-700 dark:text-gray-300 ml-1">기존 비밀번호</label>
          <div className="relative">
            <div className="absolute left-4 top-0 bottom-0 flex items-center text-gray-400 dark:text-gray-500">
              <Lock className="w-5 h-5" />
            </div>
            <input 
              type="password" 
              value={oldPassword}
              onChange={(e) => setOldPassword(e.target.value)}
              placeholder="기존 비밀번호 입력" 
              disabled={!isEditing}
              className={`w-full h-14 rounded-2xl pl-12 pr-4 font-medium focus:outline-none transition-colors ${
                isEditing 
                  ? 'bg-white dark:bg-gray-900 border border-gray-200 dark:border-gray-700 text-gray-900 dark:text-white placeholder:text-gray-400 shadow-sm focus:ring-2 focus:ring-gray-900 dark:focus:ring-gray-100' 
                  : 'bg-gray-100 dark:bg-gray-800/50 border border-transparent text-gray-500 dark:text-gray-400 placeholder:text-gray-400/50'
              }`}
            />
          </div>
        </motion.div>
        
        <motion.div initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} transition={{ delay: 0.1 }} className="flex flex-col gap-2">
          <label className="text-sm font-bold text-gray-700 dark:text-gray-300 ml-1">변경할 비밀번호</label>
          <div className="relative">
            <div className="absolute left-4 top-0 bottom-0 flex items-center text-gray-400 dark:text-gray-500">
              <Lock className="w-5 h-5" />
            </div>
            <input 
              type="password" 
              value={newPassword}
              onChange={(e) => setNewPassword(e.target.value)}
              placeholder="새 비밀번호 입력" 
              disabled={!isEditing}
              className={`w-full h-14 rounded-2xl pl-12 pr-4 font-medium focus:outline-none transition-colors ${
                isEditing 
                  ? 'bg-white dark:bg-gray-900 border border-gray-200 dark:border-gray-700 text-gray-900 dark:text-white placeholder:text-gray-400 shadow-sm focus:ring-2 focus:ring-gray-900 dark:focus:ring-gray-100' 
                  : 'bg-gray-100 dark:bg-gray-800/50 border border-transparent text-gray-500 dark:text-gray-400 placeholder:text-gray-400/50'
              }`}
            />
          </div>
        </motion.div>

        <motion.div initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} transition={{ delay: 0.2 }} className="flex flex-col gap-2">
          <label className="text-sm font-bold text-gray-700 dark:text-gray-300 ml-1">거주 지역 설정</label>
          <div className="relative">
            <div className="absolute left-4 top-0 bottom-0 flex items-center text-gray-400 dark:text-gray-500 pointer-events-none">
              <MapPin className="w-5 h-5" />
            </div>
            <select 
              value={region}
              onChange={(e) => setRegion(e.target.value)}
              disabled={!isEditing}
              className={`w-full h-14 rounded-2xl pl-12 pr-4 font-bold focus:outline-none transition-colors appearance-none ${
                isEditing 
                  ? 'bg-white dark:bg-gray-900 border border-gray-200 dark:border-gray-700 text-gray-900 dark:text-white shadow-sm focus:ring-2 focus:ring-gray-900 dark:focus:ring-gray-100' 
                  : 'bg-gray-100 dark:bg-gray-800/50 border border-transparent text-gray-600 dark:text-gray-400'
              }`}
            >
              <option value="seoul">서울</option>
              <option value="gapyeong">가평</option>
              <option value="goyang">고양</option>
              <option value="gwangmyeong">광명</option>
              <option value="gwangju_gyeonggi">광주(경기)</option>
              <option value="guri">구리</option>
              <option value="gimpo">김포</option>
              <option value="namyangju">남양주</option>
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
              <svg className={`w-4 h-4 ${isEditing ? 'text-gray-500 dark:text-gray-400' : 'text-transparent'}`} fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M19 9l-7 7-7-7"></path></svg>
            </div>
          </div>
          <p className="text-[11px] text-gray-400 dark:text-gray-500 ml-2 mt-1">* 지역 정보는 맞춤형 향기 추천(날씨 모드 등)에 활용됩니다.</p>
        </motion.div>

        <motion.div initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} transition={{ delay: 0.3 }} className="mt-auto pt-6">
          {isEditing ? (
            <div className="flex gap-3">
              <button 
                onClick={() => setIsEditing(false)}
                className="flex-1 h-14 bg-gray-200 dark:bg-gray-800 text-gray-700 dark:text-gray-300 rounded-2xl flex items-center justify-center font-bold text-lg hover:bg-gray-300 dark:hover:bg-gray-700 transition-colors"
              >
                취소
              </button>
              <button 
                onClick={handleSave}
                className="flex-[2] h-14 bg-gray-900 dark:bg-gray-100 text-white dark:text-gray-900 rounded-2xl flex items-center justify-center font-bold text-lg shadow-md hover:bg-gray-800 dark:hover:bg-gray-200 transition-colors"
              >
                저장하기
              </button>
            </div>
          ) : (
            <button 
              onClick={() => setIsEditing(true)}
              className="w-full h-14 bg-gray-900 dark:bg-gray-100 text-white dark:text-gray-900 rounded-2xl flex items-center justify-center font-bold text-lg shadow-md hover:bg-gray-800 dark:hover:bg-gray-200 transition-colors"
            >
              정보 수정하기
            </button>
          )}
        </motion.div>
      </div>
    </div>
  );
}