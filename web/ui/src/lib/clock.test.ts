import { describe, expect, it } from 'vitest';
import { deviceAgeSeconds, estimateDeviceNow } from './clock';

describe('clock-skew-safe ages', () => {
  it('measures section ages on the device clock, even when it is far behind the browser', () => {
    // A board that booted without RTC or NTP thinks it is 1970.
    const generatedAt = '1970-01-01T00:10:00Z';
    const browserSawItAt = Date.parse('2026-07-20T12:00:00Z');
    const browserNow = browserSawItAt + 30_000;

    const deviceNow = estimateDeviceNow(generatedAt, browserSawItAt, browserNow);
    expect(deviceAgeSeconds('1970-01-01T00:05:00Z', deviceNow)).toBe(330);
  });

  it('never reports negative ages', () => {
    const deviceNow = Date.parse('2026-07-20T12:00:00Z');
    expect(deviceAgeSeconds('2026-07-20T12:00:05Z', deviceNow)).toBe(0);
  });

  it('returns null for missing or unparseable timestamps', () => {
    expect(deviceAgeSeconds(undefined, Date.now())).toBeNull();
    expect(deviceAgeSeconds('not a date', Date.now())).toBeNull();
    expect(estimateDeviceNow('not a date', 0, 0)).toBeNull();
  });
});
