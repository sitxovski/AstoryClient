# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_common.py — ОБЩИЕ ТОЧКИ СБОРЩИКОВ ЭФФЕКТОВ (аудит №4, волна 3).
#
# ЗАЧЕМ. Канальных сборщиков BP-эффектов стало восемь, и у пяти из них `build_one()` был ДОСЛОВНОЙ
#   копией: чтение .jeb → режим бленда → создание BP на `AsdaEffectActor` → цикл эмиттеров →
#   `save_loaded_asset`. Расхождение копий уже наблюдалось (закон [[jeb-emitter-routing-hole]]:
#   набор типов, обновлённый в одном месте, в другом молча глотал эмиттер), поэтому общий код
#   сведён СЮДА — ровно тем же приёмом, каким это сделано в C++ ([[port-shared-points]]).
#
# ЧТО СЮДА НЕ ПЕРЕЕХАЛО И ПОЧЕМУ:
#   * `build_emitter()` / `etalon_end_mode()` — живут в `asda_place_effects.py` и остаются там:
#     это ЭТАЛОННАЯ трансляция записи эмиттера, у неё один владелец.
#   * Разбор `.jeb`/`.jem` — `asda_jeb_parse.py` / `asda_jem_parse.py`, дублей нет (аудит №4).
#   * Канальные ОТЛИЧИЯ (endMode, strict-флаги, гейты) — остаются у своего сборщика: они и есть
#     смысл канала. Здесь только форма, одинаковая у всех.
#
# ЗАКОН [[tools-no-import-side-effects]]: модуль при импорте НИЧЕГО не делает — ни на диске, ни в
#   глобальном состоянии процесса.
# ⚠ `unreal` импортируется ЛЕНИВО — модуль должен читаться и вне редактора (пробники, тесты).
#
# ★★★ ВОЛНА Ф18 (МЕЛК-2, 20.09.2026): ЗДЕСЬ БЫЛО НАРУШЕНИЕ ЗАКОНА — И ЛОЖНОЕ ЗАВЕРЕНИЕ О НЁМ.
#   ЧТО БЫЛО. На верхнем уровне стояли две строки:
#       _fxc_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
#       import asda_ue_paths as _U
#   Первая — ВЫЗОВ, то есть побочное действие при импорте: она правит `sys.path` ВСЕГО ПРОЦЕССА, и
#   с этого мига любой модуль в Tools/ (`code.py`, `types.py`, `json.py` — что угодно с именем из
#   стандартной библиотеки) начинает перебивать одноимённый для ВСЕХ, кто в этом процессе живёт,
#   включая сам редактор. Вреда пока не было, но это глобальная правка, сделанная тайком, — ровно
#   то, что закон и запрещает. Отчёт волны Ф17 (§5) при этом заверял, что «на верхнем уровне
#   только Import/Assign/ClassDef/FunctionDef (проверено разбором ast)»: заверение ЛОЖНОЕ, там был
#   и Expr(Call). Строка пришла из прежней волны и просто не попала в тот разбор.
#   ЛЕЧЕНИЕ. Вставка пути и импорт реестра — ЛЕНИВЫЕ, внутри `_paths()`: правка `sys.path` теперь
#   случается только у того, кто и вправду просит имя ассета, а не у всякого, кто модуль импортнул.
#   Результат кэшируется, поэтому вставка делается не более одного раза за процесс.
import os
import sys as _fxc_sys

_U = None   # реестр путей ассетов (asda_ue_paths); заполняется лениво в _paths()


def _paths():
    """Реестр путей ассетов `asda_ue_paths`, подгружаемый ПО ТРЕБОВАНИЮ.

    ПОЧЕМУ ЛЕНИВО, А НЕ НА ВЕРХНЕМ УРОВНЕ: закон [[tools-no-import-side-effects]] — импорт модуля
      не смеет менять ничего в процессе, а вставка каталога в `sys.path` меняет разрешение имён
      для ВСЕХ модулей процесса (разбор — в шапке файла).
    ПОЧЕМУ ВСТАВКА ВООБЩЕ НУЖНА: сборщики запускаются и как `-ExecutePythonScript <полный путь>`,
      и как `-run=pythonscript`, и каталог Tools/ в `sys.path` при этом бывает не всегда.
    """
    global _U
    if _U is None:
        here = os.path.dirname(os.path.abspath(__file__))
        if here not in _fxc_sys.path:
            _fxc_sys.path.insert(0, here)
        import asda_ue_paths as _mod   # ★ волна 2 п. 7 (14.09.2026): правило имени BP
        _U = _mod
    return _U

