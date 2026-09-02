import type { PropsWithChildren, ReactNode } from "react";
import { useQuery } from "@tanstack/react-query";
import { NavLink, useLocation } from "react-router-dom";
import { ApiFailure } from "../api/client";
import { queries } from "../api/queries";
import { PRODUCT_METADATA } from "../productMetadata";
import { productLabel } from "../productLanguage";

export function AppShell({ children }: PropsWithChildren) {
  const system = useQuery(queries.systemInfo());
  const location = useLocation();
  const links = [
    ["/", "⌂", "工作台"],
    ["/cases", "▤", "案例中心"],
    ["/environments", "≈", "环境建设"],
    ["/scenarios", "⌖", "场景设计"],
    ["/experiments", "⚙", "实验配置"],
    ["/runs", "▶", "仿真运行"],
    ["/results", "⌁", "结果分析"],
    ["/resources", "▦", "资源管理"],
    ["/system", "i", "系统信息"],
  ];
  const workspaceSection = location.pathname.startsWith("/workspace/environment") ? "/environments"
    : location.pathname.startsWith("/workspace/scenario") ? "/scenarios"
    : location.pathname.startsWith("/workspace/experiment") ? "/experiments" : null;
  const active = links.find(([to]) => to === (workspaceSection ?? (to === "/" && location.pathname === "/" ? "/" : location.pathname.split("/").slice(0, 2).join("/")))) ?? links[0];
  return (
    <div className="app-shell">
      <header className="product-header">
        <div className="product-mark">
          <span className="product-monogram">N3</span>
          <div>
          <strong>{system.data?.platform_name ?? PRODUCT_METADATA.platformName}</strong>
          <span>水声网络仿真工作台</span>
          </div>
        </div>
        <div className="shell-context"><span>当前模块</span><strong>{active[2]}</strong></div>
        <div className="engine"><i className={system.data ? "service-online" : "service-pending"} />{system.data ? "仿真服务已连接" : "正在连接…"}<small>{system.data ? `${system.data.simulation.engine} ${system.data.simulation.version}` : PRODUCT_METADATA.simulationEngineDisplay}</small></div>
      </header>
      <nav aria-label="主导航">
        <span className="nav-section">仿真工作流</span>
        {links.slice(0, 7).map(([to, icon, label]) => (
          <NavLink key={to} to={to} end={to === "/"} className={({ isActive }) => isActive || to === workspaceSection ? "active" : undefined}>
            <b>{icon}</b><span>{label}</span>
          </NavLink>
        ))}
        <span className="nav-section nav-secondary">平台</span>
        {links.slice(7).map(([to, icon, label]) => <NavLink key={to} to={to} className={({ isActive }) => isActive ? "active" : undefined}><b>{icon}</b><span>{label}</span></NavLink>)}
      </nav>
      <main>{children}</main>
      <footer>
        <span><i className={system.data ? "service-online" : "service-pending"} />{system.data ? "系统就绪" : "连接中"}</span>
        <span>{active[2]}</span>
        <span className="footer-technical">{system.data ? `${system.data.simulation.engine} ${system.data.simulation.version} · 系统就绪` : "正在连接…"}</span>
      </footer>
    </div>
  );
}

export function PageHeader({ title, detail }: { title: string; detail: string }) {
  return (
    <div className="page-header">
      <div>
        <p className="eyebrow">仿真工作台</p>
        <h1>{title}</h1>
      </div>
      <p>{detail}</p>
    </div>
  );
}

export function LoadingState() { return <div className="state-panel">正在加载…</div>; }

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
      <span>{failure?.message ?? "发生未知错误"}</span>
      {failure && <details><summary>技术详情</summary><code>{failure.code}</code></details>}
    </div>
  );
}

export function EmptyState({ children }: PropsWithChildren) {
  return <div className="state-panel">{children}</div>;
}

export function Status({ value }: { value: string }) { return <span className={`status status-${value.toLowerCase()}`} title={value}>{productLabel(value)}<span className="status-raw">{value}</span></span>; }

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
