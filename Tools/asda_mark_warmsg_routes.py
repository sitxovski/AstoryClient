# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Разметка ВСЕХ колл-сайтов WARMESSAGE_WIN (221) целевым окном ПОРТА (решение юзера 2026-08-04).
#
# Вход:  Reports/warmsg_callsites.tsv  (file line func loc color dur paired) — 316 сайтов
# Выход: он же + две колонки:
#   category    — категория EAsdaMsgCategory, к которой сайт относится по смыслу;
#   target_user — целевое окно(а) ПО РЕШЕНИЮ ЮЗЕРА (что делает наш маршрутизатор).
#
# Правило юзера: в 221 остаются ТОЛЬКО военные/кампвар; администраторские Notice — в NOTICE_WIN;
#   редкая добыча других игроков — в HERO_NOTICE; ВСЁ остальное (включая все 104 парных вызова) —
#   в СИСТЕМНЫЙ ЧАТ. Классификация ниже — по функции-владельцу сайта и по loc-id.
#
# ★ Закон tools-no-import-side-effects: при импорте модуля ничего не выполняется, только main().
import io
import os
import sys

import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
TSV = os.path.join(_P.RE_REPORTS, "warmsg_callsites.tsv")

# ── Категория по ИМЕНИ ФУНКЦИИ-владельца (точное совпадение важнее подстроки) ──
FUNC_CAT = {
    # военные / кампвар
    'uiZoneProximityWarnTick':        'War',           # 108124/108125 — зоны кампвара, TTL 2000
    'netHandleCountDownStart':        'War',           # отсчёт до начала битвы
    'recallCountdown_Tick':           'War',           # отсчёт возврата с поля боя
    'Recv_EventSet':                  'War',           # включение игрового события/битвы
    'Recv_EventOff':                  'War',           # выключение события/битвы
    # объявления администрации
    'Net_RecvPacketDispatch_Loop':    'AdminNotice',   # серверные push-нотисы диспетчера
    # редкая добыча ДРУГИХ игроков
    'packetShowItemAcquiredNotice':   'HeroLoot',
    'netParseItemAcquireNotice':      'HeroLoot',
    # движение
    'Recv_EntityMove':                'MoveRefused',
    'Input_WorldClickHandler':        'MoveRefused',
    'world_OnLeftClickPick':          'MoveRefused',
    'updatePlayerMovement':           'MoveRefused',
    'portalHandleChannelWarp':        'Connection',
    'Recv_PortalChannelChange':       'Connection',
    'netHandleTeleportToPosResponse': 'Connection',
    'netHandleAuthResult':            'Connection',
    # бой
    'Recv_CombatAnimState':           'CombatRefused',
    'Recv_CastState':                 'CombatRefused',
    'Player_UpdateAutoActionState':   'CombatRefused',
    'Player_ExecuteSkillOnTarget':    'SkillRefused',
    'combatHandleSkillUse':           'SkillRefused',
    'skillUseOnTargetDispatch':       'SkillRefused',
    'skillCastActivate':              'SkillRefused',
    'Skill_SendSkillUseOnTarget':     'SkillRefused',
    'CharSkill_SoulRecallCast':       'SkillRefused',
    'Recv_SkillCast':                 'SkillRefused',
    'recvOtherCharSkillUse':          'SkillRefused',
    'netSkill_HandleStateSync':       'SkillRefused',
    'netSkill_HandleSabzilStateSync': 'SkillRefused',
    'net_handleSoulmateSkillState':   'Social',        # состояния соулмейт-навыков
    'netHandleCharStateNotice':       'Social',        # состояния персонажа (баффы/дебаффы/статусы)
    'Recv_CharStateNotice':           'Social',
    'netRecvItemBuffApply':           'ItemRefused',   # применение баффа-предмета
    'Recv_PotionBuff':                'ItemRefused',
    # предметы / инвентарь
    'ItemPop_ShowTransferResult':     'ItemRefused',
    'itemUseDispatch':                'ItemRefused',
    'OnRecv_UseItemResult':           'ItemRefused',
    'netOnItemUseResult':             'ItemRefused',
    'netSendItemUsePacket':           'ItemRefused',
    'netHandleInventoryUpdatePacket': 'ItemRefused',
    'Recv_InventorySwap':             'ItemRefused',
    'Recv_InventorySwap2':            'ItemRefused',
    'uiInventoryShowFullNotice':      'ItemRefused',
    'uiTooltip_ShowItemStatusNotice': 'ItemRefused',
    'uiShowItemStatusNotice':         'ItemRefused',
    'uiItemNoticeDispatch':           'ItemRefused',
    'uiChatShowItemNotice':           'ItemRefused',
    'uiCraftSlotClickSendReq':        'ItemRefused',
    'upgradeAnimDlg_tick':            'ItemRefused',
    'HUD_UpdateMiniWeightBar':        'ItemRefused',   # перегруз 2358/2359, TTL 30000
    'uiInsDungeonPanelCommandHandler': 'System',
    'uiShowLocalizedNotice2118':      'System',
    'uiControlEventDispatch':         'System',
    'net_SendVerifiedRequest5466':    'System',
    'netHandleAutoResurrectReady':    'System',
    'Recv_PartyMinimapMark':          'Social',
    # торговля / магазин
    'Recv_BoosterShopResult':         'Trade',
    'recv_ItemExchangeResult':        'Trade',
    'netTradeRequestHandler':         'Trade',
    'uiPreparePartyTradeRequest':     'Trade',
    'uiTradeListRemoveEntryByItem':   'Trade',
    'tradeWnd_resetAndNotify':        'Trade',
    'shopCart_addItem':               'Trade',
    'exchangeUiOnCommand':            'Trade',
    'CSellWindow_addItemToSell':      'Trade',
    'chatBuildTransactionNotice':     'Trade',
    'chatNotice_ShowCharEvent':       'Progress',
    'Net_OnExpGiftNotice':            'Progress',
    # квесты
    'questCheckAcceptPrereq':         'Quest',
    'CQuest_Episode_OnEvent':         'Quest',
    # дуэль
    'PvpDuel_ApplyMatchResult':       'Duel',
}

