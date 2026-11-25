// Copyright (c) 2026 @sitxovski (Discord)
// NetOpcodes.h — сетевые опкоды AsdaStory (1:1, серверно-сверены).
// Сгенерировано из RE/ida/netopcode_enum.h. НЕ редактировать вручную — править источник.
#pragma once
#include <cstdint>

enum NetOpcode
{

    // ==== Логин / выбор персонажа / вход в мир (10xx) ====
    OP_LOGIN_KEEPALIVE_PING = 1000, // C->S  keepalive ping (Net_SendKeepAlivePing); сервер sub_523F00  (отв.: OP_LOGIN_PING_ACK=1001)
    OP_LOGIN_PING_ACK = 1001, // S->C  Recv_LoginPingAck
    OP_SEND_LOGIN_PACKET_1 = 1003, // C->S
    OP_NET_RECV_LOGIN_RESPONSE = 1004, // S->C  Net_RecvLoginResponse
    OP_SEND_CHARACTER_PACKET = 1005, // C->S
    OP_CHAR_SELECT_INIT_SLOT_ROTATION_FROM_PACKET = 1006, // S->C  charSelect_InitSlotRotationFromPacket
    OP_NET_RECV_CHAR_LIST_ENTRY = 1007, // S->C  Net_RecvCharListEntry
    OP_C_LOGIN_WIN_ON_BUT00 = 1010, // C->S
    OP_NET_RECV_CHAR_CREATE_RESULT = 1011, // S->C  Net_RecvCharCreateResult
    OP_SEND_CHANNEL_PACKET = 1012, // C->S
    OP_NET_RECV_UNIT_INFO63 = 1013, // S->C  netRecvUnitInfo63
    OP_UI_CHAR_CREATE_SUBMIT_REQUEST = 1014, // C->S
    OP_PARSE_PACKET_NAME_ENTRY_BLOCK = 1015, // S->C  parsePacketNameEntryBlock
    OP_CHAR_DEAL_SEND_REQ = 1016, // C->S
    OP_CHAR_DELETE_REQ = 1018, // C->S  netSendReq1018 (удаление персонажа)
    OP_NET_RECV_CHAR_SELECT = 1017, // S->C  Net_RecvCharSelect
    OP_CHAR_CREATE_ON_CREATE_RESULT_PACKET = 1019, // S->C  charCreate_OnCreateResultPacket
    OP_NET_SEND_ENTER_WORLD_HANDSHAKE = 1021, // S->C  netSendEnterWorldHandshake
    OP_RECV_NPC_WINDOW_DATA = 1022, // S->C  recvNpcWindowData
    OP_CHAR_INFO_PARSE_PACKET = 1023, // S->C  charInfoParsePacket
    OP_CHAR_SLOT_FILL_RECORDS_FROM_PACKET = 1024, // S->C  charSlotFillRecordsFromPacket
    // ★ 2026-09-02 (порт «одержимости»). 1025 в исходном `RE/ida/netopcode_enum.h` ОТСУТСТВУЕТ —
    //   генератор снимает опкоды по таблице приёмников клиента, а этот обслуживается функцией
    //   с миснеймом `Net_RecvEnterWorld@0xF03690` (входа в мир там нет — вход это 1021/1027).
    //   Доказан с ОБЕИХ сторон: сервер `LoginManager.c:3123` и ещё 6 мест `Send_GetCharInfo`
    //   шлют `sub_415A40(1025, 5, …)`; клиент читает 5 байт с `Src+9` и разбирает `[0]`/`[1]`.
    //   Тело 5 б: [0] код ветки соулмейта (0/1/2/3=онлайн/4=офлайн/5), [1] подкод (0..3 → флаг
    //   второго абзаца панели соулмейта), [2] соулмейт в игре, [3] его канал, [4] его CPoridx.
    //   ⚠ Источник `RE/ida/netopcode_enum.h` НАДО дополнить тем же именем при следующей перегенерации.
    OP_NET_RECV_ENTER_WORLD_STATUS = 1025, // S->C  Net_RecvEnterWorld@0xF03690 (МИСНЕЙМ: статус ветки соулмейта)
    OP_NET_HANDLE_NAME_ENTRY_DIALOG_PACKET = 1026, // S->C  netHandleNameEntryDialogPacket
    OP_NET_HANDLE_APPEARANCE_CHANGE_RESULT = 1027, // S->C  netHandleAppearanceChangeResult
    OP_LOGIN_WND_SHOW_CHAR_SELECT_PANEL = 1028, // C->S

    // ==== Античит / целостность (22xx) ====
    OP_UI_REFRESH_SLOT_GRID3X3 = 2243, // S->C  uiRefreshSlotGrid3x3
    OP_ANTI_CHEAT_SEND_BIT_MASK_CHECK = 2244, // C->S  ~ui Refresh Slot Grid3x3 (по парному клиент-опкоду)  (отв.: OP_UI_REFRESH_SLOT_GRID3X3=2243)
    OP_NET_RECV_SOULMATE_SKILL_COOL_TIME = 2247, // S->C  Net_RecvSoulmateSkillCoolTime
    // ★ 2026-08-29 (порт МЕНЮ ЗВАНИЙ). В эталоне ветка `else if (opcode == 2255)` диспетчера
    //   `net_dispatchIncoming` (network.c:2857): шапка 6 б + записи по 46 б, каждая уходит в
    //   `charApplyStateFlagDataA@0xE9BFE0` — это БЛОКИ СТАТОВ полученных званий (Fame).
    OP_TITLE_STAT_BATCH_2255 = 2255, // S->C  charApplyStateFlagDataA (батч блоков статов званий)

    // ==== Системные уведомления (31xx) ====
    OP_UI_NOTICE_UPDATE3_LINE_BC90 = 3113, // S->C  uiNoticeUpdate3Line_BC90

    // ==== Группа / пати-менеджмент (32xx-34xx) ====
    OP_RECV_CLIENT_ADD_MEMBER = 3410, // C->S  recvClientAddMember
    OP_RECV_CLIENT_SUB_MEMBER = 3412, // C->S  recvClientSubMember
    OP_RECV_CLIENT_GET_MEMBER = 3414, // C->S  recvClientGetMember
    OP_RECV_CLIENT_UPDATE_MEMBER = 3416, // C->S  recvClientUpdateMember
    OP_RECV_CLIENT_ADD_LEADER = 3418, // C->S  recvClientAddLeader
    OP_RECV_CLIENT_SUB_LEADER = 3420, // C->S  recvClientSubLeader
    OP_RECV_CLIENT_GET_LEADER = 3422, // C->S  recvClientGetLeader
    OP_RECV_CLIENT_UPDATE_LEADER = 3424, // C->S  recvClientUpdateLeader

    // ==== Группа / соулмейт (32xx) ====
    OP_PARTY_DISBAND_AND_CLEAR_MEMBERS = 3226, // S->C  partyDisbandAndClearMembers

    // ==== Подземелья (35xx) ====
    OP_CHAR_INDUN_SEND_CHAR_WAIT_CANCEL = 3504, // C<->S  charIndunSendCharWaitCancel
    OP_INS_DUNGEON_RECV_PICK_COUNT = 3505, // S->C  insDungeonRecvPickCount
    OP_CHAR_INDUN_RECV_ENTRANCE = 3506, // C->S  charIndunRecvEntrance  (отв.: OP_INS_DUNGEON_RECV_PICK_COUNT=3505)
    OP_CHAR_INDUN_WAIT_MEMBER_ENTRANCE = 3509, // C->S  charIndunWaitMemberEntrance
    OP_CHAR_INDUN_CLOSE_CLIENT_MSG_SEND = 3510, // C->S  charIndunCloseClientMsgSend

    // ==== Мир / движение / спавн сущностей / бой (40xx) ====
    OP_CHAR_LOGON_REQ = 4001, // C->S  charLogonReq
    OP_RECV_PACKET_SPAWN_SELF = 4003, // S->C  RecvPacket_SpawnSelf
    OP_RECV_PLAYER_APPEARANCE = 4005, // S->C  Recv_PlayerAppearance@0xF3CD30 (151б: внешность self/соседа)
    OP_RECV_PACKET_ENTITY_MOVE = 4007, // S->C  RecvPacket_EntityMove
    OP_CHAR_STOP = 4008, // C->S  charStop  (отв.: OP_RECV_PACKET_ENTITY_STOP=4009)
    OP_RECV_PACKET_ENTITY_STOP = 4009, // S->C  RecvPacket_EntityStop
    OP_RECV_PACKET_LOG_OUT_DESPAWN = 4011, // S->C  RecvPacket_LogOut_Despawn
    OP_RECV_PACKET_MOB_SPAWN = 4013, // S->C  RecvPacket_MobSpawn
    OP_COMBAT_HIT = 4015, // S->C  Recv_CombatHit
    OP_COMBAT_HIT_AUX = 4017, // S->C  Recv_CombatHitAux
    OP_MOB_SPAWN_LOOP = 4019, // S->C  Recv_MobSpawnLoop
    OP_CHAR_FULL_SPAWN = 4021, // S->C  Recv_CharFullSpawn
    OP_CHAR_SPAWN_SHORT = 4023, // S->C  Recv_CharSpawnShort
    OP_SEND_CHAR_STATE_POS = 4024, // C->S  MOVE :: CharEMotionSync() Exception Error  (отв.: OP_CHAR_SPAWN_FX=4025)
    OP_CHAR_SPAWN_FX = 4025, // S->C  Recv_CharSpawnFx
    OP_SEND_CHAR_NORMAL_MONSTER_ATTACK = 4026, // C->S  sendCharNormalMonsterAttack  (отв.: OP_COMBAT_ANIM_STATE=4027)
    OP_COMBAT_ANIM_STATE = 4027, // S->C  Recv_CombatAnimState
    OP_SEND_CHAR_NORMAL_MONSTER_ATTACK_NEXT = 4028, // C->S  sendCharNormalMonsterAttackNext  (отв.: OP_CAST_STATE=4029)
    OP_CAST_STATE = 4029, // S->C  Recv_CastState
    OP_RECV_ENTITY_SET_STATE5 = 4033, // S->C  recvEntitySetState5
    OP_CHAR_DO_TARGET_ACTION_AND_SABZIL = 4034, // S->C  charDoTargetActionAndSabzil
    OP_RECV_PACKET_LEVEL_UP = 4036, // S->C  RecvPacket_LevelUp
    OP_NET_HANDLE_HP_EXP_UPDATE = 4038, // S->C  netHandleHpExpUpdate
    OP_INVENTORY_BAG_FILL = 4045, // S->C  recv_InventoryBagFill
    OP_CHAR_MANAGER_GET_LOCAL_PLAYER = 4046, // S->C  CharManager_GetLocalPlayer
    OP_INVENTORY_FULL_REFRESH = 4048, // S->C  recv_InventoryFullRefresh
    OP_CHAR_MANAGER_GET_LOCAL_PLAYER_4049 = 4049, // S->C  CharManager_GetLocalPlayer
    OP_RECV_VISIBLE_PLAYER_LIST = 4065, // S->C  RecvPacket_VisiblePlayerList@0xF3DE50 (батч 592б/запись)
    OP_SEND_CLIENT_STORE_HOUSE_ITEM_INFO = 4050, // C->S  sendClientStoreHouseItemInfo  (отв.: OP_WAREHOUSE_LOAD_SLOTS_FROM_PACKET=4051)
    OP_WAREHOUSE_LOAD_SLOTS_FROM_PACKET = 4051, // S->C  warehouseLoadSlotsFromPacket
    OP_WAREHOUSE_DRAW_CURRENT_TAB = 4052, // S->C  warehouseDrawCurrentTab
    // S->C  Recv_CharDeath_Apply@0xF42E50 — СМЕРТЬ персонажа. Сервер SendCharDead@0x4CB480 рассылает
    //   ВСЕМ в секторе, включая умершего (SendPacket_ClippingSectorInAllUser). Тело 4б:
    //   [0..1] i16 userIdx умершего, [2..3] i16 userIdx убийцы (-1 = без убийцы).
    //   С этого момента сервер держит CUser+79=1 и режет ЛЮБОЕ действие (см. OP_ACTION_FLAG_CHECK).
    OP_RECV_CHAR_DEATH = 4053, // S->C  Recv_CharDeath_Apply
    OP_RECV_CHAR_REGEN = 4054, // C->S  recvCharRegen  (отв.: OP_NET_HANDLER_ENTER_WORLD=4055)
    OP_NET_HANDLER_ENTER_WORLD = 4055, // S->C  netHandler_EnterWorld
    // ★ ИМЯ ОБМАНЧИВО (проверено 24.08.2026): к подземельям пакет отношения не имеет.
    //   Это `GameNet_OnLocalPlayerMapEntry@0xF43390` — «игрок прибыл в мир»; порт разбирает его
    //   верно по смыслу (AsdaNetSubsystem.cpp, ветка входа вместе с 4055).
    //   Настоящая раздача добычи подземелья — 3512 (43 б), см. ниже.
    OP_INS_DUNGEON_APPLY_PICK_INFO = 4056, // S->C  на деле GameNet_OnLocalPlayerMapEntry (вход в мир)
    // S->C  netHandleHpMpUpdate@0xF43D00 — ★ СЕРВЕРНЫЙ СНАПШОТ HP ЛЮБОГО ИГРОКА (свой И чужие:
    //   CharManager_FindByUserIdx по userIdx@0). Тело (мин. 17 б, network.c:42728-42817):
    //     [0..1]  u16 userIdx           — чей HP (a2[0])
    //     [2..5]  i32 maxHp             — *(_DWORD*)(a2+1), в HUD-полосу как знаменатель
    //     [6..9]  i32 curHp             — *(_DWORD*)(a2+3), пишется в CCharacter (LABEL_17)
    //     [10]    u8  updateType        — 1/4/6 = показать всплывающую цифру дельты (стиль 0x12);
    //                                     4 → доп. бафф-FX по skillId; 6 → SkillFx_hill01 на себе
    //     [11..12] i16 skillId          — источник (267/268/269/275/276/296); -1 = нет
    //     [13..16] i32 secondHp         — CCharacter.gap_4C0[12]; если == curHp, эталон обнуляет
    //   ЖИВОЙ ТЕСТ 2026-08-04: регенерация вне боя каждые 5 с (1497→1568→…→2853=max).
    OP_NET_HANDLE_HP_MP_UPDATE = 4058, // S->C  netHandleHpMpUpdate
    // S->C  pkt_OnMpSkillFxUpdate@0xF440E0 — снапшот MP (entity_char.c:53081-53139). Тело (мин. 11 б):
    //   [0..1] u16 userIdx, [2..3] i16 maxMp (эталон НЕ читает — берёт CCharacter.iMaxMp),
    //   [4..5] i16 curMp (a1[2]), [6] u8 updateType (4=FX по skillId, 6=HpReco на себе),
    //   [7..8] i16 skillId, [9..10] i16 secondMp (== curMp → эталон обнуляет).
    OP_PKT_ON_MP_SKILL_FX_UPDATE = 4059, // S->C  pkt_OnMpSkillFxUpdate
    OP_RECV_PACKET_CHAR_STAT_BLOCK_A = 4060, // S->C  RecvPacket_CharStatBlockA
    OP_NET_RECV_ENTITY_POS_SYNC = 4062, // S->C  netRecvEntityPosSync
    OP_CHAR_DISPATCH_BY_USER_IDX = 4063, // S->C  charDispatchByUserIdx
    OP_RECV_PACKET_CHAR_STAT_BLOCK_B = 4064, // S->C  RecvPacket_CharStatBlockB
    OP_DISCONNECT_POPUP = 4067, // S->C  Recv_DisconnectPopup
    OP_CHAR_MGR_CLEAR_ALL_ENTITIES = 4069, // S->C  charMgrClearAllEntities

