import { useMemo, useRef } from 'react';
import { Canvas, useFrame } from '@react-three/fiber';
import { OrbitControls, Text, Line } from '@react-three/drei';
import * as THREE from 'three';
import type { CommunicationEvent, DemoNode, BathymetryData, LinkRays, StudioEnvironmentBounds } from '../types';

/* ─── Props ─── */
export interface Scene3DViewProps {
  nodes: DemoNode[];
  bathymetry?: BathymetryData;
  rays: LinkRays[];
  waterDepth?: number;
  environmentBounds?: StudioEnvironmentBounds;
  showCommunicationRange?: boolean;
  selectedNodeId?: number | null;
  selectedEdgeKey: string | null;
  focusedEdgeKey?: string | null;
  highlightedEdgeKeys?: string[];
  currentEvents?: CommunicationEvent[];
  onSelectEdge?: (key: string) => void;
}

/* ─── Colour helpers ─── */
const ROLE_COLOR: Record<string, string> = {
  sink: '#22d3ee',
  anchor: '#22d3ee',
  sensor: '#f59e0b',
  relay: '#34d399',
  hil: '#f472b6',
  gateway: '#f472b6',
  uuv: '#34d399',
};
const roleColor = (role: string) => ROLE_COLOR[role] ?? '#94a3b8';

/* ─── Water surface plane ─── */
function WaterSurface({ size }: { size: number }) {
  // Centred at the midpoint of node X range, at Y=0 (sea surface), Z=0
  return (
    <mesh position={[size / 2, 0, 0]} rotation={[-Math.PI / 2, 0, 0]}>
      <planeGeometry args={[size * 1.4, size * 0.4]} />
      <meshStandardMaterial color="#0ea5e9" transparent opacity={0.15} side={THREE.DoubleSide} />
    </mesh>
  );
}

/* ─── Terrain mesh from bathymetry ─── */
/*
 * The bathymetry profile (range_m → depth_m) describes the sea-floor along the
 * main transect (X axis — same direction as node positions).  A ridge at
 * range ≈ 1500 m should form a wall *perpendicular* to the node line so that
 * it blocks LOS between neighbouring nodes.
 *
 * Geometry: for each range sample we extrude a strip of vertices along Z,
 * centred around Z = 0 (where the nodes sit), producing a terrain surface
 * that stretches across the scene.
 */
