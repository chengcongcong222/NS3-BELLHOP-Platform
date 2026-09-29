export interface AcquisitionProgress {
  stage: string;
  message: string;
  dataset?: string;
  file_index?: number;
  file_count?: number;
  received_bytes?: number;
  total_bytes?: number | null;
}

/** Authoring download progress, not simulation events or simulation time. */
export async function acquireWithProgress(url: string, request: Record<string, unknown>,
  progress: (value: AcquisitionProgress) => void, signal: AbortSignal): Promise<Record<string, any>> {
  const response = await fetch(url, { method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify(request), signal });
  if (!response.ok) {
    if (response.status === 404) throw new Error("当前连接的后端版本过旧，尚不支持带进度的水文获取。请使用更新后的系统入口。");
    const body = await response.json().catch(() => ({}));
    throw new Error(body.error?.message ?? "水文参数或服务请求失败，请检查经纬度、月份与网络连接。");
  }
  if (!response.body) throw new Error("水文服务没有返回数据流，请重试。");
  const reader = response.body.getReader();
  const decoder = new TextDecoder();
  let pending = "";
  try {
    while (true) {
      const { done, value } = await reader.read();
      pending += decoder.decode(value, { stream: !done });
      const lines = pending.split("\n");
      pending = lines.pop()!;
      for (const line of lines) {
        if (!line.trim()) continue;
        const event = JSON.parse(line);
        if (event.type === "progress") progress(event);
        else if (event.type === "error") throw new Error(event.message);
        else if (event.type === "result") {
          if (!Array.isArray(event.result?.sound_speed_profile) || event.result.sound_speed_profile.length < 2)
            throw new Error("水文服务返回的剖面不完整，未替换现有数据。");
          return event.result;
        }
      }
      if (done) throw new Error("水文连接已中断，尚未收到完整剖面。请重试，已完成的下载可复用。");
    }
  } finally {
    await reader.cancel().catch(() => {});
    reader.releaseLock();
  }
}
