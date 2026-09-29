import { queryOptions } from "@tanstack/react-query";
import { apiClient } from "./client";
import type { DraftKind } from "./types";

export const queries = {
  systemInfo: () => queryOptions({ queryKey: ["system-info"], queryFn: apiClient.getSystemInfo, staleTime: Infinity }),
  environments: () => queryOptions({ queryKey: ["environments"], queryFn: apiClient.listEnvironments }),
  trajectories: () => queryOptions({ queryKey: ["trajectories"], queryFn: apiClient.listTrajectories }),
  environment: (id: string) =>
    queryOptions({ queryKey: ["environments", id], queryFn: () => apiClient.getEnvironment(id) }),
  scenarios: () => queryOptions({ queryKey: ["scenarios"], queryFn: apiClient.listScenarios }),
  scenario: (id: string, version: string) =>
    queryOptions({ queryKey: ["scenarios", id, version], queryFn: () => apiClient.getScenario(id, version) }),
  experiments: () => queryOptions({ queryKey: ["experiments"], queryFn: apiClient.listExperiments }),
  experiment: (id: string, version: string) =>
    queryOptions({ queryKey: ["experiments", id, version], queryFn: () => apiClient.getExperiment(id, version) }),
  runs: () => queryOptions({ queryKey: ["runs"], queryFn: apiClient.listRuns }),
  run: (id: string) => queryOptions({ queryKey: ["runs", id], queryFn: () => apiClient.getRun(id) }),
  runActions: (id: string) => queryOptions({ queryKey: ["run-actions", id], queryFn: () => apiClient.getRunActions(id) }),
  runDefinitionContext: (id: string) => queryOptions({ queryKey: ["run-definition-context", id], queryFn: () => apiClient.getRunDefinitionContext(id) }),
  results: () => queryOptions({ queryKey: ["results"], queryFn: apiClient.listResults }),
  result: (id: string) => queryOptions({ queryKey: ["results", id], queryFn: () => apiClient.getResult(id) }),
  acceptanceEvidence: (id: string) => queryOptions({ queryKey: ["acceptance-evidence", id], queryFn: () => apiClient.getAcceptanceEvidence(id) }),
  drafts: (kind?: DraftKind) => queryOptions({ queryKey: ["authoring-drafts", kind ?? "all"], queryFn: () => apiClient.listDrafts(kind), staleTime: 0 }),
};
