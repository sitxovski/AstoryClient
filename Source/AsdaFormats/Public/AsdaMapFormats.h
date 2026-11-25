// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMapFormats.h — порт парсеров форматов карт AsdaStory из Tools/MapViewer
// (asda_formats.h/.cpp) в чистый C++ БЕЗ raylib и БЕЗ UObject. Логика чтения
// (offset'ы, struct-смещения, циклы) перенесена БАЙТ-ТОЧНО 1:1.
//
// Портированы: геометрия/террейн/модели/сцена/вода/объекты:
//   LoadHeightmapBIN, LoadWaterMesh, LoadPatchTerrain, LoadJBM, LoadAniList,
//   LoadMapObjects, LoadScene, ScanMaps.
// НЕ портированы (эффекты + raylib-image): LoadTEXImage*, LoadTEXTexture,
//   LoadEffectSpriteImage, ParseEffectSprites, ParseJem, ReadZipEntry.
//
//  .BIN  -> 16 байт заголовок [u32 ver, u32 width, u32 height, u32 cellCount],
//           затем width*height little-endian float = карта высот террейна.
//
// raylib-типы заменены plain-структурами в namespace asda:
//   Vector3 -> Vec3,  Color -> ColorRGBA.
#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Маркер экспорта для UE-модуля (на не-UE сборке — пустышка).
#ifndef ASDAFORMATS_API
#define ASDAFORMATS_API
#endif

namespace asda {

// --- plain-замены raylib-типов (вместо Vector3/Color) ---------------------
struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };                 // замена raylib Vector3
struct ColorRGBA { unsigned char r = 0, g = 0, b = 0, a = 0; };     // замена raylib Color

struct Heightmap {
    int   w = 0;
    int   h = 0;
    std::vector<float> data;   // row major, size w*h
    float minH = 0.0f;
    float maxH = 0.0f;

    bool  valid() const { return w > 0 && h > 0 && (int)data.size() == w * h; }
    float at(int x, int y) const {
        if (x < 0) x = 0; else if (x >= w) x = w - 1;
        if (y < 0) y = 0; else if (y >= h) y = h - 1;
        return data[(size_t)y * w + x];
    }
};

// Load a .BIN terrain height map. Returns an empty (invalid) Heightmap on failure.
ASDAFORMATS_API Heightmap LoadHeightmapBIN(const std::string& path);

struct MapEntry {
    std::string name;     // folder name
    std::string dir;      // full folder path
    std::string binPath;  // main height map (.BIN)
    std::string texPath;  // main terrain texture (.TEX), may be empty
};

// Scan a map root folder; every sub-folder that contains a .BIN becomes an entry.
ASDAFORMATS_API std::vector<MapEntry> ScanMaps(const std::string& mapRoot);

// One water body = an indexed triangle mesh (positions in raw world coords + UV).
struct WaterBody {
    std::vector<float>          pos;   // raw world x,y,z (3*vtxCount)
    std::vector<float>          uv;    // u,v (2*vtxCount)
    std::vector<unsigned short> idx;   // triCount*3 local indices
};

// Full water geometry parsed from a .WAT (all bodies).
//
// .WAT layout (fully reversed, see FORMATS.md):
//   header : u32 magic(0x20060214), u32 count
//   records[count] : 0x348 bytes each, at 0x08 + i*0x348
//        +0x258 i32 triCount   (triangles)
//        +0x25c i32 vtxCount
//        +0x264 i32 stride (=40)        ; vertex = 10 floats:
//                                         [x,y,z, e0, u,v, u2,v2, uDetail,vDetail]
//        +0x01c .TGA path "Water1", +0x11c "Water2"
//        +0x2cc bbox corner A (x,y,z), +0x2d8 corner B
//   geometry[count] : in record order, each = [triCount*3 u16 indices][vtxCount*40 verts]
struct WaterMesh {
    bool                    has   = false;
    float                   level = 0.0f;   // representative surface Y
    std::vector<WaterBody>  bodies;
    std::string             texName;        // e.g. "Water1.TGA" (basename)
};

