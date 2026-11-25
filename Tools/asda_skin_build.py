# Copyright (c) 2026 @sitxovski (Discord)
# asda_skin_build.py — ЕДИНСТВЕННЫЙ вход сборки скиннованной модели .jbm + клипы .jbb → .glb
# (волна 2, п. 9 плана `Reports/dedup_audit/00_PLAN.md`, дефект E10).
#
# ★ ЗАКОН [[skin-pipeline-fixed-path]]: скин собирается ТОЛЬКО fixed-путём
#   parse_jbm_ext → resolve_names → recover_binds(bones) → fix_degenerate_binds → trs_refit_binds
#   → prune_unskinned (KEEP_ANCHORS) → build_fixed  (модуль asda_test_export_one).
#   Старый asda_skinned_to_gltf.build() приближает bind кадром клипа и растягивает модели.
#
# ★★★ ИСКЛЮЧЕНИЕ — МОДЕЛИ ЛОГИНА (шкатулка cre_creation01-a_00, задник cre_creback-a_00).
#   Их ассеты стоят в Login.umap (свет владельца, [[owner-login-light-protected]]), переимпорт
#   запрещён. Сухой прогон 14.09 (`Reports/dedup_audit/wave2_import.md` §4):
#     • текущие .glb в RawAssets совпадают со старым путём БАЙТ В БАЙТ (кроме имени меша = имени файла);
#     • fixed-путь МЕНЯЕТ геометрию: шкатулка отклонение от игры 8.31 → 0.33 ед,
#       задник 412.18 → 0.0005 ед (метрика Tools/_probe_skin_err.py).
#   Поэтому по умолчанию они собираются СТАРЫМ путём (MODE_LEGACY) — это не забытая ветка, а
#   зафиксированное решение до слова владельца. Включить fixed-путь для логина:
#   ASDA_LOGIN_SKIN_FIXED=1 (после этого нужен переимпорт — только с разрешения владельца).
#
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль ничего не делает; тяжёлые модули
#   конвертеров грузятся лениво при первом вызове.
import os
import sys
import importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

# Режимы сборки
MODE_FIXED = "fixed"     # закон: истинный двух-позиционный bind + TRS-перефит
MODE_LEGACY = "legacy"   # старый conv.build() — ТОЛЬКО модели логина до решения владельца

# Модели логина, которые по умолчанию остаются на старом пути (см. шапку).
LEGACY_LOGIN_MODELS = ("cre_creation01-a_00", "cre_creback-a_00")

# Переключатель: 1 = модели логина тоже fixed-путём.
ENV_LOGIN_FIXED = "ASDA_LOGIN_SKIN_FIXED"


def mode_for(model):
    """Режим сборки по умолчанию для модели: legacy только для моделей логина (если не включён fixed)."""
    if str(model).lower() in LEGACY_LOGIN_MODELS and os.environ.get(ENV_LOGIN_FIXED, "0") != "1":
        return MODE_LEGACY
    return MODE_FIXED


def _load(modname, filename):
    """Модуль из Tools по имени файла; повторно использует уже загруженный под тем же именем."""
    m = sys.modules.get(modname)
    if m is not None and os.path.basename(getattr(m, "__file__", "") or "") == filename:
        return m
    spec = importlib.util.spec_from_file_location(modname, os.path.join(HERE, filename))
    m = importlib.util.module_from_spec(spec)
    sys.modules[modname] = m
    spec.loader.exec_module(m)
    return m


def conv():
    """Старый конвертер asda_skinned_to_gltf (parse_jbb/parse_jbm/build) — под именем «conv», как раньше."""
    return _load("conv", "asda_skinned_to_gltf.py")


def fixed():
    """Модуль fixed-пути asda_test_export_one — под именем «T», как раньше."""
    return _load("T", "asda_test_export_one.py")


def parse_jbb(b):
    """Разбор клипа .jbb (общий для обоих путей)."""
    return conv().parse_jbb(b)


