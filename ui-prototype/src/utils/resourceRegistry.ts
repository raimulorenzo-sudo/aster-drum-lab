type ResourceKind = 'cpu' | 'mem';

interface ResourceMeterTarget {
  value: HTMLElement | null;
  fill: HTMLElement | null;
}

interface ResourceMeterSnapshot {
  label: string;
  amount: number;
}

const MEM_METER_FULL_MB = 512;

const targets = new Map<ResourceKind, Map<symbol, ResourceMeterTarget>>();
const snapshots: Record<ResourceKind, ResourceMeterSnapshot> = {
  cpu: { label: '0%', amount: 0 },
  mem: { label: '0 MB', amount: 0 },
};

function clamp01(value: number) {
  return Math.max(0, Math.min(1, value));
}

function snapshotFromRaw(cpuPercentRaw: number, sampleBytesRaw: number) {
  const cpuPercent = Math.max(0, Math.min(100, Number.isFinite(cpuPercentRaw) ? cpuPercentRaw : 0));
  const sampleBytes = Math.max(0, Number.isFinite(sampleBytesRaw) ? sampleBytesRaw : 0);
  const memMb = sampleBytes / (1024 * 1024);
  const memLabelValue = memMb < 10 && memMb > 0 ? memMb.toFixed(1) : Math.round(memMb).toString();

  return {
    cpu: {
      label: `${Math.round(cpuPercent)}%`,
      amount: clamp01(cpuPercent / 100),
    },
    mem: {
      label: `${memLabelValue} MB`,
      amount: clamp01(memMb / MEM_METER_FULL_MB),
    },
  } satisfies Record<ResourceKind, ResourceMeterSnapshot>;
}

function renderTarget(target: ResourceMeterTarget, snapshot: ResourceMeterSnapshot) {
  if (target.value) target.value.textContent = snapshot.label;
  if (target.fill) target.fill.style.width = `${Math.round(snapshot.amount * 100)}%`;
}

function renderKind(kind: ResourceKind) {
  targets.get(kind)?.forEach(target => renderTarget(target, snapshots[kind]));
}

export function updateResourceStats(cpuPercentRaw: number, sampleBytesRaw: number) {
  const next = snapshotFromRaw(cpuPercentRaw, sampleBytesRaw);

  (Object.keys(next) as ResourceKind[]).forEach(kind => {
    const prev = snapshots[kind];
    const incoming = next[kind];
    if (prev.label === incoming.label && Math.abs(prev.amount - incoming.amount) < 0.005) return;

    snapshots[kind] = incoming;
    renderKind(kind);
  });
}

export function registerResourceMeter(kind: ResourceKind, target: ResourceMeterTarget) {
  const token = Symbol('resource-meter-target');
  let kindTargets = targets.get(kind);
  if (!kindTargets) {
    kindTargets = new Map();
    targets.set(kind, kindTargets);
  }

  kindTargets.set(token, target);
  renderTarget(target, snapshots[kind]);

  return () => {
    kindTargets?.delete(token);
    if (kindTargets?.size === 0) targets.delete(kind);
  };
}
