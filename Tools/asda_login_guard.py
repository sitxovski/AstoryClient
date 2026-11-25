# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_login_guard.py — ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА В Login.umap (решение владельца 13.09.2026).
#   Владелец вручную поставил свет в сцене логина (2 SpotLight, DirectionalLight, SkyLight) и велел
#   «никогда и ни при каких обстоятельствах это не проебать». Любой инструмент, который загружает и
#   СОХРАНЯЕТ /Game/Map/Login или меняет в нём акторы, обязан первым делом позвать forbid_login_write().
#   Без явного ASDA_ALLOW_LOGIN_WRITE=1 инструмент останавливается ДО загрузки уровня.
#   Защищённые копии уровня: AstoryClient/_backup/2026-09-13_OWNER_LOGIN_LIGHT/Login.umap и
#   @@ASTORY_DEV_ROOT_FWD@@/_backup/OWNER_LOGIN_LIGHT_20260913/Login.umap (md5 346b104673371d9baea5d4c67d9fcc01).
#   Закон tools-no-import-side-effects: при импорте модуль ничего не делает.
#   ★ 16.09.2026 (раздел 17): список пакетов логина — Tools/data/asda_login_packages.json (общий с C++-сторожем); при импорте
#   модуль только читает этот файл (без лога и записи).
#   ★ 16.09.2026 (раздел 22, Reports/dedup_audit/savedir_guard_2026-09-16.md): ПАПКУ ЦЕЛИКОМ СОХРАНЯТЬ НЕЛЬЗЯ.
#   Вместо EditorAssetLibrary.save_directory(root, recursive=True) звать save_dir_login_safe(root, "имя_инструмента")
#   (функция в самом низу файла): поштучная защита отсеивает пакеты логина при импорте/правке, а save_directory по той
#   же папке пересохранил бы их следом — мимо всякой проверки. То же для delete_directory: папку, под которой могут
#   лежать пакеты логина, целиком не сносить (forbid_login_write либо разворот login_expand_roots).
#   ★ 16.09.2026 (ревизия раздела 22) — три доработки, разбор в том же отчёте, раздел 2:
#     (1) КЭШ НА ПРОЦЕСС (блок «КЭШ НА ПРОЦЕСС» ниже; явный сброс — login_cache_reset): полный обход реестра
#         ar.search_all_assets(True) шёл НА КАЖДЫЙ заход save_dir_login_safe, а в циклах по чанкам их десятки;
#     (2) login_map_write_blocked: САМ уровень сцены логина (ASDA_MAP=Login у инструментов карт) отделён от
#         ассетов сцены — его открывает только ASDA_ALLOW_LOGIN_MAP_WRITE=1;
#     (3) save_dir_login_safe(..., loaded_assets=…): «под корнем реестр пуст» — больше не отказ, а поштучное
#         сохранение загруженного (общая замена запасной ветки, которую import_jui_textures.py держал у себя).
import os
import re   # ★ 15.09.2026 (раздел 16): нормализация формы пути (_normalize_package_path)

# Имя переменной окружения, которой владелец явно разрешает запись в уровень логина
ALLOW_ENV = "ASDA_ALLOW_LOGIN_WRITE"

# ★ 16.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 17). Переменная C++-сторожа AsdaLoginSaveGuard: сохранение и удаление
#   САМОГО уровня Login (umap, _BuiltData, внешние акторы/объекты) в автоматической сессии (коммандлет, -unattended,
#   -ExecutePythonScript, -ExecCmds=py …) разрешает только она; ASDA_ALLOW_LOGIN_WRITE уровень не открывает
#   (Reports/dedup_audit/login_save_guard.md, п. 6.3). В Python — только текст подсказки, логика от неё не зависит.
MAP_ALLOW_ENV = "ASDA_ALLOW_LOGIN_MAP_WRITE"
# Подсказка к сообщениям forbid_login_write для инструментов, сохраняющих сам уровень (вызов без target)
MAP_WRITE_HINT = (" Сохранить сам уровень Login.umap в автоматике (коммандлет, -unattended, -ExecutePythonScript) C++-сторож "
                  "даст только при ещё и %s=1, иначе save_current_level вернёт false." % MAP_ALLOW_ENV)


def forbid_login_write(tool_name, target=None):
    """Останавливает инструмент, если запись в Login.umap не разрешена владельцем явно."""
    if os.environ.get(ALLOW_ENV, "") == "1":
        msg = ("[LOGIN-GUARD] %s: запись в Login.umap РАЗРЕШЕНА (%s=1). Свет владельца (2 SpotLight, "
               "DirectionalLight, SkyLight) не трогать; сверить с защищённой копией после прогона." % (tool_name, ALLOW_ENV))
        if not target:   # ★ 16.09.2026 (раздел 17): инструмент пишет сам уровень — подсказка про переменную C++-сторожа
            msg += MAP_WRITE_HINT
        _log(msg, warn=True)
        return
    msg = ("[LOGIN-GUARD] %s ОСТАНОВЛЕН: инструмент пишет в /Game/Map/Login, где стоит свет владельца. "
           "Запуск только с явного разрешения владельца и %s=1." % (tool_name, ALLOW_ENV))
    if target:   # ★ 15.09.2026: инструмент пишет не уровень, а ассеты сцены логина (target — что именно)
        msg = ("[LOGIN-GUARD] %s ОСТАНОВЛЕН: инструмент пишет %s (сцена логина). "
               "Запуск только с явного разрешения владельца и %s=1." % (tool_name, target, ALLOW_ENV))
    if not target:   # ★ 16.09.2026 (раздел 17): инструмент пишет сам уровень — подсказка про переменную C++-сторожа
        msg += MAP_WRITE_HINT
    _log(msg, warn=False)
    raise SystemExit(3)


def _log(msg, warn):
    """Пишет в лог UE, если он есть, иначе в stdout."""
    try:
        import unreal
        (unreal.log_warning if warn else unreal.log_error)(msg)
    except Exception:
        print(msg)


def login_write_allowed():
    """True, если владелец явно разрешил запись в Login.umap (ASDA_ALLOW_LOGIN_WRITE=1). Для обходов всех уровней:
    без разрешения уровень логина нужно пропустить, а не останавливать весь инструмент."""
    return os.environ.get(ALLOW_ENV, "") == "1"


# ★★★ СПИСОК ПАКЕТОВ СЦЕНЫ ЛОГИНА — Tools/data/asda_login_packages.json (16.09.2026, Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 17).
#   Единый источник для Python и C++-сторожа; схема и правила сверки — в самом файле («rules») и в разделе 17 отчёта.
#   БЫЛО (разделы 11–16): кортежи LOGIN_OWNED_MODEL_DIRS / LOGIN_OWNED_PACKAGES / LOGIN_SHARED_EXCEPTIONS с обоснованиями прямо здесь;
#   записи и примечания перенесены в JSON без изменений (+ 7 JUI-атласов окон логина, раздел 17). Имена кортежей и нотация прежние:
#   «путь» — пакет или папка целиком (JSON match=tree), «путь*» — префикс имени (match=prefix); исключения — точный пакет (exact).
#   ★ FAIL-CLOSED: файл не прочитан / не разобран / не прошёл проверки → ВСЁ под /Game считается сценой логина (строка [LOGIN-GUARD]
#   один раз за процесс, в is_login_owned_package). Почему не остановка: исключение из проверки вызывающий мог бы проглотить своим
#   `except Exception` и пойти писать, а ответ «логин» проглотить нельзя — без разрешения каждый пакет /Game пропускается; с разрешением
#   (ASDA_ALLOW_LOGIN_WRITE=1) пропуск от принадлежности не зависит, инструменты работают как раньше; forbid_login_write список не читает.
LOGIN_PACKAGES_JSON = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "asda_login_packages.json")
LOGIN_PACKAGES_FORMAT = "asda_login_packages"   # ожидаемое значение поля format
LOGIN_PACKAGES_VERSION = 1                      # ожидаемое значение поля version
# Группы JSON и допустимые значения match в каждой
_LIST_MATCH = (("model_dirs", ("tree", "prefix")), ("packages", ("tree", "prefix")), ("shared_exceptions", ("exact",)))