def align_clip_bones_by_index(ref, jbb):
    """★★★ [ОРИГИНАЛ] 04.11.2026 (баг владельца «анимация NPC Ian ломается при разговоре», `Reports/npc_talk_anim/REPORT.md`).
    Оригинал привязывает подмеши к костям ПО ИМЕНИ ОДИН РАЗ — по скелету клипа `_0` (NPC: `Entity_StreamLoadAndInitFSM@0xF99380`,
    моб: `mobEffect_LoadArchiveStep@0xF81B90` → `modelBuildMeshFromCache(…, modelDataPtr)` → `meshResolveBoneIndex@0x11A8C80` /
    `meshResolveBoneIndicesAll@0x11A8D80` пишут ИНДЕКС кости); любой другой клип читается ПО ИНДЕКСУ (и смешивание
    `Anim_BlendKeyframes@0x112AEF0` — тоже). Конвертер мапит дорожки клипов к референсу ПО ИМЕНИ, поэтому у кости,
    переименованной художником в клипе, дорожки не было и в UE она стояла в позе привязки (Ian-b у Ian в разговорных клипах 2–4,
    `mcrystal01-a_bone`, `Bone_g4` солдат шахты, `b2` у `mon_s_0_kcat02` в клипах 1/5–8, колесо `mon_s_0_madgear` в клипе 4).
    Клипу с ТЕМ ЖЕ числом костей даются имена референса по индексу — дальнейший мап по имени становится индексным, как у оригинала.
    Клип с ДРУГИМ числом костей (другая оснастка: `npc_f_h_ella`, навыковые клипы мобов `pspin01`/`omush02`/`ruby01`/`opara01`)
    не трогается: там чтение по индексу у оригинала даёт перемешанный скелет (дефект оригинала, по имени — непрерывно; проверено
    по положению костей в кадре 0). Возвращает число переименованных костей."""
    if jbb['B'] != ref['B']:
        return 0
    n = 0
    for rb, cb in zip(ref['bones'], jbb['bones']):
        if cb['name'] != rb['name']:
            cb['name'] = rb['name']
            n += 1
    return n


def fill_missing_bones_from_parent(ref, jbb):
    """★★ [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] 04.11.2026. Клип ДРУГОЙ оснастки (число костей ≠ референсу) может не содержать кость
    референса вовсе: навыковые клипы 11/13 `mon_m_3_omush02` (их играет вариант `omush05`, маска слотов 0x7) не имеют
    `bip01 spine2`, `bone10`, `bone07`, `bone08` (на них висит меш). У оригинала такой клип читается по индексу — скелет перемешан;
    у порта кость без дорожки стояла бы в позе привязки, оторванная от тела. Достраиваем: кость следует за РОДИТЕЛЕМ (родитель — по
    скелету референса, его дорожка — из этого клипа) с тем смещением к родителю, что в кадре 0 референса:
    `M_bone(k) = (M_bone_ref0 · M_parent_ref0⁻¹) · M_parent(k)` (строчные матрицы движка, v·M; кости клипа уже мировые).
    Кадры и время — от родителя. Кость, чьего родителя нет и не достроить, не трогается. Возвращает список достроенных имён."""
    S = conv()
    import numpy as np
    if jbb['B'] == ref['B']:
        return []
    nrm = lambda s: (s or '').strip().lower()
    have = {nrm(b['name']): b for b in jbb['bones']}
    refb = ref['bones']
    added = []
    progress = True
    while progress:
        progress = False
        for i, rb in enumerate(refb):
            if nrm(rb['name']) in have or not rb['keyCount']:
                continue
            p = rb['parent']
            if p < 0 or p >= len(refb):
                continue
            par = have.get(nrm(refb[p]['name']))
            if par is None or not par['keyCount'] or not refb[p]['keyCount']:
                continue
            Mb0 = S.eng_row_matrix(rb['quat'][0], rb['pos'][0], rb['scale'][0], rb['conv'])
            Mp0 = S.eng_row_matrix(refb[p]['quat'][0], refb[p]['pos'][0], refb[p]['scale'][0], refb[p]['conv'])
            O = Mb0 @ np.linalg.inv(Mp0)
            K = par['keyCount']
            pos = np.zeros((K, 3), np.float32); quat = np.zeros((K, 4), np.float32); scl = np.zeros((K, 3), np.float32)
            for k in range(K):
                M = O @ S.eng_row_matrix(par['quat'][k], par['pos'][k], par['scale'][k], par['conv'])
                R = M[:3, :3].copy()
                s = np.linalg.norm(R, axis=1)
                s[s < 1e-12] = 1e-12
                Rn = R / s[:, None]
                if np.linalg.det(Rn) < 0:
                    Rn[0] *= -1
                    s[0] *= -1
                pos[k] = M[3, :3]
                quat[k] = S.mat_to_quat(Rn)
                scl[k] = s
            nb = dict(name=rb['name'], parent=-1, keyCount=K, conv=1.0, t=par['t'].copy(), pos=pos, quat=quat, scale=scl)
            jbb['bones'].append(nb)
            have[nrm(rb['name'])] = nb
            added.append(rb['name'])
            progress = True
    jbb['B'] = len(jbb['bones'])
    return added