function TerrainMesh({ bathymetry, maxRange, nodeSpreadZ }: {
  bathymetry: BathymetryData;
  maxRange: number;
  nodeSpreadZ: number;
}) {
  const geometry = useMemo(() => {
    const { range_m, depth_m } = bathymetry;
    const n = range_m.length;
    if (n < 2) return undefined;

    // Half-width of terrain in Z direction — at least 600 or 1.5× the spread
    // of node Y-positions (mapped to Z in Three.js)
    const halfZ = Math.max(maxRange * 0.12, nodeSpreadZ * 1.5, 300);

    const positions: number[] = [];
    const indices: number[] = [];

    // Two vertices per range sample: front (−Z) and back (+Z)
    for (let i = 0; i < n; i++) {
      const x = range_m[i];
      const y = -depth_m[i]; // depth → negative Y in Three.js
      positions.push(x, y, -halfZ);
      positions.push(x, y, halfZ);
    }

    // Top surface triangles
    for (let i = 0; i < n - 1; i++) {
      const a = i * 2, b = a + 1, c = a + 2, d = a + 3;
      indices.push(a, c, b);
      indices.push(b, c, d);
    }

    // Front skirt (Z = −halfZ), connecting terrain surface down to max depth
    const maxD = Math.max(...depth_m) * 1.15;
    const skirtBase = positions.length / 3;
    for (let i = 0; i < n; i++) {
      positions.push(range_m[i], -maxD, -halfZ);
    }
    for (let i = 0; i < n - 1; i++) {
      const t = i * 2;                // top-front vertex
      const s = skirtBase + i;        // skirt vertex
      indices.push(t, s, s + 1);
      indices.push(t, s + 1, t + 2);
    }

    // Back skirt (Z = +halfZ)
    const skirtBase2 = positions.length / 3;
    for (let i = 0; i < n; i++) {
      positions.push(range_m[i], -maxD, halfZ);
    }
    for (let i = 0; i < n - 1; i++) {
      const t = i * 2 + 1;            // top-back vertex
      const s = skirtBase2 + i;
      indices.push(t, s + 1, s);
      indices.push(t, t + 2, s + 1);  // note: reversed winding vs front
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
    geo.setIndex(indices);
    geo.computeVertexNormals();
    return geo;
  }, [bathymetry, maxRange, nodeSpreadZ]);

  if (!geometry) return null;
  return (
    <mesh geometry={geometry}>
      <meshStandardMaterial color="#8b6e3c" transparent opacity={0.7} side={THREE.DoubleSide} />
    </mesh>
  );
}

/* ─── Animated node sphere ─── */
function NodeSphere({ node, scale }: { node: DemoNode; scale: number }) {
  const ref = useRef<THREE.Group>(null);
  const color = roleColor(node.role);
  useFrame(({ clock }) => {
    if (ref.current) {
      ref.current.scale.setScalar(1 + 0.05 * Math.sin(clock.getElapsedTime() * 2 + node.id));
    }
  });
  const [x, y, z] = node.position;
  return (
    <group position={[x, z, y]}>
      <group ref={ref}>
        <mesh>
          <sphereGeometry args={[scale, 18, 18]} />
          <meshStandardMaterial color={color} emissive={color} emissiveIntensity={0.35} transparent opacity={0.96} />
        </mesh>
        <Text position={[0, scale * 2.5, 0]} fontSize={scale * 1.8} color="#e2e8f0" anchorX="center" anchorY="bottom">
          {`N${node.id} (${node.role})`}
        </Text>
        <Text position={[0, -scale * 2, 0]} fontSize={scale * 1.2} color="#94a3b8" anchorX="center" anchorY="top">
          {`z=${z}m`}
        </Text>
      </group>
    </group>
  );
}

function CommunicationRangeShell({ node }: { node: DemoNode }) {
  const [x, y, z] = node.position;
  const range = Math.max(node.communication_range_m ?? 1800, 50);
  const color = roleColor(node.role);
  return (
    <group position={[x, z, y]}>
      <mesh>
        <sphereGeometry args={[range, 20, 20]} />
        <meshBasicMaterial color={color} transparent opacity={0.08} wireframe depthWrite={false} />
      </mesh>
    </group>
  );
}

/* ─── Ray path line ─── */
function RayPath({ link, txNode, rxNode, selected, emphasized, activeEvent, nodeScale, maxDepth, onClick }: {
  link: LinkRays;
  txNode: DemoNode;
  rxNode: DemoNode;
  selected: boolean;
  emphasized: boolean;
  activeEvent: boolean;
  nodeScale: number;
  maxDepth: number;
  onClick: () => void;
}) {
  const isNlos = link.is_nlos !== 0;
  const [tx, ty, tz] = txNode.position;
  const [rx, ry, rz] = rxNode.position;

  // Direct link line
  const directColor = activeEvent ? (isNlos ? '#fb7185' : '#22d3ee') : isNlos ? '#ef4444' : '#22c55e';
  const directPoints: [number, number, number][] = [[tx, tz, ty], [rx, rz, ry]];

  // Build multipath ray traces (simplified 3D visualisation)
  const rayLines = useMemo(() => {
    return link.rays.slice(0, 8).map((ray, idx) => {
      const sb = ray.surface_bounces;
      const bb = ray.bottom_bounces;
      if (sb === 0 && bb === 0) return null; // direct ray shown separately
      const pts: [number, number, number][] = [];
      pts.push([tx, tz, ty]);

      const totalBounces = sb + bb;
      // Simple interpolation with bounces for visualisation
      for (let b = 1; b <= totalBounces; b++) {
        const frac = b / (totalBounces + 1);
        const mx = tx + (rx - tx) * frac;
        const mz = ty + (ry - ty) * frac;
        // alternate between surface (0) and bottom bounce (negative)
        const isSurface = b % 2 === 1 && sb > 0;
        const bounceDepth = isSurface ? 0 : -maxDepth;
        pts.push([mx, bounceDepth, mz]);
      }
      pts.push([rx, rz, ry]);

      const color = isNlos ? '#f97316' : '#60a5fa';
      return { pts, color, idx };
    }).filter(Boolean) as { pts: [number, number, number][]; color: string; idx: number }[];
  }, [isNlos, link, maxDepth, rx, ry, rz, tx, ty, tz]);

  return (
    <group onClick={(e) => { e.stopPropagation(); onClick(); }}>
      {/* Direct link */}
      <Line
        points={directPoints}
        color={directColor}
        lineWidth={selected ? 3 : activeEvent ? 2.4 : emphasized ? 2 : 1.5}
        opacity={selected ? 1 : activeEvent ? 0.9 : emphasized ? 0.72 : 0.38}
        transparent
        dashed={isNlos}
        dashSize={nodeScale}
        dashScale={3}
      />
      {/* NLOS marker */}
      {isNlos && (
        <group position={[(tx + rx) / 2, (tz + rz) / 2, (ty + ry) / 2]}>
          <Text fontSize={nodeScale * 2} color="#ef4444" anchorX="center" anchorY="middle">
            NLOS
          </Text>
        </group>
      )}
      {/* Multipath rays */}
      {(selected || emphasized || activeEvent) && rayLines.map(({ pts, color, idx }) => (
        <Line
          key={idx}
          points={pts}
          color={color}
          lineWidth={selected ? 1.2 : 1}
          opacity={selected ? 0.65 : activeEvent ? 0.58 : 0.45}
          transparent
          dashed
          dashSize={nodeScale * 0.5}
          dashScale={5}
        />
      ))}
    </group>
  );
}

/* ─── Depth scale bar ─── */
function DepthBar({ maxDepth, scale }: { maxDepth: number; scale: number }) {
  const steps = 5;
  return (
    <group position={[-scale * 5, 0, -scale * 5]}>
      <Line
        points={[[0, 0, 0], [0, -maxDepth, 0]]}
        color="#475569"
        lineWidth={1}
      />
      {Array.from({ length: steps + 1 }, (_, i) => {
        const d = (maxDepth / steps) * i;
        return (
          <Text key={i} position={[-scale * 2, -d, 0]} fontSize={scale * 1.2} color="#94a3b8" anchorX="right" anchorY="middle">
            {`${d.toFixed(0)}m`}
          </Text>
        );
      })}
    </group>
  );
}

/* ─── Main 3D scene ─── */
function SceneContents({
  nodes,
  bathymetry,
  rays,
  waterDepth,
  environmentBounds,
  showCommunicationRange = true,
  selectedNodeId,
  selectedEdgeKey,
  focusedEdgeKey,
  highlightedEdgeKeys = [],
  currentEvents = [],
  onSelectEdge,
}: Scene3DViewProps) {
  const nodeById = useMemo(() => new Map(nodes.map((node) => [node.id, node])), [nodes]);
  // Compute scene bounds
  const { maxRange, maxDepth, centerX, centerZ, nodeSpreadZ, nodeScale } = useMemo(() => {
    const xs = nodes.map((n) => n.position[0]);
    const ys = nodes.map((n) => n.position[1]); // Y in data → Z in Three.js
    const zs = nodes.map((n) => Math.abs(n.position[2]));
    const maxX = Math.max(...xs, 1);
    const minX = Math.min(...xs, 0);
    const maxY = Math.max(...ys, 0);
    const minY = Math.min(...ys, 0);
    const rangeX = Math.max(maxX - minX, environmentBounds?.rangeMax ?? 0, 100);
    const spreadZ = maxY - minY; // lateral spread of nodes in Three.js Z
    const maxD = environmentBounds?.depthMax ?? Math.max(...zs, waterDepth ?? 100);
    const cx = (minX + maxX) / 2;
    const cz = (minY + maxY) / 2;
    // Node sphere radius: small relative to deployment, avoid visually covering the terrain.
    const sc = Math.min(Math.max((maxX - minX || 100) / 320, 3.2), 22);
    return {
      maxRange: rangeX,
      maxDepth: maxD,
      centerX: cx,
      centerZ: cz,
      nodeSpreadZ: spreadZ,
      nodeScale: sc,
    };
  }, [environmentBounds, nodes, waterDepth]);

  const validRays = useMemo(
    () => rays.filter((link) => nodeById.has(link.tx_id) && nodeById.has(link.rx_id)),
    [nodeById, rays],
  );

  const highlightedEdgeSet = useMemo(
    () => new Set(highlightedEdgeKeys.filter(Boolean)),
    [highlightedEdgeKeys],
  );

  const activeEventEdgeSet = useMemo(
    () => new Set(currentEvents.map((event) => event.edgeKey)),
    [currentEvents],
  );

  const highlightedNodeIds = useMemo(() => {
    const ids = new Set<number>();
    if (typeof selectedNodeId === 'number') {
      ids.add(selectedNodeId);
    }
    const edgeForHighlight = selectedEdgeKey ?? focusedEdgeKey;
    if (edgeForHighlight) {
      const [txId, rxId] = edgeForHighlight.split('-').map((value) => Number.parseInt(value, 10));
      if (Number.isFinite(txId)) ids.add(txId);
      if (Number.isFinite(rxId)) ids.add(rxId);
    }
    if (ids.size === 0) {
      nodes.filter((node) => node.role === 'sink').forEach((node) => ids.add(node.id));
    }
    return ids;
  }, [focusedEdgeKey, nodes, selectedEdgeKey, selectedNodeId]);

  return (
    <>
      <ambientLight intensity={0.5} />
      <directionalLight position={[centerX + maxRange, maxRange * 0.3, maxRange * 0.4]} intensity={0.8} />
      <pointLight position={[centerX, 0, centerZ]} intensity={0.3} />

      {/* Water surface at Y = 0 centred on node area */}
      <WaterSurface size={maxRange} />

      {/* Depth Reference */}
      <DepthBar maxDepth={maxDepth} scale={nodeScale} />

      {/* Terrain: ridge perpendicular to node-deployment X axis */}
      {bathymetry && (
        <TerrainMesh bathymetry={bathymetry} maxRange={maxRange} nodeSpreadZ={nodeSpreadZ} />
      )}

      {showCommunicationRange && nodes.filter((node) => highlightedNodeIds.has(node.id)).map((node) => (
        <CommunicationRangeShell key={`range-${node.id}`} node={node} />
      ))}

      {/* Nodes */}
      {nodes.map((node) => (
        <NodeSphere key={node.id} node={node} scale={nodeScale} />
      ))}

      {/* Links / Rays */}
      {validRays.map((link) => {
        const key = `${link.tx_id}-${link.rx_id}`;
        const txNode = nodeById.get(link.tx_id);
        const rxNode = nodeById.get(link.rx_id);
        if (!txNode || !rxNode) return null;
        return (
          <RayPath
            key={key}
            link={link}
            txNode={txNode}
            rxNode={rxNode}
            selected={selectedEdgeKey === key || focusedEdgeKey === key}
            emphasized={highlightedEdgeSet.has(key)}
            activeEvent={activeEventEdgeSet.has(key)}
            nodeScale={nodeScale}
            maxDepth={maxDepth}
            onClick={() => onSelectEdge?.(key)}
          />
        );
      })}

      {/* If no rays data, show edges from pairs */}
      {validRays.length === 0 && nodes.length > 1 && (
        <group>
          {nodes.map((a, i) =>
            nodes.slice(i + 1).map((b) => {
              const [ax, ay, az] = a.position;
              const [bx, by, bz] = b.position;
              const key = `${a.id}-${b.id}`;
              const highlighted = highlightedEdgeSet.has(key) || activeEventEdgeSet.has(key);
              return (
                <Line
                  key={key}
                  points={[[ax, az, ay], [bx, bz, by]]}
                  color={highlighted ? '#22d3ee' : '#475569'}
                  lineWidth={highlighted ? 2 : 1}
                  opacity={highlighted ? 0.72 : 0.3}
                  transparent
                />
              );
            }),
          )}
        </group>
      )}

      <OrbitControls
        target={[centerX, -maxDepth / 3, centerZ]}
        maxPolarAngle={Math.PI * 0.85}
        enableDamping
        dampingFactor={0.1}
      />
      <gridHelper
        args={[maxRange * 1.4, 20, '#1e293b', '#0f172a']}
        position={[centerX, -maxDepth * 1.1, centerZ]}
      />
    </>
  );
}

export default function Scene3DView(props: Scene3DViewProps) {
  return (
    <div style={{ width: '100%', height: '100%', background: '#040a14' }}>
      <Canvas
        camera={{
          position: [4000, 600, 2500],
          fov: 50,
          near: 1,
          far: 100000,
        }}
        style={{ width: '100%', height: '100%' }}
      >
        <SceneContents {...props} />
      </Canvas>
    </div>
  );
}