# ── Типы эмиттеров (единственный источник; литералы по файлам расходились) ─────────────────────
#   Дублировать эти наборы литералом ЗАПРЕЩЕНО: место, где набор не обновили, глотает эмиттер
#   молча ([[jeb-emitter-routing-hole]]).
MESH_TYPES = (0x11, 0x12, 0x1F)      # модель-эмиттеры (.jem MeshRenderer); = asda_jeb_parse.MESH_TYPES
QUAD_TYPES = (0xB, 0xC, 0x18)        # биллборд-квады (общий набор всех каналов)
QUAD_TYPES_WITH_2 = (2, 0xB, 0xC, 0x18)   # + тип 2 (близнец 0xC, cmchar01) — канал персонажей
RING_TYPE = 0xE                      # кольцо/цилиндр
PARTICLE_TYPES = (0xD, 3, 0x1B, 0x17, 0x20)   # трек-частицы (порядок как в build_emitter)

# Родительский класс BP-заготовки эффекта — один на все каналы.
FX_PARENT_CLASS = "AsdaEffectActor"


def is_direct_run(mod_globals):
    """True — файл запущен НАПРЯМУЮ (python file.py, UE -ExecutePythonScript), а не импортирован.

    ПОЧЕМУ НЕ `__name__ != "<имя модуля>"`: такая проверка ловит ТОЛЬКО импорт ровно под этим
      именем. `import Tools.X` даёт "Tools.X", а `importlib.util.spec_from_file_location("Y", ...)`
      — вообще любое, и main() выстрелил бы, перезалив ассеты.
    ПОЧЕМУ НЕ один `__name__ == "__main__"`: UE запускает файл через -ExecutePythonScript, и
      обещать конкретное значение `__name__` в этом пути нельзя.
    НАДЁЖНЫЙ ПРИЗНАК — `__spec__`: его выставляет ЛЮБАЯ машинерия импорта (import / importlib /
      пакет / runpy.run_module) и НЕ выставляет прямое исполнение файла. `python -m` даёт
      `__name__ == "__main__"` и покрыт первым условием.
    Вызывать строго как `asda_fx_common.is_direct_run(globals())` — иначе проверялись бы
      `__name__`/`__spec__` ЭТОГО модуля, а не вызывающего.
    """
    return mod_globals.get("__name__") == "__main__" or mod_globals.get("__spec__") is None


# ── Гейты каналов через переменные окружения (механизм отката, менять имена НЕЛЬЗЯ) ────────────
def env_flag(name, default=False):
    """`ASDA_*=1` → True. Единая форма чтения гейта (было три написания на файл)."""
    v = os.environ.get(name, "")
    return (v == "1") if v != "" else bool(default)


def kf_seed_enabled():
    """Сид-узлы трека (СТАРТ t=0 / КОНЕЦ t=lifetime). Откат: `ASDA_FX_NO_KF_SEED=1`."""
    return os.environ.get("ASDA_FX_NO_KF_SEED", "") != "1"


def etalon_end_enabled():
    """Эталонный `end_mode` вместо жёсткой «1». Откат: `ASDA_FX_NO_ETALON_ENDMODE=1`."""
    return os.environ.get("ASDA_FX_NO_ETALON_ENDMODE", "") != "1"


def only_filter(names, env_var, lower=True):
    """Гейт точечного ремонта `ASDA_*_ONLY="имя1;имя2"` — построить только перечисленные.

    Закон «ПОДТВЕРЖДЁННЫЕ эффекты НЕ ТРОГАТЬ при фиксах смежных»: точечная сборка нового эффекта
      без пересборки уже принятых.
    `lower=False` — режим `ASDA_CHARFX_ONLY` (сравнение без приведения регистра, как было).
    """
    only = os.environ.get(env_var, "").strip()
    if not only:
        return list(names)
    if lower:
        want = set(x.strip().lower() for x in only.split(";") if x.strip())
        return [n for n in names if n.lower() in want]
    want = set(only.split(";"))
    return [n for n in names if n in want]


