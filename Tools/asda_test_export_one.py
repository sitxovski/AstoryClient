# Copyright (c) 2026 @sitxovski (Discord)
# asda_test_export_one.py — ТЕСТ: экспорт ОДНОЙ NPC-модели с ИСПРАВЛЕННЫМ двух-позиционным bind.
# Цель: получить .glb, который можно просто перетащить в окно UE и посмотреть, как грузится скелетный меш.
#
# ЧТО ИСПРАВЛЕНО vs asda_skinned_to_gltf.py:
#   Игра хранит per-vertex ДВЕ забинженные позиции POSITION0/POSITION1 (свёрнуты в локал кости в ИСТИННОЙ
#   bind-позе: L_j=(v-pivot_j)·R_j^T). Старый экспортёр их игнорировал и брал invBind=inv(frame0 КЛИПА) —
#   неверная bind-поза → тело растягивалось/разлеталось. Здесь мы ВОССТАНАВЛИВАЕМ истинный bind пер-кость
#   фитом pos0->POSITION_j (резидуал ~0), и ставим invBind_glTF_j = ZH·IBM_game_j^T·ZH, node-rest=inv(того).
#   Анимации (jointWorld абсолютных кейфреймов) — как раньше. Тождество jointWorld·invBind·P = ZH·posWorld_game
#   проверено аналитически и численно.
#
# Запуск:  python asda_test_export_one.py [имя_модели_без_ext]
#   по умолчанию npc_f_h_charlotte-a_00. Выход: RawAssets/_test/<model>_FIXED.glb
import sys, os, struct, importlib.util
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import numpy as np
import pygltflib as g
import asda_jbm as JBM   # ★ единый разбор .jbm (шапка + Pass1)

# ★ Флаг совместимости: 1 = прежний «нейтральный bind» (identity) для костей с экстрим-масштабом.
#   По умолчанию 0 — экстрим-масштаб, подтверждённый кейфреймами клипа, считается НАСТОЯЩИМ.
_BIND_LEGACY_IDENTITY = _os.environ.get("ASDA_BIND_LEGACY_IDENTITY", "0") == "1"
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = _P.DATA
NPC_MODEL = GAME + r"\NPC\Model\NpcModel.bnz"
NPC_MOTION = GAME + r"\NPC\Motion\NpcMotion.baz"
OUT_DIR = os.path.join(HERE, "..", "RawAssets", "_test")

# переиспользуем проверенные функции (parse_jbb, jointWorld, decompose, eng_row_matrix, ZH)
spec = importlib.util.spec_from_file_location("S", os.path.join(HERE, "asda_skinned_to_gltf.py"))
S = importlib.util.module_from_spec(spec); sys.modules["S"] = S; spec.loader.exec_module(S)
ZH = np.diag([1.0, 1.0, -1.0, 1.0])

# ---------- расширенный парс .jbm: ВКЛЮЧАЯ POSITION0/POSITION1 ----------
def parse_jbm_ext(b):
    # ★ Шапка + Pass1 — ЕДИНЫЙ модуль asda_jbm.py (общий с asda_jbm_to_gltf / asda_skinned_to_gltf).
    #   Ключ модельной позиции здесь исторически 'pos0' (в модуле 'pos') — добавляем алиас,
    #   чтобы Pass2/build_fixed ниже не менялись. Пер-меш материал (matIdx@0x1f4) сохранён.
    m = JBM.parse_geometry(b, guard=False)
    if m is None: return None
    tex0 = m["tex"]; meshes = m["meshes"]; geo = m["geoEnd"]
    for me in meshes:
        me["pos0"] = me["pos"]
    # Pass2: скин-блок по meshType; ИЗВЛЕКАЕМ POSITION0/POSITION1 (бон-локальные забинженные позиции)
    p2 = geo
    for me in meshes:
        V, M, Bn, mt = me['V'], me['M'], me['B'], me['mtype']; o = p2
        sel = 0x800 if mt&0x800 else 0x200 if mt&0x200 else 0x100 if mt&0x100 else 0x80 if mt&0x80 else 0
        P0 = P1 = None; w0 = np.ones(V, np.float32); bi0 = np.zeros(V, np.int64); bi1 = np.zeros(V, np.int64); pal = None
        me['names_layout'] = []               # имена костей ПО LAYOUT (M×0x20 в начале скин-блока)
        if sel in (0x80, 0x100) and M > 0:
            me['names_layout'] = [b[o+i*0x20:o+i*0x20+0x20].split(b"\0")[0].decode("latin1", "ignore") for i in range(M)]
        if sel == 0x80:                       # ВЗВЕШЕННЫЙ мульти-кост: ДВЕ позиции + вес + 2 индекса
            o += 0x20*M
            P0 = np.frombuffer(b, np.float32, V*3, o).reshape(V, 3).copy(); o += 0xC*V
            P1 = np.frombuffer(b, np.float32, V*3, o).reshape(V, 3).copy(); o += 0xC*V
            w0 = np.frombuffer(b, np.float32, V, o).copy(); o += 4*V
            bi0 = np.frombuffer(b, np.uint32, V, o).astype(np.int64); o += 4*V
            bi1 = np.frombuffer(b, np.uint32, V, o).astype(np.int64); o += 4*V
            pal = np.frombuffer(b, np.int32, M, o).copy(); o += 4*M; o += 0x40*Bn
        elif sel == 0x100:                    # ОДНО-костный жёсткий: ОДНА позиция + индекс
            o += 0x20*M
            P0 = np.frombuffer(b, np.float32, V*3, o).reshape(V, 3).copy(); o += 0xC*V
            bi0 = np.frombuffer(b, np.uint32, V, o).astype(np.int64); o += 4*V; bi1 = bi0.copy()
            pal = np.frombuffer(b, np.int32, M, o).copy(); o += 4*M; o += 0x40*Bn
            P1 = P0
        elif sel == 0x200:                    # ригид-узел: одна позиция, привязка по nodeName
            P0 = np.frombuffer(b, np.float32, V*3, o).reshape(V, 3).copy(); o += 0xC*V
            pal = np.frombuffer(b, np.int32, 3, o).copy(); o += 12; o += 0x40*3
            P1 = P0; bi0[:] = 0; bi1[:] = 0
        else:
            P0 = me['pos0'].copy(); P1 = P0
        me['P0'] = P0; me['P1'] = P1; me['w0'] = np.clip(np.nan_to_num(w0, nan=1.0), 0, 1)
        me['bi0'] = np.clip(bi0, 0, max(M-1, 0)); me['bi1'] = np.clip(bi1, 0, max(M-1, 0)); me['pal'] = pal
        me['sel'] = sel
        p2 = o
    me_names = S._extract_mesh_bone_names(b, [m['M'] for m in meshes])
    for i, me in enumerate(meshes):
        me['names'] = me.get('names_layout') or me_names.get(i, [])   # layout приоритетно (числовые имена cremmy)
    return dict(tex=tex0, meshes=meshes)

