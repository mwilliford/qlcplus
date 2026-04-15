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

### Issue 1: POS-6 beam cone origins offset from fixture mesh
**Symptom**: Beam cones originate from a point visually displaced from the LED head mesh.

**Root cause**: Two independent transform chains compute positions:
- **Mesh**: scene graph → `bx::mtxMul(world, local, parent)` recursively in `renderSceneGraph()`
- **Beams**: rigmath `KinematicChain::forward_world()` → walks joints + beam offsets in `rebuildBeamCones()`

These are built from the same GDTF data but accumulated differently. The kinematics chain computes beam offsets relative to the last joint only, while the scene graph accumulates all intermediate geometry node transforms.

**Key files**:
- `qmlui/spatialview.cpp` — `rebuildBeamCones()` (lines ~1030-1114)
- `render/src/bgfxrenderer.cpp` — `renderSceneGraph()` (lines ~528-591)
- `engine/src/gdtfkinematics.cpp` — `collectBeams()`, beam offset computation
- rigmath `kinematic_chain.cpp` — `forward_world()`, `forward_local()`, `walk_chain()`

**Fix approach**: Either unify the two paths (compute beam origins from the scene graph during rendering), or ensure the kinematics chain produces identical transforms.

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
