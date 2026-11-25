# Copyright (c) 2026 @sitxovski (Discord)
# asda_mob_texmap.py — ЭТАЛОННЫЙ резолв ТЕКСТУР МОБОВ (и имени модели) по данным оригинала.
#
# ★ ПРАВИЛО (реверс, адреса — AsdaStory.exe):
#   1) МОДЕЛЬ моба = `Data\Mob\Mob list.BIN`, запись 272Б, поле **+0x04** (32Б ASCII) → `<model>.jbm`
#      в `Data\Mob\Model\MobModel.bnz`.
#      Эталон CMap_LoadMOB@0xF71A50 (world_map.c):
#          qmemcpy(spawnParams, tbl + 272*mobType + 4, 32);
#          strcpyWrapper(&FileName, spawnParams...); String_Append(&FileName, ".jbm");
#          dataMgr_EnsureMeshFile(..., MobModel.bnz, FileName, ...)
#      Поле **+0x24** — база КЛИПОВ `.jbb` (Effect_BuildJbbSuffixByType@0xF717C0: имя+"_N.jbb"),
#      поле **+0x44** — база звука (Sound_BuildEntryWavName@0xF71450). Это РАЗНЫЕ поля: у 117 из 854
#      записей +0x04 != +0x24 (ewood02/pawn02/pawn03/bishp02…), и модель берётся ТОЛЬКО из +0x04.
#
#   2) ТЕКСТУРА КАЖДОГО САБМЕША берётся НЕ из matTable модели, а из ПЕР-КАРТОВЫХ таблиц:
#      Эталон modelBuildMeshFromCache@0x11A45E0 (model_anim.c:31267), вызов из состояния 0x15
#      загрузчика моба mobEffect_LoadArchiveStep@0xF81B90 (npc_mob_pet.c:9232):
#          for (i = 0; i < meshCount; ++i) {
#              qmemcpy(subset[i] + 8, a8[2] + 32 * pairs[i].A, 0x20);   // ← дескриптор ТЕКСТУРЫ
#              animCopyBoneTransformRecord(a8, ..., pairs[i].A, pairs[i].B);
#          }
#      где `pairs` = a10 = *(dword_34A0380 + 4*idx) — таблица ПАР (u32 A, u32 B) на сабмеш,
#      idx — позиция mobId в списке `dword_34A0484` (файл `<map>.MON`).
#
#   3) ИМЯ ФАЙЛА текстуры для группы A лежит в `<map>.MBB` (CMap_LoadNLK@0x1165760, world_map.c:15333):
#      после magic 0x20041019 и 0x1C-байтной шапки идёт u32 blkSize и блок:
#          [32 × count]  — рантайм-слот дескриптора текстуры (в файле нули) = a8[2] + 32*A
#          [256 × count] — ПУТЬ текстуры группы: "\Data\Mob\texture\<имя>.TEX"   ★ имя отсюда
#          [4 × count]   — число сабмешей в группе
#          [4 × count]   — рантайм-указатели на массивы 140-байтных записей
#          [140 × Σ]     — записи сабмешей (VB/IB батча)
#          [4 × count]   — хвост
#      count = поле шапки @+0x08 (this->tileCountX).
#
#   4) matTable в `.jbm` — только авторские имена материалов из 3ds Max («ragepig.png», кракозябры);
#      для МОБОВ клиент их не использует. Значим только пер-меш `matIdx` (@mr+0x1F4) — он определяет
#      ГРУППИРОВКУ сабмешей, а группам эталон назначает конкретные .TEX (см. п.2–3).
#
# ★ ПОДТВЕРЖДЕНИЕ НА ДАННЫХ (20 карт с .MON/.MLK/.MBB, 705 пар (карта, mobId)):
#   • число пар в .MLK == meshCount модели @+0x04 в 703/705 (у @+0x24 — 650/705);
#   • matIdx → текстура однозначен в 680/680 записей (ни одного конфликта);
#   • набор текстур для (модель, mobId) ОДИНАКОВ на всех картах — 0 расхождений
#     ⇒ таблица текстур зависит от mobId, а не от карты.
#   • 2 исключения: mobId 237 (dragonlair) и 238 (flammio) — модель `mon_s_0_turkey_a_00` (6 сабмешей)
#     посажена упаковщиком оригинала на батч волка (5 сабмешей, `mon_s_0_rwolf02-a_03.TEX`).
#     Это данные ОРИГИНАЛА, а не ошибка разбора (счётчики сабмешей в .MBB сходятся точно).
#
# ★ Модуль БЕЗ побочных эффектов при импорте (правило tools-no-import-side-effects): только функции.

