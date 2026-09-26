import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useState,
  type PropsWithChildren,
} from 'react';
import { createPortal } from 'react-dom';
import styles from './AutomationAssign.module.css';
import { onJuceEvent, sendToJuce } from '../../utils/juceBridge';
import type { AutomationTarget } from '../../utils/automationTarget';

interface AutomationSlotState {
  index: number;
  parameterId: string;
  targetName: string;
  assigned: boolean;
}

interface SelectedTarget {
  id: string;
  name: string;
  x: number;
  y: number;
}

interface AutomationAssignContextValue {
  mode: boolean;
  toggleMode: () => void;
  openTargetMenu: (target: AutomationTarget, x: number, y: number) => void;
  assignedSlotForTarget: (targetId: string) => number | undefined;
}

const AutomationAssignContext = createContext<AutomationAssignContextValue>({
  mode: false,
  toggleMode: () => {},
  openTargetMenu: () => {},
  assignedSlotForTarget: () => undefined,
});

const EMPTY_SLOTS: AutomationSlotState[] = Array.from({ length: 24 }, (_, index) => ({
  index,
  parameterId: '',
  targetName: '',
  assigned: false,
}));

function targetFromEvent(event: Event): HTMLElement | null {
  return event.target instanceof Element
    ? event.target.closest<HTMLElement>('[data-automation-target-id]')
    : null;
}

function clampMenuPosition(x: number, y: number) {
  const width = Math.min(292, window.innerWidth - 16);
  const estimatedHeight = Math.min(430, window.innerHeight - 16);
  return {
    left: Math.max(8, Math.min(x, window.innerWidth - width - 8)),
    top: Math.max(8, Math.min(y, window.innerHeight - estimatedHeight - 8)),
  };
}

