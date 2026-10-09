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
    ["/environments", "≈", "环境建设"],
    ["/workspace/experiment", "⚙", "实验工作台"],
    ["/runs", "▶", "运行与结果"],
    ["/cases", "▤", "预置案例"],
    ["/system", "i", "系统信息"],
  ];
  const workspaceSection = location.pathname.startsWith("/workspace/environment") ? "/environments"
    : location.pathname.startsWith("/workspace/scenario") ? "/workspace/experiment"
    : location.pathname.startsWith("/workspace/experiment") ? "/workspace/experiment" : null;
  const routeSection = location.pathname.startsWith("/results") ? "/runs"
    : location.pathname.startsWith("/experiments") ? "/workspace/experiment"
    : workspaceSection ?? (location.pathname === "/" ? "/" : `/${location.pathname.split("/")[1]}`);
  const active = links.find(([to]) => to === routeSection) ?? links[0];
  // A retry without cached data temporarily returns the query to pending.
  // Retain the failed-connection state until a successful response arrives.
  const serviceUnavailable = system.isError || (!system.data && system.errorUpdatedAt > 0);
  const serviceConnected = Boolean(system.data) && !serviceUnavailable;
  const serviceLabel = serviceUnavailable ? "仿真服务未连接" : serviceConnected ? "仿真服务已连接" : "正在连接…";
  const serviceSummary = serviceUnavailable ? "服务未连接" : serviceConnected ? "系统就绪" : "连接中";
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
        <div className="engine"><i className={serviceConnected ? "service-online" : "service-pending"} /><span role="status">{serviceLabel}</span>{serviceUnavailable && <button type="button" onClick={() => { void system.refetch(); }} disabled={system.isFetching}>{system.isFetching ? "正在重试…" : "重试连接"}</button>}<small>{system.data ? `${system.data.simulation.engine} ${system.data.simulation.version}` : PRODUCT_METADATA.simulationEngineDisplay}</small></div>
      </header>
      <nav aria-label="主导航">
        <span className="nav-section">仿真工作流</span>
        {links.slice(0, 4).map(([to, icon, label]) => (
          <NavLink key={to} to={to} end={to === "/"} className={({ isActive }) => isActive || to === workspaceSection ? "active" : undefined}>
            <b>{icon}</b><span>{label}</span>
          </NavLink>
        ))}
        <span className="nav-section nav-secondary">平台</span>
        {links.slice(4).map(([to, icon, label]) => <NavLink key={to} to={to} className={({ isActive }) => isActive ? "active" : undefined}><b>{icon}</b><span>{label}</span></NavLink>)}
      </nav>
      <main>{children}</main>
      <footer>
        <span><i className={serviceConnected ? "service-online" : "service-pending"} />{serviceSummary}</span>
        <span>{active[2]}</span>
        <span className="footer-technical">{system.data ? `${system.data.simulation.engine} ${system.data.simulation.version} · ${serviceSummary}` : serviceSummary}</span>
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
  const message = error instanceof Error ? error.message : null;
  const cause = error instanceof Error && error.cause instanceof Error ? error.cause : null;
  const title = failure ? {
    NotFound: "资源不存在",
    BackendBusy: "后端正忙",
    RunNotReady: "结果尚未就绪",
    RunFailed: "运行未产生正式结果",
    ProtocolFailure: "协议数据无效",
    TransportUnavailable: "后端连接不可用",
    ApiFailure: "后端拒绝了请求",
  }[failure.kind] : "操作未完成";
  const guidance = {
    NotFound: "请返回资源目录，确认所选资源及版本仍然存在，再重新选择。系统不会自动改用其他资源。",
    BackendBusy: "当前已有实验占用运行服务，请在运行列表查看进度，结束后再试。",
    RunNotReady: "请先查看运行状态；运行完成并保存结果后再打开分析。",
    RunFailed: "请查看这次运行的失败原因，修改相关配置后重新运行。",
    ProtocolFailure: "收到的数据格式不符合当前版本。请检查前端、后端和运行程序是否一起更新。",
    TransportUnavailable: "请检查本机仿真服务是否已启动，再刷新重试。连接中断本身不代表仿真失败。",
    ApiFailure: "请求未完成，请检查输入和资源绑定后重试；需要排查时可展开技术详情。",
  }[failure?.kind ?? "ApiFailure"];
  // API diagnostics may contain implementation names and paths. Keep them
  // available for support without making them the user's primary instruction.
  const localValidation = !failure && message && /[\u3400-\u9fff]/.test(message) ? message : null;
  return (
    <div className="state-panel error" role="alert">
      <strong>{title}</strong>
      <span>{localValidation ?? guidance}</span>
      {(failure || cause || (message && !localValidation)) && <details><summary>技术详情</summary>{failure && <code>{failure.code}</code>}{cause instanceof ApiFailure && <code>{cause.code}</code>}{cause ? <p>{cause.message}</p> : message && <p>{message}</p>}</details>}
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