    // ==== PvP атака между игроками (42xx) ====
    OP_RECV_USER_ATTACK_TO_PLAYER = 4201, // C->S  recvUserAttackToPlayer
    OP_NET_SKILL_HANDLE_STATE_SYNC = 4202, // S->C  netSkill_HandleStateSync@0xF3FAB0 (МИСНЕЙМ: ответ на 4201 — PvP-атака принята/отказ; ★ 17.10.2026 Ф4)
    OP_RECV_USER_ATTACKED_TO_PLAYER = 4203, // C->S  recvUserAttackedToPlayer  (отв.: OP_NET_SKILL_HANDLE_SABZIL_STATE_SYNC=4204)
    OP_NET_SKILL_HANDLE_SABZIL_STATE_SYNC = 4204, // S->C  netSkill_HandleSabzilStateSync
    OP_NET_TRADE_CANCEL_RESET_CHAR_STATE = 4205, // S->C  netTradeCancelResetCharState
    OP_RECV_USER_BATTLE_STATE = 4206, // C<->S  recvUserBattleState
    OP_UI_FORMAT_LOCALIZED_NOTICE = 4207, // S->C  UI_FormatLocalizedNotice

    // ==== PvP дуэль / событие сервера (43xx) ====
    OP_ALARM_GAME_SERVER_EVENT_INFO = 4301, // C<->S  alarmGameServerEventInfo
    OP_RECV_USER_PVP_REQ = 4302, // C->S  recvUserPvpReq  (отв.: OP_ALARM_GAME_SERVER_EVENT_INFO=4301)
    OP_RECV_USER_PVP_RES = 4303, // C->S  recvUserPvpRes

    // ==== Клан / друзья / каналы (41xx) ====
    OP_NET_APPLY_SKILL_HIT_RESULT_PACKET = 4102, // S->C  netApplySkillHitResultPacket
    OP_NET_HANDLE_PERSONA_INTERACT = 4103, // S->C  netHandlePersonaInteract
    OP_CLAN_CREATE_REQ = 4104, // C->S  clanCreateReq  (отв.: OP_AUTH_PASSWORD=4105)
    OP_AUTH_PASSWORD = 4105, // S->C  Recv_AuthPassword
    OP_CLAN_DELETE_REQ = 4106, // C->S  clanDeleteReq  (отв.: OP_UI_INVEN_NOTICE_BY_CODE=4107)
    OP_UI_INVEN_NOTICE_BY_CODE = 4107, // S->C  uiInvenNoticeByCode
    OP_CLAN_MEMBER_INVITE_REQUEST_USER = 4108, // C->S  clanMemberInviteRequestUser  (отв.: OP_AUTH_VISIBLE=4109)
    OP_AUTH_VISIBLE = 4109, // S->C  Recv_AuthVisible
    OP_ITEM_SET_SPLIT_MOVE_SOURCE = 4110, // S->C  itemSetSplitMoveSource
    OP_CLAN_MEMBER_INVITE_RECV_RESPONSE_USER = 4111, // C->S  clanMemberInviteRecvResponseUser  (отв.: OP_ITEM_SET_SPLIT_MOVE_SOURCE=4110)
    OP_CLAN_CAPTAIN_MEMBER_LEAVE_REQ = 4114, // C->S  clanCaptainMemberLeaveReq  (отв.: OP_CHANNEL_LIST_RESP_HANDLER=4115)
    OP_CHANNEL_LIST_RESP_HANDLER = 4115, // S->C  channelListRespHandler
    OP_CLAN_MEMBER_LEAVE_REQ = 4116, // C->S  clanMemberLeaveReq  (отв.: OP_UI_INVENTORY_ITEM_NAME_RESULT_HANDLER=4117)
    OP_UI_INVENTORY_ITEM_NAME_RESULT_HANDLER = 4117, // S->C  uiInventoryItemNameResultHandler
    OP_SYSTEM_MESSAGE = 4121, // S->C  Recv_SystemMessage
    OP_SEND_CLAN_BASE_INFO = 4122, // C->S  sendClanBaseInfo  (отв.: OP_RECV_TRADE_WINDOW_STATE=4123)
    OP_RECV_TRADE_WINDOW_STATE = 4123, // S->C  recvTradeWindowState
    OP_SEND_CLAN_USER_LIST_DB = 4124, // C->S  sendClanUserListDB  (отв.: OP_RECV_PARSE_SERVER_CHANNEL_LIST=4125)
    OP_RECV_PARSE_SERVER_CHANNEL_LIST = 4125, // S->C  recvParseServerChannelList
    OP_SERVER_CHANNEL_LIST = 4126, // S->C  Recv_ServerChannelList
    OP_RECV_TRADE_WINDOW_STATE_ALT = 4131, // S->C  recvTradeWindowStateAlt
    OP_UI_TRADE_WINDOW_RESET_STATE = 4132, // S->C  uiTradeWindowResetState
    OP_AUTH_RELOGIN = 4133, // S->C  Recv_AuthRelogin
    OP_NET_PARSE_FIXED_RECORD_LIST = 4135, // S->C  netParseFixedRecordList
    OP_CLAN_BASE_INFO_MAX_CLAN_USER_COUNT_REQ = 4136, // C->S  clanBaseInfoMaxClanUserCountReq  (отв.: OP_UI_INVENTORY_REPAIR_RESULT_HANDLER=4137)
    OP_UI_INVENTORY_REPAIR_RESULT_HANDLER = 4137, // S->C  uiInventoryRepairResultHandler
    OP_CLAN_MEMBER_LEAVE_REQ_4138 = 4138, // C->S  clanMemberLeaveReq  (отв.: OP_UI_INVENTORY_REPAIR_RESULT_HANDLER=4137)
    OP_FRIEND_MEMBER_INVITE_REQ = 4150, // C->S  friendMemberInviteReq  (отв.: OP_UI_ITEM_NOTICE_DISPATCH=4151)
    OP_UI_ITEM_NOTICE_DISPATCH = 4151, // S->C  uiItemNoticeDispatch
    OP_PARTY_MEMBER = 4152, // S->C  Recv_PartyMember
    OP_RECV_USER_FRIEND_OK = 4153, // C->S  recvUserFriendOK  (отв.: OP_PARTY_MEMBER=4152)
    OP_FRIEND_MEMBER_DELETE_REQ = 4154, // C->S  friendMemberDeleteReq  (отв.: OP_UI_TRADE_LIST_REMOVE_ENTRY_BY_ITEM=4155)
    OP_UI_TRADE_LIST_REMOVE_ENTRY_BY_ITEM = 4155, // S->C  uiTradeListRemoveEntryByItem
    OP_FRIEND_MEMBER_LIST_REQ = 4156, // C->S  friendMemberListReq  (отв.: OP_PKT_PARSE_LIST_RECORDS31=4157)
    OP_PKT_PARSE_LIST_RECORDS31 = 4157, // S->C  pktParseListRecords31
    OP_RECV_TRADE_LIST_PAGE_REFRESH = 4158, // S->C  recvTradeListPageRefresh
    // ★ МИСНЕЙМ: 4159 к списку приятелей отношения НЕ имеет — это секторный broadcast применения
    //   навыка/портала (сервер RecvChar_SkillUse@0x46F900, GameServer.c:46003 и @0x48E4F0, :57174).
    OP_NET_RECV_CHAR_UPDATE_LIST_ENTRY = 4159, // S->C  netRecv_charUpdateListEntry
    // S->C  обновление ОДНОЙ записи списка приятелей, 33 б (сервер sub_40BDC0@0x40BDC0):
    //   [0..1] i16 (=0) + запись приятеля 31 б с +2. ★ В эталонном диспетчере case 0x1040 ОТСУТСТВУЕТ
    //   (дефект оригинала D-F4: сервер шлёт, клиент роняет) — в порту принимается.
    OP_FRIEND_ENTRY_UPDATE = 4160, // S->C  sub_4069F0 -> friendEntryUpdate