# ---------- резолв bi0/bi1 -> индекс кости скелета (ПО ИМЕНИ, как движок) ----------
def resolve_names(jbm, jbb):
    import re
    norm = lambda s: re.sub(r'\s+', ' ', s or '').strip().lower()
    jmap = {}
    for i, bn in enumerate(jbb['bones']): jmap.setdefault(norm(bn['name']), i)
    for me in jbm['meshes']:
        nm = me.get('names') or []
        if nm:
            idx = np.array([jmap.get(norm(n), 0) for n in nm], np.int64)
            me['j0'] = idx[np.clip(me['bi0'], 0, len(idx)-1)]
            me['j1'] = idx[np.clip(me['bi1'], 0, len(idx)-1)]
        else:
            bi = jmap.get(norm(me.get('node', '')))
            j = bi if bi is not None else _static_fallback_bone(jbb)
            me['j0'] = np.full(len(me['pos0']), j, np.int64); me['j1'] = me['j0'].copy()


# ★★★ 22.10.2026 [ИСПРАВЛЕНИЕ ПОРТА] подмеш, чьё имя узла НЕ совпало ни с одной костью клипа: оригинал рисует его как есть, без
#   кости (неподвижно). Прежний запасной вариант «кость №0» у досок объявлений (`mdel_raidboard`, `mdel_noticeboard`) попадал
#   в служебную `b_bone01` — маркер короба взаимодействия (entity_char.c:59550) с поворотом 120° в клипе: доска ложилась на бок
#   (отчёт `Reports/npc_pick_zone_2026-10-22/REPORT.md`). Теперь — первая НЕПОДВИЖНАЯ кость (позиция и поворот одинаковы на
#   всех ключах), служебные `b_bone*` — в последнюю очередь; нет такой — кость №0, как раньше.
def _static_fallback_bone(jbb):
    bones = jbb.get('bones') or []

    def still(bn):
        if not bn.get('keyCount'):
            return True
        p, q = bn['pos'], bn['quat']
        return float(np.ptp(p, axis=0).max()) < 1e-3 and float(np.ptp(q, axis=0).max()) < 1e-4

    order = [i for i, bn in enumerate(bones) if not bn['name'].lower().startswith('b_bone')] +             [i for i, bn in enumerate(bones) if bn['name'].lower().startswith('b_bone')]
    for i in order:
        if still(bones[i]):
            return i
    return 0

# ---------- ВОССТАНОВЛЕНИЕ истинного bind пер-кость: фит pos0 -> POSITION_j (game-space, row-vector) ----------
def recover_binds(jbm, B, bones=None):
    # bones (опц., список костей референс-клипа) → РЕГУЛЯРИЗАЦИЯ ПО КАДРУ 0.
    #   Зачем: у костей, чьи вершины КОМПЛАНАРНЫ/малочисленны (напр. клавикулы cre_p_0_pet01: 5 пар,
    #   3 уникальные точки, 3-й сингуляр 0), матрица [pos|1] неполноранговая. lstsq тогда выдаёт
    #   ЛЮБОЕ из бесконечного семейства решений — на практике «взорванное» (bindScale до ×284),
    #   а бэкстоп build_fixed откатывает такую кость на кадр0 клипа → возвращается ИСХОДНЫЙ баг
    #   (замер: вершина уезжала на 84 ед при размере пета 67).
    #   Решение: искать МИНИМАЛЬНУЮ поправку к кандидату «кадр 0»:  X = X0 + pinv(A)·(dst − A·X0).
    #   В определённых данными направлениях фит ТОЧЕН (резидуал = резидуалу lstsq), в остальных
    #   остаётся натуральная поза кадра 0 → масштаб адекватный. Для ПОЛНОРАНГОВОЙ A результат
    #   ТОЖДЕСТВЕНЕН старому lstsq (проверено), т.е. регрессии для нормальных костей нет.
    # на кость скелета j собираем пары (pos0, POSITION для этой кости) из всех мешей и обеих сторон скина
    srcs = [[] for _ in range(B)]; dsts = [[] for _ in range(B)]
    for me in jbm['meshes']:
        for (jarr, Parr) in ((me['j0'], me['P0']), (me['j1'], me['P1'])):
            for k in range(len(me['pos0'])):
                j = int(jarr[k]); srcs[j].append(me['pos0'][k]); dsts[j].append(Parr[k])
    IBM_game = [None]*B; resid = [None]*B
    for j in range(B):
        if len(srcs[j]) < 4:
            continue
        # ПОЛНЫЙ аффинный фит pos0→POSITION: ТОЧНО воспроизводит игру, ВКЛЮЧАЯ настоящий неравномерный масштаб
        # костей-стрейчей плаща (Bone_a3 ×7.7, Bone_r3 ×9.1 — легитимная растяжка подола). НЕ заменять на Umeyama:
        # тот выкидывает неравномерность → подол разъезжает диском. Вырожденный overfit (×40-60) ловит бэкстоп
        # в build_fixed (bind scale>13 → clip0) — он трогает ТОЛЬКО взорванные, не настоящие ≤13.
        src = np.array(srcs[j], float); dst = np.array(dsts[j], float); n = len(src)
        Asrc = np.hstack([src, np.ones((n, 1))])                           # [pos|1]  (N×4)
        X0 = None                                                          # кандидат «кадр 0» (4×3 блок IBM)
        if bones is not None and j < len(bones) and bones[j]['keyCount']:
            bn = bones[j]
            W0 = S.eng_row_matrix(bn['quat'][0], bn['pos'][0], bn['scale'][0], bn['conv'])
            try:
                X0 = np.linalg.inv(W0)[:, :3]
            except Exception:
                X0 = None
        if X0 is None:
            X, *_ = np.linalg.lstsq(Asrc, dst, rcond=None)                 # 4×3 :  [pos|1]·X = POSITION
        else:
            X = X0 + np.linalg.pinv(Asrc) @ (dst - Asrc @ X0)              # мин. поправка к кадру 0 (см. выше)
        M = np.eye(4); M[:, :3] = X                                        # 4×4 row-vector IBM (game-space)
        if abs(np.linalg.det(M[:3, :3])) < 1e-7:                           # вырождено (компланарные) → откат clip0
            continue
        if X0 is not None:                                                 # страховка: всё равно «взорвано» → чистый кадр0
            bs = np.linalg.inv(M)
            if max(np.linalg.norm(bs[k, :3]) for k in range(3)) > 13.0:
                M = np.eye(4); M[:, :3] = X0
        resid[j] = float(np.linalg.norm(Asrc @ X - dst, axis=1).mean())
        IBM_game[j] = M
    return IBM_game, resid

