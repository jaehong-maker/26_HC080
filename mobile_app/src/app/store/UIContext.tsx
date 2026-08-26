import React, { createContext, useContext, useState } from "react";

interface UIContextType {
  selectedMusic: string;
  setSelectedMusic: (music: string) => void;
}

const UIContext = createContext<UIContextType | undefined>(undefined);

export const UIProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const [selectedMusic, setSelectedMusic] = useState<string>("비오는 날의 피아노.mp3");

  return (
    <UIContext.Provider value={{ selectedMusic, setSelectedMusic }}>
      {children}
    </UIContext.Provider>
  );
};

export const useUI = () => {
  const context = useContext(UIContext);
  if (!context) throw new Error("useUI must be used within a UIProvider");
  return context;
};
