import { useEffect, useLayoutEffect, useRef, useState, type CSSProperties } from 'react';
import { createPortal } from 'react-dom';
import styles from './SettingsMenu.module.css';
import { measurePopup, positionPopupFromAnchor } from '../../utils/popupPosition';
import { isJuceAvailable, onJuceEvent, sendToJuce } from '../../utils/juceBridge';
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

type Pane = 'main' | 'about' | 'prefs' | 'automation';
interface AutomationSlotState {
  index: number;
  parameterId: string;
  targetName: string;
  assigned: boolean;
  learning: boolean;
}

const EMPTY_AUTOMATION_SLOTS: AutomationSlotState[] = Array.from({ length: 24 }, (_, index) => ({
  index,
  parameterId: '',
  targetName: '',
  assigned: false,
  learning: false,
}));
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
  const [automationSlots, setAutomationSlots] = useState<AutomationSlotState[]>(EMPTY_AUTOMATION_SLOTS);
  const panelRef = useRef<HTMLDivElement>(null);

  // ── ポジショニング ──
  useLayoutEffect(() => {
    if (!open) return;
    const update = () => {
      const el = anchorRef.current;
      if (!el) return;
      const rect = el.getBoundingClientRect();
      const automationPane = pane === 'automation';
      const width = automationPane ? 390 : 240;
      const measuredSize = measurePopup(panelRef.current, { width, height: 280 });
      // 直前のmain paneのmax-heightを引き継ぐと24行paneが極端に低くなるため、
      // automationだけは希望高を明示し、position helper側で画面内へ収める。
      const size = automationPane
        ? { width, height: 520 }
        : measuredSize;
      setStyle(positionPopupFromAnchor(rect, size, {
        align: 'start',
        width,
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

  useEffect(() => {
    const unsubscribe = onJuceEvent('automationSlots', raw => {
      const payload = raw as { slots?: AutomationSlotState[] };
      if (!Array.isArray(payload?.slots)) return;
      setAutomationSlots(EMPTY_AUTOMATION_SLOTS.map((fallback, index) => {
        const slot = payload.slots?.find(item => item.index === index);
        return slot ? {
          index,
          parameterId: typeof slot.parameterId === 'string' ? slot.parameterId : '',
          targetName: typeof slot.targetName === 'string' ? slot.targetName : '',
          assigned: Boolean(slot.assigned),
          learning: Boolean(slot.learning),
        } : fallback;
      }));
    });
    return unsubscribe;
  }, []);

  useEffect(() => {
    if (open && pane === 'automation')
      sendToJuce('requestAutomationSlots', {});
  }, [open, pane]);

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

  const openOfficialDownload = () => {
    if (isJuceAvailable()) {
      sendToJuce('openOfficialDownload', {});
      return;
    }
    window.open('https://aster.enigmajp.com/en/download/', '_blank', 'noopener,noreferrer');
  };

  return createPortal(
    <>
      {startupNoticeVersion && (
        <div className={styles.startupNotice} role="status" aria-live="polite">
          <div className={styles.startupNoticeText}>
            <strong>Update available</strong>
            <span>ASTER Drum Lab v{startupNoticeVersion}</span>
            <span>Use your purchase email to get the latest version.</span>
          </div>
          <button type="button" className={styles.startupUpdateButton} onClick={openOfficialDownload}>
            Re-download from official site
          </button>
          <button
            type="button"
            className={styles.startupCloseButton}
            aria-label="Close update notification"
            onClick={() => setStartupNoticeVersion(null)}
          >
            ×
          </button>
        </div>
      )}

      {open && (
        <>
          <button
            type="button"
            className={`${styles.scrim} ${pane === 'automation' ? styles.scrimPassThrough : ''}`}
            aria-label="close"
            onClick={onClose}
          />
          <div ref={panelRef} className={styles.panel} style={style} role="menu">
        {pane === 'main' && (
          <>
            <div className={styles.title}>SETTINGS</div>
            <button className={styles.item} onClick={() => setPane('about')}>
              <span>About / Version</span>
              <span className={styles.chevron}>▸</span>
            </button>
            <button className={styles.item} onClick={() => setPane('automation')}>
              <span>Automation Slots</span>
              <span className={styles.itemMeta}>24 ▸</span>
            </button>
            <button
              className={styles.item}
              onClick={checkForUpdates}
              disabled={updateState.status === 'checking'}
            >
              <span>{updateState.status === 'checking' ? 'Checking…' : 'Check for Updates…'}</span>
            </button>
            {updateState.status === 'latest' && (
              <div className={styles.statusLine}>You’re up to date (v{PLUGIN_VERSION}).</div>
            )}
            {updateState.status === 'available' && (
              <div className={styles.updateNotice} role="status">
                <span>Version {updateState.version} is available.</span>
                <span>Use your purchase email to receive a time-limited download link.</span>
                <button type="button" className={styles.updateButton} onClick={openOfficialDownload}>
                  Re-download from official site
                </button>
              </div>
            )}
            {updateState.status === 'error' && (
              <div className={`${styles.statusLine} ${styles.statusError}`}>
                Unable to check for updates.
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
            <PrefRow label="KEEP LENGTH on Sample Load" prefKey="keepLengthOnSampleLoad" defaultOn={true} syncOnMount />
            <PrefRow label="Smart Trim on Sample Load"   prefKey="smartTrimOnSampleLoad" defaultOn={true} />
            <PrefRow label="Auto Fade on Trim Edit"      prefKey="autoFadeOnTrim"        defaultOn={true} />
            <PrefRow label="Preview on Pad Click"        prefKey="previewOnPadClick"     defaultOn={true} />
            <PrefRow label="Preserve Pad Name on Load"   prefKey="preservePadNameOnSampleLoad" defaultOn={true} />
            <div className={styles.hint}>These settings apply to the entire plug-in.</div>
          </>
        )}

        {pane === 'automation' && (
          <>
            <button className={styles.back} onClick={() => setPane('main')}>
              <span className={styles.chevronBack}>◂</span> SETTINGS
            </button>
            <div className={styles.title}>AUTOMATION SLOTS · 24</div>
            <div className={styles.automationIntro}>
              Assign controls with AUTO mode or by right-clicking them. Use this list to review or clear assignments.
            </div>
            <div className={styles.automationList}>
              {automationSlots.map(slot => (
                <div
                  key={slot.index}
                  className={styles.automationRow}
                >
                  <span className={styles.automationNumber}>
                    AUTO {String(slot.index + 1).padStart(2, '0')}
                  </span>
                  <span
                    className={`${styles.automationTarget} ${!slot.assigned ? styles.automationTargetEmpty : ''}`}
                    title={slot.targetName || 'Unassigned'}
                  >
                    {slot.targetName || 'Unassigned'}
                  </span>
                  <button
                    type="button"
                    className={styles.clearAutomationButton}
                    aria-label={`Clear automation slot ${slot.index + 1}`}
                    disabled={!slot.assigned}
                    onClick={() => {
                      sendToJuce('clearAutomationSlot', { slot: slot.index });
                      setAutomationSlots(prev => prev.map(item => item.index === slot.index
                        ? { ...item, parameterId: '', targetName: '', assigned: false, learning: false }
                        : item));
                    }}
                  >
                    ×
                  </button>
                </div>
              ))}
            </div>
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
  syncOnMount?: boolean;
}

/** ローカル state + localStorage で保持。Standalone/プラグイン側にも sendToJuce で通知。 */
function PrefRow({ label, prefKey, defaultOn, syncOnMount = false }: PrefRowProps) {
  const storageKey = `ASTER_PREF_${prefKey}`;
  const [on, setOn] = useState<boolean>(() => {
    try {
      const raw = localStorage.getItem(storageKey);
      if (raw === null) return defaultOn;
      return raw === '1';
    } catch { return defaultOn; }
  });
  useEffect(() => {
    if (syncOnMount && isJuceAvailable())
      sendToJuce('setPreference', { key: prefKey, value: on });
    // The stored value is intentionally sent once when this preference mounts.
    // Subsequent changes are sent by toggle().
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [prefKey, syncOnMount]);
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