# ── Загрузка модулей эффектов ─────────────────────────────────────────────────────────────────
def import_fx_modules(log, with_char_fx=False):
    """`(J, PE[, CFX])` или `None` — общий пролог всех семи main().

    Импорт ОТЛОЖЕННЫЙ (внутри функции), как и было у каждого сборщика: `asda_place_effects`
      тянет `unreal` и читает окружение `ASDA_MAP` на импорте, поэтому его нельзя тащить в шапку
      модуля-библиотеки.
    """
    try:
        import asda_jeb_parse as J
        import asda_place_effects as PE
        if with_char_fx:
            import asda_build_char_fx as CFX
            return J, PE, CFX
        return J, PE
    except Exception as e:
        log("модули эффектов недоступны: %s" % e)
        return None


def point_pe_to(PE, out_pkg):
    """Перенацелить импортёр текстур/материалов `asda_place_effects` на пакет канала.

    По умолчанию `PE.FX_TEX`/`FX_MAT` смотрят в `/Game/Map/<карта>/Effect*` (модуль писался под
      карты и считает их на импорте из `ASDA_MAP`). Канальные сборщики кладут материалы рядом со
      своими BP. `_matcache` обязателен к сбросу: он ключуется (текстура, режим) БЕЗ пути, и без
      сброса второй канал получил бы MIC первого.
    """
    PE.FX_TEX = "%s/Tex" % out_pkg
    PE.FX_MAT = "%s/Mat" % out_pkg
    try:
        PE._matcache.clear()
    except Exception:
        pass


# ── Чтение .jeb и режим бленда ────────────────────────────────────────────────────────────────
def jeb_path(src_dir, eff_name):
    """Путь к .jeb эффекта (имя на диске = имя эффекта, регистр НИЖНИЙ)."""
    return os.path.join(src_dir, eff_name + ".jeb")


def read_jeb(J, src_dir, eff_name, log):
    """Разобранный .jeb или None. Все три отказа логируются ровно как раньше."""
    src = jeb_path(src_dir, eff_name)
    if not os.path.isfile(src):
        log("НЕТ файла: %s" % src)
        return None
    try:
        jb = J.parse_jeb(open(src, "rb").read())
    except Exception as e:
        log("%s: не распарсился: %s" % (eff_name, e))
        return None
    if not jb:
        log("%s: пустой JEB" % eff_name)
        return None
    return jb


def blend_mode(jb):
    """Режим бленда эффекта из шапки .jeb: "add" / "mod" / "alpha".

    Индексы бленда лежат в заголовке (+0x44/+0x48); лоадеры эталона переводят их как
      D3DBLEND = диск+1. Формула ниже — 1:1 та, что стояла в пяти сборщиках.
    """
    bd = jb.get("blendDst", 1)
    return "add" if (jb.get("blendAdditive") or bd == 1) else ("mod" if bd == 3 else "alpha")


# ── BP-заготовка эффекта ──────────────────────────────────────────────────────────────────────
def bp_name(eff_name):
    """Имя ассета канала: `BP_Fx_<jeb с '-'→'_'>`.

    РОВНО то же преобразование делает рантайм — `AAsdaEffectActor::SpawnByJebName`
      (`JebBase.Replace("-","_")`), поэтому дефис в именах на диске СОХРАНЯЕТСЯ как есть.
    """
    return _paths().fx_bp_name(eff_name)   # ★ волна 2 п. 7: правило в реестре путей (двойник C++ AsdaAssetPaths::FxBpName)


