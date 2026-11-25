# Copyright (c) 2026 @sitxovski (Discord)
# Раскладка виджетов «UI игрока» (GameMain.JUI) по тематическим папкам /Game/AsdaAssets/UI/Player_UI/*.
#   Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_player_ui_layout.py
#   Перенос — ШТАТНЫЙ EditorAssetLibrary.rename_asset (ссылки не рвутся), затем починка редиректоров.
#   Проверка: счётчик до/после, загрузка КАЖДОГО перенесённого ассета, отсутствие ObjectRedirector.
import unreal

EAL = unreal.EditorAssetLibrary
SRC = "/Game/AsdaAssets/UI/GameMain"          # откуда (генерик-импорт GameMain.JUI)
DST = "/Game/AsdaAssets/UI/Player_UI"         # куда (тематические подпапки)

# ── Группы. Ключ = подпапка Player_UI, значение = список имён ассетов (без пути). ─────────────
GROUPS = {
    # Постоянный экранный слой (видно без действий игрока)
    "HUD": [
        "WBP_MINISTATE_WIN", "WBP_EXPBar_WIn", "WBP_QUICK_NEW_WIN", "WBP_QUICK_WIN",
        "WBP_SYSTEMMAIN_NEW_WIN", "WBP_SYSTEMMAIN_WIN", "WBP_PVP_TARGET", "WBP_PVP_Gfont_Win",
        "WBP_SKILL_BAR_Win", "WBP_Quest_State_Win", "WBP_VILLAGE_NAME", "WBP_WARMESSAGE_WIN",
        "WBP_NOTICE_WIN", "WBP_HERO_NOTICE_WIN", "WBP_SystemMsg_MDown", "WBP_After_Death_Win",
        "WBP_LuckBag_Base", "WBP_FISHICON_BOX", "WBP_Title_Notice_BG",
    ],
    # Карты (мини-карта уже работает в C++)
    "Map": [
        "WBP_MINIMAP_WIN", "WBP_MINIMAPMIN_WIN", "WBP_MAXMAP_WIN", "WBP_WorldMap_Win",
        "WBP_Position_Save_Win", "WBP_Positionsave_edit",
    ],
    # Персонаж: параметры, скиллы, титулы, книги-коллекции
    "Character": [
        "WBP_CHARAINFO_WIN", "WBP_CHAR_NEW_WIN", "WBP_SKILL_WIN", "WBP_Skill_OneTree_Reset_Win",
        "WBP_Title_Win", "WBP_Hunt_report_Win", "WBP_CharNameChange_Win", "WBP_My_Introduction_Win",
        "WBP_OTHERCHARA_DATA", "WBP_Gear_book_WIN", "WBP_Constellation_S_WIN",
    ],
    # Инвентарь, склад, перекладывание предметов
    "Inventory": [
        "WBP_INVEN_NEW_WIN", "WBP_INVEN_WIN", "WBP_Open_Item_Win", "WBP_Opend_Item_Win",
        "WBP_WARE_WIN", "WBP_Warehouse_Win", "WBP_ITEM_SWAP_WIN",
    ],
    # Чат (отдельный крупный блок P0)
    "Chat": [
        "WBP_CHAT_BACKWIN", "WBP_CHAT_NEW", "WBP_CHAT_NEW_ALRAM", "WBP_CHAT_NEW_SERV",
        "WBP_CHAT_NEW_SYSTEM", "WBP_CHAT_NEW_TABX", "WBP_CHAT_OPENBUT_base", "WBP_CHAT_SETUP_Win",
        "WBP_CHAT_TAB_WIN", "WBP_CHAT_U_Icon", "WBP_CHAT_btn_Win", "WBP_CHAT_Channel_Select",
        "WBP_CHAT_Channel_Setup", "WBP_CHAT_Color_Select_Win",
    ],
    # Группа, комьюнити, клан, шёпот
    "Social": [
        "WBP_PARTY_WIN", "WBP_PARTYBOX_WIN", "WBP_PARTYDISPERSION", "WBP_PARTYTAKE_WIN",
        "WBP_Find_Party_WIn", "WBP_COMMUNITY_WIN", "WBP_Whisper_Win", "WBP_Follow_Another_win",
        "WBP_SHOUTING_WIN", "WBP_CLAN_MAKING_WIN", "WBP_CLAN_JION", "WBP_CLAN_INCREASE_BOX",
        "WBP_CLAN_DELETE_BOX",
    ],
    # Соулмейт (отдельная механика с большим набором окон)
    "Soulmate": [
        "WBP_SOULMATE_POP_WIN", "WBP_SOULMATE_LINK_WIN", "WBP_SOULMATE_GHOST_WIN",
        "WBP_SMate_ReqAnsr_Win", "WBP_Soul_RMsgWin", "WBP_Soul_RMsgWin2", "WBP_Soul_RMsgWin3",
        "WBP_Find_Soulmate_Win", "WBP_SoulmateDEEP_WIN", "WBP_SoulmateHOW_WIN",
        "WBP_Soulmate_inviCARD", "WBP_GOLD_roullete_WIN",
    ],
    # Квесты и диалог NPC
    "Quest": [
        "WBP_QUEST_WIN", "WBP_QUEST_INFO_WIN", "WBP_QUEST_Confirm_Win", "WBP_MULTYQUEST_WIN",
        "WBP_EPISODEPOP_WIN", "WBP_GET_QUESTITEM", "WBP_Subjugate_WIN", "WBP_NPC_LINE_NPC",
        "WBP_TALKBOXW_WIN", "WBP_TALKBOXB_WIN", "WBP_TALKBOX_Mon_WIN",
    ],
    # Торговля: NPC-магазины, обмен, лавки, консигнация
    "Trade": [
        "WBP_NPCSHOP_WIN", "WBP_SHELLITEM_STORE2_WIN", "WBP_EXCHANGE_WIN", "WBP_TRAN_WIN",
        "WBP_TRANTOOL_WIN", "WBP_StreetStall_Win", "WBP_StreetStall_Open_Win",
        "WBP_StreetStall_DisplayOnChar", "WBP_StreetStall_DisplayOnChar1", "WBP_HAIRSHOP_WIN",
        "WBP_EXTRACT_WIN", "WBP_BOOSTER_SHOP_WIN", "WBP_BOOSTER_PREV_WIN", "WBP_BOOSTING_WIN",
    ],
    # Заточка, крафт, разбор, совелы, ремонт, аватары
    "Craft": [
        "WBP_Item_Upgrade_NEW_win", "WBP_Item_Upgrade_Win", "WBP_Item_Initializ_Win",
        "WBP_ItemOptionValue_WIN", "WBP_Item_Union_Win", "WBP_Strength_Part_Win",
        "WBP_Smelting2_Win", "WBP_Smelting3_Win", "WBP_Disassembly_Win", "WBP_ITEM_making_WIN",
        "WBP_ITEM_making_R_BOX", "WBP_ITEM_making_Mark_WIN", "WBP_Sowel_puton_WIN",
        "WBP_Gear_seal_WIN", "WBP_Get_UpgradeOtion", "WBP_Strength_icon_massageBOX",
        "WBP_Lockopen_massageBOX", "WBP_SOWELL_SELBOX", "WBP_INVEN_Repair_Win",
        "WBP_Avatar_compose", "WBP_Avatar_resolve_WIN",
    ],
    # Питомцы
    "Pet": [
        "WBP_Pet_Cage_Win", "WBP_Pet_Hatchery_Win", "WBP_Pet_Status_Win", "WBP_Pet_Inven_Win",
        "WBP_Pet_Tooltips_Win", "WBP_Pet_Inven_Tooltip", "WBP_Pet_Evolution_Win",
        "WBP_Pet_LimitBreak_Win", "WBP_Pet_Button_Win",
    ],
    # Премиум/кэш-магазин, гача, почта премиума, встроенный браузер
    "Premium": [
        "WBP_SHELLITEM_STORE_WIN", "WBP_SHELLITEM_MsgBase", "WBP_SHELLITEM_PURCHASE_BOX",
        "WBP_Premium_Menu", "WBP_Premium_PickBAR_Win", "WBP_Premium_PickBAR_daybtn_X_B",
        "WBP_Premium_Pick_Click_Win", "WBP_Premium_Pick_Click_Win_RU", "WBP_Premium_Pick_View_Win",
        "WBP_Premium_Pick_mssageBOX", "WBP_Premium_Pick_select", "WBP_Premium_Pick_select_2",
        "WBP_Premium_Pick_select_RU", "WBP_PackageShop_Win", "WBP_Package_Contents_Win",
        "WBP_HONOR_itemmall_Win", "WBP_CashCharge_Win", "WBP_CashCharge_Win_JPN",
        "WBP_CashCharge_Win_RU", "WBP_Primium_Item_post_WIN", "WBP_Primium_Item_post2_WIN",
        "WBP_Free_post_box_WIN", "WBP_Free_post_box_go_WIN", "WBP_TW_Serial_WIN", "WBP_My_BLOG_Win",
    ],
    # ESC-меню, опции, доп-меню
    "System": [
        "WBP_SYSTEM_MenuWIN_NEW", "WBP_SYSTEM_Option_NEW", "WBP_SYSTEM_WIN", "WBP_System_3d_setup",
        "WBP_ETC_NEW_WIN",
    ],
    # Инстанс-данжи, ивенты, баннеры смены класса
    "Dungeon": [
        "WBP_INDUN_WAIT_WIN", "WBP_INSDUNGEON_CONDITION_WIN", "WBP_INSDUNGEON_ENTRY_WIN",
        "WBP_INSDUNGEON_MSGY_WIN", "WBP_EventDungeon_Win", "WBP_Event_WIN", "WBP_OneBtn_Event_Win",
        "WBP_ChangeJob_Banner", "WBP_Con_Banner2",
    ],
    # Осадная война и шахты (отдельный крупный блок, отложен)
    "CampWar": [
        "WBP_CAMPWAR_ATTACK", "WBP_CAMPWAR_ATTACK_Position", "WBP_CAMPWAR_End",
        "WBP_CAMPWAR_FONT_Win", "WBP_CAMPWAR_In_List", "WBP_CAMPWAR_Ing", "WBP_CAMPWAR_JOIN_LOGIN",
        "WBP_CAMPWAR_Join", "WBP_CAMPWAR_L_MAP", "WBP_CAMPWAR_Point", "WBP_CAMPWAR_Time_Win",
        "WBP_CAMPWAR_Win", "WBP_CAMPWAR_select", "WBP_CAPWAR_Hold_ING", "WBP_Mine_FONT_WIN",
        "WBP_Mine_Time_WIN", "WBP_Mine_box_fontwin", "WBP_Mine_ob_hp_WIN", "WBP_Mine_team_WIN",
        "WBP_Mine_team_phase_WIN", "WBP_Town_as_WIN", "WBP_Occupation_box",
    ],
    # Рыбалка и её книга-коллекция
    "Fishing": ["WBP_FISH_WIN", "WBP_FISH_BOOK_WIN"],
    # Туториал и гид по классам
    "Tutorial": [
        "WBP_Tutorial_WIN", "WBP_Tutorial_MENU_WIN", "WBP_Tutorial_Notice_WIN", "WBP_TUTO_MSG_BOX2",
        "WBP_TUTO_MSG_BOX3", "WBP_Tuto_Msg_Box_4", "WBP_Job_Guide_Win",
    ],
    # Общая инфраструктура: месседж-боксы, нумпад, тултипы (нужна почти всем окнам)
    "Common": [
        "WBP_ONEBUT_MSGBOX", "WBP_TWOBUT_MSGBOX", "WBP_THREEBUT_MSGBOX", "WBP_COMMONWIN_MSGBOX",
        "WBP_GIFT_MSGBOX", "WBP_NUMPAD_WIN", "WBP_ICON_mssageBOX", "WBP_ICON_mssage_timeBOX",
        "WBP_ITEM_TIPWIN", "WBP_ItemCompareTip_Win", "WBP_SOWEL_Tooltip", "WBP_SKILL_SOUL_tooltip",
        "WBP_SKILL_BURST_tooltip", "WBP_Item_Union_Item_Tooltip",
    ],
}

