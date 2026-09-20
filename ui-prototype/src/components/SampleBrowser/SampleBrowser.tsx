import { useCallback, useEffect, useMemo, useRef, useState, type ChangeEvent, type KeyboardEvent } from 'react';
import { Folder, FolderOpen, FileAudio, Play, Stop, MagnifyingGlass, ArrowUp, ArrowClockwise, SpeakerHigh } from '@phosphor-icons/react';
import type { PadParams, SampleStockItem } from '../../types';
import { ensureLayers } from '../../utils/layerView';
import { isJuceAvailable, onJuceEvent, sendToJuce } from '../../utils/juceBridge';
import { positionToGain } from '../../utils/fader';
import { Dropdown } from '../Dropdown/Dropdown';
import { Knob } from '../Knob/Knob';
import { PadFooterBar } from '../PadsView/PadsView';
import styles from './SampleBrowser.module.css';

interface Entry { name: string; path: string; directory: boolean; file?: File }
interface Directory { path: string; name: string; parent: string; entries: Entry[]; truncated?: boolean }
interface Preferences { recentPaths?: string[]; gain?: number; autoAudition?: boolean }
interface Props {
  active: boolean;
  pads: PadParams[];
  selectedIndex: number;
  onSelectPad: (index: number) => void;
  onSelectLayer: (index: number) => void;
  onSelectVariation: (index: number) => void;
  onImportFile: (file: File, replace: boolean) => Promise<void>;
  masterKnob: number;
  onMasterKnobChange: (value: number) => void;
  masterClipHit: boolean;
  onResetMasterClip: () => void;
}
let requestSequence = 1;
const nextRequest = () => requestSequence++;
const EMPTY: Directory = { path: '', name: '', parent: '', entries: [] };
const supported = (name: string) => /\.(wav|aif|aiff)$/i.test(name);
const basename = (path: string) => path.replace(/\\/g, '/').split('/').filter(Boolean).pop() || path;

export function sampleVariations(layer: ReturnType<typeof ensureLayers>[number]): SampleStockItem[] {
  return layer.sampleStock?.length ? layer.sampleStock : (layer.sampleFileName || layer.sampleFilePath) ? [{
    sampleFileName: layer.sampleFileName, sampleFilePath: layer.sampleFilePath,
    sampleMissing: layer.sampleMissing, sampleLengthMs: layer.sampleLengthMs,
    startMs: layer.startMs, endMs: layer.endMs, fadeInMs: layer.fadeInMs, fadeOutMs: layer.fadeOutMs,
    waveformPeaks: layer.waveformPeaks, waveformChannels: layer.waveformChannels,
  }] : [];
}