def _load_login_packages(path):
    """★ 16.09.2026 (раздел 17). Читает JSON списка пакетов логина. Возвращает (model_dirs, packages, exceptions, ошибка): кортежи
    в прежней нотации («путь» / «путь*») и None; при любой ошибке чтения, разбора или проверки — ((), (), (), текст ошибки).
    Не бросает и в лог не пишет (строку fail-closed печатает is_login_owned_package при первом обращении)."""
    try:
        import json
        with open(path, "r", encoding="utf-8") as f:
            doc = json.load(f)
        if not isinstance(doc, dict) or doc.get("format") != LOGIN_PACKAGES_FORMAT or doc.get("version") != LOGIN_PACKAGES_VERSION:
            raise ValueError("format/version не %s/%d" % (LOGIN_PACKAGES_FORMAT, LOGIN_PACKAGES_VERSION))
        out, seen = {}, set()
        for group, allowed in _LIST_MATCH:
            items = doc.get(group)
            if not isinstance(items, list):
                raise ValueError("нет массива %s" % group)
            res = []
            for i, e in enumerate(items):
                p = e.get("path") if isinstance(e, dict) else None
                m = e.get("match") if isinstance(e, dict) else None
                if (not isinstance(p, str) or not p.startswith("/Game/") or "*" in p or "\\" in p or p.endswith("/")
                        or m not in allowed):
                    raise ValueError("%s[%d]: неверная запись %r" % (group, i, e))
                key = (group == "shared_exceptions", p.lower(), m)
                if key in seen:
                    raise ValueError("%s[%d]: повтор %s" % (group, i, p))
                seen.add(key)
                res.append(p + "*" if m == "prefix" else p)
            out[group] = tuple(res)
        if "/game/map/login" not in set(x.lower() for x in out["model_dirs"] + out["packages"]):
            raise ValueError("нет записи {/Game/Map/Login, tree}")
        return out["model_dirs"], out["packages"], out["shared_exceptions"], None
    except Exception as e:   # любая ошибка чтения/разбора/проверки → fail-closed у is_login_owned_package
        return (), (), (), "%s: %s" % (type(e).__name__, e)


# Таблицы (имена прежние, данные — JSON): модели сцены логина, прочие пакеты только логина, общие с игрой пакеты внутри них; ошибка чтения
LOGIN_OWNED_MODEL_DIRS, LOGIN_OWNED_PACKAGES, LOGIN_SHARED_EXCEPTIONS, _LIST_ERROR = _load_login_packages(LOGIN_PACKAGES_JSON)
_LIST_ERROR_LOGGED = [False]   # строка fail-closed уже напечатана (один раз за процесс)


# ★★★ КЭШ НА ПРОЦЕСС (16.09.2026, ревизия раздела 22, СРЕДН-1). Три уровня, и у каждого СВОЙ срок годности —
#   это и есть ответ на вопрос «не пропустит ли кэш пакет логина, созданный этим же прогоном»:
#     1) _OWNED_MEMO — ответ is_login_owned_package. Функция ЧИСТАЯ: таблицы читаются один раз при импорте модуля
#        (_load_login_packages) и в процессе не меняются, поэтому ответ зависит ТОЛЬКО от строки пути. Инвалидация
#        не нужна и была бы ошибкой: ничто в прогоне не может сделать из «логин» — «не логин».
#     2) _SEARCH_DONE — полный синхронный обход реестра ar.search_all_assets(True). Он ищет пакеты, которых реестр
#        ЕЩЁ НЕ ЗНАЕТ (лежат на диске мимо редактора); ассеты, созданные в этом же процессе (импорт, create_asset,
#        rename), реестр получает сам, без обхода. Значит обход нужен ОДИН раз за процесс, а не на каждый заход.
#     3) _EXPAND_MEMO — результат разворота login_expand_roots по корню. Ключ включает ОТПЕЧАТОК СПИСКА ПАКЕТОВ
#        под корнем, а сам список берётся СВЕЖИМ на каждый заход (get_assets_by_path НЕ кэшируется). Поэтому кэш
#        физически не может «не заметить» пакет сцены логина, созданный этим же прогоном: появился пакет →
#        изменился список → изменился отпечаток → разворот пересчитан. Инвалидация идёт ПО ДАННЫМ, а не по
#        дисциплине вызывающего (тот мог бы забыть позвать сброс — и дыра вернулась бы молча).
#   Логи при этом НЕ кэшируются: строки [LOGIN-GUARD] печатаются на каждый заход, как раньше (кэшируется расчёт,
#   а не поведение в журнале), иначе второй заход выглядел бы «тихим» и сравнение прогонов врало бы.
#   ★★★ КОГДА ВЫЗЫВАЮЩИЙ ОБЯЗАН ЗВАТЬ login_cache_reset() (16.09.2026, ревизия §7, МЕЛК-1). Сам модуль его не
#   зовёт НИГДЕ и звать не может: он не знает, что происходит с диском за пределами процесса. Кэшируется ровно
#   один факт с внешним сроком годности — «полный обход реестра уже сделан» (_SEARCH_DONE); он перестаёт быть
#   верным, если пакеты появились/исчезли на диске МИМО редактора этого процесса. Правило: сброс нужен ПОСЛЕ
#   любого такого события и ДО следующего сохранения через save_dir_login_safe / login_expand_roots:
#     • p4 sync / p4 revert / любая операция Perforce поверх Content, пока редактор открыт;
#     • распаковка архива, копирование ассетов драйвером сета, ручное копирование .uasset/.umap в Content;
#     • откат из _backup поверх Content (режимы rollback инструментов) без перезапуска редактора;
#     • чужой процесс (второй редактор, коммандлет), дописавший пакеты в Content.
#   Сброса НЕ требуют события ВНУТРИ процесса (импорт, create_asset, rename, delete): их реестр получает сам,
#   а список пакетов под корнем берётся свежим на каждый заход (см. уровень 3 выше).
#   ★ На 16.09.2026 в проекте таких мест НЕТ, поэтому и вызовов нет: сплошной поиск по Tools, _re и скриптам
#   (.py/.bat/.ps1) не дал ни одной операции Perforce, распаковки или копирования ассетов в Content, идущей
#   в ОДНОМ процессе с последующим сохранением. Единственный откат поверх Content
#   (asda_apply_fx_light_defaults.py: ASDA_FXL_MODE=rollback) работает БЕЗ UE и сразу выходит из main() —
#   кэшу защиты там нечего протухать. Появится такое место — сброс ставить по правилу выше.
_OWNED_MEMO = {}
_EXPAND_MEMO = {}
_SEARCH_DONE = [False]
# Счётчики для замеров (стенд раздела 2 отчёта): сколько раз спросили принадлежность, сколько раз реально считали.
CACHE_STATS = {"owned_call": 0, "owned_walk": 0, "search_all": 0, "list": 0, "expand_hit": 0, "expand_miss": 0}


def login_cache_reset(full=True):
    """★ 16.09.2026 (ревизия, СРЕДН-1). Явный сброс кэша защиты. full=True — вместе с флагом «полный обход реестра
    уже сделан» (search_all_assets будет вызван снова). _OWNED_MEMO не трогаем: это чистая функция от строки пути
    (см. блок выше), протухнуть она не может."""
    _EXPAND_MEMO.clear()
    if full:
        _SEARCH_DONE[0] = False


def is_login_owned_package(pkg):
    """True, если путь пакета, объекта («…/Имя.Имя») или папки лежит в моделях сцены логина (LOGIN_OWNED_MODEL_DIRS,
    без учёта регистра). Разрешение не проверяет — только принадлежность.
    ★ 16.09.2026 (раздел 21, МЕЛК-1): сверяются ОБЕ формы пути — свёрнутая (сегменты «..»/«.» разрешены,
    _fold_dot_segments) и исходная, посегментная. Логином считается путь, который узнала ХОТЯ БЫ ОДНА из них:
    сворачивание НЕ должно уметь выводить путь из-под защиты (fail-closed, разбор — в _fold_dot_segments).
    ★ 16.09.2026 (ревизия, СРЕДН-1): ответ запоминается на процесс (_OWNED_MEMO) — разбор путей и обход таблиц
    повторялись на каждый заход разворота (у /Game/AsdaAssets/Models/Char это 1 644 пути × число заходов)."""
    key = str(pkg)
    CACHE_STATS["owned_call"] += 1
    ans = _OWNED_MEMO.get(key)
    if ans is not None:
        return ans
    folded = _normalize_package_path(pkg)             # ★ 15.09.2026 (раздел 16): форма пути → «/Game/…» до сверки с таблицами
    if _login_owned_normalized(folded):
        ans = True
    else:
        literal = _normalize_package_path(pkg, fold=False)
        ans = literal != folded and _login_owned_normalized(literal)
    _OWNED_MEMO[key] = ans
    return ans


