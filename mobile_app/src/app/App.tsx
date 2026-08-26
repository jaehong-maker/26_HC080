import { RouterProvider } from "react-router";
import { router } from "./routes";
import { useEffect } from "react";
import { AuthProvider } from "./store/AuthContext";
import { DeviceProvider } from "./store/DeviceContext";
import { DiaryProvider } from "./store/DiaryContext";
import { UIProvider } from "./store/UIContext";import { Toaster } from "./components/ui/sonner";

export default function App() {
  useEffect(() => {
    document.title = "Smart Diffuser";
  }, []);

  return (
    <AuthProvider>
      <DeviceProvider>
        <DiaryProvider>
          <UIProvider>
            <RouterProvider router={router} />
            <Toaster position="top-center" />
          </UIProvider>
        </DiaryProvider>
      </DeviceProvider>
    </AuthProvider>
  );
}
