import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { BrowserRouter, Route, Routes } from "react-router-dom";
import { AppShell } from "../components/common";
import {
  EnvironmentCatalogPage,
  EnvironmentDetailPage,
  ExperimentCatalogPage,
  ExperimentDetailPage,
  NotFoundPage,
  OverviewPage,
  ResultCatalogPage,
  RunCatalogPage,
  ScenarioCatalogPage,
  ScenarioDetailPage,
} from "../features/pages";
import { ResultDetailPage, RunMonitorPage } from "../features/operations";
import {
  CaseCatalogPage,
  CaseDetailPage,
  EnvironmentWorkspacePage,
  ExperimentWorkspacePage,
  ResourceManagementPage,
  ScenarioWorkspacePage,
  SystemPage,
  WorkbenchPage,
} from "../features/workbench";

export const queryClient = new QueryClient({
  defaultOptions: { queries: { retry: false, staleTime: 5_000 } },
});

export function AppRoutes() {
  return (
    <AppShell>
      <Routes>
        <Route path="/" element={<WorkbenchPage />} />
        <Route path="/overview" element={<OverviewPage />} />
        <Route path="/cases" element={<CaseCatalogPage />} />
        <Route path="/cases/:caseId" element={<CaseDetailPage />} />
        <Route path="/environments" element={<EnvironmentCatalogPage />} />
        <Route path="/environments/:assetId" element={<EnvironmentDetailPage />} />
        <Route path="/workspace/environment" element={<EnvironmentWorkspacePage />} />
        <Route path="/scenarios" element={<ScenarioCatalogPage />} />
        <Route path="/scenarios/:scenarioId/versions/:version" element={<ScenarioDetailPage />} />
        <Route path="/workspace/scenario" element={<ScenarioWorkspacePage />} />
        <Route path="/experiments" element={<ExperimentCatalogPage />} />
        <Route path="/experiments/:experimentId/versions/:version" element={<ExperimentDetailPage />} />
        <Route path="/workspace/experiment" element={<ExperimentWorkspacePage />} />
        <Route path="/runs" element={<RunCatalogPage />} />
        <Route path="/runs/:runId" element={<RunMonitorPage />} />
        <Route path="/results" element={<ResultCatalogPage />} />
        <Route path="/results/:runId" element={<ResultDetailPage />} />
        <Route path="/resources" element={<ResourceManagementPage />} />
        <Route path="/system" element={<SystemPage />} />
        <Route path="*" element={<NotFoundPage />} />
      </Routes>
    </AppShell>
  );
}

export function App() {
  return <QueryClientProvider client={queryClient}><BrowserRouter><AppRoutes /></BrowserRouter></QueryClientProvider>;
}