def _login_owned_normalized(pkg):
    """Сверка УЖЕ нормализованного пути с таблицами — ядро is_login_owned_package (выделено 16.09.2026, раздел 21)."""
    CACHE_STATS["owned_walk"] += 1   # ★ 16.09.2026 (ревизия, СРЕДН-1): счётчик РЕАЛЬНЫХ обходов таблиц (мимо memo)
    if not pkg:
        return False
    low = str(pkg).replace("\\", "/").rstrip("/").lower()
    if _LIST_ERROR is not None:                       # ★ 16.09.2026 (раздел 17): список пакетов не прочитан — fail-closed
        if not _LIST_ERROR_LOGGED[0]:
            _LIST_ERROR_LOGGED[0] = True
            _log("[LOGIN-GUARD] список пакетов сцены логина %s не прочитан (%s) — fail-closed: ВСЁ под /Game считается "
                 "сценой логина (без %s=1 запись пропускается)" % (LOGIN_PACKAGES_JSON, _LIST_ERROR, ALLOW_ENV), warn=False)
        return low == "/game" or low.startswith("/game/")
    last = low.rsplit("/", 1)[-1]
    if "." in last:                                   # путь объекта «…/Имя.Имя» → путь пакета
        low = low[:len(low) - len(last)] + last.split(".", 1)[0]
    for d in LOGIN_SHARED_EXCEPTIONS:                 # ★ 15.09.2026 (доработка): общие с игрой пакеты внутри папок логина
        if low == d.lower():
            return False
    for d in LOGIN_OWNED_PACKAGES:                    # ★ 15.09.2026: пакеты сцены логина вне моделей
        if _login_entry_match(low, d):
            return True
    for d in LOGIN_OWNED_MODEL_DIRS:
        dl = d.lower()
        if dl.endswith("*"):
            if low.startswith(dl[:-1]):
                return True
        elif low == dl or low.startswith(dl + "/"):
            return True
    return False


def _normalize_package_path(pkg, fold=True):
    """★ 15.09.2026 (раздел 16 отчёта). Путь пакета/объекта в любой из встречающихся форм → «/Game/…» (регистр сохраняется):
      `Класс'/Game/…'` / `Класс"/Game/…"` (ссылка, скопированная из редактора) → внутренний путь;
      дисковый `…/Content/….uasset|.umap` (абсолютный или относительный) → «/Game/…» без расширения;
      `/All/Game/…` (путь папки в Content Browser) → «/Game/…»; `Game/…` без ведущего «/» → «/Game/…»;
      «\» → «/», повторные «//» → «/».
    Прочее возвращается как есть. БЫЛО: такие формы не совпадали с таблицами — пакет логина проходил мимо защиты.
    fold=False — БЕЗ сворачивания «..»/«.» (посегментная форма); нужно is_login_owned_package для второй сверки
    (★ 16.09.2026, раздел 21, МЕЛК-1)."""
    s = str(pkg).strip().replace("\\", "/")
    m = re.match(r"""^[A-Za-z0-9_]*\s*(['"])(.*)\1$""", s)          # Класс'путь' / 'путь'
    if m:
        s = m.group(2).strip()
    # ★ 16.09.2026 (раздел 17): UE5-форма ссылки `/Script/Модуль.Класс'/Game/…'` (Copy Reference) — выражение выше её не брало
    #   («/» и «.» в имени класса), путь уходил мимо таблиц. Снимается и после внешних кавычек, снятых выше.
    m = re.match(r"""^/Script/[A-Za-z0-9_]+\.[A-Za-z0-9_]+\s*(['"])(.*)\1$""", s)
    if m:
        s = m.group(2).strip()
    low = "/" + s.lower()
    i = low.rfind("/content/")                                        # дисковый путь пакета
    if i >= 0 and low.endswith((".uasset", ".umap")):
        s = "/Game/" + s[i - 1 + len("/content/"):].rsplit(".", 1)[0]
    s = re.sub(r"/{2,}", "/", s)
    if s.lower().startswith("/all/"):
        s = s[len("/all"):]
    if s.lower().startswith("game/"):
        s = "/" + s
    return _fold_dot_segments(s) if fold else s   # ★ 16.09.2026 (ревизия, СРЕДН-2): «..» и «.» в пакетном пути


def _fold_dot_segments(path):
    """★ 16.09.2026 (ревизия задачи 16.09, СРЕДН-2). Сворачивает сегменты «.» и «..» в ПАКЕТНОМ пути.
    БЫЛО: таблицы сверяются посегментно, поэтому «/Game/_Temp/../Map/Login» и «/Game/Map/./Login» — тот же самый
    уровень логина — давали owned=False, и пакет сцены логина проходил мимо защиты (такую форму даёт склейка путей
    «корень + относительный кусок», которой в инструментах хватает).
    За корень не выходим: лишний «..» в начале отбрасывается (как это делают ОС и os.path.normpath), иначе путь
    уехал бы выше /Game и сверка стала бы бессмысленной.
    Строка без таких сегментов возвращается КАК ЕСТЬ — форма, регистр и хвостовой «/» не меняются (прежние формы
    пути, 88 проб раздела 16 и 165 проб раздела 17, отвечают ровно как раньше).
    ★ 16.09.2026 (раздел 21, МЕЛК-1). Сворачивание умеет и ВЫВОДИТЬ путь из-под защиты: «/Game/Map/Login/..» —
    посегментно это папка логина, а после сворачивания «/Game/Map», то есть owned=False. Семантически верно
    (путь указывает на /Game/Map), но защите такой обмен не нужен: направление «было логином — стало не логином»
    для нас единственно опасное. Поэтому решение — fail-closed: is_login_owned_package сверяет ОБЕ формы,
    свёрнутую и исходную, и отвечает «логин», если узнала хотя бы одна. Сворачивание таким образом только
    ДОБАВЛЯЕТ узнаваемых форм («/Game/_Temp/../Map/Login»), но ни одной не теряет.
    Цена — ложное «логин» у форм вида «/Game/Map/Login/../<не логин>»: без разрешения такой пакет будет пропущен
    (сторона безопасная), с ASDA_ALLOW_LOGIN_WRITE=1 поведение не меняется. На реальных данных (1 529 пакетов
    логина, листинг Content) путей с «..»/«.» нет вовсе, так что ложных срабатываний сегодня ноль."""
    if "." not in path:
        return path
    parts = path.split("/")
    if not any(p in (".", "..") for p in parts):
        return path
    lead = "/" if path.startswith("/") else ""
    out = []
    for seg in parts:
        if seg == "" or seg == ".":
            continue
        if seg == "..":
            if out:
                out.pop()   # за корень не выходим: «..» на пустом стеке просто отбрасывается
            continue
        out.append(seg)
    return (lead + "/".join(out)) or lead or "."


def _package_of(path):
    """★ 16.09.2026 (ревизия §7, МЕЛК-2). Путь ПАКЕТА из любой встречающейся формы пути: нормализация
    (_normalize_package_path: ссылка из редактора, дисковый путь, «/All/Game/…», «..»/«.») плюс срез «.Объект»
    у формы «…/Имя.Имя». Регистр СОХРАНЯЕТСЯ (сверка с корнем идёт отдельно, без учёта регистра).
    Нужен там, где пакеты надо сравнивать между собой (лежит ли переданный ассет под корнем), а не только
    сверять с таблицами логина — у _login_owned_normalized тот же срез сделан внутри, но с приведением к
    нижнему регистру и без возврата пути наружу."""
    s = _normalize_package_path(path).replace("\\", "/").rstrip("/")
    last = s.rsplit("/", 1)[-1]
    if "." in last:
        s = s[:len(s) - len(last)] + last.split(".", 1)[0]
    return s


def _login_entry_match(low, entry):
    """Совпадение нормализованного пути (нижний регистр, без «.Объект») с записью LOGIN_OWNED_*: «*» в конце — префикс
    имени, иначе точный пакет/папка или путь внутри неё. (15.09.2026)"""
    el = entry.lower()
    if el.endswith("*"):
        return low.startswith(el[:-1])
    return low == el or low.startswith(el + "/")


def package_blocked_for(tool_file, pkg, quiet=False):
    """`login_package_blocked(pkg, <имя файла tool_file>, quiet)` — гейт сцены логина для пакета pkg от имени инструмента,
    имя берётся из его `__file__` (дедуп №2 В1 Т18, 11.10.2026: было 9 ленивых обёрток `_login_blocked` с прошитым
    именем-литералом в шагах UI/импортёрах JUI). Защиту не меняет: та же проверка, та же строка [LOGIN-GUARD]."""
    return login_package_blocked(pkg, os.path.basename(tool_file), quiet=quiet)


def login_package_blocked(pkg, tool_name=None, quiet=False):
    """True — пакет принадлежит сцене логина, а запись НЕ разрешена (ASDA_ALLOW_LOGIN_WRITE != 1): вызывающий обязан
    его пропустить. quiet=False — строка [LOGIN-GUARD] в лог на каждый пропуск; quiet=True — без строки (итог печатает
    вызывающий)."""
    if not is_login_owned_package(pkg) or login_write_allowed():
        return False
    if not quiet:
        _log("[LOGIN-GUARD] %s: пропущен пакет сцены логина %s (запись — только с явного разрешения владельца и %s=1)"
             % (tool_name or "инструмент", pkg, ALLOW_ENV), warn=True)
    return True