# ★★★ 07.11.2026 (пачка 14, «шипы» у крыс) РАЗНОПОЗНЫЕ МЕШИ НА ОДНОЙ КОСТИ. [ИСПРАВЛЕНИЕ]
#   Игра считает вершину только как Σ w·P·W(t) (P — бон-локальная позиция блока скина); модельная позиция pos0 в отрисовке скина
#   не участвует. Порт же подгоняет ОДНУ bind-матрицу кости по парам pos0→P всех мешей. Если автор скинил меши в разных позах
#   (mon_m_0_rrat01-a_00: руки Object01 подняты, туловище в другой позе, общая кость Bip01 Spine2), пары несовместимы:
#   фит усредняет, у рук остаются торчащие вверх звенья, туловище мнётся (отклонение от игры 128 ед при размере 284).
#   Лечение: bind кости — кандидат с наибольшим числом согласных вершин (фит по каждому мешу кости и кадр 0 клипа).
#   Вершины-отщепенцы: однокостной POSITION и NORMAL пересчитываются из P (приём fix_degenerate_binds, совпадает с игрой точно);
#   двукостной позицию менять нельзя — другая кость на неё уже опирается, поэтому меш получает КЛОН кости `<кость>__pN`: скелет
#   плоский, клон несёт те же треки во всех клипах и свою bind-матрицу. Откат сборки: ASDA_MIXED_BIND_FIX=0.
MIXED_BIND_TOL = 0.5     # допуск |[pos0|1]·IBM − P|, модельные единицы
MIXED_BIND_SCALE = 3.0   # кандидат отвергается, если его масштаб расходится с кадром 0 больше этого (взорванный фит)


