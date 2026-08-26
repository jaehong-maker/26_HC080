import React, { useState, useMemo, useEffect } from "react";
import { useNavigate } from "react-router";
import { motion, AnimatePresence } from "motion/react";
import { 
  ArrowLeft, Music, Check, Search, RefreshCw, X, ChevronUp, ChevronDown, Trash2
} from "lucide-react";

import { TRACKS } from "../utils/tracks";

import { useDevice } from "../store/DeviceContext";
import { useAuth } from "../store/AuthContext";

export function MusicSettingsScreen() {
  const navigate = useNavigate();
  const { currentUser, updateUser } = useAuth();
  const { scentSlots, sendDeviceData } = useDevice();
  const [isSaving, setIsSaving] = useState(false);

  // 4개의 카트리지(슬롯) 데이터 - 다중 곡 선택 지원 (selectedTrackIds 배열)
  const [slots, setSlots] = useState(() => {
    // 기본값 설정 (1, 6, 11, 16번 곡)
    let initialSlotStrings = ["1", "6", "11", "16"];
    
    // 서버에서 저장된 데이터가 있다면 해당 데이터 사용 (예: "1,2_6,7_11_16")
    if (currentUser?.musicTracks && currentUser.musicTracks.includes("_")) {
      initialSlotStrings = currentUser.musicTracks.split("_");
    }
    
    return scentSlots.map((s, i) => {
      const rawSlotStr = initialSlotStrings[i] || "0";
      const trackNums = rawSlotStr.split(",").map(t => t.trim()).filter(Boolean);
      const selectedTrackIds = trackNums.map(n => (n === "0" ? "none" : `song_${n}`));
      
      return {
        id: s.id,
        scentName: s.name,
        selectedTrackIds: selectedTrackIds.length > 0 ? selectedTrackIds : ["none"]
      };
    });
  });

  const [isModalOpen, setIsModalOpen] = useState(false);
  const [activeSlotId, setActiveSlotId] = useState<number | null>(null);
  const [searchTerm, setSearchTerm] = useState("");
  const [isDirty, setIsDirty] = useState(false);

  // 멀티 기기 및 동기화 처리 (사용자가 편집 중이거나 저장 중인 경우 덮어쓰기 방지)
  useEffect(() => {
    if (isDirty || isModalOpen || isSaving) return;
    if (!currentUser?.musicTracks || !currentUser.musicTracks.includes("_")) return;

    const latestTrackStrings = currentUser.musicTracks.split("_");
    setSlots(scentSlots.map((s, i) => {
      const rawSlotStr = latestTrackStrings[i] || "0";
      const trackNums = rawSlotStr.split(",").map(t => t.trim()).filter(Boolean);
      const selectedTrackIds = trackNums.map(n => (n === "0" ? "none" : `song_${n}`));
      
      return {
        id: s.id,
        scentName: s.name,
        selectedTrackIds: selectedTrackIds.length > 0 ? selectedTrackIds : ["none"]
      };
    }));
  }, [currentUser?.musicTracks, scentSlots, isDirty, isModalOpen, isSaving]);

  const openSelectModal = (slotId: number) => {
    setActiveSlotId(slotId);
    setSearchTerm("");
    setIsModalOpen(true);
  };

  // 모달 내 다중 곡 선택 토글 처리
  const toggleTrackForActiveSlot = (trackId: string, e: React.MouseEvent) => {
    e.stopPropagation();
    if (activeSlotId === null) return;
    setIsDirty(true);

    setSlots(prev => prev.map(s => {
      if (s.id !== activeSlotId) return s;

      let currentList = [...s.selectedTrackIds];
      
      if (trackId === "none") {
        return { ...s, selectedTrackIds: ["none"] };
      }

      // "none"이 들어있으면 제거
      currentList = currentList.filter(id => id !== "none");

      if (currentList.includes(trackId)) {
        currentList = currentList.filter(id => id !== trackId);
      } else {
        currentList.push(trackId);
      }

      if (currentList.length === 0) {
        currentList = ["none"];
      }

      return { ...s, selectedTrackIds: currentList };
    }));
  };

  // 곡 순서 위로 이동
  const moveTrackUp = (slotId: number, index: number) => {
    if (index <= 0) return;
    setIsDirty(true);
    setSlots(prev => prev.map(s => {
      if (s.id !== slotId) return s;
      const newList = [...s.selectedTrackIds];
      const temp = newList[index];
      newList[index] = newList[index - 1];
      newList[index - 1] = temp;
      return { ...s, selectedTrackIds: newList };
    }));
  };

  // 곡 순서 아래로 이동
  const moveTrackDown = (slotId: number, index: number) => {
    setIsDirty(true);
    setSlots(prev => prev.map(s => {
      if (s.id !== slotId) return s;
      if (index >= s.selectedTrackIds.length - 1) return s;
      const newList = [...s.selectedTrackIds];
      const temp = newList[index];
      newList[index] = newList[index + 1];
      newList[index + 1] = temp;
      return { ...s, selectedTrackIds: newList };
    }));
  };

  // 곡 삭제
  const removeTrackFromSlot = (slotId: number, trackId: string) => {
    setIsDirty(true);
    setSlots(prev => prev.map(s => {
      if (s.id !== slotId) return s;
      const newList = s.selectedTrackIds.filter(id => id !== trackId);
      return {
        ...s,
        selectedTrackIds: newList.length > 0 ? newList : ["none"]
      };
    }));
  };

  const handleSave = async () => {
    if (!currentUser) return;
    setIsSaving(true);
    try {
      // 각 슬롯별 선택된 곡 번호들을 쉼표(,)로 잇고, 4개 슬롯을 언더바(_)로 연결
      // 예: "1,2_6,7_11_16"
      const musicData = slots.map(s => {
        if (s.selectedTrackIds.includes("none") || s.selectedTrackIds.length === 0) return "0";
        return s.selectedTrackIds.map(id => id.replace("song_", "")).join(",");
      }).join("_");
      
      const result = await sendDeviceData("SAVE_MUSIC", 0, musicData);
      if (result.success) { 
        // 🚀 서버 저장 성공 시, 앱이 기억하는 로컬 사용자 정보도 즉시 갱신!
        await updateUser(currentUser.email, { musicTracks: musicData });
        setIsDirty(false);
        
        alert("음악 순서 및 재생 목록이 성공적으로 저장되었습니다.");
        navigate(-1); 
      }
    } catch (error) { 
      console.error(error); 
      alert("저장 중 오류가 발생했습니다.");
    } finally { 
      setIsSaving(false); 
    }
  };

  const filteredTracks = useMemo(() => {
    if (!searchTerm) return TRACKS;
    return TRACKS.filter(t => t.name.includes(searchTerm) || t.artist.includes(searchTerm));
  }, [searchTerm]);

  const activeSlot = slots.find(s => s.id === activeSlotId);

  return (
    <div key="music-screen" className="flex-1 flex flex-col bg-white dark:bg-gray-950 min-h-screen transition-colors duration-300 relative">
      <header className="px-6 pt-12 pb-4 flex items-center justify-between z-10 sticky top-0 bg-white/80 dark:bg-gray-950/80 backdrop-blur-md transition-colors duration-300">
        <div className="flex items-center">
          <button onClick={() => navigate(-1)} className="w-10 h-10 -ml-2 rounded-full flex items-center justify-center text-gray-900 dark:text-white hover:bg-gray-200 dark:hover:bg-gray-800 transition-colors">
            <ArrowLeft className="w-6 h-6" />
          </button>
          <h1 className="text-xl font-bold tracking-tight text-gray-900 dark:text-white ml-2">음악 설정</h1>
        </div>
        <button onClick={handleSave} disabled={isSaving} className="px-5 py-2.5 bg-blue-600 dark:bg-blue-500 text-white rounded-2xl text-sm font-bold hover:bg-blue-700 transition-all flex items-center gap-1.5 disabled:opacity-50 shadow-lg shadow-blue-500/20">
          {isSaving ? <RefreshCw className="w-4 h-4 animate-spin" /> : <Check className="w-4 h-4" />}
          저장하기
        </button>
      </header>

      <div className="flex-1 px-6 py-4 flex flex-col gap-6 pb-32 overflow-y-auto">
        <div className="mb-2">
          <p className="text-sm font-medium text-gray-500 dark:text-gray-400 leading-relaxed">
            각 향기 슬롯별로 연속 재생할 곡들을 선택하고, 화살표 버튼(▲/▼)으로 **원하는 재생 순서**로 마음대로 변경할 수 있습니다.
          </p>
        </div>

        <div className="flex flex-col gap-4">
          {slots.map((slot) => {
            const selectedTracks = slot.selectedTrackIds
              .map(id => TRACKS.find(t => t.id === id))
              .filter((t): t is typeof TRACKS[0] => Boolean(t));
            const isNone = slot.selectedTrackIds.includes("none") || selectedTracks.length === 0;

            return (
              <motion.div 
                key={slot.id} 
                initial={{ opacity: 0, y: 10 }}
                animate={{ opacity: 1, y: 0 }}
                transition={{ delay: slot.id * 0.1 }}
                className="bg-gray-50 dark:bg-gray-900/50 rounded-[2rem] p-6 border border-gray-100 dark:border-gray-800/50 flex flex-col gap-4"
              >
                <div className="flex items-center justify-between">
                  <div className="flex items-center gap-4">
                    <div className="w-12 h-12 rounded-2xl flex items-center justify-center font-black text-lg bg-gray-200 dark:bg-gray-800 text-gray-700 dark:text-gray-300">
                      {slot.id}
                    </div>
                    <div>
                      <h3 className="text-base font-bold text-gray-900 dark:text-white">{slot.scentName}</h3>
                      <p className="text-[10px] font-bold text-gray-400 uppercase tracking-tighter">
                        {isNone ? "선택된 음악 없음" : `선택된 음악 ${selectedTracks.length}곡 (순서 변경 가능)`}
                      </p>
                    </div>
                  </div>
                  <button 
                    onClick={() => openSelectModal(slot.id)} 
                    className="px-4 py-2 bg-white dark:bg-gray-800 text-gray-900 dark:text-white rounded-xl text-xs font-bold border border-gray-200 dark:border-gray-700 shadow-sm active:scale-95 transition-all"
                  >
                    곡 선택 / 추가
                  </button>
                </div>

                <div className="flex flex-col gap-2 bg-white dark:bg-gray-800 p-4 rounded-2xl shadow-sm border border-gray-100 dark:border-gray-800">
                  {isNone ? (
                    <div className="flex items-center gap-3">
                      <div className="w-8 h-8 rounded-full bg-gray-100 dark:bg-gray-700 text-gray-300 flex items-center justify-center">
                        <Music className="w-4 h-4" />
                      </div>
                      <p className="text-sm font-bold text-gray-400">재생 안 함 (무음)</p>
                    </div>
                  ) : (
                    <div className="flex flex-col gap-2">
                      {selectedTracks.map((t, idx) => (
                        <div key={t.id} className="flex items-center justify-between py-2 border-b border-gray-100 dark:border-gray-700/50 last:border-none">
                          <div className="flex items-center gap-2.5 min-w-0 flex-1 pr-2">
                            <span className="text-xs font-black text-blue-500 w-5 shrink-0">{idx + 1}.</span>
                            <div className="min-w-0">
                              <p className="text-sm font-bold text-gray-900 dark:text-white truncate">{t.name}</p>
                              <p className="text-[10px] text-gray-400 truncate">{t.artist}</p>
                            </div>
                          </div>

                          {/* 순서 변경 (위/아래) 및 삭제 버튼 */}
                          <div className="flex items-center gap-1 shrink-0">
                            <button
                              onClick={() => moveTrackUp(slot.id, idx)}
                              disabled={idx === 0}
                              title="위로 이동"
                              className="p-1.5 rounded-lg text-gray-600 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 disabled:opacity-20 transition-colors"
                            >
                              <ChevronUp className="w-4 h-4" />
                            </button>
                            <button
                              onClick={() => moveTrackDown(slot.id, idx)}
                              disabled={idx === selectedTracks.length - 1}
                              title="아래로 이동"
                              className="p-1.5 rounded-lg text-gray-600 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 disabled:opacity-20 transition-colors"
                            >
                              <ChevronDown className="w-4 h-4" />
                            </button>
                            <button
                              onClick={() => removeTrackFromSlot(slot.id, t.id)}
                              title="삭제"
                              className="p-1.5 rounded-lg text-rose-500 hover:bg-rose-50 dark:hover:bg-rose-950/30 transition-colors ml-1"
                            >
                              <Trash2 className="w-4 h-4" />
                            </button>
                          </div>
                        </div>
                      ))}
                    </div>
                  )}
                </div>
              </motion.div>
            );
          })}
        </div>
      </div>

      {/* 다중 곡 선택 모달 */}
      <AnimatePresence>
        {isModalOpen && activeSlotId !== null && (
          <>
            <motion.div initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} onClick={() => setIsModalOpen(false)} className="fixed inset-0 bg-black/60 backdrop-blur-sm z-40" />
            <motion.div initial={{ y: "100%" }} animate={{ y: 0 }} exit={{ y: "100%" }} transition={{ type: "spring", damping: 30, stiffness: 250 }} className="fixed inset-x-0 bottom-0 z-50 bg-white dark:bg-gray-900 rounded-t-[3rem] shadow-2xl flex flex-col h-[80vh]">
              <div className="flex-shrink-0 flex flex-col items-center pt-3 pb-4 px-8 border-b border-gray-100 dark:border-gray-800">
                <div className="w-12 h-1.5 bg-gray-200 dark:bg-gray-700 rounded-full mb-4" />
                <div className="w-full flex items-center justify-between mb-3">
                  <h3 className="text-lg font-extrabold text-gray-900 dark:text-white">
                    {activeSlot?.scentName} - 곡 선택 (터치 순서대로 재생)
                  </h3>
                  <button onClick={() => setIsModalOpen(false)} className="p-1 rounded-full text-gray-400 hover:bg-gray-100 dark:hover:bg-gray-800">
                    <X className="w-5 h-5" />
                  </button>
                </div>
                <div className="w-full relative">
                  <Search className="absolute left-4 top-1/2 -translate-y-1/2 h-5 w-5 text-gray-400" />
                  <input type="text" placeholder="곡 제목 또는 번호 검색..." value={searchTerm} onChange={(e) => setSearchTerm(e.target.value)} className="w-full bg-gray-100 dark:bg-gray-800 border-none rounded-2xl pl-12 pr-4 py-3.5 text-sm font-bold outline-none text-gray-900 dark:text-white" />
                </div>
              </div>

              <div className="flex-1 overflow-y-auto p-6 flex flex-col gap-2.5 pb-24">
                {filteredTracks.map(track => {
                   const selectedIndex = activeSlot?.selectedTrackIds.indexOf(track.id);
                   const isSelected = selectedIndex !== undefined && selectedIndex >= 0;
                   const orderNum = isSelected ? selectedIndex + 1 : null;

                   return (
                    <div 
                      key={track.id} 
                      className={`flex items-center p-4 rounded-2xl transition-all cursor-pointer border ${isSelected ? 'bg-blue-50 dark:bg-blue-900/30 border-blue-500 text-blue-900 dark:text-blue-200 shadow-sm' : 'bg-white dark:bg-gray-800 border-gray-100 dark:border-gray-800 hover:border-gray-200'}`} 
                      onClick={(e) => toggleTrackForActiveSlot(track.id, e)}
                    >
                      {/* 선택 체크박스 + 순서 뱃지 */}
                      <div className={`w-7 h-7 rounded-lg flex items-center justify-center mr-3 border transition-colors shrink-0 ${isSelected ? 'bg-blue-500 border-blue-500 text-white font-black text-xs' : 'border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-700'}`}>
                        {isSelected ? orderNum : null}
                      </div>

                      <div className="flex-1 min-w-0">
                        <p className={`text-sm font-bold truncate ${isSelected ? 'text-blue-900 dark:text-blue-100' : 'text-gray-900 dark:text-white'}`}>{track.name}</p>
                        <p className={`text-[10px] font-bold uppercase tracking-wider ${isSelected ? 'text-blue-700/80 dark:text-blue-300/80' : 'text-gray-500'}`}>{track.artist}</p>
                      </div>

                      {isSelected && (
                        <span className="text-xs font-black text-blue-600 dark:text-blue-400 bg-blue-100 dark:bg-blue-900/60 px-2.5 py-1 rounded-full shrink-0">
                          {orderNum}번째 재생
                        </span>
                      )}
                    </div>
                  );
                })}
              </div>

              {/* 하단 완료 액션 바 */}
              <div className="absolute bottom-0 inset-x-0 p-4 bg-white/90 dark:bg-gray-900/90 backdrop-blur-md border-t border-gray-100 dark:border-gray-800 z-10 flex justify-center">
                <button
                  onClick={() => setIsModalOpen(false)}
                  className="w-full py-3.5 bg-blue-600 dark:bg-blue-500 text-white rounded-2xl font-bold text-sm shadow-lg shadow-blue-500/20 active:scale-98 transition-all"
                >
                  선택 완료 및 닫기 (총 {activeSlot?.selectedTrackIds.filter(id => id !== "none").length || 0}곡)
                </button>
              </div>
            </motion.div>
          </>
        )}
      </AnimatePresence>
    </div>
  );
}