# ★★★ ИМЯ ПАКЕТА, КОТОРОЕ UE ДАЁТ ИМПОРТИРУЕМОМУ ФАЙЛУ (16.09.2026, раздел 20 отчёта Reports/dedup_audit/tex_rebind_2026-09-15.md;
#   правка СРЕДН-1/МЕЛК-1 ревизии к §19). ЗАЧЕМ В ЭТОМ МОДУЛЕ: инструмент импорта обязан спросить защиту про ТОТ ЖЕ путь пакета,
#   который получится у UE. Спросить про «/Game/…/ne_win(2)», когда UE создаст «/Game/…/ne_win_2_», — значит спросить про
#   НЕСУЩЕСТВУЮЩИЙ пакет: пакет сцены логина в такой форме таблицы не узнают, и запись пройдёт мимо защиты.
#   ТОЧНО КАК UE 5.8, UAssetToolsImpl::ImportAssetsInternal (Engine/Source/Developer/AssetTools/Private/AssetTools.cpp:4093-4104;
#   туда приходят обе питоновские точки входа: import_assets_automated → ImportAssetsAutomated (:2952) и
#   import_asset_tasks → ImportAssetTasks (:2984)):
#       Name        = AssetImportTask->DestinationName, если задано, иначе FPaths::GetBaseFilename(Filename)
#       Name        = ObjectTools::SanitizeObjectName(Name)                                   ← INVALID_OBJECTNAME_CHARACTERS
#       PackageName = ObjectTools::SanitizeInvalidChars(FPaths::Combine(DestinationPath, Name),
#                                                      INVALID_LONGPACKAGE_CHARACTERS)        ← ВТОРОЙ проход, по всему пути
#   ObjectTools::SanitizeInvalidCharsInline (Editor/UnrealEd/Private/ObjectTools.cpp:4744) — посимвольная замена КАЖДОГО символа
#   набора на «_» (ReplaceCharInline, с учётом регистра): ничего не удаляется, длина не меняется. «_» не входит ни в один набор,
#   поэтому обе замены идемпотентны.
#   ВТОРОЙ ПРОХОД НУЖЕН: в INVALID_LONGPACKAGE_CHARACTERS есть 5 символов, которых нет в INVALID_OBJECTNAME_CHARACTERS
#   («\», «*», «?», «<», «>») — без него они дошли бы до имени пакета. Обратно: «/» есть только в первом наборе, поэтому
#   разделители пути, которые ставит Combine, второй проход переживают.
#   БЫЛО (15–16.09, import_jui_textures.py): один набор в редакции UE4 — 7 символов «"' ,\n\r\t»; в UE 5.8 их 28.
#   Сверка с живыми данными (раздел 20): на 2 198 PNG RawAssets/Textures/JUI и 12 274 PNG Tools/NormalStudio/ue_import результат
#   совпал 1:1 с именами пакетов в Content (0 расхождений в обе стороны).
UE_INVALID_OBJECTNAME_CHARACTERS = "\"' ,/.:|&!~\n\r\t@#(){}[]=;^%$`"      # NameTypes.h:191 (UE 5.8) — 28 символов
UE_INVALID_LONGPACKAGE_CHARACTERS = "\\:*?\"<>|' ,.&!~\n\r\t@#"            # NameTypes.h:197 (UE 5.8) — 20 символов


def ue_sanitize_invalid_chars(text, invalid_chars):
    """ObjectTools::SanitizeInvalidCharsInline: каждый символ из invalid_chars → «_»."""
    return "".join("_" if c in invalid_chars else c for c in str(text))


def ue_object_name(name):
    """ObjectTools::SanitizeObjectName — первый проход (INVALID_OBJECTNAME_CHARACTERS)."""
    return ue_sanitize_invalid_chars(name, UE_INVALID_OBJECTNAME_CHARACTERS)


def ue_base_filename(path):
    """FPaths::GetBaseFilename(path, bRemovePath=True): имя файла без папки и без ПОСЛЕДНЕГО расширения.
    Повторяет GetBaseFilenameImpl (Core/Private/Misc/Paths.cpp:1006): последняя «.» ищется в уже очищенном имени,
    поэтому «.hidden» даёт пустую строку (у os.path.splitext было бы «.hidden»)."""
    clean = str(path).replace("\\", "/").rsplit("/", 1)[-1]
    i = clean.rfind(".")
    return clean[:i] if i != -1 else clean


def ue_path_append(path, name):
    """★ 16.09.2026 (раздел 21, СРЕДН-1). FString::PathAppend (Core/Private/Containers/String.cpp.inl:855) — склейка
    «путь + имя» ровно так, как её делает UE. Data — буфер FString ВМЕСТЕ с «\\0», поэтому у пустой строки Data.Num()==0,
    и три ветки оригинала читаются так:
      имя пустое   → «/» дописывается, только если путь не пуст и НЕ кончается на «/» либо «\\»;
      путь пустой  → результат РАВЕН имени: ветка `DataNum > 0` не отрабатывает, ведущего «/» не появляется;
      иначе        → «/» ставится, только если путь не кончается на «/» либо «\\» И имя не начинается с «/»;
                     иначе строки склеиваются встык — повторные «//» UE НЕ схлопывает.
    БЫЛО (до 16.09): `str(destination_path).rstrip("/") + "/" + name`. Расходилось с UE на двух формах:
    dest=«/Game/T//» давало «/Game/T/a» вместо «/Game/T//a», пустой dest — «/a» вместо «a». То есть защите
    называли НЕ ТОТ пакет, который создаст UE, — а ради этого функция и существует."""
    s, n = str(path), str(name)
    if n == "":                               # StrLength == 0: только разделитель, и только если его ещё нет
        return s + "/" if s and s[-1] not in "/\\" else s
    if s == "":                               # DataNum == 0: PathAppend копирует имя как есть
        return n
    if s[-1] not in "/\\" and n[0] != "/":
        return s + "/" + n
    return s + n                              # разделитель уже есть с одной из сторон — UE его не дублирует


def ue_import_package_path(destination_path, filename=None, destination_name=None):
    """Путь пакета, который UE создаст при импорте `filename` в папку `destination_path`. `destination_name` — как
    AssetImportTask.destination_name: задано → имя берётся из него, а не из файла (AssetTools.cpp:4094).
    Склейка «папка + имя» — ue_path_append, точное повторение FPaths::Combine: CombineInternal (Core/Private/Misc/Paths.cpp:1878)
    берёт первый путь как есть и добавляет остальные через `operator/=` → FString::PathAppend (String.cpp.inl:855).
    Имя после первого прохода начинаться с «/» не может («/» — в наборе объектных имён), но ветка UE повторена и для него."""
    name = destination_name if destination_name else ue_base_filename(filename or "")
    name = ue_object_name(name)
    return ue_sanitize_invalid_chars(ue_path_append(destination_path, name), UE_INVALID_LONGPACKAGE_CHARACTERS)


def _registry_packages(root):
    """★ 15.09.2026. Пути пакетов под папкой root (рекурсивно) из реестра ассетов UE — умолчание для login_expand_roots.
    Нужен UE; при импорте модуля не вызывается."""
    import unreal
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    # ★ 16.09.2026 (ревизия, СРЕДН-1): полный обход — ОДИН раз за процесс (_SEARCH_DONE), а не на каждый заход.
    #   Он ищет пакеты, которых реестр ещё не знает (появились на диске мимо редактора); созданное в этом же
    #   процессе реестр получает сам. Список пакетов под корнем (get_assets_by_path) при этом берётся СВЕЖИМ
    #   каждый раз — именно он решает, что считать пакетом логина. Сброс — login_cache_reset().
    if not _SEARCH_DONE[0]:
        try:
            ar.search_all_assets(True)   # как C++-обход (SearchAllAssets, синхронно): реестр должен быть полным
        except Exception as e:
            # ★ 15.09.2026 (раздел 16): БЫЛО `pass` — ошибка реестра глоталась, неполный реестр «не видел» пакетов логина под корнем,
            #   и корень уходил в C++-обход целиком. Теперь отказ (fail-closed): разворот останавливается исключением до обхода.
            msg = ("[LOGIN-GUARD] реестр ассетов не обновился (search_all_assets: %s) — корни обхода без полного реестра не отдаю, "
                   "обход остановлен" % e)
            _log(msg, warn=False)
            raise RuntimeError(msg)
        _SEARCH_DONE[0] = True
        CACHE_STATS["search_all"] += 1
    CACHE_STATS["list"] += 1
    return sorted(set(str(ad.package_name) for ad in ar.get_assets_by_path(unreal.Name(root), recursive=True)))