def ensure_effect_bp(out_name, out_pkg, log):
    """Загрузить или создать BP-заготовку на `AsdaEffectActor`; вернуть (bp, cdo) или (None, None)."""
    import unreal
    full = "%s/%s" % (out_pkg, out_name)
    bp = unreal.EditorAssetLibrary.load_asset(full) \
        if unreal.EditorAssetLibrary.does_asset_exist(full) else None
    if not bp:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", getattr(unreal, FX_PARENT_CLASS))
        bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            out_name, out_pkg, unreal.Blueprint, factory)
    if not bp:
        log("%s: BP не создан" % out_name)
        return None, None
    return bp, unreal.get_default_object(bp.generated_class())


# ── ★★★ ВОЛНА Ф16 (К-1, 20.09.2026): ЯВНЫЙ ГЕЙТ «МАССИВ ЭМИТТЕРОВ ПИШЕТ СКРИПТ» ────────────────
#   ЗАЧЕМ. `AAsdaEffectActor::PostEditChangeProperty` на записи в `Emitters` решает ДВА вопроса:
#   восстанавливать ли настройки владельца из долговечной копии `EmitterGlowSaved` и авторитетна
#   ли запись (то есть чистить ли копию по нынешнему состоянию массива). Ошибка в пользу «пишет
#   владелец» стоит ПОТЕРИ НАСТРОЕК НА ДИСКЕ: копия пересобирается из свежераспечатанных
#   умолчаний, а `finish_effect_bp` тут же зовёт `save_loaded_asset` — отката нет.
#   ПОЧЕМУ САМ ПРИЗНАК НЕНАДЁЖЕН. До этой волны распознавание держалось на ФОРМЕ события правки:
#   сборщик пишет массив ЦЕЛИКОМ одним `set_editor_property`, поэтому у события совпадают адреса
#   `Property` и `MemberProperty`, а вид правки остаётся `Unspecified`. Это верно ровно до тех пор,
#   пока запись идёт целым массивом: начни сборщик писать поэлементно или в поле
#   (`emitters[i].glow…`), цепочка свойств станет длиннее, форма совпадёт с панелью деталей — и
#   запись сочтут рукой владельца. То есть защита зависела от детали реализации ВЫЗЫВАЮЩЕГО.
#   ЛЕЧЕНИЕ — ГЕЙТ, КОТОРЫЙ ФОРМЫ СОБЫТИЯ НЕ КАСАЕТСЯ ВОВСЕ. Ровно тот же приём, что у замка
#   `asda.Fx.Light.NoManualLock` (его на время своей работы поднимает
#   Tools/asda_apply_fx_light_defaults.py).
#   ЗАКОН ПРИМЕНЕНИЯ: поднимать ТОЛЬКО на время самой записи и опускать ВСЕГДА, в том числе при
#   исключении (иначе оставленный поднятым ключ сделал бы «скриптом» следующую правку владельца
#   в панели деталей — то есть ровно ту беду, от которой гейт и заводится).
ASDA_FX_EMITTERS_WRITER_CVAR = "asda.Fx.Glow.EmittersWriter"
ASDA_FX_EMITTERS_WRITER_SCRIPT = 1   # значения ключа: -1 авто, 0 «пишет панель», 1 «пишет скрипт»