    // ==== Предметы / магазин / квест / чат / пати (50xx-51xx) ====
    // ★ ДОБАВЛЕНО ВРУЧНУЮ 2026-08-08 (реверс инвентаря `Reports/inventory_re_B.md` §2.3).
    //   Генератор их пропустил: 4999/5001 приходят S→C и в клиентской таблице диспетчера
    //   разбираются косвенно, 5002 у клиента вообще не отправляется (дефект оригинала).
    //   Источник-первоисточник `RE/ida/netopcode_enum.h` дополнен теми же тремя строками.
    OP_INVENTORY_REMOVE_SLOT = 4999, // S->C  recvInventoryRemoveSlot@0x998BC0 (59 б: запись 57 + i16 вес)
    OP_CHAR_ITEM_MOVE = 5000, // C->S  charItemMove
    OP_CHAR_ITEM_MOVE_RESULT = 5001, // S->C  sub_4D12F0 (24 б: result, subKind, 2×item-ref 11 б)
    OP_CHAR_ITEM_DROP = 5002, // C->S  CharItemDrop@0x4D2690 (35 б: токен+MD5+item-ref 11 б)
    OP_ITEM_EQUIP_STAT = 5003, // S->C  Recv_ItemEquipStat
    OP_DROP_ITEM_MGR_SPAWN_GROUND_ITEM = 5005, // S->C  DropItemMgr_SpawnGroundItem
    OP_CHAR_ITEM_DUMP = 5006, // C->S  charItemDump  (отв.: OP_NET_ON_INVENTORY_USE_RESULT=5007)
    OP_NET_ON_INVENTORY_USE_RESULT = 5007, // S->C  netOnInventoryUseResult
    OP_CHAR_ITEM_KEEPING = 5008, // C->S  charItemKeeping  (отв.: OP_INVENTORY_SWAP=5009)
    OP_INVENTORY_SWAP = 5009, // S->C  Recv_InventorySwap
    OP_CHAR_ITEM_RETAKE = 5010, // C->S  charItemRetake  (отв.: OP_INVENTORY_SWAP2=5011)
    OP_INVENTORY_SWAP2 = 5011, // S->C  Recv_InventorySwap2
    OP_CHAR_ITEM_PICKING = 5012, // C->S  charItemPicking  (отв.: OP_NET_HANDLE_ITEM_PICKUP_RESULT=5013)
    OP_NET_HANDLE_ITEM_PICKUP_RESULT = 5013, // S->C  Net_HandleItemPickupResult
    OP_BILLBOARD_REMOVE_BY_KEY_CLEAR_TARGET = 5015, // S->C  billboardRemoveByKeyClearTarget
    OP_ITEM_PICKUP_GROUND_OBJECT = 5017, // S->C  itemPickupGroundObject
    OP_CHAR_ITEM_SELL = 5018, // C->S  charItemSell  (отв.: OP_SHOP_BUY_RESULT=5019)
    OP_SHOP_BUY_RESULT = 5019, // S->C  Recv_ShopBuy_Result
    OP_CHAR_ITEM_BUY = 5020, // C->S  charItemBuy  (отв.: OP_SHOP_SELL_RESULT_0=5021)
    OP_SHOP_SELL_RESULT_0 = 5021, // S->C  Recv_ShopSell_Result_0
    OP_CHAR_CELLOB_ITEM_DETACH = 5022, // C->S  charCellobItemDetach  (отв.: OP_SHOP_SELL_RESULT_0=5021)
    OP_CHAR_CELLOB_ITEM_MOVE = 5024, // C->S  charCellobItemMove  (отв.: OP_NET_RECV_ITEM_ACTION_RESULT=5025)
    OP_NET_RECV_ITEM_ACTION_RESULT = 5025, // S->C  netRecvItemActionResult
    OP_CHAR_SHELL_ITEM_VANISH = 5026, // C->S  charShellItemVanish  (отв.: OP_NET_RECV_INV_SLOT_MOVE_RESULT=5027)
    OP_NET_RECV_INV_SLOT_MOVE_RESULT = 5027, // S->C  netRecvInvSlotMoveResult
    OP_CHAR_SPEND_ITEM_USE = 5028, // C->S  charSpendItemUse  (отв.: OP_ON_RECV_USE_ITEM_RESULT=5029)
    OP_ON_RECV_USE_ITEM_RESULT = 5029, // S->C  OnRecv_UseItemResult
    OP_RECV_ITEM_RESULT_NOTICE = 5030, // S->C  recvItemResultNotice
    OP_CHAR_SPEND_HOLY_ITEM_USE = 5031, // C->S  charSpendHolyItemUse  (отв.: OP_C_CLASS_GET_FIELD142_C=5032)
    OP_C_CLASS_GET_FIELD142_C = 5032, // S->C  CClass_GetField142C
    OP_CHAR_SHELL_ITEM_UPGRADE = 5033, // C->S  charShellItemUpgrade
    // ★ 25.09.2026 (заточка, волна 2): ответ ЛЕГАСИ-заточки. Клиентский диспетчер `network.c:1901`
    //   (`case 0x13AA` → `sub_92246B` + вызов `g_pRecvUiHandler->vtbl[+24]`). Окна-владельца в этой
    //   сборке нет (`gear_upgrade_re_report.md` §12), порт принимает пакет и НИЧЕГО не делает.
    OP_CHAR_SHELL_ITEM_UPGRADE_RESULT = 5034, // S->C  ответ на 5033 (легаси, no-op)
    OP_ITEM_ACTION_SEND_USE_REQUEST = 5035, // C->S
    OP_REFINE_SHOP = 5036, // S->C  Recv_RefineShop
    OP_SHOP_SEND_BUY_CONFIRM = 5037, // C->S
    OP_SHOP_BUY_MULTI_SEND_REQ = 5039, // C->S
    // ★★★ 5041/5042 — СДАЧА (завершение шага) КВЕСТА. Имена и направление сняты с ДИЗАССЕМБЛЕРА
    //   СЕРВЕРА (авторитет), а не с клиентских эвристик:
    //     GameServer диспетчер `case 0x13B1` -> `Send_RewardQuestProcess@0x45CE50` — принимает 5041,
    //       MD5 проверяется по 7 байтам, тело читается по +33, ретранслируется на NpcServer
    //       опкодом 7103 (13 б) -> `NpcServer!sub_420C10@0x420C10` («Process_Quest()»);
    //     ответ NpcServer 7104 -> `GameServer!recvRewardQuestProcess@0x45CF90` -> клиенту 5042 (501 б).
    //   Прежнее имя 5042 `OP_NET_APPLY_PLAYER_FULL_UPDATE` было МИСНЕЙМОМ (см.
    //   `Reports/quest_accept_fix.md` §5): к «полному апдейту игрока» опкод отношения не имеет.
    OP_QUEST_REWARD_PROCESS_REQ = 5041, // C->S  Send_RewardQuestProcess (сдать шаг квеста, payload 7 б)
    OP_QUEST_REWARD_PROCESS_RESULT = 5042, // S->C  recvRewardQuestProcess (результат сдачи, тело 501 б)
    OP_SEND_START_QUEST_BOOK_PROCESS = 5043, // C->S  sendStartQuestBookProcess  (отв.: OP_C_QUEST_RECV_PACKET=5044)
    OP_C_QUEST_RECV_PACKET = 5044, // S->C  CQuest_RecvPacket
    OP_UI_RESET_ALL_WINDOWS_AND_LEAVE_5045 = 5045, // C->S  ~net Handle Char Flag Update (по парному клиент-опкоду)  (отв.: OP_NET_HANDLE_CHAR_FLAG_UPDATE=5046)
    OP_NET_HANDLE_CHAR_FLAG_UPDATE = 5046, // S->C  netHandleCharFlagUpdate
    OP_C_QUEST_ON_RECV_STATE_SYNC = 5047, // S->C  CQuest_OnRecv_StateSync
    OP_QUEST_ITEM_RESULT_APPLY = 5052, // S->C  questItemResult_apply
    OP_PORTAL_CHANNEL_CHANGE = 5055, // S->C  Recv_PortalChannelChange
    OP_CHAR_PORTAL_LOGOUT_REQ = 5056, // C->S  charPortalLogoutReq  (ответа НЕТ: сервер CharPortalLogoutReq @0x52CB30 ставит флаг и зовёт PortalDisConnectUser, прямого ответа нет (косвенная отправка в вызываемых sub_523F00/sub_5324A0/PortalDisConnectUser не исключена) — RE/ida/server_work/GameServer.c:114312; 5055 шлют другие ветки)
    OP_CHAR_PORTAL_LOGON_REQ = 5058, // C->S  charPortalLogonReq  (отв.: OP_HOTBAR_LOAD_PAGE=5059)
    OP_HOTBAR_LOAD_PAGE = 5059, // S->C  Hotbar_LoadPage
    OP_NET_RECV_SOWEL_ACTION_RESULT = 5060, // S->C  netRecvSowelActionResult
    OP_RECV_DB_QUICK_SLOT_INFO_SAVE = 5067, // C->S  recvDBQuickSlotInfoSave
    OP_CHAR_CHARGE_SHOP_BUY = 5070, // C->S  charChargeShopBuy
    OP_BOOSTER_SHOP_SEND_BUY = 5072, // C->S
    OP_NET_HANDLE_CRAFT_RESULT_PACKET = 5073, // S->C  netHandleCraftResultPacket
    OP_BOOSTER_SHOP_SEND_BUY_PRESENT = 5074, // C->S
    OP_BANK_GOLD_RESULT_PACKET_HANDLE = 5075, // S->C  bankGoldResultPacket_Handle
    OP_TRADE_ON_ITEM_BIND_RESULT = 5077, // S->C  tradeOnItemBindResult
    OP_PARTY_MEMBER_5078 = 5078, // S->C  Recv_PartyMember
    OP_RECV_NORMAL_CHAT_CLIENT = 5084, // C->S  recvNormalChatClient  (отв.: OP_CHAT_NORMAL=5085)
    OP_CHAT_NORMAL = 5085, // S->C  Recv_ChatNormal
    OP_CHAT_MESSAGE = 5087, // S->C  Recv_ChatMessage
    OP_CHAT_WHISPER = 5089, // S->C  Recv_ChatWhisper
    OP_PARTY_SEND_NEW_PARTY_REQ = 5090, // C->S  \tTarget don't exist  (отв.: OP_MAP_DECO_RELEASE_ALL_RESOURCES=5091)
    OP_MAP_DECO_RELEASE_ALL_RESOURCES = 5091, // S->C  mapDecoReleaseAllResources
    OP_RESPONSE_USER_NEW_PARTY_CREATE = 5092, // C->S  responseUserNewPartyCreate  (отв.: OP_GM_NEWPARTY_RESPONSEUSER_RES=5093)
    OP_GM_NEWPARTY_RESPONSEUSER_RES = 5093, // S->C  Recv_GM_NEWPARTY_RESPONSEUSER_RES
    OP_PARTY_SEND_JOIN_REQ = 5094, // C->S  \tTarget don't exist  (отв.: OP_PARTY_MEMBER_5095=5095)
    OP_PARTY_MEMBER_5095 = 5095, // S->C  Recv_PartyMember
    OP_PARTY_SEND_INVITE_ACCEPT = 5096, // C->S  tOldPartyRes  (отв.: OP_GM_INVITEPARTY_RESPONSE_RES=5097)
    OP_GM_INVITEPARTY_RESPONSE_RES = 5097, // S->C  Recv_GM_INVITEPARTY_RESPONSE_RES
    OP_PARTY_SEND_INVITE_BY_NAME = 5098, // C->S  tLeaveParty  (отв.: OP_GM_PARTY_LEAVEUSER_RES=5099)
    OP_GM_PARTY_LEAVEUSER_RES = 5099, // S->C  Recv_GM_PARTY_LEAVEUSER_RES
    OP_PARTY_SEND_BANISH_BY_NAME = 5100, // C->S  tBanish  (отв.: OP_GM_PARTY_BANISHUSER_RES=5101)
    OP_GM_PARTY_BANISHUSER_RES = 5101, // S->C  Recv_GM_PARTY_BANISHUSER_RES
    OP_GM_PARTY_PARTYMASTER_CHANGE = 5102, // S->C  Recv_GM_PARTY_PARTYMASTER_CHANGE
    OP_GM_PARTY_DESTROY = 5103, // S->C  Recv_GM_PARTY_DESTROY
    OP_GM_PARTY_POSITION = 5104, // S->C  Recv_GM_PARTY_POSITION
    OP_GM_PARTY_CHAT_RES = 5106, // S->C  Recv_GM_PARTY_CHAT_RES
    OP_RECV_GM_MEMBER_INFO = 5107, // S->C  recvGmMemberInfo
    OP_GM_PARTY_UPDATE_STATE = 5108, // S->C  Recv_GM_PARTY_UPDATE_STATE
    OP_RECV_GM_PARTY_UPDATE_PARAM = 5109, // S->C  recvGmPartyUpdateParam
    OP_SOULMATE_UPDATE_INFO = 5110, // S->C  soulmateUpdateInfo
    OP_SOULMATE_WND_APPLY_INFO_PACKET = 5111, // S->C  soulmateWnd_ApplyInfoPacket
    OP_BUFF_UI_LOAD_AND_REFRESH_ICONS = 5112, // S->C  buffUiLoadAndRefreshIcons
    OP_CHAR_INDUN_INDUN_QUESTION_OPEN = 5113, // C->S  charIndunIndunQuestionOpen  (отв.: OP_BUFF_UI_LOAD_AND_REFRESH_ICONS=5112)

    // ==== Соулмейт / скиллы / обмен (52xx-53xx) ====
    OP_REQUEST_USER_SOUL_MATE_CREATE = 5200, // C->S  requestUserSoulMateCreate  (отв.: OP_COUPLE_RECV_PROPOSAL_NOTICE=5201)
    OP_COUPLE_RECV_PROPOSAL_NOTICE = 5201, // S->C  coupleRecvProposalNotice
    OP_RESPONSE_USER_SOUL_MATE_CREATE = 5202, // C->S  responseUserSoulMateCreate  (отв.: OP_SOULMATE_BOND=5203)
    OP_SOULMATE_BOND = 5203, // S->C  Recv_SoulmateBond
    OP_REQUEST_USER_SOUL_MATE_DESTORY = 5204, // C->S  requestUserSoulMateDestory  (отв.: OP_ITEM_ENCHANT_ON_RESULT_PACKET=5205)
    OP_ITEM_ENCHANT_ON_RESULT_PACKET = 5205, // S->C  itemEnchant_OnResultPacket
    OP_RESPONSE_USER_SOUL_MATE_DESTORY = 5206, // C->S  responseUserSoulMateDestory  (отв.: OP_SOULMATE_WND_HANDLE_RESPONSE_PACKET=5207)
    OP_SOULMATE_WND_HANDLE_RESPONSE_PACKET = 5207, // S->C  soulmateWnd_handleResponsePacket
    OP_PARTY_MINIMAP_MARK = 5220, // S->C  Recv_PartyMinimapMark
    OP_SOULMATE_SEND_BUFF_DISABLE = 5221, // C->S  ~Recv Party Minimap Mark (по парному клиент-опкоду)  (отв.: OP_PARTY_MINIMAP_MARK=5220)
    OP_MINI_MAP_UPDATE_LOCAL_PLAYER_POS = 5223, // S->C  miniMapUpdateLocalPlayerPos
    OP_TRADE_WND_RESET_AND_NOTIFY = 5226, // S->C  tradeWnd_resetAndNotify
    OP_TRADE_WND_UPDATE_NOTICE_AND_TARGET = 5227, // S->C  tradeWnd_updateNoticeAndTarget
    OP_SKILL_HOTBAR_REFRESH_SLOTS = 5228, // S->C  skillHotbarRefreshSlots
    OP_NET_RECV_BUFF_STATE_APPLY = 5229, // S->C  netRecvBuffStateApply
    OP_NET_REMOVE_SKILL_BUFF_STATE = 5230, // S->C  netRemoveSkillBuffState
    OP_RECV_CHAR_SOUL_MATE_SKILL_USE = 5231, // C->S  recvCharSoulMateSkillUse  (отв.: OP_NET_HANDLE_SOULMATE_SKILL_STATE=5232)
    OP_NET_HANDLE_SOULMATE_SKILL_STATE = 5232, // S->C  net_handleSoulmateSkillState
    OP_NET_RECV_SOULMATE_SKILL_APPLY = 5233, // S->C  Net_RecvSoulmateSkillApply
    OP_NET_HANDLE_CHAR_STATE_NOTICE = 5234, // S->C  netHandleCharStateNotice
    // ★ ИМЯ ОБМАНЧИВО (проверено 24.08.2026): к заточке пакет отношения не имеет. Это ЗОВ ДРУГА
    //   (Mate Recall): сохраняется в буфер и разбирается из подтверждающего окна обработчиком
    //   `netHandleChannelPortal@0xF468A0` (`ui_core.c:57412`, case 0x26), нотис 8163
    //   «Друг призывает тебя. Ответишь на зов?». Сервер: `sub_49E960@0x49E960`, 42 б.
    OP_SOULMATE_RECALL_REQUEST = 5236, // S->C  зов друга (Mate Recall); прежнее имя — миснейм
    OP_SEND_RESPONSE_USER_TO_REQUEST_USER_MATE_RECALL_SKILL = 5237, // C<->S  sendResponseUserToRequestUserMateRecallSkill
    OP_NET_HANDLE_SKILL_SLOT_ENABLE = 5238, // S->C  netHandleSkillSlotEnable
    OP_SKILL_WND_PARSE_SKILL_UPDATE_PACKET = 5251, // S->C  skillWnd_ParseSkillUpdatePacket
    OP_NET_RECV_LEARNED_SKILL_LIST = 5252, // S->C  Net_RecvLearnedSkillList
    OP_RECV_SKILL_POINT_SKILL_ACTIVE = 5253, // C->S  recvSkillPointSkillActive  (отв.: OP_SKILL_HANDLE_UPGRADE_RESPONSE=5254)
    OP_SKILL_HANDLE_UPGRADE_RESPONSE = 5254, // S->C  skillHandleUpgradeResponse
    // ★★★ C->S  ПРИМЕНЕНИЕ ОБЫЧНОГО СКИЛЛА (коды 501..1200). Тело 60 б — БАЙТ В БАЙТ то же, что у
    //   6158 (`OP_SOULGUARDER_SKILL_USE` ниже): один и тот же отправитель `skillCastSelfSendPacket
    //   @0x9FE210` (skill_buff.c:2224-2234) выбирает опкод по коду скилла — `< 1201 → 5256`,
    //   `>= 1201 → 6158`. Раскладка:
    //     [0..7]   анти-реплей токен (QPC + 30 бит счётчика)
    //     [8..23]  MD5(head, 7)
    //     [24..30] head 7 б: i16 skillCode | u8 1 | i16 тайл X каста | i16 тайл Z каста
    //     [31..59] hit-запись 29 б: u8 targetType | i16 targetIdx | … | i16 gndX@+15 | i16 gndZ@+17
    //   Сервер `CSkillManager::RecvChar_SkillUse@0x46F900` (GameServer.c:45840, case 20024) читает
    //   head с `pkt+33`, записи с `pkt+40+29·i`, `N = (len−41)/29 ≤ 20`; клиент всегда шлёт N = 1.
    //   ⚠ `targetIdx` — i16 СЕССИОННЫЙ индекс (renderId моба / userIdx игрока), НЕ charId
    //     (закон charid-vs-usernum: i32 charId сюда не влезает, сервер молча игнорировал бы).
    //   Ответы: 5257 (отказ/результат), 5270 (старт каста), 5271 (конец каста).
    OP_SKILL_USE = 5256, // C->S  RecvChar_SkillUse  (отв.: 5257 / 5270 / 5271)
    // S->C  `Recv_SkillCast@0xF4E370` (network.c:44805) — РЕЗУЛЬТАТ применения обычного скилла.
    //   Диспетчер эталона: `case 0x1489: goto LABEL_723` → та же функция, что и у 6159
    //   (network.c:1819 → :654). 8 б шапки + N×29 б записей попаданий:
    //     +0 u8 код (1 = успех, иначе индекс нотиса в `dword_15B5D78`) | +1 i16 casterUserIdx |
    //     +3 i16 skillCode | +5 u8 1 | +6 i16 уровень навыка.
    //   Отказ (8 б) уходит ТОЛЬКО кастеру (GameServer.c:46251); результат с уроном — по сектору
    //   (`sub_470DB0`, :46450).
    OP_SKILL_CAST_RESULT = 5257, // S->C  Recv_SkillCast (обычные скиллы)
    OP_RECV_TOGGLE_SKILL_STATE_END = 5267, // C->S  recvToggleSkillStateEnd  (отв.: OP_NET_HANDLER_REMOVE_CHAR_SKILL_EFFECT=5268)
    OP_NET_HANDLER_REMOVE_CHAR_SKILL_EFFECT = 5268, // S->C  netHandler_RemoveCharSkillEffect
    // ★ S->C  `Recv_SkillCastIdle@0xF4D9C0` — НАЧАЛО каста обычного скилла (motionType 1/7),
    //   по сектору (GameServer.c:46469). Диспетчер эталона: `case 0x1496: goto LABEL_724`
    //   (network.c:1824 → :658) — та же функция, что и у 6160. Шапка 7 б:
    //     +0 i16 casterUserIdx | +2 i16 skillCode | +4 i16 уровень | +6 u8 1, далее N×29.
    //   Клиент ставит состояние 14 и `slot.field_56C = 0` («ждём сервер», см. отчёт §5.4).
    OP_SKILL_CAST_IDLE_NORMAL = 5270, // S->C  Recv_SkillCastIdle (обычные скиллы)
    // ⚠ ИСПРАВЛЕНО 2026-08-09 (закон fix-original-defects + verify-prior-data-against-binary):
    //   раньше здесь стояло `OP_SHOP_ON_RECV_TRANSACTION_RESULT = 5270`, что ЗАНИМАЛО опкод старта
    //   каста. В эталоне `shop_OnRecvTransactionResult` висит на **5071** — `network.c:1830`
    //   (`else if ( opcode == 5071 )`), а 5270 (0x1496) идёт в `Recv_SkillCastIdle` (`network.c:1824`).
    OP_SHOP_ON_RECV_TRANSACTION_RESULT = 5071, // S->C  shop_OnRecvTransactionResult
    // S->C  «каст ОБЫЧНОГО скилла начался/кончился» — тот же обработчик `recvOtherCharSkillUse@0xF4D7D0`,
    //   что и у 6161 (диспетчер network.c:1606 `LABEL_725`, ветка `opcode == 5271`). Рассылается по
    //   всему сектору (`sub_52E630`, GameServer.c:46427 / :46595). 7 б: u8 result, i16 userIdx,
    //   i16 skillId, i16 flag. Отличие от 6161 — только класс скилла (обычный / хранителя).
    OP_SKILL_USE_STATE_BROADCAST = 5271, // S->C  recvOtherCharSkillUse (обычные скиллы)
    OP_PKT_HANDLER_CHAR_SKILL_EFFECT_RESTART = 5272, // S->C  pktHandlerCharSkillEffectRestart
    OP_SKILL_EFFECT_REMOVE_FULL = 5273, // S->C  skillEffectRemoveFull
    OP_PKT_HANDLER_CHAR_FX_STOP = 5274, // S->C  pktHandlerCharFxStop
    OP_CHAR_EXCHANGE_REQ = 5282, // C->S  charExchangeReq  (отв.: OP_PARTY_MEMBER_5283=5283)
    OP_PARTY_MEMBER_5283 = 5283, // S->C  Recv_PartyMember
    OP_CHAR_EXCHANGE_ACCEPT_REQ = 5284, // C->S  charExchangeAcceptReq  (отв.: OP_EXCHANGE_REQUEST_RESULT=5285)
    OP_EXCHANGE_REQUEST_RESULT = 5285, // S->C  Recv_Exchange_RequestResult
    OP_CHAR_EXCHANAGE_CANCEL_REQ = 5286, // C->S  charExchanageCancelReq  (отв.: OP_EXCHANGE_CLOSED=5287)
    OP_EXCHANGE_CLOSED = 5287, // S->C  Recv_Exchange_Closed
    OP_CHAR_EXCHANGE_DRAG_DROP_ITEM = 5288, // C->S  charExchangeDragDropItem  (отв.: OP_EXCHANGE_SLOT_UPDATE=5289)
    OP_EXCHANGE_SLOT_UPDATE = 5289, // S->C  Recv_Exchange_SlotUpdate
    OP_CHAR_EXCHANGE_DRAG_DROP_DELETE_ITEM = 5290, // C->S  charExchangeDragDropDeleteItem  (отв.: OP_EXCHANGE_SLOT_UPDATE=5289)
    OP_CHAR_EXCHANGE_FIX_REQ = 5291, // C->S  charExchangeFixReq  (отв.: OP_EXCHANGE_LOCK_STATE=5292)
    OP_EXCHANGE_LOCK_STATE = 5292, // S->C  Recv_Exchange_LockState
    OP_CHAR_EXCHANGE_COMPLETE_REQ = 5293, // C->S  charExchangeCompleteReq  (отв.: OP_EXCHANGE_COMPLETE_FULL=5294)
    OP_EXCHANGE_COMPLETE_FULL = 5294, // S->C  Recv_Exchange_Complete_Full
    OP_EXCHANGE_COMPLETE = 5295, // S->C  Recv_Exchange_Complete
    OP_NET_CLEAR_ALL_CHARACTER_BUFFS_PACKET = 5306, // S->C  netClearAllCharacterBuffsPacket
    OP_NET_CLEAR_CHARACTER_BUFFS_PACKET = 5307, // S->C  netClearCharacterBuffsPacket

