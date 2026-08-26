import React, { useState } from "react";
import { useNavigate } from "react-router";
import { motion, AnimatePresence } from "motion/react";
import { 
  ArrowLeft, 
  Cpu, 
  Wifi, 
  Battery, 
  Wind, 
  RefreshCw, 
  Power, 
  Trash2, 
  Smartphone,
  CheckCircle2,
  AlertTriangle,
  Edit2,
  Check,
  X,
  RotateCcw
} from "lucide-react";
import { useDevice } from "../store/DeviceContext";
import { useAuth } from "../store/AuthContext";

export function DeviceManagementScreen() {
  const navigate = useNavigate();
  const { currentUser, updateUser } = useAuth();
  const { sendDeviceData } = useDevice();
  
  const [isEditingId, setIsEditingId] = useState(false);
  const [newDeviceId, setNewDeviceId] = useState(currentUser?.deviceId || "SS");
  const [isResetting, setIsResetting] = useState(false);

  // 커스텀 모달 상태
  const [confirmDialog, setConfirmDialog] = useState<{isOpen: boolean, title: string, message: string, onConfirm: () => void, onCancel: () => void, isDanger?: boolean} | null>(null);
  const [alertDialog, setAlertDialog] = useState<{isOpen: boolean, title: string, message: string, isError?: boolean, onConfirm?: () => void} | null>(null);



  const handleWifiReset = () => {
    setConfirmDialog({
      isOpen: true,
      title: "Wi-Fi 초기화",
      message: "기기의 Wi-Fi 설정을 초기화하고 재부팅하시겠습니까?\n이후 기기를 다시 네트워크에 연결해야 합니다.",
      onConfirm: async () => {
        setConfirmDialog(null);
        setIsResetting(true);
        try {
          const res = await sendDeviceData("WIFI_RESET", 0);
          if (res.success) {
            setAlertDialog({
              isOpen: true,
              title: "명령 전송 완료",
              message: "기기에 Wi-Fi 초기화 명령을 보냈습니다.\n기기가 재부팅되면 설정 모드로 진입합니다.",
              isError: false
            });
          } else {
            setAlertDialog({
              isOpen: true,
              title: "명령 전송 실패",
              message: "명령 전송 실패: " + res.message,
              isError: true
            });
          }
        } catch (err) {
          console.error("WiFi Reset Error:", err);
          setAlertDialog({
            isOpen: true,
            title: "오류 발생",
            message: "서버와 통신 중 오류가 발생했습니다.",
            isError: true
          });
        } finally {
          setIsResetting(false);
        }
      },
      onCancel: () => setConfirmDialog(null)
    });
  };

  const handleFactoryReset = () => {
    setConfirmDialog({
      isOpen: true,
      title: "기기 공장 초기화",
      message: "다음 데이터가 완전히 삭제됩니다:\n\n1. AI 추천 학습 데이터 (좋아요/싫어요)\n2. 향기 분사 히스토리 및 통계\n3. 저장된 음악 설정 및 스케줄\n4. 작성된 향기 다이어리 기록\n\n이 작업은 되돌릴 수 없습니다. 진행하시겠습니까?",
      isDanger: true,
      onConfirm: async () => {
        setConfirmDialog(null);
        setIsResetting(true);
        try {
          const res = await sendDeviceData("RESET_DATABASE", 0);
          if (res.success) {
            setAlertDialog({
              isOpen: true,
              title: "초기화 완료",
              message: "공장 초기화가 완료되었습니다.\n앱의 모든 사용자 데이터가 삭제되었습니다.",
              isError: false,
              onConfirm: () => navigate("/modes")
            });
          } else {
            setAlertDialog({
              isOpen: true,
              title: "초기화 실패",
              message: "초기화 실패: " + res.message,
              isError: true
            });
          }
        } catch (err) {
          console.error("Factory Reset Error:", err);
          setAlertDialog({
            isOpen: true,
            title: "오류 발생",
            message: "서버와 통신 중 오류가 발생했습니다.",
            isError: true
          });
        } finally {
          setIsResetting(false);
        }
      },
      onCancel: () => setConfirmDialog(null)
    });
  };

  const saveDeviceId = async () => {
    if (!currentUser) return;
    const res = await updateUser(currentUser.email, { deviceId: newDeviceId });
    if (res.success) {
      setIsEditingId(false);
      setAlertDialog({
        isOpen: true,
        title: "업데이트 완료",
        message: "기기 번호가 성공적으로 업데이트되었습니다.",
        isError: false
      });
    } else {
      setAlertDialog({
        isOpen: true,
        title: "업데이트 실패",
        message: "업데이트 실패: " + res.message,
        isError: true
      });
    }
  };

  return (
    <div key="device-screen" className="flex-1 flex flex-col bg-gray-50 dark:bg-gray-950 overflow-y-auto relative min-h-screen transition-colors duration-300">
      <header className="px-6 pt-12 pb-4 flex items-center justify-between z-10 sticky top-0 bg-gray-50/90 dark:bg-gray-950/90 backdrop-blur-md border-b border-gray-200/50 dark:border-gray-800/50 transition-colors duration-300">
        <div className="flex items-center">
          <button 
            onClick={() => navigate(-1)}
            className="w-10 h-10 -ml-2 rounded-full flex items-center justify-center text-gray-900 dark:text-white hover:bg-gray-200 dark:hover:bg-gray-800 transition-colors"
          >
            <ArrowLeft className="w-6 h-6" />
          </button>
          <h1 className="text-xl font-bold tracking-tight text-gray-900 dark:text-white ml-2">기기 관리</h1>
        </div>
      </header>

      <div className="flex-1 px-6 py-6 flex flex-col gap-6 pb-12">
        
        {/* Device Status Hero */}
        <motion.div 
          initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }}
          className="bg-white dark:bg-gray-900 rounded-3xl p-6 shadow-sm border border-gray-200 dark:border-gray-800 flex flex-col items-center text-center transition-colors duration-300"
        >
          <div className="relative mb-4">
            <div className="w-24 h-24 bg-gray-50 dark:bg-gray-800 rounded-full flex items-center justify-center border border-gray-200 dark:border-gray-700 shadow-sm z-10 relative">
              <Smartphone className="w-10 h-10 text-gray-600 dark:text-gray-300" />
            </div>
          </div>
          
          <h2 className="text-xl font-bold text-gray-900 dark:text-white">Smart Diffuser v3</h2>
          
          <div className="mt-2 flex flex-col items-center">
            {isEditingId ? (
              <div className="flex items-center gap-2 mt-1">
                <input 
                  type="text"
                  value={newDeviceId}
                  onChange={(e) => setNewDeviceId(e.target.value)}
                  className="bg-gray-100 dark:bg-gray-800 border-none rounded-lg px-3 py-1.5 text-sm font-bold text-gray-900 dark:text-white focus:ring-2 focus:ring-blue-500 outline-none w-40"
                  placeholder="기기 번호 입력"
                />
                <button onClick={saveDeviceId} className="p-2 bg-blue-500 text-white rounded-lg"><Check className="w-4 h-4" /></button>
                <button onClick={() => { setIsEditingId(false); setNewDeviceId(currentUser?.deviceId || ""); }} className="p-2 bg-gray-200 dark:bg-gray-700 text-gray-600 dark:text-gray-400 rounded-lg"><X className="w-4 h-4" /></button>
              </div>
            ) : (
              <div className="flex items-center gap-2 text-sm font-medium text-gray-500 dark:text-gray-400">
                <span>기기 번호: <span className="font-bold text-gray-700 dark:text-gray-300">{currentUser?.deviceId || "미등록"}</span></span>
                <button onClick={() => setIsEditingId(true)} className="p-1 hover:bg-gray-100 dark:hover:bg-gray-800 rounded transition-colors">
                  <Edit2 className="w-3 h-3" />
                </button>
              </div>
            )}
          </div>
          
        </motion.div>

        {/* Maintenance Actions */}
        <div className="flex flex-col gap-3">
          <h3 className="text-sm font-bold text-gray-500 dark:text-gray-400 px-1">기기 유지보수</h3>
          
          <motion.div initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} transition={{ delay: 0.1 }} className="bg-white dark:bg-gray-900 rounded-3xl border border-gray-200 dark:border-gray-800 overflow-hidden shadow-sm">
            <div className="p-5 flex flex-col gap-4">
              <div className="flex items-start justify-between">
                <div className="flex items-center gap-3">
                  <div className="w-10 h-10 bg-blue-50 dark:bg-blue-900/20 text-blue-600 dark:text-blue-400 rounded-xl flex items-center justify-center">
                    <Wifi className="w-5 h-5" />
                  </div>
                  <div>
                    <h4 className="text-base font-bold text-gray-900 dark:text-white">Wi-Fi 초기화</h4>
                    <p className="text-xs font-medium text-gray-500 dark:text-gray-400 mt-0.5">기기의 네트워크 설정을 삭제합니다</p>
                  </div>
                </div>
              </div>

              <button 
                onClick={handleWifiReset}
                disabled={isResetting}
                className="w-full py-3 bg-gray-900 dark:bg-gray-100 hover:bg-gray-800 dark:hover:bg-gray-200 text-white dark:text-gray-900 font-bold text-sm rounded-xl transition-colors flex items-center justify-center gap-2"
              >
                {isResetting ? <RefreshCw className="w-4 h-4 animate-spin" /> : <RotateCcw className="w-4 h-4" />}
                {isResetting ? "초기화 명령 전송 중..." : "Wi-Fi 초기화 및 재시작"}
              </button>
              
              <p className="text-[10px] text-gray-400 mt-1 text-center px-4 break-keep leading-relaxed">
                Wi-Fi 정보가 삭제되면 기기가 재부팅되며 다시 설정 모드로 진입합니다.
              </p>
            </div>
          </motion.div>
        </div>

        {/* System Settings */}
        <div className="flex flex-col gap-3">
          <h3 className="text-sm font-bold text-gray-500 dark:text-gray-400 px-1 mt-2">시스템 설정</h3>
          
          <motion.div initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} transition={{ delay: 0.2 }} className="bg-white dark:bg-gray-900 rounded-3xl border border-gray-200 dark:border-gray-800 overflow-hidden shadow-sm">
            <button 
              onClick={handleFactoryReset}
              className="w-full flex items-center justify-between p-4 hover:bg-red-50 dark:hover:bg-red-900/10 transition-colors text-left"
            >
              <div className="flex items-center gap-3">
                <div className="w-10 h-10 bg-red-50 dark:bg-red-900/20 rounded-xl flex items-center justify-center text-red-500">
                  <Trash2 className="w-5 h-5" />
                </div>
                <div className="flex flex-col">
                  <span className="font-bold text-red-500">기기 공장 초기화</span>
                  <span className="text-[11px] font-medium text-red-400 dark:text-red-500/70">모든 설정과 향기 데이터가 삭제됩니다</span>
                </div>
              </div>
            </button>
          </motion.div>
        </div>
      </div>

      {/* 커스텀 확인 다이얼로그 (Confirm) */}
      <AnimatePresence>
        {confirmDialog && (
          <div className="fixed inset-0 z-50 flex items-center justify-center px-6">
            <motion.div 
              initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}
              onClick={confirmDialog.onCancel}
              className="absolute inset-0 bg-gray-900/60 dark:bg-gray-950/80 backdrop-blur-sm"
            />
            <motion.div 
              initial={{ opacity: 0, scale: 0.95, y: 10 }} 
              animate={{ opacity: 1, scale: 1, y: 0 }} 
              exit={{ opacity: 0, scale: 0.95, y: 10 }}
              className="relative w-full max-w-sm bg-white dark:bg-gray-900 rounded-3xl p-6 shadow-2xl border border-gray-200 dark:border-gray-800"
            >
              <div className="flex flex-col items-center text-center mb-6">
                <div className={`w-12 h-12 rounded-full flex items-center justify-center mb-4 ${confirmDialog.isDanger ? 'bg-red-50 dark:bg-red-900/20 text-red-600 dark:text-red-400' : 'bg-blue-50 dark:bg-blue-900/20 text-blue-600 dark:text-blue-400'}`}>
                  {confirmDialog.isDanger ? <AlertTriangle className="w-6 h-6" /> : <RefreshCw className="w-6 h-6" />}
                </div>
                <h2 className="text-lg font-bold text-gray-900 dark:text-white mb-2">{confirmDialog.title}</h2>
                <div className="text-sm font-medium text-gray-500 dark:text-gray-400 whitespace-pre-line leading-relaxed text-left inline-block w-full">
                  {confirmDialog.message.split('\n').map((line, i) => (
                    <React.Fragment key={i}>
                      {line}
                      {i < confirmDialog.message.split('\n').length - 1 && <br />}
                    </React.Fragment>
                  ))}
                </div>
              </div>
              <div className="flex gap-3">
                <button 
                  onClick={confirmDialog.onCancel}
                  className="flex-1 py-3.5 bg-gray-100 dark:bg-gray-800 text-gray-700 dark:text-gray-300 font-bold rounded-xl hover:bg-gray-200 dark:hover:bg-gray-700 transition-colors"
                >
                  취소
                </button>
                <button 
                  onClick={confirmDialog.onConfirm}
                  className={`flex-1 py-3.5 font-bold rounded-xl shadow-md transition-colors text-white ${confirmDialog.isDanger ? 'bg-red-600 hover:bg-red-700' : 'bg-blue-600 hover:bg-blue-700'}`}
                >
                  진행하기
                </button>
              </div>
            </motion.div>
          </div>
        )}
      </AnimatePresence>

      {/* 커스텀 알림 다이얼로그 (Alert) */}
      <AnimatePresence>
        {alertDialog && (
          <div className="fixed inset-0 z-[60] flex items-center justify-center px-6">
            <motion.div 
              initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}
              onClick={() => {
                if (alertDialog.onConfirm) alertDialog.onConfirm();
                setAlertDialog(null);
              }}
              className="absolute inset-0 bg-gray-900/40 dark:bg-gray-950/60 backdrop-blur-sm"
            />
            <motion.div 
              initial={{ opacity: 0, scale: 0.9, y: 20 }} 
              animate={{ opacity: 1, scale: 1, y: 0 }} 
              exit={{ opacity: 0, scale: 0.9, y: 20 }}
              className="relative w-full max-w-sm bg-white dark:bg-gray-900 rounded-3xl p-6 shadow-2xl border border-gray-200 dark:border-gray-800 flex flex-col items-center text-center"
            >
              <div className={`w-14 h-14 rounded-full flex items-center justify-center mb-4 ${alertDialog.isError ? 'bg-red-50 dark:bg-red-900/20 text-red-500' : 'bg-green-50 dark:bg-green-900/20 text-green-500'}`}>
                {alertDialog.isError ? <AlertTriangle className="w-7 h-7" /> : <Check className="w-7 h-7" />}
              </div>
              <h2 className="text-xl font-bold text-gray-900 dark:text-white mb-2">{alertDialog.title}</h2>
              <p className="text-sm font-medium text-gray-500 dark:text-gray-400 whitespace-pre-line leading-relaxed mb-6">
                {alertDialog.message}
              </p>
              <button 
                onClick={() => {
                  if (alertDialog.onConfirm) alertDialog.onConfirm();
                  setAlertDialog(null);
                }}
                className="w-full py-3.5 bg-gray-900 dark:bg-white text-white dark:text-gray-900 font-bold rounded-xl shadow-md active:scale-95 transition-all"
              >
                확인
              </button>
            </motion.div>
          </div>
        )}
      </AnimatePresence>

    </div>
  );
}

