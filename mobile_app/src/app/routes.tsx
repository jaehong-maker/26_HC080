import { createBrowserRouter, Navigate, Outlet, ScrollRestoration, useLocation, useNavigate } from "react-router";
import { useEffect, useRef } from "react";
import { App as CapApp } from "@capacitor/app";
import { toast } from "sonner";
import { Login } from "./components/Login";
import { SignupScreen } from "./components/SignupScreen";
import { ModesScreen } from "./components/ModesScreen";
import { ScentsScreen } from "./components/ScentsScreen";
import { SettingsScreen } from "./components/SettingsScreen";
import { DiaryScreen } from "./components/DiaryScreen";
import { PrivacyScreen } from "./components/PrivacyScreen";
import { DeviceManagementScreen } from "./components/DeviceManagementScreen";
import { LedSettingsScreen } from "./components/LedSettingsScreen";
import { MusicSettingsScreen } from "./components/MusicSettingsScreen";
import { AiTasteReportScreen } from "./components/AiTasteReportScreen";

function Root() {
  const location = useLocation();
  const navigate = useNavigate();
  const lastTimeRef = useRef<number>(0);
  
  useEffect(() => {
    window.scrollTo(0, 0);
  }, [location.pathname]);

  useEffect(() => {
    const handleBackButton = CapApp.addListener("backButton", (data) => {
      const currentPath = window.location.pathname;

      // 최상위 메인 화면(로그인, 회원가입, 홈/모드 화면)인 경우
      if (currentPath === "/" || currentPath === "/modes" || currentPath === "/signup") {
        const now = Date.now();
        if (now - lastTimeRef.current < 2000) {
          CapApp.exitApp();
        } else {
          lastTimeRef.current = now;
          toast("한 번 더 누르면 앱이 종료됩니다.", { icon: "ℹ️" });
        }
      } else {
        // 서브 페이지인 경우 이전 화면으로 뒤로가기
        if (data.canGoBack) {
          window.history.back();
        } else {
          navigate(-1);
        }
      }
    });

    return () => {
      handleBackButton.then(listener => listener.remove());
    };
  }, [navigate]);

  return (
    <>
      <ScrollRestoration />
      <Outlet />
    </>
  );
}

export const router = createBrowserRouter([
  {
    path: "/",
    Component: Root,
    children: [
      {
        index: true,
        Component: Login,
      },
      {
        path: "signup",
        Component: SignupScreen,
      },
      {
        path: "home",
        element: <Navigate to="/modes" replace />,
      },
      {
        path: "modes",
        Component: ModesScreen,
      },
      {
        path: "led",
        Component: LedSettingsScreen,
      },
      {
        path: "diary",
        Component: DiaryScreen,
      },
      {
        path: "scents",
        Component: ScentsScreen,
      },
      {
        path: "settings",
        Component: SettingsScreen,
      },
      {
        path: "privacy",
        Component: PrivacyScreen,
      },
      {
        path: "ai-report",
        Component: AiTasteReportScreen,
      },
      {
        path: "device",
        Component: DeviceManagementScreen,
      },
      {
        path: "music",
        Component: MusicSettingsScreen,
      },
      {
        path: "*",
        element: <Navigate to="/" replace />,
      }
    ],
  }
]);
