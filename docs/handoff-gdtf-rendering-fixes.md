# Handoff: GDTF Rendering Pipeline — BlenderDMX Comparison & Systematic Fixes

## Session Goal

Study BlenderDMX's open-source GDTF rendering pipeline and fix our systematic rendering issues. BlenderDMX correctly renders the same GDTF fixtures we're getting wrong. The strategy is: find the general rules BlenderDMX follows, then align our pipeline to match.

## What Was Built (R2-R4, this session)

### Commits on worktree `charming-sammet`:
| Commit | Summary |
|--------|---------|
| `9c00f39` | R2: per-frame articulated rendering (DOF rotation from live DMX), R4: GeometryReference resolution (inline target properties), R3: 3DS loader scaffold |
| `1a6d280` | R3: minimal 3DS chunk parser with smooth normals, 32-bit index buffers |
| `4658171` | Per-axis model scaling to GDTF dimensions, primitive sizing from model dims, GDTF unit fixes (removed erroneous ×0.001 on dimensions) |
| `6a6a243` | Mode-specific geometry trees (namedRoots map), Position translation unit fix (removed ×0.001), TdsLoader no-center (matches BlenderDMX) |

### What works:
- **R2 Articulation**: Fixtures visually pan/tilt with live DMX. DOF rotation via Rodrigues formula in renderSceneGraph(). AxisDofTag from buildGDTFKinematics() as single source of truth.
- **R4 GeometryReference**: Inlined at parse time via GetGeometryReference(). Beam/model properties copied to reference nodes. Multi-beam rendering via collectBeams heuristic.
- **R3 3DS Loading**: Minimal parser (no assimp dependency). Smooth vertex normals. 32-bit index buffers for large meshes. Per-axis scaling to GDTF model dimensions.
- **Mode-specific geometry**: Each DMX mode references a specific root geometry tree. namedRoots map + rootForMode() selects the correct tree.

### Test coverage:
71 tests pass (22 gdtfkinematics + 18 calibrationmodel + 31 spatialmodel).

## Known Rendering Issues (QA failures)

### Issue 1: Primitive scaling produces wrong shapes
**Symptom**: The Inno Scan HP shows a giant sphere for the mirror, way too large. The POS-6 pixel cylinders were giant sausages before scaling fix.

**Root cause**: Our PrimitiveGen creates meshes at fixed sizes (e.g., Cylinder r=0.1 h=0.3, Sphere r=0.1). We scale them by `gdtf_dimension / primitive_default_extent`, but:
- The primitive default extents are hardcoded guesses that may not match the actual mesh bounding box
- Sphere primitive scaling doesn't handle non-uniform dimensions well (a 40mm×40mm×15mm cylinder scaled from a 0.2m sphere looks wrong)
- Some fixtures use PrimitiveType=Sphere for non-spherical geometry (like a mirror head)

**BlenderDMX approach**: They generate primitives at unit size (1×1×1) and scale via `Matrix.Diagonal(length, width, height)`. No ratio computation needed. They also have pre-built GLB primitives for GDTF primitive types (Base1_1, Scanner1_1, etc.) that they scale to dimensions.

**Fix needed**: Either generate unit-size primitives or use BlenderDMX's pre-built primitive GLB assets.

### Issue 2: Model positioning — baked offset vs Position matrix
**Symptom**: XTILT head was rendered too low. After removing centering in TdsLoader, some models render offset from expected joint positions.

**Root cause**: Unclear whether 3DS model vertices include a baked position offset or are centered at origin. If the model has baked offset AND the GDTF Position matrix adds more offset, positioning is doubled. If the model is centered but we don't center, it's under-positioned.

**BlenderDMX approach**: They do NOT center 3DS models. They only scale (no translation). The model's baked vertex positions are preserved, and the GDTF Position matrix provides additional offset via `matrix_local`.

**Current state**: We removed centering (matching BlenderDMX). But some models may still look wrong because the GDTF Position matrix might assume the model IS centered.

### Issue 3: Position matrix rotation inversion
**Symptom**: Joint orientations may be wrong for well-authored GDTF files with non-identity rotation in Position matrices.

**BlenderDMX approach**: They INVERT the rotation part of the Position matrix:
```python
rotation = geometry_mtx.to_3x3().inverted()
```
This is critical for correct parent-child spatial relationships. We do NOT invert — we use the rotation as-is. This may cause incorrect joint orientations for fixtures with non-identity axis rotations.

