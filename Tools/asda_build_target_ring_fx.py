# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_target_ring_fx.py — BP-заготовки КОЛЬЦА ПОД ВЫБРАННОЙ ЦЕЛЬЮ (BP_Fx_friendlyselect /
#   BP_Fx_unfriendlyselect).
#
# ★★★ ЭТО ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА — ПО ПРЯМОМУ РЕШЕНИЮ ВЛАДЕЛЬЦА (2026-08-21).
#   В НАШЕЙ сборке (`AsdaStory.exe`, 13 344 768 б) кольца выбора НЕТ: строк `FriendlySelect` /
#   `UnfriendlySelect` в EXE 0 вхождений, файлов в нашем `EffectModel.bnz` (2406 записей) нет,
#   и ни одна из пяти функций «квад по террейну» не смотрит на `g_nTargetUserIdx`
#   (доказательство двумя методами — `Reports/npc_visual_fix.md` §2.1).
#   Кольцо существует у КЛИЕНТОВ-БЛИЗНЕЦОВ (`Asda2_WoD.exe` / `GlobalDump.exe`, по 2 и 1 вхождению
#   строк). Владелец играл именно в ту сборку и решил кольцо портировать и включить по умолчанию.
#   Разбор и мотивировка — `Reports/npc_target_ring.md`.
#
# ИСТОЧНИК ФАЙЛОВ. Владелец назвал `@@ASTORY_DEV_ROOT@@\Donors\AsdaGlobal\data\Effect\Model\EffectModel.bnz`, но
#   этот архив НЕ ZIP (сигнатура `MSPK` — контейнер сборки под Themida) и нашим кодом не читается.
#   Тот же набор лежит ZIP-архивом у второго близнеца — `NEW\WorldOfDreams\...\EffectModel.bnz`
#   (3166 записей); оттуда и берём. Извлечение делает САМ этот скрипт (шаг 1), чтобы прогон был
#   воспроизводим, а заимствованные файлы лежали в отдельном каталоге с говорящим именем.
#
# ЧТО ВНУТРИ (разбор нашим `asda_jeb_parse`): у обоих файлов РОВНО ОДИН эмиттер типа 0x0B
#   (плоский квад в плоскости ЗЕМЛИ, узел `SowelFloor`), текстура `eff_1_char_03_sfarw01-c_00.jpg`
#   (она ЕСТЬ в нашем `Texture.btz` и уже импортирована), бленд аддитивный, life 0.2 с,
#   полу-экстент 150→50, alpha 1.0→1.0 (тройка = старт/КОНЕЦ/разброс; выцветания НЕТ),
#   цвет 1,1,1 → (0,1,0) зелёный / (1,0,0) красный.
#   Файлы отличаются ТОЛЬКО конечным цветом.
#
# ПОЧЕМУ СОБСТВЕННОГО ПАЙПЛАЙНА НЕТ. Сборка идёт через `asda_build_char_fx.build_one` — ту же
#   общую точку, что у крыльев/соулмейта/навыков/состояний ([[port-shared-points]]). Отличие
#   канала ровно одно: `SRC_DIR` указывает на каталог заимствованных `.jeb`, а не на распакованное
#   дерево нашей сборки. Подмена локальная (только в этом процессе) ⇒ чужие каналы не меняются.
#
# ★★★ endMode — С ДИСКА (канал ВХОДИТ в гейт `ONE_SHOT` через `FXC.extend_channel_gates`).
#   ИСПРАВЛЕНО 2026-08-21 по замечанию владельца «кольца не разово проигрываются, а бесконечно
#   пульсируют». Прежняя шапка утверждала, что пульс — эталонное поведение; это НЕВЕРНО, и обе
#   половины утверждения опровергнуты сырыми данными и кодом близнеца (полный разбор —
#   `Reports/npc_target_ring_fix.md`):
#     * у ЭМИТТЕРА на диске `endMode = 0` (оба файла, оба по 456 б) — в НАШЕМ рантайме это
#       «стоп после одной проходки», у близнеца — «замереть на конечных значениях» (см. ниже);
#     * у ЗАГОЛОВКА `endMode = 3`, и это НЕ «луп»: машина холдера близнеца
#       (`Asda2.exe` `CEffect::Update@0x012C1FD0`, switch по `this[0x29C]`, таблица @0x12C21EC)
#       для case 3 делает РОВНО НИЧЕГО (`0x12C215F: return 1`), гасит только case 0/2,
#       перезапускает только case 1;
#     * сам близнец перезапускает кольцо ЯВНО и ТОЛЬКО на смене цели
#       (`0x00A819E0`: цель изменилась → `SetEnable(true)` + vtbl[1]-`Restart`), а в кадровой
#       ветке (`0x00A7CB50`) есть лишь `Update`/`SetPos`/`SetEnable` — перезапуска нет.
#   Без гейта ветка `growQuad` в `build_emitter` ставит `end_mode = 1` БЕЗУСЛОВНО («закон вечных
#   эффектов КАРТ», у которых перезапуск делает холдер) — это и был источник вечного пульса.
#   Гасит кольцо рантайм (снятие выбора/смерть/уход цели); срок жизни эффекта теперь ограничивает
#   ЕДИНСТВЕННУЮ проходку квада (0.0672 с задержки + 0.20 с жизни).
#
# ★★★ ПОВЕРХ ЭТОГО — УДЕРЖАНИЕ ПОСЛЕДНЕГО КАДРА (2026-08-21, ВТОРОЕ замечание владельца:
#   «оно не должно просто отыграть и исчезнуть, а на определённом диаметре остановиться и быть
#   видимым — собственно это и называется выделение»). Строка в `main()` ставит каналу
#   `HOLD_LAST_FRAME`, и `end_mode` становится 3 = ФРИЗ одиночного квада на конечном узле.
#
#   ★★★ И ЭТО НЕ ОТСТУПЛЕНИЕ, А ВОССТАНОВЛЕНИЕ (перепроверка по близнецу, `npc_target_ring_fix2.md`
#   §2.2). У близнеца кольцо ОСТАЁТСЯ на земле: `endMode = 0` его эмиттера
#   (`xp::xpCSpriteKeyFrameAni`, анимация `0x132EB10`, case 0 @`0x132EE75`) означает «подставить
#   КОНЕЧНЫЕ значения каналов и заморозить» (`node[0x114] = −1.0`), после чего отрисовка
#   `vtbl[16]@0x13420A0` продолжает вызываться БЕЗУСЛОВНО каждый кадр. То есть под целью висит
#   квад 50×50, alpha 1.0, конечного цвета — ровно то, что просит владелец.
#   Строка выше про «endMode 0 = стоп после одной проходки» описывает трактовку НАШЕГО рантайма,
#   а не близнеца; у нас тот же смысл даёт номер 3, поэтому канал ставит 3.
#   ДАННЫЕ НЕ ПРАВЯТСЯ: alpha у обоих файлов = [старт 1.0, КОНЕЦ 1.0, разброс 0.0] — выцветания
#   в них нет (прошлые отчёты читали третий элемент тройки как конечное значение).
#   Гейт `ONE_SHOT` ОСТАВЛЕН: он показывает дисковое значение и снимает «закон вечных эффектов
#   КАРТ» (`growQuad` → 1); удержание переопределяет его последним шагом.
#   ASDA_TARGETRING_NO_HOLD=1 — вернуть поведение «отыграл и исчез» (это НАШЕ прежнее поведение,
#     а не поведение близнеца).
#   ASDA_TARGETRING_HOLD_ALPHA=<0..1> — подобрать яркость удержанного кадра.
#
# Запуск: Tools\run_headless.bat Tools\asda_build_target_ring_fx.py
#   ASDA_TARGETRING_ONLY="имя" — построить только один эффект (точечный ремонт).
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
import os
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import asda_fx_common as FXC
import asda_paths as _P

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.abspath(os.path.join(HERE, ".."))

