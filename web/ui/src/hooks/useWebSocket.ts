import { useEffect, useRef } from 'preact/hooks';
import type { HealthState } from '../types/health';
import { updateHealthState, setConnected, isOffline } from '../state/signals';
import { persistState, loadPersistedState } from '../state/persistence';

const WS_URL = `${window.location.protocol === 'https:' ? 'wss:' : 'ws:'}//${window.location.host}/ws/health`;
const API_URL = '/api/health';

// Reconnection config
const INITIAL_DELAY = 1000;
const MAX_DELAY = 30000;
const BACKOFF_MULTIPLIER = 1.5;

// Polling fallback interval
const POLL_INTERVAL = 5000;

export function useWebSocket() {
  const wsRef = useRef<WebSocket | null>(null);
  const reconnectDelayRef = useRef(INITIAL_DELAY);
  const reconnectTimerRef = useRef<number | null>(null);
  const pollTimerRef = useRef<number | null>(null);

  useEffect(() => {
    // Load persisted state on mount
    const persisted = loadPersistedState();
    if (persisted) {
      updateHealthState(persisted);
    }

    // Initial fetch via HTTP
    fetchState();

    // Try WebSocket connection
    connect();

    return () => {
      if (wsRef.current) {
        wsRef.current.close();
      }
      if (reconnectTimerRef.current) {
        clearTimeout(reconnectTimerRef.current);
      }
      if (pollTimerRef.current) {
        clearInterval(pollTimerRef.current);
      }
    };
  }, []);

  function connect() {
    // Don't connect if offline
    if (isOffline.value) {
      startPolling();
      return;
    }

    try {
      const ws = new WebSocket(WS_URL);

      ws.onopen = () => {
        setConnected(true);
        reconnectDelayRef.current = INITIAL_DELAY;
        stopPolling();
      };

      ws.onmessage = (event) => {
        try {
          const state = JSON.parse(event.data) as HealthState;
          updateHealthState(state);
          persistState(state);
        } catch {
          // Invalid JSON, ignore
        }
      };

      ws.onclose = () => {
        setConnected(false);
        wsRef.current = null;
        scheduleReconnect();
      };

      ws.onerror = () => {
        // Will trigger onclose
      };

      wsRef.current = ws;
    } catch {
      // WebSocket creation failed, fall back to polling
      startPolling();
    }
  }

  function scheduleReconnect() {
    // Clear any existing timer
    if (reconnectTimerRef.current) {
      clearTimeout(reconnectTimerRef.current);
    }

    // Schedule reconnect with exponential backoff
    reconnectTimerRef.current = window.setTimeout(() => {
      reconnectTimerRef.current = null;
      connect();
    }, reconnectDelayRef.current);

    // Increase delay for next attempt
    reconnectDelayRef.current = Math.min(
      reconnectDelayRef.current * BACKOFF_MULTIPLIER,
      MAX_DELAY
    );

    // Start polling while disconnected
    startPolling();
  }

  function startPolling() {
    if (pollTimerRef.current) return;

    pollTimerRef.current = window.setInterval(() => {
      fetchState();
    }, POLL_INTERVAL);
  }

  function stopPolling() {
    if (pollTimerRef.current) {
      clearInterval(pollTimerRef.current);
      pollTimerRef.current = null;
    }
  }

  async function fetchState() {
    try {
      const response = await fetch(API_URL);
      if (response.ok) {
        const state = await response.json() as HealthState;
        updateHealthState(state);
        persistState(state);
      }
    } catch {
      // Network error, will retry on next poll
    }
  }
}