class emitters_writer_gate(object):
    """Контекст «сейчас массив `emitters` пишет СКРИПТ»: `with FXC.emitters_writer_gate(): …`.

    Поднимает `asda.Fx.Glow.EmittersWriter` в 1 на входе и ВОЗВРАЩАЕТ прежнее значение на выходе —
      в том числе когда тело вышло исключением (`__exit__` зовётся и тогда).
    Прежнее значение, а не безусловный -1: вложенные контексты (сборщик внутри раскладчика) и
      ручная установка ключа владельцем не должны ломаться.

    ★ УСТАНОВКА ПРОВЕРЯЕТСЯ ЧТЕНИЕМ ОБРАТНО (закон «доказывать вторым методом»): консольная
      команда молча не делает ничего, если ключа в бинаре нет (старая сборка) или имя изменилось.
      `ProcessUserConsoleInput` мира не требует, поэтому работает и в коммандлете
      (`-run=pythonscript`): KismetSystemLibrary.cpp:571-590 пробует его ПЕРВЫМ.
    ★ Ключа нет ⇒ предупреждение в лог и работа БЕЗ гейта (распознавание по форме события никуда
      не делось). Ронять сборку эффектов из-за отсутствия ключа нельзя: сборщики гоняются и на
      сборках без нашей правки.
    ★ Ф17 (МЕЛК-1): если ЧТЕНИЕ ОБРАТНО не подтвердило поднятие, ключ всё равно ВОЗВРАЩАЕТСЯ в
      прежнее значение прямо на входе: команда установки к этому моменту уже выполнялась, и
      «не подтвердилось» ≠ «не записалось». Прежде этот случай оставлял ключ поднятым навсегда.
    ★ Вне редактора (`unreal` не импортируется) — полный no-op: модуль обязан читаться пробниками.
    """

    def __init__(self, value=ASDA_FX_EMITTERS_WRITER_SCRIPT, log=None):
        self.value = value          # какое значение поднять (1 = «пишет скрипт»)
        self.prev = None            # значение ключа ДО входа — его и вернём
        self.active = False         # удалось ли и вправду поднять (иначе выход ничего не делает)
        self._log = log             # необязательный печатальщик канала (у сборщиков он свой)

    # Чтение значения ключа; None — ключа в этой сборке нет
    @staticmethod
    def _read(unreal):
        try:
            if not unreal.SystemLibrary.get_console_variable_string_value(ASDA_FX_EMITTERS_WRITER_CVAR):
                return None      # пустая строка = переменная не найдена (KismetSystemLibrary.cpp:593-608)
            return int(unreal.SystemLibrary.get_console_variable_int_value(ASDA_FX_EMITTERS_WRITER_CVAR))
        except Exception:
            return None

    @staticmethod
    def _write(unreal, value):
        unreal.SystemLibrary.execute_console_command(None, "%s %d" % (ASDA_FX_EMITTERS_WRITER_CVAR, value))
        return emitters_writer_gate._read(unreal)

    def _say(self, text):
        if self._log:
            self._log(text)
        else:
            try:
                import unreal
                unreal.log_warning(text)
            except Exception:
                pass

    def __enter__(self):
        try:
            import unreal
        except Exception:
            return self          # не в редакторе — гейта нет и не нужно
        self.prev = self._read(unreal)
        if self.prev is None:
            self._say("[свечение] ключ %s в этой сборке не найден — запись эмиттеров идёт БЕЗ явного гейта "
                      "(распознаётся по форме события правки)" % ASDA_FX_EMITTERS_WRITER_CVAR)
            return self
        got = self._write(unreal, self.value)
        if got != self.value:
            # ═══ ★★★ ВОЛНА Ф17 (МЕЛК-1, 20.09.2026): ПУТЬ УТЕЧКИ ГЕЙТА ═══════════════════════════
            #   ЧТО БЫЛО НЕ ТАК. `_write` СНАЧАЛА выполняет консольную команду и только потом
            #   читает значение обратно, а `_read` глотает ЛЮБОЕ исключение и возвращает None.
            #   Значит, случай «запись прошла, а чтение бросило» (или вернуло не то) давал
            #   got != value ⇒ active оставался False ⇒ `__exit__` не делал НИЧЕГО — и ключ
            #   оставался поднятым в 1 на весь сеанс редактора. А поднятый ключ — это ровно
            #   залипший гейт из С-2: каждая правка владельца в панели молча откатывается.
            #   ЛЕЧЕНИЕ. Раз команда УЖЕ выполнена, считать, что ключ не тронут, нельзя.
            #   Пробуем вернуть прежнее значение прямо здесь и говорим, чем кончилось.
            back = None
            try:
                back = self._write(unreal, self.prev)
            except Exception as e:
                self._say("[свечение] ключ %s: возврат в %r не удался (%s: %s)"
                          % (ASDA_FX_EMITTERS_WRITER_CVAR, self.prev, type(e).__name__, e))
            self._say("[свечение] ключ %s НЕ поднялся (прочитано %r вместо %d) — запись идёт без гейта; "
                      "команда установки всё же выполнялась, поэтому ключ возвращён в %r (прочитано %r)%s"
                      % (ASDA_FX_EMITTERS_WRITER_CVAR, got, self.value, self.prev, back,
                         "" if back == self.prev else
                         " — ★ ВЕРНУТЬ НЕ УДАЛОСЬ, верните вручную: %s %r"
                         % (ASDA_FX_EMITTERS_WRITER_CVAR, self.prev)))
            return self
        self.active = True
        return self

    def __exit__(self, exc_type, exc, tb):
        if not self.active:
            return False         # исключение не глушим
        try:
            import unreal
            got = self._write(unreal, self.prev)
            if got != self.prev:
                self._say("[свечение] ключ %s НЕ вернулся в %r (прочитано %r) — правки владельца в панели "
                          "деталей могут быть сочтены записью скрипта; верните ключ вручную"
                          % (ASDA_FX_EMITTERS_WRITER_CVAR, self.prev, got))
        except Exception as e:
            # ★ Ф17 (МЕЛК-1): своё исключение выхода НЕ ДОЛЖНО подменять исключение тела. Молчать
            #   тоже нельзя: ключ в этот момент почти наверняка остался поднятым.
            self._say("[свечение] ключ %s: возврат в %r сорвался исключением (%s: %s) — ★ ВЕРНИТЕ ВРУЧНУЮ: %s %r"
                      % (ASDA_FX_EMITTERS_WRITER_CVAR, self.prev, type(e).__name__, e,
                         ASDA_FX_EMITTERS_WRITER_CVAR, self.prev))
        finally:
            self.active = False
        return False


