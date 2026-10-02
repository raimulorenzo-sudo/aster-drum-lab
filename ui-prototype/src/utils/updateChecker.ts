export const UPDATE_VERSION_URL =
  'https://raw.githubusercontent.com/raimulorenzo-sudo/aster-drum-lab-update/main/latest-version.txt';

const UPDATE_CACHE_KEY = 'ASTER_UPDATE_CACHE_V1';
const UPDATE_NOTICE_KEY = 'ASTER_UPDATE_NOTICE_V1';
export const UPDATE_CACHE_MAX_AGE_MS = 24 * 60 * 60 * 1000;
export const UPDATE_NOTICE_COOLDOWN_MS = 24 * 60 * 60 * 1000;

const VERSION_PATTERN = /^v?(\d+)\.(\d+)\.(\d+)$/i;

/** `1.0.0` / `v1.0.0` のような3要素のバージョンを比較用の数値に変換する。 */
export function parseVersion(value: string): [number, number, number] | null {
  const match = value.trim().match(VERSION_PATTERN);
  if (!match) return null;
  return [Number(match[1]), Number(match[2]), Number(match[3])];
}

/** latest が current より新しい場合だけ true。 */
export function isNewerVersion(latest: string, current: string): boolean {
  const latestParts = parseVersion(latest);
  const currentParts = parseVersion(current);
  if (!latestParts || !currentParts) throw new Error('Invalid version format');

  for (let index = 0; index < latestParts.length; index += 1) {
    if (latestParts[index] > currentParts[index]) return true;
    if (latestParts[index] < currentParts[index]) return false;
  }
  return false;
}

/** GitHub の公開ファイルから最新バージョンを取得する。 */
export async function fetchLatestVersion(signal?: AbortSignal): Promise<string> {
  const response = await fetch(UPDATE_VERSION_URL, {
    cache: 'no-store',
    signal,
  });
  if (!response.ok) throw new Error(`Update check failed: ${response.status}`);

  const version = (await response.text()).trim();
  if (!parseVersion(version)) throw new Error('Invalid latest version');
  return version.replace(/^v/i, '');
}

interface StoredUpdateInfo {
  version: string;
  savedAt: number;
}

function readStoredUpdateInfo(storage: Storage, key: string): StoredUpdateInfo | null {
  try {
    const parsed = JSON.parse(storage.getItem(key) ?? '') as Partial<StoredUpdateInfo>;
    if (!parseVersion(parsed.version ?? '') || !Number.isFinite(parsed.savedAt)) return null;
    return { version: parsed.version!.replace(/^v/i, ''), savedAt: parsed.savedAt! };
  } catch {
    return null;
  }
}

/** 24時間以内に取得した最新バージョンを返す。 */
export function readCachedLatestVersion(
  storage: Storage,
  now = Date.now(),
): string | null {
  const cached = readStoredUpdateInfo(storage, UPDATE_CACHE_KEY);
  if (!cached || now - cached.savedAt >= UPDATE_CACHE_MAX_AGE_MS) return null;
  return cached.version;
}

export function cacheLatestVersion(storage: Storage, version: string, now = Date.now()): void {
  if (!parseVersion(version)) return;
  try {
    storage.setItem(UPDATE_CACHE_KEY, JSON.stringify({ version, savedAt: now }));
  } catch {
    // ストレージが無効でも更新確認自体は続行できる。
  }
}

/** 別のプラグインインスタンスで同じ通知を連続表示しない。 */
export function shouldShowStartupNotice(
  storage: Storage,
  version: string,
  now = Date.now(),
): boolean {
  const shown = readStoredUpdateInfo(storage, UPDATE_NOTICE_KEY);
  return !shown
    || shown.version !== version
    || now - shown.savedAt >= UPDATE_NOTICE_COOLDOWN_MS;
}

export function markStartupNoticeShown(storage: Storage, version: string, now = Date.now()): void {
  if (!parseVersion(version)) return;
  try {
    storage.setItem(UPDATE_NOTICE_KEY, JSON.stringify({ version, savedAt: now }));
  } catch {
    // 保存できない環境では現在のインスタンス内だけで抑制する。
  }
}