def _expand_login_root(root, items):
    """★ 15.09.2026. ЧИСТОЕ ядро разворота (без UE). root — папка; items — [(путь пакета, принадлежит логину)] пакетов ПОД root.
    Возвращает (папки, россыпь): папки, под которыми нет ни одного пакета логина (их можно обходить целиком), и пакеты
    не логина, лежащие ПРЯМО в папке, под которой пакеты логина есть (рекурсивный обход такой папки их не отделит).
    Папка из одних пакетов логина не даёт ничего."""
    if not any(own for _p, own in items):
        return [root], []
    folders, loose, sub = [], [], {}
    n = len(root) + 1
    for p, own in items:
        rest = p[n:]
        if "/" in rest:
            name = rest.split("/", 1)[0]
            sub.setdefault(name.lower(), (p[:n + len(name)], []))[1].append((p, own))   # регистр папки — как в реестре
        elif not own:
            loose.append(p)
    for key in sorted(sub):
        child, lst = sub[key]
        f, l = _expand_login_root(child, lst)
        folders += f
        loose += l
    return folders, loose


def _expand_root_compute(root, pkgs, pref):
    """★ 16.09.2026 (ревизия, СРЕДН-1). ЧИСТЫЙ расчёт разворота ОДНОГО корня — то, что кэшируется в _EXPAND_MEMO.
    Возвращает (папки, россыпь, причина, сколько пакетов логина). Причина: «clean» — под корнем пакетов логина нет,
    «expanded» — есть и корень развёрнут, «root_is_login» — сам корень принадлежит сцене логина, «registry_empty» —
    реестр не дал под корнем ни одного пакета. Ни строки в лог отсюда не пишется: логи — на каждый заход, в
    login_expand_roots, иначе второй (кэшированный) заход выглядел бы «тихим»."""
    items = [(p, is_login_owned_package(p)) for p in pkgs if p.lower().startswith(pref)]
    n_login = sum(1 for _p, own in items if own)
    if not items:
        return [], [], ("root_is_login" if is_login_owned_package(root) else "registry_empty"), 0
    if not n_login:
        return [], [], "clean", 0
    f, l = _expand_login_root(root, items)
    return f, l, "expanded", n_login


def login_expand_roots(roots, tool_name=None, list_packages=None, reasons=None):
    """★ 15.09.2026 (доработка, раздел 12 отчёта). Корни обхода без пакетов сцены логина — для инструментов, которые передают
    папку целиком (C++ UAsdaMeshMaintenance: FARFilter.PackagePaths рекурсивно, пакеты не принимает). Возвращает (папки, россыпь):
      папки   — корень без пакетов логина под ним — как есть; иначе его подпапки (рекурсивно) до уровня, где пакетов логина нет;
      россыпь — пакеты не логина, лежащие прямо в папке, под которой есть пакеты логина: вызывающий обрабатывает их поштучно
                или сообщает, что не может (login_filter_roots).
    С разрешением (ASDA_ALLOW_LOGIN_WRITE=1) — (корни как есть, []). Принадлежность — is_login_owned_package (обе таблицы
    и LOGIN_SHARED_EXCEPTIONS); класс ассета не учитывается: папка «грязная», если под ней есть ЛЮБОЙ пакет логина.
    list_packages(root) → пути пакетов под root рекурсивно; по умолчанию реестр UE (_registry_packages).
    БЫЛО: корень, содержащий модель логина, пропускался целиком — массовый обход, скажем, `/Game` молча не делал ничего.
    ★ 16.09.2026 (ревизия, СРЕДН-3): reasons — если передать список, в него кладётся по строке на КАЖДЫЙ корень:
    «ok» (корень отдан целиком либо развёрнут), «root_is_login» или «registry_empty». Вызывающему это нужно, чтобы
    отличить две fail-closed причины пустого ответа: первая — сохранять нечего и нечем, вторая — обычный первый
    импорт в пустую папку (save_dir_login_safe сохраняет тогда загруженное поштучно, а не отказывает).
    ★ 16.09.2026 (ревизия, СРЕДН-1): расчёт разворота кэшируется по (корень, отпечаток списка пакетов) — блок
    «КЭШ НА ПРОЦЕСС» выше; список берётся свежим на каждый заход, поэтому пакет сцены логина, созданный этим же
    прогоном, кэш пропустить не может."""
    roots = list(roots)   # ★ 16.09.2026: корни могли прийти генератором — ниже они нужны дважды
    if login_write_allowed():
        if reasons is not None:
            reasons.extend(["ok"] * len(roots))
        return list(roots), []
    lister = list_packages or _registry_packages
    tool = tool_name or "инструмент"
    folders, loose = [], []
    for r in roots:
        root = str(r).replace("\\", "/").rstrip("/")
        pref = root.lower() + "/"
        pkgs = [str(x) for x in lister(root)]
        key = (root, len(pkgs), hash(tuple(pkgs)))   # отпечаток списка: изменился состав — кэш не сработает
        got = _EXPAND_MEMO.get(key)
        if got is None:
            CACHE_STATS["expand_miss"] += 1
            got = _expand_root_compute(root, pkgs, pref)
            _EXPAND_MEMO[key] = got
        else:
            CACHE_STATS["expand_hit"] += 1
        f, l, why, n_login = got
        if reasons is not None:
            reasons.append("ok" if why in ("clean", "expanded") else why)
        if why == "root_is_login":
            _log("[LOGIN-GUARD] %s: корень %s — сцена логина, пропущен (%s=1)" % (tool, r, ALLOW_ENV), warn=True)
            continue
        if why == "registry_empty":   # ★ 15.09.2026 (раздел 16): под корнем реестр пуст (не готов, чужая форма пути) — целиком в обход НЕ отдаём
            #   (fail-closed: C++ со своим SearchAllAssets нашёл бы там и пакеты логина). БЫЛО: такой корень уходил как есть.
            _log("[LOGIN-GUARD] %s: под корнем %s реестр не дал ни одного пакета — корень в обход не отдан (fail-closed)" % (tool, r),
                 warn=True)
            continue
        if why == "clean":
            folders.append(r)
            continue
        _log("[LOGIN-GUARD] %s: корень %s содержит пакеты сцены логина (%d) — развёрнут без них: папок %d, пакетов поштучно %d "
             "(логин — только с %s=1)" % (tool, r, n_login, len(f), len(l), ALLOW_ENV), warn=True)
        folders += f
        loose += l
    return folders, loose


def login_filter_roots(roots, tool_name=None, list_packages=None):
    """★ 15.09.2026. Корни обхода без пакетов сцены логина — ТОЛЬКО папки (login_expand_roots). С разрешением — корни как есть.
    ★ Доработка 15.09: корень, содержащий пакеты логина, разворачивается, а не пропускается целиком. Пакеты «россыпи» папкой
    не передать — они НЕ обрабатываются, о чём пишется строка [LOGIN-GUARD] (число и первые пути). Инструмент, умеющий
    поштучную обработку, зовёт login_expand_roots."""
    folders, loose = login_expand_roots(roots, tool_name, list_packages)
    if loose:
        _log("[LOGIN-GUARD] %s: %d пакетов лежат прямо в папках с пакетами логина — обходом по папкам их не отделить, НЕ "
             "обработаны: %s%s" % (tool_name or "инструмент", len(loose), ", ".join(loose[:10]), " …" if len(loose) > 10 else ""),
             warn=True)
        # ★ 16.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 18): полный разбор НЕ обработанного по папкам — у плоской смешанной папки
        #   (Textures/JUI: 7 атласов логина среди 2 198) строка выше показала бы только 10 путей из 2 191.
        by_dir = {}
        for p in loose:
            d = p.rsplit("/", 1)[0]
            by_dir[d] = by_dir.get(d, 0) + 1
        _log("[LOGIN-GUARD] %s: НЕ обработанные пакеты по папкам (%d): %s" % (tool_name or "инструмент", len(by_dir),
             ", ".join("%s — %d" % (d, n) for d, n in sorted(by_dir.items()))), warn=True)
    return folders


