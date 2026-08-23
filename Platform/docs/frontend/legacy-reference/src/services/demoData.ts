import type { DemoDataset, LinkMetric } from '../types';
import { fetchResults, fetchScenario, fetchScenarios } from './api';

const DEFAULT_SCENARIO = 'star_monitoring_demo_simple';

/**
 * 从后端 API 加载指定场景及该场景最近一次结果。
 * 不传 scenarioName 时自动发现：优先用默认场景，否则取列表第一项。
 */
export async function loadDemoDataset(scenarioName?: string): Promise<DemoDataset & { scenarioName: string }> {
  let name = scenarioName ?? DEFAULT_SCENARIO;

  if (!scenarioName) {
    try {
      const names = await fetchScenarios();
      if (names.length > 0 && !names.includes(DEFAULT_SCENARIO)) {
        name = names[0];
      }
    } catch {
      // 后端尚未就绪，继续尝试默认名称
    }
  }

  const [scenario, resultsResp] = await Promise.all([
    fetchScenario(name),
    fetchResults(name).catch((): { file: null; rows: LinkMetric[] } => ({
      file: null,
      rows: [],
    })),
  ]);

  return { scenario, metrics: resultsResp.rows, scenarioName: name };
}