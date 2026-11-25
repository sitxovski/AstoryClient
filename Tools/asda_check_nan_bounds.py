# Copyright (c) 2026 @sitxovski (Discord)
# asda_check_nan_bounds.py — БЫСТРАЯ ПРОВЕРКА без кука: грузит указанные StaticMesh'и и печатает
#   для каждого габариты. Сама загрузка ассета заставляет движок поднять рендер-данные из DDC
#   (FStaticMeshRenderData::Serialize) — если в записи DDC лежат NaN-габариты, движок тут же
#   выдаёт «found NaN in Bounds of ...» в тот же лог. Т.е. по логу видно, вылечен меш или нет,
#   не гоняя полный кук.
# Запуск: headless, ASDA_LIST=<файл со списком пакетов>.
import os, unreal

def log(m): unreal.log("[check_nan] " + str(m))

def _pkg(path):
    head, _, tail = path.rpartition("/")
    return head + "/" + tail.split(".")[0] if head else path

def main():
    lst = os.environ.get("ASDA_LIST", "")
    if not (lst and os.path.isfile(lst)):
        log("нужен ASDA_LIST=<файл со списком пакетов>"); return
    pkgs = [_pkg(l.strip()) for l in open(lst, "r", encoding="utf-8") if l.strip()]
    log("проверяю %d ассетов" % len(pkgs))
    for p in pkgs:
        sm = unreal.load_asset(p)
        if not isinstance(sm, unreal.StaticMesh):
            log("%s: не StaticMesh" % p); continue
        b = sm.get_bounds()
        log("%s r=%.3f" % (p, b.sphere_radius))
    log("проверка окончена")

if __name__ == "__main__":
    main()
