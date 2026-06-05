import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    open: true,
  },
  // JUCE WebView は origin = juce://juce.backend/ で
  // resource provider 経由でファイルを返すため、相対パスにする必要がある。
  base: './',
  build: {
    assetsDir: 'assets',
    outDir: 'dist',
  },
});