    // ==== Внешность / Soul Jar / торговля (54xx) ====
    OP_SEND_AGAIN_USER_INFO = 5416, // C->S  sendAgainUserInfo  (отв.: OP_RECV_PACKET_PLAYER_APPEARANCE=5417)
    OP_RECV_PACKET_PLAYER_APPEARANCE = 5417, // S->C  RecvPacket_PlayerAppearance
    OP_RECV_PACKET_VISIBLE_PLAYER_LIST = 5418, // S->C  RecvPacket_VisiblePlayerList
    OP_SOUL_JAR_REQUEST_USER_REQ = 5419, // C->S  soulJarRequestUserReq  (отв.: OP_NET_RECV_TRADE_STATUS_RESULT=5420)
    OP_NET_RECV_TRADE_STATUS_RESULT = 5420, // S->C  netRecvTradeStatusResult
    OP_C_TRADE_ON_TARGET_INFO_PACKET = 5421, // S->C  cTrade_OnTargetInfoPacket
    OP_SOUL_JAR_RESPONSE_USER_RES = 5422, // C->S  soulJarResponseUserRes  (отв.: OP_C_TRADE_ON_TARGET_INFO_PACKET=5421)
    OP_SOUL_JAR_FORWARD_EXP_REQ = 5423, // C->S  soulJarForwardExpReq  (отв.: OP_EXP_GIFT_BUFF=5424)
    OP_EXP_GIFT_BUFF = 5424, // S->C  Recv_ExpGiftBuff
    OP_RECV_COPY_POS22_HANDLER = 5425, // S->C  recvCopyPos22Handler
    OP_POTION_BUFF = 5426, // S->C  Recv_PotionBuff
    OP_CHAR_SOUL_MATE_RECALL_POSITION = 5427, // C<->S  charSoulMateRecallPosition
    OP_CHAR_E_MOTION_MINING_END = 5428, // C->S  charEMotionMiningEnd  (отв.: OP_CHAR_SOUL_MATE_RECALL_POSITION=5427)
    OP_MINING_RESULT = 5430, // S->C  итог копки {u8 итог, i16 userNum} (network.c:1530); ★ 10.10.2026 переименован из миснейма OP_EFFECT_SPAWN_BY_NAME (имя было по вызову Effect_SpawnByName внутри обработчика)
    OP_SKILL_APPLY_STATE_UPDATE = 5432, // S->C  skillApplyStateUpdate
    OP_RECV_SHELL_ITEM_OPTION_EXTRACTION = 5433, // C->S  recvShellItemOptionExtraction  (отв.: OP_EXTRACT_SHOP=5434)
    OP_EXTRACT_SHOP = 5434, // S->C  Recv_ExtractShop
    OP_CHAR_FUNCTION_ITEM_USE = 5450, // C->S  charFunctionItemUse
    // S->C  netRecvItemBuffApply@0xF23C60 (диспетч network.c:1434, тело 68 б = pPacket+9).
    //   Body[0] = подтип эффекта (switch на 60+ веток), Body[3..6] = itemCode.
    //   ★ ВЕТКА case 50 (network.c:36535-36551) — ЕДИНСТВЕННЫЙ в клиентском срезе канал МАКСИМАЛЬНОГО
    //     веса: `char->gap_43B[13] (=+0x448, i16) = HIWORD(body[16])` → Body[66..67],
    //     `HUD_UpdateMiniWeightBar(SLOWORD(body[15]))` → текущий вес Body[60..61] (i16).
    OP_NET_RECV_ITEM_BUFF_APPLY = 5451,
    OP_RECV_ITEM_REMOVE_BY_ID = 5452, // S->C  recvItemRemoveById
    OP_RECV_ITEM_APPLY_PM_BUFF = 5453, // S->C  recvItemApplyPMBuff
    OP_RECV_ITEM_REMOVE_BY_TABLE2_RECORD = 5454, // S->C  recvItemRemoveByTable2Record
    OP_POP_FUNCTION_ITEM_STATE_TOGGLE = 5455, // C->S  popFunctionItemStateToggle  (отв.: OP_RECV_WAREHOUSE_CHANGE_BAG_PAGE=5456)
    OP_RECV_WAREHOUSE_CHANGE_BAG_PAGE = 5456, // S->C  recvWarehouseChangeBagPage
    OP_RECV_INVENTORY_CONFIRM_DYE = 5457, // S->C  recvInventoryConfirmDye
    OP_RECV_CLIENT_FUNCTION_ITEM_MEET = 5458, // C->S  recvClientFunctionItemMeet  (отв.: OP_RECV_ITEM_STATUS_NOTICE=5459)
    OP_RECV_ITEM_STATUS_NOTICE = 5459, // S->C  recvItemStatusNotice
    OP_RECV_SKILL_TREE_RESET_SLOTS = 5462, // S->C  recvSkillTreeResetSlots
    OP_RECV_SKILL_LEARN_RESULT = 5464, // S->C  recvSkillLearnResult
    OP_NET_RECV_SKILL_COOL_TIME = 5465, // S->C  Net_RecvSkillCoolTime
    OP_RECV_MAC_VALUE_INQUIRY = 5466, // C->S  recvMacValueInquiry  (отв.: OP_NET_HANDLE_AUTH_RESULT=5467)
    OP_NET_HANDLE_AUTH_RESULT = 5467, // S->C  netHandleAuthResult
    OP_RECV_GACHA_ITEM_USE = 5468, // C->S  recvGachaItemUse  (отв.: OP_BOOSTER_SHOP_RESULT=5469)
    OP_BOOSTER_SHOP_RESULT = 5469, // S->C  Recv_BoosterShopResult
    // ★ ПЕРЕИМЕНОВАН 2026-08-17: имя `OP_RECV_GACHA_ITEM_USE_5470` было МИСНЕЙМОМ — это НЕ гача,
    //   а ПАРИКМАХЕРСКАЯ. Доказательство: серверный обработчик `Recv_GachaItemUse_0@0x4F59C0`
    //   внутри печатает `Recv_HairShopBuy()`, а отправитель в клиенте — `hairShop_SendApplyRequest@0xC4D130`.
    //   Ответ 5471 = `hairShop_HandleResult`, 5472 = применение причёски/шлема чужому игроку.
    OP_RECV_HAIR_SHOP_BUY = 5470, // C->S  hairShop_SendApplyRequest  (отв.: OP_HAIR_SHOP_HANDLE_RESULT=5471)
    OP_HAIR_SHOP_HANDLE_RESULT = 5471, // S->C  hairShop_HandleResult
    OP_CHAR_PACKET_APPLY_HELMET = 5472, // S->C  CharPacket_ApplyHelmet
    OP_ON_RECV_OTHER_PLAYER_BOOST = 5473, // S->C  OnRecv_OtherPlayerBoost
    OP_RECV_GACHA_ITEM_EFFECT = 5474, // C->S  recvGachaItemEffect  (отв.: OP_ON_RECV_OTHER_PLAYER_BOOST=5473)
    OP_CHAR_ITEM_TRADE_REQ = 5475, // C->S  charItemTradeReq  (отв.: OP_ITEM_GRADE_CHANGE_RESULT=5476)
    OP_ITEM_GRADE_CHANGE_RESULT = 5476, // S->C  Recv_ItemGradeChange_Result
    OP_RECV_CONTINENT_PORTAL_MOVE = 5477, // C->S  recvContinentPortalMove  (отв.: OP_ITEM_GRADE_CHANGE_RESULT=5476)
    OP_SEND_TARGET_CHAR_INFORMATION = 5478, // C->S  sendTargetCharInformation  (отв.: OP_PLAYER_INFO_WIN_ON_RECV_CHAR_DATA=5479)
    OP_PLAYER_INFO_WIN_ON_RECV_CHAR_DATA = 5479, // S->C  playerInfoWin_onRecvCharData
    OP_SEND_WHO_COMMAND = 5480, // C->S  sendWhoCommand
    // S->C  страница списка вкладки «Кто?» (communityWhoAppendListFromPacket@0xEBFC10):
    //   записи по 35 б, до 18 записей (630 б) на пакет; конец списка — отдельный пустой 5482.
    OP_COMMUNITY_WHO_APPEND_LIST = 5481, // S->C  communityWhoAppendListFromPacket
    OP_UI_COMMUNITY_WHO_UPDATE_PAGING = 5482, // S->C  uiCommunityWhoUpdatePaging
    OP_SEND_SOUL_SEARCH = 5483, // C->S  sendSoulSearch  (отв.: OP_SOULMATE_PARSE_SEARCH_RESULT=5484)
    OP_SOULMATE_PARSE_SEARCH_RESULT = 5484, // S->C  soulmateParseSearchResult
    OP_FIND_SOULMATE_WIN_SHOW = 5485, // S->C  findSoulmateWin_Show

