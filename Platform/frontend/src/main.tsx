import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { App } from "./app/App";
import "./styles.css";
import "./studio.css";
import "./features/runtimeLayout.css";
import "./workflow.css";
import "./features/authoringLayout.css";
import "./research.css";

createRoot(document.getElementById("root")!).render(
  <StrictMode><App /></StrictMode>,
);
