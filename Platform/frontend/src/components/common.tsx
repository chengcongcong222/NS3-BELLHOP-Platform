import type { PropsWithChildren, ReactNode } from "react";
import { useQuery } from "@tanstack/react-query";
import { NavLink } from "react-router-dom";
import { ApiFailure } from "../api/client";
import { queries } from "../api/queries";
import { PRODUCT_METADATA } from "../productMetadata";

export function AppShell({ children }: PropsWithChildren) {
  const system = useQuery(queries.systemInfo());
  const links = [
    ["/", "工作台"],
    ["/cases", "案例中心"],
    ["/environments", "环境建设"],
    ["/scenarios", "场景设计"],
    ["/experiments", "实验配置"],
    ["/runs", "仿真运行"],
    ["/results", "结果分析"],
    ["/resources", "资源管理"],
    ["/system", "系统信息"],
  ];
  return (
    <div className="app-shell">
      <header className="product-header">
        <div>
          <strong>{system.data?.platform_name ?? PRODUCT_METADATA.platformName}</strong>
          <span>水声网络仿真工作台</span>
        </div>
        <div className="engine">Simulation Engine: {system.data ? `${system.data.simulation.engine} ${system.data.simulation.version}` : PRODUCT_METADATA.simulationEngineDisplay}</div>
      </header>
      <nav aria-label="主导航">
        {links.map(([to, label]) => (
          <NavLink key={to} to={to} end={to === "/"}>
            {label}
          </NavLink>
        ))}
      </nav>
      <main>{children}</main>
      <footer>
        {system.data
          ? `${system.data.simulation.time_authority} clock · ${system.data.simulation.scheduler_authority} scheduler · ${system.data.simulation.scheduling_gateway} · ${system.data.product_baseline}`
          : `${PRODUCT_METADATA.footerAuthority} · ${PRODUCT_METADATA.schedulingGateway}`}
      </footer>
    </div>
  );
}

export function PageHeader({ title, detail }: { title: string; detail: string }) {
  return (
    <div className="page-header">
      <div>
        <p className="eyebrow">SIMULATION WORKBENCH</p>
        <h1>{title}</h1>
      </div>
      <p>{detail}</p>
    </div>
  );
}

export function LoadingState() {
  return <div className="state-panel">正在读取后端权威资源…</div>;
}

export function ErrorState({ error }: { error: unknown }) {
  const failure = error instanceof ApiFailure ? error : null;
  const title = {
    NotFound: "资源不存在",
    BackendBusy: "后端正忙",
    RunNotReady: "结果尚未就绪",
    RunFailed: "运行未产生正式结果",
    ProtocolFailure: "协议数据无效",
    TransportUnavailable: "后端连接不可用",
    ApiFailure: "后端拒绝了请求",
  }[failure?.kind ?? "ApiFailure"];
  return (
    <div className="state-panel error" role="alert">
      <strong>{title}</strong>
      <span>{failure?.message ?? "未知前端错误"}</span>
      {failure && <code>{failure.code}</code>}
    </div>
  );
}

export function EmptyState({ children }: PropsWithChildren) {
  return <div className="state-panel">{children}</div>;
}

export function Status({ value }: { value: string }) {
  return <span className={`status status-${value.toLowerCase()}`}>{value}</span>;
}

export function MetricCard({ label, value }: { label: string; value: ReactNode }) {
  return (
    <div className="metric-card">
      <span>{label}</span>
      <strong>{value}</strong>
    </div>
  );
}

export function Field({ label, children }: PropsWithChildren<{ label: string }>) {
  return (
    <div className="field">
      <dt>{label}</dt>
      <dd>{children}</dd>
    </div>
  );
}
