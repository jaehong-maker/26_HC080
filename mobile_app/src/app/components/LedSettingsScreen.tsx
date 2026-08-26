import React, { useState, useEffect, useRef } from "react";
import { motion, AnimatePresence } from "motion/react";
import { Lightbulb, Power, SlidersHorizontal, RefreshCw, Sparkles, Sun, SunMedium, SunDim, Minus, Plus } from "lucide-react";
import { BottomNav } from "./BottomNav";
import { useDevice } from "../store/DeviceContext";
import { useAuth } from "../store/AuthContext";

function hexToRgb(hex: string) {
  const result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
  if (!result) return { r: 255, g: 255, b: 255 };
  
  // LED의 물빠진 색을 방지하고 진하게(vivid) 출력하기 위해 간단한 감마 보정(Gamma 2.0) 적용
  const rawR = parseInt(result[1], 16);
  const rawG = parseInt(result[2], 16);
  const rawB = parseInt(result[3], 16);
  
  return {
    r: Math.round(Math.pow(rawR / 255, 2.0) * 255),
    g: Math.round(Math.pow(rawG / 255, 2.0) * 255),
    b: Math.round(Math.pow(rawB / 255, 2.0) * 255)
  };
}

// HSL to Hex 변환 유틸리티 함수
function hslToHex(h: number, s: number, l: number) {
  h = Math.round(h);
  s = Math.round(s);
  l = Math.round(l);
  l /= 100;
  const a = s * Math.min(l, 1 - l) / 100;
  const f = (n: number) => {
    const k = (n + h / 30) % 12;
    const color = l - a * Math.max(Math.min(k - 3, 9 - k, 1), -1);
    return Math.round(255 * color).toString(16).padStart(2, '0');
  };
  return `#${f(0)}${f(8)}${f(4)}`;
}

interface ColorWheelProps {
  color: string;
  thumbPos: { x: number, y: number };
  setThumbPos: (pos: { x: number, y: number }) => void;
  onChange: (hex: string) => void;
  disabled: boolean;
  onInteractionStart: () => void;
}

function ColorWheel({ color, thumbPos, setThumbPos, onChange, disabled, onInteractionStart }: ColorWheelProps) {
  const wheelRef = useRef<HTMLDivElement>(null);
  const [isDragging, setIsDragging] = useState(false);

  const updateColor = (e: React.PointerEvent | PointerEvent) => {
    if (!wheelRef.current || disabled) return;
    const rect = wheelRef.current.getBoundingClientRect();
    
    let x = e.clientX - rect.left;
    let y = e.clientY - rect.top;
    const cx = rect.width / 2;
    const cy = rect.height / 2;

    const dx = x - cx;
    const dy = y - cy;
    let distance = Math.sqrt(dx * dx + dy * dy);
    
    if (distance > cx) {
      distance = cx;
    }
    
    let angle = Math.atan2(dy, dx) * (180 / Math.PI) + 90;
    if (angle < 0) angle += 360;

    const distNormalized = distance / cx;
    // 기존 100~50 범위보다 훨씬 진한 색(원색)을 내기 위해 Lightness 범위를 100~40으로 확장
    const lightness = 100 - (distNormalized * 60); 
    const hue = angle;
    
    const constrainedX = cx + Math.sin(angle * Math.PI / 180) * distance;
    const constrainedY = cy - Math.cos(angle * Math.PI / 180) * distance;

    setThumbPos({
      x: (constrainedX / rect.width) * 100,
      y: (constrainedY / rect.height) * 100,
    });

    onChange(hslToHex(hue, 100, lightness));
  };

  useEffect(() => {
    const handlePointerMove = (e: PointerEvent) => {
      if (isDragging) updateColor(e);
    };
    const handlePointerUp = () => setIsDragging(false);

    if (isDragging) {
      window.addEventListener('pointermove', handlePointerMove);
      window.addEventListener('pointerup', handlePointerUp);
    }
    return () => {
      window.removeEventListener('pointermove', handlePointerMove);
      window.removeEventListener('pointerup', handlePointerUp);
    };
  }, [isDragging]);

  return (
    <div 
      ref={wheelRef}
      className={`relative w-full aspect-square max-w-[260px] mx-auto rounded-full touch-none select-none shadow-inner transition-opacity duration-300 ${disabled ? 'opacity-30 cursor-not-allowed' : 'cursor-crosshair'}`}
      style={{
        background: 'conic-gradient(red, yellow, lime, cyan, blue, magenta, red)'
      }}
      onPointerDown={(e) => {
        if (!disabled) {
          onInteractionStart();
          setIsDragging(true);
          (e.target as HTMLElement).setPointerCapture(e.pointerId);
          updateColor(e);
        }
      }}
    >
      <div 
        className="absolute inset-0 rounded-full pointer-events-none"
        style={{
          background: 'radial-gradient(circle at center, white 0%, transparent 100%)'
        }}
      />
      <div 
        className="absolute w-8 h-8 -ml-4 -mt-4 rounded-full border-[3px] border-white shadow-[0_2px_8px_rgba(0,0,0,0.4)] pointer-events-none z-10"
        style={{
          left: `${thumbPos.x}%`,
          top: `${thumbPos.y}%`,
          backgroundColor: color,
          transform: isDragging ? 'scale(1.2)' : 'scale(1)',
          transition: isDragging ? 'none' : 'all 0.3s ease-out'
        }}
      />
    </div>
  );
}

