// TypeScript types matching edge.health.state.v1.0 JSON schema

export type Severity = 'ok' | 'warn' | 'crit' | 'unknown';
export type ServiceState = 'active' | 'inactive' | 'failed' | 'activating' | 'deactivating' | 'unknown';
export type LinkState = 'up' | 'down' | 'unknown';
export type Duplex = 'full' | 'half' | 'unknown';
export type TimeSyncSource = 'none' | 'ntp' | 'ptp';
export type TimeSyncState = 'locked' | 'free_running' | 'holdover' | 'unknown';
export type UpdateResult = 'success' | 'failed' | 'unknown';

export interface OsInfo {
  distro: string;
  version?: string;
  build_id?: string;
  kernel: string;
}

export interface DeviceInfo {
  device_id: string;
  hostname: string;
  platform: string;
  arch: string;
  os: OsInfo;
}

export interface BootStatus {
  boot_id: string;
  last_boot_at: string;
  uptime: number;
  boot_ok: boolean;
  boot_fail_count: number;
  last_reboot_reason?: string | null;
}

export interface ServiceUnit {
  name: string;
  state: ServiceState;
  severity: Severity;
  since?: string | null;
  restart_count: number;
  result?: string | null;
  detail?: string | null;
  log_excerpt?: string[];
}

export interface ServicesStatus {
  overall: Severity;
  units: ServiceUnit[];
}

export interface CpuLoad {
  load1: number;
  load5: number;
  load15: number;
}

export interface MemoryUsage {
  mem_total_mb: number;
  mem_used_mb: number;
  swap_used_mb: number;
}

export interface StorageMount {
  mount: string;
  fs?: string;
  used_pct: number;
  avail_mb: number;
}

export interface ThermalSensor {
  sensor: string;
  temp_c: number;
}

export interface NetworkInterface {
  ifname: string;
  link: LinkState;
  rx_bytes: number;
  tx_bytes: number;
  rx_packets: number;
  tx_packets: number;
  rx_dropped: number;
  tx_dropped: number;
  rx_err: number;
  tx_err: number;
  ip?: string | null;
  carrier?: boolean | null;
  speed_mbps?: number | null;
  duplex?: Duplex | null;
}

export interface ResourcesStatus {
  sample_window_sec: number;
  cpu: CpuLoad;
  memory: MemoryUsage;
  storage?: StorageMount[];
  thermal?: ThermalSensor[];
  network: NetworkInterface[];
}

export interface NtpStatus {
  enabled: boolean;
  state?: TimeSyncState;
  last_sync_at?: string | null;
}

export interface PtpStatus {
  enabled: boolean;
  interface?: string | null;
  offset_ns?: number | null;
  rms_ns?: number | null;
  state?: TimeSyncState;
  last_sync_at?: string | null;
  role?: string | null;
}

export interface RtcStatus {
  enabled: boolean;
  hctosys?: boolean;       // RTC was used to set system clock at boot
  voltage_mv?: number;     // backup battery voltage in mV
  drift_sec?: number;      // RTC vs system clock skew in seconds
}

export interface TimeSyncStatus {
  overall: Severity;
  source: TimeSyncSource;
  ntp?: NtpStatus;
  ptp?: PtpStatus;
  rtc?: RtcStatus;
}

export interface JournalStatus {
  overall: Severity;
  error_count: number;
  recent_errors: string[];
}

export interface LastUpdate {
  id: string;
  installed_at?: string | null;
  result: UpdateResult;
  detail?: string | null;
}

export interface UpdateStatus {
  overall: Severity;
  active_slot?: string | null;
  last_update?: LastUpdate | null;
}

export interface SnapshotSummary {
  severity: Severity;
  reasons: string[];
  notes?: string | null;
}

export interface HealthState {
  schema: 'edge.health.state';
  schema_version: '1.0';
  generated_at: string;
  cycle: number;
  device: DeviceInfo;
  boot: BootStatus;
  services: ServicesStatus;
  resources: ResourcesStatus;
  time_sync: TimeSyncStatus;
  update: UpdateStatus;
  journal: JournalStatus;
  summary: SnapshotSummary;
}
