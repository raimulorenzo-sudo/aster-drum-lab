import { useEffect, useLayoutEffect, useRef, useState, type CSSProperties } from 'react';
import { createPortal } from 'react-dom';
import styles from './SettingsMenu.module.css';
import { measurePopup, positionPopupFromAnchor } from '../../utils/popupPosition';
import { isJuceAvailable, sendToJuce } from '../../utils/juceBridge';
import packageMeta from '../../../package.json';
import {
  cacheLatestVersion,
  fetchLatestVersion,
  isNewerVersion,
  markStartupNoticeShown,
  readCachedLatestVersion,
  shouldShowStartupNotice,
} from '../../utils/updateChecker';

// package.json のバージョンを Web UI ビルドに焼き込む。
const PLUGIN_VERSION = packageMeta.version;
const COMPANY_NAME   = 'ENIGMA';
const PLUGIN_NAME    = 'ASTER Drum Lab';

interface SettingsMenuProps {
  /** アンカー要素 (歯車ボタン) */
  anchorRef: React.RefObject<HTMLElement | null>;
  open: boolean;
  onClose: () => void;
  /** plugin が動作中のフォーマット (Standalone/AU/VST3 等)。判別不能なら 'Web' */
  pluginFormat?: string;
}

type Pane = 'main' | 'about' | 'prefs';
type UpdateState =
  | { status: 'idle' }
  | { status: 'checking' }
  | { status: 'latest' }
  | { status: 'available'; version: string }
  | { status: 'error' };