# Каталог заимствованных .jeb (НЕ смешивать с распакованным деревом нашей сборки).
BORROWED_DIR = os.path.join(PROJ, "RawAssets", "Fx", "Borrowed_WoD")
# Архив-донор: обычный ZIP у сборки WorldOfDreams (у AsdaGlobal контейнер MSPK — см. шапку).
DONOR_BNZ = os.environ.get(
    "ASDA_TARGETRING_DONOR",
    os.path.join(_P.ROOT, "Donors", "WorldOfDreams", "Data", "Effect", "Model", "EffectModel.bnz"))

# Имена эффектов = имена записей в архиве-доноре (нижний регистр, как везде в канале).
JEBS = ["friendlyselect", "unfriendlyselect"]


def log(m):
    try:
        import unreal
        unreal.log_warning("[кольцо-цели] %s" % m)
    except Exception:
        print("[кольцо-цели] %s" % m)


def extract_borrowed():
    """Шаг 1: достать `.jeb` из архива-донора в `RawAssets/Fx/Borrowed_WoD`. Возвращает список имён.

    Уже лежащие файлы НЕ перезаписываются молча: если размер совпал — пропускаем, иначе кладём
      заново и пишем в лог (чтобы подмена донора была видна, а не «само собой поменялось»).
    """
    if not os.path.isfile(DONOR_BNZ):
        log("НЕТ архива-донора: %s" % DONOR_BNZ)
        return []
    if not os.path.isdir(BORROWED_DIR):
        os.makedirs(BORROWED_DIR)
    got = []
    try:
        z = zipfile.ZipFile(DONOR_BNZ)
    except Exception as e:
        log("архив-донор не читается как ZIP (%s): %s" % (DONOR_BNZ, e))
        return []
    names = {n.lower(): n for n in z.namelist()}
    for eff in JEBS:
        src = names.get(eff + ".jeb")
        if src is None:
            log("в доноре НЕТ записи %s.jeb" % eff)
            continue
        data = z.read(src)
        dst = os.path.join(BORROWED_DIR, eff + ".jeb")
        if os.path.isfile(dst) and os.path.getsize(dst) == len(data):
            got.append(eff)
            continue
        with open(dst, "wb") as f:
            f.write(data)
        log("извлечён %s.jeb (%d б) ← %s" % (eff, len(data), DONOR_BNZ))
        got.append(eff)
    return got


