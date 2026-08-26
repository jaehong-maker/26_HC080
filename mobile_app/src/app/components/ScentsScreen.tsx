import React, { useState, useEffect } from "react";
import { Link } from "react-router";
import { BottomNav } from "./BottomNav";
import { motion, AnimatePresence } from "motion/react";
import { Home, Droplets, SlidersHorizontal, RefreshCw, X, CheckCircle2, Settings, BookHeart, Loader2, AlertTriangle, Check } from "lucide-react";
import { useDevice } from "../store/DeviceContext";
import { useAuth } from "../store/AuthContext";
import { apiSendData } from "../utils/api";

export function ScentsScreen() {
  const { currentUser } = useAuth();
  const { scentSlots, updateScentSlot, refreshDeviceState } = useDevice();
  const [replacingId, setReplacingId] = useState<number | null>(null);
  const [loading, setLoading] = useState(false);
  
  const availableScents = [
    { name: "시트러스", color: "bg-orange-500" },
    { name: "센달우드", color: "bg-amber-700" },
    { name: "패츌리", color: "bg-green-700" },
    { name: "페퍼민트", color: "bg-teal-500" },
    { name: "라벤더", color: "bg-violet-500" },
    { name: "바닐라", color: "bg-orange-300" },
    { name: "무향(물)", color: "bg-cyan-500" },
  ];

  useEffect(() => {
    setLoading(true);
    refreshDeviceState().finally(() => setLoading(false));
  }, []);


  const handleReplace = async (newName: string, newColor: string) => {
    if (replacingId === null) return;
    updateScentSlot(replacingId, { name: newName, color: newColor, remaining: 100 });
    
    const newSlots = scentSlots.map(s => s.id === replacingId ? { ...s, name: newName } : s);
    const kindMap: Record<string, number> = {
      "시트러스": 1, "시트러스 가든": 1,
      "센달우드": 2, "패츌리": 3, "페퍼민트": 4,
      "라벤더": 5, "라벤더 포레스트": 5,
      "바닐라": 6, "화이트 머스크": 6,
      "무향(물)": 7, "아쿠아": 7, "오션 브리즈": 7
    };
    
    const mappingDict: Record<string, number> = {};
    newSlots.forEach(slot => { mappingDict[slot.id.toString()] = kindMap[slot.name] || slot.id; });
    
    if (currentUser) {
      setLoading(true);
      await apiSendData({
        email: currentUser.email,
        action: "SET_MAPPING",
        mapping: mappingDict as any,
        deviceId: currentUser.deviceId || "SS"
      });
      setLoading(false);
    }
    
    setReplacingId(null);
  };

  return (
    <div key="scents-screen" className="flex-1 flex flex-col bg-gray-50 dark:bg-gray-950 overflow-y-auto relative pb-32 min-h-screen transition-colors duration-300">
      <header className="px-6 pt-12 pb-4 flex justify-between items-center z-10 sticky top-0 bg-gray-50/90 dark:bg-gray-950/90 backdrop-blur-md transition-colors duration-300">
        <div>
          <h1 className="text-2xl font-bold tracking-tight text-gray-900 dark:text-white">향기 관리</h1>
          <p className="text-sm font-medium text-gray-500 dark:text-gray-400 mt-1">디퓨저 카트리지 상태 및 교체</p>
        </div>
      </header>

      <div className="px-6 py-4 flex flex-col gap-4">
        {scentSlots.map((cart) => (
          <motion.div 
            key={cart.id} initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }}
            className="bg-white dark:bg-gray-900 rounded-3xl p-5 shadow-sm border border-gray-200 dark:border-gray-800 flex flex-col gap-4 transition-colors duration-300"
          >
            <div className="flex justify-between items-start">
              <div className="flex items-center gap-3">
                <div className={`w-10 h-10 rounded-xl flex items-center justify-center text-white font-bold ${cart.color} dark:bg-opacity-90`}>
                  {cart.id}
                </div>
                <div>
                  <h3 className="text-lg font-bold text-gray-900 dark:text-white">{cart.name}</h3>
                  <p className="text-sm font-semibold text-gray-400 dark:text-gray-500">슬롯 {cart.id}</p>
                </div>
              </div>
              <button 
                onClick={() => setReplacingId(cart.id)}
                className="w-10 h-10 bg-gray-50 dark:bg-gray-800 rounded-full flex items-center justify-center text-gray-600 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
              >
                <RefreshCw className="w-5 h-5" />
              </button>
            </div>

            <div>
              <div className="flex justify-between items-center text-sm mb-2.5">
                <div className="flex items-center gap-1.5">
                  <span className="font-bold text-gray-500 dark:text-gray-400 uppercase tracking-tight text-[10px]">Remaining</span>
                  {cart.remaining < 20 && (
                    <span className="bg-red-100 dark:bg-red-900/30 text-red-600 dark:text-red-400 text-[9px] font-black px-1.5 py-0.5 rounded-md animate-pulse">LOW</span>
                  )}
                </div>
                <div className="flex items-baseline gap-2">
                  <span className="text-[10px] font-bold text-gray-400 dark:text-gray-500">
                    {cart.weightGrams?.toFixed(1) || "0.0"}g
                  </span>
                  <span className={`text-base font-black tracking-tighter ${cart.remaining < 20 ? 'text-red-600 dark:text-red-400' : 'text-gray-900 dark:text-white'}`}>
                    {cart.remaining}<span className="text-[10px] ml-0.5 opacity-60">%</span>
                  </span>
                </div>
              </div>
              <div className="h-3 w-full bg-gray-100 dark:bg-gray-800 rounded-full overflow-hidden p-0.5 border border-gray-100 dark:border-gray-700 shadow-inner">
                <motion.div 
                  initial={{ width: 0 }} 
                  animate={{ width: `${cart.remaining}%` }} 
                  transition={{ type: "spring", stiffness: 100, damping: 20 }}
                  className={`h-full rounded-full ${cart.remaining < 20 ? 'bg-gradient-to-r from-red-500 to-rose-400' : cart.color} shadow-sm`} 
                />
              </div>
            </div>
          </motion.div>
        ))}
      </div>

      <AnimatePresence>
        {replacingId !== null && (
          <div className="fixed inset-0 z-50 flex items-end justify-center">
            <motion.div 
              initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}
              onClick={() => setReplacingId(null)}
              className="absolute inset-0 bg-gray-900/40 dark:bg-gray-950/60 backdrop-blur-sm transition-colors duration-300"
            />
            <motion.div 
              initial={{ y: "100%" }} animate={{ y: 0 }} exit={{ y: "100%" }}
              transition={{ type: "spring", damping: 25, stiffness: 200 }}
              className="relative w-full bg-white dark:bg-gray-900 rounded-t-[2.5rem] p-6 pb-12 shadow-2xl transition-colors duration-300 border-t border-gray-200 dark:border-gray-800"
            >
              <div className="flex justify-between items-center mb-6">
                <div>
                  <h2 className="text-xl font-bold text-gray-900 dark:text-white">새 향기 등록</h2>
                  <p className="text-sm text-gray-500 dark:text-gray-400 font-medium">슬롯 {replacingId}에 장착할 카트리지를 선택하세요</p>
                </div>
                <button onClick={() => setReplacingId(null)} className="w-8 h-8 bg-gray-100 dark:bg-gray-800 rounded-full flex items-center justify-center text-gray-600 dark:text-gray-400 hover:bg-gray-200 dark:hover:bg-gray-700 transition-colors">
                  <X className="w-5 h-5" />
                </button>
              </div>

              <div className="grid grid-cols-2 gap-3 max-h-[300px] overflow-y-auto pr-2">
                {availableScents.map((scent) => (
                  <button
                    key={scent.name}
                    onClick={() => handleReplace(scent.name, scent.color)}
                    className="flex items-center gap-3 p-4 border border-gray-200 dark:border-gray-800 shadow-sm rounded-2xl hover:bg-gray-50 dark:hover:bg-gray-800 hover:shadow-md transition-all"
                  >
                    <div className={`w-8 h-8 rounded-full ${scent.color} dark:bg-opacity-90 flex items-center justify-center`}>
                      <CheckCircle2 className="w-4 h-4 text-white opacity-0" />
                    </div>
                    <span className="font-bold text-gray-700 dark:text-gray-200">{scent.name}</span>
                  </button>
                ))}
              </div>
            </motion.div>
          </div>
        )}
      </AnimatePresence>


      <BottomNav />
    </div>
  );
}
