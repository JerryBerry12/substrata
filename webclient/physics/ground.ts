/*
Adds a virtual ground plane to the physics world
*/

/*
1) Find player position.
2) Move ground plane to coincide with player position
3) Don't need to store a ref to visual object, only need a BVH for intersection queries.
*/

import * as THREE from '../build/three.module.js';
import BVH, { Triangles } from './bvh.js';
import { generatePatch } from '../maths/generators.js';

export class Ground {
  private readonly bvh_: BVH;
  private readonly mesh_: THREE.Mesh;

  private readonly worldToObject_: THREE.Matrix4;

  public constructor () {
    const geo = generatePatch(1, 1);
    const buf = new THREE.BufferGeometry();
    buf.setAttribute('position', new THREE.BufferAttribute(geo[0], 3, false));
    buf.setIndex(new THREE.BufferAttribute(geo[1], 1, false));
    this.mesh_ = new THREE.Mesh(buf, new THREE.MeshBasicMaterial({ color: 'red', transparent: true, opacity: 0.5 }));
    this.mesh_.position.set(0, 0, .001);
    this.mesh_.scale.set(10, 10, 10);

    const triangles = new Triangles(geo[0], geo[1], 0, 3); // Stride = 3 (multiples of 4 bytes) = 12 bytes per vertex
    this.bvh_ = new BVH(triangles);

    this.worldToObject_ = new THREE.Matrix4();
  }

  // In order to visualise the ground collision plane
  public get mesh (): THREE.Mesh { return this.mesh_; }
  public get bvh (): BVH { return this.bvh_; }
  public get worldToObject (): THREE.Matrix4 { return this.worldToObject_; }
  public get objectToWorld (): THREE.Matrix4 { return this.mesh_.matrixWorld; }

  // Implemented method in MainWindow::UpdateGroundPlane
  public updateGroundPlane (camPos: Float32Array): void {
    this.mesh.position.set(camPos[0], camPos[1], .001);
    this.worldToObject.copy(this.mesh_.matrixWorld); // Copy the objectToWorld matrix of the THREE.js mesh and invert
    this.worldToObject.invert(); // TODO: Optimise
  }
}
