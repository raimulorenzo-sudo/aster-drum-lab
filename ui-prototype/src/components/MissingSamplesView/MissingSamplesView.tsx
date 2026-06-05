import { useRef, useState } from 'react';
import styles from './MissingSamplesView.module.css';
import type { PadParams } from '../../types';
import { padDisplayColor } from '../../data/padData';
import { isMissingSamplePad } from '../../utils/missingSamples';

interface RelinkResult {
  restored: number;
  notFound: number;
  details: Array<{ index: number; padName: string; found: boolean }>;
}

interface MissingSamplesViewProps {
  pads: PadParams[];
  useNativeRelink?: boolean;
  onRelinkPad: (index: number) => void;
  onRelinkPadFile: (index: number, fileName: string, filePath: string) => void;
  onRelinkAll: (updates: Array<{ index: number; fileName: string; filePath: string }>) => void;
}

export function MissingSamplesView({
  pads,
  useNativeRelink = false,
  onRelinkPad,
  onRelinkPadFile,
  onRelinkAll,
}: MissingSamplesViewProps) {
  const [result, setResult] = useState<RelinkResult | null>(null);
  const folderInputRef = useRef<HTMLInputElement>(null);
  const fileInputRefs = useRef<Map<number, HTMLInputElement>>(new Map());

  const missingPads = pads
    .map((pad, i) => ({ pad, index: i }))
    .filter(({ pad }) => isMissingSamplePad(pad));

  // ── Individual Relink ───────────────────────────────────────────
  function handleRelinkOne(padIndex: number) {
    if (useNativeRelink) {
      onRelinkPad(padIndex);
      return;
    }

    const input = fileInputRefs.current.get(padIndex);
    if (input) input.click();
  }

  function handleFileSelected(padIndex: number, e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0];
    if (!file) return;
    // In prototype: use webkitRelativePath if available, else just the filename
    const filePath = (file as File & { path?: string }).path
      ?? (file as File & { webkitRelativePath?: string }).webkitRelativePath
      ?? file.name;
    onRelinkPadFile(padIndex, file.name, filePath || file.name);
    setResult(null);
    e.target.value = '';
  }

  // ── Relink All (folder) ─────────────────────────────────────────
  function handleFolderPick() {
    setResult(null);
    folderInputRef.current?.click();
  }

  function handleFolderSelected(e: React.ChangeEvent<HTMLInputElement>) {
    const files = Array.from(e.target.files ?? []);
    if (files.length === 0) return;

    // Build a map: filename (lowercase) → File
    const fileMap = new Map<string, File>();
    for (const f of files) {
      fileMap.set(f.name.toLowerCase(), f);
    }

    const updates: Array<{ index: number; fileName: string; filePath: string }> = [];
    const details: RelinkResult['details'] = [];

    for (const { pad, index } of missingPads) {
      const key = pad.sampleFileName.toLowerCase();
      const found = fileMap.get(key);
      if (found) {
        const fp = (found as File & { path?: string }).path
          ?? (found as File & { webkitRelativePath?: string }).webkitRelativePath
          ?? found.name;
        updates.push({ index, fileName: found.name, filePath: fp || found.name });
        details.push({ index, padName: pad.padName, found: true });
      } else {
        details.push({ index, padName: pad.padName, found: false });
      }
    }

    if (updates.length > 0) onRelinkAll(updates);

    setResult({
      restored: updates.length,
      notFound: missingPads.length - updates.length,
      details,
    });

    e.target.value = '';
  }

  return (
    <div className={styles.root}>
      {/* ── ヘッダー ─────────────────────────────────────────────── */}
      <div className={styles.header}>
        <div className={styles.titleBlock}>
          <span className={styles.titleIcon} aria-hidden>
            <svg width="16" height="16" viewBox="0 0 16 16" fill="none">
              <circle cx="8" cy="8" r="6.5" stroke="currentColor" strokeWidth="1.2" />
              <line x1="8" y1="4.5" x2="8" y2="8.5" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" />
              <circle cx="8" cy="10.5" r="0.8" fill="currentColor" />
            </svg>
          </span>
          <span className={styles.titleText}>MISSING SAMPLES</span>
          <span className={styles.countChip}>
            {missingPads.length} missing
          </span>
        </div>

        <div className={styles.actions}>
          {/* hidden folder input */}
          <input
            ref={folderInputRef}
            type="file"
            /* @ts-expect-error webkitdirectory not in HTMLInputElement types */
            webkitdirectory=""
            multiple
            style={{ display: 'none' }}
            onChange={handleFolderSelected}
          />
          <button
            className={`${styles.btn} ${styles.btnPrimary}`}
            onClick={handleFolderPick}
            disabled={missingPads.length === 0}
            title="フォルダを選択してMissing Sampleをまとめて検索"
          >
            <svg width="13" height="13" viewBox="0 0 13 13" aria-hidden>
              <path d="M1 3.5 A1 1 0 0 1 2 2.5 H5L6 4H11A1 1 0 0 1 12 5V10A1 1 0 0 1 11 11H2A1 1 0 0 1 1 10Z"
                stroke="currentColor" strokeWidth="1" fill="none" />
            </svg>
            RELINK ALL
          </button>
        </div>
      </div>

      {/* ── 結果バナー ────────────────────────────────────────────── */}
      {result && (
        <div className={`${styles.resultBanner} ${result.notFound === 0 ? styles.resultSuccess : styles.resultPartial}`}>
          <span className={styles.resultIcon} aria-hidden>
            {result.notFound === 0
              ? <svg width="14" height="14" viewBox="0 0 14 14"><path d="M2 7L5.5 10.5L12 3" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" fill="none" /></svg>
              : <svg width="14" height="14" viewBox="0 0 14 14"><path d="M2 7L5.5 10.5L12 3" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" fill="none" /></svg>
            }
          </span>
          <span>
            <strong>{result.restored}</strong> sample{result.restored !== 1 ? 's' : ''} restored
            {result.notFound > 0 && (
              <span className={styles.notFoundNote}> · <strong>{result.notFound}</strong> not found</span>
            )}
          </span>
          <button className={styles.dismissBtn} onClick={() => setResult(null)} aria-label="dismiss">✕</button>
        </div>
      )}

      {/* ── テーブル ─────────────────────────────────────────────── */}
      {missingPads.length === 0 ? (
        <div className={styles.empty}>
          <svg width="32" height="32" viewBox="0 0 32 32" aria-hidden>
            <circle cx="16" cy="16" r="13" stroke="currentColor" strokeWidth="1.5" fill="none" opacity="0.4" />
            <path d="M10 16 L14.5 20.5 L22 11" stroke="currentColor" strokeWidth="2" strokeLinecap="round" fill="none" opacity="0.6" />
          </svg>
          <p>All samples are linked correctly.</p>
        </div>
      ) : (
        <div className={styles.tableWrap}>
          <table className={styles.table}>
            <thead>
              <tr>
                <th className={styles.thNum}>#</th>
                <th className={styles.thName}>PAD NAME</th>
                <th className={styles.thFile}>SAMPLE FILE</th>
                <th className={styles.thPath}>ORIGINAL PATH</th>
                <th className={styles.thAction} />
              </tr>
            </thead>
            <tbody>
              {missingPads.map(({ pad, index }) => {
                const color = padDisplayColor(pad);
                return (
                  <tr key={index} className={styles.row}>
                    <td className={styles.tdNum}>
                      <span className={styles.colorDot} style={{ background: color }} />
                      {String(index + 1).padStart(2, '0')}
                    </td>
                    <td className={styles.tdName}>{pad.padName}</td>
                    <td className={styles.tdFile}>{pad.sampleFileName || '—'}</td>
                    <td className={styles.tdPath} title={pad.sampleFilePath}>
                      {pad.sampleFilePath || '—'}
                    </td>
                    <td className={styles.tdAction}>
                      {/* hidden file input per row */}
                      <input
                        ref={el => {
                          if (el) fileInputRefs.current.set(index, el);
                          else fileInputRefs.current.delete(index);
                        }}
                        type="file"
                        accept="audio/*"
                        style={{ display: 'none' }}
                        onChange={(e) => handleFileSelected(index, e)}
                      />
                      <button
                        className={`${styles.btn} ${styles.btnRelink}`}
                        onClick={() => handleRelinkOne(index)}
                        title={`Relink ${pad.padName}`}
                      >
                        RELINK
                      </button>
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      )}

      {/* ── 注記 ─────────────────────────────────────────────────── */}
      <div className={styles.footer}>
        Relink は Sample File Path のみ更新します。Pad Name / Trim / Output / Choke は変更されません。
      </div>
    </div>
  );
}
