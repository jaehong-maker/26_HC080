import React from "react";
import { motion } from "motion/react";

interface ModeCardProps {
  id: string;
  title: string;
  description: string;
  icon: React.ReactNode;
  color: string;
  activeColor: string;
  borderColor: string;
  dotColor: string;
  isSelected: boolean;
  onClick: () => void;
  disabled?: boolean;
}

export function ModeButton({
  title,
  description,
  icon,
  color,
  activeColor,
  borderColor,
  dotColor,
  isSelected,
  onClick,
  disabled
}: ModeCardProps) {
  return (
    <button
      onClick={onClick}
      disabled={disabled}
      className={`w-full flex items-center p-4 rounded-2xl transition-all border-2 ${
        isSelected 
          ? `${borderColor} bg-white dark:bg-gray-900 shadow-md` 
          : "border-gray-200 dark:border-gray-800 bg-white dark:bg-gray-900 hover:bg-gray-50 dark:hover:bg-gray-800 shadow-sm"
      } disabled:opacity-50`}
    >
      <div
        className={`w-12 h-12 rounded-xl flex items-center justify-center mr-4 transition-colors ${
          isSelected ? activeColor : color
        }`}
      >
        {icon}
      </div>
      <div className="text-left flex-1">
        <h4 className="text-base font-bold text-gray-900 dark:text-gray-100 transition-colors">
          {title}
        </h4>
        <p className="text-xs text-gray-500 dark:text-gray-400 font-medium mt-0.5 transition-colors">
          {description}
        </p>
      </div>
      <div
        className={`w-6 h-6 rounded-full border-2 flex items-center justify-center transition-colors ${
          isSelected ? borderColor : "border-gray-300 dark:border-gray-700"
        }`}
      >
        {isSelected && (
          <div className={`w-3 h-3 rounded-full ${dotColor}`} />
        )}
      </div>
    </button>
  );
}
