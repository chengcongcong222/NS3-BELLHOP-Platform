import { QueryClient, QueryClientProvider, useQuery } from "@tanstack/react-query";
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
import { UnifiedExperimentEditor } from "../features/UnifiedExperimentEditor";
import { RunGroupPage, RunGroupCatalogPage } from "../features/runGroups";
import { queries } from "../api/queries";
import { LoadingState, ErrorState } from "../components/common";
import {
  EnvironmentAuthoringPage,
  ServerWorkspacePage,
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
  const draft = params.get("draft");
  const saved = useQuery({ ...queries.drafts("experiment"), enabled: !!draft });
  if (draft && saved.isPending) return <LoadingState />;
  if (draft && saved.error) return <ErrorState error={saved.error} />;
  const existing = saved.data?.find(d => d.draft_id === draft);
  if (draft && !existing) return <p role="alert">实验草稿不存在，未加载默认实验。</p>;
  const recorded = existing ? existing.payload.physical_layer === "Channel3Recorded" : params.get("mode") === "C";
  return <UnifiedExperimentEditor key={JSON.stringify([draft, params.get("scenario")])}
    initialKind={recorded ? "Recorded" : "Bellhop"} draftId={draft} scenarioId={params.get("scenario")} />;
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
        <Route path="/run-groups" element={<RunGroupCatalogPage />} />
        <Route path="/run-groups/:groupId" element={<RunGroupPage />} />
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
