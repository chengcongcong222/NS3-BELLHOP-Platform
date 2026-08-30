import { queryOptions } from "@tanstack/react-query";
import { apiClient } from "./client";

export const queries = {
  environments: () => queryOptions({ queryKey: ["environments"], queryFn: apiClient.listEnvironments }),
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
  results: () => queryOptions({ queryKey: ["results"], queryFn: apiClient.listResults }),
  result: (id: string) => queryOptions({ queryKey: ["results", id], queryFn: () => apiClient.getResult(id) }),
};