ASDAFORMATS_API WaterMesh LoadWaterMesh(const std::string& path);

// ---------------------------------------------------------------------------
// Real terrain: the baked patch system (see FORMATS.md).
//
// The runtime does NOT render terrain from .BIN/.TER. It streams per-patch
// vertex buffers (world-space, baked) plus two baked tiles per patch:
//   TerrainColor.TEX  = grayscale lightmap
//   TerrainAlpha.TEX  = splat weight map for the detail ground tiles
//
// Files (per map folder, all confirmed on `mine`, 64 patches = 8x8 grid):
//   BUFFER.SEQ          N*u32  byte offset of each patch block in BUFFER.TVB
//   BUFFER.TVB          per patch: u32 magic 0x20060117, u32 vtxCount,
//                       vtxCount * 32B vertex (FVF 0x242 = XYZ|DIFFUSE|TEX2):
//                         +0x00 float x,y,z (world coords, baked; y=height)
//                         +0x0c u32   diffuse ARGB (baked light, ~0xFFFFFFFF)
//                         +0x10 float u0,v0   (0..1 over the patch)
//                         +0x18 float u1,v1   (== u0,v0 on mine)
//   BUFFER.TIB          u32 magic, u32 indexCount, indexCount*u16  (SHARED by
//                       every patch; local indices 0..vtxCount-1, row stride 65)
//   TerrainColor.SEQ    N*u32  byte offset of patch tile in TerrainColor.TEX
//   TerrainAlpha.SEQ    N*u32  byte offset of patch tile in TerrainAlpha.TEX
//   TerrainColor.TEX    array of N standard .TEX tiles (256x256 DXT3) concatenated
//   TerrainAlpha.TEX    same, splat weights
struct TerrainPatch {
    int                        vtxCount = 0;
    std::vector<float>         pos;   // 3*vtx, raw world x,y,z
    std::vector<float>         uv0;   // 2*vtx
    std::vector<float>         uv1;   // 2*vtx
    std::vector<unsigned char> col;   // 4*vtx RGBA (from diffuse ARGB)
    unsigned int               colorOff = 0;  // offset into TerrainColor.TEX
    unsigned int               alphaOff = 0;  // offset into TerrainAlpha.TEX
    unsigned int               srcOff   = 0;  // byte offset of this patch's vertex array in BUFFER.TVB (for editor save)
    float minX = 0.0f, minZ = 0.0f;           // patch origin (world)
    // detail splat layers (from ExTileInfo.inf): base ground tile always present,
    // overlay tile painted in via the TerrainAlpha mask (alpha channel).
    std::string detail0;  float scale0 = 1.0f;          // base layer texture name
    std::string detail1;  float scale1 = 1.0f;          // overlay (empty = none)
};

struct PatchTerrain {
    bool                            has = false;
    std::string                     dir;
    std::vector<TerrainPatch>       patches;
    std::vector<unsigned short>     indices;   // shared index buffer
};

// Parse the patch terrain from a map folder. has==false if the files are absent
// (older/other maps) so the caller can fall back to the .BIN draped mesh.
ASDAFORMATS_API PatchTerrain LoadPatchTerrain(const std::string& mapDir);

