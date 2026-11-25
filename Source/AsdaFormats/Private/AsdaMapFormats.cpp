// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMapFormats.cpp — порт парсеров форматов карт AsdaStory из Tools/MapViewer
// (asda_formats.cpp). Логика чтения перенесена БАЙТ-ТОЧНО 1:1. Отличия от оригинала:
//   * raylib-типы Vector3/Color заменены на asda::Vec3 / asda::ColorRGBA;
//   * raylib LoadFileData/UnloadFileData заменены на локальные std::ifstream-хелперы
//     (та же семантика: указатель на буфер + парный free через UnloadFileData);
//   * Themida-маркеры (STR_ENCRYPT_*, VM_*) убраны — они были no-op без -DASDA_THEMIDA;
//   * эффекты (секция C / .JEB / ParseEffectSprites) НЕ портированы — заглушены.
#include "AsdaMapFormats.h"

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cctype>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <map>
#include <set>
#include <array>

namespace fs = std::filesystem;

namespace asda {

// --- raylib-free замена LoadFileData/UnloadFileData --------------------------
// Читает файл целиком в malloc-буфер; *dataSize = размер в байтах (0 при ошибке).
// Семантика 1:1 с raylib LoadFileData: NULL при ошибке, буфер освобождается
// парным UnloadFileData (free).
static unsigned char* LoadFileData(const char* path, int* dataSize) {
    if (dataSize) *dataSize = 0;
    std::error_code ec;
    std::uintmax_t sz = fs::file_size(path, ec);
    if (ec) return nullptr;
    std::ifstream f(path, std::ios::binary);
    if (!f) return nullptr;
    unsigned char* buf = (unsigned char*)std::malloc(sz ? (size_t)sz : 1);
    if (!buf) return nullptr;
    if (sz) f.read((char*)buf, (std::streamsize)sz);
    if (!f && sz) { std::free(buf); return nullptr; }
    if (dataSize) *dataSize = (int)sz;
    return buf;
}
static void UnloadFileData(unsigned char* data) { std::free(data); }

Heightmap LoadHeightmapBIN(const std::string& path) {
    Heightmap hm;
    int dataSize = 0;
    unsigned char* raw = LoadFileData(path.c_str(), &dataSize);
    if (!raw || dataSize < 16) {
        if (raw) UnloadFileData(raw);
        return hm;
    }

    uint32_t hdr[4];
    std::memcpy(hdr, raw, 16);
    int w = (int)hdr[1];
    int h = (int)hdr[2];
    uint32_t count = hdr[3];

    // Sanity: the file must hold exactly w*h floats after the header.
    long long need = 16LL + (long long)w * h * 4;
    if (w <= 0 || h <= 0 || count != (uint32_t)(w * h) || need > dataSize) {
        UnloadFileData(raw);
        return hm;
    }

    hm.w = w;
    hm.h = h;
    hm.data.resize((size_t)w * h);
    std::memcpy(hm.data.data(), raw + 16, (size_t)w * h * 4);
    UnloadFileData(raw);

    hm.minH = hm.maxH = hm.data.empty() ? 0.0f : hm.data[0];
    for (float v : hm.data) {
        if (v < hm.minH) hm.minH = v;
        if (v > hm.maxH) hm.maxH = v;
    }
    return hm;
}

static std::string FindMainTex(const fs::path& dir, const std::string& stem) {
    // Preferred names for the draped terrain texture.
    const char* exts[] = { ".TEX", ".tex" };
    std::string candidates[] = { stem, stem + "_big", stem + "_town_big" };
    for (const auto& c : candidates)
        for (const char* e : exts) {
            fs::path p = dir / (c + e);
            if (fs::exists(p)) return p.string();
        }
    // Fallback: first .TEX in the folder that is not a tile/system texture.
    std::error_code ec;
    for (auto& it : fs::directory_iterator(dir, ec)) {
        if (!it.is_regular_file()) continue;
        std::string ext = it.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".tex") return it.path().string();
    }
    return std::string();
}

WaterMesh LoadWaterMesh(const std::string& path) {
    WaterMesh wm;
    int dataSize = 0;
    unsigned char* raw = LoadFileData(path.c_str(), &dataSize);
    if (!raw || dataSize < 0x14) { if (raw) UnloadFileData(raw); return wm; }

    auto i32 = [&](int off) -> int32_t {
        int32_t v = 0;
        if (off >= 0 && off + 4 <= dataSize) std::memcpy(&v, raw + off, 4);
        return v;
    };
    auto f32 = [&](int off) -> float {
        float v = 0.0f;
        if (off >= 0 && off + 4 <= dataSize) std::memcpy(&v, raw + off, 4);
        return v;
    };

    const uint32_t magic = (uint32_t)i32(0);
    const int count = i32(4);
    if (magic != 0x20060214u || count <= 0 || count > 256) {
        UnloadFileData(raw); return wm;
    }

    const int recBase   = 0x08;
    const int recStride = 0x348;
    int geoOff = recBase + count * recStride;   // geometry starts after records

    // texture basename from record 0's Water1 path (offset +0x1c in record)
    {
        int po = recBase + 0x1c;
        if (po + 1 < dataSize) {
            std::string p((const char*)(raw + po), 0);
            for (int k = 0; k < 256 && po + k < dataSize && raw[po + k]; ++k)
                p.push_back((char)raw[po + k]);
            size_t slash = p.find_last_of("/\\");
            wm.texName = (slash == std::string::npos) ? p : p.substr(slash + 1);
        }
    }

    double levelSum = 0.0; int levelN = 0;

    for (int r = 0; r < count; ++r) {
        int ro = recBase + r * recStride;
        int triCount = i32(ro + 0x258);
        int vtxCount = i32(ro + 0x25c);
        int stride   = i32(ro + 0x264);
        if (stride <= 0) stride = 40;

        if (triCount < 0 || vtxCount < 0) break;
        long long idxBytes = (long long)triCount * 3 * 2;
        long long vtxBytes = (long long)vtxCount * stride;
        if (geoOff + idxBytes + vtxBytes > dataSize) break;  // corrupt / out of range

        WaterBody body;
        body.idx.resize((size_t)triCount * 3);
        std::memcpy(body.idx.data(), raw + geoOff, (size_t)idxBytes);
        int vtxOff = geoOff + (int)idxBytes;

        body.pos.resize((size_t)vtxCount * 3);
        body.uv.resize((size_t)vtxCount * 2);
        for (int v = 0; v < vtxCount; ++v) {
            int vo = vtxOff + v * stride;
            float x = f32(vo + 0),  y = f32(vo + 4),  z = f32(vo + 8);
            float u = f32(vo + 16), w = f32(vo + 20);  // floats [4],[5]
            body.pos[v * 3 + 0] = x;
            body.pos[v * 3 + 1] = y;
            body.pos[v * 3 + 2] = z;
            body.uv[v * 2 + 0] = u;
            body.uv[v * 2 + 1] = w;
            levelSum += y; ++levelN;
        }
        if (!body.idx.empty() && !body.pos.empty())
            wm.bodies.push_back(std::move(body));

        geoOff = vtxOff + (int)vtxBytes;
    }

    UnloadFileData(raw);
    if (!wm.bodies.empty()) {
        wm.has = true;
        wm.level = levelN ? (float)(levelSum / levelN) : 0.0f;
    }
    return wm;
}

static std::vector<uint32_t> ReadU32File(const std::string& path) {
    std::vector<uint32_t> v;
    int n = 0;
    unsigned char* d = LoadFileData(path.c_str(), &n);
    if (d) {
        v.resize((size_t)(n / 4));
        std::memcpy(v.data(), d, v.size() * 4);
        UnloadFileData(d);
    }
    return v;
}

