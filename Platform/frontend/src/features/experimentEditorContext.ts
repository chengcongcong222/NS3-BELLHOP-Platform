import { createContext, useContext } from "react";
import { useSearchParams } from "react-router-dom";

export type ResourceKind = "Bellhop" | "Recorded";
export interface EditorContext {
  draftId: string | null;
  scenarioId: string | null;
  selection: { id: string; serial: number } | null;
  registerSave: (save: () => Promise<unknown>) => void;
  reportResource: (id: string) => void;
  reportState: (dirty: boolean, busy: boolean) => void;
}
export const ExperimentEditorContext = createContext<EditorContext | null>(null);
export function useExperimentIdentity() {
  const context = useContext(ExperimentEditorContext);
  const [params] = useSearchParams();
  return { context, draftId: context ? context.draftId : params.get("draft"),
    scenarioId: context ? context.scenarioId : params.get("scenario") };
}