# ---------- фикс вырожденных костей (<4 фит-пар): «мосты»-перепонки лент/рукавов ----------
def fix_degenerate_binds(jbm, jbb, IBM_game):
    # Кости, используемые вершинами, но НЕ пофиченные recover_binds (<4 пар — фит недоопределён:
    # резидуал 0, а поворот мусор). Симптом: маленькие меши-«ленты» (V≈M, по 1-2 вершины на кость)
    # рисуются ПЕРЕПОНКАМИ в авторской T-позе (мосты от рук — wendy/lualua/fhashu).
    # ТОЧНАЯ подстановка вместо фита: вершины кости переводим в МИР КАДРА 0 (pos0 := [P|1]·W0_row),
    # IBM_game := inv(W0_row) → тождество [pos0|1]·IBM = P (бон-локаль) выполняется ТОЧНО для любого
    # числа вершин, и рантайм v(t)=P·W(t) совпадает с игрой 1:1.
    used = set()
    for me in jbm['meshes']:
        used |= set(int(x) for x in me['j0']); used |= set(int(x) for x in me['j1'])
    W0row = {}
    for j in sorted(used):
        if IBM_game[j] is not None: continue
        bn = jbb['bones'][j]
        if bn['keyCount'] < 1: continue
        M = S.eng_row_matrix(bn['quat'][0], bn['pos'][0], bn['scale'][0], bn['conv'])
        try: IBM_game[j] = np.linalg.inv(M)
        except Exception: continue
        W0row[j] = M
    if not W0row: return 0
    nfix = 0
    for me in jbm['meshes']:
        for k in range(len(me['pos0'])):
            j = int(me['j0'][k])
            if j in W0row:
                p = np.append(np.asarray(me['P0'][k], float), 1.0) @ W0row[j]
                me['pos0'][k] = p[:3]
                nfix += 1
    return nfix

# ---------- ★★★ TRS-ПЕРЕФИТ BIND (2026-09-11, дефект «скукожена часть костей») ----------
# ЗАЧЕМ. Узел glTF несёт ТОЛЬКО translation/rotation/scale — СДВИГА (shear) в нём нет. UE при
#   импорте кладёт POSITION в меш КАК ЕСТЬ (доказано замером габаритов импортированного
#   SkeletalMesh: bbox == bbox POSITION 1:1, `Reports/mob_ground/ue_refpose.json`), а обратную
#   bind для скиннинга берёт из ref-скелета, т.е. inv(TRS(bind)), а НЕ из аккессора
#   inverseBindMatrices. Значит любой СДВИГ в нашей bind-матрице в UE теряется, и вершины кости
#   уезжают ИЛИ СКУКОЖИВАЮТСЯ ровно на потерянную часть.
# ОТКУДА СДВИГ. `recover_binds` делает ПОЛНЫЙ аффинный фит pos0→POSITION. У кости, чьи вершины
#   почти коллинеарны/компланарны (3-10 вершин лент, «зонтик» opara: 5 точек в одной плоскости),
#   матрица [pos|1] неполноранговая → lstsq выдаёт ЛЮБОЕ решение семейства: со сдвигом, с
#   зеркалом (det<0) и с выдуманным масштабом. Пример npc_f_m_opara-a_00 / `Bip01 Ponytail1`:
#   bind (9.18, 7.43, 1.00), det −11.4, а НАСТОЯЩИЙ масштаб кости в клипе (1,1,1) → в UE вершины
#   делятся на 9.18 и скукоживаются к началу кости. Это ЗЕРКАЛО бага «огромные головы»
#   (там наоборот: настоящий масштаб ×20 подменяли на identity и вершины раздувало).
# ЧТО ДЕЛАЕМ. Авторская bind в 3ds Max — это T·R·S, сдвига там и не было. Ищем НАИЛУЧШУЮ
#   TRS-матрицу (чередование Прокруста и покоординатного масштаба, closed-form на каждом шаге) и
#   принимаем её ТОЛЬКО если она СТРОГО лучше по замеру «наш меш в UE vs игра» по кадрам клипа.
#   Поэтому регрессия невозможна по построению: текущий bind всегда среди кандидатов.
# ОТКАТ: ASDA_BIND_TRS_REFIT=0.
_BIND_TRS_REFIT = _os.environ.get("ASDA_BIND_TRS_REFIT", "1") != "0"

