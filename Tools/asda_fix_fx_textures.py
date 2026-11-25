# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_fx_textures.py — качество ВСЕХ текстур эффектов (аудит 2026-07-27, прецедент форджинга):
#   мелкие авторские текстуры (128px) с DXT-сжатием + мипами + стримингом давали «ступени»/мыло на
#   дистанции. Фикс: RGBA8 без сжатия + NoMipmaps + NeverStream — для /AsdaAssets/Textures/Effects и
#   всех /Game/Map/<map>/EffectTex.
import unreal
import os as _os_lg, sys as _sys_lg   # ★ 15.09.2026: текстуры эффектов сцены логина — только с ASDA_ALLOW_LOGIN_WRITE=1
_sys_lg.path.insert(0, _os_lg.path.dirname(_os_lg.path.abspath(__file__)))
import asda_login_guard as _LG
_LOGIN_SKIPPED = []   # ★ 15.09.2026 (раздел 16): пропущенные пакеты сцены логина — итог одной строкой в main()
EAL = unreal.EditorAssetLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()

def fix_dir(path):
    n = 0
    if not EAL.does_directory_exist(path): return 0
    for ap in EAL.list_assets(path, recursive=True, include_folder=False):
        if _LG.login_package_blocked(ap, "asda_fix_fx_textures.py", quiet=True):   # ★ 15.09.2026: сцена логина (eft_creation0*, hp/mp/reptn01-b/c)
            _LOGIN_SKIPPED.append(ap)   # ★ (раздел 16): без строки на каждый пропуск — итог в main()
            continue
        a = EAL.load_asset(ap.split('.')[0])
        if not a or not isinstance(a, unreal.Texture2D): continue
        changed = False
        if a.get_editor_property("compression_settings") != unreal.TextureCompressionSettings.TC_EDITOR_ICON:
            a.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON); changed = True
        if a.get_editor_property("mip_gen_settings") != unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS:
            a.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS); changed = True
        if not a.get_editor_property("never_stream"):
            a.set_editor_property("never_stream", True); changed = True
        if changed:
            EAL.save_loaded_asset(a); n += 1
    return n


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    del _LOGIN_SKIPPED[:]   # ★ (раздел 16): итог пропусков — за этот прогон
    total = fix_dir("/Game/AsdaAssets/Textures/Effects")
    # карт-каталоги: /Game/Map/<map>/EffectTex
    for sub in EAL.list_assets("/Game/Map", recursive=False, include_folder=True):
        sub = sub.rstrip('/')
        if not sub: continue
        total += fix_dir(sub + "/EffectTex")
    if _LOGIN_SKIPPED:   # ★ 15.09.2026 (раздел 16): одна итоговая строка вместо строки на каждый пропуск
        unreal.log_warning("[LOGIN-GUARD] asda_fix_fx_textures.py: пакетов сцены логина пропущено %d (%s%s) — только с явного разрешения владельца и ASDA_ALLOW_LOGIN_WRITE=1"
                           % (len(_LOGIN_SKIPPED), ", ".join(p.split(".")[0].rsplit("/", 1)[-1] for p in _LOGIN_SKIPPED[:10]),
                              " …" if len(_LOGIN_SKIPPED) > 10 else ""))
    unreal.log("=== FX TEX FIXED: %d ===" % total)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