    // ==== GM-команды / питомцы / рыбалка / соулмейт-заметки (60xx-61xx) ====
    OP_GAME_GUARD_ON_CALLBACK = 6000, // C->S
    // S->C  ЭХО ПРОВЕРКИ СОСТОЯНИЯ ПЕРСОНАЖА. Сервер sub_52BFF0 (IsCheck_CharFlag, GameServer.c:113825)
    //   вызывается перед обработкой действий (4026/4028/5000/5002/5006/5012/5016/5022/5024/5026/5028/
    //   5031/5231/5256/5282/6158/6166/6176) и ВСЕГДА шлёт этот пакет. Тело 3б:
    //   [0..1] i16 проверяемый опкод, [2] u8 причина: 0=ОК (действие пойдёт дальше),
    //   1=ПЕРСОНАЖ МЁРТВ (CUser+79, sub_4CB030), 2/3/4/5 = прочие блокирующие состояния.
    //   Причина != 0 → сервер молча отклоняет действие (для 5012 это 5013 result=0).
    OP_ACTION_FLAG_CHECK = 6001, // S->C  IsCheck_CharFlag echo
    OP_NET_RECV_RESPAWN_RESULT = 6004, // S->C  Recv_RespawnResult@0xF3A840 — enter-world/respawn РЕЗУЛЬТАТ (body[0]: 0/1/3=fail, 2=успех)
    // ★★★ [ОРИГИНАЛ] GM-«скрыться». C->S, приёмник `Send_HideCommand@0x54E970` (GameServer.c:128161,
    //   диспетчер :19627 веткой `v4 == 6005`). Конверт СЕКРЕТ+MD5 (`sub_523F00(..., 2, ..., a4=1)`),
    //   payload РОВНО 2 байта: [0] сервер перетирает результатом, [1] — 1 = скрыться, иначе показаться.
    //   Отправитель в оригинальном клиенте ЕСТЬ — `sub_9F7AE0(char bHide)`, но он НЕ ВЫЗЫВАЕТСЯ
    //   ни из одного места розничной сборки (единственная ссылка — линковочный трамплин 0x92A4A9).
    //   В декомпиле `RE/ida/export/AsdaStory_game.c` этой функции нет вовсе — дыра между
    //   sub_9F7A60 и sub_9F80C0; снята дизассемблированием `client\system\AsdaStory.exe`.
    OP_GM_HIDE_REQ = 6005, // C->S  Send_HideCommand — GM-невидимость, переключатель (payload 2 б)
    OP_LOCAL_PLAYER_INPUT_TOGGLE_BY_FLAGS = 6006, // S->C  localPlayerInputToggleByFlags — ОТВЕТ на 6005: [0] 1=принято/0=отказ, [1] 1=скрыт/0=виден
    OP_PORTAL_CHANNEL_TRANSFER_BEGIN = 6013, // S->C  portalChannelTransferBegin
    OP_NET_HANDLE_CHANNEL_TRANSFER = 6015, // S->C  netHandleChannelTransfer
    OP_NET_SHOW_DISCONNECT_POPUP_AND_CHAT_NOTICE = 6019, // S->C  netShowDisconnectPopupAndChatNotice
    // ★ НАША правка (серверный патч P25, AsdaPatch.dll): GM-команда «пересчитать
    //   рейтинги СЕЙЧАС». В стоке сервер принимает этот опкод ПУСТЫМ кейсом
    //   (0x1784 @0x42CDFB — только два маркера прогресса и уход в хвост switch),
    //   а оригинальный клиент его не шлёт вовсе (во всём декомпиле 6020 —
    //   только id контрола окна). Патч оживляет кейс: сервер проверяет
    //   операторский гейт (UserIndex 0…5000 + GMFlag + анти-тампер + MD5) и
    //   просит DBManager сделать ОБА пересчёта — звания 65…69 и место в топе
    //   по очкам славы (то, что в стоке идёт в 03:00 у ChannelManager).
    //   Тело (payload 8 б): +0 u8 что (0=оба, 1=звания, 2=топ), +1..7 нули.
    //   Ответ приходит СТОКОВЫМ 6561 — отдельного S->C опкода не заводится.
    OP_GM_RATING_RECALC = 6020, // C->S  ★ наш (P25): немедленный пересчёт рейтингов
    OP_NET_HANDLE_AUTO_RESURRECT_READY = 6023, // S->C  netHandleAutoResurrectReady
    OP_CHAR_ON_LOCAL_PLAYER_MAP_ENTRY = 6025, // S->C  char_onLocalPlayerMapEntry
    OP_UI_SHOW_LOCALIZED_NOTICE2118 = 6027, // S->C  uiShowLocalizedNotice2118
    OP_ITEM_MIX_RESULT_DUMP_TO_CHAT = 6034, // S->C  ItemMixResult_DumpToChat
    // ★ ac0r (12.10.2026, античит): 6036 — операторский запрос «значения персонажа» (`Recv_OperatorCommand_GetValue@0x551970`).
    //   Отправитель оригинала `0xA0B550` (снят дизассемблированием `AsdaStory.exe` IDB-сборки; вызовов в рознице нет):
    //   тело = ТОЛЬКО 8 б анти-реплей токена (0x57A7A5 + счётчик), без MD5 и полезной нагрузки,
    //   `CNetwork_Send(field1C, body, 8, 0x1794, 1, 1, 1, 0, 0)`. Сервер: гейт `sub_52ECF0` (+ GM-флаг P47) → токен
    //   `sub_524390` → только ЧТЕНИЕ полей CUser → 6037 на NpcServer (`Process_PlayerInfoInBattle@0x457D80`, тоже только
    //   чтение) → 6037 (135 б) ТОЛЬКО отправителю (`sub_551B60`). Без прав — ни обработчика, ни ответа.
    //   Порт шлёт его как пробу прав GM-меню (вместо 6005, который меняет состояние действия, см. `SendGmRightsProbe6036`).
    OP_GM_GET_VALUE_REQ = 6036, // C->S  Recv_OperatorCommand_GetValue — проба прав оператора (тело 8 б: токен)
    OP_CHAT_DUMP_CHARACTER_STATS = 6037, // S->C  chatDumpCharacterStats — ответ на 6036 (порт: подтверждение прав, см. HandlePacket)
    OP_EVENT_SET = 6039, // S->C  Recv_EventSet
    OP_EVENT_OFF = 6041, // S->C  Recv_EventOff
    OP_ITEM_POP_SHOW_TRANSFER_RESULT = 6043, // S->C  ItemPop_ShowTransferResult
    OP_NET_HANDLE_ITEM_SLOT_UPDATE = 6044, // S->C  netHandleItemSlotUpdate
    OP_RECV_OPERATOR_COMMAND_MONSTER_POP = 6045, // C->S  recvOperatorCommandMonsterPop  (ответа нет: 6044 — выдача предмета 6042, не этот; group2_spec §7, 19.10.2026)
    OP_RECV_OPERATOR_COMMAND_MONSTER_RETURN = 6046, // C->S  recvOperatorCommandMonsterReturn  (ответа нет: 6047 — не ответ на этот; group2_spec §8, 19.10.2026)
    OP_NET_HANDLE_SESSION_FIELD_UPDATE = 6047, // S->C  netHandleSessionFieldUpdate
    OP_NET_ON_GUILD_ICON_NOTICE = 6048, // S->C  net_OnGuildIconNotice
    OP_NET_ON_DUEL_STAGE_NOTICE = 6049, // S->C  net_OnDuelStageNotice
    OP_NET_ON_EXP_GIFT_NOTICE = 6051, // S->C  Net_OnExpGiftNotice
    OP_SOULMATE_SEND_NOTE_REQ = 6052, // C->S  soulmateSendNoteReq  (отв.: OP_GUILD_HANDLE_EVENT_NOTIFY=6053)
    OP_GUILD_HANDLE_EVENT_NOTIFY = 6053, // S->C  guildHandleEventNotify
    OP_SOULMATE_RECEIVE_NOTE_CONFIRM = 6054, // C->S  soulmateReceiveNoteConfirm  (отв.: OP_PARTY_MEMBER_SYNC_ICONS=6055)
    OP_PARTY_MEMBER_SYNC_ICONS = 6055, // S->C  partyMemberSyncIcons
    OP_SKILL_WND_ON_SKILL_LIST_PACKET = 6056, // S->C  skillWnd_OnSkillListPacket
    OP_SKILL_WND_RESET_ELEMENT_SLOTS = 6057, // S->C  skillWnd_ResetElementSlots
    OP_SKILL_HOTBAR_REFRESH_FOR_USER = 6058, // S->C  skillHotbarRefreshForUser
    OP_EVENT_NCP_PROCESS_REQ = 6059, // C->S  eventNcpProcessReq  (отв.: OP_NET_HANDLE_ENCHANT_RESULT_NOTICE=6060)
    OP_NET_HANDLE_ENCHANT_RESULT_NOTICE = 6060, // S->C  netHandleEnchantResultNotice
    OP_APR_CLIENT_OVER_SIZE_CHECK = 6063, // C->S  aprClientOverSizeCheck
    OP_UI_TITLE_ON_JOB_CHANGE_RESULT = 6065, // S->C  uiTitle_OnJobChangeResult
    OP_NET_ON_CHAR_EQUIP_UPDATE = 6066, // S->C  netOnCharEquipUpdate
    OP_CHANGE_ACTIVE_FAME = 6067, // C->S  changeActiveFame  (отв.: OP_CHAR_TITLE_RECV_TITLE_SLOT_UPDATE=6068)
    OP_CHAR_TITLE_RECV_TITLE_SLOT_UPDATE = 6068, // S->C  charTitle_RecvTitleSlotUpdate
    OP_DELETE_FAME_INFO = 6069, // C->S  deleteFameInfo  (отв.: OP_QUEST_TRACK_BIT_TOGGLE_BY_USER_IDX=6070)
    OP_QUEST_TRACK_BIT_TOGGLE_BY_USER_IDX = 6070, // S->C  questTrackBitToggleByUserIdx
    OP_CHAR_MOVE_TO_CELL = 6071, // S->C  Recv_CharMoveToCell
    OP_CHAR_SYNC_EQUIP_SLOT_ENTRIES = 6073, // S->C  charSyncEquipSlotEntries
    OP_UPDATE_LOCAL_PLAYER_TARGET_SLOT = 6074, // S->C  updateLocalPlayerTargetSlot
    OP_DEBUG_DUMP_TARGET_COMBAT_STATS = 6075, // S->C  Debug_DumpTargetCombatStats
    OP_RECV_USER_CHASE_REQ = 6076, // C->S  recvUserChaseReq  (отв.: OP_RECV_MAP_TRANSFER_TARGET_POS=6077)
    OP_RECV_MAP_TRANSFER_TARGET_POS = 6077, // S->C  recvMapTransferTargetPos
    OP_CHAR_NET_UPDATE_FIELD_REFRESH_PLATE = 6078, // S->C  charNet_UpdateFieldRefreshPlate
    OP_PKT_HANDLER_SKILL_CAST = 6079, // S->C  pktHandlerSkillCast
    OP_RECV_SOULMATE_INTRODUCTION = 6080, // C<->S  recvSoulmateIntroduction
    OP_RECV_USER_APR_INIT_ERROR = 6081, // C->S  recvUserAprInitError  (отв.: OP_RECV_SOULMATE_INTRODUCTION=6080)
    OP_RECV_GET_USER_ID = 6082, // C->S  recvGetUserID  (отв.: OP_ITEM_MALL_LOGIN_BEGIN=6083)
    OP_ITEM_MALL_LOGIN_BEGIN = 6083, // S->C  itemMallLoginBegin
    OP_RECV_USER_CHANNEL_MOVE = 6084, // C->S  recvUserChannelMove  (отв.: OP_NET_HANDLER_ON_GUILD_TITLE_RESULT=6085)
    OP_NET_HANDLER_ON_GUILD_TITLE_RESULT = 6085, // S->C  netHandler_OnGuildTitleResult
    OP_NET_HANDLER_ON_CHAR_DATA32 = 6086, // S->C  netHandler_OnCharData32
    OP_PARTY_MEMBER_6087 = 6087, // S->C  Recv_PartyMember
    OP_UI_SHOW_ITEM_STATUS_NOTICE = 6089, // S->C  uiShowItemStatusNotice
    OP_ITEM_SEND_CRAFT_REQUEST_EEE0 = 6091, // C->S
    OP_RECV_CHAR_RESURRECTION_CONFIRM = 6093, // C<->S  recvCharResurrectionConfirm
    OP_NET_SEND_CHAR_FIELD_SYNC_OPCODE6094 = 6094, // C->S  GM Recv_CharBlock()  (отв.: OP_RECV_CHAR_RESURRECTION_CONFIRM=6093)
    OP_RECV_CHAR_BLOCK = 6095, // C->S  recvCharBlock
    OP_RECV_OPEN_PACKAGE_ITEM = 6096, // C->S  recvOpenPackageItem  (отв.: OP_NET_ON_STORAGE_RESULT_PACKET=6097)
    OP_NET_ON_STORAGE_RESULT_PACKET = 6097, // S->C  netOnStorageResultPacket
    OP_EVENT_ITEM_REQUEST_REQ = 6098, // C->S  eventItemRequestReq  (отв.: OP_NET_HANDLE_TRADE_STATUS_NOTICE=6099)
    OP_NET_HANDLE_TRADE_STATUS_NOTICE = 6099, // S->C  netHandleTradeStatusNotice
    // ════════════════ ПИТОМЦЫ 6100…6123 + 6148/6149 ═══════════════════════════════════════════
    // ★ ПЕРЕИМЕНОВАНО 2026-09-21 (волна 1 порта питомцев). Девять ОТВЕТНЫХ опкодов раньше носили
    //   «refine»-имена — наследие миснеймов IDA: приёмники в эталоне лежат в `item_enhance.c`
    //   рядом с настоящей заточкой и Hex-Rays назвал их `refineWnd*`. На деле ВСЕ они — методы
    //   клиентской модели питомца `CPetData` (доказано: каждый ищет запись в списке
    //   `CPetData+0x1E4` по `petUniqueId` и пишет поля `CPetSlot`). Отчёт:
    //   `Reports/pet_windows_re_report.md` §8.4. Старые имена в коде порта не использовались
    //   (grep 2026-09-21: единственная ссылка была на 6120 в `AsdaNetSubsystem.cpp`).
    // ★ КОНВЕРТ C→S у ВСЕХ восьми запросов — «с секретом» (`SendSecured`): 8 б скрамблированного
    //   QPC + 16 б MD5(тело) + тело. Доказано на сервере: `sub_523F00(pkt, <тело>, userNum, 1)` —
    //   четвёртый аргумент 1 ⇒ MD5 с +17, тело с +33 (GameServer.c:34425, 34551, 35518, 35823,
    //   35971, 36238, 36429, 36624).
    // ★ РАЗМЕРЫ ТЕЛА ВЗЯТЫ С СЕРВЕРА, не с клиента: второй аргумент того же `sub_523F00`.
    OP_PET_CALL_REQ = 6100, // C->S  Recv_CallPetReq (GameServer.c:34404) — призыв/отзыв, тело 8 б (отв. 6101)
    OP_PET_CALL_RESULT = 6101, // S->C  CPetData::OnCallPetResult @0xDC85E0 (item_enhance.c:4465), тело 9 б
    OP_PET_INCUBATE_REQ = 6102, // C->S  Recv_IncubatorReq (GameServer.c:34503) — инкубатор, тело 13 б (отв. 6103)
    OP_PET_INCUBATE_RESULT = 6103, // S->C  CPetWin_OnResult @0xDC2CA0 (npc_mob_pet.c:5060), тело 18 б
    OP_PET_CONDITION_UPDATE = 6104, // S->C  CPetData::OnConditionUpdate @0xDC8B00 (item_enhance.c:4592), тело 29 б
    OP_PET_EXP_UPDATE = 6105, // S->C  CPetData::OnExpUpdate @0xDC8D10 (item_enhance.c:4660), тело 15 б
    OP_PET_LIST = 6106, // S->C  CPetData::OnPetList @0xDC7730 (network.c:25582), тело 5 + 46·N
    OP_PET_HUNGER_RECALL = 6107, // S->C  CPetData::OnHungerRecall @0xDC88A0 (item_enhance.c:4545), тело 8 б
    OP_PET_FEED_REQ = 6108, // C->S  Recv_PetConditionUp (GameServer.c:35479) — кормление, тело 11 б (отв. 6109)
    OP_PET_FEED_RESULT = 6109, // S->C  CPetData::OnFeedResult @0xDC9710, тело 89 б
    OP_PET_RELEASE_REQ = 6110, // C->S  Recv_PetLiberate_Req (GameServer.c:35798) — выбросить, тело 8 б (отв. 6111)
    OP_PET_RELEASE_RESULT = 6111, // S->C  CPetData::OnReleaseResult @0xDCA420 (item_enhance.c:4727), тело 9 б
    OP_PET_UNION_REQ = 6112, // C->S  Recv_PetUnion_Req (GameServer.c:35936) — эволюция/юнион, тело 18 б (отв. 6113)
    OP_PET_UNION_RESULT = 6113, // S->C  petUI_OnPetDataPacket @0xEE0920, тело 56 б
    // ★★★ ВОЛНА 8 (21.09.2026, пункт 9 сквозной ревизии питомцев): 6114 И 6116 ЗАВЕДЕНЫ С ПОМЕТКОЙ.
    //   Они ЕСТЬ в диспетчере живого сервера: `case 0x17E2:` → `Recv_PetCagePush_Req@0x459320`
    //   и `case 0x17E4:` → `Recv_PetCagePop_Req@0x459470` (GameServer.c:19465-19472; сами тела —
    //   :36123 и :36160). До волны 8 в реестре порта их не было вовсе, и дыра в нумерации
    //   6113→6118 читалась как «таких опкодов нет».
    //   ⚠ ЧТО ОНИ ДЕЛАЮТ — НЕИЗВЕСТНО, И ВЫДУМЫВАТЬ ЗАПРЕЩЕНО (закон `never-invent-only-original`):
    //   оба тела сервера ПУСТЫЕ по существу — читают 8 б в `a2`, сверяют пользователя, копируют
    //   0x22 б в локальный буфер и ВЫХОДЯТ, ничего не меняя и ничего не отвечая. Клиентских
    //   отправителей у них в срезе эталона тоже нет. Поэтому здесь только НОМЕРА и адреса: ни
    //   раскладки тела, ни ответа не называем.
    //   ★ Имена даны по именам серверных функций («клетка: положить / достать»), а не по догадке
    //   о смысле. Порт их НЕ шлёт и НЕ принимает — запись нужна, чтобы номер не считался свободным.
    OP_PET_CAGE_PUSH_REQ = 6114, // C->S  Recv_PetCagePush_Req @0x459320 (GameServer.c:36123) — ТЕЛО ПУСТОЕ, ответа нет
    OP_PET_CAGE_POP_REQ = 6116, // C->S  Recv_PetCagePop_Req  @0x459470 (GameServer.c:36160) — ТЕЛО ПУСТОЕ, ответа нет
    OP_PET_NAME_CHANGE_REQ = 6118, // C->S  Recv_PetNameChange_Req (GameServer.c:36214) — тело 24 б (отв. 6119)
    OP_PET_NAME_CHANGE_RESULT = 6119, // S->C  CPetData::OnRenameResult @0xDCB040, тело 25 б
    OP_PEER_ACTIVE_PET = 6120, // S->C  CPetData::EnqueuePeerPet @0xDCA620 — активный пет свой и чужих, тело 22 б
    OP_PET_SLOT_UP_REQ = 6121, // C->S  Recv_PetPossessionCountUp_Req (GameServer.c:36397) — тело 7 б (отв. 6122)
    OP_PET_SLOT_UP_RESULT = 6122, // S->C  CPetData::OnSlotUpResult @0xDCB9E0 (item_enhance.c:5014), тело 13 б
    OP_PET_SLOT_COUNT = 6123, // S->C  CPetData::OnSlotCount @0xDCBC50 (item_enhance.c:5075), тело 5 б — при входе в мир
    OP_RECV_RAID_MONSTER_CALL_REQ = 6124, // C->S  recvRaidMonsterCallReq  (отв.: OP_NET_HANDLE_ITEM_ACTION_RESULT=6125)
    OP_NET_HANDLE_ITEM_ACTION_RESULT = 6125, // S->C  netHandleItemActionResult
    OP_NET_HANDLE_CHAR_ID_BYTE_UPDATE = 6126, // S->C  netHandleCharIdByteUpdate
    OP_RECV_DICE_VALUE = 6127, // C->S  recvDiceValue  (отв.: OP_CHAT_NOTICE_SHOW_CHAR_EVENT=6128)
    OP_CHAT_NOTICE_SHOW_CHAR_EVENT = 6128, // S->C  chatNotice_ShowCharEvent
    OP_RECV_ITEM_INFO_DISPLAY = 6129, // C->S  recvItemInfoDisplay  (отв.: OP_ITEM_LIST_WND_ON_FILL_PACKET=6130)
    OP_ITEM_LIST_WND_ON_FILL_PACKET = 6130, // S->C  itemListWnd_OnFillPacket
    OP_PACKET_SHOW_ITEM_ACQUIRED_NOTICE = 6131, // S->C  packetShowItemAcquiredNotice
    OP_RECV_USER_EVENT_ITEM_STACK_REQ = 6132, // C->S  recvUserEventItemStackReq  (отв.: OP_ON_RECV_REFINE_SLOT_UPDATE=6133)
    OP_ON_RECV_REFINE_SLOT_UPDATE = 6133, // S->C  onRecvRefineSlotUpdate
    OP_RECV_CLIENT_FUNCTION_ITEM_SUMMON = 6134, // C->S  recvClientFunctionItemSummon  (отв.: OP_NET_RECV_INVENTORY_ITEM_UPDATE=6135)
    OP_NET_RECV_INVENTORY_ITEM_UPDATE = 6135, // S->C  netRecvInventoryItemUpdate
    OP_CAMP_WAR_BUILD_RESULT_TOOLTIP = 6136, // S->C  campWarBuildResultTooltip
    OP_RECV_CLIENT_PREMIUM_SUMMON_RELAY_RES = 6137, // C->S  recvClientPremiumSummonRelayRes  (отв.: OP_CAMP_WAR_BUILD_RESULT_TOOLTIP=6136)
    OP_RECV_CLIENT_CHAR_NAME_CHECK = 6138, // C->S  recvClientCharNameCheck  (отв.: OP_ITEM_TOOLTIP_BUILD_OR_NOTICE=6139)
    OP_ITEM_TOOLTIP_BUILD_OR_NOTICE = 6139, // S->C  itemTooltipBuildOrNotice
    OP_RECV_CLIENT_CHAR_NAME_CHANGE = 6140, // C->S  recvClientCharNameChange  (отв.: OP_RECV_ITEM_ACTION_RESULT_SHOW_NOTICE=6141)
    OP_RECV_ITEM_ACTION_RESULT_SHOW_NOTICE = 6141, // S->C  recvItemActionResult_ShowNotice
    OP_RECV_CLIENT_ADVANTAGE_BUFF_END = 6142, // C->S  recvClientAdvantageBuffEnd  (отв.: OP_RECV_ITEM_ACTION_RESULT_SHOW_NOTICE=6141)
    OP_INV_RECV_ITEM_REMOVE = 6144, // S->C  inv_RecvItemRemove
    OP_NET_HANDLE_COUNT_DOWN_START = 6145, // S->C  netHandleCountDownStart
    OP_RECV_CLIENT_TARGET_JUMP = 6146, // C->S  recvClientTargetJump  (отв.: OP_RECV_CLIENT_MAP_BLOCK=6147)
    OP_RECV_CLIENT_MAP_BLOCK = 6147, // C<->S  recvClientMapBlock
    OP_PET_LIMITBREAK_REQ = 6148, // C->S  RecvClient_PetMaxLevelUp (GameServer.c:36570) — рост, тело 9 б (отв. 6149)
    OP_PET_LIMITBREAK_RESULT = 6149, // S->C  pet_OnRecvItemOpResult @0xEE2370 (npc_mob_pet.c:7691), тело 110 б
    OP_RECV_CLIENT_TITLE_ITEM_CHECK = 6150, // C->S  recvClientTitleItemCheck  (отв.: OP_INVENTORY_UI_ON_ITEM_MOVE_PACKET=6151)
    OP_INVENTORY_UI_ON_ITEM_MOVE_PACKET = 6151, // S->C  inventoryUI_onItemMovePacket
    OP_PARTY_MARK_HELPER2 = 6152, // S->C  PartyMark_Helper2
    OP_NET_PARSE_RECORD_LIST_PACKET = 6153, // S->C  netParseRecordListPacket
    OP_RECV_PACKET_CHAR_STATE_INFO_6154 = 6154, // S->C  RecvPacket_CharStateInfo_6154
    OP_SPAWN_VISIBLE_PLAYER_FROM_PKT = 6155, // S->C  spawnVisiblePlayerFromPkt
    OP_CHAR_HANDLE_SLOT_KEY_PACKET = 6156, // S->C  charHandleSlotKeyPacket
    OP_RECV_SKILL_SLOT_STATE = 6157, // S->C  recvSkillSlotState
    // C->S  ПРИМЕНЕНИЕ СКИЛЛА ХРАНИТЕЛЯ (Soul Guardian). Тот же пакет, что обычный SKILL_USE 5256
    //   (`skillCastSelfSendPacket@0x9FE210`, skill_buff.c:2224-2234), но опкод выбирается по коду
    //   скилла: `skillCode < 1201` → 5256, `>= 1201` → 6158. Тело 60 б:
    //   [0..7] анти-реплей токен | [8..23] MD5(head,7) | [24..30] head = i16 skillCode, u8 1,
    //   i16 тайл X, i16 тайл Z | [31..59] запись цели 29 б: u8 1, i16 userIdx цели, дальше нули.
    //   Сервер: `CSoulGuarderManager::RecvChar_SoulGuarderSkillUse@0x48E4F0` (GameServer.c:56935),
    //   индекс в SoulGuarderSkillTable = skillCode − 1201; отказ → 6159 (8 б, код в байте 0).
    OP_SOULGUARDER_SKILL_USE = 6158, // C->S  RecvChar_SoulGuarderSkillUse  (отв.: 6159/6160/6161)
    OP_SKILL_CAST = 6159, // S->C  Recv_SkillCast
    OP_SKILL_CAST_IDLE = 6160, // S->C  Recv_SkillCastIdle
    // S->C  recvOtherCharSkillUse@0xF4D7D0 — «каст начался/завершился» (7 б: u8 result, i16 userIdx,
    //   i16 skillId, i16 flag). Сервер шлёт его по сектору из RecvBattle_SoulGuarderSkillUse
    //   (GameServer.c:57356) и RecvBattle_SoulGuarderSkillCastingEnd (:57518).
    OP_RECV_OTHER_CHAR_SKILL_USE = 6161, // S->C  recvOtherCharSkillUse
    OP_CHAR_ROSE_WAR_STORE_BUY = 6162, // C->S  charRoseWarStoreBuy  (отв.: OP_UI_HANDLE_ITEM_ACTION_RESULT=6163)
    OP_UI_HANDLE_ITEM_ACTION_RESULT = 6163, // S->C  uiHandleItemActionResult
    OP_UI_UPDATE_STAT_VALUE_LABEL = 6164, // S->C  uiUpdateStatValueLabel
    OP_RECV_FISHING_TOOL_USE = 6165, // C->S  recvFishingToolUse  (отв.: OP_INV_HANDLE_ITEM_SLOT_SYNC_PACKET=6166)
    OP_INV_HANDLE_ITEM_SLOT_SYNC_PACKET = 6166, // S->C  invHandleItemSlotSyncPacket
    OP_RECV_START_FISHING = 6167, // C->S  recvStartFishing  (отв.: OP_NET_HANDLE_TOOLTIP_NOTICE_PACKET=6168)
    OP_NET_HANDLE_TOOLTIP_NOTICE_PACKET = 6168, // S->C  netHandleTooltipNoticePacket
    OP_RECV_PHASE_RESULT = 6169, // C->S  recvPhaseResult  (отв.: OP_RECV_SET_INPUT_GAUGE_VALUE=6170)
    OP_RECV_SET_INPUT_GAUGE_VALUE = 6170, // S->C  recvSetInputGaugeValue
    OP_RECV_END_FISHING = 6171, // C->S  recvEndFishing  (отв.: OP_NET_ON_ITEM_UPGRADE_RESULT=6172)
    OP_NET_ON_ITEM_UPGRADE_RESULT = 6172, // S->C  netOnItemUpgradeResult
    OP_STAT_RECV_STAT_SLOT_UPDATE = 6173, // S->C  stat_RecvStatSlotUpdate
    OP_LIST_PARSE_ROSTER_ENTRY_PACKET = 6174, // S->C  listParseRosterEntryPacket
    OP_RECV_FISHING_BOOK_REGIST = 6176, // C->S  recvFishingBookRegist  (отв.: OP_INV_HANDLE_ITEM_USE_RESULT_PACKET=6177)
    OP_INV_HANDLE_ITEM_USE_RESULT_PACKET = 6177, // S->C  invHandleItemUseResultPacket
    OP_RECV_START_FISHING_6178 = 6178, // C->S  recvStartFishing  (отв.: OP_ITEM_COLLECT_ON_REMOVE_PACKET=6179)
    OP_ITEM_COLLECT_ON_REMOVE_PACKET = 6179, // S->C  itemCollectOnRemovePacket
    OP_RECV_FISHING_BOOK_UPDATE = 6180, // C->S  recvFishingBookUpdate  (отв.: OP_NET_HANDLE_INVENTORY_UPDATE_9020=6181)
    OP_NET_HANDLE_INVENTORY_UPDATE_9020 = 6181, // S->C  netHandleInventoryUpdate_9020
    OP_RECV_FISHING_BOOK_REWARD = 6182, // C->S  recvFishingBookReward  (отв.: OP_NET_HANDLE_INVENTORY_UPDATE=6183)
    OP_NET_HANDLE_INVENTORY_UPDATE = 6183, // S->C  netHandleInventoryUpdate
    OP_RECV_USER_TUTORIAL_INFO_UPDATE = 6184, // C->S  recvUserTutorialInfoUpdate  (отв.: OP_SKILL_TUTORIAL_RECV_BLOB32=6185)
    OP_SKILL_TUTORIAL_RECV_BLOB32 = 6185, // S->C  skillTutorial_RecvBlob32
    OP_RECV_MEMBER_KICK = 6186, // C->S  recvMemberKick  (отв.: OP_INVENTORY_PKT_ON_ITEM_SLOT_UPDATE=6187)
    OP_INVENTORY_PKT_ON_ITEM_SLOT_UPDATE = 6187, // S->C  inventoryPkt_onItemSlotUpdate
    OP_DUEL_HANDLE_CHALLENGE_REQUEST = 6188, // S->C  duelHandleChallengeRequest
    OP_RECV_KICK_POLL = 6189, // C->S  recvKickPoll  (отв.: OP_UI_BUILD_SHIP_TOOLTIP_TEXT=6190)
    OP_UI_BUILD_SHIP_TOOLTIP_TEXT = 6190, // S->C  uiBuildShipTooltipText

