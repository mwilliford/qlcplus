# Handoff: GDTF Rendering Pipeline — Status & Remaining Issues

## What Was Built (R2-R6)

### Commits on worktree `charming-sammet`:
| Commit | Summary |
|--------|---------|
| `9c00f39` | R2: per-frame articulated rendering, R4: GeometryReference resolution, R3: 3DS loader scaffold |
| `1a6d280` | R3: minimal 3DS chunk parser with smooth normals, 32-bit index buffers |
| `4658171` | Per-axis model scaling to GDTF dimensions, primitive sizing, GDTF unit fixes |
| `6a6a243` | Mode-specific geometry trees (namedRoots map), Position translation fix, TdsLoader no-center |
| `6564bc5` | Handoff doc for BlenderDMX comparison |
| `f2c9a05` | **R5: Scale-free localTransform** — glTF node transforms, PrimitiveGen stateless factory, non-uniform cylinder/sphere scaling |
| `50db132` | **R6: DOF articulation fix** — local-frame rotation, Geometry-as-Axis support, attribute-based axis inference |
| `81c9192` | glTF comment clarification |

### What works:
- **R2 Articulation**: Pan/tilt with live DMX. DOF rotation in local frame (fixed in R6).
- **R3 3DS Loading**: Minimal parser, smooth normals, per-axis scaling to GDTF dimensions.
- **R4 GeometryReference**: Inlined at parse time. Multi-beam via collectBeams.
- **R5 Scale-free transforms**: localTransform = GDTF Position matrix only. All mesh sizing baked into vertex data.
  - **GltfLoader**: Walks tinygltf node tree, applies node transforms (scale/rotation/translation) to vertices, then scales to GDTF target dimensions.
  - **PrimitiveGen**: Stateless factory. Unit cylinder/sphere → non-uniform scale by (L,W,H). Flat mirror discs render correctly.
  - **buildSceneNode**: No localTransform modification. Primitives cached by (type + dimensions).
- **R6 DOF fixes**:
  - DOF rotation uses LOCAL axis directly (not world-transformed). Fixes child DOF rotating with parent.
  - `collectAxes()` finds `<Geometry>` nodes with Pan/Tilt channels (not just `<Axis>` nodes). Fixes ERA 700.
  - Axis always inferred from attribute name (Pan→Z/Y, Tilt→X) regardless of Position matrix rotation.
- **Mode-specific geometry**: namedRoots map + rootForMode().

### Test coverage:
130 tests pass (22 gdtfkinematics + 59 agentcontext + 18 calibrationmodel + 31 spatialmodel).

### Fixtures tested:
| Fixture | Mesh type | Status |
|---------|-----------|--------|
| **Bright XTILT** | 3DS | ✅ Great — correct shape, tilt works |
| **Robe Scan 575XT** | glTF + primitive | ✅ Articulation correct, mirror pans/tilts properly. Body mesh distorted due to GDTF axis convention mismatch (cosmetic, not pipeline bug) |
| **Martin ERA 700** | glTF | ✅ Pan/tilt working (was broken — used `<Geometry>` instead of `<Axis>`) |
| **Futurelight POS-6** | 3DS | ⚠️ Mesh renders correctly, beam cones offset (pre-existing, see Issue 1) |
| **Inno Scan HP** | Primitive only | ⚠️ GDTF authoring error — 660mm pan offset is ~10x too large for this fixture |

## Remaining Issues

### ~~Issue 1: POS-6 beam cone origins offset from fixture mesh~~ ✅ FIXED (2026-04-16)

**Was**: Beam cones originated from a point visually displaced from the LED head mesh.

**Root cause (confirmed)**: `buildGDTFKinematics()` set each joint's `parent_to_joint` to `axisNode->localTransform` — the axis node's transform relative to its **immediate** parent, not to the fixture root. GDTF trees can have non-axis grouping nodes between the root and the first axis (POS-6's "Base 1" at Z=-141mm) whose transforms were silently dropped from the kinematics chain. The scene graph path was correct because it walks every node.

**Fix**: `buildGDTFKinematics()` now composes the full path transform from each previous axis (or root) down to the current axis, including any intermediate non-axis geometry nodes. `collectBeams()` similarly accumulates intermediate transforms between the last axis and each beam node. See `engine/src/gdtfkinematics.cpp` and commit message "POS-6 beam cone origin offset fix".

**Tests added** (5 in `gdtfkinematics_test.cpp`):
- `intermediateNode_beforeFirstAxis` — POS-6 pattern
- `intermediateNode_betweenAxes`
- `intermediateNode_beforeBeam`
- `beamOrigin_matchesSceneGraphWalk` — kinematics vs scene-graph walk at 5 angles
- `beamOrigin_matchesSceneGraphWalk_withIntermediates` — comprehensive with intermediates at every level

Also: rigmath bumped to v1.1.0 and `rebuildBeamCones()` now uses `forward_world_all` + `rigmath::Beam::hit_plane_z(0)` (cleanup; not required for the fix).

### Issue 2: glTF axis convention mismatch (cosmetic)
**Symptom**: Robe 575XT body mesh appears distorted — stretched in Y, squished in Z.

**Root cause**: The glTF model was authored with the body along Z, but GDTF dimensions map Length→X, Width→Y, Height→Z. Scaling to GDTF dims produces extreme non-uniform factors (1.0×, 3.0×, 0.29×). This is a GDTF authoring issue, not a pipeline bug.

**Status**: Accepted as cosmetic. Skipping the scale makes the mesh proportionally correct but wrong orientation. The current approach (scale to GDTF dims) is what BlenderDMX also does.

## Key Files

### Rendering pipeline:
- `render/src/bgfxrenderer.cpp` — renderSceneGraph() with local-frame DOF rotation
- `render/src/gltfloader.cpp` — glTF loading with node transform walking + GDTF dim scaling
- `render/src/tdsloader.cpp` — 3DS parser with per-axis scaling to GDTF dims
- `render/src/primitivegen.cpp` — stateless factory, unit shapes → non-uniform scale
- `render/src/fixturescenegraph.h` — SceneNode (dofIndex, dofAxis, isBeamNode)
- `qmlui/spatialview.cpp` — buildSceneNode(), getOrBuildSceneGraph(), rebuildFixtureDofs()
- `engine/src/gdtfparser.cpp` — extractGeometryNode(), convertTransform()
- `engine/src/gdtfkinematics.cpp` — buildGDTFKinematics(), collectAxes(), AxisDofTag

### BlenderDMX (reference):
- GitHub: https://github.com/open-stage/blender-dmx
- `gdtf.py` — geometry loading, Position matrix, model scaling
- `fixture.py` — Pan/Tilt articulation

## Parallel Sessions Note

This worktree (`charming-sammet`) owns: render/src/*, bgfxrenderer, scenegraph, spatialview rendering, gdtfparser geometry extraction, tdsloader, primitivegen, gdtfkinematics axis logic.