def save_dir_login_safe(root, tool_name=None, only_if_is_dirty=True, list_packages=None, loaded_assets=None):
    """★ 16.09.2026 (раздел 22, Reports/dedup_audit/savedir_guard_2026-09-16.md). ЗАМЕНА вызова
    EditorAssetLibrary.save_directory(root, recursive=True) везде, где под папкой МОГУТ лежать пакеты сцены логина.

    ЗАЧЕМ. Сохранение папки целиком — отдельный класс дыры (образец разобран в разделе 21.3): поштучная защита
    отсеивает пакеты логина при импорте и правке, а идущий следом save_directory по ТОЙ ЖЕ папке пересохраняет их
    всё равно, мимо всякой проверки. С only_if_is_dirty=False пересохраняется даже нетронутое.

    ЧТО ДЕЛАЕТ. login_expand_roots отдаёт (папки, под которыми пакетов логина нет; россыпь пакетов не логина,
    лежащих прямо в смешанной папке). Папки сохраняются целиком — как раньше, одним вызовом; россыпь — поштучно
    save_asset. С разрешением владельца (ASDA_ALLOW_LOGIN_WRITE=1) разворот возвращает сам корень, то есть ровно
    один прежний save_directory(root): обычный разрешённый прогон не меняется ничем.
    Возвращает (сколько папок сохранено, сколько пакетов сохранено поштучно).

    ★ FAIL-CLOSED, НО НЕ МОЛЧА. У login_expand_roots есть ветки, где корень пропускается ЦЕЛИКОМ (сам корень —
    сцена логина; реестр не дал под корнем ни одного пакета). Тогда не сохранено НИЧЕГО, и промолчать об этом нельзя:
    вызывающий счёл бы прогон успешным, а правки остались бы грязными в памяти редактора и пропали при выходе.
    Поэтому — RuntimeError с текстом, что именно не сохранено и чем лечить. Решение то же, что у локальной
    save_dir_login_safe в Reports/char_bind/ue_reimport_char_bind.py (поправка МЕЛК-5 ревизии 16.09); здесь оно
    вынесено в общий модуль, чтобы новые инструменты не повторяли ни шаблон дыры, ни эту поправку.

    ★ 16.09.2026 (ревизия, СРЕДН-3). loaded_assets — ассеты (объекты UE или пути пакетов), которые вызывающий
    ТОЛЬКО ЧТО загрузил/импортировал. Нужны для одной-единственной ветки: реестр под корнем пуст. Это не поломка,
    а обычный ПЕРВЫЙ импорт в новую папку — реестр её ещё не знает; отказывать здесь нельзя (импортированное
    осталось бы не сохранённым), но и сохранять папку целиком нельзя (ровно тот шаблон, ради которого функция и
    писалась). Поэтому: сохраняем поштучно ровно то, что передали, каждый пакет — через login_package_blocked.
    БЫЛО: своя запасная ветка у import_jui_textures.py (третья копия правила) — теперь их одна, здесь.
    Отказ (RuntimeError) остаётся там, где сохранить действительно нечего и нечем: корень целиком — сцена логина,
    вызывающий ничего не передал, либо реестр недоступен (тогда исключение приходит ещё из _registry_packages).

    ★ 16.09.2026 (ревизия §7, МЕЛК-2) — ветка loaded_assets приведена к ОБЩЕМУ КОНТРАКТУ функции.
    БЫЛО две дыры в контракте, обе кончались «успешным» прогоном, в котором не сохранено НИЧЕГО:
      (1) все переданные пакеты оказались пакетами сцены логина → сохранено 0, но возвращалось (0, 0) и лишь
          предупреждение. Вызывающий (n_dirs, n_pkgs = …) счёл бы это успехом, а правки остались бы грязными
          в памяти редактора и пропали при выходе — ровно то, из-за чего в этой же функции стоит fail-closed
          RuntimeError для пустого разворота. Теперь и здесь RuntimeError: «сохранить нечего и нечем».
      (2) переданные пакеты НЕ проверялись на принадлежность корню. Контракт функции — «сохрани то, что лежит
          под этим корнем»: под корнем считался разворот, по корню печатается итог, корнем же названа причина
          registry_empty. Пакет вне корня — ошибка вызывающего (перепутан корень, склеен не тот путь), и
          сохранять его тихо нельзя: прогон отчитался бы по одному корню, а записал бы в другой, мимо разворота
          и его логов. Выбран ОТКАЗ, а не предупреждение: проверка идёт ДО единой записи, поэтому ничего не
          теряется и не пишется наполовину, вызывающий чинит вызов и повторяет; «предупредить и сохранить»
          означало бы молча расширить охват записи, «предупредить и пропустить» — потерять несохранённое
          (пункт 1). Сегодня под правило не подпадает ни один вызов: import_jui_textures.py передаёт то, что
          сам импортировал в DEST, ue_reimport_char_bind.py — вовсе не передаёт loaded_assets."""
    import unreal
    tool = tool_name or "инструмент"
    why = []
    folders, loose = login_expand_roots([root], tool, list_packages, reasons=why)
    if not folders and not loose:
        if why[:1] == ["registry_empty"] and loaded_assets:
            # ★ 16.09.2026 (ревизия §7, МЕЛК-2): СНАЧАЛА проверка «все переданные пакеты лежат под корнем», и
            #   только потом запись — чтобы отказ не оставил половину сохранённой (разбор в докстроке).
            root_pkg = _package_of(root)
            rl = root_pkg.lower()
            pairs = [(a, _package_of(a if isinstance(a, str) else a.get_path_name())) for a in loaded_assets]
            outside = [p for _a, p in pairs if not (p.lower() == rl or p.lower().startswith(rl + "/"))]
            if outside:
                msg = ("[LOGIN-GUARD] %s: корень %s НЕ сохранён — loaded_assets содержит %d пакет(ов) ВНЕ этого "
                       "корня: %s%s. Функция сохраняет то, что лежит под переданным корнем; пакет из другой папки "
                       "она записала бы мимо разворота и мимо отчёта по корню. Ничего не сохранено (проверка идёт "
                       "до первой записи). Лечение: передавать корень, в который шёл импорт, либо звать "
                       "save_dir_login_safe отдельно на каждый корень."
                       % (tool, root, len(outside), ", ".join(outside[:5]), " …" if len(outside) > 5 else ""))
                _log(msg, warn=False)
                raise RuntimeError(msg)
            n = 0
            for a, p in pairs:
                if login_package_blocked(p, tool):
                    continue
                if isinstance(a, str):
                    # ★ 16.09.2026 (ревизия savedir3, МЕЛК-4): сохраняем НОРМАЛИЗОВАННЫЙ путь p, а не исходную
                    #   строку a. Принадлежность корню сверена по p, и запись обязана идти по тому же пути:
                    #   форма «/All/Game/…» (путь из Content Browser) уходила в save_asset как есть и не находила
                    #   пакет — прогон отчитался бы об успехе, не сохранив ничего.
                    unreal.EditorAssetLibrary.save_asset(p, only_if_is_dirty=only_if_is_dirty)
                else:
                    unreal.EditorAssetLibrary.save_loaded_asset(a, only_if_is_dirty=only_if_is_dirty)
                n += 1
            if not n:
                # ★ 16.09.2026 (ревизия §7, МЕЛК-2): ВСЁ переданное — пакеты сцены логина, сохранено 0. Это тот же
                #   случай, что пустой разворот: сохранять нечего и нечем, а молчаливое (0, 0) вызывающий принял бы
                #   за успешный прогон.
                msg = ("[LOGIN-GUARD] %s: корень %s НЕ сохранён — реестр не дал под ним ни одного пакета, а все %d "
                       "переданных loaded_assets принадлежат сцене логина (запись в них — только с %s=1). Сохранено "
                       "НОЛЬ; правки остались НЕ сохранёнными, молча пропустить это нельзя. Лечение: запускать с "
                       "явного разрешения владельца %s=1 либо не гнать через эту папку ничего, кроме логина."
                       % (tool, root, len(pairs), ALLOW_ENV, ALLOW_ENV))
                _log(msg, warn=False)
                raise RuntimeError(msg)
            _log("[LOGIN-GUARD] %s: под корнем %s реестр не дал ни одного пакета (первый импорт в новую папку) — "
                 "сохранено поштучно загруженного: %d из %d, остальное — пакеты сцены логина"
                 % (tool, root, n, len(loaded_assets)), warn=True)
            return 0, n
        msg = ("[LOGIN-GUARD] %s: корень %s НЕ сохранён — login_expand_roots не отдал ни папок, ни пакетов "
               "(fail-closed: корень целиком принадлежит сцене логина ЛИБО реестр ассетов не дал под ним ни одного "
               "пакета). Правки остались НЕ сохранёнными; молча пропустить это нельзя. Лечение: убедиться, что реестр "
               "полон (search_all_assets) и корень верный, либо запускать с явного разрешения владельца %s=1."
               % (tool, root, ALLOW_ENV))
        _log(msg, warn=False)
        raise RuntimeError(msg)
    for f in folders:
        unreal.EditorAssetLibrary.save_directory(f, only_if_is_dirty=only_if_is_dirty, recursive=True)
    for p in loose:
        unreal.EditorAssetLibrary.save_asset(p, only_if_is_dirty=only_if_is_dirty)
    return len(folders), len(loose)