import os
import glob
import struct
import zipfile

import asda_paths as _P
import asda_jbm as _J

MOB_LIST_BIN = os.path.join(_P.DATA, "Mob", "Mob list.BIN")
MOB_TEX_BTZ  = os.path.join(_P.DATA, "Mob", "Texture", "Texture.btz")

REC_SIZE   = 272        # запись Mob list.BIN
OFF_ID     = 0x00       # u32 id моба (== индекс+1)
OFF_MODEL  = 0x04       # ★ имя модели .jbm (32Б)
OFF_MOTION = 0x24       # база клипов .jbb (32Б)
OFF_SOUND  = 0x44       # база звука .wav (32Б)

MBB_MAGIC = 0x20041019


import asda_bintable as _BT   # общий разбор C-строк (волна 2 аудита дублей)


def _cstr(b, off, size):
    """ASCII-строка фиксированного поля до первого NUL — общий `asda_bintable.cstr`."""
    return _BT.cstr(b, off, size, "latin1")


# ─────────────────────────────────────────────────────────────── Mob list.BIN
def mob_list(path=None):
    """{mobId: dict(idx, model, motion, sound)} — model из поля +0x04 (эталон CMap_LoadMOB)."""
    b = open(path or MOB_LIST_BIN, "rb").read()
    cnt = struct.unpack_from("<I", b, 4)[0]
    out = {}
    for i in range(cnt):
        r = 20 + REC_SIZE * i
        if r + REC_SIZE > len(b):
            break
        out[struct.unpack_from("<I", b, r + OFF_ID)[0]] = dict(
            idx=i,
            model=_cstr(b, r + OFF_MODEL, 32),
            motion=_cstr(b, r + OFF_MOTION, 32),
            sound=_cstr(b, r + OFF_SOUND, 32))
    return out


# ─────────────────────────────────────────────────────────── пер-карточные файлы
def read_mbb_groups(path):
    """`<map>.MBB` → (names, meshcounts): имя .TEX и число сабмешей каждой группы A."""
    b = open(path, "rb").read()
    if struct.unpack_from("<I", b, 0)[0] != MBB_MAGIC:
        return [], []
    cnt = struct.unpack_from("<I", b, 8)[0]          # шапка @+0x08 = tileCountX = число групп
    if cnt <= 0 or 36 + 292 * cnt > len(b):
        return [], []
    o256 = 36 + 32 * cnt
    ocnt = 36 + 288 * cnt
    names = [os.path.basename(_cstr(b, o256 + 256 * a, 256).replace("\\", "/")) for a in range(cnt)]
    mc = list(struct.unpack_from("<%dI" % cnt, b, ocnt))
    return names, mc


def read_mlk_tables(path):
    """`<map>.MLK` → список таблиц пар [(A, B), …] по одному элементу на mobId карты (порядок .MON)."""
    b = open(path, "rb").read()
    magic, blob, cnt = struct.unpack_from("<III", b, 0)
    if magic != MBB_MAGIC:
        return []
    sizes = struct.unpack_from("<%dI" % cnt, b, 12)
    off = 12 + 8 * cnt                               # 4*cnt размеров + 4*cnt рантайм-указателей
    tabs = []
    for k in range(cnt):
        n = sizes[k]
        pr = struct.unpack_from("<%dI" % (2 * n), b, off)
        off += 8 * n
        tabs.append(list(zip(pr[0::2], pr[1::2])))
    return tabs


def read_mon_ids(path, cnt):
    """`<map>.MON` → список id мобов карты (порядок = порядок таблиц .MLK)."""
    b = open(path, "rb").read()
    return list(struct.unpack_from("<%dI" % cnt, b, 0))


def map_dirs(map_root=None):
    """Каталоги карт, у которых есть полный комплект .MBB/.MLK/.MON → [(имя_карты, stem_пути)]."""
    root = map_root or os.path.join(_P.CLIENT_MINE, "map")
    out = []
    for d in sorted(os.listdir(root)):
        dd = os.path.join(root, d)
        if not os.path.isdir(dd):
            continue
        g = glob.glob(os.path.join(dd, "*.MBB"))
        if not g:
            continue
        stem = os.path.splitext(g[0])[0]
        if os.path.exists(stem + ".MLK") and os.path.exists(stem + ".MON"):
            out.append((d, stem))
    return out


