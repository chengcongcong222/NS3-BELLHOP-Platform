import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { render } from "@testing-library/react";
import { MemoryRouter } from "react-router-dom";
import { vi } from "vitest";
import { AppRoutes } from "../app/App";
import { acceptanceEvidence, experiment, scenario, systemInfo } from "./fixtures";

type Fixture = { status?: number; body: unknown } | Error;
type FixtureMap = Record<string, Fixture>;

export function installApi(fixtures: FixtureMap) {
  const fetchMock = vi.fn(async (input: RequestInfo | URL, init?: RequestInit) => {
    const url = typeof input === "string" ? input : input.toString();
    const method = init?.method ?? "GET";
    const fixture = fixtures[`${method} ${url}`] ?? fixtures[url] ??
      (url === "/system/info" ? { body: systemInfo } : undefined) ??
      (url === `/scenarios/${scenario.scenario_id}/versions/${scenario.version}` ? { body: scenario } : undefined) ??
      (url === `/experiments/${experiment.experiment_id}/versions/${experiment.version}` ? { body: experiment } : undefined) ??
      (url.endsWith("/acceptance-evidence") ? { body: acceptanceEvidence } : undefined);
    if (!fixture) throw new Error(`Unexpected request: ${method} ${url}`);
    if (fixture instanceof Error) throw fixture;
    return new Response(JSON.stringify(fixture.body), {
      status: fixture.status ?? 200,
      headers: { "Content-Type": "application/json" },
    });
  });
  vi.stubGlobal("fetch", fetchMock);
  return fetchMock;
}

export function renderRoute(path: string) {
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