def finish_effect_bp(bp, cdo, emitters):
    """Записать список эмиттеров в CDO и сохранить ассет. Возвращает True, если СОХРАНЕНО.

    ★★★ ЧЕСТНЫЙ РЕЗУЛЬТАТ СОХРАНЕНИЯ (2026-08-21, поймано на кольце цели). `save_loaded_asset`
      возвращает False, когда `.uasset` занят ДРУГИМ процессом (открытый редактор держит файл:
      `MoveFile … (Error Code 32)` = ERROR_SHARING_VIOLATION). Раньше результат ВЫБРАСЫВАЛСЯ, и
      канальный сборщик всё равно печатал «создан …» — то есть лог утверждал, что ассет
      пересобран, хотя на диске остался СТАРЫЙ. Именно так пересборка кольца «прошла успешно»,
      а `end_mode` у `friendlyselect` остался прежним (1 = вечный пульс).
      Теперь результат возвращается наверх, а `build_one`/`build_channel` печатают правду.

    ★★★ ВОЛНА Ф16 (К-1, 20.09.2026): ЗАПИСЬ ИДЁТ ПОД ЯВНЫМ ГЕЙТОМ. Через эту функцию перепечатывают
      эмиттеры ШЕСТЬ канальных сборщиков (asda_build_char_fx.py:410, _click_marker:88,
      _combat_impacts:60, _loot_fx:49, _mount_fx:90, _weapon_glow_fx:159) — то есть это главный
      путь, на котором настройки свечения владельца могли бы быть затёрты умолчаниями и тут же
      записаны на диск строкой ниже. Обоснование гейта — у класса `emitters_writer_gate`.
      Гейт накрывает ТОЛЬКО запись массива: `save_loaded_asset` вынесен из-под него намеренно —
      `PreSave` зовёт `SaveEmitterGlow()` НЕавторитетно и в гейте не нуждается, а держать ключ
      поднятым дольше необходимого значит расширять окно, в котором правка владельца, случившаяся
      в этот момент в панели деталей, была бы сочтена записью скрипта.
    """
    import unreal
    # ★ 10.10.2026 (`Reports/owner_1010/fx_blend_fix.md`): материалы — по бленду эталона (0x1F непрозрачный,
    #   аддитивная половина пары dst = 3 по src). Без правок список возвращается как есть; откат при пересборке —
    #   ASDA_FX_NO_ETALON_BLEND=1. Правки владельца (материал не от сборщика) не трогаются.
    try:
        import asda_fx_blend_fix as _BF   # импорт без побочных действий
        emitters = _BF.retarget_for_build(cdo, emitters)
    except Exception as _ex:
        unreal.log_warning("[fxblend] правка бленда при сборке не применена: %s" % _ex)
    with emitters_writer_gate():
        cdo.set_editor_property("emitters", emitters)
    return bool(unreal.EditorAssetLibrary.save_loaded_asset(bp))