# ★ НЕ ПЕРЕНОСИМ: мировые плашки над головами — их сейчас правят параллельно
#   (UAsdaNameplateComponent + WBP_NAMEPLATE_*). Остаются в UI/GameMain до окончания той работы.
SKIP = {
    "WBP_NAMEPLATE_MAN", "WBP_NAMEPLATE_MAN1", "WBP_NAMEPLATE_MON",
    "WBP_TARGETMARK_MAN", "WBP_TARGETMARK_MAN1", "WBP_TARGETMARK_MON",
}


def main():
    src_assets = [a for a in EAL.list_assets(SRC, recursive=False, include_folder=False)]
    names = sorted({a.split("/")[-1].split(".")[0] for a in src_assets})
    unreal.log("=== Player_UI: в %s ассетов=%d ===" % (SRC, len(names)))

    plan = {}
    for folder, lst in GROUPS.items():
        for n in lst:
            if n in plan:
                unreal.log_error("ДУБЛЬ в раскладке: %s (%s и %s)" % (n, plan[n], folder))
            plan[n] = folder

    missing = [n for n in plan if n not in names]           # в раскладке есть, ассета нет
    unmapped = [n for n in names if n not in plan and n not in SKIP]   # ассет есть, места нет
    for n in missing:
        unreal.log_warning("нет ассета для раскладки: %s" % n)
    for n in unmapped:
        unreal.log_error("НЕ РАСПРЕДЕЛЁН: %s" % n)
    if unmapped:
        unreal.log_error("=== СТОП: есть нераспределённые ассеты, перенос не выполнен ===")
        return

    moved, failed = 0, []
    for n in names:
        if n in SKIP:
            continue
        folder = plan.get(n)
        if not folder:
            continue
        dst_dir = "%s/%s" % (DST, folder)
        if not EAL.does_directory_exist(dst_dir):
            EAL.make_directory(dst_dir)
        old = "%s/%s" % (SRC, n)
        new = "%s/%s" % (dst_dir, n)
        if EAL.does_asset_exist(new):
            unreal.log_warning("уже на месте: %s" % new)
            continue
        if EAL.rename_asset(old, new):
            moved += 1
        else:
            failed.append(n)
            unreal.log_error("НЕ ПЕРЕНЕСЁН: %s → %s" % (old, new))

    unreal.log("=== перенесено=%d, ошибок=%d ===" % (moved, len(failed)))
    EAL.save_directory(DST, only_if_is_dirty=False, recursive=True)

    # ── Починка редиректоров (fix up redirectors) во всём дереве UI ────────────────────────
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    ar.search_all_assets(True)
    redirs = []
    for d in (SRC, DST):
        for a in EAL.list_assets(d, recursive=True, include_folder=False):
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
        for a in list(EAL.list_assets(SRC, recursive=True, include_folder=False)):
            data = EAL.find_asset_data(a)
            if data and str(data.asset_class_path.asset_name) == "ObjectRedirector":
                EAL.delete_asset(a.split(".")[0])

    # ── Проверка: всё ли грузится и не осталось ли редиректоров ────────────────────────────
    ok, bad = 0, []
    for a in EAL.list_assets(DST, recursive=True, include_folder=False):
        p = a.split(".")[0]
        if EAL.load_asset(p):
            ok += 1
        else:
            bad.append(p)
    left_src = [a for a in EAL.list_assets(SRC, recursive=True, include_folder=False)]
    unreal.log("=== ИТОГ: в Player_UI=%d (загрузилось=%d, битых=%d), осталось в GameMain=%d ===" %
               (len(EAL.list_assets(DST, recursive=True, include_folder=False)), ok, len(bad), len(left_src)))
    for p in bad:
        unreal.log_error("НЕ ГРУЗИТСЯ: %s" % p)
    for a in left_src:
        unreal.log("  GameMain: %s" % a.split("/")[-1])


if __name__ == "__main__":   # ★ закон: Tools/*.py без побочных эффектов при импорте
    main()
