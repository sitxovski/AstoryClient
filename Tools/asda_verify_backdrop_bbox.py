# Copyright (c) 2026 @sitxovski (Discord)
# Сверка: мировой bbox декомпозированных BackdropElem_* vs старый скелетный LoginBackdrop_OLD.
# Совпадение = раскладка/оси декомпозиции верны (без визуала).
import unreal
LEVEL = "/Game/Map/Login"
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

def acc(actor, mn, mx):
    o = actor.get_actor_bounds(only_colliding_components=False)
    org, ext = o[0], o[1]
    for i,ax in enumerate(('x','y','z')):
        lo = getattr(org,ax)-getattr(ext,ax); hi = getattr(org,ax)+getattr(ext,ax)
        mn[i]=min(mn[i],lo); mx[i]=max(mx[i],hi)

INF=1e18
omn=[INF]*3; omx=[-INF]*3

def fmt(mn,mx): return "min(%.0f,%.0f,%.0f) max(%.0f,%.0f,%.0f) size(%.0f,%.0f,%.0f)"%(
    mn[0],mn[1],mn[2],mx[0],mx[1],mx[2],mx[0]-mn[0],mx[1]-mn[1],mx[2]-mn[2])


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)
    emn=[INF]*3; emx=[-INF]*3; nel=0
    for a in eas.get_all_level_actors():
        lbl=a.get_actor_label()
        if lbl.startswith("BackdropElem"): acc(a,emn,emx); nel+=1
        elif lbl=="LoginBackdrop_OLD": acc(a,omn,omx)
    print("ЭЛЕМЕНТЫ (%d): %s"%(nel,fmt(emn,emx)))
    print("СТАРЫЙ SKEL : %s"%fmt(omn,omx))
    # насколько центры/размеры совпадают
    ec=[(emn[i]+emx[i])/2 for i in range(3)]; oc=[(omn[i]+omx[i])/2 for i in range(3)]
    es=[emx[i]-emn[i] for i in range(3)]; os_=[omx[i]-omn[i] for i in range(3)]
    dc=max(abs(ec[i]-oc[i]) for i in range(3)); ds=max(abs(es[i]-os_[i]) for i in range(3))
    print("Δцентр=%.0f Δразмер=%.0f → %s"%(dc,ds,"СОВПАДАЕТ (оси верны)" if dc<500 and ds<2000 else "РАСХОДИТСЯ (проверить оси/трансформ)"))
    print("=== DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