def login_map_write_blocked(level, tool_name=None, quiet=False):
    """★ 16.09.2026 (ревизия раздела 22, СРЕДН-2). True — путь `level` принадлежит сцене логина, а
    ASDA_ALLOW_LOGIN_MAP_WRITE=1 не задана: вызывающий обязан НЕ трогать этот уровень (выйти из main, пропустить карту).

    ЗАЧЕМ ОТДЕЛЬНАЯ ПРОВЕРКА И ОТДЕЛЬНАЯ ПЕРЕМЕННАЯ. Инструменты карт берут имя карты из ASDA_MAP, а оно НИ С ЧЕМ
    не сверяется (asda_maps.map_asset_name только чистит недопустимые символы, реестр карт не спрашивается).
    ASDA_MAP=Login у инструмента, который откатывается на уровень без подпапки (asda_convert_map_lit: нет
    /Game/Map/<A>/<A> → LVL=/Game/Map/<A>), даёт САМ уровень владельца: load_level + save_current_level + удаление
    <уровень>_BuiltData. У asda_build_sky_dome тем же ключом создавались бы пакеты ПОД деревом логина
    (/Game/Map/<A>/Models, /Game/Map/<A>/Materials) — там проверка уровня стоит уже ПОСЛЕ их создания.
    ASDA_ALLOW_LOGIN_WRITE для этого мало по замыслу: ею открывают АССЕТЫ сцены логина (модели, текстуры, MIC), а
    сам уровень со светом владельца — отдельное решение и отдельный ключ, тот же, которого требует C++-сторож
    AsdaLoginSaveGuard в автоматической сессии (раздел 17, login_save_guard.md п. 6.3).

    Принадлежность — is_login_owned_package (не сравнение с литералом «/Game/Map/Login»: по уроку false-negatives-method
    литерал не узнаёт ни /Game/Map/Login/<что угодно>, ни другие пакеты логина, ни иные формы записи пути)."""
    if not is_login_owned_package(level):
        return False
    tool = tool_name or "инструмент"
    if os.environ.get(MAP_ALLOW_ENV, "") == "1":
        if not quiet:
            _log("[LOGIN-GUARD] %s: работа с уровнем сцены логина %s РАЗРЕШЕНА (%s=1). Свет владельца (2 SpotLight, "
                 "DirectionalLight, SkyLight) не трогать; сверить с защищённой копией после прогона."
                 % (tool, level, MAP_ALLOW_ENV), warn=True)
        return False
    if not quiet:
        _log("[LOGIN-GUARD] %s ОСТАНОВЛЕН на %s: это сцена логина, в её уровне стоит свет владельца. Работа с уровнем "
             "логина (load_level / save_current_level / удаление BuiltData / создание пакетов под /Game/Map/Login) — "
             "только с явного разрешения владельца и %s=1. Имя карты приходит из ASDA_MAP и ни с чем не сверяется, "
             "поэтому проверка стоит здесь, ДО первой записи. (%s открывает ассеты сцены логина, но НЕ сам уровень.)"
             % (tool, level, MAP_ALLOW_ENV, ALLOW_ENV), warn=False)
    return True


# ★★★ ИМЯ КАРТЫ ИЗ ASDA_MAP — СВЕРКА С РЕЕСТРОМ КАРТ (16.09.2026, ревизия §7, п. 5; §6 п. 3 и §7.7 п. 2 отчёта
#   Reports/dedup_audit/savedir_guard_2026-09-16.md). Имя карты приходит из окружения и НИ С ЧЕМ не сверялось:
#   asda_maps.map_asset_name только заменяет недопустимые символы на «_», поэтому ASDA_MAP=alpne (опечатка) молча
#   давал корень /Game/Map/alpne, а инструменты создают под ним папки и ассеты (Models, Materials, ScrollMat,
#   MapEffect_*) — мусор в Content, который потом никто не связывает с опечаткой.
#   ЕДИНЫЙ ИСТОЧНИК ИМЁН — Tools/data/asda_map_registry.json (тот же, из которого генерируется asda_maps.py; см.
#   Tools/asda_map_registry.py). Читается ЭТОТ файл, а не сгенерированный asda_maps: список карт в проекте один,
#   и сверять имя надо с источником. Модуль защиты выбран домом для проверки по двум причинам: (1) его уже
#   импортируют все инструменты с ASDA_MAP — гейт сцены логина стоит ровно в тех же строках, новых импортов не
#   появляется; (2) дыра та же по природе — «инструмент пишет не туда, потому что имя карты никто не проверил».
MAP_REGISTRY_JSON = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "asda_map_registry.json")
# Имя карты ВНЕ реестра разрешает эта переменная: временные и тестовые карты (L_LightTest сета E-5b, карта,
# которую ещё не внесли в реестр, разовый эксперимент). Явный ключ, а не молчаливый пропуск: в логе видно, что
# работали по имени вне реестра.
MAP_UNKNOWN_ENV = "ASDA_MAP_ALLOW_UNKNOWN"
_MAP_NAMES = [None]          # множество имён реестра в нижнем регистре (ленивo, один раз за процесс)
_MAP_NAMES_ERROR = [None]    # текст ошибки чтения реестра либо None
_MAP_WARNED = set()          # о каком имени/ошибке уже написано (строка на имя, а не на каждый заход)


def _load_map_names(path=MAP_REGISTRY_JSON):
    """★ 16.09.2026. Допустимые имена карт из реестра → (множество в нижнем регистре, ошибка|None).
    Берутся ВСЕ формы, которыми карту называют инструменты: dir (имя папки клиента), display, folder и имя
    ассета от каждой из них (map_asset_name: не [A-Za-z0-9_] → «_», отсюда «Mountain Flamion» и
    «Mountain_Flamion» — обе допустимы), плюс имена уровней из cook_extra_levels (Login, L_Loading — карты
    не из списка maps, но ASDA_MAP=Login/L_Loading инструментам передают штатно).
    Не бросает и в лог не пишет: решение принимает map_name_blocked."""
    try:
        import json
        with open(path, "r", encoding="utf-8") as f:
            reg = json.load(f)
        names = set()
        for m in (reg.get("maps") or []):
            if not isinstance(m, dict):
                continue
            for k in ("dir", "display", "folder"):
                v = m.get(k)
                if isinstance(v, str) and v:
                    names.add(v.lower())
                    names.add(re.sub(r"[^A-Za-z0-9_]", "_", v).lower())   # тот же закон, что asda_maps.map_asset_name
        for lvl in (reg.get("cook_extra_levels") or []):
            if isinstance(lvl, str) and lvl:
                names.add(str(lvl).replace("\\", "/").rstrip("/").rsplit("/", 1)[-1].lower())
        if not names:
            raise ValueError("в реестре нет ни одного имени карты")
        return names, None
    except Exception as e:
        return set(), "%s: %s" % (type(e).__name__, e)


