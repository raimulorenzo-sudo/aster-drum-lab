import { memo, useEffect, useMemo, useRef, useState, type DragEvent, type MouseEvent } from 'react';
import styles from './PadCell.module.css';
import type { PadParams } from '../../types';
import { midiNoteName, padDisplayColor } from '../../data/padData';
import { waveformToBars } from '../../utils/waveform';
import { registerPadFlashTarget, triggerPadFlash } from '../../utils/padFlashRegistry';

interface PadCellProps {
  index: number;
  pad: PadParams;
  selected: boolean;
  onClick: () => void;
  onContextMenu?: (index: number, x: number, y: number) => void;
  onChange?: (patch: Partial<PadParams>) => void;
  onSampleDrop?: (index: number, file: File) => void;
  onPadSwap?: (sourceIndex: number, targetIndex: number) => void;
}

const SUPPORTED_AUDIO_EXTENSIONS = new Set(['wav', 'aif', 'aiff', 'mp3', 'flac']);
const INTERNAL_PAD_DND_TYPE = 'application/x-aster-pad-index';

function isSupportedAudioFile(file: File): boolean {
  const ext = file.name.split('.').pop()?.toLowerCase() ?? '';
  return SUPPORTED_AUDIO_EXTENSIONS.has(ext);
}

function PadCellComponent({ index, pad, selected, onClick, onContextMenu, onChange, onSampleDrop, onPadSwap }: PadCellProps) {
  const [dragOver, setDragOver] = useState(false);
  const flashOverlayRef = useRef<HTMLSpanElement | null>(null);
  const waveformBars = useMemo(
    () => (pad.sampleFileName && !pad.sampleMissing && pad.waveformPeaks?.length
      ? waveformToBars(pad.waveformPeaks, 32)
      : []),
    [pad.sampleFileName, pad.sampleMissing, pad.waveformPeaks],
  );

  useEffect(() =>
    registerPadFlashTarget(index, flashOverlayRef.current),
    [index]);

  const handleMouseDown = () => {
    triggerPadFlash(index);
    onClick();
  };

  const handleMute = (event: MouseEvent<HTMLButtonElement>) => {
    event.stopPropagation();
    onChange?.({ mute: !pad.mute });
  };

  const handleSolo = (event: MouseEvent<HTMLButtonElement>) => {
    event.stopPropagation();
    const turningOn = !pad.solo;
    // Solo 優先: Solo を ON にすると同時に Mute を解除
    onChange?.(turningOn ? { solo: true, mute: false } : { solo: false });
  };

  const cls = [
    styles.cell,
    selected ? styles.selected : '',
    pad.sampleFileName ? styles.hasSample : '',
    pad.sampleMissing ? styles.missingSample : '',
    dragOver ? styles.dragOver : '',
  ].join(' ');

  const color = padDisplayColor(pad);

  return (
    <div
      className={cls}
      draggable
      onMouseDown={handleMouseDown}
      onDragStart={(event: DragEvent<HTMLDivElement>) => {
        console.info('[ASTER PAD SWAP] drag start', { sourceIndex: index });
        event.dataTransfer.effectAllowed = 'move';
        event.dataTransfer.setData(INTERNAL_PAD_DND_TYPE, String(index));
        event.dataTransfer.setData('text/plain', `aster-pad:${index}`);
      }}
      onDragEnter={(event: DragEvent<HTMLDivElement>) => {
        const isInternalPad = Array.from(event.dataTransfer.types).includes(INTERNAL_PAD_DND_TYPE);
        console.info(isInternalPad ? '[ASTER PAD SWAP] dragover fired' : '[ASTER DND] dragover fired', {
          padIndex: index,
          phase: 'enter',
        });
        event.preventDefault();
        event.stopPropagation();
        setDragOver(true);
      }}
      onDragOver={(event: DragEvent<HTMLDivElement>) => {
        const isInternalPad = Array.from(event.dataTransfer.types).includes(INTERNAL_PAD_DND_TYPE);
        console.info(isInternalPad ? '[ASTER PAD SWAP] dragover fired' : '[ASTER DND] dragover fired', { padIndex: index });
        event.preventDefault();
        event.stopPropagation();
        event.dataTransfer.dropEffect = isInternalPad ? 'move' : 'copy';
      }}
      onDragLeave={(event: DragEvent<HTMLDivElement>) => {
        event.preventDefault();
        event.stopPropagation();
        setDragOver(false);
      }}
      onDrop={(event: DragEvent<HTMLDivElement>) => {
        const sourcePadRaw = event.dataTransfer.getData(INTERNAL_PAD_DND_TYPE);
        console.info(sourcePadRaw ? '[ASTER PAD SWAP] drop fired' : '[ASTER DND] drop fired', { padIndex: index });
        event.preventDefault();
        event.stopPropagation();
        setDragOver(false);

        if (sourcePadRaw) {
          const sourceIndex = Number(sourcePadRaw);
          console.info('[ASTER PAD SWAP] target pad index', index);
          console.info('[ASTER PAD SWAP] bridge call start', { sourceIndex, targetIndex: index });

          if (!Number.isInteger(sourceIndex) || sourceIndex < 0 || sourceIndex >= 48) {
            console.error('[ASTER PAD SWAP] bridge call error: invalid source pad index', { sourcePadRaw });
            return;
          }

          if (sourceIndex === index) {
            console.info('[ASTER PAD SWAP] bridge call success: same pad no-op', { sourceIndex, targetIndex: index });
            return;
          }

          onPadSwap?.(sourceIndex, index);
          return;
        }

        const files = Array.from(event.dataTransfer.files ?? []);
        const items = Array.from(event.dataTransfer.items ?? []);
        console.info('[ASTER DND] files count', files.length);
        console.info('[ASTER DND] items', items.map(item => ({ kind: item.kind, type: item.type })));

        const file = files.find(isSupportedAudioFile);
        if (!file) {
          console.warn('[ASTER DND] bridge call error: no supported audio file', {
            padIndex: index,
            files: files.map(f => f.name),
          });
          return;
        }

        console.info('[ASTER DND] dropped file name', file.name);
        console.info('[ASTER DND] dropped file path or available file info', {
          path: (file as File & { path?: string }).path,
          webkitRelativePath: file.webkitRelativePath,
          type: file.type,
          size: file.size,
          targetPadIndex: index,
        });

        onSampleDrop?.(index, file);
      }}
      onContextMenu={(event) => {
        event.preventDefault();
        onClick();
        onContextMenu?.(index, event.clientX, event.clientY);
      }}
    >
      {/* Flash overlay (Web Animations API で opacity を駆動 — compositor 動作のみ) */}
      <span
        ref={flashOverlayRef}
        className={styles.flashOverlay}
        aria-hidden
      />

      {/* カテゴリーカラーの縦バー (左端) — 単一の縦列を Layer 数で内部分割する。
         1 Layer = 1 セグメント (従来見た目とほぼ同じ)、2+ Layer = N 等分。
         テキスト "2L" バッジより視覚ノイズが少なく、ASTER のストリップ言語に馴染む。 */}
      {(() => {
        const layerCount = Math.max(1, Math.min(8, pad.layers?.length ?? 1));
        return (
          <span
            className={styles.colorStripe}
            style={{ boxShadow: `0 0 4px ${color}66` }}
            title={layerCount > 1 ? `${layerCount} Layers` : undefined}
            aria-label={layerCount > 1 ? `${layerCount} layers` : undefined}
            aria-hidden={layerCount === 1}
          >
            {Array.from({ length: layerCount }, (_, i) => (
              <span
                key={i}
                className={styles.colorStripeSegment}
                style={{ background: color }}
              />
            ))}
          </span>
        );
      })()}

      {/* 番号 (左上) */}
      <span className={styles.num}>{String(index + 1).padStart(2, '0')}</span>

      {/* Pad 名 */}
      <div className={styles.name} title={pad.padName}>{pad.padName}</div>

      {/* Sample File Name (補助、薄く小さく) */}
      <div className={styles.sampleName} title={pad.sampleFilePath || pad.sampleFileName || '— empty —'}>
        {pad.sampleMissing ? 'Missing Sample' : pad.sampleFileName || '— empty —'}
      </div>

      {/* 小さな波形プレビュー */}
      <div className={styles.preview} aria-hidden>
        {waveformBars.map((height, i) => (
          <span key={i} style={{ height: `${height}%` }} />
        ))}
      </div>

      <div className={styles.bottomRow}>
        <span className={styles.midi}>{midiNoteName(pad.midiNote)}</span>
        <div className={styles.padButtons}>
          <button
            type="button"
            className={`${styles.msButton} ${pad.mute ? styles.msOnMute : ''}`}
            onMouseDown={(event) => event.stopPropagation()}
            onClick={handleMute}
            aria-pressed={pad.mute}
            aria-label="Mute"
          >
            M
          </button>
          <button
            type="button"
            className={`${styles.msButton} ${pad.solo ? styles.msOnSolo : ''}`}
            onMouseDown={(event) => event.stopPropagation()}
            onClick={handleSolo}
            aria-pressed={pad.solo}
            aria-label="Solo"
          >
            S
          </button>
        </div>
      </div>
    </div>
  );
}

export const PadCell = memo(PadCellComponent, (prev, next) =>
  prev.index === next.index &&
  prev.pad === next.pad &&
  prev.selected === next.selected &&
  prev.onSampleDrop === next.onSampleDrop &&
  prev.onPadSwap === next.onPadSwap
);