**Investigation needed**: Determine if the inversion is Blender-specific (Blender's `matrix_local` convention) or a general GDTF requirement. Test with a well-authored GDTF fixture that has non-identity axis rotations.

### Issue 4: Beam cone origins offset from fixture mesh
**Symptom**: POS-6 beam cones originate above/beside the fixture mesh, not from the LED face.

**Root cause**: The beam origin comes from the kinematics pipeline (chain->forward_world()), which uses the GDTF geometry transforms. The 3DS mesh has its own coordinate system. If the mesh's visual center doesn't align with the GDTF origin, beams appear offset from the mesh.

**BlenderDMX approach**: They use Blender's constraint system (TrackTo targets) for beam direction, which automatically aligns with the model. For position, they rely on the same GDTF Position matrices.

**Investigation needed**: Compare beam origin computation between our kinematics and BlenderDMX's target system.

### Issue 5: GDTF coordinate system interpretation
**Symptom**: Various positioning issues across multiple fixtures.

**Key question**: The GDTF Position matrix format is `{u1,u2,u3,tx}{v1,v2,v3,ty}{w1,w2,w3,tz}{0,0,0,1}` where each group is a ROW of the 4x4 matrix. Our `convertTransform()` treats this as: u/v/w are COLUMNS of the column-major output. Is this correct?

**BlenderDMX approach**: They use `Matrix(geometry.position.matrix)` which creates a Blender Matrix from the GDTF data. Blender matrices are row-major. They then decompose into translation, rotation (inverted), and scale.

**Investigation needed**: Verify our `convertTransform()` correctly maps the GDTF matrix format to our column-major convention. The row/column interpretation could cause transposed rotations.

## Key Files

### Our rendering pipeline:
- `render/src/bgfxrenderer.cpp` — renderSceneGraph() with DOF rotation (Rodrigues)
- `render/src/fixturescenegraph.h` — SceneNode (dofIndex, dofAxis, isBeamNode, beamAngle)
- `render/src/rendertypes.h` — RenderFixture (dofAngles)
- `render/src/tdsloader.cpp` — 3DS parser with per-axis scaling
- `render/src/primitivegen.cpp` — procedural primitive meshes with hardcoded sizes
- `qmlui/spatialview.cpp` — buildSceneNode(), getOrBuildSceneGraph(), rebuildFixtureDofs()
- `engine/src/gdtfparser.cpp` — extractGeometryNode(), convertTransform(), extractModelProperties()
- `engine/src/gdtfkinematics.cpp` — buildGDTFKinematics(), collectBeams(), AxisDofTag
- `engine/src/gdtfgeometrydata.h` — GDTFGeometryNode, GDTFDmxModeInfo, namedRoots

### BlenderDMX (reference implementation):
- GitHub: https://github.com/open-stage/blender-dmx
- `gdtf.py` — GDTF geometry parsing, model loading, Position matrix handling, GeometryReference
- `fixture.py` — Pan/Tilt articulation, beam rendering, DMX control

### Key BlenderDMX patterns to study:
1. `load_geometries()` — how geometry tree is walked and models instantiated
2. `add_child_position()` — Position matrix decomposition with rotation inversion
3. 3DS import with `APPLY_MATRIX=False` + diagonal scale to GDTF dimensions
4. `load_blender_primitive()` / `load_gdtf_primitive()` — primitive generation and scaling
5. `updatePTDirectly()` — direct Pan/Tilt rotation on yoke/head (Z/X axis mapping)
6. GeometryReference deep-copy + double-positioning (target position + reference position)

## Test Fixtures

These GDTF files are already downloaded in the user's gdtf-cache:
- **Futurelight POS-6** — LED bar, 6 GeometryReferences, tilt-only, 3DS models (119K + 130K verts)
- **Betopper LM120** — Moving head, pan+tilt, small 3DS ring model
- **Bright XTILT** — Tilt-only LED bar, 3 root geometries (mode-specific), 3DS models
- **Inno Scan HP** — Scanner with mirror, sphere primitive renders way too large

Workspace: `/Users/mwilliford/Documents/dmx/test1.aqw`

## Strategy for Next Session

1. **Read BlenderDMX's full rendering pipeline** — focus on `gdtf.py` (model loading, Position matrix, scaling) and `fixture.py` (articulation). Get the actual Python code, not summaries.

2. **Create a comparison matrix**: For each of our 4 test fixtures, document what BlenderDMX does vs what we do at each step:
   - Position matrix interpretation (row vs column, inversion?)
   - Model loading (3DS scale, centering, coordinate swap)
   - Primitive generation (unit size? dimension mapping?)
   - Joint articulation (axis direction, rotation order)

3. **Identify the general rules** — extract the ~5 key rules that BlenderDMX follows for GDTF rendering. These become our spec.

4. **Fix our pipeline to match** — make targeted fixes based on the rules, not per-fixture patches.

## Parallel Sessions Note

This worktree (`charming-sammet`) owns: render/src/*, bgfxrenderer, scenegraph, spatialview rendering, gdtfparser geometry extraction, tdsloader, primitivegen.
