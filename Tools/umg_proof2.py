# Copyright (c) 2026 @sitxovski (Discord)
# umg_proof2.py — proof через C++ хелпер UAsdaUmgBuilder: создать WBP с Image/Button/Text.
import unreal
def log(m): unreal.log("[umg_proof2] " + str(m))
# ★ D5 (волна 2 п. 7, 14.09.2026, сверка с Content): папки /Game/AsdaAssets/UI/Generated нет; путь не менялся.
PKG="/Game/AsdaAssets/UI/Generated"; NAME="WBP_UmgProof2"; FULL=PKG+"/"+NAME
B = unreal.AsdaUmgBuilder


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    if unreal.EditorAssetLibrary.does_asset_exist(FULL):
        unreal.EditorAssetLibrary.delete_asset(FULL)
    bp = B.create_widget_bp(PKG, NAME)
    if not bp: log("FAIL create"); raise SystemExit
    canvas = B.get_root_canvas(bp)
    log("canvas: "+str(canvas))
    # тестовая текстура n_02
    tex = unreal.load_asset("/Game/AsdaAssets/Textures/JUI/n_02.n_02")
    img = B.add_image(bp, canvas, "Img1", unreal.Vector2D(100,100), unreal.Vector2D(200,60),
                      tex, unreal.Box2D(unreal.Vector2D(0.0,0.0), unreal.Vector2D(0.5,0.2)),
                      unreal.LinearColor(1,1,1,1), False, unreal.Margin(0,0,0,0))
    btn = B.add_button(bp, canvas, "Btn1", unreal.Vector2D(100,200), unreal.Vector2D(160,40))
    txt = B.add_text(bp, canvas, "Txt1", unreal.Vector2D(110,210), unreal.Vector2D(140,24),
                     "Проверка UMG", 11, unreal.LinearColor(0.8,0.8,0.8,1), 1)
    log("img="+str(img)+" btn="+str(btn)+" txt="+str(txt))
    B.compile_and_save(bp)
    ok = unreal.EditorAssetLibrary.save_loaded_asset(bp)
    log("saved="+str(ok)+" -> "+FULL)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
