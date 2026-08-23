import { create } from 'zustand';

import type { CommunicationEvent } from '../types';

export type StudioSelection =
  | { scope: 'scene' }
  | { scope: 'node'; nodeId: number }
  | { scope: 'edge'; edgeKey: string };

export type StudioViewMode = 'topology' | 'profile' | 'scene3d';
export type RunPhase = 'idle' | 'running' | 'done' | 'error';
export type LogLevel = 'info' | 'warn' | 'error' | 'success';
export type PlaybackTempoMode = 'real' | 'compressed';

export interface LogEntry {
  msg: string;
  level: LogLevel;
}

interface StudioRuntimeState {
  selection: StudioSelection;
  viewMode: StudioViewMode;
  runPhase: RunPhase;
  runLogs: LogEntry[];
  runStartTs: number | null;
  runElapsed: string;
  playbackSpeed: number;
  playbackTempoMode: PlaybackTempoMode;
  playbackPlaying: boolean;
  playbackFrameIndex: number;
  communicationEvents: CommunicationEvent[];
  setSelection: (selection: StudioSelection) => void;
  selectScene: () => void;
  selectNode: (nodeId: number) => void;
  selectEdge: (edgeKey: string) => void;
  setViewMode: (viewMode: StudioViewMode) => void;
  setRunPhase: (runPhase: RunPhase) => void;
  appendRunLog: (entry: LogEntry) => void;
  clearRunLogs: () => void;
  setRunStartTs: (value: number | null) => void;
  setRunElapsed: (value: string) => void;
  setPlaybackSpeed: (value: number) => void;
  setPlaybackTempoMode: (value: PlaybackTempoMode) => void;
  setPlaybackPlaying: (value: boolean | ((previous: boolean) => boolean)) => void;
  setPlaybackFrameIndex: (value: number | ((previous: number) => number)) => void;
  setCommunicationEvents: (events: CommunicationEvent[]) => void;
  hydrateScenarioRuntime: (playbackSpeed: number) => void;
}

function resolveUpdater<T>(value: T | ((previous: T) => T), previous: T): T {
  if (typeof value === 'function') {
    return (value as (previous: T) => T)(previous);
  }
  return value;
}

export const useStudioRuntimeStore = create<StudioRuntimeState>((set) => ({
  selection: { scope: 'scene' },
  viewMode: 'topology',
  runPhase: 'idle',
  runLogs: [],
  runStartTs: null,
  runElapsed: '0 s',
  playbackSpeed: 1,
  playbackTempoMode: 'real',
  playbackPlaying: false,
  playbackFrameIndex: 0,
  communicationEvents: [],
  setSelection: (selection) => set({ selection }),
  selectScene: () => set({ selection: { scope: 'scene' } }),
  selectNode: (nodeId) => set({ selection: { scope: 'node', nodeId } }),
  selectEdge: (edgeKey) => set({ selection: { scope: 'edge', edgeKey } }),
  setViewMode: (viewMode) => set({ viewMode }),
  setRunPhase: (runPhase) => set({ runPhase }),
  appendRunLog: (entry) => set((state) => ({ runLogs: [...state.runLogs, entry] })),
  clearRunLogs: () => set({ runLogs: [] }),
  setRunStartTs: (runStartTs) => set({ runStartTs }),
  setRunElapsed: (runElapsed) => set({ runElapsed }),
  setPlaybackSpeed: (playbackSpeed) => set({ playbackSpeed }),
  setPlaybackTempoMode: (playbackTempoMode) => set({ playbackTempoMode }),
  setPlaybackPlaying: (value) => set((state) => ({
    playbackPlaying: resolveUpdater(value, state.playbackPlaying),
  })),
  setPlaybackFrameIndex: (value) => set((state) => ({
    playbackFrameIndex: resolveUpdater(value, state.playbackFrameIndex),
  })),
  setCommunicationEvents: (communicationEvents) => set({ communicationEvents }),
  hydrateScenarioRuntime: (playbackSpeed) => set({
    selection: { scope: 'scene' },
    runPhase: 'idle',
    runLogs: [],
    runStartTs: null,
    runElapsed: '0 s',
    playbackSpeed,
    playbackTempoMode: 'real',
    playbackPlaying: false,
    playbackFrameIndex: 0,
    communicationEvents: [],
  }),
}));