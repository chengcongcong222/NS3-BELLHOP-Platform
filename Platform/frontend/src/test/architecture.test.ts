import { readFileSync, readdirSync, statSync } from "node:fs";
import { join, resolve } from "node:path";
import { expect, it } from "vitest";

function sourceFiles(root: string): string[] {
  return readdirSync(root).flatMap((entry) => {
    const path = join(root, entry);
    return statSync(path).isDirectory() ? sourceFiles(path) : path.endsWith(".tsx") || path.endsWith(".ts") ? [path] : [];
  });
}

it("keeps pages behind the typed API boundary and free of legacy DTOs", () => {
  const root = resolve(import.meta.dirname, "../features");
  for (const path of sourceFiles(root)) {
    const source = readFileSync(path, "utf8");
    expect(source).not.toMatch(/\bfetch\s*\(/);
    expect(source).not.toMatch(/legacy|StudioPage|DemoScenario|WorkerCompleted|NDJSON/);
  }
});

it("keeps ns-3 display provenance in the formal product metadata module", () => {
  const root = resolve(import.meta.dirname, "..");
  const metadata = resolve(root, "productMetadata.ts");
  for (const path of sourceFiles(root)) {
    if (path === metadata || path.includes(`${join("src", "test")}`)) continue;
    const source = readFileSync(path, "utf8");
    expect(source).not.toMatch(/ns-3 3\.47|仿真时间与事件调度由 ns-3 Simulator/);
  }
  expect(readFileSync(metadata, "utf8")).toMatch(/ns3Version: "3\.47"/);
});
