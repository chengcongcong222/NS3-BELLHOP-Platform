import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { BrowserRouter, Navigate, Route, Routes, useSearchParams } from "react-router-dom";
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
  ScenarioDetailPage,
} from "../features/pages";
import { StudioResultPage, StudioRunPage } from "../features/studio";
import {
  EnvironmentAuthoringPage,
  ServerWorkspacePage,
  ExperimentAuthoringPage,
  TrajectoryAuthoringPage,
} from "../features/authoring";
import {
  CaseCatalogPage,
  CaseDetailPage,
  SystemPage,
  WorkbenchPage,
} from "../features/workbench";

export const queryClient = new QueryClient({
  defaultOptions: { queries: { retry: false, staleTime: 5_000 } },
});

function ScenarioEditorRedirect() {
  const [params] = useSearchParams();
  const draft = params.get("draft");
  return <Navigate replace to={`/workspace/experiment${draft ? `?scenario=${encodeURIComponent(draft)}` : ""}`} />;
}

function ExperimentEditorRoute() {
  const [params] = useSearchParams();
  return <ExperimentAuthoringPage key={JSON.stringify([params.get("draft"), params.get("scenario")])} />;
}

export function AppRoutes() {
  return (
    <AppShell>
      <Routes>
        <Route path="/" element={<WorkbenchPage />} />
        <Route path="/new-simulation" element={<Navigate replace to="/workspace/experiment" />} />
        <Route path="/overview" element={<OverviewPage />} />
        <Route path="/cases" element={<CaseCatalogPage />} />
        <Route path="/cases/:caseId" element={<CaseDetailPage />} />
        <Route path="/environments" element={<EnvironmentCatalogPage />} />
        <Route path="/environments/:assetId" element={<EnvironmentDetailPage />} />
        <Route path="/workspace/environment" element={<EnvironmentAuthoringPage />} />
        <Route path="/trajectories" element={<TrajectoryAuthoringPage />} />
        <Route path="/scenarios" element={<Navigate replace to="/workspace/experiment" />} />
        <Route path="/scenarios/:scenarioId/versions/:version" element={<ScenarioDetailPage />} />
        <Route path="/workspace/scenario" element={<ScenarioEditorRedirect />} />
        <Route path="/experiments" element={<ExperimentCatalogPage />} />
        <Route path="/experiments/:experimentId/versions/:version" element={<ExperimentDetailPage />} />
        <Route path="/workspace/experiment" element={<ExperimentEditorRoute />} />
        <Route path="/runs" element={<RunCatalogPage />} />
        <Route path="/runs/:runId" element={<StudioRunPage />} />
        <Route path="/results" element={<ResultCatalogPage />} />
        <Route path="/results/:runId" element={<StudioResultPage />} />
        <Route path="/resources" element={<ServerWorkspacePage />} />
        <Route path="/system" element={<SystemPage />} />
        <Route path="*" element={<NotFoundPage />} />
      </Routes>
    </AppShell>
  );
}

export function App() {
  return <QueryClientProvider client={queryClient}><BrowserRouter><AppRoutes /></BrowserRouter></QueryClientProvider>;
}