# ── Категория по ФАЙЛУ (фолбэк, если функция не в словаре) ──
FILE_CAT = {
    'combat_pvp.c':     'Duel',
    'quest.c':          'Quest',
    'shop_trade.c':     'Trade',
    'item_enhance.c':   'ItemRefused',
    'item_inventory.c': 'ItemRefused',
    'skill_buff.c':     'SkillRefused',
    'entity_char.c':    'CombatRefused',
    'world_map.c':      'MoveRefused',
    'input.c':          'MoveRefused',
    'math_geom.c':      'MoveRefused',
    'chat.c':           'System',
    'game_main.c':      'Tip',
    'file_vfs.c':       'System',
    'ui_core.c':        'System',
    'network.c':        'System',
}

# ── loc-id, которые ОДНОЗНАЧНО военные/кампвар (перекрывают категорию функции) ──
#   ★ 10183/10184 сюда НЕ входят: это ОТКАЗЫ ДВИЖЕНИЯ (пусть и по военной причине), а юзер
#   явно отнёс «отказы движения/атаки/замаха» к системному чату. Так же они и в коде (4007).
WAR_LOCS = {
    '10111',   # флаг кампвара
    '10221',   # «Битва начнется через %d мин.»
    '108124', '108125',   # предупреждения о зонах кампвара (TTL 2000)
    '108143',  # отсчёт возврата с поля боя
}

# ── Куда уходит категория по таблице маршрутизации (MessageRoutes.csv) ──
#   ★ ПОПРАВКА ЮЗЕРА 2026-08-04: военные — в NOTICE_WIN 228 (окно с рамкой), а НЕ в бесфоновый 221
#   и БЕЗ дубля в журнал. После этого в 221 не пишет НИ ОДНА категория (канал сохранён, но пуст).
CAT_TARGET = {
    'War':           'NOTICE228',
    'AdminNotice':   'NOTICE228',
    'HeroLoot':      'HERO_NOTICE',
    'System':        'сисчат',
    'MoveRefused':   'сисчат(+error.wav)',
    'CombatRefused': 'сисчат',
    'SkillRefused':  'сисчат',
    'ItemRefused':   'сисчат(+error.wav)',
    'Loot':          'сисчат+буферB',
    'Trade':         'сисчат',
    'Quest':         'сисчат',
    'Progress':      'сисчат+буферB',
    'Social':        'сисчат',
    'Connection':    'сисчат',
    'Duel':          'сисчат+буферB',
    'Tip':           'сисчат',
}


def classify(fname, func, loc):
    if loc in WAR_LOCS:
        return 'War'
    if func in FUNC_CAT:
        return FUNC_CAT[func]
    return FILE_CAT.get(fname, 'System')


def main():
    if not os.path.isfile(TSV):
        print('нет файла %s' % TSV)
        return 1
    with io.open(TSV, 'r', encoding='utf-8') as f:
        lines = f.read().splitlines()
    head = lines[0].split('\t')
    if 'target_user' in head:
        print('колонки уже добавлены — перезаписываю значения')
        # снять прежние две колонки, чтобы не плодить дубли
        ci = head.index('category')
        head = head[:ci]
        lines = [ '\t'.join(head) ] + [ '\t'.join(l.split('\t')[:ci]) for l in lines[1:] ]

    out = ['\t'.join(head + ['category', 'target_user'])]
    stat = {}
    for ln in lines[1:]:
        if not ln.strip():
            continue
        c = ln.split('\t')
        fname = c[0] if len(c) > 0 else ''
        func  = c[2] if len(c) > 2 else ''
        loc   = c[3] if len(c) > 3 else ''
        cat = classify(fname, func, loc)
        out.append('\t'.join(c + [cat, CAT_TARGET[cat]]))
        stat[cat] = stat.get(cat, 0) + 1

    with io.open(TSV, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out) + '\n')

    total = sum(stat.values())
    print('размечено сайтов: %d' % total)
    for k in sorted(stat, key=lambda x: -stat[x]):
        print('  %-14s %4d  →  %s' % (k, stat[k], CAT_TARGET[k]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