    // ==== Личная лавка / прочее (62xx-63xx) ====
    OP_UI_WND_HIDE_AND_KILL_FOCUS = 6200, // S->C  uiWndHideAndKillFocus
    OP_PORTAL_HANDLE_CHANNEL_WARP = 6201, // S->C  portalHandleChannelWarp
    OP_RESPAWN_RESULT = 6203, // S->C  Recv_RespawnResult
    OP_NET_PARSE_ITEM_ACQUIRE_NOTICE = 6226, // S->C  netParseItemAcquireNotice
    OP_CHAR_SELF_STORE_READY = 6300, // C->S  charSelfStoreReady  (отв.: OP_ITEM_ENHANCE_ON_RESULT=6301)
    OP_ITEM_ENHANCE_ON_RESULT = 6301, // S->C  itemEnhanceOnResult
    OP_CHAR_SELF_STORE_OPEN = 6302, // C->S  charSelfStoreOpen  (отв.: OP_PKT_HANDLE_SHOP_WINDOW_OPEN=6303)
    OP_PKT_HANDLE_SHOP_WINDOW_OPEN = 6303, // S->C  pktHandleShopWindowOpen
    OP_CHAR_MGR_ADD_NAME_STATUS_ENTRY = 6304, // S->C  charMgrAddNameStatusEntry
    OP_CHAR_SELF_STORE_CLOSE = 6305, // C->S  charSelfStoreClose  (отв.: OP_SELL_WINDOW_ON_SELL_RESULT=6306)
    OP_SELL_WINDOW_ON_SELL_RESULT = 6306, // S->C  sellWindowOnSellResult
    OP_CHAR_SELF_STORE_INFO = 6307, // C->S  charSelfStoreInfo  (отв.: OP_SHOP_HANDLE_OPEN_SHOP_PACKET=6308)
    OP_SHOP_HANDLE_OPEN_SHOP_PACKET = 6308, // S->C  shopHandleOpenShopPacket
    OP_CHAR_SELF_STORE_BUY_ITEM = 6309, // C->S  charSelfStoreBuyItem  (отв.: OP_SHOP_SELL_DLG_ON_SELL_RESULT=6310)
    OP_SHOP_SELL_DLG_ON_SELL_RESULT = 6310, // S->C  shopSellDlg_OnSellResult
    OP_SHOP_DLG_ON_BUY_RESULT_PACKET = 6311, // S->C  shopDlg_onBuyResultPacket
    OP_SHOP_DLG_ON_CONFIRM_BUY_PACKET = 6312, // S->C  shopDlg_onConfirmBuyPacket
    OP_RECV_SELF_STORE_CHAT_CLIENT = 6313, // C->S  recvSelfStoreChatClient  (отв.: OP_SHOP_DLG_RECV_LOG_LINE_COLORED=6314)
    OP_SHOP_DLG_RECV_LOG_LINE_COLORED = 6314, // S->C  shopDlg_RecvLogLineColored
    // ★ 29.09.2026 (волна О0 торговли): 6315 — вход/выход посетителя личной лавки, 5 б (+0 u8 число гостей, +1 i32 charId);
    //   приёмник эталона `shopDlgAppendTradeResultLog@0xDDB3F0` — МИСНЕЙМ (это не «лог сделки»: нотисы 107087/107088),
    //   B_selfstore_client.md §3.1.8. Раньше номера в перечне не было вовсе.
    OP_SHOP_DLG_APPEND_TRADE_RESULT_LOG = 6315, // S->C  shopDlgAppendTradeResultLog (МИСНЕЙМ: гость вошёл/вышел)