# ── Строгие флаги эффектов, живущих в трансформе КОСТИ ────────────────────────────────────────
def apply_bone_fx_flags(fx, em_type, quad_types=QUAD_TYPES):
    """Строгие оси для эффекта на ЖИВОЙ КОСТИ (персонаж, транспорт, свечение заточки).

    ⚠ КАРТЫ СЮДА НЕ ХОДЯТ. Они откалиброваны под ИСТОРИЧЕСКИЙ (нестрогий) знак и остаются на
      дефолтах False — иначе разворот на 180° (регрессия 2026-07-30). Поэтому функция вызывается
      только канальными сборщиками, а `asda_place_effects.build_emitter` её не знает.

    * MESH_TYPES → `strict_channel_rot` (знак канальных вращений сверен с live.dmp по всем трём
      осям, §H6) и `strict_mesh_pos` (pos-канал через ПОЛНУЮ W: pos(0,2.5,0)·W = +25 см, §H3).
    * quad_types → `strict_quad_orbit` (орбита pos-канала биллборд-квадов, §H10: кольцо соулмейта
      8×0xC, pos=(50,0,0), rotY 0→360 = облёт вокруг персонажа).
      Канал персонажей передаёт `QUAD_TYPES_WITH_2` — у него есть тип 2 (близнец 0xC, cmchar01).
    * RING_TYPE → `strict_ring_base_pos` (basePos кольца через локаль актора, эталонная формула).
    """
    if em_type in MESH_TYPES:
        try:
            fx.set_editor_property("strict_channel_rot", True)
        except Exception:
            pass
        try:
            fx.set_editor_property("strict_mesh_pos", True)
        except Exception:
            pass
    if em_type in quad_types:
        try:
            fx.set_editor_property("strict_quad_orbit", True)
        except Exception:
            pass
    if em_type == RING_TYPE:
        try:
            fx.set_editor_property("strict_ring_base_pos", True)
        except Exception:
            pass


# ── Каналы, переиспользующие `asda_build_char_fx.build_one` (навыки, состояния) ────────────────
def extend_channel_gates(CFX, jebs):
    """Расширить множества-гейты `asda_build_char_fx` списком СВОЕГО канала.

    Расширение живёт ТОЛЬКО в процессе своего сборщика ⇒ чужие каналы (крылья/соулмейт/хранитель,
      свечение заточки, транспорт, лут, импакты, эффекты карт) измениться не могут — ровно тот же
      приём, что применялся к `ONE_SHOT` вручную в каждом сборщике.
    * `ONE_SHOT`    — endMode берётся С ДИСКА, иначе ветки growQuad/mesh12 форсят `end_mode = 1`
      («закон бессмертных эффектов КАРТ») и разовый эффект пульсирует.
    * `KF_SEED_FX`  — сид-узлы трека: СТАРТ(t=0) + ключи файла + КОНЕЦ(t=lifetime); в файле лежат
      только промежуточные ключи (`Reports/item_fx_ring_motion.md`).
    """
    CFX.ONE_SHOT = set(CFX.ONE_SHOT) | set(jebs)
    CFX.KF_SEED_FX = set(CFX.KF_SEED_FX) | set(jebs)


def build_channel(J, PE, CFX, jebs, log, skip_existing=False, force=False):
    """Прогон канала через `asda_build_char_fx.build_one`. Возвращает (построено, пропущено, нет_на_диске).

    `skip_existing=True` (канал состояний): уже собранный BP НЕ пересобирается — чужие эффекты
      (навыки/крылья/карты) могли попасть в пересечение списков, а перегенерация стирает ручные
      правки ([[effects-conversion-model]]). `force=True` снимает пропуск (раскатка правок ОБЩЕГО
      кода: обычный прогон только ДОБАВЛЯЕТ недостающие BP и изменения кода до канала не доезжают).
    """
    import unreal
    ok, skipped, missing = 0, 0, []
    for eff in jebs:
        if not os.path.isfile(jeb_path(CFX.SRC_DIR, eff)):
            missing.append(eff)
            continue
        if skip_existing and not force and \
                unreal.EditorAssetLibrary.does_asset_exist("%s/%s" % (CFX.OUT_PKG, bp_name(eff))):
            skipped += 1
            continue
        ok += 1 if CFX.build_one(J, PE, eff) else 0
    return ok, skipped, missing