def _mat_trs(M):
    """Ближайшее T·R·S к M (ровно то, что останется от матрицы в узле glTF)."""
    t, q, s = S.decompose(M); x, y, z, w = q
    R = np.array([[1-2*(y*y+z*z), 2*(x*y-z*w),   2*(x*z+y*w)],
                  [2*(x*y+z*w),   1-2*(x*x+z*z), 2*(y*z-x*w)],
                  [2*(x*z-y*w),   2*(y*z+x*w),   1-2*(x*x+y*y)]], float)
    out = np.eye(4); out[:3, :3] = R @ np.diag(np.asarray(s, float)); out[:3, 3] = np.asarray(t, float)
    return out

def _safe_inv(M):
    try:
        if abs(np.linalg.det(M)) < 1e-12: return np.linalg.pinv(M)
        return np.linalg.inv(M)
    except Exception:
        return np.linalg.pinv(M)

def _fit_trs(P, V, s0=None, iters=60):
    """bm (столбцовая 4×4, блок 3×3 = R·diag(s)) : bm·P ≈ V по методу наименьших квадратов.
    Чередуем Прокруста (при фиксированном масштабе) и покоординатный масштаб (при фиксированном
    повороте) — оба шага в замкнутой форме, сходится за десятки итераций. По осям, которые данные
    НЕ определяют (Σp²≈0), масштаб не трогаем — остаётся стартовый, т.е. выдумки не появляется."""
    P = np.asarray(P, float); V = np.asarray(V, float)
    cP = P.mean(0); cV = V.mean(0); P0 = P - cP; V0 = V - cV
    den = (P0*P0).sum(0)
    s = np.array([1.0, 1.0, 1.0]) if s0 is None else np.clip(np.abs(np.asarray(s0, float)), 1e-3, 1e3)
    R = np.eye(3)
    for _ in range(iters):
        A = P0*s[None, :]
        U, _sv, Vt = np.linalg.svd(A.T @ V0)
        D = np.diag([1.0, 1.0, float(np.sign(np.linalg.det(Vt.T @ U.T)) or 1.0)])
        R = Vt.T @ D @ U.T
        Q = V0 @ R
        new = np.where(den > 1e-9, (Q*P0).sum(0)/np.maximum(den, 1e-12), s)
        new = np.where(np.abs(new) < 1e-4, s, new)
        if np.allclose(new, s, rtol=1e-9, atol=1e-12): s = new; break
        s = new
    out = np.eye(4); out[:3, :3] = R @ np.diag(s); out[:3, 3] = cV - out[:3, :3] @ cP
    return out

def _bind_backstop(bm, bone):
    """Тот же бэкстоп экстрим-масштаба, что применяет build_fixed (идемпотентен)."""
    s = [float(np.linalg.norm(bm[:3, k])) for k in range(3)]
    if max(s) > 13.0 or min(s) < 1/13.0:
        jw = S.jointWorld(bone, 0); sj = [float(np.linalg.norm(jw[:3, k])) for k in range(3)]
        if not (max(sj) > 13.0 or min(sj) < 1/13.0): return jw
        if _BIND_LEGACY_IDENTITY: return np.eye(4)
    return bm

def _ue_bone_err(bone, bm, pos0g, Pgame, frames):
    """Отклонение вершин кости «наш меш в UE» от ИГРЫ, макс по кадрам клипа.
       v_UE(k) = W_gltf(k)·inv(TRS(bind))·pos0 ;  v_игра(k) = ZH( (P|1)·W_game_row(k) )."""
    IT = _safe_inv(_mat_trs(bm))
    Vh = np.hstack([pos0g, np.ones((len(pos0g), 1))])
    Ph = np.hstack([Pgame, np.ones((len(Pgame), 1))])
    kc = max(bone['keyCount'], 1); worst = 0.0
    for k in frames:
        kk = min(k, kc-1)
        Wg = S.eng_row_matrix(bone['quat'][kk], bone['pos'][kk], bone['scale'][kk], bone['conv'])              if bone['keyCount'] else np.eye(4)
        ue = (S.jointWorld(bone, kk) @ IT @ Vh.T).T[:, :3]
        gm = (Ph @ Wg)[:, :3].copy(); gm[:, 2] *= -1
        worst = max(worst, float(np.linalg.norm(ue-gm, axis=1).max()))
    return worst

def trs_refit_binds(jbm, jbb, IBM_game, nframes=6, verbose=False):
    """Перефит bind в форму T·R·S там, где это СТРОГО уменьшает отклонение от игры в UE.
    Возвращает список (имя_кости, было, стало). IBM_game правится на месте."""
    if not _BIND_TRS_REFIT: return []
    B = len(IBM_game); bones = jbb['bones']
    src = [[] for _ in range(B)]; dst = [[] for _ in range(B)]
    for me in jbm['meshes']:
        for jarr, Parr in ((me['j0'], me['P0']), (me['j1'], me['P1'])):
            for k in range(len(me['pos0'])):
                j = int(jarr[k])
                if j < B: src[j].append(me['pos0'][k]); dst[j].append(Parr[k])
    changed = []
    for j in range(B):
        if IBM_game[j] is None or not src[j]: continue
        bone = bones[j]
        pos0g = np.array(src[j], float); pos0g[:, 2] *= -1          # модельная позиция в glTF-осях
        Pg = np.array(dst[j], float); Pgg = Pg.copy(); Pgg[:, 2] *= -1   # бон-локаль в glTF-осях
        kc = max(bone['keyCount'], 1)
        frames = sorted(set(int(round(i*(kc-1)/max(nframes-1, 1))) for i in range(nframes)))
        ib = ZH @ IBM_game[j].T @ ZH
        bmA = _bind_backstop(_safe_inv(ib), bone)
        eA = _ue_bone_err(bone, bmA, pos0g, Pg, frames)
        if eA <= 1e-6: continue                                     # уже точно — не трогаем
        s0 = [float(np.linalg.norm(bmA[:3, k])) for k in range(3)]
        best = (eA, bmA, None)
        for cand in (_fit_trs(Pgg, pos0g, s0), _fit_trs(Pgg, pos0g, None)):
            bm = _bind_backstop(cand, bone)
            e = _ue_bone_err(bone, bm, pos0g, Pg, frames)
            if e < best[0] - 1e-6: best = (e, bm, 'trs')
        if best[2] is None: continue
        IBM_game[j] = (ZH @ _safe_inv(best[1]) @ ZH).T              # обратно в game-space IBM
        changed.append((bone['name'], eA, best[0]))
        if verbose: print("    bind TRS-перефит %-24s %8.2f -> %8.2f" % (bone['name'], eA, best[0]))
    return changed

