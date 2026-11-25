# Copyright (c) 2026 @sitxovski (Discord)
# asda_jbm_to_gltf.py — конвертация модели AsdaStory .jbm → glTF (.glb) [ФАЗА 1: статик-геометрия].
# Порт LoadJBM из MapViewer/src/asda_formats.cpp + FVF-таблица (strideFor/uvOff/normalOff).
# Координаты: игра LH Y-up → glTF RH Y-up (негейт Z + реверс winding). Скиннинг/анимация — позже.
# Запуск: python asda_jbm_to_gltf.py <in.jbm> <out.glb>
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import pygltflib as g

# ★ Разбор .jbm (шапка + Pass1-геометрия) — ЕДИНЫЙ модуль asda_jbm.py (общий с asda_skinned_to_gltf /
#   asda_test_export_one). Локальная копия удалена 2026-08-01; вместе с ней ушёл её дефект:
#   она ставила ВСЕМ мешам текстуру первого материала (tex0), игнорируя пер-меш matIdx@0x1f4
#   (закон npc-texture-source). Бэкап: RE/_refactor_bak/2026-08-01/asda_jbm_to_gltf.py.orig
import asda_jbm as JBM

# FVF-таблица оставлена как реэкспорт из модуля (внешние потребители ссылались на эти имена).
STRIDE = JBM.FVF_STRIDE
UVOFF  = JBM.FVF_UVOFF
NRMOFF = JBM.FVF_NRMOFF


def parse_jbm(path):
    return JBM.parse_file(path, guard=True)


def parse_jbm_bytes(b):
    return JBM.parse_geometry(b, guard=True)


def to_glb(model, out, blend_mat_suffix=None, mat_names=None):
    # LH Y-up (игра) → RH Y-up (glTF): негейт Z, реверс winding.
    # ★ mat_names (2026-08-18, зона NPC/мобов, `Reports/blend_rollout_npc_mobs.md`) — готовый СПИСОК
    #   имён материалов по подсеткам (длина = len(model["meshes"])). Побеждает и имя текстуры, и
    #   `blend_mat_suffix`. Нужен там, где в ОДНОЙ модели подсетки РАЗНЫХ техник (tSkin/tRigid и
    #   tNoSkin) носят ОДНУ текстуру: UE сливает материалы по имени в один слот, и без разных имён
    #   мастера не развести. Имена считает `asda_mesh_render_class.material_names()` из данных .jbm.
    # ★ blend_mat_suffix (2026-08-18, `Reports/jbm_blend_flag_study.md` §5): подсетки, прошедшие
    #   `asda_jbm.is_alpha_blend` (рендер-бит mr+0x004 & 0x1|0x2 И техника tNoSkin 0x200/0x201/
    #   0x800/0x801), получают ОТДЕЛЬНОЕ имя материала. Импортёр по этой метке вешает на слот
    #   прозрачный мастер `M_AsdaObjectBlendLit` (asda_material_policy.master_for), поэтому правка
    #   воспроизводится при переимпорте, а не делается разово в редакторе.
    #   None → поведение НЕ меняется.
    bins = bytearray(); accessors = []; bufviews = []; prims = []; mats = []
    def add_view(data, target):
        off = len(bins)
        bins.extend(data)
        while len(bins) % 4: bins.append(0)
        bufviews.append(g.BufferView(buffer=0, byteOffset=off, byteLength=len(data), target=target))
        return len(bufviews) - 1
    san = lambda a: np.nan_to_num(a, nan=0.0, posinf=0.0, neginf=0.0).astype(np.float32)
    for mi, m in enumerate(model["meshes"]):
        # Ремап только используемых вершин (выбрасывает неинициализированные 0xCD-вершины вне индексов).
        idx = np.array(m["idx"], np.uint32)
        used, inv = np.unique(idx, return_inverse=True)
        P = san(m["pos"][used].copy()); P[:, 2] *= -1.0   # LH→RH: негейт Z
        N = san(m["nrm"][used].copy()); N[:, 2] *= -1.0
        UVv = san(m["uv"][used].copy())
        I = inv.astype(np.uint32).reshape(-1, 3)[:, ::-1].reshape(-1)  # ремап + реверс winding
        vP = add_view(P.astype("<f4").tobytes(), g.ARRAY_BUFFER)
        accessors.append(g.Accessor(bufferView=vP, componentType=g.FLOAT, count=len(P), type=g.VEC3,
                                    min=P.min(0).tolist(), max=P.max(0).tolist())); aP = len(accessors)-1
        vN = add_view(N.astype("<f4").tobytes(), g.ARRAY_BUFFER)
        accessors.append(g.Accessor(bufferView=vN, componentType=g.FLOAT, count=len(N), type=g.VEC3)); aN = len(accessors)-1
        vU = add_view(UVv.astype("<f4").tobytes(), g.ARRAY_BUFFER)
        accessors.append(g.Accessor(bufferView=vU, componentType=g.FLOAT, count=len(UVv), type=g.VEC2)); aU = len(accessors)-1
        vI = add_view(I.astype("<u4").tobytes(), g.ELEMENT_ARRAY_BUFFER)
        accessors.append(g.Accessor(bufferView=vI, componentType=g.UNSIGNED_INT, count=len(I), type=g.SCALAR)); aI = len(accessors)-1
        mat_name = os.path.splitext(m["tex"])[0] or ("mat%d" % mi)
        if blend_mat_suffix and JBM.is_alpha_blend(m):
            mat_name += blend_mat_suffix          # отдельный слот для полупрозрачной подсетки
        if mat_names is not None:                 # ★ готовый список имён побеждает (см. шапку)
            mat_name = mat_names[mi]
        mats.append(g.Material(name=mat_name,
                               pbrMetallicRoughness=g.PbrMetallicRoughness(metallicFactor=0.0, roughnessFactor=1.0)))
        prims.append(g.Primitive(attributes=g.Attributes(POSITION=aP, NORMAL=aN, TEXCOORD_0=aU),
                                 indices=aI, material=len(mats)-1))
    gltf = g.GLTF2(
        scene=0, scenes=[g.Scene(nodes=[0])],
        nodes=[g.Node(mesh=0, name=model["name"])],
        meshes=[g.Mesh(primitives=prims, name=model["name"])],
        materials=mats, accessors=accessors, bufferViews=bufviews,
        buffers=[g.Buffer(byteLength=len(bins))])
    gltf.set_binary_blob(bytes(bins))
    gltf.save_binary(out)

def main():
    inp, out = sys.argv[1], sys.argv[2]
    mdl = parse_jbm(inp)
    if not mdl or not mdl["meshes"]:
        print("FAIL parse:", inp); return
    to_glb(mdl, out)
    tv = sum(len(m["pos"]) for m in mdl["meshes"]); tt = sum(len(m["idx"])//3 for m in mdl["meshes"])
    print("OK %s -> %s | name=%s meshes=%d verts=%d tris=%d tex=%s" %
          (os.path.basename(inp), os.path.basename(out), mdl["name"], len(mdl["meshes"]), tv, tt, mdl["tex"]))

if __name__ == "__main__":
    main()