def fix_mixed_binds(jbm, clips, IBM_game):
    """Согласовать bind костей с разнопозными мешами. clips — [(имя, jbb)]; IBM_game дополняется клонами.
    Возвращает [(кость, вершин пересчитано, клонов, вершин осталось несогласных)]."""
    if os.environ.get("ASDA_MIXED_BIND_FIX", "1") == "0":
        return []
    import numpy as np
    jbb = clips[0][1]
    pairs = {}   # кость → [(меш, вершина, сторона 0/1, P, однокостная)]
    for mi, me in enumerate(jbm["meshes"]):
        for k in range(len(me["pos0"])):
            w = float(me["w0"][k])
            j0, j1 = int(me["j0"][k]), int(me["j1"][k])
            if j0 == j1 or w >= 0.999:
                p = w * np.asarray(me["P0"][k], float) + (1.0 - w) * np.asarray(me["P1"][k], float) if j0 == j1 else me["P0"][k]
                pairs.setdefault(j0, []).append((mi, k, 0, p, True))
            else:
                pairs.setdefault(j0, []).append((mi, k, 0, me["P0"][k], False))
                pairs.setdefault(j1, []).append((mi, k, 1, me["P1"][k], False))

    def src_of(rows):
        """[pos0|1] вершин пар."""
        return np.array([np.append(jbm["meshes"][r[0]]["pos0"][r[1]], 1.0) for r in rows])

    def dst_of(rows):
        """P вершин пар."""
        return np.array([r[3] for r in rows], float)

    def errs(rows, M):
        """Отклонение |[pos0|1]·M − P| по парам."""
        return np.linalg.norm((src_of(rows) @ M)[:, :3] - dst_of(rows), axis=1)

    def sane(M, W0):
        """Bind без взрыва: определитель не ноль, масштаб близок к кадру 0."""
        if abs(np.linalg.det(M[:3, :3])) < 1e-7:
            return False
        if W0 is None:
            return True
        s = np.linalg.norm(np.linalg.inv(M)[:3, :3], axis=1)
        s0 = np.linalg.norm(W0[:3, :3], axis=1)
        return bool(np.all(s <= s0 * MIXED_BIND_SCALE) and np.all(s >= s0 / MIXED_BIND_SCALE))

    out = []
    for j, rows in sorted(pairs.items()):
        M0 = IBM_game[j] if j < len(IBM_game) else None
        if M0 is None or errs(rows, M0).max() <= MIXED_BIND_TOL:
            continue
        bn = jbb["bones"][j]
        W0 = conv().eng_row_matrix(bn["quat"][0], bn["pos"][0], bn["scale"][0], bn["conv"]) if bn["keyCount"] else None
        cands = [M0] if sane(M0, W0) else []
        for mi in sorted({r[0] for r in rows}):
            sub = [r for r in rows if r[0] == mi]
            A = src_of(sub)
            if len(sub) < 4 or np.linalg.matrix_rank(A, tol=1e-3) < 4:
                continue
            X, *_ = np.linalg.lstsq(A, dst_of(sub), rcond=None)
            M = np.eye(4); M[:, :3] = X
            if sane(M, W0):
                cands.append(M)
        if W0 is not None and abs(np.linalg.det(W0[:3, :3])) > 1e-7:
            cands.append(np.linalg.inv(W0))
        if not cands:
            continue
        best = max(cands, key=lambda M: int((errs(rows, M) <= MIXED_BIND_TOL).sum()))
        e = errs(rows, best)
        IBM_game[j] = best
        inv = np.linalg.inv(best)
        moved = left = clones = 0
        multi = {}   # меш → двукостные отщепенцы
        for r, d in zip(rows, e):
            if d <= MIXED_BIND_TOL:
                continue
            if not r[4]:
                multi.setdefault(r[0], []).append(r)
                continue
            me = jbm["meshes"][r[0]]
            me["pos0"][r[1]] = (np.append(np.asarray(r[3], float), 1.0) @ inv)[:3]
            n = np.asarray(me["nrm_bone"][r[1]], float) @ inv[:3, :3]   # нормаль: локаль кости → поза bind
            ln = np.linalg.norm(n)
            if np.isfinite(ln) and ln > 1e-6:
                me["nrm"][r[1]] = n / ln
            moved += 1
        def try_clone(mi, sub):
            """Клон кости для пар sub меша mi: True — все пары сошлись с допуском."""
            nonlocal clones
            A = src_of(sub); dst = dst_of(sub); X0 = best[:, :3]
            X = X0 + np.linalg.pinv(A) @ (dst - A @ X0)   # минимальная поправка к bind кости
            M = np.eye(4); M[:, :3] = X
            if not sane(M, W0) or errs(sub, M).max() > MIXED_BIND_TOL:
                return False
            clones += 1
            name = "%s__p%d" % (bn["name"], clones)
            nb = len(jbb["bones"])
            for _cn, cj in clips:   # клон в каждом клипе: кости клипов мапятся к референсу по имени
                cb = next((x for x in cj["bones"] if (x["name"] or "").strip().lower() == (bn["name"] or "").strip().lower()), bn)
                cj["bones"].append(dict(cb, name=name))
                cj["B"] = len(cj["bones"])
            IBM_game.append(M)
            me = jbm["meshes"][mi]
            for r in sub:
                me["j0" if r[2] == 0 else "j1"][r[1]] = nb
            return True

        for mi, sub in sorted(multi.items()):
            if try_clone(mi, sub):
                continue
            # Меш не сошёлся целиком — группы по второй кости вершины (пара костей автора).
            me = jbm["meshes"][mi]
            groups = {}
            for r in sub:
                groups.setdefault(int(me["j1" if r[2] == 0 else "j0"][r[1]]), []).append(r)
            for g in groups.values():
                if not try_clone(mi, g):
                    left += len(g)
        out.append((bn["name"], moved, clones, left))
    return out


