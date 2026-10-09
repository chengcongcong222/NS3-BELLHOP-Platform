import { useEffect, useRef, useState } from "react";
import { createPortal } from "react-dom";
import type { TrajectoryAssetDto } from "../api/types";
import { TrajectoryAuthoringPage } from "../features/trajectoryBuilder";

/** A view of the same server-backed authoring API, never a separate resource store. */
export function TrajectoryDialog({ onClose, onChoose, initialPosition, nodeId }: {
  onClose: () => void;
  onChoose: (asset: TrajectoryAssetDto, trackId: string) => void;
  initialPosition: { x_meters: number; y_meters: number; z_meters: number };
  nodeId: string;
}) {
  const ref = useRef<HTMLDialogElement>(null);
  const [previous] = useState(() => document.activeElement as HTMLElement | null);
  useEffect(() => {
    const dialog = ref.current;
    if (dialog?.showModal) dialog.showModal(); else dialog?.setAttribute("open", "");
    return () => { dialog?.close?.(); previous?.focus(); };
  }, [previous]);
  return createPortal(<dialog ref={ref} className="trajectory-dialog" aria-label="节点轨迹" onCancel={event => { event.preventDefault(); onClose(); }}>
    <div className="trajectory-dialog-heading"><strong>N{nodeId} · 节点运动路径</strong><button type="button" autoFocus onClick={onClose}>关闭</button></div>
    <TrajectoryAuthoringPage embedded initialPosition={initialPosition} onChoose={onChoose} />
  </dialog>, document.body);
}
