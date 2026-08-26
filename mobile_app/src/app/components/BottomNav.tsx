import React from "react";
import { Link, useLocation } from "react-router";
import { Home, Droplets, Lightbulb, BookHeart, Settings } from "lucide-react";

export function BottomNav() {
  const location = useLocation();
  const path = location.pathname;

  const navItems = [
    { path: "/modes", label: "홈", icon: Home },
    { path: "/scents", label: "향기", icon: Droplets },
    { path: "/led", label: "조명", icon: Lightbulb },
    { path: "/diary", label: "일기장", icon: BookHeart },
    { path: "/settings", label: "설정", icon: Settings },
  ];

  return (
    <nav 
      className="fixed bottom-0 left-0 right-0 bg-white/90 dark:bg-gray-950/90 backdrop-blur-md border-t border-gray-100 dark:border-gray-800 px-4 sm:px-6 pt-3 flex justify-around items-center z-20 transition-colors duration-300"
      style={{ paddingBottom: 'calc(1.5rem + env(safe-area-inset-bottom, 0px))' }}
    >
      {navItems.map(({ path: itemPath, label, icon: Icon }) => {
        const isActive = path.startsWith(itemPath);
        return (
          <Link
            key={itemPath}
            to={itemPath}
            className={`flex flex-col items-center gap-1 transition-colors outline-none focus:outline-none [-webkit-tap-highlight-color:transparent] ${
              isActive 
                ? "text-gray-900 dark:text-white" 
                : "text-gray-400 dark:text-gray-500 hover:text-gray-900 dark:hover:text-gray-200"
            }`}
          >
            <Icon className="w-5 h-5 sm:w-6 sm:h-6" />
            <span className="text-[10px] font-bold">{label}</span>
          </Link>
        );
      })}
    </nav>
  );
}
