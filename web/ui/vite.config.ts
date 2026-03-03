import { defineConfig } from 'vite';
import preact from '@preact/preset-vite';

export default defineConfig({
  plugins: [preact()],
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    rollupOptions: {
      output: {
        // Single chunk for simplicity
        manualChunks: undefined,
      },
    },
    // Target modern browsers only (edge devices use latest Chromium)
    target: 'es2020',
    // Minimize bundle size
    minify: 'terser',
    terserOptions: {
      compress: {
        drop_console: true,
        drop_debugger: true,
      },
    },
  },
  server: {
    port: 5173,
    host: true,
    proxy: {
      // Proxy API requests to Go server during development
      // Use 'server' hostname when running in Docker, 'localhost' otherwise
      '/api': {
        target: process.env.VITE_API_URL || 'http://localhost:8080',
        changeOrigin: true,
      },
      '/ws': {
        target: process.env.VITE_WS_URL || 'ws://localhost:8080',
        ws: true,
      },
    },
  },
});
