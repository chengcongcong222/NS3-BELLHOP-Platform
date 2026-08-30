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