function hexToThumbPos(hex: string) {
  const result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
  if (!result) return { x: 75, y: 25 };
  const r = parseInt(result[1], 16) / 255;
  const g = parseInt(result[2], 16) / 255;
  const b = parseInt(result[3], 16) / 255;

  const max = Math.max(r, g, b), min = Math.min(r, g, b);
  let h = 0, l = (max + min) / 2;
  const d = max - min;

  if (d !== 0) {
    switch (max) {
      case r: h = (g - b) / d + (g < b ? 6 : 0); break;
      case g: h = (b - r) / d + 2; break;
      case b: h = (r - g) / d + 4; break;
    }
    h /= 6;
  }

  const hueDeg = h * 360;
  const lightness = l * 100;
  const distNormalized = Math.min(1, Math.max(0, (100 - lightness) / 60));
  const radius = 50 * distNormalized;
  
  const angleRad = (hueDeg - 90) * (Math.PI / 180);
  const x = 50 + radius * Math.cos(angleRad);
  const y = 50 + radius * Math.sin(angleRad);

  return { x: Math.min(95, Math.max(5, x)), y: Math.min(95, Math.max(5, y)) };
}

export function LedSettingsScreen() {
  const { currentUser } = useAuth();
  const { isLedOn, ledColor, ledBrightness, sendDeviceData } = useDevice();
  
  const [isOn, setIsOn] = useState(false);
  const [color, setColor] = useState("#FBBF24");
  const [brightness, setBrightness] = useState(80);
  const [effect, setEffect] = useState(0); // 0: Solid, 1: Breathe, 2: Rainbow, 3: Sync
  const [thumbPos, setThumbPos] = useState({ x: 75, y: 25 });
  
  const [isSyncing, setIsSyncing] = useState(false);
  const [isDraggingSlider, setIsDraggingSlider] = useState(false);
  const isInteractingRef = useRef(false);
  const lastInteractionTimeRef = useRef(0);

  // 서버 상태가 변경되면 로컬 UI에 반영 (사용자가 조작 중이 아닐 때만)
  useEffect(() => {
    if (Date.now() - lastInteractionTimeRef.current > 5000) {
      if (isLedOn !== isOn) setIsOn(isLedOn);
      if (ledColor && ledColor !== color) {
        setColor(ledColor);
        setThumbPos(hexToThumbPos(ledColor));
      }
      if (ledBrightness !== undefined && ledBrightness !== brightness) setBrightness(ledBrightness);
    }
  }, [isLedOn, ledColor, ledBrightness]);

  const syncTimerRef = useRef<NodeJS.Timeout | null>(null);

  const syncWithHardware = (r: number, g: number, b: number, br: number, powerOn: boolean, eff: number) => {
    if (!currentUser) return;
    
    if (syncTimerRef.current) clearTimeout(syncTimerRef.current);
    
    syncTimerRef.current = setTimeout(async () => {
      // 사용자가 조작했을 때만 서버로 전송
      if (isInteractingRef.current) {
        setIsSyncing(true);
        try {
          const targetBr = powerOn ? Math.round((br / 100) * 255) : 0;
          await sendDeviceData("SET_LED", 0, undefined, undefined, { r, g, b, br: targetBr, effect: eff } as any);
        } catch (err) {
          console.error("LED Sync Error:", err);
        } finally {
          setIsSyncing(false);
          isInteractingRef.current = false;
        }
      }
    }, 300);
  };

  const markInteraction = () => {
    isInteractingRef.current = true;
    lastInteractionTimeRef.current = Date.now();
  };

  useEffect(() => {
    const rgb = hexToRgb(color);
    syncWithHardware(rgb.r, rgb.g, rgb.b, brightness, isOn, effect);
  }, [color, brightness, isOn, effect]);

  return (
    <div key="led-screen" className="flex-1 flex flex-col bg-white dark:bg-gray-950 overflow-x-hidden relative pb-32 min-h-screen transition-colors duration-500">
      <div className="absolute top-0 left-0 right-0 h-[45vh] pointer-events-none overflow-hidden">
        <motion.div 
          animate={{ 
            background: isOn 
              ? `radial-gradient(circle at 50% 40%, ${color}33 0%, transparent 70%)` 
              : `radial-gradient(circle at 50% 40%, transparent 0%, transparent 70%)`
          }}
          className="absolute inset-0 transition-all duration-1000"
        />
        <div className="absolute bottom-0 left-0 right-0 h-32 bg-gradient-to-t from-white dark:from-gray-950 to-transparent" />
      </div>

      <header className="px-6 pt-12 pb-2 flex justify-between items-center z-20 relative">
        <div>
          <h1 className="text-2xl font-bold tracking-tight text-gray-900 dark:text-white uppercase tracking-tighter">LED Control</h1>
        </div>
      </header>

      <div className="px-6 py-4 flex flex-col gap-6 z-10 relative">
        <div className="flex flex-col items-center justify-center py-10 relative min-h-[280px]">
          <div className="absolute inset-0 flex items-center justify-center pointer-events-none">
            <AnimatePresence>
              {isOn && (
                <motion.div
                  initial={{ opacity: 0, scale: 0.9 }}
                  animate={{ opacity: 1, scale: 1 }}
                  exit={{ opacity: 0, scale: 0.9 }}
                  className="absolute w-[320px] h-[320px] rounded-full"
                  style={{ 
                    background: `radial-gradient(circle, ${color}4d 0%, transparent 75%)`,
                    filter: 'blur(10px)'
                  }}
                />
              )}
            </AnimatePresence>
          </div>

          <motion.div 
            whileHover={{ scale: 1.05 }}
            whileTap={{ scale: 0.95 }}
            onClick={() => { markInteraction(); setIsOn(!isOn); }}
            className={`relative z-10 w-28 h-28 rounded-full flex items-center justify-center cursor-pointer transition-all duration-700 shadow-2xl ${
              isOn 
                ? "border-[0.5px] border-white/40" 
                : "bg-gray-50 border-gray-100 dark:bg-gray-900 dark:border-gray-800"
            }`}
            style={isOn ? { 
              backgroundColor: color,
              boxShadow: `0 15px 45px ${color}66, inset 0 0 20px rgba(255,255,255,0.4)`
            } : {
              boxShadow: "0 10px 30px rgba(0,0,0,0.02), inset 0 2px 5px rgba(255,255,255,0.9)"
            }}
          >
            <Lightbulb className={`w-10 h-10 transition-all duration-700 ${isOn ? "text-white drop-shadow-[0_0_10px_rgba(255,255,255,0.8)]" : "text-gray-300"}`} />
          </motion.div>

          <div className="mt-12 flex bg-gray-100/80 dark:bg-gray-900/80 backdrop-blur-md p-1 rounded-2xl w-40 shadow-inner z-10 relative border border-gray-200/50 dark:border-gray-800/50">
            <button
              onClick={() => { markInteraction(); setIsOn(true); }}
              className={`flex-1 py-2.5 rounded-xl text-[10px] font-black tracking-widest transition-all duration-300 ${
                isOn 
                  ? "bg-white dark:bg-gray-800 text-gray-900 dark:text-white shadow-md" 
                  : "text-gray-400 hover:text-gray-500"
              }`}
            >
              ON
            </button>
            <button
              onClick={() => { markInteraction(); setIsOn(false); }}
              className={`flex-1 py-2.5 rounded-xl text-[10px] font-black tracking-widest transition-all duration-300 ${
                !isOn 
                  ? "bg-white dark:bg-gray-800 text-gray-900 dark:text-white shadow-md" 
                  : "text-gray-400 hover:text-gray-500"
              }`}
            >
              OFF
            </button>
          </div>
        </div>

        <div className="grid grid-cols-1 gap-6">
          <div className="bg-white/70 dark:bg-gray-900/70 backdrop-blur-xl rounded-[2.5rem] p-8 shadow-xl border border-white/40 dark:border-gray-800/40 transition-all duration-500">
            <div className="flex items-center justify-between mb-8">
              <div>
                <h3 className="text-sm font-bold text-gray-900 dark:text-white uppercase tracking-wider">Mood Color</h3>
                <p className="text-[10px] text-gray-500 mt-1">원하는 색상을 선택해 보세요</p>
              </div>
              {isOn && (
                <span className="text-[10px] font-black tracking-widest text-gray-400 bg-gray-100 dark:bg-gray-800 px-3 py-1.5 rounded-full border border-gray-200/50 dark:border-gray-700/50">
                  {color.toUpperCase()}
                </span>
              )}
            </div>
            
            <ColorWheel 
              color={color}
              thumbPos={thumbPos}
              setThumbPos={setThumbPos}
              onChange={(newColor) => { markInteraction(); setColor(newColor); }}
              disabled={!isOn || effect === 2} // 무지개 모드일 때는 컬러휠 비활성화
              onInteractionStart={markInteraction}
            />
          </div>

          <div className="bg-white/70 dark:bg-gray-900/70 backdrop-blur-xl rounded-[2rem] p-6 shadow-lg border border-white/40 dark:border-gray-800/40">
            <div className="flex items-center justify-between mb-4">
              <h3 className="text-xs font-bold text-gray-900 dark:text-white flex items-center gap-2 uppercase tracking-wider">
                <SunMedium className="w-4 h-4 text-amber-500" />
                Brightness
              </h3>
              <span className="text-sm font-black text-gray-900 dark:text-white bg-gray-100 dark:bg-gray-800 px-3 py-1 rounded-full border border-gray-200/50 dark:border-gray-700/50">
                {brightness}%
              </span>
            </div>
            
            {/* 100% 동기화 및 프리미엄 미적 디자인이 적용된 밝기 조절 슬라이더 */}
            <div className="py-6 px-1 select-none relative">
              {/* 드래그 조작 시 노브 위에 깔끔하게 떠오르는 실시간 툴팁 */}
              <div 
                className={`absolute -top-1 transition-all duration-200 pointer-events-none z-30 ${
                  isDraggingSlider && isOn ? 'opacity-100 scale-100 -translate-y-2' : 'opacity-0 scale-90 translate-y-1'
                }`}
                style={{
                  left: `${brightness}%`,
                  transform: 'translateX(-50%)'
                }}
              >
                <div 
                  className="px-2.5 py-1 rounded-lg text-white dark:text-gray-900 text-[11px] font-black shadow-xl flex items-center gap-1 border border-white/20"
                  style={{ backgroundColor: isOn ? color : '#4b5563' }}
                >
                  <span>{brightness}%</span>
                </div>
              </div>

              <div className="relative w-full h-3.5 bg-gray-200/80 dark:bg-gray-800/80 rounded-full flex items-center shadow-inner overflow-visible">
                {/* 진행 게이지 바 - 노브 위치와 100% 일치하도록 보장 */}
                <div 
                  className="h-full rounded-full transition-colors duration-300 relative"
                  style={{ 
                    width: `${brightness}%`,
                    background: isOn 
                      ? `linear-gradient(90deg, ${color}66 0%, ${color} 100%)` 
                      : "#9ca3af",
                    boxShadow: isOn ? `0 0 10px ${color}88` : undefined,
                    transition: isDraggingSlider ? 'none' : 'width 0.2s cubic-bezier(0.4, 0, 0.2, 1), background 0.3s ease'
                  }}
                />
                
                {/* 원형 조절 노브 (Thumb Handle) - 게이지 바 끝점과 100% 동기화 */}
                <div 
                  className="absolute w-7 h-7 rounded-full bg-white border-[3px] shadow-md flex items-center justify-center pointer-events-none z-20"
                  style={{ 
                    left: `${brightness}%`,
                    transform: `translateX(-50%) ${isDraggingSlider ? 'scale(1.2)' : 'scale(1)'}`,
                    borderColor: isOn ? color : "#9ca3af",
                    boxShadow: isOn 
                      ? `0 0 14px ${color}aa, 0 3px 10px rgba(0,0,0,0.25)` 
                      : "0 2px 6px rgba(0,0,0,0.15)",
                    transition: isDraggingSlider ? 'transform 0.15s ease' : 'left 0.2s cubic-bezier(0.4, 0, 0.2, 1), transform 0.15s ease, border-color 0.3s ease, box-shadow 0.3s ease'
                  }}
                >
                  <div 
                    className="w-2.5 h-2.5 rounded-full transition-colors duration-300"
                    style={{ backgroundColor: isOn ? color : "#9ca3af" }}
                  />
                </div>

                {/* 대형 반응형 터치 영역 (Range Input) */}
                <input
                  type="range"
                  min="0"
                  max="100"
                  value={brightness}
                  onPointerDown={() => {
                    markInteraction();
                    setIsDraggingSlider(true);
                  }}
                  onPointerUp={() => setIsDraggingSlider(false)}
                  onPointerCancel={() => setIsDraggingSlider(false)}
                  onChange={(e) => {
                    markInteraction();
                    setBrightness(Number(e.target.value));
                  }}
                  disabled={!isOn}
                  className="absolute -top-4 left-0 w-full h-12 opacity-0 cursor-pointer disabled:cursor-not-allowed z-30"
                />
              </div>
            </div>

            {/* 미세 조절 및 퀵 프리셋 버튼 */}
            <div className="flex items-center justify-between gap-2 mt-2 pt-3 border-t border-gray-100 dark:border-gray-800/60">
              <div className="flex gap-1.5">
                <button
                  disabled={!isOn || brightness <= 0}
                  onClick={() => { markInteraction(); setBrightness(prev => Math.max(0, prev - 10)); }}
                  className="p-2 rounded-xl bg-gray-100 dark:bg-gray-800 hover:bg-gray-200 dark:hover:bg-gray-700 text-gray-700 dark:text-gray-300 disabled:opacity-40 transition-colors"
                  title="밝기 감소"
                >
                  <Minus className="w-3.5 h-3.5" />
                </button>
                <button
                  disabled={!isOn || brightness >= 100}
                  onClick={() => { markInteraction(); setBrightness(prev => Math.min(100, prev + 10)); }}
                  className="p-2 rounded-xl bg-gray-100 dark:bg-gray-800 hover:bg-gray-200 dark:hover:bg-gray-700 text-gray-700 dark:text-gray-300 disabled:opacity-40 transition-colors"
                  title="밝기 증가"
                >
                  <Plus className="w-3.5 h-3.5" />
                </button>
              </div>

              <div className="flex gap-1.5">
                {[25, 50, 75, 100].map((preset) => (
                  <button
                    key={preset}
                    disabled={!isOn}
                    onClick={() => { markInteraction(); setBrightness(preset); }}
                    className={`px-2.5 py-1 rounded-xl text-[11px] font-bold transition-all ${
                      brightness === preset && isOn
                        ? "bg-gray-900 dark:bg-white text-white dark:text-gray-900 shadow-sm"
                        : "bg-gray-100 dark:bg-gray-800 hover:bg-gray-200 dark:hover:bg-gray-700 text-gray-600 dark:text-gray-400 disabled:opacity-40"
                    }`}
                  >
                    {preset}%
                  </button>
                ))}
              </div>
            </div>
          </div>


        </div>
      </div>

      <BottomNav />
    </div>
  );
}