# ---------- сборка glTF с ИСПРАВЛЕННЫМ bind ----------
def prune_unskinned(jbm, clips, IBM_game, extra_anchors=()):
    # ★ extra_anchors (дедуп №2 В1 Т10, 11.10.2026) — ДОПОЛНИТЕЛЬНЫЕ якоря (имена в нижнем регистре) сверх общих
    #   KEEP_ANCHORS для одного вызова. Умолчание () — поведение прежнее для всех. Нужен рыбалке (8a1_dummy/Bone24/
    #   fs_Bone01): раньше там жила копия этой функции `fishing_export_assets.prune_keep_anchors`. Общий набор НЕ
    #   расширяем: `Bone24` встречается в чужих моделях и поменял бы их скелеты при переэкспорте.
    # FLAT-скелет: кости-сиблинги под Armature. Кости, которые меш НЕ использует (j0/j1), — хелперы
    # (Ponytail/IK Chain/Box/Nub/Dummy) с абсолютными кейфреймами → улетают, раздувают bounds, рисуются
    # «растянутыми». Безопасно отсекаем их (на меш не влияют). Возвращаем remap старый->новый индекс.
    B = clips[0][1]['B']
    used = set()
    for me in jbm['meshes']:
        used |= set(int(x) for x in me['j0']); used |= set(int(x) for x in me['j1'])
    # Якоря крепления (не скиннятся, но НУЖНЫ): оружие/эффекты крепятся к M_DUMMY (эталон
    # List_FindIndexByName(...,"M_DUMMY"), унисекс cremmy). Сохраняем кость+кейфреймы для единого скелета.
    # ★ Flag_Link — кость крепления ФЛАГА КЛАНА в меше ТРАНСПОРТА (эталон List_FindIndexByName(...,"Flag_Link"),
    #   mapobj_updateFlagLinkAnim@0xFB0CB0). Она НЕ скиннута ни в одной из 50 моделей транспорта, где есть,
    #   поэтому без этого исключения prune_unskinned её отсекал бы и уже работающая фича «флаг на маунте»
    #   ломалась бы молча (AsdaPlayerCharacter::ClanFlagBone не находит кость → флаг не создаётся).
    KEEP_ANCHORS = {'m_dummy', 'flag_link'} | set(extra_anchors)
    for _i, _b in enumerate(clips[0][1]['bones']):
        if (_b.get('name') or '').strip().lower() in KEEP_ANCHORS:
            used.add(_i)
    keep = sorted(used)
    remap = {old: new for new, old in enumerate(keep)}
    # ремап индексов костей в мешах
    for me in jbm['meshes']:
        me['j0'] = np.array([remap[int(x)] for x in me['j0']], np.int64)
        me['j1'] = np.array([remap[int(x)] for x in me['j1']], np.int64)
    # ужимаем ТОЛЬКО референс-клип (скелет + bind); остальные клипы маппятся по ИМЕНИ — их не трогаем
    ref_name, ref = clips[0]
    ref2 = dict(ref, B=len(keep), bones=[ref['bones'][i] for i in keep])
    new_clips = [(ref_name, ref2)] + list(clips[1:])
    new_IBM = [IBM_game[i] for i in keep]
    return new_clips, new_IBM, len(keep), B

