# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_click_marker.py — сборка BP-заготовки МЕТКИ КЛИКА по земле (эффект «иду сюда»).
#
# ЭТАЛОН (RE/reports/entity/click_marker_effect_re.md):
#   Файл эффекта  : eff_0_Char_99_mtget01-a_00.jeb («mtget» = move target), 4 объекта, additive.
#   Спавн         : CEffectMgr_CreateEffect_F561F0@0xF561F0, type=1 (мировая точка) из трёх мест —
#                   Input_WorldClickHandler@0x968890 (клик по земле), CGame_HandleGroundClickAttack@0x996480,
#                   MainLoop_Tick_SoundUpdate@0x95EAB0 (ПОВТОР ~раз в секунду во время автобега).
#   Позиция       : точка клика, посаженная на террейн, + 20.0 по вертикали (константа flt_1396DA8).
#   Жизнь         : ~1.1 с (JEB duration), удаление АВТОМАТИЧЕСКОЕ (objectMgr_TickEffects), явного стопа нет;
#                   при новом клике старая метка НЕ гасится.
#   Состав        : круг-роза ветров — плоский квад В ПЛОСКОСТИ ЗЕМЛИ 50×50; две вертикальные стрелки
#                   (крест, rotX=90 / +rotY=90) на высоте ~25; искра-билборд 5×5 на 28.5. Без вращения/роста.
#   Условия       : ТОЛЬКО клик по земле (по сущности метки нет); на непроходимой клетке метки нет —
#                   вместо неё сообщение 1013 + error.wav.
#
# Скрипт делает: парс .jeb штатным парсером → создаёт Blueprint на базе AAsdaEffectActor с заполненными
#   эмиттерами → сохраняет как /Game/AsdaAssets/Effects/BP_ClickMarker. Дальше C++ просто спавнит этот BP.
#
# Запуск (headless): UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=asda_build_click_marker.py
# ★ Аудит №4 (волна 3): форма сборки BP переехала в `asda_fx_common` (общая с семью другими
#   канальными сборщиками); здесь остались ТОЛЬКО отличия метки клика.
import sys, os, json
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_fx_common as FXC
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

JEB_SRC   = _os.path.join(_P.EFFECT_MODEL_ORIG_DIR, "eff_0_char_99_mtget01-a_00.jeb")
OUT_PKG   = _U.G_EFFECTS
OUT_NAME  = "BP_ClickMarker"
EFF_NAME  = "eff_0_char_99_mtget01-a_00"


def log(m): unreal.log_warning("[clickfx] %s" % m)


def main():
    if not os.path.isfile(JEB_SRC):
        log("НЕТ файла эффекта: %s" % JEB_SRC); return

    # Парсер JEB + СБОРЩИК ЭМИТТЕРОВ — те же, что у размещения эффектов карт (модель верифицирована live.dmp).
    #   Без заполнения Emitters актор ничего не рисует (была первая версия — метка не появлялась).
    mods = FXC.import_fx_modules(log)
    if mods is None: return
    J, PE = mods
    # ★ ОБЩИЕ (не привязанные к карте) пути текстур/материалов: метка клика нужна на ВСЕХ картах.
    #   place_effects по умолчанию кладёт FX-ассеты в /Game/Map/<карта>/EffectTex|EffectMat — тогда на
    #   остальных 22 картах материала нет и метка невидима. Переопределяем на общую папку.
    FXC.point_pe_to(PE, OUT_PKG)
    jb = FXC.read_jeb(J, _P.EFFECT_MODEL_ORIG_DIR, EFF_NAME, log)
    if jb is None: return
    emitters = jb.get("emitters", [])
    blend_add = FXC.blend_mode(jb)
    log("эффект %s: эмиттеров %d, блендинг=%s" % (EFF_NAME, len(emitters), blend_add))

    # Blueprint на базе AAsdaEffectActor (эффект-актор порта, симуляция 1:1).
    #   Если BP уже есть — ПЕРЕИСПОЛЬЗУЕМ (удаление не проходит, когда ассет загружен в память).
    full = "%s/%s" % (OUT_PKG, OUT_NAME)
    bp, cdo = FXC.ensure_effect_bp(OUT_NAME, OUT_PKG, log)
    if bp is None: return
    cdo.set_editor_property("effect_name", EFF_NAME)
    # ★ Знак поворота узлов: у метки вертикальные стрелки (rotX=90) — с дефолтным знаком они уходят
    #   ПОД ЗЕМЛЮ (проверено расчётом: высота −45…−5 вместо +5…+45 по реверсу).
    cdo.set_editor_property("invert_node_rot", True)
    # Инверсия знака ставит стрелки на верную высоту, но переворачивает их — отражаем по вертикали.
    cdo.set_editor_property("flip_quad_v", True)
    # ЭМИТТЕРЫ: собираются тем же build_emitter, что и для эффектов карт (материалы/UV/жизнь/размеры).
    ems = []
    for em in emitters:
        try:
            # ★ kf_seed_nodes — этап 3 раскатки (см. `Reports/item_fx_ring_motion.md`):
            #   у маркера 4 квада с keyframe-ключами, им возвращается потерянный КОНЕЦ-узел трека.
            #   Откат: `ASDA_FX_NO_KF_SEED=1` + пересборка.
            # ★★★ etalon_end (2026-08-14, `fx_scale_timing_audit.md` §4): у всех 4 квадов маркера
            #   на диске `endMode = 0` (одна вспышка), а ветка growQuad форсила `end_mode = 1` —
            #   маркер клика перезапускался каждую секунду. Откат: `ASDA_FX_NO_ETALON_ENDMODE=1`.
            ems.append(PE.build_emitter(em, blend_add,
                                        kf_seed_nodes=FXC.kf_seed_enabled(),
                                        etalon_end=FXC.etalon_end_enabled()))
        except Exception as e:
            log("  эмиттер %s пропущен: %s" % (em.get("name", "?"), e))
    # ★ ЧЕСТНО ПРО СОХРАНЕНИЕ (2026-08-21, см. шапку `asda_fx_common.finish_effect_bp`):
    #   занятый другим процессом `.uasset` НЕ сохраняется, а строка «создан…» печаталась всё равно.
    if not FXC.finish_effect_bp(bp, cdo, ems):
        log("НЕ СОХРАНЁН %s: файл занят другим процессом (закрой редактор UE и повтори)" % full)
        return
    log("создан %s: эмиттеров записано %d" % (full, len(ems)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
