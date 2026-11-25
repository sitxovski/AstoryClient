# Copyright (c) 2026 @sitxovski (Discord)
# Перенос ПОДТВЕРЖДЁННО неиспользуемых виджетов UI игрока в /Game/AsdaAssets/UI/Player_UI/Unused/<Группа>/
#   Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_ui_unused_move.py
#
#   Список формируется ТОЛЬКО из окон, по которым доказано отсутствие ссылок в клиенте
#   (0 immediate-форм idnum окна и всех его контролов во всём AsdaStory.exe, 0 ссылок в
#   эталонном декомпиле, окно не попадает ни в один диапазонный чек id) —
#   см. Docs/UI/UNUSED_REPORT.md.
#
#   Перенос — ШТАТНЫЙ EditorAssetLibrary.rename_asset (ссылки не рвутся) + починка редиректоров.
#   НЕ ТРОГАЕМ: виджеты, чьи пути зашиты в C++ (WBP_MINIMAP_WIN, WBP_MINIMAPMIN_WIN,
#   WBP_MAXMAP_WIN, WBP_VILLAGE_NAME, WBP_NAMEPLATE_*/WBP_TARGETMARK_*).
import unreal

EAL = unreal.EditorAssetLibrary
ROOT = "/Game/AsdaAssets/UI/Player_UI"

# Группа → список имён виджетов (без пути), подтверждённо неиспользуемых.
UNUSED = {
    "Map":      ["WBP_WorldMap_Win", "WBP_Position_Save_Win", "WBP_Positionsave_edit"],
    "Craft":    ["WBP_Avatar_compose", "WBP_Avatar_resolve_WIN", "WBP_Sowel_puton_WIN",
                 "WBP_Strength_icon_massageBOX"],
    # 2026-08-02 (полный прогон): к четырём прежним добавлены локализационные дубли
    # окна пополнения счёта и рулетки-гачи. Окно-оригинал создаётся ровно одно
    # (FindControlByID(18000) в CCashChargeWin_InitControls, 68000/68100/68200/68300/68450/
    # 68500 в premiumGatya_InitWindow); id окон-дублей 18200/79000/79100/79150 в клиенте
    # НЕ встречаются ни разу (0 сырых байт для 18200/79100/79150; два сырых для 79000 —
    # это `fstp [eax+0x134]`). В коде живут ТОЛЬКО id их кнопок закрытия/ОК — списками
    # исключений в общих обработчиках uiHandleCloseOnServerMsg @0xEBDAA0 и
    # GachaScene_OnControlMessage @0xB70... (та же схема, что у Gear_book_WIN).
    "Premium":  ["WBP_Free_post_box_WIN", "WBP_Free_post_box_go_WIN",
                 "WBP_Primium_Item_post2_WIN", "WBP_TW_Serial_WIN",
                 "WBP_CashCharge_Win_RU", "WBP_CashCharge_Win_JPN",
                 "WBP_Premium_Pick_Click_Win_RU", "WBP_Premium_Pick_select_RU"],
    "Soulmate": ["WBP_SoulmateDEEP_WIN", "WBP_SoulmateHOW_WIN", "WBP_Soulmate_inviCARD"],
    "Common":   ["WBP_Item_Union_Item_Tooltip"],
    # CHAT_U_Icon (2930): ни idnum окна, ни 2931/2932 не встречаются как imm32 ни в одном
    # модуле; единственные сырые вхождения — disp32 полей структур (`c7 80 74 0b 00 00`)
    # и rel32 переходов (`0f 84 73 0b 00 00`).
    # 2026-08-04 (chatinv-agent, отмашка пользователя): полная инвентаризация группы Chat,
    # см. Docs/UI/Chat/CHAT_SYSTEM_OVERVIEW.md и UNUSED_REPORT.md §12.
    #  • CHAT_BACKWIN (214) + CHAT_OPENBUT_base (200) + CHAT_NEW_SERV (2550) + CHAT_btn_Win (2540)
    #    — «ВИЗУАЛЬНОЕ ЛЕГАСИ»: окна резолвятся по idnum, но конструктор их ГАСИТ и ни одна
    #    функция эталона не включает обратно. Подтверждено живым минидампом клиента
    #    (live.dmp, g_pChatWnd @0x2BE06C8, CControl+0x124=isVisible, +0x16C=controlId):
    #    214 vis=0, 210 vis=0, 200 vis=0, 2550/2557/2558 vis=0, 2540 vis=0.
    #    ⚠ Логика невидимых буферов 2557/2558 ОСТАЁТСЯ в UAsdaChatSubsystem как ДАННЫЕ.
    #    ⚠ CloseWindow("CHAT_NEW_SERV") в AsdaChatSubsystem.cpp работает по реестру ОТКРЫТЫХ
    #      окон (Windows.Find), путь ассета не трогает — перенос ссылку не рвёт.
    #  • CHAT_Channel_Setup (2800) + CHAT_Channel_Select (2900) — невыпущенная платная фича
    #    «оформление чата» (цвет текста + декоративная иконка со сроком действия; третий член
    #    семьи CHAT_U_Icon 2930 уже здесь). 0 чистых ссылок во всех 6 сборках; «чистые» хиты
    #    2900/2901/2905 декодированы до lib_d3dx.c (D3DXShaderAsm_*) — ложные.
    "Chat":     ["WBP_CHAT_NEW_ALRAM", "WBP_CHAT_U_Icon",
                 "WBP_CHAT_BACKWIN", "WBP_CHAT_OPENBUT_base", "WBP_CHAT_NEW_SERV",
                 "WBP_CHAT_btn_Win", "WBP_CHAT_Channel_Setup", "WBP_CHAT_Channel_Select"],
    # 2026-08-02: Gear_book_WIN — 0 вхождений idnum окна (80300), кнопки-открывашки
    # Gear_bookX_btn (80250) и всех контролов во ВСЕХ PE-модулях client/system; 0 строк
    # "Gear_book"; три id комбобоксов (80321/80324/80323) — лишь список исключений
    # авто-скрытия в обобщённом comboBox_OnChildEvent @0x12F4C80.
    # 2026-08-02: CHAR_NEW_WIN (84000) — РЕШЕНИЕ ПОЛЬЗОВАТЕЛЯ. Окно мёртвое во ВСЕХ ШЕСТИ
    # доступных сборках клиента (0 чистых и 0 сырых вхождений 84000; три «хита» контролов —
    # ложные: rel32 `0f 84`/`e9` и таблицы указателей вне кода). Несмотря на суффикс NEW,
    # для параметров персонажа используется обычное CHARAINFO_WIN (900, CCharaInfoWin).
    # ★ Подтверждённое исключение из закона jui-prefer-new-windows: NEW берём, только если
    # NEW ЖИВАЯ в бинаре. Ср. Item_Upgrade_NEW_win/INVEN_NEW_WIN — живы в других сборках,
    # поэтому остаются каноном и сюда НЕ попадают.
    "Character": ["WBP_Gear_book_WIN", "WBP_CHAR_NEW_WIN"],
}