    // ==== Улучшение / разбор / крафт предметов (65xx-66xx) ====
    OP_RECV_USER_UPPER_REFINE = 6500, // C->S  recvUserUpperRefine  (отв.: OP_ITEM_UPDATE=6501)
    OP_ITEM_UPDATE = 6501, // S->C  Recv_ItemUpdate
    OP_RECV_USER_DISASSEMBLE = 6502, // C->S  recvUserDisassemble  (отв.: OP_ITEM_DISJOINT=6503)
    OP_ITEM_DISJOINT = 6503, // S->C  Recv_ItemDisjoint
    OP_CHAR_SHELL_ITEM_UPGRADE_6504 = 6504, // C->S  charShellItemUpgrade  (отв.: OP_ON_RECV_ITEM_UPGRADE_BATCH=6505)
    OP_ON_RECV_ITEM_UPGRADE_BATCH = 6505, // S->C  OnRecv_ItemUpgradeBatch
    OP_RECV_USER_SHELL_ITEM_UPGRADE_RESET = 6506, // C->S  recvUserShellItemUpgradeReset  (отв.: OP_UI_INVENTORY_ON_RECV_SLOT_UPDATE=6507)
    OP_UI_INVENTORY_ON_RECV_SLOT_UPDATE = 6507, // S->C  uiInventoryOnRecvSlotUpdate
    OP_M_SGCLANAUTHORITYREQ = 6508, // C->S  mSGCLANAUTHORITYREQ  (отв.: OP_NET_HANDLE_ITEM_TOOLTIP_NOTICE=6509)
    OP_NET_HANDLE_ITEM_TOOLTIP_NOTICE = 6509, // S->C  netHandleItemTooltipNotice
    // ★★★ ЭТО ОБЪЯВЛЕНИЯ КЛАНА, А НЕ СПИСОК ЧЛЕНОВ (доказано: `Reports/clan_soulmate_gaps.md` §2.5-2.6).
    //   6511 C→S `Recv_User_Notice_Read@0x406EF0`, payload 8: @0 i32 charId · @4 u16 clanIndex · @6 u16 курсор.
    //   6512 S→C `guildMemberListRecvUpdate@0xBCC3A0`, payload 546: [0] флаг (1 = есть данные) ·
    //     [1] маркер порции (2 = последняя) · [2..545] запись объявления 544 б (внутренняя раскладка НЕ доказана).
    //   ⚠ Имя `OP_GUILD_MEMBER_LIST_RECV_UPDATE` ВРЁТ — список членов клана несёт 4125, конец списка 4126.
    OP_RECV_USER_NOTICE_READ = 6511, // C->S  recvUserNoticeRead — ЧТЕНИЕ ОБЪЯВЛЕНИЙ КЛАНА (отв.: 6512)
    OP_GUILD_MEMBER_LIST_RECV_UPDATE = 6512, // S->C  ОБЪЯВЛЕНИЯ КЛАНА (★ имя-миснейм, к списку членов не относится)
    OP_RECV_USER_NOTICE_WRITE = 6513, // C->S  recvUserNoticeWrite — НАПИСАТЬ ОБЪЯВЛЕНИЕ КЛАНА (отв.: 6514)
    // ★ 2026-09-02: 6514/6519 — ответы на 6513/6518. Раньше в перечне их не было вовсе.
    //   Диспетчер эталона их принимает (`network.c:844` `case 0x1972`, `:853` `case 0x1977`), но ОБА
    //   обработчика — ПУСТЫЕ заглушки: `sub_BCC190` и `sub_BCCAF0` это дословно `return a1;`
    //   (проверено по дизасму живого exe через thunk'и 0x92FE27 / 0x92363B). То есть оригинал ответ
    //   получает и НИЧЕГО с ним не делает — окно объявления не подтверждает и об отказе не сообщает.
    //   Сервер шлёт 6514 телом 1 б (код результата) — `GameServer.c:3057…3117`; 6519 телом 70 б при
    //   отказе на длину/клан (`:3940…3954`) и 1 б после ответа БД (`:3989`).
    OP_RECV_USER_NOTICE_WRITE_RES = 6514, // S->C  ответ на 6513 (эталонный обработчик пуст)
    OP_RECV_USER_NOTICE_DEL = 6515, // C->S  recvUserNoticeDel
    OP_MEMBER_LIST_SELECT_BY_ID = 6517, // S->C  memberListSelectById
    OP_RECV_USER_MEMO_WRITE = 6518, // C->S  recvUserMemoWrite — НАПИСАТЬ ЗАМЕТКУ О СЕБЕ (отв.: 6519)
    OP_RECV_USER_MEMO_WRITE_RES = 6519, // S->C  ответ на 6518 (эталонный обработчик пуст)
    OP_PARTY_UPDATE_MEMBER_FROM_ITEM = 6520, // S->C  partyUpdateMemberFromItem
    OP_RECV_USER_CLAN_GRADE = 6521, // C->S  recvUserClanGrade  (отв.: OP_UI_SLOT_RESULT_SHOW_NOTICE17172=6522)
    OP_UI_SLOT_RESULT_SHOW_NOTICE17172 = 6522, // S->C  uiSlotResultShowNotice17172
    OP_UI_HANDLE_SLOT_RESULT_PACKET = 6523, // S->C  uiHandleSlotResultPacket
    OP_RECV_USER_FLAG_UPDATE = 6524, // C->S  recvUserFlagUpdate  (отв.: OP_NET_ON_REFINE_SLOT_UPDATE=6525)
    OP_NET_ON_REFINE_SLOT_UPDATE = 6525, // S->C  netOnRefineSlotUpdate
    OP_RECV_USER_MASTER_TRANS = 6526, // C->S  recvUserMasterTrans  (отв.: OP_UI_TRADE_WINDOW_SET_DATA=6527)
    OP_UI_TRADE_WINDOW_SET_DATA = 6527, // S->C  uiTradeWindowSetData
    OP_UI_PANEL_SELECT_MODE_NOTIFY = 6528, // S->C  uiPanelSelectModeNotify
    OP_QUICK_SLOT_RECV_PARTY_MEMBER_DISPATCH = 6529, // S->C  quickSlot_RecvPartyMemberDispatch
    OP_RECV_USER_MASTER_TRANS_REPLY = 6530, // C->S  recvUserMasterTransReply  (отв.: OP_QUICK_SLOT_RECV_PARTY_MEMBER_DISPATCH=6529)
    OP_RECV_USER_TARGET_HPMP = 6538, // C->S  recvUserTargetHPMP  (отв.: OP_NET_RECV_CHAR_STATE_UPDATE=6539)
    OP_NET_RECV_CHAR_STATE_UPDATE = 6539, // S->C  netRecvCharStateUpdate
    OP_PARTY_MEMBER_UPDATE_HP_STATE = 6542, // S->C  partyMemberUpdateHpState
    OP_EVENT_DUNGEON_LIST_REQ = 6545, // C->S  eventDungeonListReq
    OP_EVENT_DUNGEON_OPEN_REQ = 6547, // C->S  eventDungeonOpenReq
    OP_RECV_USER_UPGRADE_EXTRACTION = 6549, // C->S  recvUserUpgradeExtraction  (отв.: OP_NET_HANDLE_ITEM_OP_RESULT_PACKET=6550)
    OP_NET_HANDLE_ITEM_OP_RESULT_PACKET = 6550, // S->C  netHandleItemOpResultPacket
    OP_RECV_USER_UPGRADE_BLESS = 6551, // C->S  recvUserUpgradeBless  (отв.: OP_INVENTORY_PARSE_ITEM_UPDATE_PACKET=6552)
    OP_INVENTORY_PARSE_ITEM_UPDATE_PACKET = 6552, // S->C  inventory_parseItemUpdatePacket
    OP_RECV_USER_ITEM_OPTION_TRANS = 6553, // C->S  recvUserItemOptionTrans  (отв.: OP_NET_ON_ITEM_UPGRADE_RESULT_6554=6554)
    OP_NET_ON_ITEM_UPGRADE_RESULT_6554 = 6554, // S->C  Net_OnItemUpgradeResult
    OP_RECV_USER_PRIMIUM_SUM = 6555, // C->S  recvUserPrimiumSum  (отв.: OP_UI_HANDLE_CRAFT_UNION_RESULT=6556)
    OP_UI_HANDLE_CRAFT_UNION_RESULT = 6556, // S->C  uiHandleCraftUnionResult
    OP_NET_APPLY_SLOT_STATE_FLAG = 6570, // S->C  netApplySlotStateFlag
    OP_RECV_USER_DURA_REGEN = 6571, // C->S  recvUserDuraRegen  (отв.: OP_NET_HANDLE_STATE_ARRAY_PACKET=6572)
    OP_NET_HANDLE_STATE_ARRAY_PACKET = 6572, // S->C  netHandleStateArrayPacket
    OP_RECV_USER_SOUL_ROULETTET = 6575, // C->S  recvUserSoulRoulettet  (отв.: OP_ROULETTE_PANEL_HANDLE_NET_EVENT=6576)
    OP_ROULETTE_PANEL_HANDLE_NET_EVENT = 6576, // S->C  roulettePanelHandleNetEvent
    OP_RECV_USER_PRIMIUM_GA_CHA = 6577, // C->S  recvUserPrimiumGaCha  (отв.: OP_GACHA_SCENE_ON_SERVER_RESULT=6578)
    OP_GACHA_SCENE_ON_SERVER_RESULT = 6578, // S->C  GachaScene_OnServerResult
    OP_RECV_USER_SOUL_RESULT = 6579, // C->S  recvUserSoulResult  (отв.: OP_ROULETTE_RESULT=6580)
    OP_ROULETTE_RESULT = 6580, // S->C  Recv_RouletteResult
    OP_RECV_USER_PRIMIUM_GA_CHA_REWARD = 6581, // C->S  recvUserPrimiumGaChaReward  (отв.: OP_UI_GACHA_RESULT_APPLY=6582)
    OP_UI_GACHA_RESULT_APPLY = 6582, // S->C  uiGachaResultApply
    OP_INV_ITEM_MOVE_PACKET_HANDLE = 6583, // S->C  invItemMovePacket_Handle
    OP_RECV_WING_DATA_TABLE_INSERT = 6585, // S->C  recvWingDataTableInsert
    OP_RECV_USER_STAR_POINT_UPDATE = 6586, // C->S  recvUserStarPointUpdate  (отв.: OP_ITEM_GRADE_NOTICE_HANDLE_RESULT=6587)
    OP_ITEM_GRADE_NOTICE_HANDLE_RESULT = 6587, // S->C  itemGradeNotice_HandleResult
    OP_PACKET_PARSE_RECORD_LIST8 = 6588, // S->C  packetParseRecordList8
    OP_RECV_QUEST_CRAFT_ACK = 6589, // S->C  recvQuestCraftAck
    OP_BUDDY_ON_STATUS_UPDATE_PACKET = 6590, // S->C  buddyOnStatusUpdatePacket
    OP_RECV_USER_LIMITATION_QUEST_ENTER = 6591, // C->S  recvUserLimitationQuestEnter  (отв.: OP_BUDDY_ON_STATUS_UPDATE_PACKET=6590)
    OP_RECV_USER_LIMITATION_QUEST_RESULT = 6593, // C->S  recvUserLimitationQuestResult  (отв.: OP_ITEM_EXCHANGE_RESULT=6594)
    OP_ITEM_EXCHANGE_RESULT = 6594, // S->C  recv_ItemExchangeResult
    OP_ITEM_LIST_CLEAR_READY_FLAGS = 6595, // S->C  itemListClearReadyFlags
    OP_SHOP_PARSE_STOCK_PACKET = 6597, // S->C  shopParseStockPacket
    OP_RECV_USER_CRAFT_RECIPE_REGIST = 6598, // C->S  recvUserCraftRecipeRegist  (отв.: OP_NET_HANDLE_INVENTORY_UPDATE_PACKET=6599)
    OP_NET_HANDLE_INVENTORY_UPDATE_PACKET = 6599, // S->C  netHandleInventoryUpdatePacket
    OP_RECV_USER_CRAFT_RECIPE_DELETE = 6600, // C->S  recvUserCraftRecipeDelete  (отв.: OP_NET_ON_SUBCODE_NOTICE_HANDLER=6601)
    OP_NET_ON_SUBCODE_NOTICE_HANDLER = 6601, // S->C  netOnSubcodeNoticeHandler
    OP_RECV_USER_CRAFT_ENTER = 6602, // C->S  recvUserCraftEnter  (отв.: OP_UPGRADE_DLG_ON_RESULT_PACKET=6603)
    OP_UPGRADE_DLG_ON_RESULT_PACKET = 6603, // S->C  upgradeDlg_onResultPacket
    OP_RECV_USER_CRAFT_SCROLL_LEVEL_UP = 6604, // C->S  recvUserCraftScrollLevelUP  (отв.: OP_NET_ON_ITEM_USE_RESULT=6605)
    OP_NET_ON_ITEM_USE_RESULT = 6605, // S->C  netOnItemUseResult
    OP_NET_SEND_INVENTORY_ITEM_USE = 6606, // C->S
    OP_RECV_USER_ITEM_TRANS = 6608, // C->S  recvUserItemTrans
    OP_C_ITEM_MANAGER_NOTIFY_ITEM_FOUND = 6615, // S->C  CItemManager_NotifyItemFound
    // ★ C->S «я получил редкий предмет, объяви всем» — netSendSignedPacket6616@0xA0F140.
    //   Тело 48 б: [0..7] анти-реплей-секрет, [8..23] MD5(msg,24), [24..47] msg = itemCode dword + имя char[20].
    //   Шлётся клиентом из CItemManager_NotifyItemInfo@0xD0C8F0 (подбор / коробка / заточка / магазин)
    //   при грейде>=3. Сервер ретранслирует как 6131 (см. AstoryServer/Docs/GameServer/
    //   rare-item-notice-6616-handler.md — в стоковом GameServer обработчика НЕТ).
    OP_SEND_SIGNED_RARE_ITEM_NOTICE = 6616, // C->S  netSendSignedPacket6616