PatchTerrain LoadPatchTerrain(const std::string& mapDir) {
    PatchTerrain pt;
    pt.dir = mapDir;

    std::vector<uint32_t> bseq = ReadU32File(mapDir + "/BUFFER.SEQ");
    if (bseq.empty()) return pt;
    std::vector<uint32_t> cseq = ReadU32File(mapDir + "/TerrainColor.SEQ");
    std::vector<uint32_t> aseq = ReadU32File(mapDir + "/TerrainAlpha.SEQ");

    int tvbSize = 0, tibSize = 0;
    unsigned char* tvb = LoadFileData((mapDir + "/BUFFER.TVB").c_str(), &tvbSize);
    unsigned char* tib = LoadFileData((mapDir + "/BUFFER.TIB").c_str(), &tibSize);
    if (!tvb || !tib || tibSize < 8) {
        if (tvb) UnloadFileData(tvb);
        if (tib) UnloadFileData(tib);
        return pt;
    }

    // shared index buffer
    uint32_t imagic, icount;
    std::memcpy(&imagic, tib, 4);
    std::memcpy(&icount, tib + 4, 4);
    if (8 + (long long)icount * 2 <= tibSize) {
        pt.indices.resize(icount);
        std::memcpy(pt.indices.data(), tib + 8, (size_t)icount * 2);
    }
    UnloadFileData(tib);

    const int kStride = 32;
    pt.patches.reserve(bseq.size());
    for (size_t p = 0; p < bseq.size(); ++p) {
        uint32_t o = bseq[p];
        if ((long long)o + 8 > tvbSize) break;
        uint32_t mg, vc;
        std::memcpy(&mg, tvb + o, 4);
        std::memcpy(&vc, tvb + o + 4, 4);
        if (mg != 0x20060117u) break;
        long long need = (long long)o + 8 + (long long)vc * kStride;
        if (need > tvbSize) break;

        TerrainPatch tp;
        tp.vtxCount = (int)vc;
        tp.pos.resize((size_t)vc * 3);
        tp.uv0.resize((size_t)vc * 2);
        tp.uv1.resize((size_t)vc * 2);
        tp.col.resize((size_t)vc * 4);
        tp.colorOff = (p < cseq.size()) ? cseq[p] : 0;
        tp.alphaOff = (p < aseq.size()) ? aseq[p] : 0;
        tp.srcOff   = (unsigned int)(o + 8);   // vertex array begins after the 8-byte patch header

        const unsigned char* vbase = tvb + o + 8;
        float minX = 1e30f, minZ = 1e30f;
        for (uint32_t v = 0; v < vc; ++v) {
            const unsigned char* q = vbase + (size_t)v * kStride;
            float x, y, z; uint32_t diff;
            float u0, v0, u1, v1;
            std::memcpy(&x, q + 0, 4);
            std::memcpy(&y, q + 4, 4);
            std::memcpy(&z, q + 8, 4);
            std::memcpy(&diff, q + 12, 4);
            std::memcpy(&u0, q + 16, 4);
            std::memcpy(&v0, q + 20, 4);
            std::memcpy(&u1, q + 24, 4);
            std::memcpy(&v1, q + 28, 4);
            tp.pos[v * 3 + 0] = x;
            tp.pos[v * 3 + 1] = y;
            tp.pos[v * 3 + 2] = z;
            tp.uv0[v * 2 + 0] = u0;
            tp.uv0[v * 2 + 1] = v0;
            tp.uv1[v * 2 + 0] = u1;
            tp.uv1[v * 2 + 1] = v1;
            // D3DCOLOR ARGB -> RGBA bytes
            tp.col[v * 4 + 0] = (unsigned char)((diff >> 16) & 0xff);
            tp.col[v * 4 + 1] = (unsigned char)((diff >> 8) & 0xff);
            tp.col[v * 4 + 2] = (unsigned char)(diff & 0xff);
            tp.col[v * 4 + 3] = (unsigned char)((diff >> 24) & 0xff);
            if (x < minX) minX = x;
            if (z < minZ) minZ = z;
        }
        tp.minX = minX;
        tp.minZ = minZ;
        pt.patches.push_back(std::move(tp));
    }
    UnloadFileData(tvb);

    // <map>.TER (FUN_01133580, magic 0x20050802) is the real detail-tile source
    // (NOT the stale ExTileInfo.inf editor text). Layout:
    //   header 0x2f6c bytes at file+12:
    //     +0x14c  name table, 40 x 0x100 ASCII tile texture paths
    //     +0x2da4 u32 arr1 count, +0x29f0 u32 patch count
    //   patch records (0x198 each) at file + 12 + 0x2f6c + arr1count*0x14:
    //     +0x108 i32 tileA name index, +0x10c i32 tileB name index
    //     +0x08  f32 tileA uv scale,   +0x48  f32 tileB uv scale
    // Per patch the two named tiles are blended by the TerrainAlpha mask.
    {
        std::string terPath;
        std::error_code ec;
        for (auto& it : fs::directory_iterator(mapDir, ec)) {
            std::string ext = it.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".ter") { terPath = it.path().string(); break; }
        }
        int tsz = 0;
        unsigned char* ter = terPath.empty() ? nullptr
                                             : LoadFileData(terPath.c_str(), &tsz);
        uint32_t magic = 0;
        if (ter && tsz > 0x2f78) std::memcpy(&magic, ter, 4);
        if (ter && magic == 0x20050802u) {
            const int H = 12;
            auto i32at = [&](long long o) -> int32_t {
                int32_t v = 0; if (o >= 0 && o + 4 <= tsz) std::memcpy(&v, ter + o, 4); return v;
            };
            auto f32at = [&](long long o) -> float {
                float v = 0; if (o >= 0 && o + 4 <= tsz) std::memcpy(&v, ter + o, 4); return v;
            };
            auto nameAt = [&](int idx) -> std::string {
                if (idx < 0 || idx >= 40) return std::string();
                long long o = H + 0x14c + (long long)idx * 0x100;
                std::string s;
                for (int k = 0; k < 0x100 && o + k < tsz && ter[o + k]; ++k)
                    s.push_back((char)ter[o + k]);
                size_t sl = s.find_last_of("/\\");
                return (sl == std::string::npos) ? s : s.substr(sl + 1);
            };
            uint32_t cA = (uint32_t)i32at(H + 0x2da4);
            long long patchBase = (long long)H + 0x2f6c + (long long)cA * 0x14;
            for (size_t p = 0; p < pt.patches.size(); ++p) {
                long long po = patchBase + (long long)p * 0x198;
                if (po + 0x198 > tsz) break;
                int   ia = i32at(po + 0x108), ib = i32at(po + 0x10c);
                float sa = f32at(po + 0x08),  sb = f32at(po + 0x48);
                TerrainPatch& tp = pt.patches[p];
                if (ia >= 0) { tp.detail0 = nameAt(ia); if (sa > 0.01f) tp.scale0 = sa; }
                if (ib >= 0) { tp.detail1 = nameAt(ib); if (sb > 0.01f) tp.scale1 = sb; }
            }
        }
        if (ter) UnloadFileData(ter);
    }

    pt.has = !pt.patches.empty() && !pt.indices.empty();
    return pt;
}

Model3D LoadJBM(const std::string& path) {
    Model3D mdl;
    int n = 0;
    unsigned char* b = LoadFileData(path.c_str(), &n);
    if (!b || n < 4 + 0x20c) { if (b) UnloadFileData(b); return mdl; }

    uint32_t magic; std::memcpy(&magic, b, 4);
    if (magic != 0x20050706u) { UnloadFileData(b); return mdl; }

    const int H = 4;  // header begins after the 4-byte magic
    auto u32 = [&](long long o) -> uint32_t {
        uint32_t v = 0; if (o >= 0 && o + 4 <= n) std::memcpy(&v, b + o, 4); return v;
    };
    auto f32 = [&](long long o) -> float {
        float v = 0; if (o >= 0 && o + 4 <= n) std::memcpy(&v, b + o, 4); return v;
    };

    uint32_t meshCount = u32(H + 0x04);
    uint32_t matCount  = u32(H + 0x30);
    uint32_t blobSize  = u32(H + 0x204);
    if (meshCount == 0 || meshCount > 4096) { UnloadFileData(b); return mdl; }

    {   // model name
        long long o = H + 0x178;
        for (int k = 0; k < 0x60 && o + k < n && b[o + k]; ++k) mdl.name.push_back((char)b[o + k]);
    }

    long long blobStart   = H + 0x20c;
    long long matTable     = blobStart;
    long long meshRecBase  = matTable + (long long)matCount * 0x20;
    long long geoPtr       = meshRecBase + (long long)meshCount * 0x29c;
    long long blobEnd      = blobStart + blobSize;
    if (blobEnd > n) blobEnd = n;

    {   // mat0 texture filename (basename)
        std::string t;
        for (int k = 0; k < 0x20 && matTable + k < n && b[matTable + k]; ++k)
            t.push_back((char)b[matTable + k]);
        size_t sl = t.find_last_of("/\\");
        mdl.texture = (sl == std::string::npos) ? t : t.substr(sl + 1);
    }

    for (uint32_t m = 0; m < meshCount; ++m) {
        long long mr = meshRecBase + (long long)m * 0x29c;
        uint32_t vtx = u32(mr + 0x28);
        uint32_t tri = u32(mr + 0x2c);
        long long idxBytes = (long long)tri * 3 * 2;
        long long vtxBytes = (long long)vtx * 0x2c;
        if (geoPtr + idxBytes + vtxBytes > blobEnd) break;   // corrupt / unsupported

        ModelMesh mesh;
        mesh.idx.resize((size_t)tri * 3);
        std::memcpy(mesh.idx.data(), b + geoPtr, (size_t)idxBytes);

        long long vbase = geoPtr + idxBytes;
        mesh.pos.resize((size_t)vtx * 3);
        mesh.nrm.resize((size_t)vtx * 3);
        mesh.uv.resize((size_t)vtx * 2);
        for (uint32_t v = 0; v < vtx; ++v) {
            long long o = vbase + (long long)v * 0x2c;
            mesh.pos[v * 3 + 0] = f32(o + 0);
            mesh.pos[v * 3 + 1] = f32(o + 4);
            mesh.pos[v * 3 + 2] = f32(o + 8);
            mesh.nrm[v * 3 + 0] = f32(o + 12);
            mesh.nrm[v * 3 + 1] = f32(o + 16);
            mesh.nrm[v * 3 + 2] = f32(o + 20);
            mesh.uv[v * 2 + 0]  = f32(o + 28);
            mesh.uv[v * 2 + 1]  = f32(o + 32);
        }
        mdl.meshes.push_back(std::move(mesh));

        // advance past indices + vertices + the 0xc-byte extra stream
        geoPtr = vbase + vtxBytes + (long long)vtx * 0x0c;
    }

    mdl.has = !mdl.meshes.empty();
    UnloadFileData(b);
    return mdl;
}

