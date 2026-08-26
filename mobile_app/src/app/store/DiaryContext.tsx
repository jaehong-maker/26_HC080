import React, { createContext, useContext, useState } from "react";
import { apiFetchDiaries } from "../utils/api";

export interface DiaryEntry {
  id: string;
  date: string;
  timestamp?: string;
  emotion: string;
  mood?: string; // 호환용
  scent: string;
  text: string;
  note?: string; // 호환용
  color?: string;
}

interface DiaryContextType {
  diaries: Record<string, DiaryEntry[]>;
  addDiaryEntry: (email: string, entry: DiaryEntry) => void;
  fetchDiaries: (email: string) => Promise<void>;
}

const DiaryContext = createContext<DiaryContextType | undefined>(undefined);

export const DiaryProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const [diaries, setDiaries] = useState<Record<string, DiaryEntry[]>>({});

  const addDiaryEntry = (email: string, entry: DiaryEntry) => {
    setDiaries(prev => {
      const userDiaries = prev[email] || [];
      return { ...prev, [email]: [entry, ...userDiaries] };
    });
  };

  const fetchDiaries = async (email: string) => {
    try {
      const res = await apiFetchDiaries(email);
      if (res.result === "SUCCESS" && res.diaries) {
        setDiaries(prev => ({
          ...prev,
          [email]: res.diaries
        }));
      }
    } catch (err) {
      console.error("Fetch Diaries Error:", err);
    }
  };

  return (
    <DiaryContext.Provider value={{ diaries, addDiaryEntry, fetchDiaries }}>
      {children}
    </DiaryContext.Provider>
  );
};

export const useDiary = () => {
  const context = useContext(DiaryContext);
  if (!context) throw new Error("useDiary must be used within a DiaryProvider");
  return context;
};
