// Clock-skew-safe time arithmetic.
//
// The gateway's clock and the browser's clock can disagree by minutes (or
// years, on a board that booted without RTC or NTP). So we never subtract a
// device timestamp from `Date.now()`. Instead:
//
//   - "how long since new data" uses only the browser clock: the time since
//     this UI first saw the current `generated_at`;
//   - section ages use only the device clock: device-now is estimated as
//     `generated_at` plus the browser time elapsed since we first saw it.

export function parseTime(iso: string | null | undefined): number | null {
  if (!iso) return null;
  const t = Date.parse(iso);
  return Number.isNaN(t) ? null : t;
}

/** Estimated current time on the device's clock, in ms since the epoch. */
export function estimateDeviceNow(generatedAt: string, firstSeenAtMs: number, browserNowMs: number): number | null {
  const generated = parseTime(generatedAt);
  if (generated === null) return null;
  return generated + Math.max(0, browserNowMs - firstSeenAtMs);
}

/** Seconds between a device timestamp and estimated device-now; null if unknown. */
export function deviceAgeSeconds(iso: string | null | undefined, deviceNowMs: number | null): number | null {
  const t = parseTime(iso);
  if (t === null || deviceNowMs === null) return null;
  return Math.max(0, (deviceNowMs - t) / 1000);
}

/**
 * How long without a new snapshot before the page says so. The daemon's
 * default collection interval is 60 s; three missed cycles is a real gap
 * rather than jitter.
 */
export const SNAPSHOT_SILENCE_SECONDS = 180;