def build_fixed(jbm, clips, out, IBM_game, rest_frame=None, bake_pose=False, blend_mat_suffix=None,
                mat_names=None):
    # ★ mat_names (2026-08-18, зона NPC/мобов, отчёт `Reports/blend_rollout_npc_mobs.md`) — СПИСОК
    #   имён материалов по подсеткам (длина = len(jbm['meshes'])). Если задан, он ПОБЕЖДАЕТ и имя
    #   текстуры, и `blend_mat_suffix`. Зачем отдельный список, а не ещё один суффикс: у NPC и мобов
    #   в ОДНОЙ модели встречаются подсетки РАЗНЫХ техник (tSkin/tRigid и tNoSkin) с ОДНОЙ текстурой —
    #   79 таких слотов. UE сливает материалы по имени в ОДИН слот, поэтому развести их мастера
    #   можно только разными именами. Имена считает `asda_mesh_render_class.material_names()`
    #   прямо из `meshType`/`renderFlags`, то есть переэкспорт воспроизводит результат.
    # blend_mat_suffix=None → поведение НЕ меняется (имя материала = имя текстуры без расширения).
    #   Строка → у мешей, прошедших `asda_jbm.is_alpha_blend` (рендер-бит `mr+0x004 & 0x1|0x2`
    #   И техника tNoSkin), имя материала получает этот суффикс, поэтому UE заводит для них
    #   ОТДЕЛЬНЫЙ слот меша и на него можно повесить прозрачный мастер `M_AsdaObjectBlendLit`.
    #   Иначе купол автомата гачи (`Bowl`) делит слот с корпусом (обе подсетки носят текстуру
    #   `md_Gachya_x_00`) и физически не может быть прозрачным.
    #   Импортёры выбирают мастер ПО ИМЕНИ СЛОТА (asda_material_policy.master_for), поэтому
    #   результат воспроизводится при переимпорте.
    # bake_pose=True → POSITION/NORMAL запекаются в позу кадра 0 клипа 0 (idle): v_idle = Σ w·(W[j][0]·invBind[j])·v.
    #   Меш становится СТАТИЧНО idle-позированным (руки вниз) — обходит рантайм-скиннинг UE, который НЕ тянет
    #   2-позиционный взвешенный бинд Asda (та же причина «нет тел NPC»). Скелет/JOINTS/WEIGHTS/IBM оставляем,
    #   но аниму на компоненте НЕ проигрывать (иначе двойная деформация). Для char-select превью + экип-частей.
    # rest_frame=None → узловая rest-поза = bindG (bind, обычно T-поза). rest_frame=K → rest = jointWorld(kадр K)
    #   клипа 0. Для char-select базы K=0 c клипом dr_ch_ani_idle: статичная rest-поза = idle (руки ВНИЗ), даже
    #   если UE дропнет арм-треки анимы (плоский скелет). invBind НЕ меняется → тождество jointWorld·invBind·P
    #   держится для ЛЮБОЙ позы (вкл. idle rest); скиннинг корректен.
    jbb = clips[0][1]; B = jbb['B']; bones = jbb['bones']
    def safe_inv(M):
        try:
            if abs(np.linalg.det(M)) < 1e-12: return np.linalg.pinv(M)
            return np.linalg.inv(M)
        except Exception:
            return np.linalg.pinv(M)
    # bind-матрица кости в glTF-пространстве: если восстановлена фитом — из неё, иначе fallback на frame0 клипа
    def scale_of(M):
        R3 = M[:3, :3]
        return [float(np.linalg.norm(R3[:, k])) for k in range(3)]
    bindG = []; invBind = []
    for i in range(B):
        if IBM_game[i] is not None:
            ib = ZH @ IBM_game[i].T @ ZH                 # invBind_glTF = ZH·IBM_game^T·ZH (доказано)
            bm = safe_inv(ib)                            # bind world (glTF) = inv(invBind)
        else:
            bm = S.jointWorld(bones[i], 0); ib = safe_inv(bm)   # fallback (кость без скин-вершин / плохой фит)
        # БЭКСТОП: bind-нода с экстрим-масштабом «взрывается» в вес-редакторе UE → нейтрализуем.
        # ★★★ 2026-09-11: бэкстоп ловил и НАСТОЯЩИЙ крупный масштаб кости из данных оригинала.
        #   Признак «взрыва фита» — экстрим ТОЛЬКО в bind, при натуральном масштабе кости в клипе (jw≈1).
        #   Признак НАСТОЯЩЕГО масштаба — тот же экстрим стоит и в кейфреймах клипа (jw == bind).
        #   Тогда «нейтральный bind» = identity делал вершину модельной, и она уезжала/раздувалась
        #   на весь масштаб кости: голова моба уходила от шеи и становилась в ~s раз больше
        #   (palace_of_queen: knigt01 s=20.4, pawn01 s=14.4 — отклонение 2865 и 2002 ед при размере ~140).
        #   Фикс: если экстрим-масштаб ЕСТЬ И в клипе — он из данных, bind фита ТОЧЕН, оставляем его.
        #   Откат к прежнему поведению: ASDA_BIND_LEGACY_IDENTITY=1.
        s = scale_of(bm)
        if max(s) > 13.0 or min(s) < 1/13.0:
            jw = S.jointWorld(bones[i], 0); s_jw = scale_of(jw)
            if max(s_jw) > 13.0 or min(s_jw) < 1/13.0:
                if _BIND_LEGACY_IDENTITY:
                    bm = np.eye(4); ib = np.eye(4)              # прежнее поведение (ломало кость)
                # иначе: масштаб настоящий → bm/ib из фита оставляем как есть
            else:
                bm = jw; ib = safe_inv(bm)                      # натуральный масштаб кости (≤~9)
        bindG.append(bm); invBind.append(ib)
    # анимационные мировые матрицы кости по кадрам — как в оригинале (абсолютные кейфреймы)
    W = [[S.jointWorld(bones[i], k) for k in range(max(bones[i]['keyCount'], 1))] for i in range(B)]

    bins = bytearray(); accessors = []; views = []
    def view(data, target=None):
        off = len(bins); bins.extend(data)
        while len(bins) % 4: bins.append(0)
        views.append(g.BufferView(buffer=0, byteOffset=off, byteLength=len(data), target=target)); return len(views)-1
    def acc(data, ctype, count, atype, mn=None, mx=None, target=None):
        v = view(data, target); a = g.Accessor(bufferView=v, componentType=ctype, count=count, type=atype)
        if mn is not None: a.min = mn; a.max = mx
        accessors.append(a); return len(accessors)-1

    nodes = []; joint_node = []
    for i in range(B):
        restM = S.jointWorld(bones[i], rest_frame) if (rest_frame is not None) else bindG[i]
        t, q, s = S.decompose(restM)
        nodes.append(g.Node(name=bones[i]['name'] or ("bone%d" % i),
                            translation=t.tolist(), rotation=[float(x) for x in q], scale=s.tolist()))
        joint_node.append(len(nodes)-1)
    nodes.append(g.Node(name="Armature", children=list(joint_node))); root_idx = len(nodes)-1

    prims = []; mats = []
    for _mesh_i, me in enumerate(jbm['meshes']):
        used, inv = np.unique(me['idx'], return_inverse=True)
        if bake_pose:
            # запечь idle (кадр0 клипа0) в вершины: v_idle = Σ w·(W[j][0]·invBind[j])·v ; нормаль — поворотом дом. кости
            pos_f = me['pos0'].copy(); pos_f[:, 2] *= -1
            nrm_f = me['nrm'].copy(); nrm_f[:, 2] *= -1
            j0f = me['j0']; j1f = me['j1']; w0f = me['w0']
            bp = np.zeros_like(pos_f); bn = np.zeros_like(nrm_f)
            for k in range(len(pos_f)):
                v = np.append(pos_f[k], 1.0)
                a = int(j0f[k]); b = int(j1f[k]); w = float(w0f[k])
                m0 = W[a][0] @ invBind[a]; m1 = W[b][0] @ invBind[b]
                bp[k] = (w*(m0 @ v) + (1.0-w)*(m1 @ v))[:3]
                md = m0 if w >= 0.5 else m1
                bn[k] = md[:3, :3] @ nrm_f[k]
            P = bp[used].copy(); N = bn[used].copy()
            UVv = me['uv'][used].copy()
        else:
            P = me['pos0'][used].copy(); P[:, 2] *= -1                       # модельная позиция, z-зеркало (LH→RH)
            N = me['nrm'][used].copy(); N[:, 2] *= -1
            UVv = me['uv'][used].copy()
        # САНИТИЗАЦИЯ: в части .jbm нормали/позиции бывают нечисловыми (NaN) — UE их обнуляет с ошибкой.
        P = np.nan_to_num(P, nan=0.0, posinf=0.0, neginf=0.0)
        UVv = np.nan_to_num(UVv, nan=0.0, posinf=0.0, neginf=0.0)
        N = np.nan_to_num(N, nan=0.0, posinf=0.0, neginf=0.0)
        ln = np.linalg.norm(N, axis=1); N[ln < 1e-6] = np.array([0.0, 0.0, 1.0])   # нулевые нормали → вверх
        J4 = np.zeros((len(P), 4), np.uint16); J4[:, 0] = np.clip(me['j0'][used], 0, B-1); J4[:, 1] = np.clip(me['j1'][used], 0, B-1)
        W4 = np.zeros((len(P), 4), np.float32); W4[:, 0] = me['w0'][used]; W4[:, 1] = 1.0 - me['w0'][used]
        I = inv.astype(np.uint32).reshape(-1, 3)[:, ::-1].reshape(-1)
        aP = acc(P.astype("<f4").tobytes(), g.FLOAT, len(P), g.VEC3, P.min(0).tolist(), P.max(0).tolist(), g.ARRAY_BUFFER)
        aN = acc(N.astype("<f4").tobytes(), g.FLOAT, len(N), g.VEC3, target=g.ARRAY_BUFFER)
        aU = acc(UVv.astype("<f4").tobytes(), g.FLOAT, len(UVv), g.VEC2, target=g.ARRAY_BUFFER)
        aJ = acc(J4.tobytes(), g.UNSIGNED_SHORT, len(P), g.VEC4, target=g.ARRAY_BUFFER)
        aW = acc(W4.astype("<f4").tobytes(), g.FLOAT, len(P), g.VEC4, target=g.ARRAY_BUFFER)
        aI = acc(I.astype("<u4").tobytes(), g.UNSIGNED_INT, len(I), g.SCALAR, target=g.ELEMENT_ARRAY_BUFFER)
        mat_name = os.path.splitext(me.get('tex') or jbm['tex'])[0] or "mat"
        # ★★★ КРИТЕРИЙ (2026-08-18, `Reports/jbm_blend_flag_study.md` §5): метку получает меш,
        #   у которого взведён рендер-бит `mr+0x004 & (0x1|0x2)` И техника шейдера — tNoSkin
        #   (meshType 0x200/0x201/0x800/0x801). Раньше здесь стоял ОДИН бит `me['blend']`: у tSkin/
        #   tRigid тот же бит стоит у 553 мешей NPC/экипа/мобов, но их альфа по `PShade()` — маска
        #   блика (Tex.w заменён на 1), и прозрачность там воспроизвела бы баг «дырявого оружия».
        if blend_mat_suffix and JBM.is_alpha_blend(me):
            mat_name += blend_mat_suffix          # отдельный слот для полупрозрачной подсетки
        if mat_names is not None:                 # ★ готовый список имён побеждает (см. шапку)
            mat_name = mat_names[_mesh_i]
        mats.append(g.Material(name=mat_name,
                    pbrMetallicRoughness=g.PbrMetallicRoughness(metallicFactor=0.0, roughnessFactor=1.0)))
        prims.append(g.Primitive(attributes=g.Attributes(POSITION=aP, NORMAL=aN, TEXCOORD_0=aU, JOINTS_0=aJ, WEIGHTS_0=aW),
                                 indices=aI, material=len(mats)-1))

    ibm = np.stack([invBind[i].T for i in range(B)]).astype("<f4")
    aIBM = acc(ibm.tobytes(), g.FLOAT, B, g.MAT4)
    skin = g.Skin(inverseBindMatrices=aIBM, joints=joint_node, skeleton=root_idx)
    nodes.append(g.Node(name="mesh", mesh=0, skin=0)); mesh_idx = len(nodes)-1

    # анимации — как в оригинале (FLAT абсолют), кости клипа мапим к референсу по имени
    nrm = lambda s: (s or "").strip().lower()
    anims = []
    for cname, cjbb in clips:
        cbones = cjbb['bones']; cidx = {nrm(cbones[j]['name']): j for j in range(len(cbones))}
        samplers = []; channels = []
        for i in range(B):
            ci = i if (i < len(cbones) and nrm(cbones[i]['name']) == nrm(bones[i]['name'])) else cidx.get(nrm(bones[i]['name']))
            if ci is None or cbones[ci]['keyCount'] < 1: continue
            cb = cbones[ci]; kc = cb['keyCount']
            Wc = [S.jointWorld(cb, k) for k in range(kc)]
            times = cb['t'].astype("<f4"); Ts = []; Qs = []; Ss = []
            for k in range(kc):
                t, q, s = S.decompose(Wc[k]); Ts.append(t); Qs.append(np.asarray(q, np.float64)); Ss.append(s)
            for k in range(1, len(Qs)):
                if np.dot(Qs[k], Qs[k-1]) < 0: Qs[k] = -Qs[k]
            Qs = [[float(x) for x in q] for q in Qs]
            aT = acc(times.tobytes(), g.FLOAT, len(times), g.SCALAR, [float(times.min())], [float(times.max())])
            aTr = acc(np.array(Ts, "<f4").tobytes(), g.FLOAT, len(Ts), g.VEC3)
            aRo = acc(np.array(Qs, "<f4").tobytes(), g.FLOAT, len(Qs), g.VEC4)
            aSc = acc(np.array(Ss, "<f4").tobytes(), g.FLOAT, len(Ss), g.VEC3)
            for path, a in (("translation", aTr), ("rotation", aRo), ("scale", aSc)):
                samplers.append(g.AnimationSampler(input=aT, output=a, interpolation="LINEAR"))
                channels.append(g.AnimationChannel(sampler=len(samplers)-1,
                                target=g.AnimationChannelTarget(node=joint_node[i], path=path)))
        if channels: anims.append(g.Animation(name=cname, samplers=samplers, channels=channels))

    modelname = os.path.splitext(os.path.basename(out))[0]
    gltf = g.GLTF2(scene=0, scenes=[g.Scene(nodes=[root_idx, mesh_idx])], nodes=nodes,
                   meshes=[g.Mesh(primitives=prims, name=modelname)], materials=mats, skins=[skin],
                   animations=anims, accessors=accessors, bufferViews=views, buffers=[g.Buffer(byteLength=len(bins))])
    gltf.set_binary_blob(bytes(bins)); gltf.save_binary(out)
    return invBind, bindG, W

