import { cleanup } from "@testing-library/react";
import { afterEach, vi } from "vitest";

class QuietEventSource {
  onerror: ((event: Event) => void) | null = null;
  addEventListener(): void {}
  close(): void {}
}

vi.stubGlobal("EventSource", QuietEventSource);

afterEach(() => {
  cleanup();
  localStorage.clear();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
  vi.stubGlobal("EventSource", QuietEventSource);
});
