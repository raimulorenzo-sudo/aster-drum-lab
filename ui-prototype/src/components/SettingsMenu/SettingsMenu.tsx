import { useEffect, useLayoutEffect, useRef, useState, type CSSProperties } from 'react';
import { createPortal } from 'react-dom';
import styles from './SettingsMenu.module.css';
import { measurePopup, positionPopupFromAnchor } from '../../utils/popupPosition';
import { isJuceAvailable, sendToJuce } from '../../utils/juceBridge';

// Plugin meta (静的)。バージョン値はビルド時に挿し替え可能だが今は手動。
const PLUGIN_VERSION = '0.1.0';
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

export function SettingsMenu({ anchorRef, open, onClose, pluginFormat }: SettingsMenuProps) {
  const [pane, setPane]       = useState<Pane>('main');
  const [style, setStyle]     = useState<CSSProperties>({});
  const [updateMsg, setUpdateMsg] = useState<string | null>(null);
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

  if (!open) return null;

  const checkForUpdates = () => {
    // 実 update server は未実装。stub: 現在バージョンを表示するだけ。
    if (isJuceAvailable()) sendToJuce('checkForUpdates', {});
    setUpdateMsg(`現在 v${PLUGIN_VERSION} を使用中。最新です。`);
    window.setTimeout(() => setUpdateMsg(null), 3500);
  };

  return createPortal(
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
            <button className={styles.item} onClick={checkForUpdates}>
              <span>Check for Updates…</span>
            </button>
            {updateMsg && <div className={styles.statusLine}>{updateMsg}</div>}
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
