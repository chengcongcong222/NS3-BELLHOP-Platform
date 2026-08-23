import { NavLink, Outlet, useLocation } from 'react-router-dom';

import type { AppCtx } from '../types';

const navigation = [
  { to: '/', icon: '⊞', label: '总览首页', end: true },
  { to: '/studio', icon: '◧', label: '试验编排与仿真' },
  { to: '/environment', icon: '≋', label: 'Bellhop 环境管理' },
  { to: '/templates', icon: '⌘', label: '实验模板' },
  { to: '/assets', icon: '⎘', label: '场景管理与归档' },
  { to: '/settings', icon: '◈', label: '系统设置' },
];

const workbenchNavigation = [
  { to: '/studio', label: '工作台总览' },
  { to: '/config', label: '场景配置' },
  { to: '/monitor', label: '运行监视' },
  { to: '/results', label: '结果分析' },
];

const workbenchPaths = new Set(workbenchNavigation.map((item) => item.to));

const sectionDescriptions: Record<string, string> = {
  '/': '查看当前场景概览、关键指标和高频入口。',
  '/studio': '在同一工作台中完成场景配置、运行监视和结果分析。',
  '/environment': '统一构建、管理和选择可复用的 Bellhop 环境库。',
  '/templates': '管理实验模板，并基于模板派生新的试验场景。',
  '/assets': '集中查看场景库、最新结果、试验归档和运行历史。',
  '/settings': '检查服务状态，并维护当前场景的基础平台设置。',
};

interface ShellProps {
  ctx: AppCtx;
}

export function Shell({ ctx }: ShellProps) {
  const location = useLocation();
  const { dataset, activeScenario } = ctx;
  const meta = dataset.scenario.scenario_metadata;
  const version = meta?.version ?? '1.0';
  const pathname = location.pathname || '/';
  const inWorkbench = workbenchPaths.has(pathname);
  const primaryNav = inWorkbench
    ? navigation.find((item) => item.to === '/studio') ?? navigation[0]
    : navigation.find((item) => item.to === '/' ? pathname === '/' : pathname.startsWith(item.to)) ?? navigation[0];
  const activeWorkbenchTab = workbenchNavigation.find((item) => item.to === pathname) ?? workbenchNavigation[0];
  const currentPageTitle = inWorkbench ? '试验编排与仿真工作台' : primaryNav.label;
  const currentPageDescription = inWorkbench
    ? `当前视图：${activeWorkbenchTab.label} · 当前场景：${activeScenario}`
    : `当前场景：${activeScenario} · ${sectionDescriptions[primaryNav.to]}`;

  return (
    <div className="app-shell">
      <aside className="app-sidebar">
        <div className="app-sidebar__logo" title="NS3 Factory 水下数字孪生">N3</div>

        <nav className="app-nav">
          {navigation.map((item) => (
            <NavLink
              key={item.to}
              to={item.to}
              end={item.end}
              title={item.label}
              className={({ isActive }) => {
                const groupActive = item.to === '/studio' && inWorkbench;
                return isActive || groupActive ? 'app-nav__link app-nav__link--active' : 'app-nav__link';
              }}
            >
              {item.icon}
            </NavLink>
          ))}
        </nav>

        <div className="app-sidebar__foot" title={`${activeScenario} · v${version}`}>
          ◉
        </div>
      </aside>

      <div className="app-main">
        <header className="app-header">
          <div className="app-header__page">
            <p className="eyebrow" style={{ margin: 0, fontSize: 11 }}>当前页面</p>
            <h2 style={{ margin: 0, fontSize: 16, fontWeight: 600 }}>{currentPageTitle}</h2>
            <p className="app-header__subtitle">{currentPageDescription}</p>
            {inWorkbench ? (
              <div className="app-header__tabs" aria-label="试验编排与仿真工作台导航">
                {workbenchNavigation.map((item) => (
                  <NavLink
                    key={item.to}
                    to={item.to}
                    className={({ isActive }) => isActive ? 'app-header__tab app-header__tab--active' : 'app-header__tab'}
                  >
                    {item.label}
                  </NavLink>
                ))}
              </div>
            ) : null}
          </div>

          <div className="app-header__statusbar">
            <span className="status-pill status-pill--ready">后端已连接</span>
            <span className="status-pill">
              {meta?.project_tags?.length ? meta.project_tags.join(' · ') : '仿真平台'}
            </span>
            <span className="status-pill">v{version}</span>
          </div>
        </header>

        <main className="app-content">
          <Outlet context={ctx} />
        </main>
      </div>
    </div>
  );
}