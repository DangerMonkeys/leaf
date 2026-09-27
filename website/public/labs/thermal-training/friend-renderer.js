import * as THREE from 'three';
import { GLTFLoader } from './vendor/three/loaders/GLTFLoader.js';
import './friend-colors.js';

// A transparent, live 3D sprite composited into the existing 2D simulation canvas.
export async function createFriendRenderer(config, palette = 'lime') {
  const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true, preserveDrawingBuffer: true });
  renderer.setSize(256 * config.sideViewPixelRatio, 256 * config.sideViewPixelRatio, false);
  renderer.setClearColor(0x000000, 0);
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.NeutralToneMapping;
  renderer.toneMappingExposure = 1;
  let contextLost = false;
  renderer.domElement.addEventListener('webglcontextlost', event => { event.preventDefault(); contextLost = true; });
  renderer.domElement.addEventListener('webglcontextrestored', () => { contextLost = false; });
  const scene = new THREE.Scene();
  scene.add(new THREE.HemisphereLight(0xffffff, 0x8b91aa, 2));
  const key = new THREE.DirectionalLight(0xffffff, 2.6);
  key.position.set(-8, 10, -6);
  scene.add(key);
  const fill = new THREE.DirectionalLight(0xdde5ff, 1);
  fill.position.set(8, 1, 5);
  scene.add(fill);
  const gltf = await new GLTFLoader().loadAsync(new URL('./friend-pilot.glb', import.meta.url).href);
  const bank = new THREE.Group();
  bank.add(gltf.scene);
  scene.add(bank);
  // Preserve the traced skin and rounded-cell shading, with matte illustrated surfaces.
  const recoloredMaps = new Map();
  const styledMaterials = new Set();
  gltf.scene.traverse(object => {
    if (!object.isMesh) return;
    const materials = Array.isArray(object.material) ? object.material : [object.material];
    for (const material of materials) {
      if (styledMaterials.has(material)) continue;
      styledMaterials.add(material);
      if (palette === 'orange-red' && material.map) {
        const source = material.map;
        if (!recoloredMaps.has(source)) {
          const map = source.clone();
          // A fresh source prevents the clone from changing the original texture.
          map.source = new THREE.Source(FriendColors.orangeRedCanvas(source.image));
          map.needsUpdate = true;
          recoloredMaps.set(source, map);
        }
        material.map = recoloredMaps.get(source);
      }
      if ('metalness' in material) material.metalness = 0;
      if ('roughness' in material) material.roughness = 0.88;
      if ('clearcoat' in material) material.clearcoat = 0;
      if (material.map) material.map.anisotropy = Math.min(4, renderer.capabilities.getMaxAnisotropy());
    }
    if (/^(Lower|Middle|Upper) —/.test(object.name)) {
      object.material = new THREE.MeshBasicMaterial({ color: 0x465054 });
    }
  });
  gltf.scene.updateMatrixWorld(true);
  const projectionPoints = [];
  gltf.scene.traverse(object => {
    if (!object.isMesh) return;
    const positions = object.geometry.attributes.position;
    // Sample the real surface, avoiding a large empty margin around the wing.
    for (let i = 0; i < positions.count; i += 4) {
      projectionPoints.push(new THREE.Vector3().fromBufferAttribute(positions, i).applyMatrix4(object.matrixWorld));
    }
  });
  const box = new THREE.Box3().setFromObject(gltf.scene);
  const center = box.getCenter(new THREE.Vector3());
  const corners = [];
  for (const x of [box.min.x, box.max.x]) for (const y of [box.min.y, box.max.y]) for (const z of [box.min.z, box.max.z]) {
    corners.push(new THREE.Vector3(x, y, z));
  }
  const camera = new THREE.PerspectiveCamera(config.sideViewFovDeg, 1, 0.1, 200);
  const axis = new THREE.Vector3(0, 0, 1);
  const target = new THREE.Vector3();
  const direction = new THREE.Vector3();
  let lastRender = -Infinity;
  let lastKey = '';
  let pilotAnchorY = 0.78;
  let pilotAnchorX = 0.5;
  let bounds = {left: 0, right: 1};
  return {
    render(view, now = performance.now()) {
      if (contextLost) return null;
      const poseKey = [view.localX, view.localZ, view.elevation, view.bankDeg].map(v => v.toFixed(3)).join(',');
      if (poseKey === lastKey || now - lastRender < 1000 / config.sideViewMaxFps) {
        return { canvas: renderer.domElement, pilotAnchorY, pilotAnchorX, bounds };
      }
      lastRender = now;
      lastKey = poseKey;
      // Roll about the aircraft's forward axis, not the observer's screen normal.
      const roll = -THREE.MathUtils.degToRad(view.bankDeg);
      bank.rotation.z = roll;
      bank.updateMatrixWorld(true);
      target.copy(center).applyAxisAngle(axis, roll);
      const elevation = THREE.MathUtils.degToRad(view.elevation);
      direction.set(view.localX * Math.cos(elevation), Math.sin(elevation), view.localZ * Math.cos(elevation));
      camera.position.copy(target).addScaledVector(direction, 40);
      camera.lookAt(target);
      camera.updateMatrixWorld(true);
      // Fit all rotated bounding-box corners in the perspective frame, with padding.
      const inverseRotation = camera.quaternion.clone().invert();
      const tangent = Math.tan(THREE.MathUtils.degToRad(config.sideViewFovDeg / 2));
      let distance = 12;
      for (const corner of corners) {
        const p = corner.clone().applyAxisAngle(axis, roll).sub(target).applyQuaternion(inverseRotation);
        distance = Math.max(distance, p.z + Math.max(Math.abs(p.x), Math.abs(p.y)) / tangent * 1.08);
      }
      camera.position.copy(target).addScaledVector(direction, distance);
      camera.updateMatrixWorld(true);
      // The reference altitude is the pilot, not the canopy or image center.
      const anchor = new THREE.Vector3(0, -3.1, 0).applyAxisAngle(axis, roll).project(camera);
      pilotAnchorY = (1 - anchor.y) / 2;
      pilotAnchorX = (1 + anchor.x) / 2;
      let left = 1, right = 0;
      const projected = new THREE.Vector3();
      for (const point of projectionPoints) {
        projected.copy(point).applyAxisAngle(axis, roll).project(camera);
        left = Math.min(left, (projected.x + 1) / 2);
        right = Math.max(right, (projected.x + 1) / 2);
      }
      bounds = {left: Math.max(0, left - 0.02), right: Math.min(1, right + 0.02)};
      renderer.render(scene, camera);
      return { canvas: renderer.domElement, pilotAnchorY, pilotAnchorX, bounds };
    },
    dispose() {
      gltf.scene.traverse(object => {
        object.geometry?.dispose();
        const materials = Array.isArray(object.material) ? object.material : [object.material];
        for (const material of materials) { material?.map?.dispose(); material?.dispose(); }
      });
      renderer.dispose();
    }
  };
}