# ---------- численная самопроверка скиннинга ----------
def validate(jbm, jbb, IBM_game, invBind, W):
    B = jbb['B']
    # 1) реконструкция bind: pos0·IBM_game ≈ POSITION (game-space)
    errs = []
    for me in jbm['meshes']:
        for jarr, Parr in ((me['j0'], me['P0']), (me['j1'], me['P1'])):
            for k in range(0, len(me['pos0']), 5):
                j = int(jarr[k])
                if IBM_game[j] is None: continue
                v = np.append(me['pos0'][k], 1.0); L = (v @ IBM_game[j])[:3]
                errs.append(np.linalg.norm(L - Parr[k]))
    # 2) скиннинг кадра0 в glTF-пространстве: Σ w·jointWorld(0)·invBind·P  → должно ≈ модельная pos0 (z-neg)
    maxr = 0; biggest = None
    for me in jbm['meshes']:
        P = me['pos0'].copy(); P[:, 2] *= -1; j0 = me['j0']; j1 = me['j1']; w0 = me['w0']
        out = np.zeros_like(P)
        for k in range(len(P)):
            v = np.append(P[k], 1.0)
            m0 = W[j0[k]][0] @ invBind[j0[k]]; m1 = W[j1[k]][0] @ invBind[j1[k]]
            out[k] = (w0[k]*(m0@v) + (1-w0[k])*(m1@v))[:3]
        r = np.linalg.norm(out, axis=1).max()
        if r > maxr: maxr = r; biggest = me['node']
    return (np.mean(errs) if errs else -1), maxr, biggest

