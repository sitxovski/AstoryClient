# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_skinned_models.py — БАТЧ: все скиннутые модели (мобы/персонажи/транспорт) из архивов → .glb
# (1 SkeletalMesh + ВСЕ доступные анимации-клипы каждой модели). Привязка скин-костей ПО ИМЕНИ.
# Запуск (вне UE): python asda_build_skinned_models.py [mobs|npc|chars|vehicle|all]
#
# Источник: MobModel.bnz/MobMotion.baz, CharModel.bnz/CharMotion.baz (стандартный ZIP).
# Клипы модели "X_00.jbm" = "X_00_N.jbb" (N — индекс анимации).
# Выход: RawAssets/Models/Skeletal/<Mobs|Characters>/<model>.glb
#        RawAssets/Models/Vehicle/<model>.glb                       (транспорт/маунты)
import sys, os, re, glob, zipfile, importlib.util
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
from collections import defaultdict

HERE=os.path.dirname(os.path.abspath(__file__))
OUT_ROOT=os.path.join(HERE,"..","RawAssets","Models","Skeletal")
GAME=_P.DATA   # ОСНОВНОЙ клиент
# (модель-архив, мотив-архив, подпапка выхода)
SRCS={
 "Mobs":      (GAME+r"\Mob\Model\MobModel.bnz",   GAME+r"\Mob\Motion\MobMotion.baz"),
 "NPC":       (GAME+r"\NPC\Model\NpcModel.bnz",   GAME+r"\NPC\Motion\NpcMotion.baz"),
 "Characters":(GAME+r"\Character\Model\CharModel.bnz", GAME+r"\Character\Motion\CharMotion.baz"),
 # ТРАНСПОРТ (маунты): 52 × itm1_n_96_vehicle_00NN.jbm + по 3 клипа _000/_001/_002.jbb.
 # Слоты (mount_system_re.md §2.2 п.9): 000=idle, 001=не выбирается, 002=движение.
 "Vehicle":   (GAME+r"\Character\Model\CharModel.bnz", GAME+r"\Character\Motion\CharMotion.baz"),
 # ★ ФЛАГ КЛАНА НА ТРАНСПОРТЕ (clan_flag_mount_re.md): small_flag.jbm + small_flag000/001/002.jbb.
 #   Тип 1 таблицы CBattleFlag_Construct@0xFAF3D0. Клипы БЕЗ подчёркивания перед номером!
 "ClanFlag":  (GAME+r"\Character\Model\CharModel.bnz", GAME+r"\Character\Motion\CharMotion.baz"),
 # ★ АВТОМАТЫ ПРЕМИУМ-ГАЧИ (gacha_fx_re.md): md_gachya_{a..e}_00.jbm + по 5 клипов _00.._04.jbb.
 #   Слоты (gachaScene_UpdateChildAnims@0xB6B0B0): 1=покой, 2=прокрутка, 3=выдача, 4=вскрытие;
 #   слот 0 (_00.jbb) в оригинале НЕ загружается никогда — дефект оригинала, клип забираем всё равно.
 "Gachya":    (GAME+r"\Character\Model\CharModel.bnz", GAME+r"\Character\Motion\CharMotion.baz"),
 # ★ 23.10.2026 НОВЫЕ МАУНТЫ ДОНОРА AsdaFableClassic (Reports/fable_mounts/FABLE_MOUNTS.md): 28 моделей
 #   itm1_n_96_vehicle_0053…0080, которых нет в основной сборке. Архивы — того же формата; общие 0000…0052
 #   фильтр не пропускает (у донора 4 меша и 5 клипов из них отличаются, прежние .glb не трогаем).
 "VehicleFable": (_P.DONORS+r"\AsdaFableClassic\Data\Character\Model\CharModel.bnz",
                  _P.DONORS+r"\AsdaFableClassic\Data\Character\Motion\CharMotion.baz"),
}
# фильтр имён моделей по группе (None = все модели архива)
NAME_FILTER={ "Vehicle":  re.compile(r"^itm1_n_\d+_vehicle_\d+$", re.I),
              "ClanFlag": re.compile(r"^small_flag$", re.I),
              "Gachya":   re.compile(r"^md_gachya_[a-e]_00$", re.I),
              "VehicleFable": re.compile(r"^itm1_n_96_vehicle_00(5[3-9]|[67]\d|80)$", re.I) }
