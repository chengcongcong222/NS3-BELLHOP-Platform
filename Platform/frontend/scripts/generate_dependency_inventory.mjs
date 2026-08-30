import { readFileSync, readdirSync, statSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";

const frontendRoot = resolve(import.meta.dirname, "..");
const modulesRoot = join(frontendRoot, "node_modules");
const inventoryPath = resolve(
  frontendRoot,
  "../third_party/npm_dependencies.json",
);
const packages = new Map();

function visit(directory) {
  for (const entry of readdirSync(directory)) {
    if (entry.startsWith(".")) continue;
    const path = join(directory, entry);
    if (!statSync(path).isDirectory()) continue;
    if (entry.startsWith("@")) {
      visit(path);
      continue;
    }
    const manifestPath = join(path, "package.json");
    try {
      const manifest = JSON.parse(readFileSync(manifestPath, "utf8"));
      if (!manifest.name || !manifest.version || !manifest.license) {
        throw new Error(`Incomplete license metadata: ${manifestPath}`);
      }
      packages.set(`${manifest.name}@${manifest.version}`, {
        name: manifest.name,
        version: manifest.version,
        license: manifest.license,
      });
    } catch (error) {
      if (error?.code !== "ENOENT") throw error;
    }
    const nested = join(path, "node_modules");
    try {
      if (statSync(nested).isDirectory()) visit(nested);
    } catch (error) {
      if (error?.code !== "ENOENT") throw error;
    }
  }
}

visit(modulesRoot);
const dependencies = [...packages.values()].sort((left, right) =>
  left.name.localeCompare(right.name) || left.version.localeCompare(right.version),
);
writeFileSync(
  inventoryPath,
  `${JSON.stringify({ schema_version: 1, dependencies }, null, 2)}\n`,
);