def main():
    import zipfile
    model = sys.argv[1] if len(sys.argv) > 1 else "npc_f_h_charlotte-a_00"
    zm = zipfile.ZipFile(NPC_MODEL); za = zipfile.ZipFile(NPC_MOTION)
    jbm = parse_jbm_ext(zm.read(model + ".jbm"))
    clips = []
    for n in sorted(za.namelist()):
        base = n[:-4] if n.lower().endswith(".jbb") else None
        if base and base.startswith(model + "_") and base[len(model)+1:].isdigit():
            jbb = S.parse_jbb(za.read(n))
            if jbb: clips.append((("anim_%02d" % int(base[len(model)+1:])), jbb))
    if not jbm or not clips: print("нет модели/клипов для", model); return
    clips.sort(key=lambda c: c[0])
    jbb0 = clips[0][1]; B = jbb0['B']
    resolve_names(jbm, jbb0)
    IBM_game, resid = recover_binds(jbm, B)
    nfit = sum(1 for x in IBM_game if x is not None)
    # отсекаем не-скиннутые хелпер-кости (улетают в анимации → раздувают bounds / «растянутые» кости)
    clips, IBM_game, Bk, B0 = prune_unskinned(jbm, clips, IBM_game)
    jbb0 = clips[0][1]
    os.makedirs(OUT_DIR, exist_ok=True)
    out = os.path.join(OUT_DIR, model.replace("-", "_") + "_FIXED.glb")
    invBind, bindG, W = build_fixed(jbm, clips, out, IBM_game)
    bind_err, maxr, biggest = validate(jbm, jbb0, IBM_game, invBind, W)
    print("="*64)
    print("МОДЕЛЬ:", model, "| костей:", B0, "→", Bk, "(отсечено хелперов %d) | мешей:" % (B0-Bk), len(jbm['meshes']), "| клипов:", len(clips))
    print("восстановлено bind пер-кость: %d/%d (фит pos0->POSITION)" % (nfit, B0))
    print("реконструкция bind  pos0·IBM vs POSITION (game):  err=%.4f  (норма ~0 = верно)" % bind_err)
    print("скиннинг кадра0 в glTF: max|vertex|=%.1f (биггест меш %s) — должно ~ размер модели (десятки)" % (maxr, biggest))
    print("ВЫВОД:", "✅ OK" if (bind_err < 1.0 and maxr < 500) else "⚠️ проверь")
    print("ФАЙЛ ДЛЯ UE (перетащи в окно):")
    print("  ", os.path.abspath(out))
    print("="*64)

if __name__ == "__main__":
    main()
