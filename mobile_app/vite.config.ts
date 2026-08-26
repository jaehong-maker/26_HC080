import { defineConfig } from 'vite'
import path from 'path'
import tailwindcss from '@tailwindcss/vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [
    react(),
    tailwindcss(),
  ],
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src'),
    },
  },
  build: {
    target: 'esnext',
    minify: false,
    cssMinify: false,
    reportCompressedSize: false,
    sourcemap: false, // ★ 소스맵 비활성화 (빌드 속도 향상)
    lib: false,
    rollupOptions: {
      treeshake: false,
      output: {
        manualChunks: undefined,
      },
    },
    commonjsOptions: {
      include: [/node_modules/],
    }
  },
  optimizeDeps: {
    // 미리 빌드하여 속도 향상
    include: ['react', 'react-dom', 'react-router', 'motion/react', 'lucide-react'],
  },
  assetsInclude: ['**/*.svg', '**/*.csv'],
})

