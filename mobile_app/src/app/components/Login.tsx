import React, { useState, useEffect } from "react";
import { useNavigate } from "react-router";
import { Wind, Loader2 } from "lucide-react";
import { useAuth } from "../store/AuthContext";

export function Login() {
  const navigate = useNavigate();
  const { currentUser, loginUser } = useAuth();
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    if (currentUser) {
      navigate("/modes", { replace: true });
    }
  }, [currentUser, navigate]);

  const handleLogin = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!email || !password) {
      setError("이메일과 비밀번호를 모두 입력해주세요.");
      return;
    }

    setLoading(true);
    setError("");

    try {
      const result = await loginUser(email, password);
      if (result.success) {
        navigate("/modes");
      } else {
        setError(result.message || "이메일 또는 비밀번호가 올바르지 않습니다.");
      }
    } catch (err) {
      setError("로그인 중 오류가 발생했습니다.");
    } finally {
      setLoading(false);
    }
  };

  return (
    <div key="login-screen" className="h-screen w-full bg-white dark:bg-gray-950 flex flex-col items-center justify-center px-6 relative overflow-hidden fixed inset-0 transition-colors duration-500">
      {/* Soft background light */}
      <div className="absolute top-[-10%] right-[-10%] w-[120%] h-[120%] bg-[radial-gradient(circle_at_50%_0%,_#f8fafc_0%,_#ffffff_100%)] dark:bg-[radial-gradient(circle_at_50%_0%,_#0f172a_0%,_#030712_100%)] -z-10" />
      <div className="absolute top-[-20%] left-[-10%] w-96 h-96 bg-gray-50/50 dark:bg-blue-900/10 rounded-full blur-3xl -z-10" />

      <div className="bg-white dark:bg-gray-900 p-8 sm:p-10 rounded-[2.5rem] shadow-[0_8px_40px_-12px_rgba(0,0,0,0.1)] dark:shadow-[0_8px_40px_-12px_rgba(0,0,0,0.5)] w-full max-w-md border border-gray-200 dark:border-gray-800 shrink-0 transition-colors duration-300">
        <div className="flex flex-col items-center mb-8 sm:mb-10">
          <div className="w-14 h-14 sm:w-16 sm:h-16 bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 rounded-full flex items-center justify-center mb-4 sm:mb-6 shadow-sm">
            <Wind className="text-gray-800 dark:text-gray-200 w-7 h-7 sm:w-8 sm:h-8" strokeWidth={1.5} />
          </div>
          <h1 className="text-xl sm:text-2xl font-bold text-gray-900 dark:text-white mb-1.5 sm:mb-2 tracking-tight">
            Smart Diffuser
          </h1>
          <p className="text-gray-500 dark:text-gray-400 text-xs sm:text-sm font-medium">
            당신의 공간을 향기로 채워보세요
          </p>
        </div>

        <form onSubmit={handleLogin} className="space-y-3 sm:space-y-4">
          <div>
            <input
              type="text"
              value={email}
              onChange={(e) => setEmail(e.target.value)}
              placeholder="이메일"
              disabled={loading}
              className="w-full px-4 sm:px-5 py-3 sm:py-4 bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 shadow-sm rounded-2xl text-gray-800 dark:text-gray-100 placeholder:text-gray-400 dark:placeholder:text-gray-500 focus:outline-none focus:ring-2 focus:ring-gray-300 dark:focus:ring-gray-700 focus:border-gray-400 dark:focus:border-gray-600 transition-all font-medium text-sm sm:text-base disabled:opacity-50"
            />
          </div>
          <div>
            <input
              type="password"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              placeholder="비밀번호"
              disabled={loading}
              className="w-full px-4 sm:px-5 py-3 sm:py-4 bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 shadow-sm rounded-2xl text-gray-800 dark:text-gray-100 placeholder:text-gray-400 dark:placeholder:text-gray-500 focus:outline-none focus:ring-2 focus:ring-gray-300 dark:focus:ring-gray-700 focus:border-gray-400 dark:focus:border-gray-600 transition-all font-medium text-sm sm:text-base disabled:opacity-50"
            />
          </div>
          {error && <p className="text-red-500 text-xs font-semibold px-2">{error}</p>}

          <div className="pt-4 sm:pt-6">
            <button
              type="submit"
              disabled={loading}
              className="w-full py-3.5 sm:py-4 bg-gray-900 dark:bg-gray-100 hover:bg-gray-800 dark:hover:bg-white text-white dark:text-gray-900 rounded-2xl font-bold shadow-lg shadow-gray-200/50 dark:shadow-black/50 transition-all transform hover:-translate-y-0.5 active:translate-y-0 border border-gray-800 dark:border-gray-200 text-sm sm:text-base flex items-center justify-center disabled:opacity-70 disabled:transform-none"
            >
              {loading ? <Loader2 className="w-5 h-5 animate-spin" /> : "로그인"}
            </button>
          </div>
        </form>

        <div className="mt-6 sm:mt-8 text-center">
          <p className="text-gray-500 dark:text-gray-400 text-xs sm:text-sm font-medium">
            아직 계정이 없으신가요?{" "}
            <button 
              onClick={() => navigate("/signup")}
              className="text-gray-900 dark:text-gray-100 font-bold hover:text-gray-600 dark:hover:text-gray-300 transition-colors ml-1"
            >
              회원가입
            </button>
          </p>
        </div>
      </div>
    </div>
  );
}