# группы, чей вывод лежит НЕ в Skeletal/<grp>, а в отдельной папке RawAssets/Models/<...>
OUT_OVERRIDE={ "Vehicle":  os.path.join(HERE,"..","RawAssets","Models","Vehicle"),
               "ClanFlag": os.path.join(HERE,"..","RawAssets","Models","ClanFlag"),
               "Gachya":   os.path.join(HERE,"..","RawAssets","Models","Gachya"),
               "VehicleFable": os.path.join(HERE,"..","RawAssets","Models","Vehicle") }   # та же папка, что у Vehicle
# транспорт лежит в CharModel.bnz — исключаем его из группы Characters (собирается отдельной группой Vehicle)
NAME_EXCLUDE={ "Characters": re.compile(r"^itm1_n_\d+_vehicle_\d+$", re.I) }
# ★ Разбор имени КЛИПА в базу+индекс. По умолчанию "<модель>_<N>"; у флага клана — "<модель><NNN>"
#   (эталон CBattleFlag_Construct: "small_flag000.jbb" .. "small_flag002.jbb", без разделителя).
CLIP_RE={ "ClanFlag": re.compile(r"^(small_flag)(\d{3})$", re.I),
          "Gachya":   re.compile(r"^(md_gachya_[a-e])_(\d+)$", re.I) }
CLIP_RE_DEFAULT=re.compile(r"(.+)_(\d+)$")
# ★★★ ЧТО ПОЧИНИЛО ГАЧУ. clipmap строится по базе из имени КЛИПА, а сопоставляется с именем МОДЕЛИ.
#   У автоматов гачи модель зовётся `md_gachya_a_00`, а клипы `md_gachya_a_01..04` — база из клипа
#   выходит `md_gachya_a`, ключ не совпадает с моделью, и все 5 моделей молча уходили в `skip`
#   БЕЗ строки в лог. Отсюда суффикс базы: он дописывается к ключу clipmap, чтобы тот совпал с
#   именем модели. Для остальных групп пусто — поведение не меняется.
CLIP_BASE_SUFFIX={ "Gachya": "_00" }
# ★★★ ОТДЕЛЬНЫЙ СЛОТ ДЛЯ ПОЛУПРОЗРАЧНЫХ ПОДСЕТОК (рендер-флаг .jbm mr+0x004 бит 0x1, см. asda_jbm.py).
#   У автомата гачи флаг стоит у купола `Bowl` и у двух квадов тени. Купол носит ТУ ЖЕ текстуру, что и
#   корпус (`md_Gachya_x_00`), поэтому без разного имени материала UE сливает их в ОДИН слот меша и
#   сделать купол сквозным нельзя — а он обязан быть сквозным: девять шаров `Object01…Object10`
#   лежат целиком внутри его габарита. Суффикс включаем только тем группам, чей импортёр знает,
#   что делать с новым слотом (сейчас Gachya → asda_import_gachya_models.py).
BLEND_MAT_SUFFIX={ "Gachya": "_blend" }

# ★★★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛЬНЫХ ДАННЫХ: «четвёртый автомат гачи носит текстуру пятого».
#   Правило и весь разбор — в ОБЩЕМ модуле `asda_material_policy.fix_cross_model_tex`
#   (отчёт `Reports/gacha_model_d_texture.md`). Здесь только вызов: те же `.jbm`
#   конвертирует и `asda_build_models.py` (статик-версии в `RawAssets/Models/Char`), и починить
#   обе ветви надо ОДИНАКОВО, иначе статик и скелеталка разъедутся по раскраске.
#   Правка живёт в СБОРЩИКЕ ⇒ воспроизводится при каждом переэкспорте.
import asda_material_policy as _POL
fix_cross_model_tex = _POL.fix_cross_model_tex
import asda_skin_build as SB   # ★ единственный вход сборки скина (волна 2, E10)
import asda_login_guard as _LG  # ★ защита моделей логина (ревизия волны 2, 14.09.2026)