# ─────────────────────────────────────────────────────── .jbm: matIdx по сабмешам
def jbm_index(bnz=None):
    """{имя_модели_lower: dict(name, meshCount, matTable, matIdx=[…по сабмешам])} для всего MobModel.bnz."""
    zm = zipfile.ZipFile(bnz or _P.MOB_MODEL_BNZ)
    out = {}
    for n in zm.namelist():
        if not n.lower().endswith(".jbm"):
            continue
        h = _J.parse_header(zm.read(n))
        if not h:
            continue
        out[n[:-4].lower()] = dict(name=n[:-4], meshCount=h["meshCount"],
                                   matTable=h["matTable"],
                                   matIdx=[r["matIdx"] for r in h["recs"]])
    return out


# ─────────────────────────────────────────────────────────────── главный резолв
def resolve(map_root=None, bnz=None, mob_list_bin=None):
    """Эталонная сводка текстур мобов.

    Возвращает dict:
      by_mob     : {mobId: dict(model=…, submesh=[tex…], by_matidx={matIdx: tex})}
      by_model   : {model: {mobId: {matIdx: tex}}}      (+ by_model_lc: тот же по lower-ключу)
      anomalies  : [(карта, mobId, model, meshCount, len(pairs))] — расхождение числа сабмешей
      anomaly_ids: {mobId} — типы из anomalies; их эталонные текстуры НЕ применимы к нашей модели
                   (упаковщик оригинала посадил тип на ЧУЖОЙ батч — см. turkey 237/238)
      conflicts  : [(mobId, …)] — если один matIdx получил разные текстуры (в оригинале пусто)
    """
    ML = mob_list(mob_list_bin)
    JB = jbm_index(bnz)
    by_mob, by_model = {}, {}
    anomalies, conflicts = [], []
    for mapname, stem in map_dirs(map_root):
        names, _mc = read_mbb_groups(stem + ".MBB")
        tabs = read_mlk_tables(stem + ".MLK")
        if not names or not tabs:
            continue
        ids = read_mon_ids(stem + ".MON", len(tabs))
        for k, mid in enumerate(ids):
            rec = ML.get(mid)
            if not rec:
                continue
            model = rec["model"]
            jb = JB.get(model.lower())
            pairs = tabs[k]
            tex = [names[a] if 0 <= a < len(names) else "" for (a, _b) in pairs]
            if jb and jb["meshCount"] != len(pairs):
                anomalies.append((mapname, mid, model, jb["meshCount"], len(pairs)))
            d = {}
            if jb:
                for mi, t in zip(jb["matIdx"], tex):
                    if mi in d and d[mi] != t:
                        conflicts.append((mapname, mid, model, mi, d[mi], t))
                    d[mi] = t
            prev = by_mob.get(mid)
            if prev and prev["submesh"] != tex:
                conflicts.append((mapname, mid, model, "по картам", prev["submesh"], tex))
            by_mob[mid] = dict(model=model, submesh=tex, by_matidx=d)
            by_model.setdefault(model, {})[mid] = d
    # Аномальные типы исключаем из выдачи: их пары указывают на ЧУЖОЙ батч (число сабмешей не сошлось),
    # применять такие текстуры к нашей модели нельзя — это дало бы волчью шкуру на индюке.
    anomaly_ids = set(a[1] for a in anomalies)
    for mid in anomaly_ids:
        rec = by_mob.pop(mid, None)
        if rec and rec["model"] in by_model:
            by_model[rec["model"]].pop(mid, None)
            if not by_model[rec["model"]]:
                del by_model[rec["model"]]
    by_model_lc = {k.lower(): v for k, v in by_model.items()}
    return dict(by_mob=by_mob, by_model=by_model, by_model_lc=by_model_lc,
                anomalies=anomalies, anomaly_ids=anomaly_ids, conflicts=conflicts)


def base_mob_of_model(by_model):
    """Базовый (канонический) mobId модели = минимальный id — его набор кладём в MIC самого меша."""
    return {model: min(mobs) for model, mobs in by_model.items()}


def tex_stem(name):
    """'mon_s_0_ragepig01-b_00.TEX' → 'mon_s_0_ragepig01-b_00' (имя ассета UE)."""
    return os.path.splitext(name)[0]