def main():
    have = extract_borrowed()
    if not have:
        log("нечего строить: заимствованные .jeb не получены")
        return

    mods = FXC.import_fx_modules(log, with_char_fx=True)
    if mods is None:
        return
    J, PE, CFX = mods
    FXC.point_pe_to(PE, CFX.OUT_PKG)

    # ★ ЕДИНСТВЕННОЕ отличие канала: источник .jeb. Подмена локальна для процесса сборщика —
    #   каналы крыльев/соулмейта/навыков/состояний читают свой каталог и не затрагиваются.
    old_src = CFX.SRC_DIR
    CFX.SRC_DIR = BORROWED_DIR
    try:
        jebs = FXC.only_filter(have, "ASDA_TARGETRING_ONLY")
        # ★★★ endMode «как на диске» — ЕДИНОЙ общей точкой, как у канала состояний/навыков/лута.
        #   Расширение множеств `ONE_SHOT`/`KF_SEED_FX` живёт ТОЛЬКО в процессе этого сборщика,
        #   поэтому крылья/соулмейт/хранитель/навыки/карты измениться не могут. Пересечений имён
        #   с чужими каналами нет: `friendlyselect`/`unfriendlyselect` есть только у близнеца.
        #   `KF_SEED_FX` здесь — no-op по построению: у обоих эмиттеров `trackNodes` пуст, а
        #   сид-узлы строятся только внутри ветки `if tn:` (`asda_place_effects`, стр. 496/510).
        FXC.extend_channel_gates(CFX, jebs)
        # ★★★ КОЛЬЦО ОСТАЁТСЯ ВИДИМЫМ — ОТСТУПЛЕНИЕ ОТ ДАННЫХ ПО РЕШЕНИЮ ВЛАДЕЛЬЦА (2026-08-21):
        #   «оно не должно просто отыграть и исчезнуть, а на определённом диаметре остановиться и
        #   быть видимым — собственно это и называется выделение».
        #   Механика (`asda_fx_common.hold_last_frame`): `end_mode = 3` — у ОДИНОЧНОГО КВАДА это
        #   штатный ФРИЗ рантайма на конечном узле (`AAsdaEffectActor::Tick`, `case 3` для
        #   `bSingleQuad`): обод замирает на полу-экстенте 50 в конечном цвете (зелёный/красный)
        #   при alpha 1.0 и продолжает рисоваться. Данные не правятся — см. шапку файла.
        #   ⚠ Строка ПЕРЕОПРЕДЕЛЯЕТ `end_mode`, который ветка `ONE_SHOT` берёт с диска (там 0).
        #     Гейт `ONE_SHOT` оставлен намеренно: он остаётся ЕДИНСТВЕННЫМ местом, где видно
        #     дисковое значение, и снимает «закон вечных эффектов КАРТ» (`growQuad` → 1).
        #   Откат к нашему прежнему поведению («отыграл и исчез») — `ASDA_TARGETRING_NO_HOLD=1`.
        #   Яркость удержанного кадра можно подобрать без правки кода: `ASDA_TARGETRING_HOLD_ALPHA`.
        if os.environ.get("ASDA_TARGETRING_NO_HOLD", "") != "1":
            CFX.HOLD_LAST_FRAME = set(getattr(CFX, "HOLD_LAST_FRAME", set())) | set(jebs)
            a_env = os.environ.get("ASDA_TARGETRING_HOLD_ALPHA", "")
            CFX.HOLD_LAST_FRAME_ALPHA = float(a_env) if a_env else None
        ok, skipped, missing = FXC.build_channel(J, PE, CFX, jebs, log,
                                                 skip_existing=False, force=True)
        log("ГОТОВО: построено %d из %d%s" % (ok, len(jebs),
                                              (", нет на диске: %s" % ", ".join(missing)) if missing else ""))
    finally:
        CFX.SRC_DIR = old_src


if FXC.is_direct_run(globals()):
    main()
