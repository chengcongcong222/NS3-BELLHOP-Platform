import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { render } from "@testing-library/react";
import { MemoryRouter } from "react-router-dom";
import { vi } from "vitest";
import { AppRoutes } from "../app/App";
import { acceptanceEvidence, environment, experiment, scenario, systemInfo } from "./fixtures";
import type { RunDefinitionContextDto, RunDto } from "../api/types";
import { algorithmCatalog } from "./algorithmFixtures";

type Fixture = { status?: number; body: unknown; stream?: boolean } | Error;
type FixtureMap = Record<string, Fixture>;

function knownRunContext(url: string, fixtures: FixtureMap): Fixture | undefined {
  const match = /^\/runs\/([^/]+)\/definition-context$/.exec(url);
  if (!match) return undefined;
  const supplied = fixtures[`GET /runs/${match[1]}`] ?? fixtures[`/runs/${match[1]}`];
  if (!supplied || supplied instanceof Error || (supplied.status ?? 200) !== 200
      || !supplied.body || typeof supplied.body !== "object") return undefined;
  const record = supplied.body as RunDto;
  // A test must explicitly declare this Run, and it must reference the known
  // preset fixtures. Never invent context for arbitrary authored identities.
  if (record.run_id !== match[1] || record.scenario_id !== scenario.scenario_id
      || record.scenario_version !== scenario.version || record.experiment_id !== experiment.experiment_id
      || record.experiment_version !== experiment.version) return undefined;
  const body: RunDefinitionContextDto = {
    run_id: record.run_id, source: "Preset", definition_sha256: null,
    scenario,
    experiment: { ...experiment, phy: { ...experiment.phy, physical_layer: "Channel1Abstract" } },
    execution: { physical_layer: "Channel1Abstract", simulation_cycle_count: experiment.simulation_cycle_count, deterministic_seed: experiment.deterministic_seed },
    limitations: [],
  };
  return { body };
}

export function installApi(fixtures: FixtureMap) {
  const fetchMock = vi.fn(async (input: RequestInfo | URL, init?: RequestInit) => {
    const url = typeof input === "string" ? input : input.toString();
    const method = init?.method ?? "GET";
    const fixture = fixtures[`${method} ${url}`] ?? fixtures[url] ??
      (method === "GET" ? knownRunContext(url, fixtures) : undefined) ??
      (url === "/system/info" ? { body: systemInfo } : undefined) ??
      (url === "/algorithms" ? { body: algorithmCatalog } : undefined) ??
      (url === "/recorded-channels" ? { body: [] } : undefined) ??
      (url === "/authoring/experiment-presets" ? { body: [
        { id: "bellhop-osdm", version: 1, name: "Bellhop OSDM 标准配置", physical_layer: "Channel2Waveform", osdm: { block_length: 256, vector_length: 16, cyclic_prefix: 64, constellation: "Qpsk", minimum_sync_correlation: .05, maximum_sync_search: 128 } },
        { id: "recorded-osdm", version: 1, name: "实录 OSDM 标准配置", physical_layer: "Channel3Recorded", osdm: { block_length: 256, vector_length: 16, cyclic_prefix: 128, constellation: "Qpsk", minimum_sync_correlation: .05, maximum_sync_search: 256 } },
      ] } : undefined) ??
      (url === "/environments/authoring-summaries" ? { body: {} } : undefined) ??
      (url === `/environments/${environment.environment_asset_id}` ? { body: environment } : undefined) ??
      (url === `/scenarios/${scenario.scenario_id}/versions/${scenario.version}` ? { body: scenario } : undefined) ??
      (url === `/experiments/${experiment.experiment_id}/versions/${experiment.version}` ? { body: experiment } : undefined) ??
      (url.endsWith("/acceptance-evidence") ? { body: acceptanceEvidence } : undefined);
    if (!fixture) throw new Error(`Unexpected request: ${method} ${url}`);
    if (fixture instanceof Error) throw fixture;
    return new Response(fixture.stream ? JSON.stringify({ type: "result", result: fixture.body }) + "\n" : JSON.stringify(fixture.body), {
      status: fixture.status ?? 200,
      headers: { "Content-Type": fixture.stream ? "application/x-ndjson" : "application/json" },
    });
  });
  vi.stubGlobal("fetch", fetchMock);
  return fetchMock;
}

export function renderRoute(path: string): ReturnType<typeof render> {
  const client = new QueryClient({
    defaultOptions: { queries: { retry: false }, mutations: { retry: false } },
  });
  return render(
    <QueryClientProvider client={client}>
      <MemoryRouter initialEntries={[path]}>
        <AppRoutes />
      </MemoryRouter>
    </QueryClientProvider>,
  );
}