def build_fixed_glb(jbm, clips, out, *, recover_bones=True, fix_degenerate=True, extra_anchors=(),
                    blend_mat_suffix=None, mat_names_fn=None, on_step=None, mixed_bind_fix=False):
    """Fixed-путь (закон [[skin-pipeline-fixed-path]]) над УЖЕ разобранной моделью: jbm — результат
    `T.parse_jbm_ext` (после своих правок вызывающего, например правки чужих текстур), clips — [(имя, jbb)],
    clips[0] — референс-клип. Шаги 1:1 с прежними копиями сборщиков:
      resolve_names → recover_binds → [fix_degenerate_binds] → trs_refit_binds → prune_unskinned(+extra_anchors)
      → build_fixed → validate.
    Дедуп №2 В1 Т10 (11.10.2026): была ручная сборка этой цепочки в asda_build_mob_fixed, asda_build_npc_fixed,
    asda_build_pet_models, asda_build_soulguard_models, fishing_export_assets.build_object и здесь же в build_skinned_glb.

    recover_bones  — True: recover_binds(jbm, B, bones) с регуляризацией по кадру 0 для неполноранговых костей
                     (петы, хранители, рыбалка, build_skinned_glb); False: recover_binds(jbm, B) — как у мобов/NPC
                     (★ расхождение копий сохранено 1:1, решение о выравнивании — отдельной правкой);
    fix_degenerate — чинить кости <4 фит-пар («мосты»-перепонки);
    extra_anchors  — дополнительные якоря prune_unskinned (рыбалка);
    blend_mat_suffix / mat_names_fn(jbm) — материалы build_fixed (mat_names считается ПОСЛЕ prune, как у копий);
    mixed_bind_fix — согласовать bind костей с разнопозными мешами (fix_mixed_binds);
    on_step(шаг, значение) — журнал вызывающего сразу после шага: ("degenerate", nfix), ("mixed", список), ("trs", ntrs) —
                     порядок строк в выводе сборщиков прежний.
    Возвращает dict: bind_err, max_vertex, biggest, bones_before, bones_after, clips (после prune), degenerate, trs,
    resid."""
    T = fixed()
    jbb0 = clips[0][1]
    T.resolve_names(jbm, jbb0)                   # палитра имён → кости референс-клипа
    if recover_bones:
        IBM_game, resid = T.recover_binds(jbm, jbb0['B'], jbb0['bones'])
    else:
        IBM_game, resid = T.recover_binds(jbm, jbb0['B'])
    nfix = 0
    if fix_degenerate:
        nfix = T.fix_degenerate_binds(jbm, jbb0, IBM_game)   # кости <4 фит-пар («мосты»-перепонки)
        if on_step: on_step("degenerate", nfix)
    if mixed_bind_fix:   # ★ 07.11.2026: только мобы и NPC (см. fix_mixed_binds)
        mixed = fix_mixed_binds(jbm, clips, IBM_game)
        if on_step: on_step("mixed", mixed)
    # bind в форму T·R·S при СТРОГОМ улучшении отклонения в UE (откат ASDA_BIND_TRS_REFIT=0)
    ntrs = T.trs_refit_binds(jbm, jbb0, IBM_game)
    if on_step: on_step("trs", ntrs)
    # prune_unskinned сохраняет якоря крепления (M_DUMMY, Flag_Link) — KEEP_ANCHORS (+ extra_anchors)
    clips, IBM_game, Bk, B0 = T.prune_unskinned(jbm, clips, IBM_game, extra_anchors=extra_anchors)
    kw = {}
    if blend_mat_suffix is not None: kw["blend_mat_suffix"] = blend_mat_suffix
    if mat_names_fn is not None: kw["mat_names"] = mat_names_fn(jbm)
    invBind, _bindG, W = T.build_fixed(jbm, clips, out, IBM_game, **kw)
    be, maxr, big = T.validate(jbm, clips[0][1], IBM_game, invBind, W)
    return dict(bind_err=be, max_vertex=maxr, biggest=big, bones_before=B0, bones_after=Bk, clips=clips,
                degenerate=nfix, trs=ntrs, resid=resid)