# ★★★ .glb ЛОГИНА В ОБЩЕЙ ПАПКЕ Characters (ревизия 14.09.2026). Эти два файла ведут точечные экспортёры
#   (свои bind_frames и `_blend`); группа chars/all собрала бы ДРУГОЙ файл по тому же пути и молча перезаписала
#   шкатулку/задник логина. Без ASDA_ALLOW_LOGIN_WRITE=1 — пропуск (см. [[owner-login-light-protected]]).
LOGIN_OWNED_GLB = {"cre_creation01-a_00": "export_casket.py", "cre_creback-a_00": "export_creback.py"}

# ★ ФИКС СКИННИНГА (2026-07-30). Группы, которые собираются ИСПРАВЛЕННЫМ двух-позиционным путём
#   (asda_test_export_one: parse_jbm_ext → recover_binds → fix_degenerate_binds → prune_unskinned → build_fixed).
#   Причина: .jbm (meshType 0x80) хранит ИСТИННЫЙ bind — по ДВЕ бон-локальные позиции POSITION0/POSITION1
#   на вершину (шейдер g_pSkinShaderA@0x15C39B0: posWorld = (POSITION0·Bone0)·w0 + (POSITION1·Bone1)·w1),
#   а старый conv.build() приближал bind кадром 0 клипа → вершины уезжали. Замер по готовым .glb
#   (Tools/_probe_skin_err.py): у ТРАНСПОРТА 12 из 52 моделей давали >5 ед, худшие
#   itm1_n_96_vehicle_0029/0031 = 98.4 ед при размере модели 167; у small_flag = 12.2 ед.
#   Тот же баг чинили у NPC/мобов/петов ([[npc-skin-twoposition-bind]], pet_system_re.md §8).
#   Мобы/NPC/персонажи собираются своими *_fixed-скриптами — эти группы здесь не трогаем.
FIXED_GROUPS={"Vehicle","ClanFlag","Gachya","VehicleFable"}

def load_conv():
    spec=importlib.util.spec_from_file_location("conv",os.path.join(HERE,"asda_skinned_to_gltf.py"))
    m=importlib.util.module_from_spec(spec); sys.modules["conv"]=m; spec.loader.exec_module(m); return m

def load_fixed():
    """Модуль ИСПРАВЛЕННОГО скин-пайплайна (тот же, что у NPC/мобов/петов) — через единый вход."""
    return SB.fixed()

def build_one_fixed(T, conv, zmodel, zmot, jm, clips_meta, out, blend_mat_suffix=None,
                    out_sub=None, model=None):
    """Сборка ОДНОЙ модели исправленным путём — через ЕДИНЫЙ вход asda_skin_build (волна 2, E10).
    Возвращает (bind_err, костей_было, костей_стало, клипов, вырожденных, список_правок_текстур).
    Аргумент T оставлен для совместимости вызова: шаги пути живут в asda_skin_build.build_skinned_glb
    (parse_jbm_ext → правка чужих текстур → resolve_names → recover_binds → fix_degenerate_binds →
    trs_refit_binds → prune_unskinned → build_fixed → validate)."""
    clips=[]
    for N,mn in clips_meta:
        jbb=conv.parse_jbb(zmot.read(mn))
        if jbb: clips.append(("anim_%02d"%N, jbb))
    rep=SB.build_skinned_glb(model, zmodel.read(jm), clips, out, mode=SB.MODE_FIXED,
                             blend_mat_suffix=blend_mat_suffix)
    return (rep["bind_err"], rep["bones_before"], rep["bones_after"], rep["clips"],
            rep["degenerate"], rep["tex_fixes"])

def find_archive(name):
    fs=glob.glob(r"@@ASTORY_DEV_ROOT@@\**\%s"%name, recursive=True)
    return fs[0] if fs else None

def clip_index(clip_base, model):
    m=re.match(re.escape(model)+r"_(\d+)$", clip_base)
    return int(m.group(1)) if m else None

