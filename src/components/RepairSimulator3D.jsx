import React, { useEffect, useRef, useState, useMemo } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';

export default function RepairSimulator3D(props) {
  const {
    imageData, defects, selectedDefectId, scanPathResult, doseMatrixResult
  } = props;

  const containerRef = useRef(null);
  const sceneRef = useRef(null);
  const rendererRef = useRef(null);
  const cameraRef = useRef(null);
  const controlsRef = useRef(null);
  const animFrameRef = useRef(null);
  const objectsRef = useRef({});
  const [simPlaying, setSimPlaying] = useState(false);
  const [beamIndex, setBeamIndex] = useState(0);
  const [simSpeed, setSimSpeed] = useState(10);

  const allCommands = useMemo(() => {
    return doseMatrixResult?.commands || [];
  }, [doseMatrixResult]);

  useEffect(() => {
    if (!containerRef.current) return;

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x06090f);
    scene.fog = new THREE.Fog(0x06090f, 2000, 8000);
    sceneRef.current = scene;

    const camera = new THREE.PerspectiveCamera(45, 1, 0.1, 100000);
    cameraRef.current = camera;

    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    containerRef.current.appendChild(renderer.domElement);
    rendererRef.current = renderer;

    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.05;
    controls.minDistance = 10;
    controls.maxDistance = 10000;
    controlsRef.current = controls;

    const ambient = new THREE.AmbientLight(0xffffff, 0.35);
    scene.add(ambient);

    const dirLight = new THREE.DirectionalLight(0xffffff, 0.9);
    dirLight.position.set(1000, 1500, 800);
    dirLight.castShadow = true;
    dirLight.shadow.mapSize.width = 2048;
    dirLight.shadow.mapSize.height = 2048;
    dirLight.shadow.camera.left = -2000;
    dirLight.shadow.camera.right = 2000;
    dirLight.shadow.camera.top = 2000;
    dirLight.shadow.camera.bottom = -2000;
    scene.add(dirLight);

    const rimLight = new THREE.DirectionalLight(0x00d4ff, 0.3);
    rimLight.position.set(-800, 600, -1000);
    scene.add(rimLight);

    const fillLight = new THREE.PointLight(0xffaa00, 0.4, 5000);
    fillLight.position.set(0, 800, 0);
    scene.add(fillLight);

    const handleResize = () => {
      if (!containerRef.current) return;
      const { clientWidth, clientHeight } = containerRef.current;
      renderer.setSize(clientWidth, clientHeight);
      camera.aspect = clientWidth / clientHeight;
      camera.updateProjectionMatrix();
    };
    handleResize();
    const resizeObs = new ResizeObserver(handleResize);
    resizeObs.observe(containerRef.current);

    const animate = () => {
      animFrameRef.current = requestAnimationFrame(animate);
      controls.update();
      renderer.render(scene, camera);
    };
    animate();

    return () => {
      cancelAnimationFrame(animFrameRef.current);
      resizeObs.disconnect();
      controls.dispose();
      renderer.dispose();
      if (containerRef.current && renderer.domElement.parentNode === containerRef.current) {
        containerRef.current.removeChild(renderer.domElement);
      }
    };
  }, []);

  useEffect(() => {
    const scene = sceneRef.current;
    if (!scene) return;

    Object.values(objectsRef.current).forEach(obj => scene.remove(obj));
    objectsRef.current = {};
    setBeamIndex(0);

    const w = imageData?.width || 2000;
    const h = imageData?.height || 2000;
    const cx = w / 2;
    const cy = h / 2;
    const scale = Math.min(1, 1000 / Math.max(w, h));

    const gridHelper = new THREE.GridHelper(Math.max(w, h) * scale * 1.1, 20, 0x003355, 0x001833);
    gridHelper.rotation.x = Math.PI / 2;
    gridHelper.position.z = -0.5;
    scene.add(gridHelper);
    objectsRef.current.grid = gridHelper;

    const baseGeom = new THREE.BoxGeometry(w * scale, h * scale, 1);
    const baseMat = new THREE.MeshStandardMaterial({
      color: 0x1a2332,
      metalness: 0.8,
      roughness: 0.4,
      transparent: true,
      opacity: 0.9
    });
    const baseMesh = new THREE.Mesh(baseGeom, baseMat);
    baseMesh.position.z = -0.5;
    baseMesh.receiveShadow = true;
    scene.add(baseMesh);
    objectsRef.current.base = baseMesh;

    const axesHelper = new THREE.AxesHelper(50);
    axesHelper.position.set(-cx * scale - 30, -cy * scale - 30, 0.5);
    scene.add(axesHelper);
    objectsRef.current.axes = axesHelper;

    if (imageData?.canvas || imageData?.src) {
      try {
        const texSrc = imageData.src || imageData.canvas.toDataURL();
        const loader = new THREE.TextureLoader();
        loader.crossOrigin = 'anonymous';
        loader.load(texSrc, (texture) => {
          texture.anisotropy = 8;
          texture.needsUpdate = true;
          const imgGeom = new THREE.PlaneGeometry(w * scale, h * scale);
          const imgMat = new THREE.MeshBasicMaterial({
            map: texture,
            transparent: true,
            opacity: 0.88
          });
          const imgMesh = new THREE.Mesh(imgGeom, imgMat);
          imgMesh.position.z = 0;
          scene.add(imgMesh);
          objectsRef.current.image = imgMesh;
        });
      } catch (e) { console.warn('Texture load failed', e); }
    }

    if (defects?.length) {
      const defectsGroup = new THREE.Group();
      
      for (const d of defects) {
        if (!d.points || d.points.length < 3) continue;
        const isSel = d.id === selectedDefectId;
        const isOpaque = d.type === 'opaque';

        const shape = new THREE.Shape();
        const p0 = d.points[0];
        shape.moveTo((p0.x - cx) * scale, (p0.y - cy) * scale);
        for (let i = 1; i < d.points.length; i++) {
          shape.lineTo((d.points[i].x - cx) * scale, (d.points[i].y - cy) * scale);
        }

        const etchDepth = isOpaque ? 12 : 8;
        const extrudeSettings = {
          depth: isSel ? etchDepth * 1.3 : etchDepth,
          bevelEnabled: true,
          bevelThickness: 0.5,
          bevelSize: 0.3,
          bevelSegments: 2,
          curveSegments: 8
        };

        try {
          const geom = new THREE.ExtrudeGeometry(shape, extrudeSettings);
          const color = isOpaque ? (isSel ? 0xff3b3b : 0xff6b6b) : (isSel ? 0x20e0d2 : 0x4ecdc4);
          const mat = new THREE.MeshStandardMaterial({
            color,
            transparent: true,
            opacity: isSel ? 0.85 : 0.6,
            emissive: color,
            emissiveIntensity: isSel ? 0.35 : 0.12,
            metalness: 0.3,
            roughness: 0.5
          });
          const mesh = new THREE.Mesh(geom, mat);
          mesh.rotation.x = Math.PI;
          mesh.position.z = isOpaque ? extrudeSettings.depth / 2 + 1 : -extrudeSettings.depth / 2;
          mesh.castShadow = true;
          mesh.receiveShadow = true;
          mesh.userData.defectId = d.id;
          defectsGroup.add(mesh);

          const edges = new THREE.EdgesGeometry(geom);
          const lineMat = new THREE.LineBasicMaterial({
            color: isOpaque ? 0xff8a8a : 0x8ef0e8,
            transparent: true,
            opacity: isSel ? 1 : 0.7
          });
          const wire = new THREE.LineSegments(edges, lineMat);
          wire.rotation.x = Math.PI;
          wire.position.z = mesh.position.z;
          defectsGroup.add(wire);

          if (isSel && d.centroid) {
            const markerGeom = new THREE.SphereGeometry(3, 16, 16);
            const markerMat = new THREE.MeshBasicMaterial({ color: 0xffff00 });
            const marker = new THREE.Mesh(markerGeom, markerMat);
            marker.position.set((d.centroid.x - cx) * scale, -(d.centroid.y - cy) * scale, isOpaque ? etchDepth + 3 : -etchDepth - 3);
            defectsGroup.add(marker);
          }
        } catch (e) {
          console.warn('Extrude failed for defect', d.id, e);
        }
      }

      scene.add(defectsGroup);
      objectsRef.current.defects = defectsGroup;
    }

    if (scanPathResult?.scanLines?.length) {
      const scanGroup = new THREE.Group();

      for (const line of scanPathResult.scanLines) {
        if (!line.points || line.points.length < 2) continue;
        const pts = [];
        const zBase = 4 + (line.direction === 1 ? 0 : 2);
        for (const p of line.points) {
          pts.push(new THREE.Vector3(
            (p.x - cx) * scale,
            -(p.y - cy) * scale,
            zBase
          ));
        }
        const geom = new THREE.BufferGeometry().setFromPoints(pts);
        const lineColor = line.direction === 1 ? 0xffab00 : 0x00e5ff;
        const lineMat = new THREE.LineBasicMaterial({
          color: lineColor,
          transparent: true,
          opacity: 0.75
        });
        const lineMesh = new THREE.Line(geom, lineMat);
        scanGroup.add(lineMesh);
      }
      scene.add(scanGroup);
      objectsRef.current.scanPath = scanGroup;
    }

    if (cameraRef.current && controlsRef.current) {
      const dist = Math.max(w, h) * scale * 1.3;
      cameraRef.current.position.set(0, -dist * 0.85, dist * 1.1);
      cameraRef.current.lookAt(0, 0, 0);
      controlsRef.current.target.set(0, 0, 0);
      controlsRef.current.update();
    }
  }, [imageData, defects, selectedDefectId, scanPathResult]);

  useEffect(() => {
    if (!simPlaying || !sceneRef.current || allCommands.length === 0) {
      if (objectsRef.current.effect) {
        sceneRef.current.remove(objectsRef.current.effect);
        sceneRef.current.remove(objectsRef.current.beamLight);
        delete objectsRef.current.effect;
        delete objectsRef.current.beamLight;
      }
      return;
    }

    const scene = sceneRef.current;
    const w = imageData?.width || 2000;
    const h = imageData?.height || 2000;
    const cx = w / 2;
    const cy = h / 2;
    const scale = Math.min(1, 1000 / Math.max(w, h));

    const beamGeom = new THREE.ConeGeometry(3, 25, 12);
    const beamMat = new THREE.MeshBasicMaterial({
      color: 0x00d4ff,
      transparent: true,
      opacity: 0.75
    });
    const beamMesh = new THREE.Mesh(beamGeom, beamMat);
    beamMesh.rotation.x = Math.PI;
    scene.add(beamMesh);
    objectsRef.current.effect = beamMesh;

    const beamLight = new THREE.PointLight(0x00ffff, 2.5, 200);
    scene.add(beamLight);
    objectsRef.current.beamLight = beamLight;

    const exposureGeom = new THREE.SphereGeometry(0.8, 8, 8);
    const exposureMat = new THREE.MeshBasicMaterial({
      color: 0xffff00,
      transparent: true,
      opacity: 0.6
    });
    const exposures = [];

    let idx = beamIndex;
    let cancelled = false;
    const step = () => {
      if (cancelled || !simPlaying || !sceneRef.current) return;
      
      for (let s = 0; s < simSpeed && idx < allCommands.length; s++, idx++) {
        const c = allCommands[idx];
        const px = (c.x - cx) * scale;
        const py = -(c.y - cy) * scale;
        const z = 18;

        beamMesh.position.set(px, py, z + 12.5);
        beamLight.position.set(px, py, z);

        const doseFactor = Math.min(1, (c.dwellTime || 500) / 2000);
        beamMesh.scale.setScalar(0.6 + doseFactor * 0.8);
        beamLight.intensity = 1.5 + doseFactor * 3;
        beamLight.distance = 80 + doseFactor * 150;

        if ((idx % 3) === 0) {
          const exp = new THREE.Mesh(exposureGeom, exposureMat.clone());
          exp.position.set(px, py, z * 0.2);
          const expSize = 0.6 + (c.dwellTime || 500) / 2000 * 1.5;
          exp.scale.setScalar(expSize);
          exp.userData.born = Date.now();
          scene.add(exp);
          exposures.push(exp);
        }

        for (let i = exposures.length - 1; i >= 0; i--) {
          const e = exposures[i];
          const age = Date.now() - e.userData.born;
          if (age > 1200) {
            scene.remove(e);
            e.geometry.dispose();
            e.material.dispose();
            exposures.splice(i, 1);
          } else {
            const t = age / 1200;
            e.material.opacity = 0.6 * (1 - t);
            e.scale.multiplyScalar(1.008);
          }
        }
      }

      setBeamIndex(idx);

      if (idx >= allCommands.length) {
        setSimPlaying(false);
        return;
      }

      setTimeout(step, 16);
    };
    step();

    return () => {
      cancelled = true;
      for (const e of exposures) {
        scene.remove(e);
        e.geometry?.dispose();
        e.material?.dispose();
      }
      exposures.length = 0;
    };
  }, [simPlaying, allCommands, simSpeed, imageData, beamIndex]);

  const resetSim = () => {
    setSimPlaying(false);
    setBeamIndex(0);
  };

  const progressPct = allCommands.length > 0 ? (beamIndex / allCommands.length) * 100 : 0;

  return (
    <div style={{ flex: 1, display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
      <div className="viewer-toolbar">
        <div className="toolbar-group">
          <button
            className={`tool-btn ${simPlaying ? 'active' : ''}`}
            onClick={() => setSimPlaying(v => !v)}
            title={simPlaying ? '暂停' : '播放模拟'}
            disabled={allCommands.length === 0}
          >
            {simPlaying ? '⏸' : '▶'}
          </button>
          <button className="tool-btn" onClick={resetSim} title="重置">⏹</button>
          <select
            className="form-select"
            style={{ width: 90, height: 32, fontSize: 11 }}
            value={simSpeed}
            onChange={e => setSimSpeed(parseInt(e.target.value))}
          >
            <option value={1}>×1</option>
            <option value={5}>×5</option>
            <option value={10}>×10</option>
            <option value={30}>×30</option>
            <option value={100}>×100</option>
          </select>
        </div>
        <div className="toolbar-group">
          <div style={{ padding: '0 12px', fontSize: 11, fontFamily: 'Consolas,monospace', color: 'var(--text-muted)' }}>
            {allCommands.length > 0 ? (
              <>
                指令进度: <span style={{ color: 'var(--accent-primary)' }}>{beamIndex.toLocaleString()}</span>
                /{allCommands.length.toLocaleString()}
                &nbsp;·&nbsp;
                <span style={{ color: progressPct >= 100 ? 'var(--success)' : 'var(--warning)' }}>
                  {progressPct.toFixed(1)}%
                </span>
              </>
            ) : (
              <>剂量矩阵为空，无法模拟</>
            )}
          </div>
        </div>
        <div className="toolbar-group">
          <span style={{ fontSize: 10, color: 'var(--text-muted)' }}>🖱️ 左键旋转 | 滚轮缩放 | 右键平移</span>
        </div>
      </div>

      <div ref={containerRef} className="three-container" style={{ flex: 1 }}></div>

      {allCommands.length > 0 && (
        <div style={{
          padding: '8px 12px',
          borderTop: '1px solid var(--border-color)',
          background: 'var(--bg-secondary)'
        }}>
          <div className="progress-bar" style={{ height: 4, marginBottom: 6 }}>
            <div className="progress-fill" style={{ width: progressPct + '%' }}></div>
          </div>
          <div style={{
            display: 'flex',
            justifyContent: 'space-between',
            fontSize: 10,
            color: 'var(--text-muted)',
            fontFamily: 'Consolas,monospace'
          }}>
            <span>E-Beam 修复模拟进度</span>
            <span>{beamIndex.toLocaleString()} / {allCommands.length.toLocaleString()} 指令 ({progressPct.toFixed(1)}%)</span>
          </div>
        </div>
      )}

      <div style={{ padding: '12px 16px', borderTop: '1px solid var(--border-color)' }}>
        <div className="legend">
          <div className="legend-item">
            <span className="legend-swatch opaque"></span>
            <span><b style={{ color: 'var(--opaque)' }}>Opaque</b> - 多余金属突起 (+Z)</span>
          </div>
          <div className="legend-item">
            <span className="legend-swatch clear"></span>
            <span><b style={{ color: 'var(--clear)' }}>Clear</b> - 金属缺失凹陷 (-Z)</span>
          </div>
          <div className="legend-item">
            <span className="legend-swatch scan"></span>
            <span><b style={{ color: 'var(--warning)' }}>Raster Path</b> - 往复扫描路径</span>
          </div>
          <div className="legend-item">
            <span className="legend-swatch beam"></span>
            <span><b style={{ color: 'var(--accent-primary)' }}>E-Beam</b> - 电子束曝光锥</span>
          </div>
        </div>
      </div>
    </div>
  );
}