def map_name_blocked(map_or_path, tool_name=None, quiet=False):
    """★ 16.09.2026 (ревизия §7, п. 5). True — имени карты НЕТ в реестре карт и нет разрешения
    ASDA_MAP_ALLOW_UNKNOWN=1: вызывающий обязан остановиться ДО создания чего-либо под /Game/Map/<имя>.

    ЧТО ПРИНИМАЕТ. Либо само имя карты («alpen», «Mountain Flamion»), либо любой путь вида
    «/Game/Map/<имя>/…» — проверяется первый сегмент после /Game/Map. Так проверку можно поставить ровно в ту же
    строку, где уже стоит гейт сцены логина, и передать ей ТОТ ЖЕ аргумент (корень карты или путь уровня).
    Путь вне /Game/Map (явный уровень из ASDA_LIGHT_LEVELS/ASDA_LEVELS, /Game/Temp/L_LightTest) эта проверка НЕ
    судит — там имени карты нет вовсе; сцену логина такие пути проходят через login_map_write_blocked, которая
    стоит рядом и от реестра не зависит.
    «ALL» (обход всех карт) пропускается — но ТОЛЬКО как голое имя режима, без слэшей: путь «/Game/Map/ALL»
    режимом не является (★ 16.09.2026, ревизия savedir7, СРЕДН-2). Сегодня эта ветка недостижима: все 14 мест
    вызова передают ПОСТРОЕННЫЙ путь, а режим ALL разбирается у вызывающего до гейта. Ветка оставлена страховкой
    для будущего вызывающего, который передаст имя.
    Пустое имя — блокируется: «/Game/Map//…» или ASDA_MAP='' это опечатка в чистом виде.

    ПОЧЕМУ ЗДЕСЬ FAIL-OPEN (реестр не прочитан → предупреждение и работаем), в отличие от списка пакетов логина,
    где fail-closed. Это РАЗНЫЕ по цене ошибки. Список пакетов логина решает, сохранится ли свет владельца:
    не прочитали — считаем логином всё, потерять нельзя. Реестр карт ловит ОПЕЧАТКУ, и защита сцены логина от
    него не зависит ни одной строкой (гейт логина отдельный и стоит раньше). Если бы нечитаемый реестр
    останавливал инструменты, одна испорченная строка JSON положила бы всю работу по картам — цена ошибки
    несоразмерна пользе. Поэтому: не прочитан → строка [MAP-REGISTRY] в лог (один раз за процесс) и пропуск."""
    tool = tool_name or "инструмент"
    raw = str(map_or_path or "").replace("\\", "/").strip().rstrip("/")
    # ★ 16.09.2026 (ревизия savedir3, МЕЛК-2): формы пути приводятся к «/Game/…» тем же _package_of, что и везде
    #   в модуле. БЫЛО: «/All/Game/Map/alpne» (путь папки из Content Browser) уходил мимо проверки по ветке
    #   «начинается с / и не /Game/Map», а «alpen.alpen» (форма с объектом) блокировался как неизвестная карта.
    # ★ 16.09.2026 (ревизия savedir6, СРЕДН-1). Срез «.Объект» — свойство ПУТИ пакета, а не имени карты.
    #   Для голого имени из ASDA_MAP его применять нельзя: 'alpen.alpen' — это НЕ карта alpen, инструмент
    #   построит из него папку 'alpen_alpen' (asda_maps.map_asset_name: точка → «_»).
    name = _package_of(raw) if ("/" in raw or ":" in raw) else raw
    # ★ 16.09.2026 (доводка СРЕДН-1). Пустой первый сегмент виден ТОЛЬКО в сырой строке: нормализация схлопывает
    #   «//», и «/Game/Map//Models/SM_skydome» (так выглядит путь у asda_build_sky_dome при ASDA_MAP='') стало бы
    #   «/Game/Map/Models» — блокировка верная, но причина чужая («карты Models в реестре нет»), и владелец чинил
    #   бы не то. Сверяем до склейки, регистр не важен; форма «/All/Game/Map//…» тоже учтена.
    _r = raw[len("/all"):] if raw.lower().startswith("/all/") else raw
    if _r.lower().startswith("/game/map//"):
        name = ""
    if name.lower().startswith("/game/map/"):
        name = name[len("/game/map/"):].split("/", 1)[0]
    elif name.lower() == "/game/map":
        # ★ 16.09.2026 (ревизия savedir4, СРЕДН-1). Путь схлопнулся в САМ корень: это «/Game/Map/» + пустое имя
        #   карты (и «/Game/Map//…» — двойной слэш сворачивает нормализация). Имя пустое → падаем в проверку ниже.
        #   БЫЛО: такой путь уходил по ветке «не под /Game/Map» с ответом «не сужу», и пустое ASDA_MAP ловилось
        #   только у тех четверых вызывающих, кто передаёт ИМЯ карты; восемь передают путь — и писали бы прямо
        #   в корень Content\Map, где лежат Login.umap и три ассета света владельца (самый дорогой случай —
        #   asda_build_sky_dome: create_asset + save_loaded_asset ДО проверки уровня).
        #   Легитимного вызова с корнем нет: все 12 строят путь как «/Game/Map/<имя>/…».
        name = ""
    elif name and _r.lower().startswith("/game/map/") and name.lower() != "/game/map":
        # ★ 16.09.2026 (ревизия savedir5, МЕЛК-2). Сырой путь шёл ПОД /Game/Map, а после сворачивания «..»
        #   оказался ВЫШЕ («/Game/Map/..» → «/Game»). Имени карты в нём нет, но и «не сужу» отвечать нельзя:
        #   такую форму даёт только склейка корня с относительным куском, то есть ошибка вызывающего.
        #   ★ `name and` обязательно: у форм с пустым именем («/All/Game/Map//Models/…») сырая сверка выше уже
        #   поставила name="", и без этого условия они получали бы ЧУЖУЮ причину вместо «имя карты ПУСТОЕ».
        if not quiet:
            _log("[MAP-REGISTRY] %s ОСТАНОВЛЕН: путь %r выходит ВЫШЕ /Game/Map (сегменты «..» после склейки "
                 "дают %s). Имени карты в нём нет; инструмент работал бы не в той папке. Лечение: передавать "
                 "путь под /Game/Map/<имя карты> либо само имя." % (tool, map_or_path, name), warn=False)
        return True
    elif name.startswith("/"):
        return False                      # путь не под /Game/Map — имени карты в нём нет
    if name.upper() == "ALL" and "/" not in raw:
        # ★ 16.09.2026 (ревизия savedir7, СРЕДН-2). «ALL» пропускается ТОЛЬКО как голое имя режима — у
        #   asda_convert_map_lit и asda_audit_local_lights он есть, и имена там берутся из самого реестра.
        #   Путь с сегментом ALL («/Game/Map/ALL») режимом не является: у asda_build_map_level и
        #   asda_build_map_anim режима ALL нет вовсе, и привычка владельца написать ASDA_MAP=ALL создала бы
        #   там мусорную папку /Game/Map/ALL с delete_directory и create_asset внутри.
        return False
    if not name and _r.lower().startswith("/game/map/") and "//" not in _r:
        # ★ 16.09.2026 (ревизия savedir6, МЕЛК-1). «/Game/Map/../..» сворачивается в «/»: имя пусто, но это НЕ
        #   пустое ASDA_MAP, а выход выше корня на ДВА уровня (ветка выше ловит только один, там ещё остаётся
        #   «/Game»). Причина обязана совпадать с настоящей ошибкой, иначе владелец чинит не то.
        if not quiet:
            _log("[MAP-REGISTRY] %s ОСТАНОВЛЕН: путь %r выходит ВЫШЕ /Game/Map (после сворачивания «..» от него "
                 "не остаётся даже корня карт). Имени карты в нём нет; инструмент работал бы не в той папке. "
                 "Лечение: передавать путь под /Game/Map/<имя карты> либо само имя." % (tool, map_or_path),
                 warn=False)
        return True
    if not name:
        # ★ 16.09.2026 (ревизия savedir3, МЕЛК-3): пустое имя — стоп БЕЗУСЛОВНО, выше ASDA_MAP_ALLOW_UNKNOWN=1.
        #   Тем ключом разрешают КОНКРЕТНУЮ временную карту, которой ещё нет в реестре; пустая строка не карта
        #   вовсе (ASDA_MAP='' или «/Game/Map//…»), и разрешать её нечем — инструмент писал бы в корень /Game/Map.
        if not quiet:
            _log("[MAP-REGISTRY] %s ОСТАНОВЛЕН: имя карты ПУСТОЕ (%r). Это опечатка в чистом виде — ASDA_MAP не "
                 "задана либо в пути двойной «/»; инструмент работал бы прямо в корне /Game/Map. Лечение: задать "
                 "имя карты из реестра %s." % (tool, map_or_path, MAP_REGISTRY_JSON), warn=False)
        return True
    if _MAP_NAMES[0] is None:
        _MAP_NAMES[0], _MAP_NAMES_ERROR[0] = _load_map_names()
    if _MAP_NAMES_ERROR[0]:
        if not quiet and "!err" not in _MAP_WARNED:
            _MAP_WARNED.add("!err")
            _log("[MAP-REGISTRY] %s: реестр карт %s не прочитан (%s) — имя карты %r НЕ сверено (проверка опечаток "
                 "выключена; защита сцены логина от неё не зависит и работает)"
                 % (tool, MAP_REGISTRY_JSON, _MAP_NAMES_ERROR[0], name), warn=True)
        return False
    # ★ 16.09.2026 (ревизия savedir6, СРЕДН-1). Сверяем ИМЕННО ту форму, которой инструмент назовёт папку:
    #   asda_maps.map_asset_name заменяет всё, кроме [A-Za-z0-9_], на «_». Реестр держит обе формы каждого
    #   имени (_load_map_names применяет тот же закон), поэтому «Mountain Flamion» и «Mountain_Flamion» проходят,
    #   а 'alpen.alpen' → 'alpen_alpen' — нет. БЫЛО: сверялось имя ПОСЛЕ среза «.Объект», то есть гейт одобрял
    #   одно имя, а запись шла в другую папку — ровно тот мусор, ради которого гейт и ставился.
    if re.sub(r"[^A-Za-z0-9_]", "_", name).lower() in _MAP_NAMES[0]:
        return False
    if os.environ.get(MAP_UNKNOWN_ENV, "") == "1":
        if not quiet and name.lower() not in _MAP_WARNED:
            _MAP_WARNED.add(name.lower())
            _log("[MAP-REGISTRY] %s: карты %r в реестре нет, но разрешено явно (%s=1) — работаю по /Game/Map/%s "
                 "(временная/тестовая карта; внести в Tools/data/asda_map_registry.json, если она остаётся)"
                 % (tool, name, MAP_UNKNOWN_ENV, name), warn=True)
        return False
    if not quiet:
        _log("[MAP-REGISTRY] %s ОСТАНОВЛЕН: карты %r в реестре карт нет (%s). Похоже на опечатку в ASDA_MAP: "
             "инструмент создал бы папки и ассеты под /Game/Map/%s — мусор, который потом не связать с опечаткой. "
             "Лечение: верное имя карты (реестр — единый источник, генерирует asda_maps.py), либо внести карту в "
             "реестр (Tools/asda_map_registry.py, шапка «КАК ДОБАВИТЬ КАРТУ»), либо разовый запуск с %s=1."
             % (tool, name, MAP_REGISTRY_JSON, name, MAP_UNKNOWN_ENV), warn=False)
    return True