def build_group(model_arc, motion_arc, out_sub, conv, T=None):
    zmodel=zipfile.ZipFile(model_arc); zmot=zipfile.ZipFile(motion_arc)
    models=[n for n in zmodel.namelist() if n.lower().endswith(".jbm")]
    motions=[n for n in zmot.namelist() if n.lower().endswith(".jbb")]
    keep=NAME_FILTER.get(out_sub); drop=NAME_EXCLUDE.get(out_sub)
    if keep: models=[n for n in models if keep.match(n[:-4])]
    if drop: models=[n for n in models if not drop.match(n[:-4])]
    # индекс клипов по базовому имени модели
    clipmap=defaultdict(list)   # model_base -> [(N, jbbname)]
    clip_re=CLIP_RE.get(out_sub, CLIP_RE_DEFAULT)
    for mn in motions:
        base=mn[:-4]
        m=clip_re.match(base)
        if m: clipmap[m.group(1)+CLIP_BASE_SUFFIX.get(out_sub,"")].append((int(m.group(2)), mn))
    outdir=OUT_OVERRIDE.get(out_sub) or os.path.join(OUT_ROOT, out_sub)
    outdir=os.path.abspath(outdir); os.makedirs(outdir, exist_ok=True)
    fixed = out_sub in FIXED_GROUPS and T is not None      # ★ исправленный двух-позиционный bind
    ok=skip=fail=0; worst=[]; nfixtex=[0]                  # nfixtex — счётчик правок чужих текстур
    for jm in sorted(models):
        model=jm[:-4]
        clips_meta=sorted(clipmap.get(model, []))
        if not clips_meta:
            skip+=1; continue   # без клипов (вариант/статика) — отдельный проход позже
        if model in LOGIN_OWNED_GLB and not _LG.login_write_allowed():
            print("  ПРОПУСК %s: .glb логина ведёт %s (перезапись — ASDA_ALLOW_LOGIN_WRITE=1)" % (model, LOGIN_OWNED_GLB[model]))
            skip+=1; continue
        try:
            out=os.path.join(outdir, model+".glb")
            if fixed:
                be,B0,Bk,nc,nfix,tex_fixes=build_one_fixed(T, conv, zmodel, zmot, jm, clips_meta, out,
                                                 blend_mat_suffix=BLEND_MAT_SUFFIX.get(out_sub),
                                                 out_sub=out_sub, model=model)
                ok+=1
                print("  %-28s кости %d→%d клипов=%d bind_err=%.4f%s"
                      % (model, B0, Bk, nc, be, ("  вырожденных вершин=%d"%nfix) if nfix else ""))
                # ★ дефект оригинала исправлен — печатаем ПОИМЁННО, чтобы правка не была молчаливой
                for node, was, now in tex_fixes:
                    print("     ★ ИСПРАВЛЕН ДЕФЕКТ ОРИГИНАЛА: подсетка %-10s текстура %s → %s"
                          % (node, was, now))
                    nfixtex[0]+=1
                if be>1.0: worst.append((model, be))
                continue
            # ★★★ ВОЛНА 2 (14.09.2026, дефект E10): группы вне FIXED_GROUPS (Mobs/NPC/Characters) раньше шли
            #   СТАРЫМ conv.build() в обход закона [[skin-pipeline-fixed-path]]. Теперь — единый вход
            #   asda_skin_build: fixed-путь для всех, КРОМЕ моделей логина (шкатулка/задник: режим по
            #   умолчанию MODE_LEGACY, см. asda_skin_build.mode_for и `Reports/dedup_audit/wave2_import.md`).
            #   ★ NPC и мобов в проект собирают asda_build_npc_fixed.py / asda_build_mob_fixed.py
            #   (RawAssets/Models/Skeletal/NPC и /Mob); запись группы NPC сюда закрыта FOREIGN_OUT (см. main).
            clips=[]
            for N,mn in clips_meta:
                jbb=conv.parse_jbb(zmot.read(mn))
                if jbb: clips.append(("anim_%02d"%N, jbb))
            if not clips: skip+=1; continue
            mode=SB.mode_for(model)
            rep=SB.build_skinned_glb(model, zmodel.read(jm), clips, out, mode=mode)
            # ★ исправление дефекта оригинала «чужая текстура в matTable» — на ОБОИХ путях (внутри входа)
            for node, was, now in rep["tex_fixes"]:
                print("     ★ ИСПРАВЛЕН ДЕФЕКТ ОРИГИНАЛА: подсетка %-10s текстура %s → %s"
                      % (node, was, now))
                nfixtex[0]+=1
            if mode==SB.MODE_FIXED and rep["bind_err"] is not None and rep["bind_err"]>1.0:
                worst.append((model, rep["bind_err"]))
            ok+=1
            if ok%20==0: print("  ...%d собрано"%ok)
        except Exception as e:
            fail+=1; print("  FAIL %s: %s"%(model, e))
    if worst:
        print("  ⚠️ bind-реконструкция >1.0 (проверить визуально):")
        for m,e in sorted(worst, key=lambda x:-x[1]): print("     %-28s err=%.2f"%(m,e))
    print("[%s] OK=%d SKIP(без клипов)=%d FAIL=%d исправлено_чужих_текстур=%d → %s"
          %(out_sub, ok, skip, fail, nfixtex[0], outdir))
    return ok