// ---------------------------------------------------------------------------
// .SCE scene: fog + directional light + ambient (loader FUN_011d3370).
//   file: u32 magic (0x20060819, or old 0x20041008) + struct (0x1f8 bytes).
//   Field offsets are FILE offsets (struct begins right after the 4-byte magic):
//     +0x08 u32   fog color ARGB
//     +0x0c f32   fog start          +0x10 f32 fog end
//     +0x20..0x28 f32x3 ambient RGB
//     +0x44..0x4c f32x3 diffuse (light) RGB  (usually 1,1,1)
//     +0x50 f32   light angle theta  +0x54 f32 light angle phi
//   +0xB4..0xBC f32x3 sun direction (unit, TOWARD the sun, Y up) — what the client
//   actually uses (fread(0x1F8) → CRenderer_SetDirLight). Equals
//   (−cos th·sin ph, sin th, −cos th·cos ph) on all 22 maps (audit 2026-09-03).
//   [OLD, WRONG: (sin th·cos ph, cos th, sin th·sin ph) reproduced row 3 of the
//   matrix at +0x58 and was 30–90° off — kept here only as a warning.]
struct Scene {
    bool      valid    = false;
    ColorRGBA fogColor = { 150, 170, 200, 255 };
    float     fogStart = 5000.0f;
    float     fogEnd   = 10000.0f;
    Vec3      ambient  = { 0.4f, 0.4f, 0.4f };
    Vec3      diffuse  = { 1.0f, 1.0f, 1.0f };
    // ★ Направление К СОЛНЦУ (единичный, Y вверх) — ГОТОВЫЙ вектор из .SCE @0xB4, ровно тот, что
    //   клиент читает `fread(0x1F8)` (world_map.c:18770) и отдаёт в `CRenderer_SetDirLight`.
    //   Аудит 2026-09-03 (`lighting_audit_2026-09-03.md` Д-1): прежняя формула
    //   `(sin th·cos ph, cos th, sin th·sin ph)` по углам @0x50/@0x54 воспроизводила строку матрицы
    //   @0x58 и давала ошибку 30–90° на всех 22 картах. Верная формула (сходится с файлом 0.0000):
    //   `(−cos th·sin ph, sin th, −cos th·cos ph)`. Материалы ждут N·L с направлением К свету,
    //   поэтому `lightDir` = `sunDir`. `hasSunDir` = false только у усечённого файла (< 0xC0 байт).
    Vec3      sunDir   = { 0.0f, 1.0f, 0.0f };
    bool      hasSunDir = false;
    Vec3      lightDir = { 0.0f, 1.0f, 0.0f };
};

ASDAFORMATS_API Scene LoadScene(const std::string& path);

// ---------------------------------------------------------------------------
// .JBM 3D model (loader FUN_011b6ae0, magic 0x20050706).
//   file = u32 magic + header(0x20c) + blob(size @ header+0x204)
//   header: +0x04 meshCount, +0x30 matCount, +0x178 model name
//   blob: [matTable: matCount * 0x20  (each starts with a texture filename)]
//         [meshRecords: meshCount * 0x29c  (+0x28 vtxCount, +0x2c triCount)]
//         [geometry per mesh, in order:
//             indices  triCount*3 * u16
//             vertices vtxCount   * 0x2c (44):
//                +0x00 pos xyz (3f), +0x0c normal xyz (3f),
//                +0x18 diffuse u32,  +0x1c uv0 (2f), +0x24 uv1 (2f)
//             extra    vtxCount   * 0x0c   (skipped; tangent/extra)
//          ... skinning/morph arrays for some flags come AFTER all meshes and do
//          NOT affect vertex/index positions, so geometry extraction is a single
//          flag-independent pass.]
struct ModelMesh {
    std::vector<float>          pos;   // 3*vtx
    std::vector<float>          nrm;   // 3*vtx
    std::vector<float>          uv;    // 2*vtx (uv0)
    std::vector<unsigned short> idx;   // triCount*3
};
struct Model3D {
    bool                   has = false;
    std::vector<ModelMesh> meshes;
    std::string            texture;   // mat0 texture filename (basename)
    std::string            name;      // model name from header
};

ASDAFORMATS_API Model3D LoadJBM(const std::string& path);