# Виджеты, чьи пути зашиты в C++ — переносить нельзя ни при каких условиях (страховка).
HARDCODED = {
    "WBP_MINIMAP_WIN", "WBP_MINIMAPMIN_WIN", "WBP_MAXMAP_WIN", "WBP_VILLAGE_NAME",
    "WBP_NAMEPLATE_MAN", "WBP_NAMEPLATE_MAN1", "WBP_NAMEPLATE_MON",
    "WBP_TARGETMARK_MAN", "WBP_TARGETMARK_MAN1", "WBP_TARGETMARK_MON",
}


def count_all():
    return len(EAL.list_assets(ROOT, recursive=True, include_folder=False))


def main():
    before = count_all()
    unreal.log("=== Unused-перенос: в Player_UI ДО = %d ассетов ===" % before)

    moved, skipped, failed = 0, [], []
    for group, names in UNUSED.items():
        dst_dir = "%s/Unused/%s" % (ROOT, group)
        if not EAL.does_directory_exist(dst_dir):
            EAL.make_directory(dst_dir)
        for n in names:
            if n in HARDCODED:
                unreal.log_error("ЗАШИТ В C++, пропуск: %s" % n)
                skipped.append(n)
                continue
            old = "%s/%s/%s" % (ROOT, group, n)
            new = "%s/%s" % (dst_dir, n)
            if EAL.does_asset_exist(new):
                unreal.log_warning("уже на месте: %s" % new)
                continue
            if not EAL.does_asset_exist(old):
                unreal.log_warning("нет ассета: %s" % old)
                skipped.append(n)
                continue
            if EAL.rename_asset(old, new):
                moved += 1
                unreal.log("перенесён: %s → %s" % (old, new))
            else:
                failed.append(n)
                unreal.log_error("НЕ ПЕРЕНЕСЁН: %s → %s" % (old, new))

    EAL.save_directory(ROOT, only_if_is_dirty=False, recursive=True)

    # ── Починка редиректоров ────────────────────────────────────────────────────────────────
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    ar.search_all_assets(True)
    redirs = []
    for a in EAL.list_assets(ROOT, recursive=True, include_folder=False):
        data = EAL.find_asset_data(a)
        if data and str(data.asset_class_path.asset_name) == "ObjectRedirector":
            obj = data.get_asset()
            if obj:
                redirs.append(obj)
    unreal.log("редиректоров найдено: %d" % len(redirs))
    if redirs:
        try:
            unreal.AssetToolsHelpers.get_asset_tools().fixup_referencers(redirs, False)
        except Exception as e:
            unreal.log_warning("fixup_referencers: %s" % e)
        for a in list(EAL.list_assets(ROOT, recursive=True, include_folder=False)):
            data = EAL.find_asset_data(a)
            if data and str(data.asset_class_path.asset_name) == "ObjectRedirector":
                EAL.delete_asset(a.split(".")[0])

    # ── Проверка: всё грузится, редиректоров не осталось ───────────────────────────────────
    ok, bad, left_redirs = 0, [], 0
    for a in EAL.list_assets(ROOT, recursive=True, include_folder=False):
        p = a.split(".")[0]
        data = EAL.find_asset_data(a)
        if data and str(data.asset_class_path.asset_name) == "ObjectRedirector":
            left_redirs += 1
        if EAL.load_asset(p):
            ok += 1
        else:
            bad.append(p)
    after = count_all()
    unreal.log("=== ИТОГ: перенесено=%d, пропущено=%d, ошибок=%d ===" % (moved, len(skipped), len(failed)))
    unreal.log("=== Player_UI ДО=%d ПОСЛЕ=%d, загрузилось=%d, битых=%d, редиректоров осталось=%d ==="
               % (before, after, ok, len(bad), left_redirs))
    for b in bad:
        unreal.log_error("НЕ ГРУЗИТСЯ: %s" % b)


if __name__ == "__main__":   # ★ закон: Tools/*.py без побочных эффектов при импорте
    main()