# ★★★ ЧУЖИЕ ПАПКИ ВЫВОДА (решение координатора волны 2, 14.09.2026). RawAssets/Models/Skeletal/NPC ведёт
#   asda_build_npc_fixed.py (fixed-путь с пер-меш материалами NPC, сборка 11.09). Группа NPC этого батча
#   пишет в ТУ ЖЕ папку и молча перезаписала бы те .glb своей сборкой. Без явного разрешения — стоп.
#   Разрешить: аргумент --allow-foreign-out или ASDA_SKINNED_ALLOW_FOREIGN_OUT=1.
FOREIGN_OUT = {"NPC": "asda_build_npc_fixed.py"}
ALLOW_FOREIGN_ARG = "--allow-foreign-out"
ALLOW_FOREIGN_ENV = "ASDA_SKINNED_ALLOW_FOREIGN_OUT"


def foreign_out_allowed(argv=None):
    """True, если запись в папки других сборщиков разрешена явно (аргумент или переменная окружения)."""
    argv = sys.argv if argv is None else argv
    return ALLOW_FOREIGN_ARG in argv or os.environ.get(ALLOW_FOREIGN_ENV, "0") == "1"

def main():
    args=[a for a in sys.argv[1:] if a!=ALLOW_FOREIGN_ARG]
    what=args[0] if args else "npc"
    conv=load_conv(); T=load_fixed()
    sel={"mobs":["Mobs"],"npc":["NPC"],"chars":["Characters"],"vehicle":["Vehicle"],
         "clanflag":["ClanFlag"],
         "gachya":["Gachya"],
         "vehiclefable":["VehicleFable"],   # ★ 23.10: новые маунты донора AsdaFableClassic (в "all" не входит)
         "all":["Mobs","NPC","Characters","Vehicle","ClanFlag","Gachya"]}.get(what,[what])
    for grp in sel:
        if grp not in SRCS: print("неизвестная группа:",grp); continue
        if grp in FOREIGN_OUT and not foreign_out_allowed():
            print("СТОП [%s]: папку %s ведёт %s — эта группа её перезаписала бы. Нужен свой .glb — запускайте %s; "
                  "осознанная перезапись — аргумент %s или %s=1."
                  % (grp, os.path.abspath(OUT_OVERRIDE.get(grp) or os.path.join(OUT_ROOT, grp)),
                     FOREIGN_OUT[grp], FOREIGN_OUT[grp], ALLOW_FOREIGN_ARG, ALLOW_FOREIGN_ENV))
            continue
        md,mo=SRCS[grp]
        if os.path.exists(md) and os.path.exists(mo): build_group(md, mo, grp, conv, T)
        else: print("нет архивов для %s: %s | %s"%(grp,md,mo))

if __name__=="__main__":
    main()