export function SampleBrowser(props: Props) {
  const { active, pads, selectedIndex, onSelectPad, onSelectLayer, onSelectVariation, onImportFile } = props;
  const native = isJuceAvailable();
  const [directory, setDirectory] = useState<Directory>(EMPTY);
  const directoryRef = useRef(directory);
  directoryRef.current = directory;
  const [recent, setRecent] = useState<string[]>([]);
  const [query, setQuery] = useState('');
  const [selectedPath, setSelectedPath] = useState('');
  const [gain, setGain] = useState(0.5);
  const [autoAudition, setAutoAudition] = useState(true);
  const [loading, setLoading] = useState(false);
  const [importing, setImporting] = useState(false);
  const [playing, setPlaying] = useState(false);
  const [auditionLoading, setAuditionLoading] = useState(false);
  const [error, setError] = useState('');
  const [notice, setNotice] = useState('');
  const chooser = useRef<HTMLInputElement>(null);
  const directoryRequest = useRef(0), previewRequest = useRef(0), importRequest = useRef(0);
  const activeRef = useRef(active); activeRef.current = active;
  const previewTimer = useRef<ReturnType<typeof setTimeout>>();
  const prefsTimer = useRef<ReturnType<typeof setTimeout>>();
  const prefsReady = useRef(false);
  const localFolders = useRef(new Map<string, File[]>());
  const audio = useRef<AudioContext>();
  const source = useRef<AudioBufferSourceNode>();
  const gainNode = useRef<GainNode>();
  const list = useRef<HTMLDivElement>(null);
  const layers = ensureLayers(pads[selectedIndex]);
  const layerIndex = Math.min(Math.max(0, pads[selectedIndex].selectedLayerIndex ?? 0), layers.length - 1);
  const layer = layers[layerIndex];
  const variations = sampleVariations(layer);
  const activeVariation = Math.min(Math.max(0, layer.activeSampleStockIndex ?? 0), Math.max(0, variations.length - 1));
  const entries = useMemo(() => directory.entries.filter(entry => entry.name.toLocaleLowerCase().includes(query.toLocaleLowerCase())), [directory.entries, query]);
  const selected = entries.find(entry => entry.path === selectedPath && !entry.directory);
  const disabled = importing || loading;

  const stop = useCallback(() => {
    previewRequest.current = nextRequest();
    clearTimeout(previewTimer.current);
    if (source.current) { source.current.onended = null; try { source.current.stop(); } catch { /* already stopped */ } source.current.disconnect(); source.current = undefined; }
    sendToJuce('browserStop', { requestId: previewRequest.current });
    setPlaying(false); setAuditionLoading(false);
  }, []);

  const showLocalFolder = useCallback((path: string) => {
    const root = [...localFolders.current.keys()].find(key => path === key || path.startsWith(`${key}/`));
    if (!root) { setError('Select the folder again to grant access.'); setLoading(false); return; }
    const rows = new Map<string, Entry>();
    for (const file of localFolders.current.get(root) ?? []) {
      const relative = file.webkitRelativePath || `${root}/${file.name}`;
      if (!relative.startsWith(`${path}/`)) continue;
      const tail = relative.slice(path.length + 1);
      const [name, child] = tail.split('/');
      const rowPath = `${path}/${name}`;
      if (child) rows.set(rowPath, { name, path: rowPath, directory: true });
      else if (supported(name)) rows.set(rowPath, { name, path: rowPath, directory: false, file });
    }
    setDirectory({ path, name: basename(path), parent: path === root ? '' : path.slice(0, path.lastIndexOf('/')), entries: [...rows.values()].sort((a, b) => Number(b.directory) - Number(a.directory) || a.name.localeCompare(b.name, undefined, { numeric: true })) });
    setRecent(prev => [path, ...prev.filter(p => p !== path)].slice(0, 8));
    setLoading(false);
  }, []);

  const openDirectory = useCallback((path: string) => {
    stop(); setQuery(''); setSelectedPath(''); setError(''); setNotice(''); setLoading(true);
    directoryRequest.current = nextRequest();
    if (native) sendToJuce('browserOpenDirectory', { path, requestId: directoryRequest.current });
    else showLocalFolder(path);
  }, [native, showLocalFolder, stop]);

  useEffect(() => {
    if (!active) { stop(); setImporting(false); setLoading(false); return; }
    if (native) {
      // Request ids distinguish a closed/reopened view from older worker results.
      prefsReady.current = false;
      sendToJuce('browserInit', {});
    } else {
      try {
        const stored = JSON.parse(localStorage.getItem('aster-browser-preferences') || '{}') as Preferences;
        if (typeof stored.gain === 'number') setGain(Math.max(0, Math.min(1, stored.gain)));
        if (typeof stored.autoAudition === 'boolean') setAutoAudition(stored.autoAudition);
      } catch { /* default preferences */ }
      prefsReady.current = true;
    }
  }, [active, native, stop]);

  useEffect(() => {
    const unsubscribers = [
      onJuceEvent('browserPreferences', raw => {
        const value = raw as Preferences;
        setRecent(value.recentPaths ?? []);
        if (!prefsReady.current && activeRef.current) {
          prefsReady.current = true;
          if (typeof value.gain === 'number' && Number.isFinite(value.gain)) setGain(Math.max(0, Math.min(1, value.gain)));
          if (typeof value.autoAudition === 'boolean') setAutoAudition(value.autoAudition);
          const path = directoryRef.current.path || value.recentPaths?.[0];
          if (path) openDirectory(path);
        }
      }),
      onJuceEvent('browserDirectory', raw => {
        const value = raw as Directory & { requestId: number; error?: string };
        if (!activeRef.current || value.requestId !== directoryRequest.current) return;
        setLoading(false);
        if (value.error) { setError(value.error); return; }
        setDirectory(value); setError('');
      }),
      onJuceEvent('browserResult', raw => {
        const value = raw as { kind: string; requestId: number; error?: string };
        if (!activeRef.current) return;
        if (value.kind === 'import' && value.requestId === importRequest.current) {
          setImporting(false);
          if (value.error) setError(value.error);
          else { stop(); setNotice('Sample loaded into the selected layer.'); }
        } else if (value.requestId === previewRequest.current && ['preview', 'ended', 'stopped'].includes(value.kind)) {
          setAuditionLoading(false); setPlaying(value.kind === 'preview' && !value.error);
          if (value.error) setError(value.error);
        } else if (value.requestId === directoryRequest.current && ['folderCancelled', 'error'].includes(value.kind)) {
          setLoading(false); if (value.error) setError(value.error);
        }
      }),
    ];
    return () => unsubscribers.forEach(unsubscribe => unsubscribe());
  }, [openDirectory, stop]);

  useEffect(() => {
    if (!prefsReady.current) return;
    if (gainNode.current && audio.current) gainNode.current.gain.setTargetAtTime(gain * positionToGain(props.masterKnob), audio.current.currentTime, 0.01);
    clearTimeout(prefsTimer.current);
    prefsTimer.current = setTimeout(() => {
      if (native) sendToJuce('browserSettings', { gain, autoAudition });
      else { try { localStorage.setItem('aster-browser-preferences', JSON.stringify({ gain, autoAudition })); } catch { /* storage unavailable */ } }
    }, 120);
    return () => clearTimeout(prefsTimer.current);
  }, [gain, autoAudition, native, props.masterKnob]);
  useEffect(() => () => { stop(); void audio.current?.close(); }, [stop]);
  useEffect(() => { if (!autoAudition) stop(); }, [autoAudition, stop]);
  useEffect(() => { stop(); setNotice(''); setError(''); }, [selectedIndex, layerIndex, stop]);
  useEffect(() => { list.current?.querySelector('[aria-selected="true"]')?.scrollIntoView({ block: 'nearest' }); }, [selectedPath]);

  const audition = useCallback((entry: Entry, delay = 0) => {
    stop(); setError(''); setNotice(''); setAuditionLoading(true);
    const request = nextRequest(); previewRequest.current = request;
    previewTimer.current = setTimeout(async () => {
      if (native) { sendToJuce('browserPreview', { path: entry.path, requestId: request }); return; }
      try {
        if (!entry.file) throw new Error('Select the folder again.');
        if (entry.file.size > 128 * 1024 * 1024) throw new Error('File too large for browser audition.');
        audio.current ??= new AudioContext();
        await audio.current.resume();
        const buffer = await audio.current.decodeAudioData(await entry.file.arrayBuffer());
        if (previewRequest.current !== request || !activeRef.current) return;
        const nextSource = audio.current.createBufferSource();
        gainNode.current?.disconnect();
        gainNode.current = audio.current.createGain();
        gainNode.current.gain.value = gain * positionToGain(props.masterKnob);
        nextSource.buffer = buffer; nextSource.connect(gainNode.current); gainNode.current.connect(audio.current.destination);
        nextSource.onended = () => { nextSource.disconnect(); if (previewRequest.current === request) setPlaying(false); };
        source.current = nextSource; nextSource.start(); setPlaying(true); setAuditionLoading(false);
      } catch (err) {
        if (previewRequest.current === request) { setError(err instanceof Error ? err.message : 'This file could not be auditioned.'); setAuditionLoading(false); }
      }
    }, delay);
  }, [gain, native, props.masterKnob, stop]);

  const selectEntry = (entry: Entry, keyboard = false) => {
    if (entry.directory) { if (!keyboard) openDirectory(entry.path); else { stop(); setSelectedPath(entry.path); } return; }
    setSelectedPath(entry.path); setNotice('');
    if (autoAudition) audition(entry, keyboard ? 90 : 0); else stop();
  };
  const onListKey = (event: KeyboardEvent<HTMLDivElement>) => {
    if (disabled) return;
    if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
      event.preventDefault();
      const current = entries.findIndex(entry => entry.path === selectedPath);
      const index = Math.max(0, Math.min(entries.length - 1, current + (event.key === 'ArrowDown' ? 1 : -1)));
      if (entries[index]) selectEntry(entries[index], true);
    } else if (event.key === 'Enter' || event.key === ' ') {
      event.preventDefault();
      const entry = entries.find(item => item.path === selectedPath);
      if (entry?.directory) openDirectory(entry.path); else if (entry) audition(entry);
    } else if (event.key === 'Escape') { event.preventDefault(); stop(); }
  };
  const chooseFolder = () => {
    stop(); setError(''); setNotice(''); directoryRequest.current = nextRequest();
    if (native) { setLoading(true); sendToJuce('browserChooseFolder', { requestId: directoryRequest.current }); }
    else chooser.current?.click();
  };
  const pickedFolder = (event: ChangeEvent<HTMLInputElement>) => {
    const files = Array.from(event.currentTarget.files ?? []); event.currentTarget.value = '';
    if (!files.length) return;
    const root = files[0].webkitRelativePath.split('/')[0];
    localFolders.current.set(root, files); openDirectory(root);
  };
  const importSample = async (replace: boolean) => {
    if (!selected || disabled || (replace ? variations.length === 0 : variations.length >= 5)) return;
    setImporting(true); setError(''); setNotice(''); stop();
    importRequest.current = nextRequest();
    if (native) sendToJuce('browserImport', { requestId: importRequest.current, path: selected.path, index: selectedIndex, layerIndex, replace, expectedCount: variations.length, expectedActive: activeVariation });
    else {
      try { if (!selected.file) throw new Error('Select the folder again.'); await onImportFile(selected.file, replace); setNotice('Sample loaded into the selected layer.'); }
      catch (err) { setError(err instanceof Error ? err.message : 'Sample could not be loaded.'); }
      finally { setImporting(false); }
    }
  };

  return <div className={styles.view} hidden={!active}>
    <div className={styles.columns}>
      <aside className={styles.folders}>
        <h2>RECENT FOLDERS</h2>
        <button className={styles.selectFolder} disabled={disabled} onClick={chooseFolder}><FolderOpen size={20} weight="fill" />SELECT FOLDER</button>
        <div className={styles.recentList}>{recent.map(path => <button key={path} title={path} disabled={disabled} className={path === directory.path ? styles.selectedFolder : ''} onClick={() => openDirectory(path)}><Folder size={19} weight="fill" /><span>{basename(path)}</span></button>)}</div>
        {!recent.length && <p className={styles.hint}>Your sample folders will appear here.</p>}
        <p className={styles.folderHint}>WAV / AIFF · Local folders</p>
      </aside>
      <section className={styles.library} aria-label="Sample files">
        <div className={styles.folderHeading}><div><h1>{directory.name ? directory.name.toUpperCase() : 'SAMPLE BROWSER'}</h1><p title={directory.path}>{directory.path || 'Select a folder to explore your samples.'}</p></div><button title="Parent folder" aria-label="Parent folder" disabled={!directory.parent || disabled} onClick={() => openDirectory(directory.parent)}><ArrowUp size={19} /></button><button title="Refresh folder" aria-label="Refresh folder" disabled={!directory.path || disabled} onClick={() => openDirectory(directory.path)}><ArrowClockwise size={19} /></button></div>
        <label className={styles.search}><MagnifyingGlass size={20} /><input aria-label="Search this folder" placeholder="Search this folder…" value={query} disabled={disabled} onChange={event => { setQuery(event.target.value); stop(); setSelectedPath(''); }} /></label>
        <div className={styles.fileList} role="listbox" aria-label="Samples and folders" aria-busy={loading} aria-activedescendant={entries.some(entry => entry.path === selectedPath) ? `browser-file-${entries.findIndex(entry => entry.path === selectedPath)}` : undefined} tabIndex={0} onKeyDown={onListKey} ref={list}>
          {loading ? <p className={styles.empty}>Loading folder…</p> : entries.map((entry, index) => <button id={`browser-file-${index}`} role="option" aria-selected={entry.path === selectedPath} tabIndex={-1} disabled={importing} key={entry.path} title={entry.name} className={entry.path === selectedPath ? styles.selectedFile : ''} onClick={() => { selectEntry(entry); list.current?.focus(); }}>{entry.directory ? <Folder size={21} weight="fill" /> : entry.path === selectedPath && playing ? <SpeakerHigh size={21} weight="fill" className={styles.goldIcon} /> : <FileAudio size={21} />}<span>{entry.name}</span></button>)}
          {!loading && entries.length === 0 && <div className={styles.empty}>{!directory.path ? <><FolderOpen size={36} /><p>Find your next sound.</p><button className={styles.selectFolder} onClick={chooseFolder}>SELECT FOLDER</button></> : query ? 'No matching files in this folder.' : 'No WAV or AIFF files in this folder.'}</div>}
        </div>
        <p className={styles.listHint}>{directory.truncated ? 'Showing the first 10,000 entries. Open a smaller folder to see more.' : 'Click or use ↑ ↓ to audition. Enter opens folders. Esc stops.'}</p>
        <div className={styles.audition}>
          <span>AUDITION</span>
          <button className={styles.play} aria-label="Play selected sample" title="Audition selected sample" disabled={!selected || disabled} onClick={() => selected && audition(selected)}><Play size={21} weight="fill" /></button>
          <button aria-label="Stop audition" title="Stop audition" onClick={stop}><Stop size={19} weight="fill" /></button>
          <span className={styles.auditionName} title={selected?.name}>{auditionLoading ? 'Loading…' : selected?.name || 'Select a sample'}</span>
          <div className={styles.level}><span>LEVEL</span><Knob value={gain} onChange={setGain} size={36} valueText={null} defaultValue={0.5} inputText={`${Math.round(gain * 100)}`} parseInput={text => { const value = Number(text); return Number.isFinite(value) ? value / 100 : null; }} /></div>
          <label className={styles.auto}><input type="checkbox" checked={autoAudition} onChange={event => setAutoAudition(event.target.checked)} />AUTO AUDITION</label>
        </div>
      </section>
      <aside className={styles.destination}>
        <h2>DESTINATION</h2>
        <fieldset disabled={importing} className={styles.targetControls}>
          <Dropdown value={selectedIndex} onChange={index => { if (!importing) { stop(); setNotice(''); onSelectPad(index); } }} width="100%" title="Destination pad" options={pads.map((pad, index) => ({ value: index, label: `PAD ${String(index + 1).padStart(2, '0')} — ${pad.padName.toUpperCase()}` }))} />
          <div className={styles.layerRow}><span>LAYER</span><div className={styles.layers}>{layers.map((_, index) => <button key={index} aria-label={`Destination layer ${index + 1}`} aria-pressed={index === layerIndex} className={index === layerIndex ? styles.activeLayer : ''} onClick={() => { stop(); setNotice(''); onSelectLayer(index); }}>{index + 1}</button>)}</div></div>
        </fieldset>
        <div className={styles.variationHeading}><h2>SAMPLE VARIATIONS</h2><span>{variations.length}/5</span></div>
        <div className={styles.variations}>{variations.map((item, index) => <button key={index} disabled={importing} title={item.sampleFileName} className={index === activeVariation ? styles.activeVariation : ''} onClick={() => { stop(); setNotice(''); onSelectVariation(index); }}><span>{index + 1}</span><span>{item.sampleFileName}{item.sampleMissing ? ' (Missing)' : ''}</span>{index === activeVariation && <small>ACTIVE</small>}</button>)}{!variations.length && <p className={styles.hint}>No samples in this layer yet.</p>}</div>
        {layer.roundRobin && <p className={styles.rr}>ROUND ROBIN ON</p>}
        <div className={styles.actions}>
          <h2>SELECTED FILE</h2><p className={styles.selectedName} title={selected?.name}>{selected?.name || 'No sample selected'}</p>
          <button className={styles.add} disabled={!selected || disabled || variations.length >= 5} onClick={() => void importSample(false)}>{importing ? 'LOADING SAMPLE…' : variations.length >= 5 ? 'VARIATIONS FULL (5/5)' : `ADD AS VARIATION ${variations.length + 1}`}</button>
          <button className={styles.replace} disabled={!selected || disabled || variations.length === 0} onClick={() => void importSample(true)}>REPLACE VARIATION {activeVariation + 1}</button>
          <p className={styles.hint}>Audition does not change your kit.</p>
          <div className={styles.status} role={error ? 'alert' : 'status'} aria-live="polite">{error ? <span className={styles.error}>{error}</span> : notice}</div>
        </div>
      </aside>
    </div>
    {active && <PadFooterBar masterKnob={props.masterKnob} onMasterKnobChange={props.onMasterKnobChange} masterClipHit={props.masterClipHit} onResetMasterClip={props.onResetMasterClip} />}
    <input ref={chooser} type="file" multiple hidden {...{ webkitdirectory: '' }} onChange={pickedFolder} />
  </div>;
}