export function SettingsMenu({ anchorRef, open, onClose, pluginFormat }: SettingsMenuProps) {
  const [pane, setPane]       = useState<Pane>('main');
  const [style, setStyle]     = useState<CSSProperties>({});
  const [updateState, setUpdateState] = useState<UpdateState>({ status: 'idle' });
  const [startupNoticeVersion, setStartupNoticeVersion] = useState<string | null>(null);
  const panelRef = useRef<HTMLDivElement>(null);

  // ── ポジショニング ──
  useLayoutEffect(() => {
    if (!open) return;
    const update = () => {
      const el = anchorRef.current;
      if (!el) return;
      const rect = el.getBoundingClientRect();
      const size = measurePopup(panelRef.current, { width: 240, height: 280 });
      setStyle(positionPopupFromAnchor(rect, size, {
        align: 'start',
        width: 240,
        minHeight: 120,
        gap: 8,
      }));
    };
    update();
    window.addEventListener('resize', update);
    window.addEventListener('scroll', update, true);
    return () => {
      window.removeEventListener('resize', update);
      window.removeEventListener('scroll', update, true);
    };
  }, [open, anchorRef, pane]);

  // ── Esc で閉じる ──
  useEffect(() => {
    if (!open) return;
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape') onClose(); };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [open, onClose]);

  // エディタを開いた後に自動確認。最新・通信失敗は無表示。
  useEffect(() => {
    let cancelled = false;
    const controller = new AbortController();
    const timerId = window.setTimeout(async () => {
      try {
        const cachedVersion = readCachedLatestVersion(localStorage);
        const latestVersion = cachedVersion ?? await fetchLatestVersion(controller.signal);
        if (!cachedVersion) cacheLatestVersion(localStorage, latestVersion);
        if (cancelled || !isNewerVersion(latestVersion, PLUGIN_VERSION)) return;

        setUpdateState({ status: 'available', version: latestVersion });
        if (shouldShowStartupNotice(localStorage, latestVersion)) {
          markStartupNoticeShown(localStorage, latestVersion);
          setStartupNoticeVersion(latestVersion);
        }
      } catch (error) {
        if (!cancelled) console.warn('[Update Checker] Automatic check failed:', error);
      }
    }, 2000);

    return () => {
      cancelled = true;
      window.clearTimeout(timerId);
      controller.abort();
    };
  }, []);

  const checkForUpdates = async () => {
    if (updateState.status === 'checking') return;

    setUpdateState({ status: 'checking' });
    const controller = new AbortController();
    const timeoutId = window.setTimeout(() => controller.abort(), 8000);

    try {
      const latestVersion = await fetchLatestVersion(controller.signal);
      cacheLatestVersion(localStorage, latestVersion);
      setUpdateState(
        isNewerVersion(latestVersion, PLUGIN_VERSION)
          ? { status: 'available', version: latestVersion }
          : { status: 'latest' },
      );
    } catch (error) {
      console.warn('[Update Checker] Failed to check for updates:', error);
      setUpdateState({ status: 'error' });
    } finally {
      window.clearTimeout(timeoutId);
    }
  };

  const openBoothLibrary = () => {
    if (isJuceAvailable()) {
      sendToJuce('openBoothLibrary', {});
      return;
    }
    window.open('https://accounts.booth.pm/library', '_blank', 'noopener,noreferrer');
  };

  return createPortal(
    <>
      {startupNoticeVersion && (
        <div className={styles.startupNotice} role="status" aria-live="polite">
          <div className={styles.startupNoticeText}>
            <strong>Update available</strong>
            <span>ASTER Drum Lab v{startupNoticeVersion}</span>
          </div>
          <button type="button" className={styles.startupUpdateButton} onClick={openBoothLibrary}>
            BOOTHから取得
          </button>
          <button
            type="button"
            className={styles.startupCloseButton}
            aria-label="更新通知を閉じる"
            onClick={() => setStartupNoticeVersion(null)}
          >
            ×
          </button>
        </div>
      )}

      {open && (
        <>
          <button type="button" className={styles.scrim} aria-label="close" onClick={onClose} />
          <div ref={panelRef} className={styles.panel} style={style} role="menu">
        {pane === 'main' && (
          <>
            <div className={styles.title}>SETTINGS</div>
            <button className={styles.item} onClick={() => setPane('about')}>
              <span>About / Version</span>
              <span className={styles.chevron}>▸</span>
            </button>
            <button
              className={styles.item}
              onClick={checkForUpdates}
              disabled={updateState.status === 'checking'}
            >
              <span>{updateState.status === 'checking' ? 'Checking…' : 'Check for Updates…'}</span>
            </button>
            {updateState.status === 'latest' && (
              <div className={styles.statusLine}>v{PLUGIN_VERSION} は最新です。</div>
            )}
            {updateState.status === 'available' && (
              <div className={styles.updateNotice} role="status">
                <span>v{updateState.version} があります。</span>
                <button type="button" className={styles.updateButton} onClick={openBoothLibrary}>
                  BOOTHから取得
                </button>
              </div>
            )}
            {updateState.status === 'error' && (
              <div className={`${styles.statusLine} ${styles.statusError}`}>
                更新を確認できませんでした。
              </div>
            )}
            <span className={styles.divider} />
            <button className={styles.item} onClick={() => setPane('prefs')}>
              <span>Preferences</span>
              <span className={styles.chevron}>▸</span>
            </button>
          </>
        )}

        {pane === 'about' && (
          <>
            <button className={styles.back} onClick={() => setPane('main')}>
              <span className={styles.chevronBack}>◂</span> SETTINGS
            </button>
            <div className={styles.title}>ABOUT</div>
            <div className={styles.aboutBlock}>
              <div className={styles.aboutName}>{PLUGIN_NAME}</div>
              <div className={styles.aboutLine}>Version <b>{PLUGIN_VERSION}</b></div>
              <div className={styles.aboutLine}>Format <b>{pluginFormat ?? 'Web'}</b></div>
              <div className={styles.aboutLine}>by <b>{COMPANY_NAME}</b></div>
            </div>
          </>
        )}

        {pane === 'prefs' && (
          <>
            <button className={styles.back} onClick={() => setPane('main')}>
              <span className={styles.chevronBack}>◂</span> SETTINGS
            </button>
            <div className={styles.title}>PREFERENCES</div>
            <PrefRow label="Smart Trim on Sample Load"   prefKey="smartTrimOnSampleLoad" defaultOn={true} />
            <PrefRow label="Auto Fade on Trim Edit"      prefKey="autoFadeOnTrim"        defaultOn={true} />
            <PrefRow label="Preview on Pad Click"        prefKey="previewOnPadClick"     defaultOn={true} />
            <PrefRow label="Preserve Pad Name on Load"   prefKey="preservePadNameOnSampleLoad" defaultOn={true} />
            <PrefRow label="Output Name follows Pad"     prefKey="outputNameFollowsPadName"   defaultOn={true} />
            <div className={styles.hint}>※ 設定はプラグイン全体に適用</div>
          </>
        )}
          </div>
        </>
      )}
    </>,
    document.body,
  );
}

interface PrefRowProps {
  label: string;
  prefKey: string;
  defaultOn: boolean;
}

/** ローカル state + localStorage で保持。Standalone/プラグイン側にも sendToJuce で通知。 */
function PrefRow({ label, prefKey, defaultOn }: PrefRowProps) {
  const storageKey = `ASTER_PREF_${prefKey}`;
  const [on, setOn] = useState<boolean>(() => {
    try {
      const raw = localStorage.getItem(storageKey);
      if (raw === null) return defaultOn;
      return raw === '1';
    } catch { return defaultOn; }
  });
  const toggle = () => {
    const next = !on;
    setOn(next);
    try { localStorage.setItem(storageKey, next ? '1' : '0'); } catch { /* noop */ }
    if (isJuceAvailable()) sendToJuce('setPreference', { key: prefKey, value: next });
  };
  return (
    <button className={styles.prefRow} onClick={toggle} role="menuitemcheckbox" aria-checked={on}>
      <span>{label}</span>
      <span className={`${styles.toggle} ${on ? styles.toggleOn : ''}`} aria-hidden>
        <span className={styles.toggleDot} />
      </span>
    </button>
  );
}