def set_end_mode(fx, value):
    """Записать `end_mode` (свойства может не быть у старого класса — молча пропускаем)."""
    try:
        fx.set_editor_property("end_mode", value)
    except Exception:
        pass


# ── ★★★ «ДЕРЖАТЬ ПОСЛЕДНИЙ КАДР» (кольцо выбора цели) ─────────────────────────────────────────
def hold_last_frame(fx, log=None, alpha=None):
    """Квад отыгрывает свою проходку ОДИН раз и ЗАМИРАЕТ на конечном узле, оставаясь видимым.

    ЗАЧЕМ (кольцо выбора цели, замечание владельца 2026-08-21): «оно не должно просто отыграть и
      исчезнуть, а на определённом диаметре остановиться и быть видимым — собственно это и
      называется выделение». Разбор — `Reports/npc_target_ring_fix2.md` §2.

    ЧТО ДЕЛАЕТСЯ. По сути ровно одно: `end_mode = 3`. У ОДИНОЧНОГО КВАДА (`bSingleQuad`) рантайм
      трактует 3 как ФРИЗ — `AAsdaEffectActor::Tick`, `case 3`: `if (E.bSingleQuad || E.bRing)
      S.LifeTimer = Period;` ⇒ `NodeAt` навсегда отдаёт значения КОНЕЧНОГО узла, эмиттер НЕ
      помечается `bStopped` и продолжает строить геометрию. То есть «замереть на конечном
      диаметре» — штатная ветка рантайма, а не новый режим и не пересоздание эффекта в тике.
      Для сравнения `end_mode = 0` (что стоит по данным) на той же развилке гасит эмиттер и
      ЧИСТИТ секцию — кольца на земле не остаётся; `1`/`2` дают вечный перезапуск (пульс).

    ★ ДАННЫЕ ПРИ ЭТОМ НЕ ПРАВЯТСЯ: у кольца `alpha = [старт 1.0, КОНЕЦ 1.0, разброс 0.0]`, то есть
      обод и так не выцветает, и удержанный кадр непрозрачен сам по себе (прошлые отчёты читали
      третий элемент тройки — разброс — как конечное значение и ошибочно писали «alpha 1 → 0»).
      Строка ниже страхует ОБЩИЙ случай: если у какого-то эффекта конечная альфа действительно
      нулевая, фриз дал бы невидимый кадр — тогда (и только тогда) держим стартовую яркость того
      же файла, о чём честно пишем в лог. `alpha=<число>` — ручное переопределение яркости.
      Размер и цвет не трогаются никогда: держится конечный узел как есть.

    Возвращает True, если фриз применён.
    """
    try:
        nodes = list(fx.get_editor_property("nodes") or [])
    except Exception:
        nodes = []
    if len(nodes) < 2:
        if log:
            log("удержание последнего кадра НЕ применено: узлов %d (нужно >= 2)" % len(nodes))
        return False
    last = nodes[-1]
    a_was = float(last.get_editor_property("alpha"))
    a_hold = a_was
    if alpha is not None:
        a_hold = float(alpha)
    elif a_was <= 0.01:
        a_hold = float(nodes[0].get_editor_property("alpha"))
    if a_hold != a_was:
        last.set_editor_property("alpha", a_hold)
        nodes[-1] = last
        fx.set_editor_property("nodes", nodes)
    set_end_mode(fx, 3)
    if log:
        log("удержание последнего кадра: end_mode=3, конечный узел t=%.3f полу-экстент %.1fx%.1f, "
            "альфа %.2f%s"
            % (float(last.get_editor_property("time")),
               float(last.get_editor_property("size_x")),
               float(last.get_editor_property("size_y")),
               a_hold,
               "" if a_hold == a_was else " (поднята с %.2f — иначе кадр был бы прозрачным)" % a_was))
    return True