static bool IsNameChar(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

std::vector<AniClip> LoadAniList(const std::string& mapDir) {
    std::vector<AniClip> clips;
    int n = 0;
    unsigned char* b = LoadFileData((mapDir + "/ani_list.ani").c_str(), &n);
    if (!b) return clips;
    auto U = [&](long long o) -> uint32_t { uint32_t v = 0; if (o >= 0 && o + 4 <= n) std::memcpy(&v, b + o, 4); return v; };
    auto F = [&](long long o) -> float { float v = 0; if (o >= 0 && o + 4 <= n) std::memcpy(&v, b + o, 4); return v; };
    auto name = [&](long long o) -> std::string { std::string s; for (int k = 0; k < 0x20 && o + k < n && b[o+k] >= 0x20 && b[o+k] < 0x7f; ++k) s.push_back((char)b[o+k]); return s; };
    if (U(0) != 0x20041019u) { UnloadFileData(b); return clips; }
    uint32_t count = U(8);
    if (count == 0 || count > 100000) { UnloadFileData(b); return clips; }
    const long long blob = 12;
    long long d = (long long)count * 0x40;                     // cursor past object headers
    for (uint32_t c = 0; c < count; ++c) {
        long long e = blob + (long long)c * 0x40;
        int boneCount = (int)U(e + 0x20);
        AniClip clip; clip.name = name(e);
        size_t dot = clip.name.rfind('.'); if (dot != std::string::npos) clip.name = clip.name.substr(0, dot);
        if (boneCount < 0 || boneCount > 100000) { UnloadFileData(b); return clips; }
        long long boneHdr = d + (long long)boneCount * 4;      // parent array then bone headers
        long long dd = boneHdr + (long long)boneCount * 0x94;  // keyframe data starts here
        for (int s = 0; s < boneCount; ++s) {
            long long se = blob + boneHdr + (long long)s * 0x94;
            int keyCount = (int)U(se + 0x20);
            AniBone bone; bone.name = name(se);
            int par = (int)U(blob + d + (long long)s * 4);     // parent-index array (-1 = root)
            bone.parent = (par >= 0 && par < boneCount && par != s) ? par : -1;
            float cv = F(se + 0x90);                            // CONVERT_MATRIX_VALUE (bone +0x90)
            bone.convert = (std::isfinite(cv) && std::fabs(cv) > 1e-6f) ? cv : 1.0f;
            if (keyCount < 0 || keyCount > 2000000) { UnloadFileData(b); return clips; }
            bone.keys.reserve(keyCount);
            for (int k = 0; k < keyCount; ++k) {
                long long ko = blob + dd + (long long)k * 0x2c;
                AniKey key;
                key.t = F(ko);
                key.pos[0] = F(ko+4); key.pos[1] = F(ko+8); key.pos[2] = F(ko+0xc);
                key.rot[0] = F(ko+0x10); key.rot[1] = F(ko+0x14); key.rot[2] = F(ko+0x18); key.rot[3] = F(ko+0x1c);
                key.scale[0] = F(ko+0x20); key.scale[1] = F(ko+0x24); key.scale[2] = F(ko+0x28);
                bone.keys.push_back(key);
            }
            if (!bone.keys.empty()) clip.duration = std::max(clip.duration, bone.keys.back().t);
            dd += (long long)keyCount * 0x2c;
            clip.bones.push_back(std::move(bone));
        }
        d = dd;
        clips.push_back(std::move(clip));
    }
    UnloadFileData(b);
    return clips;
}

MapObjects LoadMapObjects(const std::string& mapDir) {
    MapObjects mo;

    // ===========================================================================================
    // Faithful reproduction of the game's map-object loader (FUN_011dbcf0 + FUN_011dd920 + the
    // .PBB loaders), validated against the engine's own invariants (offset==fullSize, meshCount==
    // Pos.LIN count) on every patch of every map. See RE/nettools/posval.py.
    //
    //   * <map>.TER (magic 0x20050802) holds per-patch COUNTS: section records 0x198 apart at
    //     12 + 0x2f6c + tableCount*0x14; per patch cA@+0x110, cB@+0x114, cC@+0x118, cD@+0x11c, cE@+0x120.
    //   * Pos.POS block per patch = sections in file order A,B,C,E,D:
    //       A: cA instances(0x20c, meshCount@+4, flag@+0) -> mesh records(0x29c) -> flag&2 render
    //          structs(mc*(0x190+0x74+4)) -> per-mesh 0x40 sub-arrays (mesh[+0x244] entries; one is
    //          the WORLD MATRIX).
    //       B: cB groups(0x1cc, instCount@+4, idxList@+8) -> sub-instances(0x20c) -> records(0x29c)
    //          -> flag&2 structs -> per-mesh (flag&0x200?0xcc : rec[+0x1f8]*4 + rec[+0x244]*0x40)
    //          -> per-group idxList grp[+8]*4.
    //       C: cC*0x438 ; E: cE*0x138 (non-render) ; D: cD instances(0x20c) -> records(0x29c) ->
    //          per-mesh 0x80 slot (zero in file -> section D has no rotation).
    //   * Pos.LIN renders sections A,B,D in order; 4 u32 per mesh = [geomIdx, slot, startIdx, startVtx].
    //   * GEOMETRY POOL BY SECTION: A->Pos0.PBB, B->Pos1.PBB, D->Pos2.PBB. Each render mesh is a slice
    //     verts[startVtx..+vtxCount], indices[startIdx..next] of the patch's pool buffer.
    //   * Placement: A/B use the world matrix (rotation baked in, translation = position); D uses the
    //     mesh record world position (+0x174), no rotation.
    // ===========================================================================================
    // Full FVF->stride table (engine FUN_0111d670, all 13 entries). An unknown FVF here used to
    // abort the whole patch's buffer walk (ok=false) and drop every later mesh — so completeness
    // matters. 0 = unsupported (skip just that mesh).
    auto strideFor = [](uint32_t fvf) -> int {
        switch (fvf) {
            case 0x002: return 12; case 0x042: return 16; case 0x044: return 20;
            case 0x102: return 20; case 0x104: return 24; case 0x142: return 24;
            case 0x144: return 28; case 0x152: return 36; case 0x242: return 32;
            case 0x252: return 44; case 0x256: return 48; case 0x304: return 40;
            case 0x60252: return 72; default: return 0;
        }
    };
    // Byte offset of uv0 (first texcoord); -1 = vertex has no UV. Derived from D3D FVF layout
    // (position size + NORMAL/DIFFUSE), verified vs the engine. NOTE 0x256 = XYZB1 (a 4-byte blend
    // weight before NORMAL) so its uv0 is at 32, not 28.
    auto uvOff = [](uint32_t fvf) -> int {
        switch (fvf) {
            case 0x002: case 0x042: case 0x044: return -1;
            case 0x102: return 12; case 0x104: return 16; case 0x142: return 16;
            case 0x144: return 20; case 0x152: return 28; case 0x242: return 16;
            case 0x252: return 28; case 0x256: return 32; case 0x304: return 16;
            case 0x60252: return 28; default: return -1;
        }
    };
    // Byte offset of the vertex NORMAL (D3DFVF_NORMAL, bit 0x10); -1 = no normal in this FVF.
    // Only these four carry it: normal sits right after the position (XYZ=12, or XYZB1=12+4 for 0x256).
    auto normalOff = [](uint32_t fvf) -> int {
        switch (fvf) {
            case 0x152: case 0x252: case 0x60252: return 12;  // pos(12) -> normal
            case 0x256: return 16;                            // pos(12)+blendweight(4) -> normal
            default: return -1;
        }
    };

    // ---- 1. PBB geometry pools, per patch, per file (Pos0/Pos1/Pos2.PBB) -------------------
    struct PoolMesh { long long vtxOff; int vtxCount; uint32_t fvf; int stride; long long idxOff; int idxCount; };
    struct PoolPatch { std::vector<PoolMesh> meshes; std::vector<std::string> typeTex; };
    std::vector<std::vector<PoolPatch>> pools(3);
    unsigned char* poolData[3] = { nullptr, nullptr, nullptr };
    const char* pbbName[3] = { "/Pos0.PBB", "/Pos1.PBB", "/Pos2.PBB" };
    for (int f = 0; f < 3; ++f) {
        int n = 0;
        unsigned char* b = LoadFileData((mapDir + pbbName[f]).c_str(), &n);
        poolData[f] = b;
        if (!b) continue;
        auto U = [&](long long o) -> uint32_t { uint32_t v = 0; if (o >= 0 && o + 4 <= n) std::memcpy(&v, b + o, 4); return v; };
        long long c = 8;
        while (c + 0x20 <= n) {
            uint32_t count = U(c + 4), bs = U(c + 0x1c);
            PoolPatch pp;
            if (bs == 0) { pools[f].push_back(pp); c += 0x20; continue; }
            long long data = c + 0x20;
            uint32_t mc0 = U(data + (long long)count * 0x20 + (long long)count * 0x100);
            if (mc0 == 0 || mc0 > 4096) { pools[f].push_back(pp); break; }
            for (uint32_t t = 0; t < count && t < 4096; ++t) {
                long long so = data + (long long)count * 0x20 + (long long)t * 0x100;
                std::string full;
                for (int k = 0; k < 0x100 && so + k < n && b[so + k]; ++k) full.push_back((char)b[so + k]);
                size_t sl = full.find_last_of("/\\");
                pp.typeTex.push_back(sl == std::string::npos ? full : full.substr(sl + 1));
            }
            long long o = data + bs;
            std::vector<std::pair<long long, int>> idxs;
            bool ok = true;
            for (uint32_t i = 0; i < mc0; ++i) {
                uint32_t ib = U(o);
                if (o + 4 + ib > n) { ok = false; break; }
                idxs.push_back({ o + 4, (int)(ib / 2) }); o += 4 + ib;
            }
            for (uint32_t i = 0; i < mc0 && ok; ++i) {
                uint32_t vb = U(o), fvf = U(o + 4);
                int st = strideFor(fvf);
                if (st == 0 || vb == 0 || vb % st != 0 || o + 8 + vb > n) { ok = false; break; }
                pp.meshes.push_back({ o + 8, (int)(vb / st), fvf, st, idxs[i].first, idxs[i].second });
                o += 8 + vb;
            }
            if (!ok) { pools[f].push_back(pp); break; }
            pools[f].push_back(std::move(pp));
            c = o;
        }
    }

    // Local geometry of ONE buffer = pools[pool][patch].meshes[slot]. A Pos.LIN entry carries [geomIdx
    // (type/texture), slot (which buffer), startIdx, startVtx]; startVtx/startIdx are LOCAL to that buffer
    // (NOT a concatenation of all buffers), and index values are relative to startVtx (D3D BaseVertexIndex).
    struct Buffer { std::vector<float> pos, nrm, uv; std::vector<unsigned short> idx; int vtxCount = 0; };
    std::map<std::array<int, 3>, Buffer> bufCache;
    auto getBuffer = [&](int patch, int pool, int slot) -> Buffer* {
        std::array<int, 3> k{ patch, pool, slot };
        auto it = bufCache.find(k);
        if (it != bufCache.end()) return &it->second;
        Buffer bf;
        if (pool >= 0 && pool < (int)pools.size() && patch < (int)pools[pool].size()) {
            const PoolPatch& pp = pools[pool][patch];
            if (slot >= 0 && slot < (int)pp.meshes.size()) {
                unsigned char* b = poolData[pool];
                auto F = [&](long long o) -> float { float v = 0; if (b && o >= 0) std::memcpy(&v, b + o, 4); return v; };
                const PoolMesh& pm = pp.meshes[slot];
                int u2 = uvOff(pm.fvf);
                int n2 = normalOff(pm.fvf);
                for (int v = 0; v < pm.vtxCount; ++v) {
                    long long q = pm.vtxOff + (long long)v * pm.stride;
                    bf.pos.push_back(F(q)); bf.pos.push_back(F(q + 4)); bf.pos.push_back(F(q + 8));
                    if (n2 >= 0) { bf.nrm.push_back(F(q + n2)); bf.nrm.push_back(F(q + n2 + 4)); bf.nrm.push_back(F(q + n2 + 8)); }
                    else         { bf.nrm.push_back(0.0f); bf.nrm.push_back(0.0f); bf.nrm.push_back(0.0f); }
                    if (u2 >= 0) { bf.uv.push_back(F(q + u2)); bf.uv.push_back(F(q + u2 + 4)); }
                    else         { bf.uv.push_back(0.0f); bf.uv.push_back(0.0f); }
                }
                bf.vtxCount = pm.vtxCount;
                for (int j = 0; j < pm.idxCount; ++j) {
                    uint16_t v = 0; if (b) std::memcpy(&v, b + pm.idxOff + (long long)j * 2, 2);
                    bf.idx.push_back(v);
                }
            }
        }
        auto res = bufCache.emplace(k, std::move(bf));
        return &res.first->second;
    };
    auto texOf = [&](int patch, int pool, int geomIdx) -> std::string {
        if (pool < (int)pools.size() && patch < (int)pools[pool].size()) {
            const auto& tt = pools[pool][patch].typeTex;
            if (geomIdx >= 0 && geomIdx < (int)tt.size()) return tt[geomIdx];
            if (!tt.empty()) return tt[0];
        }
        return std::string();
    };

    // ---- 2. <map>.TER per-patch counts ----------------------------------------------------
    std::vector<std::array<int, 5>> counts;     // [patch] = {cA,cB,cC,cD,cE}
    {
        // the TER file is named after the map folder (e.g. alpen/alpen.TER)
        std::string mapName = mapDir;
        size_t sl = mapName.find_last_of("/\\");
        if (sl != std::string::npos) mapName = mapName.substr(sl + 1);
        int tn = 0;
        unsigned char* tb = LoadFileData((mapDir + "/" + mapName + ".TER").c_str(), &tn);
        if (tb) {
            auto T = [&](long long o) -> uint32_t { uint32_t v = 0; if (o >= 0 && o + 4 <= tn) std::memcpy(&v, tb + o, 4); return v; };
            if (T(0) == 0x20050802u) {
                uint32_t tableCount = T(12 + 0x2da4), sectionCount = T(12 + 0x29f0);
                long long secStart = 12 + 0x2f6c + (long long)tableCount * 0x14;
                for (uint32_t P = 0; P < sectionCount && P < 4096; ++P) {
                    long long r = secStart + (long long)P * 0x198;
                    counts.push_back({ (int)T(r + 0x110), (int)T(r + 0x114), (int)T(r + 0x118),
                                       (int)T(r + 0x11c), (int)T(r + 0x120) });
                }
            }
            UnloadFileData(tb);
        }
    }

    // ---- 3. Pos.LIN per-patch entries -----------------------------------------------------
    struct Lin { uint32_t geomIdx, slot, startIdx, startVtx; };
    std::vector<std::vector<Lin>> linPatch;
    {
        int ln = 0;
        unsigned char* lb = LoadFileData((mapDir + "/Pos.LIN").c_str(), &ln);
        if (lb) {
            auto U = [&](long long o) -> uint32_t { uint32_t v = 0; if (o >= 0 && o + 4 <= ln) std::memcpy(&v, lb + o, 4); return v; };
            long long o = 0;
            while (o + 4 <= ln) {
                uint32_t cnt = U(o);
                if (cnt == 0) { linPatch.push_back({}); o += 4; continue; }
                if (o + 4 + (long long)cnt * 4 > ln) break;
                int m = (int)(cnt / 4);
                std::vector<Lin> e; e.reserve(m);
                for (int k = 0; k < m; ++k) {
                    long long b = o + 4 + (long long)k * 16;
                    e.push_back({ U(b), U(b + 4), U(b + 8), U(b + 12) });
                }
                linPatch.push_back(std::move(e));
                o += 4 + (long long)cnt * 4;
            }
            UnloadFileData(lb);
        }
    }

    // ---- 4. Pos.POS: walk sections A,B,D per patch, enumerate render meshes in Pos.LIN order
    struct Node { float x, y, z; int section; int vtxCount; int triCount; int groupB; bool hasMat; float m[16]; long long posOff = -1; long long rawPosOff = -1; long long matArrOff = -1; int matCount = 0; long long recOff = -1; std::string model; std::string node; std::vector<int> boneRemap; };
    struct PatchMesh { std::vector<Node> nodes; int mcA = 0, mcB = 0;
        std::vector<std::array<float,16>> bGroupMat;  // section-B group world matrix (grpBase+0x168) per group
        std::vector<int> bGroupClip;                  // section-B group ani-clip index (idxList[0]) per group
        std::vector<int> instAmc;                    // mc count for each section A instance (for single-mesh check)
    };
    std::vector<PatchMesh> patchMesh;
    int pn = 0;
    unsigned char* pb = LoadFileData((mapDir + "/Pos.POS").c_str(), &pn);
    if (pb) {
        const long long MR = 0x29c;
        auto U = [&](long long o) -> uint32_t { uint32_t v = 0; if (o >= 0 && o + 4 <= pn) std::memcpy(&v, pb + o, 4); return v; };
        auto F = [&](long long o) -> float { float v = 0; if (o >= 0 && o + 4 <= pn) std::memcpy(&v, pb + o, 4); return v; };
        auto ortho = [&](const float* m) {
            auto ul = [](float a, float b, float c) { float l = a*a + b*b + c*c; return l > 0.94f && l < 1.06f; };
            return ul(m[0],m[1],m[2]) && ul(m[4],m[5],m[6]) && ul(m[8],m[9],m[10]);
        };
        long long cur = 12; int P = 0;
        while (cur + 8 <= pn) {
            uint32_t full = U(cur);
            if (full == 0) { patchMesh.push_back({}); P++; cur += 8; continue; }
            if (cur + 8 + (long long)full > (long long)pn + 16) break;
            long long base = cur + 8;
            auto BU = [&](long long o) { return U(base + o); };
            auto BF = [&](long long o) { return F(base + o); };
            // model name stored at instance +0x178 (".jbm" / ".JBM"); return it without extension
            auto readName = [&](long long instOff) -> std::string {
                long long n = base + instOff + 0x178; std::string s;
                for (int k = 0; k < 0x40 && n + k <= pn - 1 && pb[n + k] >= 0x20 && pb[n + k] < 0x7f; ++k) s.push_back((char)pb[n + k]);
                size_t d = s.rfind('.');
                if (d != std::string::npos && d >= 1) s = s.substr(0, d);
                return s;
            };
            std::array<int,5> c5 = (P < (int)counts.size()) ? counts[P] : std::array<int,5>{0,0,0,0,0};
            int cA = c5[0], cB = c5[1], cC = c5[2], cD = c5[3], cE = c5[4];
            auto badc = [](int v) { return v < 0 || v > 200000; };
            if (badc(cA) || badc(cB) || badc(cC) || badc(cD) || badc(cE)) { patchMesh.push_back({}); cur = base + full; P++; continue; }

            // finds a world matrix in [arrOff, arrOff+entries*0x40) whose translation is near (px,pz)
            // The model's WORLD matrix lives in the per-mesh 0x40-array (the filler entry before it
            // is non-orthonormal). Its translation T is the MODEL ORIGIN, which can sit far from this
            // part's center (+0x174) because the part's offset is baked into the local geometry. So we
            // accept the first orthonormal entry whose T is inside the map, NOT the one nearest the part.
            // The first 0x40 entry is a filler: either non-orthonormal garbage or an identity matrix
            // with T=(0,0,0). The real world matrix (entry 1, copied to entry 2) is orthonormal with T
            // = the model origin inside the map. So require ortho + in-map T + T not at the world origin.
            auto matValid = [&](const float* m) -> bool {
                return ortho(m)
                    && m[12] > -4000.0f && m[12] < 70000.0f
                    && m[14] > -4000.0f && m[14] < 70000.0f
                    && m[13] > -8000.0f && m[13] < 20000.0f
                    && !(std::fabs(m[12]) < 1.0f && std::fabs(m[14]) < 1.0f);   // skip identity/zero-T filler
            };
            auto findMat = [&](long long arrOff, int entries, float px, float pz, float* out, long long* outMo = nullptr) -> bool {
                (void)px; (void)pz;
                for (int e = 0; e < entries; ++e) {
                    long long mo = arrOff + (long long)e * 0x40;
                    float m[16]; for (int k = 0; k < 16; ++k) m[k] = BF(mo + k * 4);
                    if (matValid(m)) { for (int k = 0; k < 16; ++k) out[k] = m[k]; if (outMo) *outMo = mo; return true; }
                }
                return false;
            };
            // Last valid matrix in a per-mesh array. Section B per-mesh blocks chain filler/intermediate
            // matrices before the real per-part world matrix (flag&0x200 block = 3 x 0x40 = [identity
            // filler, group/model origin, per-part WORLD]), so the world matrix is the LAST valid entry,
            // not the first. Verified on the alpen portal: the centred knob's last matrix carries its
            // mid-height translation and tilt, while the first/group matrix drops it to the model origin.
            auto findMatLast = [&](long long arrOff, int entries, float* out, long long* outMo = nullptr) -> bool {
                for (int e = entries - 1; e >= 0; --e) {
                    long long mo = arrOff + (long long)e * 0x40;
                    float m[16]; for (int k = 0; k < 16; ++k) m[k] = BF(mo + k * 4);
                    if (matValid(m)) { for (int k = 0; k < 16; ++k) out[k] = m[k]; if (outMo) *outMo = mo; return true; }
                }
                return false;
            };

            PatchMesh pm;
            pm.instAmc.assign(cA, 0);
            // ---- Section A (pool 0) ----
            std::vector<int> mcAi(cA);
            std::vector<char> f2A(cA);
            long long mcA = 0;
            for (int i = 0; i < cA; ++i) { mcAi[i] = (int)BU((long long)i*0x20c + 4); f2A[i] = (BU((long long)i*0x20c) & 2) != 0; mcA += mcAi[i]; }
            long long recA = (long long)cA * 0x20c;
            long long off = recA + mcA * MR;
            for (int i = 0; i < cA; ++i) if (f2A[i]) off += (long long)mcAi[i] * (0x190 + 0x74 + 4);
            { long long j = 0;
              for (int i = 0; i < cA; ++i) { std::string nm = readName((long long)i*0x20c); pm.instAmc[i] = mcAi[i];
                for (int m = 0; m < mcAi[i]; ++m) {
                  long long ro = recA + j * MR; int p244 = (int)BU(ro + 0x244);
                  Node nd{}; nd.section = 0; nd.x = BF(ro+0x174); nd.y = BF(ro+0x178); nd.z = BF(ro+0x17c);
                  nd.vtxCount = (int)BU(ro + 0x190); nd.triCount = (int)BU(ro + 0x2c); nd.hasMat = false; nd.model = nm;
                  nd.groupB = i;   // reuse field to carry section-A instance index (for mc lookup later)
                  nd.posOff = base + ro + 0x174;   // primary offset (overridden to matrix-T below if matrix-placed)
                  nd.rawPosOff = base + ro + 0x174; // raw node x/y/z, kept even when matrix-placed
                  nd.recOff = base + ro; nd.matArrOff = base + off; nd.matCount = (p244 > 0 && p244 < 256) ? p244 : 0;
                  if (p244 > 0 && p244 < 256) { float mm[16]; long long matRel = -1; if (findMat(off, p244, nd.x, nd.z, mm, &matRel)) { for (int k=0;k<16;++k) nd.m[k]=mm[k]; nd.hasMat = true; nd.posOff = base + matRel + 0x30; } }
                  pm.nodes.push_back(nd);
                  off += (long long)((p244 > 0 && p244 < 256) ? p244 : 0) * 0x40; j++;
              } } }
            pm.mcA = (int)mcA;
            // ---- Section B (pool 1) ----
            // Each section-B mesh carries its OWN per-mesh world matrix, exactly like sections A and D.
            // The matrix lives in the per-mesh block that follows the mesh records: for flag&0x200 meshes
            // the block is 0xcc = [0x0c header][3 x 0x40 matrices] (M0 identity filler, M1 group/model
            // origin, M2 = per-part WORLD); for the rest it is [rec[+0x1f8]*4][rec[+0x244] x 0x40] and the
            // world matrix is again the last valid entry. Earlier the loader applied ONE group matrix
            // (grpRec+0x168 == M1) to every part, which dropped centred parts to the model origin and
            // shifted surfaces (the broken alpen portals). The engine uses the per-mesh world matrix (M2).
            long long grpBase = off; off += (long long)cB * 0x1cc;
            pm.bGroupMat.assign(cB, std::array<float,16>{});
            pm.bGroupClip.assign(cB, -1);
            for (int g = 0; g < cB; ++g)                       // group world matrix (model->world placement)
                for (int k = 0; k < 16; ++k) pm.bGroupMat[g][k] = BF(grpBase + (long long)g*0x1cc + 0x168 + k*4);
            std::vector<int> mcBi; std::vector<char> f2B; std::vector<std::string> bNames; std::vector<int> subGroup;
            long long curB = off;
            for (int g = 0; g < cB; ++g) { int ic = (int)BU(grpBase + (long long)g*0x1cc + 4);
                for (int k = 0; k < ic; ++k) { mcBi.push_back((int)BU(curB + 4)); f2B.push_back((BU(curB) & 2) != 0); bNames.push_back(readName(curB)); subGroup.push_back(g); curB += 0x20c; } }
            off = curB;
            long long mcB = 0; for (int v : mcBi) mcB += v;
            long long recB = off; off += mcB * MR;
            for (size_t s = 0; s < mcBi.size(); ++s) if (f2B[s]) off += (long long)mcBi[s] * (0x190 + 0x74 + 4);
            { long long j = 0;
              for (size_t s = 0; s < mcBi.size(); ++s) for (int m = 0; m < mcBi[s]; ++m) {
                  long long ro = recB + j * MR; uint32_t fl = BU(ro);
                  Node nd{}; nd.section = 1; nd.x = BF(ro+0x174); nd.y = BF(ro+0x178); nd.z = BF(ro+0x17c);
                  nd.vtxCount = (int)BU(ro + 0x190); nd.triCount = (int)BU(ro + 0x2c); nd.hasMat = false; nd.groupB = subGroup[s]; nd.model = (s < bNames.size()) ? bNames[s] : std::string();
                  nd.posOff = base + ro + 0x174;   // primary offset (overridden to matrix-T below if matrix-placed)
                  nd.rawPosOff = base + ro + 0x174; // raw node x/y/z, kept even when matrix-placed
                  { long long nn = base + ro + 0x1fc; for (int k = 0; k < 0x40 && nn + k <= pn - 1 && pb[nn+k] >= 0x20 && pb[nn+k] < 0x7f; ++k) nd.node.push_back((char)pb[nn+k]); }  // 3DS Max node name (== ani bone)
                  // locate this mesh's per-mesh matrix array, then advance the cursor exactly as the engine.
                  long long matArr; int mcnt; long long inlineOff = off;
                  if (fl & 0x200) { matArr = off + 0xc; mcnt = 3; off += 0xcc; }
                  else {
                      matArr = off + (long long)BU(ro + 0x1f8) * 4;
                      mcnt = (int)BU(ro + 0x244);
                      off = matArr + (long long)((mcnt > 0 && mcnt < 256) ? mcnt : 0) * 0x40;
                  }
                  nd.recOff = base + ro; nd.matArrOff = base + matArr; nd.matCount = (mcnt > 0 && mcnt < 256) ? mcnt : 0;
                  if (mcnt > 0 && mcnt < 256) { float mm[16]; long long matRel = -1; if (findMatLast(matArr, mcnt, mm, &matRel)) { for (int k=0;k<16;++k) nd.m[k]=mm[k]; nd.hasMat = true; nd.posOff = base + matRel + 0x30; } }
                  // SKIN remap: for FVF 0x256 meshes the engine stores, right before the matrix array,
                  // an int[skinCnt] array (mesh+0x24c) mapping palette-slot s>=2 -> .ani bone = inline[s-2]
                  // (FUN_01146300 skin branch: local_d8 from 2, bone = mesh+0x24c[local_d8-2]; slots 0/1 are
                  // reserved placement). Vertex register at +12 = slot*3+8. Read it for the skin path.
                  if (!(fl & 0x200)) {
                      int sc = (int)BU(ro + 0x1f8);
                      if (sc > 0 && sc < 256) { nd.boneRemap.resize(sc); for (int k = 0; k < sc; ++k) nd.boneRemap[k] = (int)BU(inlineOff + (long long)k*4); }
                  }
                  pm.nodes.push_back(nd); j++;
              } }
            for (int g = 0; g < cB; ++g) {                     // idxList per group: first entry = ani-clip index
                int nidx = (int)BU(grpBase + (long long)g*0x1cc + 8);
                if (nidx > 0 && g < (int)pm.bGroupClip.size()) pm.bGroupClip[g] = (int)BU(off);
                off += (long long)nidx * 4;
            }
            pm.mcB = (int)mcB;
            // ---- Section C = visual effects (0x438), section E = 3D sounds (0x138) ----
            // Not render geometry, but we collect position + name so the viewer can mark them.
            auto readStrBase = [&](long long o, int maxn) -> std::string {
                long long n = base + o; std::string s;
                for (int k = 0; k < maxn && n + k <= pn - 1 && pb[n + k] >= 0x20 && pb[n + k] < 0x7f; ++k) s.push_back((char)pb[n + k]);
                size_t sl = s.find_last_of("/\\"); if (sl != std::string::npos) s = s.substr(sl + 1);
                return s;
            };
            for (int i = 0; i < cC; ++i) {
                long long r = off + (long long)i * 0x438;
                MapEffect ef;
                ef.pos = { BF(r + 0x2f8), BF(r + 0x2fc), BF(r + 0x300) };
                ef.type = (int)BU(r + 0x314);
                ef.attachInst = (int)BU(r + 0x434);
                ef.model = readStrBase(r + 0x32c, 0x100);
                size_t d = ef.model.rfind('.'); if (d != std::string::npos) ef.model = ef.model.substr(0, d);
                // ATTACH: the engine composes the effect onto its parent's WORLD MATRIX.
                //  type 1 -> section-A instance: matrix at sectionA_base + attach*0x20c + 0x1b0 (FUN ~755454).
                //  type 2 -> section-B group: matrix at grpBase + attach*0x1cc + 0x168 == pm.bGroupMat[attach] (FUN ~755493).
                if (ef.type == 1 && ef.attachInst >= 0 && ef.attachInst < cA) {
                    long long mo2 = (long long)ef.attachInst * 0x20c + 0x1b0;
                    for (int k = 0; k < 16; ++k) ef.pmat[k] = BF(mo2 + k * 4);
                    ef.attached = true;
                } else if (ef.type == 2 && ef.attachInst >= 0 && ef.attachInst < (int)pm.bGroupMat.size()) {
                    for (int k = 0; k < 16; ++k) ef.pmat[k] = pm.bGroupMat[ef.attachInst][k];
                    ef.bone = (int)BU(r + 0x31c);                                   // attach BONE inside the parent skeleton
                    ef.clipIdx = (ef.attachInst < (int)pm.bGroupClip.size()) ? pm.bGroupClip[ef.attachInst] : -1;
                    ef.attached = true;
                }
                mo.effects.push_back(std::move(ef));
            }
            off += (long long)cC * 0x438;
            for (int i = 0; i < cE; ++i) {
                long long r = off + (long long)i * 0x138;
                MapSound sd;
                sd.pos = { BF(r + 0x104), BF(r + 0x108), BF(r + 0x10c) };
                sd.minR = BF(r + 0x110); sd.maxR = BF(r + 0x114);
                sd.wav = readStrBase(r + 0x04, 0xfc);
                mo.sounds.push_back(std::move(sd));
            }
            off += (long long)cE * 0x138;
            // ---- Section D (pool 2) ----
            // Per mesh there is a 0x80 block AFTER the mesh records: two 4x4 matrices. The first is a
            // local/identity placeholder; the SECOND (offset +0x40) is the model's WORLD matrix, whose
            // T is the model origin. As in sections A/B the geometry is model-space, so place the slice
            // with R*local + T(origin) — NOT by the per-part position (which left trunks/foliage floating).
            long long dBase = off;
            std::vector<int> mcDi(cD); long long mcD = 0;
            for (int i = 0; i < cD; ++i) { mcDi[i] = (int)BU(dBase + (long long)i*0x20c + 4); mcD += mcDi[i]; }
            off += (long long)cD * 0x20c;
            long long recD = off;
            long long mtxD = recD + mcD * MR;     // per-mesh 0x80 blocks follow all D mesh records
            { long long j = 0;
              for (int i = 0; i < cD; ++i) { std::string nm = readName(dBase + (long long)i*0x20c);
                for (int m = 0; m < mcDi[i]; ++m) {
                  long long ro = recD + j * MR;
                  Node nd{}; nd.section = 2; nd.x = BF(ro+0x174); nd.y = BF(ro+0x178); nd.z = BF(ro+0x17c);
                  nd.vtxCount = (int)BU(ro + 0x190); nd.triCount = (int)BU(ro + 0x2c); nd.hasMat = false; nd.model = nm;
                  nd.posOff = base + ro + 0x174;   // primary offset (overridden to matrix-T below if matrix-placed)
                  nd.rawPosOff = base + ro + 0x174; // raw node x/y/z, kept even when matrix-placed
                  nd.recOff = base + ro; nd.matArrOff = base + mtxD + j * 0x80; nd.matCount = 2;
                  // scan both 4x4 in the 0x80 block; matValid skips the identity filler, keeps the origin
                  float mm[16]; long long matRel = -1; if (findMat(mtxD + j * 0x80, 2, nd.x, nd.z, mm, &matRel)) { for (int k = 0; k < 16; ++k) nd.m[k] = mm[k]; nd.hasMat = true; nd.posOff = base + matRel + 0x30; }
                  pm.nodes.push_back(nd); j++;
              } } }
            patchMesh.push_back(std::move(pm));
            cur = base + full; P++;
        }
        UnloadFileData(pb);
    }

    // ---- 4b. ani_list.ani -> clips + name lookups (model -> clip, bone name -> bone index) ----
    mo.aniClips = LoadAniList(mapDir);
    std::map<std::string, int> clipByModel;
    std::vector<std::map<std::string, int>> boneByName(mo.aniClips.size());
    for (size_t c = 0; c < mo.aniClips.size(); ++c) {
        clipByModel[mo.aniClips[c].name] = (int)c;
        for (size_t bi = 0; bi < mo.aniClips[c].bones.size(); ++bi)
            boneByName[c][mo.aniClips[c].bones[bi].name] = (int)bi;
    }
    auto stripExt = [](std::string s) { size_t d = s.rfind('.'); return d == std::string::npos ? s : s.substr(0, d); };

    // ---- 5. align Pos.LIN <-> nodes; build unique slices + instances -----------------------
    std::map<std::array<int, 9>, int> sliceIdx;   // (patch,pool,slot,geomIdx,startIdx,startVtx,vc,triCount,rotKey)->mesh
    int nPatch = (int)linPatch.size();
    for (int P = 0; P < nPatch; ++P) {
        const std::vector<Lin>& ent = linPatch[P];
        const PatchMesh* pmp = (P < (int)patchMesh.size()) ? &patchMesh[P] : nullptr;
        if (!pmp) continue;
        const std::vector<Node>& nodes = pmp->nodes;
        // Section D records carry 0 vtx/tri counts -> infer the slice length from the gap to the next
        // entry IN THE SAME BUFFER (same slot). nextOf scans only same-slot entries.
        auto nextOf = [&](int slot, uint32_t val, bool useVtx, int total) -> int {
            int best = total;
            for (int k = 0; k < (int)ent.size(); ++k) {
                if ((int)ent[k].slot != slot) continue;
                int v = (int)(useVtx ? ent[k].startVtx : ent[k].startIdx);
                if (v > (int)val && v < best) best = v;
            }
            return best;
        };
        int totalN = (int)nodes.size();
        for (int j = 0; j < (int)ent.size() && j < totalN; ++j) {
            const Lin& e = ent[j]; const Node& nd = nodes[j];
            int pool = nd.section;                                  // 0=A->Pos0, 1=B->Pos1, 2=D->Pos2
            int slot = (int)e.slot;                                 // which buffer within this pool+patch
            if (pool < 0 || pool > 2 || P >= (int)pools[pool].size()) continue;

            Buffer* cc = getBuffer(P, pool, slot);                  // geometry of buffer[slot] only
            if (!cc || cc->vtxCount == 0) continue;
            int vbase = (int)e.startVtx;                            // LOCAL to this buffer
            int vc = nd.vtxCount;
            if (vc <= 0) vc = nextOf(slot, e.startVtx, true, cc->vtxCount) - vbase;
            if (vbase < 0 || vbase >= cc->vtxCount) continue;
            if (vbase + vc > cc->vtxCount) vc = cc->vtxCount - vbase;
            if (vc < 3) continue;

            // GPU-skinned section-B sub-mesh (FVF 0x256): each vertex names its ani bone via a float at
            // offset +12 (= boneIndex*3 + 8). Verts are bone-local; final = objPlace * aniBoneChain[bone]
            // * local. Route to skinMeshes (CPU-skinned per frame).
            if (nd.section == 1 && pool == 1 && slot < (int)pools[1][P].meshes.size()
                && pools[1][P].meshes[slot].fvf == 0x256u && nd.groupB >= 0
                && nd.groupB < (int)pmp->bGroupClip.size() && pmp->bGroupClip[nd.groupB] >= 0) {
                const PoolMesh& pmsh = pools[1][P].meshes[slot];
                unsigned char* rb = poolData[1];
                auto RF = [&](long long o) -> float { float v = 0; if (rb && o >= 0) std::memcpy(&v, rb + o, 4); return v; };
                SkinMesh sm; sm.clip = pmp->bGroupClip[nd.groupB];
                // ENGINE TRUTH (cdb capture of FUN_01146300's placement param_1+0x168): the skin object
                // placement is IDENTITY rotation + world translation only. ALL orientation comes from the
                // skeleton (.ani bones), NOT the file group/per-mesh matrix (which carries a stray per-
                // instance yaw that wrongly twisted the flags). So use identity rotation + translation.
                // ENGINE TRUTH (asda.dmp: render-obj +0x168 for flag_005_001 = pure Y-yaw 120deg + world T;
                // bGroupMat carries exactly these per-instance yaw/tilt rotations, nd.m does NOT). The engine
                // placement is the GROUP matrix, so use the FULL bGroupMat (rotation + translation) - this is
                // what gives flags their radial fan/lean. Falls back to nd.m only when no group matrix exists.
                bool grpOk = (nd.groupB >= 0 && nd.groupB < (int)pmp->bGroupMat.size());
                const float* src = grpOk ? pmp->bGroupMat[nd.groupB].data() : nd.m;
                for (int k = 0; k < 16; ++k) sm.objPlace[k] = src[k];  // full per-instance placement
                for (int v = 0; v < vc; ++v) {
                    long long q = pmsh.vtxOff + (long long)(vbase + v) * pmsh.stride;
                    SkinVertex sv;
                    sv.pos[0] = RF(q); sv.pos[1] = RF(q + 4); sv.pos[2] = RF(q + 8);
                    sv.nrm[0] = RF(q + 16); sv.nrm[1] = RF(q + 20); sv.nrm[2] = RF(q + 24);  // 0x256 normal at +16
                    sv.uv[0] = RF(q + 32); sv.uv[1] = RF(q + 36);          // 0x256 uv0 at +32
                    // float@+12 = paletteSlot*3+8. Palette slots 0/1 are reserved (identity/placement);
                    // slot s>=2 maps to .ani bone via the file's int[skinCnt] remap: bone = remap[s-2]
                    // (engine FUN_01146300: bone = mesh+0x24c[local_d8-2], local_d8 starts at 2). bone=-1
                    // means a reserved slot → no skeletal transform (placement only).
                    // Vertex float@+12 = boneRegister = paletteBoneIndex*3 + 8 (shader: mov a0.x,v1.x;
                    // m4x3 r0,v0,c[a0.x]; the per-vertex bone palette starts at c8, step 3). So
                    // paletteBoneIndex = (reg-8)/3, and the .ani bone = remap[paletteBoneIndex] DIRECTLY.
                    // (The c0/c4 reserved registers are the world/placement matrices used by the final
                    // m4x4 oPos step - they are NOT part of the c8+ bone palette, so there is NO -2 offset.
                    // The old -2 orphaned the first two palette bones: flag cloth lost its pole edge to the
                    // placement origin (corner-pinned + gap) and bird bodies dropped to -1 and spiked.)
                    { int s = (int)((RF(q + 12) - 8.0f) / 3.0f + 0.5f);
                      sv.bone = (s >= 0 && s < (int)nd.boneRemap.size()) ? nd.boneRemap[s] : -1; }
                    sm.verts.push_back(sv);
                }
                int idxEnd = (nd.triCount > 0) ? ((int)e.startIdx + nd.triCount * 3)
                                               : nextOf(slot, e.startIdx, false, (int)cc->idx.size());
                for (int t = (int)e.startIdx; t + 2 < idxEnd && t + 2 < (int)cc->idx.size(); t += 3) {
                    unsigned short a = cc->idx[t], b1 = cc->idx[t+1], b2 = cc->idx[t+2];
                    if (a < vc && b1 < vc && b2 < vc) { sm.idx.push_back(a); sm.idx.push_back(b1); sm.idx.push_back(b2); }
                }
                if (!sm.idx.empty()) { sm.texture = texOf(P, pool, (int)e.geomIdx); mo.skinMeshes.push_back(std::move(sm)); }
                continue;
            }

            // GPU-skinned section-B sub-mesh, 2-bone blend (FVF 0x60252): the BIRD BODY (and similar).
            // Layout (stride 72): pos@0, normal@12, diffuse@24, uv0@28, weights@44/@48, boneRegs@52/@56
            // (reg = paletteBoneIndex*3+8). Linear blend skinning across 2 bones. Without this the body
            // falls through to the static batch (node 'Object09' is not a skeleton bone) and hangs in the
            // air while the 0x256 wings fly.
            if (nd.section == 1 && pool == 1 && slot < (int)pools[1][P].meshes.size()
                && pools[1][P].meshes[slot].fvf == 0x60252u && nd.groupB >= 0
                && nd.groupB < (int)pmp->bGroupClip.size() && pmp->bGroupClip[nd.groupB] >= 0) {
                const PoolMesh& pmsh = pools[1][P].meshes[slot];
                unsigned char* rb = poolData[1];
                auto RF = [&](long long o) -> float { float v = 0; if (rb && o >= 0) std::memcpy(&v, rb + o, 4); return v; };
                SkinMesh sm; sm.clip = pmp->bGroupClip[nd.groupB];
                bool grpOk = (nd.groupB >= 0 && nd.groupB < (int)pmp->bGroupMat.size());
                const float* src = grpOk ? pmp->bGroupMat[nd.groupB].data() : nd.m;
                for (int k = 0; k < 16; ++k) sm.objPlace[k] = src[k];
                auto slotBone = [&](int s) -> int { return (s >= 0 && s < (int)nd.boneRemap.size()) ? nd.boneRemap[s] : -1; };
                for (int v = 0; v < vc; ++v) {
                    long long q = pmsh.vtxOff + (long long)(vbase + v) * pmsh.stride;
                    SkinVertex sv;
                    sv.pos[0] = RF(q); sv.pos[1] = RF(q + 4); sv.pos[2] = RF(q + 8);
                    sv.nrm[0] = RF(q + 12); sv.nrm[1] = RF(q + 16); sv.nrm[2] = RF(q + 20);  // 0x60252 normal at +12
                    sv.uv[0] = RF(q + 28); sv.uv[1] = RF(q + 32);          // 0x60252 uv0 at +28
                    int s0 = (int)((RF(q + 52) - 8.0f) / 3.0f + 0.5f);
                    int s1 = (int)((RF(q + 56) - 8.0f) / 3.0f + 0.5f);
                    sv.bone = slotBone(s0); sv.bone2 = slotBone(s1); sv.w0 = RF(q + 44);
                    if (sv.w0 < 0.0f || sv.w0 > 1.0f) sv.w0 = 1.0f;
                    sm.verts.push_back(sv);
                }
                int idxEnd = (nd.triCount > 0) ? ((int)e.startIdx + nd.triCount * 3)
                                               : nextOf(slot, e.startIdx, false, (int)cc->idx.size());
                for (int t = (int)e.startIdx; t + 2 < idxEnd && t + 2 < (int)cc->idx.size(); t += 3) {
                    unsigned short a = cc->idx[t], b1 = cc->idx[t+1], b2 = cc->idx[t+2];
                    if (a < vc && b1 < vc && b2 < vc) { sm.idx.push_back(a); sm.idx.push_back(b1); sm.idx.push_back(b2); }
                }
                if (!sm.idx.empty()) { sm.texture = texOf(P, pool, (int)e.geomIdx); mo.skinMeshes.push_back(std::move(sm)); }
                continue;
            }

            // Animated section-B sub-mesh? Match its node name to an ani_list bone and route the RAW
            // local geometry + rest matrix to animMeshes (drawn separately, transformed per frame).
            if (nd.section == 1 && nd.hasMat && !nd.node.empty()) {
                auto ci = clipByModel.find(stripExt(nd.model));
                if (ci != clipByModel.end()) {
                    auto bi = boneByName[ci->second].find(nd.node);
                    if (bi != boneByName[ci->second].end()) {
                        AnimMesh am; am.clip = ci->second; am.bone = bi->second; am.model = stripExt(nd.model);
                        am.pos.assign(cc->pos.begin() + (size_t)vbase * 3, cc->pos.begin() + (size_t)(vbase + vc) * 3);
                        if (!cc->nrm.empty())
                            am.nrm.assign(cc->nrm.begin() + (size_t)vbase * 3, cc->nrm.begin() + (size_t)(vbase + vc) * 3);
                        am.uv.assign(cc->uv.begin() + (size_t)vbase * 2, cc->uv.begin() + (size_t)(vbase + vc) * 2);
                        for (int k = 0; k < 16; ++k) am.m[k] = nd.m[k];
                        // engine placement = the GROUP matrix bGroupMat (asda.dmp: render-obj +0x168 == the
                        // per-instance Y-yaw carried by bGroupMat; all bGroupMat are clean det+1 Y-rotations).
                        // The earlier "airship flip" was the bone-composition bug (now removed), NOT this
                        // rotation. Use the FULL bGroupMat so airship/flags/birds get their real heading.
                        bool grpOk = (nd.groupB >= 0 && nd.groupB < (int)pmp->bGroupMat.size());
                        const float* src = grpOk ? pmp->bGroupMat[nd.groupB].data() : nd.m;
                        for (int k = 0; k < 16; ++k) am.objPlace[k] = src[k];  // full per-instance placement
                        int idxEnd = (nd.triCount > 0) ? ((int)e.startIdx + nd.triCount * 3)
                                                       : nextOf(slot, e.startIdx, false, (int)cc->idx.size());
                        for (int t = (int)e.startIdx; t + 2 < idxEnd && t + 2 < (int)cc->idx.size(); t += 3) {
                            unsigned short a = cc->idx[t], b1 = cc->idx[t+1], b2 = cc->idx[t+2];
                            if (a < vc && b1 < vc && b2 < vc) { am.idx.push_back(a); am.idx.push_back(b1); am.idx.push_back(b2); }
                        }
                        if (!am.idx.empty()) { am.texture = texOf(P, pool, (int)e.geomIdx); mo.animMeshes.push_back(std::move(am)); }
                        continue;   // not added to the static batch
                    }
                }
            }

            // rotation key so identical (slice,rotation) instances are shared
            int rotKey = 0;
            if (nd.hasMat) for (int k = 0; k < 11; ++k) rotKey = rotKey * 131 + (int)(nd.m[k] * 64.0f);
            // key includes slot + vtx/tri counts: the same (startVtx,startIdx) is reused across buffers
            // and for slices of different size, so all must distinguish the cached mesh.
            std::array<int, 9> key{ P, pool, slot, (int)e.geomIdx, (int)e.startIdx, (int)e.startVtx, vc, nd.triCount, rotKey };
            int mi = -1;
            auto it = sliceIdx.find(key);
            if (it != sliceIdx.end()) mi = it->second;
            if (mi < 0) {
                PbbMesh mesh; mesh.vtxCount = vc;
                mesh.pos.assign(cc->pos.begin() + (size_t)vbase * 3, cc->pos.begin() + (size_t)(vbase + vc) * 3);
                mesh.uv.assign(cc->uv.begin() + (size_t)vbase * 2, cc->uv.begin() + (size_t)(vbase + vc) * 2);
                mesh.nrm.assign((size_t)vc * 3, 0.0f);
                if (!cc->nrm.empty())
                    std::copy(cc->nrm.begin() + (size_t)vbase * 3, cc->nrm.begin() + (size_t)(vbase + vc) * 3, mesh.nrm.begin());
                if (nd.hasMat) {
                    const float* m = nd.m;
                    for (int v = 0; v < vc; ++v) {
                        float x = mesh.pos[v*3+0], y = mesh.pos[v*3+1], z = mesh.pos[v*3+2];
                        mesh.pos[v*3+0] = x*m[0] + y*m[4] + z*m[8];
                        mesh.pos[v*3+1] = x*m[1] + y*m[5] + z*m[9];
                        mesh.pos[v*3+2] = x*m[2] + y*m[6] + z*m[10];
                        // normal: rotate by R only (m is orthonormal -> no inverse-transpose needed)
                        float nx = mesh.nrm[v*3+0], ny = mesh.nrm[v*3+1], nz = mesh.nrm[v*3+2];
                        mesh.nrm[v*3+0] = nx*m[0] + ny*m[4] + nz*m[8];
                        mesh.nrm[v*3+1] = nx*m[1] + ny*m[5] + nz*m[9];
                        mesh.nrm[v*3+2] = nx*m[2] + ny*m[6] + nz*m[10];
                    }
                }
                // exact index count from the mesh record (triCount @+0x2c); the engine draws
                // primCount=triCount from startIdx (indices are slice-relative, BaseVertexIndex=startVtx).
                // Section D records carry 0 here, so fall back to the LIN-gap heuristic for D only.
                int idxEnd = (nd.triCount > 0) ? ((int)e.startIdx + nd.triCount * 3)
                                               : nextOf(slot, e.startIdx, false, (int)cc->idx.size());
                for (int t = (int)e.startIdx; t + 2 < idxEnd && t + 2 < (int)cc->idx.size(); t += 3) {
                    unsigned short a = cc->idx[t], b1 = cc->idx[t+1], b2 = cc->idx[t+2];
                    if (a < vc && b1 < vc && b2 < vc) { mesh.idx.push_back(a); mesh.idx.push_back(b1); mesh.idx.push_back(b2); }
                }
                if (mesh.idx.empty()) continue;
                mesh.texture = texOf(P, pool, (int)e.geomIdx);
                mi = (int)mo.meshes.size();
                mo.meshes.push_back(std::move(mesh));
                sliceIdx.emplace(key, mi);
            }
            ObjInstance inst;
            inst.meshIndex = mi;
            inst.model = nd.model; inst.texture = texOf(P, pool, (int)e.geomIdx);
            inst.patch = P; inst.section = nd.section; inst.geomIdx = (int)e.geomIdx;
            inst.startVtx = (int)e.startVtx; inst.startIdx = (int)e.startIdx; inst.vtxCount = vc; inst.hasMat = nd.hasMat;
            inst.posOff = nd.posOff;        // for the editor: where to write the moved position back into Pos.POS
            inst.rawPosOff = nd.rawPosOff;  // raw node x/y/z (the game's actual per-mesh position; shifted on save)
            inst.rawOrig = Vec3{ nd.x, nd.y, nd.z };   // original raw x/y/z (save-delta baseline)
            inst.matArrOff = nd.matArrOff; inst.matCount = nd.matCount; inst.recOff = nd.recOff;  // diagnostics
            // world = R*local + matrix_T  (R baked above). This is the state that rendered the portal
            // correctly. Same for A/B/D.
            inst.pos = nd.hasMat ? Vec3{ nd.m[12], nd.m[13], nd.m[14] } : Vec3{ nd.x, nd.y, nd.z };
            inst.posOrig = inst.pos;   // baseline for the editor save delta — MUST be after inst.pos is set
            if (!(inst.pos.x > -2000 && inst.pos.x < 60000 && inst.pos.z > -2000 && inst.pos.z < 60000)) continue;
            mo.instances.push_back(inst);
        }
    }

    for (int f = 0; f < 3; ++f) if (poolData[f]) UnloadFileData(poolData[f]);
    mo.has = !mo.meshes.empty() && !mo.instances.empty();
    return mo;
}



Scene LoadScene(const std::string& path) {
    Scene s;
    int n = 0;
    unsigned char* d = LoadFileData(path.c_str(), &n);
    if (!d || n < 0x58) { if (d) UnloadFileData(d); return s; }

    uint32_t magic; std::memcpy(&magic, d, 4);
    if (magic != 0x20060819u && magic != 0x20041008u) { UnloadFileData(d); return s; }

    auto u32 = [&](int o) -> uint32_t { uint32_t v; std::memcpy(&v, d + o, 4); return v; };
    auto f32 = [&](int o) -> float { float v; std::memcpy(&v, d + o, 4); return v; };

    uint32_t c = u32(0x08);
    s.fogColor = { (unsigned char)((c >> 16) & 0xff), (unsigned char)((c >> 8) & 0xff),
                   (unsigned char)(c & 0xff), 255 };
    s.fogStart = f32(0x0c);
    s.fogEnd   = f32(0x10);
    s.ambient  = { f32(0x20), f32(0x24), f32(0x28) };
    s.diffuse  = { f32(0x44), f32(0x48), f32(0x4c) };

    // ★ Солнце — ГОТОВЫЙ вектор @0xB4 (см. AsdaMapFormats.h::Scene::sunDir). Прежняя формула по
    //   углам давала ошибку 30–90° на всех 22 картах (аудит 2026-09-03, Д-1). Углы @0x50/@0x54
    //   оставлены как запасной путь ТОЛЬКО для усечённого файла, и тогда считаются ВЕРНОЙ формулой.
    float th = f32(0x50), ph = f32(0x54);
    Vec3 fromAngles = { -cosf(th) * sinf(ph), sinf(th), -cosf(th) * cosf(ph) };
    if (n >= 0xC0)
    {
        s.sunDir = { f32(0xB4), f32(0xB8), f32(0xBC) };
        s.hasSunDir = true;
        // Самопроверка против формулы: расхождение = либо другой формат файла, либо порча.
        const float dx = s.sunDir.x - fromAngles.x, dy = s.sunDir.y - fromAngles.y, dz = s.sunDir.z - fromAngles.z;
        if (dx * dx + dy * dy + dz * dz > 1e-4f)
        {
            // Модуль без UE-заголовков (raylib-free C++), поэтому stderr, а не UE_LOG.
            std::fprintf(stderr, "LoadScene: sunDir @0xB4 (%.3f %.3f %.3f) != angles @0x50/@0x54 (%.3f %.3f %.3f) - using file\n", s.sunDir.x, s.sunDir.y, s.sunDir.z, fromAngles.x, fromAngles.y, fromAngles.z);
        }
    }
    else
    {
        s.sunDir = fromAngles;
        s.hasSunDir = false;
    }
    // ★ Контракт потребителя (`M_AsdaObject`/`M_AsdaTerrain`, `AsdaMapActor.cpp` параметр `LightDir`):
    //   направление РАСПРОСТРАНЕНИЯ света (граф делает `−LightDir`, дефолт (0,0,−1)) — как во
    //   вьювере (`lightDir = −sunDir`). Ревизия `review_lighting_sun_and_ambient.md` п.7.
    s.lightDir = { -s.sunDir.x, -s.sunDir.y, -s.sunDir.z };

    // sane fallbacks if a field is degenerate
    if (!(s.fogEnd > s.fogStart) || s.fogEnd <= 0.0f) { s.fogStart = 5000.0f; s.fogEnd = 20000.0f; }

    s.valid = true;
    UnloadFileData(d);
    return s;
}

std::vector<MapEntry> ScanMaps(const std::string& mapRoot) {
    std::vector<MapEntry> out;
    std::error_code ec;
    if (!fs::is_directory(mapRoot, ec)) return out;

    for (auto& it : fs::directory_iterator(mapRoot, ec)) {
        if (!it.is_directory()) continue;
        fs::path dir = it.path();

        // A folder may hold more than one map (e.g. conquester + nightvalley,
        // quizstage + OX_Quiz). Emit one entry per .BIN so none are lost.
        std::vector<fs::path> bins;
        std::error_code ec2;
        for (auto& f : fs::directory_iterator(dir, ec2)) {
            if (!f.is_regular_file()) continue;
            std::string ext = f.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".bin") bins.push_back(f.path());
        }
        if (bins.empty()) continue;

        std::string folder = dir.filename().string();
        std::string folderLow = folder;
        std::transform(folderLow.begin(), folderLow.end(), folderLow.begin(), ::tolower);

        for (const fs::path& bp : bins) {
            std::string stem = bp.stem().string();
            std::string stemLow = stem;
            std::transform(stemLow.begin(), stemLow.end(), stemLow.begin(), ::tolower);

            MapEntry e;
            // single map, or the .BIN matching the folder name -> show folder name;
            // extra maps in the same folder -> "folder : stem".
            e.name = (bins.size() == 1 || stemLow == folderLow)
                         ? folder : (folder + " : " + stem);
            e.dir = dir.string();
            e.binPath = bp.string();
            e.texPath = FindMainTex(dir, stem);
            out.push_back(std::move(e));
        }
    }

    std::sort(out.begin(), out.end(),
              [](const MapEntry& a, const MapEntry& b) { return a.name < b.name; });
    return out;
}

} // namespace asda