def build_skinned_glb(model, jbm_bytes, clips, out, *, mode=None, blend_mat_suffix=None,
                      bind_frames=None, fix_tex=True, log=print):
    """Собрать .glb скиннованной модели.

    model        — имя модели без расширения (для режима по умолчанию, правки текстур и логов);
    jbm_bytes    — содержимое .jbm;
    clips        — [(имя_анимации, jbb)] уже разобранные parse_jbb; clips[0] — референс-клип;
    out          — путь .glb;
    mode         — MODE_FIXED / MODE_LEGACY; None → mode_for(model);
    blend_mat_suffix — метка слотов полупрозрачных подсеток (asda_material_policy.BLEND_SUFFIX) или None;
    bind_frames  — {кость: кадр} bind-кадры СТАРОГО пути (шкатулка); fixed-путь их не использует —
                   истинный bind восстанавливается из POSITION0/POSITION1;
    fix_tex      — исправить дефект оригинала «чужая текстура в matTable» (asda_material_policy).
    Возвращает отчёт dict: mode, bind_err, bones_before, bones_after, clips, degenerate, trs, tex_fixes.
    Исключение RuntimeError — если .jbm не разобран или клипов нет.
    """
    import asda_material_policy as POL
    mode = mode or mode_for(model)
    if not clips:
        raise RuntimeError("ни один .jbb не распарсился")
    if mode == MODE_LEGACY:
        C = conv()
        jbm = C.parse_jbm(jbm_bytes)
        if not jbm:
            raise RuntimeError("parse_jbm вернул пусто")
        tex_fixes = POL.fix_cross_model_tex(model, jbm['meshes']) if fix_tex else []
        C.resolve_skin_by_name(jbm, clips[0][1])                   # линковка скина по референс-клипу
        kw = {}
        if bind_frames is not None: kw["bind_frames"] = bind_frames
        if blend_mat_suffix is not None: kw["blend_mat_suffix"] = blend_mat_suffix
        C.build(jbm, clips, out, **kw)
        B = clips[0][1]['B']
        return dict(mode=mode, bind_err=None, bones_before=B, bones_after=B, clips=len(clips),
                    degenerate=0, trs=[], tex_fixes=tex_fixes)
    # ─── fixed-путь (закон) — порядок шагов 1:1 с прежним build_one_fixed (asda_build_skinned_models) ───
    T = fixed()
    jbm = T.parse_jbm_ext(jbm_bytes)             # читает POSITION0/POSITION1 — истинный bind игры
    if not jbm:
        raise RuntimeError("parse_jbm_ext вернул пусто")
    tex_fixes = POL.fix_cross_model_tex(model, jbm['meshes']) if fix_tex else []

    def _step(name, val):
        """Журнал TRS-перефита — прежняя строка build_skinned_glb."""
        if name == "trs" and val and log:
            log("  %-28s bind TRS-перефит костей: %d (макс. было %.2f -> стало %.2f)"
                % (model, len(val), max(a for _n, a, _b in val), max(b for _n, _a, b in val)))
    # ★ blend_mat_suffix передаётся всегда (None = прежнее умолчание build_fixed)
    r = build_fixed_glb(jbm, clips, out, recover_bones=True, fix_degenerate=True, on_step=_step,
                        mat_names_fn=None, blend_mat_suffix=blend_mat_suffix)
    return dict(mode=mode, bind_err=r["bind_err"], bones_before=r["bones_before"], bones_after=r["bones_after"],
                clips=len(r["clips"]), degenerate=r["degenerate"], trs=r["trs"], tex_fixes=tex_fixes)