// ---------------------------------------------------------------------------
// Map static objects: Pos.POS (instances) + Pos0.PBB (baked geometry).
//   Pos.POS  : per instance a model name "*.jbm"; world position = 3 floats at
//              (nameStart - 0x20). 481 instances / 25 types on mine.
//   Pos0.PBB : per type a material (texture path "\map\..\texture\X.TEX") then
//              mesh blocks: index buffer [u32 idxBytes][u16..], vertex buffer
//              [u32 vtxBytes][u32 FVF][verts]. FVF 0x252 = 44-byte vertex
//              (pos+normal+diffuse+uv0+uv1), local model space.
// Instance->mesh pairing is by texture name (model "crystaltree02_3" -> tex
// "crystaltree02"). Verts are local -> placed at the instance world position.
struct PbbMesh {
    std::vector<float>          pos;   // 3*vtx local
    std::vector<float>          nrm;   // 3*vtx
    std::vector<float>          uv;    // 2*vtx
    std::vector<unsigned short> idx;
    std::string                 texture;   // basename, e.g. "crystal.TEX"
    int                         vtxCount = 0;
};
struct ObjInstance {
    Vec3        pos;            // world coords (node position)
    int         meshIndex = -1; // index into MapObjects::meshes (the slice geometry)
    // identifiers for in-viewer selection/debug
    std::string model;          // instance model name (e.g. "tree_003")
    std::string texture;        // slice texture basename
    int         patch = -1;     // patch (block) index
    int         section = -1;   // 0=A(Pos0) 1=B(Pos1) 2=D(Pos2)
    int         geomIdx = 0, startVtx = 0, startIdx = 0, vtxCount = 0;
    bool        hasMat = false; // placed by world matrix (A/B) vs raw position (D)
    long long   posOff = -1;    // absolute byte offset in Pos.POS of pos.x (the 3 floats that drive .pos);
                                // for matrix-placed objects this is the matrix translation (m[12]). -1 = unknown.
    long long   rawPosOff = -1; // absolute offset of the raw node x/y/z (ro+0x174), ALWAYS (even when matrix-placed);
                                // the game positions each mesh from THIS, not the cached matrix, so the editor shifts it.
    Vec3        posOrig{};      // .pos at load time (matrix T or raw) - baseline for the save delta
    Vec3        rawOrig{};      // raw node x/y/z at load time          - baseline for shifting rawPosOff
    long long   matArrOff = -1; // abs offset of this mesh's matrix array (diagnostics)
    int         matCount  = 0;  // matrix entries in that array (diagnostics)
    long long   recOff    = -1; // abs offset of this mesh record (ro) (diagnostics)
};
// Pos.POS section C = visual effect emitters (particle/effect models .JEB, packed
// in Data\Effect\Model\EffectModel.bnz). Record 0x438: pos @+0x2f8, type @+0x314
// (-1 standalone loop, 2 attached to a section-A instance @+0x434), model path
// @+0x32c "\Data\Effect\Model\eff_*.JEB". Rendered as a world marker (the effect
// itself is an animated particle system, out of scope for a static viewer).
struct MapEffect {
    Vec3        pos;            // raw world coords
    std::string model;         // effect model basename (e.g. "eff_flammio-a_10")
    int         type = -1;     // +0x314: -1 standalone, 1 attach section-A obj, 2 attach section-B group
    int         attachInst = -1; // +0x434: section-A/B instance index (or -1)
    bool        attached = false;           // type 1/2 with a resolved parent transform
    float       pmat[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };  // parent group world matrix; engine: A-rec +0x1b0 / B grp +0x168
    int         bone = -1;                  // +0x31c: attach BONE index inside the parent's skeleton (type 2)
    int         clipIdx = -1;               // parent group's ani-clip (bGroupClip[attachInst]); -1 = none
};
// Pos.POS section E = 3D sound emitters. Record 0x138: wav @+0x04, pos @+0x104,
// min radius @+0x110, max radius @+0x114.
struct MapSound {
    Vec3        pos;            // raw world coords
    std::string wav;           // WAV filename (e.g. "S_OBJ_PROPEL1.WAV")
    float       minR = 0.0f;
    float       maxR = 0.0f;
};
// ---------------------------------------------------------------------------
// ani_list.ani — baked skeletal animation of map props (.jbb clips). Per map,
// loaded by the patch loader before Pos.POS. magic 0x20041019 + u32 blobSize +
// u32 count + blob: [count×0x40 object headers (name@+0, boneCount@+0x20)] then
// per object [boneCount×i32 parentIdx][boneCount×0x94 bone (name@+0, keyCount@
// +0x20)] then per bone [keyCount×0x2c keyframe: f32 time(ms) + pos(3)+quat(4)+
// scale(3)]. Bones bind to section-B sub-meshes by node name (Pos.POS mesh
// record +0x1fc).
struct AniKey   { float t; float pos[3]; float rot[4]; float scale[3]; };
// `convert` = the per-bone CONVERT_MATRIX_VALUE at bone record +0x90 (a uniform scalar the engine
// multiplies into the 3x3 rotation block, FUN_0112cba0). 1.0 if absent/degenerate.
struct AniBone  { std::string name; int parent = -1; float convert = 1.0f; std::vector<AniKey> keys; };
struct AniClip  { std::string name; std::vector<AniBone> bones; float duration = 0.0f; };