export function AutomationAssignProvider({ children }: PropsWithChildren) {
  const [mode, setMode] = useState(false);
  const [slots, setSlots] = useState<AutomationSlotState[]>(EMPTY_SLOTS);
  const [target, setTarget] = useState<SelectedTarget | null>(null);
  const [toast, setToast] = useState<string | null>(null);

  useEffect(() => {
    document.documentElement.dataset.automationAssign = mode ? 'true' : 'false';
    return () => { delete document.documentElement.dataset.automationAssign; };
  }, [mode]);

  useEffect(() => onJuceEvent('automationSlots', raw => {
    const payload = raw as { slots?: AutomationSlotState[] };
    if (!Array.isArray(payload?.slots)) return;
    setSlots(EMPTY_SLOTS.map((fallback, index) => {
      const slot = payload.slots?.find(item => item.index === index);
      return slot ? {
        index,
        parameterId: typeof slot.parameterId === 'string' ? slot.parameterId : '',
        targetName: typeof slot.targetName === 'string' ? slot.targetName : '',
        assigned: Boolean(slot.assigned),
      } : fallback;
    }));
  }), []);

  useEffect(() => {
    if (mode || target) sendToJuce('requestAutomationSlots', {});
  }, [mode, target]);

  useEffect(() => {
    const openForElement = (element: HTMLElement, x: number, y: number) => {
      const id = element.dataset.automationTargetId;
      if (!id) return;
      const name = element.dataset.automationTargetName
        || element.getAttribute('aria-label')
        || id;
      const position = clampMenuPosition(x, y);
      setTarget({ id, name, x: position.left, y: position.top });
    };

    const onPointerDownCapture = (event: PointerEvent) => {
      if (!mode || event.button !== 0) return;
      const element = targetFromEvent(event);
      if (!element) return;
      event.preventDefault();
      event.stopPropagation();
      const rect = element.getBoundingClientRect();
      openForElement(element, rect.right + 8, rect.top);
    };
    const onClickCapture = (event: MouseEvent) => {
      if (!mode) return;
      const element = targetFromEvent(event);
      if (!element) return;
      event.preventDefault();
      event.stopPropagation();
    };
    const onContextMenuCapture = (event: MouseEvent) => {
      const element = targetFromEvent(event);
      if (!element) return;
      event.preventDefault();
      event.stopPropagation();
      openForElement(element, event.clientX + 4, event.clientY + 4);
    };
    const onKey = (event: KeyboardEvent) => {
      if (event.key !== 'Escape') return;
      if (target) setTarget(null);
      else setMode(false);
    };

    document.addEventListener('pointerdown', onPointerDownCapture, true);
    document.addEventListener('click', onClickCapture, true);
    document.addEventListener('contextmenu', onContextMenuCapture, true);
    window.addEventListener('keydown', onKey);
    return () => {
      document.removeEventListener('pointerdown', onPointerDownCapture, true);
      document.removeEventListener('click', onClickCapture, true);
      document.removeEventListener('contextmenu', onContextMenuCapture, true);
      window.removeEventListener('keydown', onKey);
    };
  }, [mode, target]);

  useEffect(() => {
    if (!toast) return;
    const timer = window.setTimeout(() => setToast(null), 1800);
    return () => window.clearTimeout(timer);
  }, [toast]);

  const toggleMode = useCallback(() => {
    setTarget(null);
    setMode(current => !current);
  }, []);

  const openTargetMenu = useCallback((nextTarget: AutomationTarget, x: number, y: number) => {
    const position = clampMenuPosition(x, y);
    setTarget({ ...nextTarget, x: position.left, y: position.top });
  }, []);

  const assignedSlotForTarget = useCallback((targetId: string) => {
    const slot = slots.find(item => item.parameterId === targetId);
    return slot?.index;
  }, [slots]);

  const assign = useCallback((slotIndex: number) => {
    if (!target) return;
    sendToJuce('assignAutomationSlot', { slot: slotIndex, targetId: target.id });
    setSlots(previous => previous.map(slot => {
      if (slot.parameterId === target.id)
        return { ...slot, parameterId: '', targetName: '', assigned: false };
      if (slot.index === slotIndex)
        return { ...slot, parameterId: target.id, targetName: target.name, assigned: true };
      return slot;
    }));
    setToast(`${target.name} → ASTER AUTO ${String(slotIndex + 1).padStart(2, '0')}`);
    setTarget(null);
  }, [target]);

  const assignedSlot = target
    ? slots.find(slot => slot.parameterId === target.id)
    : undefined;
  const firstFreeSlot = slots.find(slot => !slot.assigned);
  const menuPosition = target ? { left: target.x, top: target.y } : undefined;

  const contextValue = useMemo(
    () => ({ mode, toggleMode, openTargetMenu, assignedSlotForTarget }),
    [assignedSlotForTarget, mode, openTargetMenu, toggleMode],
  );

  return (
    <AutomationAssignContext.Provider value={contextValue}>
      {children}
      {mode && createPortal(
        <div className={styles.assignBanner} role="status">
          AUTOMATION ASSIGN — Click a highlighted control · Right-click works anytime · Esc to exit
        </div>,
        document.body,
      )}
      {target && createPortal(
        <>
          <button
            type="button"
            aria-label="Close automation assignment"
            onClick={() => setTarget(null)}
            style={{ position: 'fixed', inset: 0, zIndex: 2499, border: 0, background: 'transparent' }}
          />
          <div className={styles.menu} style={menuPosition} role="menu">
            <div className={styles.menuTitle}>{target.name}</div>
            <div className={styles.menuHint}>Assign this control to one of the 24 fixed DAW automation slots.</div>
            {firstFreeSlot && (
              <button type="button" className={styles.primary} onClick={() => assign(firstFreeSlot.index)}>
                ASSIGN TO NEXT FREE · AUTO {String(firstFreeSlot.index + 1).padStart(2, '0')}
              </button>
            )}
            <div className={styles.sectionLabel}>CHOOSE SLOT</div>
            <div className={styles.slotList}>
              {slots.map(slot => {
                const current = slot.parameterId === target.id;
                return (
                  <button
                    key={slot.index}
                    type="button"
                    className={`${styles.slot} ${current ? styles.slotCurrent : ''}`}
                    disabled={slot.assigned && !current}
                    onClick={() => assign(slot.index)}
                  >
                    <span className={styles.slotNumber}>AUTO {String(slot.index + 1).padStart(2, '0')}</span>
                    <span className={styles.slotTarget}>{current ? 'CURRENT' : slot.targetName || 'Unassigned'}</span>
                  </button>
                );
              })}
            </div>
            {assignedSlot && (
              <button
                type="button"
                className={styles.remove}
                onClick={() => {
                  sendToJuce('clearAutomationSlot', { slot: assignedSlot.index });
                  setSlots(previous => previous.map(slot => slot.index === assignedSlot.index
                    ? { ...slot, parameterId: '', targetName: '', assigned: false }
                    : slot));
                  setToast(`${target.name} automation removed`);
                  setTarget(null);
                }}
              >
                REMOVE FROM AUTO {String(assignedSlot.index + 1).padStart(2, '0')}
              </button>
            )}
          </div>
        </>,
        document.body,
      )}
      {toast && createPortal(<div className={styles.toast} role="status">{toast}</div>, document.body)}
    </AutomationAssignContext.Provider>
  );
}

export function AutomationModeButton() {
  const { mode, toggleMode } = useContext(AutomationAssignContext);
  return (
    <button
      type="button"
      className={`${styles.autoButton} ${mode ? styles.autoButtonActive : ''}`}
      aria-label="Automation Assign"
      aria-pressed={mode}
      title="Automation Assign"
      onClick={toggleMode}
    >
      AUTOMATION
    </button>
  );
}

export function useAutomationAssign() {
  return useContext(AutomationAssignContext);
}