    // ==== Клан-игра / Camp War / Unicon War (67xx) ====
    OP_USER_GAME_GET_MAIN_INFO = 6700, // C->S  userGameGetMainInfo  (отв.: OP_CLAN_INFO=6701)
    OP_CLAN_INFO = 6701, // S->C  Recv_ClanInfo
    OP_USER_GAME_SELECT_SQUAD = 6702, // C->S  userGameSelectSquad  (отв.: OP_CLAN_CREATE_RESULT=6703)
    OP_CLAN_CREATE_RESULT = 6703, // S->C  Recv_ClanCreateResult
    OP_USER_GAME_SQUAD_RESET = 6704, // C->S  userGameSquadReset  (отв.: OP_CLAN_STORAGE_ITEM=6705)
    OP_CLAN_STORAGE_ITEM = 6705, // S->C  Recv_ClanStorageItem
    OP_USER_GAME_REGIST_USER = 6706, // C->S  userGameRegistUser  (отв.: OP_CLAN_RANK_RESULT=6707)
    OP_CLAN_RANK_RESULT = 6707, // S->C  Recv_ClanRankResult
    OP_USER_GAME_REGIST_CANCEL = 6708, // C->S  userGameRegistCancel  (отв.: OP_CLAN_KICK_RESULT=6709)
    OP_CLAN_KICK_RESULT = 6709, // S->C  Recv_ClanKickResult
    OP_CLAN_MEMBER_POINT = 6710, // S->C  Recv_ClanMemberPoint
    OP_CLAN_APPLY_RESULT = 6711, // S->C  Recv_ClanApplyResult
    OP_USER_GAME_REGIST_CANCEL_6712 = 6712, // C->S  userGameRegistCancel  (отв.: OP_CLAN_MY_MEMBERSHIP=6713)
    OP_CLAN_MY_MEMBERSHIP = 6713, // S->C  Recv_ClanMyMembership
    // ★ ИМЯ ОБМАНЧИВО (проверено 24.08.2026): это не подземелье, а RoseWar («битва армий»).
    OP_CHAR_INDUN_RECV_ENTRANCE_6714 = 6714, // C->S  RoseWar, вход (отв.: OP_CLAN_MEMBER_LIST_BULK=6715)
    OP_CLAN_MEMBER_LIST_BULK = 6715, // S->C  Recv_ClanMemberListBulk
    OP_CLAN_DRAW_MEMBER_LIST = 6716, // S->C  Recv_ClanDrawMemberList
    OP_USER_GAME_REGIST_USER_6717 = 6717, // C->S  userGameRegistUser  (отв.: OP_CLAN_DRAW_MEMBER_LIST=6716)
    OP_USER_GAME_REGIST_CANCEL_6718 = 6718, // C->S  userGameRegistCancel  (отв.: OP_CLAN_CHAT=6719)
    OP_CLAN_CHAT = 6719, // S->C  Recv_ClanChat
    OP_CLAN_MEMBER_ONLINE = 6720, // S->C  Recv_ClanMemberOnline
    OP_CLAN_MEMBER_UPDATE = 6721, // S->C  Recv_ClanMemberUpdate
    OP_RECV_CAMP_WAR_CLAN_MEMBER_UPDATE = 6722, // S->C  recvCampWarClanMemberUpdate
    OP_USER_GAME_REGIST_CANCEL_6723 = 6723, // C->S  userGameRegistCancel  (отв.: OP_C_CAMP_WAR_ON_OBJECT_EFFECT_PACKET=6724)
    OP_C_CAMP_WAR_ON_OBJECT_EFFECT_PACKET = 6724, // S->C  CCampWar_OnObjectEffectPacket
    OP_QUICK_SLOT_ON_UPDATE_PACKET = 6725, // S->C  QuickSlot_OnUpdatePacket
    OP_CAMP_WAR_SET_OBJECTIVE_SLOT = 6726, // S->C  campWarSetObjectiveSlot
    OP_CAMP_WAR_HANDLE_STATE_NOTICE = 6728, // S->C  campWarHandleStateNotice
    OP_UI_PANEL_SHOW_WITH3 = 6729, // S->C  uiPanelShowWith3
    OP_FLOATING_TEXT_SPAWN_VALUE_OVER_CHAR = 6730, // S->C  floatingText_SpawnValueOverChar
    OP_USER_GAME_REGIST_CANCEL_6731 = 6731, // C->S  userGameRegistCancel  (отв.: OP_RECV_CAMP_WAR_SYNTH_RESULT_REFRESH=6732)
    OP_RECV_CAMP_WAR_SYNTH_RESULT_REFRESH = 6732, // S->C  recvCampWarSynthResultRefresh
    OP_ON_RECV_HONOR_AND_INVENTORY_UPDATE = 6733, // S->C  onRecvHonorAndInventoryUpdate
    OP_USER_GAME_REGIST_CANCEL_6734 = 6734, // C->S  userGameRegistCancel  (отв.: OP_ON_RECV_HONOR_AND_INVENTORY_UPDATE=6733)
    OP_CAMP_WAR_UPDATE_COUNTDOWN = 6736, // S->C  campWarUpdateCountdown
    OP_C_CAMP_WAR_RESET_STATE = 6737, // S->C  CCampWar_ResetState
    OP_CAMP_WAR_SET_FACTION_FLAG_TOOLTIP = 6738, // S->C  campWarSetFactionFlagTooltip
    OP_CAMP_WAR_RANKING_APPLY = 6739, // S->C  campWarRankingApply
    OP_UI_CAMP_WAR_RANKING_RESULT_HANDLER = 6740, // S->C  uiCampWarRankingResultHandler
    OP_UI_TOGGLE_BY_FLAG_BYTE = 6741, // S->C  uiToggleByFlagByte
    OP_CAMP_WAR_UPDATE_SCORE_LABELS = 6742, // S->C  campWar_UpdateScoreLabels
    OP_UNICON_WAR_UPDATE_FACTION_STATE_UI = 6743, // S->C  uniconWar_UpdateFactionStateUI
    OP_USER_GAME_TAX_CHANGE = 6744, // C->S  userGameTaxChange  (отв.: OP_C_CHAT_SHOW_ITEM_SUBNOTICE=6745)
    OP_C_CHAT_SHOW_ITEM_SUBNOTICE = 6745, // S->C  CChat_ShowItemSubnotice
    OP_ANTI_CHEAT_SEND_HEARTBEAT6746 = 6746, // C->S
    OP_NET_PARSE_CAMP_WAR_BANNER_PACKET = 6747, // S->C  netParseCampWarBannerPacket
    OP_NET_SEND_REQ_SELF_ID_6749 = 6749, // C->S  netSendReqSelfId_op6749@0xFEA180 (CampWar, код меню 30078)
    OP_NET_PARSE_AUTO_HIDE_PANEL_PACKET = 6750, // S->C  netParseAutoHidePanelPacket
    OP_CAMP_WAR_PARSE_PANEL_INFO = 6751, // S->C  campWarParsePanelInfo
    OP_ANTI_CHEAT_SEND_HEARTBEAT6752 = 6752, // C->S
    OP_NET_ON_UNICON_RESULT = 6753, // S->C  netOnUniconResult
    OP_UPGRADE_RESULT_FX = 6754, // S->C  Recv_UpgradeResultFx
    OP_ANTI_CHEAT_SEND_TIME_MASK_HEARTBEAT = 6755, // C->S
    OP_RECV_UPGRADE_RESULT_NOTICE = 6756, // S->C  recvUpgradeResultNotice
    OP_5_BYTE_FIELD = 6757, // S->C  Recv_5ByteField
    OP_NET_ON_MOB_TARGET_SELECT = 6758, // S->C  netOnMobTargetSelect
    OP_UPGRADE_SUC_FAIL_FX = 6760, // S->C  Recv_UpgradeSucFailFx

    // ==== Сведения о чужом персонаже — ВТОРАЯ ПОЛОВИНА ответа (70xx) ====
    // ★ 2026-09-17 (порт окна `OTHERCHARA_DATA` 4200). 7006 в исходном `RE/ida/netopcode_enum.h`
    //   ОТСУТСТВУЕТ: генератор снимает опкоды по таблице приёмников клиента, а у 7006 своей записи
    //   нет — в диспетчере он ПРОВАЛИВАЕТСЯ в тело 5481 из-за пропущенного `break`
    //   (дефект оригинала D-W10, доказан дизасмом; разбор — `AsdaCommunityWhoSubsystem.cpp:675`).
    //   Сервер шлёт 7006 ПАРОЙ к 5479 из одной функции `Send_TargetChar_Information@0x549D10`
    //   (`GameServer.c:126101-126102`: `sub_4C28F0(…, 5479, 121, …)` и сразу
    //   `sub_4C28F0(…, 7006, 631, …)`). Тело 631 б: [0..3] тот же 4-байтный ключ пары, что лежит
    //   в 5479@+116, дальше 627 б = 11 записей надетых предметов по 57 б.
    //   Приёмник эталона — `netHandlePlayerInfoPacket@0xDA8BD0` (`AsdaStory_game.c:401755`).
    //   ⚠ Источник `RE/ida/netopcode_enum.h` дополнен тем же именем (см. там же).
    OP_NET_HANDLE_PLAYER_INFO_PACKET = 7006, // S->C  netHandlePlayerInfoPacket (2-я половина 5479)

    // ==== NPC-магазин / транзакции (99xx) ====
    OP_NPC_SHOP_DISPATCH_RESULT = 9902, // S->C  npcShop_DispatchResult
    OP_NPC_SHOP_DISPATCH_RESULT_9904 = 9904, // S->C  npcShop_DispatchResult
    OP_NPC_SHOP_DISPATCH_RESULT_9913 = 9913, // S->C  npcShop_DispatchResult
    OP_NPC_SHOP_DISPATCH_RESULT_9914 = 9914, // S->C  npcShop_DispatchResult
    OP_C_ITEM_MEDIATION_SEND_OPEN = 9915, // C->S  ~npc Shop Dispatch Result (по парному клиент-опкоду)  (отв.: OP_NPC_SHOP_DISPATCH_RESULT_9914=9914)

    // ==== Премиум-магазин (110xx) ====
    OP_CASH_SHOP_OPEN_WEB_URL = 11002, // S->C  CashShop_OpenWebUrl
    // ★★★ ЭЛИТНЫЙ ПОЧТОВЫЙ ЯЩИК (окно 69000 `Primium_Item_post_WIN`) — четыре опкода одной стойки.
    //   Полный реверс: `Reports/elite_mailbox_re.md` §2. Владелец в порту —
    //   `UAsdaPremiumPostSubsystem`. «Ящик» в БД = таблица покупок кэш-шопа (`UPCashitemList`),
    //   куда кладутся и покупка премиум-шопа (BuyType 1), и приз гачи (BuyType 3).
    OP_RECV_PREMIUM_ITEM_LIST_REQ = 11004, // C->S  netSecure_sendGuardedRequest@0xEF0120 (28 б = 8+16+4 нуля)
    OP_RECV_PREMIUM_ITEM_LIST_RES = 11005, // S->C  netParseLocalSlotSync@0xEF0990 (CharIndex + N×10)
    OP_RECV_PREMIUM_ITEM_TO_INVEN_REQ = 11006, // C->S  recvPremiumItemToInvenReq (34 б = 8+16+10)
    OP_RECV_PREMIUM_ITEM_TO_INVEN_RES = 11007, // S->C  inventorySyncApplyPacket@0xEF1320 (409 б)

    // ==== Battle-server / события (80xx-89xx) ====
    // 8008 — «моб/NPC говорит»: сервер CMonsterManager::sub_4280D0 @0x4280D0 рассылает
    //   SendPacket_ClippingSectorInAllUser(..., 8008, ...) всем в клип-секторе; клиент
    //   recvCharChatBubble @0xBA7FE0 (network.c:10692, диспетчер network.c:1252) вешает облачко
    //   TALKBOX_Mon над сущностью, найденной по renderId. Гейт диспетчера: g_GameState 1/8,
    //   либо (11/500 и g_LoadProgress >= 3.0).
    OP_MOB_CHAT_BUBBLE = 8008, // S->C  recvCharChatBubble
    // ★ 30.10.2026 НАВЫКИ МОБОВ (разбор `Reports/mob_skills/FLOW.md`). Диспетчер эталона network.c:1224/1259/1263,
    //   владелец в порту — `UAsdaMobSkillSubsystem`.
    OP_MOB_SKILL_CAST = 8011,   // S->C  netApplyCharActionState@0xF33F00 — начало каста (7 б: навык, вид цели, цель, renderId моба)
    OP_MOB_SKILL_RESULT = 8012, // S->C  NetHandler_CharActionUpdate@0xF33FC0 — результат (9 б шапки + N×29 б целей)
    OP_MOB_HP_RESTORE = 8013,   // S->C  Entity_ApplyHpRestore@0xF34500 — лечение моба (16 б: renderId @6, HP @12)
    OP_CHAR_UPDATE_DASH = 8215, // C->S
    OP_DISCONNECT_POPUP_8402 = 8402, // S->C  Recv_DisconnectPopup
    OP_OBJ_APPLY_MOVE_ACTION_PACKET = 8403, // S->C  obj_applyMoveActionPacket
    OP_NPC_SHOP_ON_RECV_RESULT7050 = 8913, // S->C  npcShop_OnRecvResult7050
    OP_UI_SHOW_GRADE_STARS = 8921, // S->C  uiShowGradeStars

    // ==== Сессия / handshake (20000) ====
    OP_ACCEPT_EX_USER = 20000, // C->S  acceptExUser
};