// A section-B sub-mesh that is animated: RAW local geometry (not R-baked), its
// rest world matrix, and a reference into MapObjects::aniClips / that clip's bone.
struct AnimMesh {
    std::vector<float>          pos;   // 3*vtx local
    std::vector<float>          nrm;   // 3*vtx local (for the game's BLOCK 7-style directional lighting)
    std::vector<float>          uv;    // 2*vtx
    std::vector<unsigned short> idx;
    std::string                 texture;
    float                       m[16];        // per-part rest world matrix (Pos.POS), kept for debug
    float                       objPlace[16]; // section-B group world matrix (grpBase+0x168) - engine placement
    int                         clip = -1;
    int                         bone = -1;
    std::string                 model; // owning prop, e.g. "center_001"
};

// GPU-skinned section-B sub-mesh (FVF 0x256). Vertices are in BONE-LOCAL space; each vertex names
// its ani bone via a float at vertex offset +12 = boneIndex*3+8 (shader constant register). Final:
// world = ObjPlace * aniBoneChain[bone] * localVertex. Verified vs the client (no inverse-bind).
// 1-bone meshes (FVF 0x256): bone set, bone2=-1, w0=1. 2-bone blend meshes (FVF 0x60252,
// e.g. the bird body): both bones set, w0 = weight of bone (w1 = 1-w0). Linear blend skinning.
struct SkinVertex { float pos[3]; float nrm[3] = {0,0,1}; float uv[2]; int bone; int bone2 = -1; float w0 = 1.0f; };
struct SkinMesh {
    std::vector<SkinVertex>     verts;
    std::vector<unsigned short> idx;
    std::string                 texture;
    float                       objPlace[16];  // section-B group world matrix (grpBase+0x168)
    int                         clip = -1;     // ani_list clip index for this prop
};

struct MapObjects {
    bool                     has = false;
    std::vector<PbbMesh>     meshes;
    std::vector<ObjInstance> instances;
    std::vector<MapEffect>   effects;   // section C
    std::vector<MapSound>    sounds;    // section E
    std::vector<AniClip>     aniClips;  // ani_list.ani
    std::vector<AnimMesh>    animMeshes;// animated section-B sub-meshes (rigid, 1 bone)
    std::vector<SkinMesh>    skinMeshes;// GPU-skinned section-B sub-meshes (FVF 0x256)
};

// Parse <mapDir>\ani_list.ani into clips. Empty on absence.
ASDAFORMATS_API std::vector<AniClip> LoadAniList(const std::string& mapDir);

ASDAFORMATS_API MapObjects LoadMapObjects(const std::string& mapDir);

} // namespace asda
