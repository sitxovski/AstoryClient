// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaClanSubsystem — ДВА ПОД-ОКНА КЛАНОВОГО ОКНА, которые открывает диалог NPC:
//     `CLAN_MAKING_WIN` (JUI IDNum **14400**) — СОЗДАНИЕ КЛАНА     ← код кнопки NPC **30015**
//     `CLAN_DELETE_BOX` (JUI IDNum **14470**) — РОСПУСК КЛАНА      ← код кнопки NPC **30016**
//   Третий код задания, **30027**, открывает `CLAN_INCREASE_BOX` (14450) — УВЕЛИЧЕНИЕ ЧИСЛА МЕСТ
//   в клане. ★ 27.09.2026 окно РЕАЛИЗОВАНО по правилам СЕРВЕРА (+10 мест, 5 расширений, 5 цен) —
//   решение владельца «авторитет — сервер»; разбор — раздел «РАСШИРЕНИЕ КЛАНА» ниже.
//   ⚠ Кнопки с кодом 30027 нет ни в одном `.NSR` пяти сборок данных (разбор ниже) — окно
//   поднимается кодом 30027, если он появится в данных, и командой `asda.Clan.OpenIncrease`.
//   Полный реверс с адресами, побайтовыми раскладками и таблицей нотисов —
//   `Reports/npc_repair_variants_30015.md`.
//
// ═════════ ★★★ ПОДПИСИ «РЕМОНТ СНАРЯЖЕНИЯ» БЫЛИ ЛОЖНЫМИ ═══════════════════════════════════════
//   В порте 30015/30016/30027 были подписаны «ремонт снаряжения / вариант B / ремонт из
//   инвентаря». Ни одна из трёх подписей не верна: все три кода открывают под-боксы КЛАНОВОГО
//   окна. Доказательства — три независимых:
//     (1) КОД. `game_main.c:10004/10007/10071` зовут `uiRepairWindowToggle@0xBC2CE0`,
//         `uiRepairWindowToggle_54D0@0xBC54D0`, `uiInventoryRepairDialogToggle@0xBC5600` —
//         все три на одном объекте `g_pCommunityWnd`. Поле-редактор первой из них читает
//         `netSendPasswordPacket@0xBC8A10` и шлёт **`OP_CLAN_CREATE_REQ` (4104)**; вторая
//         закрывается кнопкой 14274 `CLAN_DEL_CABTN`, третья — кнопкой 14462 `CLAN_INC_CABTN`.
//     (2) ДАННЫЕ UI. Билдер класса `CmtyClan_Delete_BuildUI@0xBC0650` (`ui_core.c:14884`)
//         печатает idc в диагностических `MessageBoxA`: `CLAN_MAKING_WIN`, `CLAN_NAME_INPUT`,
//         `CLAN_FEE_VALUE`, `CLAN_MONEY_VALUE`, `CLAN_MAKE_BTN`, `CLAN_DELETE_BOX`,
//         `CLAN_DEL_OKBTN`, `CLAN_INCREASE_BOX`, `CLAN_INC_OKBTN` и т.д.
//     (3) ДАННЫЕ ДИАЛОГОВ. `ella.nsr` (NPC 107 «Командир рыцарей Элла»): кнопка 0 —
//         «Распустить клан» → 30016, кнопка 1 — «Основать клан» → 30015.
//   ★ ПРИВЯЗКА ПО СМЕЩЕНИЯМ (дизасм 0xBC2CE0…0xBC2E46, поля класса — DWORD-ы подряд с +0x574):
//       +0x574 = 1396 → `makeWinCtrl`     14400   (панель, которую показывает 30015)
//       +0x580 = 1408 → `makeNameInputCtrl` 14402 (поле имени, его читает отправка 4104)
//       +0x594 = 1428 → `makeFeeValueCtrl` 14410  (СТОИМОСТЬ создания)
//       +0x59C = 1436 → `makeMoneyValueCtrl` 14413 (ЗОЛОТО игрока)
//       +0x5F0 = 1520 → `delBoxCtrl`       14470   (панель 30016; в эталоне `*(this + 380)`)
//       +0x5BC = 1468 → `incBoxCtrl`       14450   (панель 30027; в эталоне `gap_5A0[28]`)
//     Индексы сходятся с ПОРЯДКОМ присвоений в билдере (make… → join… → inc… → del…) без зазоров.
//
// ═════════ ГЕЙТЫ ОТКРЫТИЯ (1:1, дизасм 0xBC2CE0 / декомпил 0xBC54D0 / 0xBC5600) ════════════════
//   Поля персонажа (смещения сняты дизассемблером, а не по именам gap_*):
//     `CCharacter+0x968` (char+2408, i16) — **clanId**, −1 = клана нет. Тот же байт-в-байт
//        оффсет уже используется приёмом 6542 в `AsdaNetSubsystem` (`*(__int16*)(j + 2408)`).
//     `CCharacter+0x8C1` (char+2241, u8)  — **признак «Друг» (соулмейт)**; ненулевой → отказ
//        нотисом 12435 «Друзьям этот навык недоступен». Поле пишет приём **6152** (тело +12).
//     `CCharacter+0x97B` (char+2427, u8)  — **звание в клане**, 1 = глава (`ui_core.c:125851`
//        буквально `return *(u8*)(g_LocalPlayer + 2427) == 1;`).
//   30015 (создание): clanId должен быть −1, иначе нотис **12418**; «Друг» → **12435**.
//   30016 (роспуск):  «Друг» → **12435**; иначе нужно clanId != −1 И звание == 1, иначе **12431**.
//   30027 (места):    clanId != −1 иначе **12431**; «Друг» → **12435**; счётчик расширений < 4
//                     иначе **12455** — ТАК В КЛИЕНТЕ-ЭТАЛОНЕ; порт ставит порог СЕРВЕРА (< 5),
//                     см. «РАСШИРЕНИЕ КЛАНА» ниже.
//
// ═════════ ДЕРЕВО КОНТРОЛОВ (`Data/Decoded/jui/gamemain_jui.ini:22445…24120`) ════════════════════════
//   **CLAN_MAKING_WIN 14400** `Popup_Win` 300×180, Visible=0, ручка перетаскивания EVS{EVT=6,
//     RET=0x20003, CtrlID=14400} на самом окне (в порту её ставит `UAsdaWindowSubsystem` из
//     мета-ассета JUI — руками ничего не добавляем):
//       14404 `CLAN_TITLE`         NoImage 200×20 @(50,10)   ← нотис 12216 «Создание клана»
//       14403 `CLAN_NAME`          NoImage  70×20 @(38,56)   ← нотис 12217 «Название клана:»
//       14402 `CLAN_NAME_INPUT`    NoImage_EditBox 118×20    ← ввод (подложка Box_White1Stroke_Ot15)
//       14405 `CLAN_TYPE`          NoImage (Visible=0!)      ← нотис 12218 «Тип клана:»
//       14406 `CLAN_TYPE_BTN`      Cir_RadioCheck_2Btn       ← ★ ДЕФЕКТ D1: обработчика НЕТ
//       14407 `CLAN_TYPE1`/14408 `CLAN_TYPE2` ← нотисы 12219/12220 (мирный/боевой)
//       14409 `CLAN_FEE`   ← 12221 «Затраты на создание:» · 14410 `CLAN_FEE_VALUE` ← 10 000
//       14412 `CLAN_MONEY` ← 12222 «Золото»              · 14413 `CLAN_MONEY_VALUE` ← золото игрока
//       14215 `CLAN_MAKE_BTN`   WkBtn_W100 ← нотис 12223 «Создание клана»
//       14416 `CLAN_CANCEL_BTN` RdBtn_W100 ← нотис 2289 «Отмена»
//   **CLAN_DELETE_BOX 14470** `Popup_Win` 300×140, Visible=0, ручка EVS{6,0x20003,14470}:
//       14471 `CLAN_DEL_NAME`  NoImage 180×15 @(61,17)  ← ★ ДЕФЕКТ D2: билдер ставит ПУСТУЮ строку
//                                                          и больше её НИКТО не заполняет
//       14272 `CLAN_DEL_TEXT`  NoImage_Clip 200×50      ← нотис 12449 «Распустить клан?»
//       14473 `CLAN_DEL_OKBTN` WkBtn_W100 ← 2288 «ОК» · 14274 `CLAN_DEL_CABTN` ← 2289 «Отмена»
//       `CLAN_DEL_WMARK` Popup_TalkBox 38×38 — декор (восклицательный знак)
//
// ═════════ КНОПКИ (1:1 `uiPanel_HandleCommand@0xBC16D0`, гейт сообщения 0x10401 = клик) ════════
//   0x3787 (14215) → мат-фильтр (нотис 7009) → длина < 17 → `netSendPasswordPacket` (4104),
//                    иначе нотис 12436 «Название клана не может быть длиннее 16 символов»;
//   0x3850 (14416) → `uiRepairWindowToggle(this, 0)` — закрыть окно создания;
//   0x3889 (14473) → `netSendAcceptAuth@0xBC9270` (4106);
//   0x37C2 (14274) → `uiRepairWindowToggle_54D0(this, 0)` — закрыть окно роспуска;
//   0x387D (14461) → `netSendInventoryItemNamePacket@0xBC50D0` (4136, мёртвая ветка 30027);
//   0x387E (14462) → `uiInventoryRepairDialogToggle(this, 0)`.
//   ⚠ Кода 14406 (переключатель типа клана) в диспетчере НЕТ — см. дефект D1.
//
// ═════════ ПАКЕТЫ ═════════════════════════════════════════════════════════════════════════════
//   C→S **4104** `OP_CLAN_CREATE_REQ` (96 б = 8 секрет + 16 MD5 + 72 тело).
//        `netSendPasswordPacket@0xBC8A10` + `authPasswordCtxCtor@0xBC8C00` (обнуление тела):
//          @0  i32 = −1 в конструкторе, перед отправкой = `g_aCharSelectData`; сервер поле
//              НЕ читает (`ClanCreateReq@0x4017B0` берёт `clanBody` с байта 4);
//          @4  u8  тип клана — эталон ВСЕГДА шлёт 0 (см. дефект D1);
//          @5  17 б имя клана (cp1251, из 14402, пробелы вырезаны `strRemoveSpaces@0xBC2F30`);
//          @22 4 × u16 = 0 · @31…@70 5 × {i32,i32} = 0 · @30 = 0 · @71 = 0 — сервер их копирует
//              в запись клана как есть (звания/права, при создании пустые).
//   S→C **4105** `OP_AUTH_PASSWORD` (тело 196 б) `Recv_AuthPassword@0xBC8CD0`:
//          @0 u8 код · @1 u16 clanId · @3 имя клана (20) · @173 звание (9) · @192 i32 ЗОЛОТО.
//        Коды: 1 успех · 0/2/3/4/6/7/8/9/10 — отказы (таблица нотисов в .cpp).
//        При коде 1 эталон дополнительно шлёт **4138** (тело 4 б = userIdx) — обновление состава
//        клана (имя сервера `ClanMemberLeaveReq@0x403840` ВРЁТ: тело функции просто зовёт
//        `sub_405560` = разослать состав клана).
//   C→S **4106** `OP_CLAN_DELETE_REQ` (26 б = 8 + 16 + 2 тело) `netSendAcceptAuth@0xBC9270`:
//          @0 u16 clanId (эталон берёт КЭШ окна `window+1556`, у нас — свой clanId).
//   S→C **4107** `OP_UI_INVEN_NOTICE_BY_CODE` (тело 1 б) `uiInvenNoticeByCode@0xBC94A0`:
//          @0 u8 код: 1 успех · 0/2/3/4 — отказы.
//   ⚠ Оба ответа приходят и после закрытия окна — разбираем их ВСЕГДА (как окно 74500).
//
// ═════════ НУМПАД (окно 1410) ═════════════════════════════════════════════════════════════════
//   ★ НЕ УЧАСТВУЕТ. Доказано двумя способами:
//     (1) полный перечень ВСЕХ 26 вызовов `uiOpenContextPopupWithHandlers@0xDA55A0` во всех
//         25 файлах эталона — ни один не лежит в диапазоне класса 0xBC0470…0xBC9ED0
//         (ближайший «сосед» — `case 30051` того же switch'а диалога, `game_main.c:10219`);
//     (2) в фиче нет ни одного места, где выбирается КОЛИЧЕСТВО: у создания — только текст,
//         у роспуска — только «да/нет», у расширения — ФИКСИРОВАННЫЙ шаг (в клиенте-эталоне +5:
//         `sprintf(buf, "%d", cap); sprintf(buf, "%d", cap + 5)`, `ui_core.c:16209/16211`;
//         сервер прибавляет +10 — порт показывает серверный шаг, см. ниже).
//
// ═════════ ЗВУКИ ══════════════════════════════════════════════════════════════════════════════
//   ★ СВОИХ ЗВУКОВ У ФИЧИ НЕТ: в диапазоне `ui_core.c:14878…17130` (весь класс кланового окна)
//   нет ни одного вызова звуковой подсистемы, и ни у одного плана трёх окон в `.JUI` нет
//   `Animate="1"`. Единственный звук пути — «error» общего `ONEBUT_MSGBOX`.
//
// ═════════ ДЕФЕКТЫ ОРИГИНАЛА ══════════════════════════════════════════════════════════════════
//   D1. ★ ПЕРЕКЛЮЧАТЕЛЬ ТИПА КЛАНА МЁРТВЫЙ. Контрол 14406 `CLAN_TYPE_BTN` есть в `.JUI`, подписи
//       14407/14408 («мирный»/«боевой») билдер заполняет, но НИ билдер не резолвит 14406, НИ
//       диспетчер `uiPanel_HandleCommand` не знает кода 0x3846 — а отправка жёстко кладёт в
//       тело `LOBYTE(v8[1]) = 0`. То есть клан ВСЕГДА создаётся типа 0. Воспроизведено 1:1
//       (шлём 0); переключатель показываем неактивным. Откат/эксперимент — CVar
//       `asda.Clan.SendType` (по умолчанию 0 = как в оригинале).
//   D2. ★ В ОКНЕ РОСПУСКА НЕ ВИДНО ИМЕНИ КЛАНА. `CmtyClan_Delete_BuildUI` ставит 14471
//       `CLAN_DEL_NAME` ПУСТУЮ строку (`ui_core.c:15178`), и больше к полю не обращается никто
//       (единственные ссылки на `delNameCtrl` — эти две строки билдера). Игрок подтверждает
//       роспуск, не видя, ЧТО распускает. ИСПРАВЛЕНО: подставляем имя своего клана.
//       Откат к 1:1 — `asda.Clan.ShowNameInDisbandBox 0`.
//   D3. ПУСТОЕ ИМЯ МОЛЧА НИЧЕГО НЕ ДЕЛАЕТ. `netSendPasswordPacket` при `strlen(name) == 0`
//       не шлёт пакет и НЕ показывает нотиса — кнопка «Создание клана» выглядит сломанной.
//       Воспроизведено 1:1 (нотиса нет), в лог пишем причину.
//   D4. ЗАГОЛОВОК 14404 И ПОДПИСЬ 14405 В `.JUI` ИМЕЮТ `Visible = "0"`, хотя билдер кладёт в них
//       локализованный текст. Воспроизведено 1:1 (оба скрыты); откат — `asda.Clan.ShowHiddenLabels 1`.
//   D5. КОД РЕЗУЛЬТАТА 5 У 4105 НЕ ОБРАБОТАН (`default: return;`), хотя сервер его не шлёт —
//       мёртвая ветка, зафиксирована.
//   D6. `ClanDeleteReq@0x402090` при `clanIndex > 10000` только пишет в лог и НЕ отвечает вовсе —
//       клиент остаётся ждать ответа. Дефект СЕРВЕРА, порт его не воспроизводит (у нас есть
//       флаг ожидания, который снимается при закрытии окна).
//   D7. ★ 27.09.2026: ЗАКРЫТИЕ ОКНА 14450 ИДЁТ ЧЕРЕЗ ГЕЙТЫ ОТКРЫТИЯ. `uiInventoryRepairDialogToggle(this, 0)`
//       (ui_core.c:16173-16231) при закрытии пропускает только гейт «клана нет»; «Друг» (12435) и «расширений ≥ 4»
//       (12455) вместо закрытия показывают нотис. Итог: глава клана с 4 расширениями при «Распустить клан» у NPC
//       получает лишний 12455. ИСПРАВЛЕНО [ПОРТ]: `CloseIncreaseWindow` закрывает без проверок.
//   D8. ★ 08.11.2026 (пачка 16, баг 7): БЕЗ КЛАНА — «ТОЛЬКО ГЛАВА». Гейт 30016 (`ui_core.c:16154`) объединяет «клана нет» и
//       «не глава» в один нотис 12431 «Привилегия только главы клана.»; гейт 30027 (`ui_core.c:16188`) при «клана нет» —
//       тот же 12431; приглашение 4108 без клана сервер отвергает кодом 4 (`ClanMemberInvite_RequestUser`, GameServer.c:735,
//       `sub_401070 < 0`) → 12417 «Новых членов клана добавляет только его глава». Готовой строки «ты не в клане» в данных нет
//       (7071 — только про клан-чат) ⇒ новая loc 991210. [ИСПРАВЛЕНИЕ] `asda.Clan.NoClanNotice 1`; 0 = 1:1. `NoClanLocOr`.
//
// ═════════ 30027: ПОЧЕМУ ВЕТКА МЁРТВАЯ ════════════════════════════════════════════════════════
//   `CLAN_INCREASE_BOX` (14450) открывается ТОЛЬКО из `case 30027` — других вызовов
//   `uiInventoryRepairDialogToggle(x, 1)` во всём эталоне нет (остальные три — с нулём, то есть
//   закрытие). А кода 30027 НЕТ ни в одной кнопке ни одного `.NSR`: проверены все пять деревьев
//   данных (187/187/187/190/195 файлов), и по разобранной структуре записи кнопки, и по сырому
//   поиску константы. Единственные совпадения байт 0x754B — внутри СТРОК заголовков NPC
//   (`kurian.nsr` смещение 0x0F = поле Title), то есть ложные. ★ 27.09.2026: код в клиенте
//   ЖИВОЙ (ветка `case 30027` → `uiInventoryRepairDialogToggle(this, 1)`), серверная половина
//   ЖИВАЯ (`ClanBaseInfo_MaxClanUserCount_Req@0x404710` → DB 2199 `UpClanMaxgrade` → 4137 + 4132),
//   недостаёт только КНОПКИ в данных диалога. Окно реализовано целиком; данные для него
//   (лимит членов, число расширений) дают блок клана `UAsdaCommunityClanSubsystem` (4123/4131/4132).
//
// ═════════ РАСШИРЕНИЕ КЛАНА: КЛИЕНТ-ЭТАЛОН ПРОТИВ СЕРВЕРА (решение владельца 27.09.2026) ═══════
//   | что                     | клиент-эталон (`uiInventoryRepairDialogToggle@0xBC5600`) | СЕРВЕР (`0x404710`, живой образ) |
//   | прибавка мест           | `cap + 5` в 14455 (`ui_core.c:16211`)                    | `v15 = v24 + 10`, `v16 = 10` (0x404ADA/0x404AE8) |
//   | число расширений        | гейт `gap_616[45] < 4` → иначе 12455                      | `v25 == 5` → код 5 (0x40498E)     |
//   | цены по ступеням        | ctor `uiCharWnd_Ctor`: 80000/190000/400000/800000 (4 шт.) | 80000/190000/400000/800000/2000000 (0x4049F0…0x404A18) |
//   | потолок мест            | нет                                                       | `v24 + 5 <= 100` → иначе код 3 (проверяет +5, прибавляет +10) |
//   Авторитет — СЕРВЕР (закон `client-server-table-desync`): порт показывает +10, пускает до
//   5 расширений и берёт цену 5-й ступени 2 000 000. Прежние клиентские числа — за ключом
//   `asda.Clan.IncreaseRule 1` (1:1 клиент-эталон, для сравнения).
//   Проверка потолка на сервере (`+5 <= 100` при прибавке +10) — остаток шага +5; клиент её не
//   повторяет (решает сервер кодом 3 → 12453), сервер не правим (закон `old-server-protocol-compat`).
//   ★ Поправка ревизии 27.09.2026: начальный лимит УСТАНОВЛЕН — `ClanCreateReq@0x4017B0`, `0x401B67`
//   `C6 85 9C FE FF FF 32` → блок+45 = 50. Путь 50 + 5×10 = ровно 100 (перед 5-м расширением 90, проверка
//   95 ≤ 100). Лимит 105 в обычной игре НЕДОСТИЖИМ (только при лимите 91…95 из БД); код 3 на деле мёртв.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaClanSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UButton;
class UTextBlock;
class UEditableText;
class UAsdaButtonRelay;

UCLASS()
class ASDAREBORN_API UAsdaClanSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Окно, его защёлка «запрос в полёте» и полученные от сервера данные
	// принадлежат СЕССИИ и переживали разрыв связи вместе с GameInstance.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaClanSubsystem, STATGROUP_Tickables); }
	// ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── IDNum контролов (`CmtyClan_Delete_BuildUI@0xBC0650`) ──
	static constexpr int32 kIdMakeWin    = 14400;  // `CLAN_MAKING_WIN`   — панель создания
	static constexpr int32 kIdMakeTitle  = 14404;  // `CLAN_TITLE`        — нотис 12216
	static constexpr int32 kIdMakeNameLb = 14403;  // `CLAN_NAME`         — нотис 12217
	static constexpr int32 kIdMakeInput  = 14402;  // `CLAN_NAME_INPUT`   — поле ввода
	static constexpr int32 kIdMakeTypeLb = 14405;  // `CLAN_TYPE`         — нотис 12218
	static constexpr int32 kIdMakeTypeBt = 14406;  // `CLAN_TYPE_BTN`     — ★ дефект D1: мёртвый
	static constexpr int32 kIdMakeType1  = 14407;  // `CLAN_TYPE1`        — нотис 12219
	static constexpr int32 kIdMakeType2  = 14408;  // `CLAN_TYPE2`        — нотис 12220
	static constexpr int32 kIdMakeFeeLb  = 14409;  // `CLAN_FEE`          — нотис 12221
	static constexpr int32 kIdMakeFeeVal = 14410;  // `CLAN_FEE_VALUE`    — 10 000
	static constexpr int32 kIdMakeMonLb  = 14412;  // `CLAN_MONEY`        — нотис 12222
	static constexpr int32 kIdMakeMonVal = 14413;  // `CLAN_MONEY_VALUE`  — золото игрока
	static constexpr int32 kIdMakeOk     = 14215;  // `CLAN_MAKE_BTN`     — нотис 12223
	static constexpr int32 kIdMakeCancel = 14416;  // `CLAN_CANCEL_BTN`   — нотис 2289
	static constexpr int32 kIdDelBox     = 14470;  // `CLAN_DELETE_BOX`   — панель роспуска
	static constexpr int32 kIdDelName    = 14471;  // `CLAN_DEL_NAME`     — ★ дефект D2
	static constexpr int32 kIdDelText    = 14272;  // `CLAN_DEL_TEXT`     — нотис 12449
	static constexpr int32 kIdDelOk      = 14473;  // `CLAN_DEL_OKBTN`    — нотис 2288
	static constexpr int32 kIdDelCancel  = 14274;  // `CLAN_DEL_CABTN`    — нотис 2289
	static constexpr int32 kIdIncBox     = 14450;  // `CLAN_INCREASE_BOX` — окно расширения (код NPC 30027)
	static constexpr int32 kIdIncName    = 14451;  // `CLAN_INC_NAME`     — имя клана (`char+0x93A+34`)
	static constexpr int32 kIdIncNowLb   = 14452;  // `CLAN_INC_MEMNOW`   — нотис 12444
	static constexpr int32 kIdIncNowVal  = 14453;  // `CLAN_INC_MEMNOW_V` — текущий лимит членов (блок+45)
	static constexpr int32 kIdIncNewLb   = 14454;  // `CLAN_INC_MEMINC`   — нотис 12445
	static constexpr int32 kIdIncNewVal  = 14455;  // `CLAN_INC_MEMINC_V` — лимит после расширения
	static constexpr int32 kIdIncCostLb  = 14456;  // `CLAN_INC_COST`     — нотис 12446
	static constexpr int32 kIdIncCostVal = 14457;  // `CLAN_INC_COST_V`   — цена ступени
	static constexpr int32 kIdIncMonLb   = 14458;  // `CLAN_INC_MONEY`    — нотис 12447
	static constexpr int32 kIdIncMonVal  = 14459;  // `CLAN_INC_MONEY_V`  — золото игрока
	static constexpr int32 kIdIncText    = 14460;  // `CLAN_INC_TEXT`     — нотис 12448
	static constexpr int32 kIdIncOk      = 14461;  // `CLAN_INC_OKBTN`    — нотис 2288 → 4136
	static constexpr int32 kIdIncCancel  = 14462;  // `CLAN_INC_CABTN`    — нотис 2289

	// ── Правила расширения клана (таблица «клиент против сервера» — в шапке) ──
	/** СЕРВЕР: прибавка мест за одно расширение (`ClanBaseInfo_MaxClanUserCount_Req@0x404710`:
	 *  `mov byte [ebp-0xEE], 0xA` @0x404ADA, `add eax, 0xA` @0x404AE8 — дизасм ЖИВОГО образа). */
	static constexpr int32 kIncSeatsServer = 10;
	/** СЕРВЕР: расширений всего (`cmp eax, 5` @0x40498E → код 5 при пятом сделанном). */
	static constexpr int32 kIncStepsServer = 5;
	/** КЛИЕНТ-ЭТАЛОН: прибавка в 14455 (`ui_core.c:16211`, `cap + 5`) — только под `asda.Clan.IncreaseRule 1`. */
	static constexpr int32 kIncSeatsClient = 5;
	/** КЛИЕНТ-ЭТАЛОН: гейт `gap_616[45] < 4` (`uiInventoryRepairDialogToggle@0xBC5600`). */
	static constexpr int32 kIncStepsClient = 4;
	// Цены ступеней (СЕРВЕР, пять штук) — таблица `kIncCostTable` в .cpp (массив вне UCLASS, чтобы
	//   не нагружать разбор UHT); источник и сверка с клиентом — там же.

	// Стоимость создания клана. ★ КОНСТАНТА КЛИЕНТА: конструктор кланового окна
	//   `uiCharWnd_Ctor@0xBBBAA0` кладёт в поле +0x7B0 литерал `0x2710` (дизасм 0xBBBC2E:
	//   `mov dword ptr [eax + 0x7b0], 0x2710`), и `uiRepairWindowToggle` печатает ЭТО поле в 14410.
	//   Сервер сходится: `ClanCreateReq@0x4017B0` требует `денег > 10000`, иначе код 3.
	static constexpr int32 kCreateFeeGold = 10000;
	// Максимальная длина имени клана в БАЙТАХ: эталон `strlen(buf) < 0x11` (`ui_core.c:15355`),
	//   тело пакета отводит под имя 17 байт со завершающим нулём ⇒ 16 значащих.
	static constexpr int32 kClanNameMaxBytes = 16;
	// Z-порядок семьи «окна от NPC»: инвентарь 720, магазин 730, ремонт 731, витрина 732,
	//   зодиак 733 ⇒ клановым боксам 734.
	static constexpr int32 kWinZ = 734;

	// ═══ ОТКРЫТИЕ / ЗАКРЫТИЕ ═══
	/** Порт `uiRepairWindowToggle(this, 1)@0xBC2CE0` — окно СОЗДАНИЯ клана (код NPC 30015).
	 *  false = гейт не пропустил (показан нотис) либо виджет не загрузился. */
	UFUNCTION(BlueprintCallable, Category="Asda|Клан")
	bool OpenCreateWindow();

	/** Порт `uiRepairWindowToggle(this, 0)` — спрятать окно создания и очистить поля. */
	UFUNCTION(BlueprintCallable, Category="Asda|Клан")
	void CloseCreateWindow();

	/** Порт `uiRepairWindowToggle_54D0(this, 1)@0xBC54D0` — окно РОСПУСКА клана (код NPC 30016). */
	UFUNCTION(BlueprintCallable, Category="Asda|Клан")
	bool OpenDisbandWindow();

	/** ★ 08.11.2026 (пачка 16, баг 7) [ИСПРАВЛЕНИЕ] ДЕФЕКТ ОРИГИНАЛА D8: игроку БЕЗ клана эталон отвечает «только глава»
	 *  (12431 у гейтов 30016/30027 — `ui_core.c:16154`/`16188`; 12417 у ответа 4109 код 4 — сервер
	 *  `ClanMemberInvite_RequestUser`, GameServer.c:735, шлёт 4 и при «клана нет»). При ключе `asda.Clan.NoClanNotice 1`
	 *  (по умолчанию) и своём clanId == −1 возвращает loc 991210 «Ты не состоишь в клане.»; иначе — `OrigLocId` (1:1). */
	static int32 NoClanLocOr(const UGameInstance* GI, int32 OrigLocId);

	/** Порт `uiRepairWindowToggle_54D0(this, 0)` — спрятать окно роспуска. */
	UFUNCTION(BlueprintCallable, Category="Asda|Клан")
	void CloseDisbandWindow();

	/** ★ 27.09.2026. Порт `uiInventoryRepairDialogToggle(this, 1)@0xBC5600` — окно РАСШИРЕНИЯ клана
	 *  (код NPC 30027). Гейты 1:1 по порядку эталона (клан → «Друг» → число расширений), порог числа
	 *  расширений и прибавка мест — по правилам сервера (шапка, «РАСШИРЕНИЕ КЛАНА»). */
	UFUNCTION(BlueprintCallable, Category="Asda|Клан")
	bool OpenIncreaseWindow();

	/** Порт `uiInventoryRepairDialogToggle(this, 0)` — спрятать окно расширения. ★ [ПОРТ] БЕЗ гейтов эталона
	 *  «Друг»/«расширений ≥ 4» — исправление дефекта оригинала D7 (шапка). */
	UFUNCTION(BlueprintCallable, Category="Asda|Клан")
	void CloseIncreaseWindow();

	UFUNCTION(BlueprintPure, Category="Asda|Клан")
	bool IsCreateOpen() const { return bCreateOpen; }
	UFUNCTION(BlueprintPure, Category="Asda|Клан")
	bool IsDisbandOpen() const { return bDisbandOpen; }
	UFUNCTION(BlueprintPure, Category="Asda|Клан")
	bool IsIncreaseOpen() const { return bIncOpen; }

	// ═══ ДЕЙСТВИЯ (ветки `uiPanel_HandleCommand`) ═══
	void OnCreateOkClicked();       // 14215 → мат-фильтр → длина → 4104
	void OnCreateCancelClicked();   // 14416
	void OnDisbandOkClicked();      // 14473 → 4106
	void OnDisbandCancelClicked();  // 14274
	void OnIncreaseOkClicked();     // 14461 → гейт «Друг» → 4136 (`netSendInventoryItemNamePacket@0xBC50D0`)
	void OnIncreaseCancelClicked(); // 14462 → закрыть окно расширения

	/** Текст поля 14402 (для консоли и тестов; в игре его вводит игрок). */
	UFUNCTION(BlueprintCallable, Category="Asda|Клан")
	void SetClanNameInput(const FString& Text);
	UFUNCTION(BlueprintPure, Category="Asda|Клан")
	FString GetClanNameInput() const;

	// ═══ ДИАГНОСТИКА ═══
	FString DumpControls() const;   // КАЖДЫЙ разобранный контрол (закон full-structural-analysis)
	FString DumpState() const;      // гейты, свой клан, золото, флаги ожидания
	/** ★ 27.09.2026: самотест окна расширения 14450 (ТОЛЬКО офлайн, команда `asda.Clan.IncreaseSelfTest`).
	 *  Редакция 27.09.2026 (`who_clan_fix.md`): 4131/4132/4137 подаются через штатный приём
	 *  `DebugFeedPacket` → `HandlePacket`, очередь 4137 прокачивается `Tick`; 4136 — боевой отправитель под
	 *  узким перехватом; контрольные пары для кодов отказа 4137, коротких тел и закрытия [ПОРТ] без гейтов.
	 *  Окно открывается штатным путём (гейт «клана нет» снят только на время теста — офлайн клана нет).
	 *  Возвращает число проваленных проверок. */
	int32 RunIncreaseSelfTest();
	/** Самотест, режим `keep`: подать тестовый блок (4132) и оставить окно 14450 открытым для снимка. */
	void OpenIncreaseWindowForSelfTest(int32 Cap, int32 Level);

	// ═══ ★ 27.09.2026: ГЕЙТ «ДРУГ» 12435 — ОБЩАЯ ТОЧКА (`Reports/owner_0927/friend_gate_12435.md`) ═══
	/** [ОРИГИНАЛ] Порт проверки `CClass_GetField142C(&g_InventoryBag)->gap_8BE[3]` (char+0x8C1 = SoulState: 1 «душа»,
	 *  2 одержимость соулмейтом; зеркало `UAsdaNetSubsystem::SelfSoulSilhouette` ← 6152 @12, `PartyMark_Helper2@0xE64750`)
	 *  с нотисом 12435 «Друзьям этот навык недоступен» в ONEBUT 1500 (`CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1)`).
	 *  Все 7 мест эталона (второй метод — бинарь: 8 `push 3093h`, восьмое 0xBCB304 в функции без ссылок, опкод 4127
	 *  сервер не знает). bDropTopMsgBox = перед нотисом `itemBuyPanelRefreshOrHide` (`DropTopMsgBoxBeforeNotice`):
	 *  ДА — 4136 network.c:11775, 4106 :11984, 4108 social.c:3434, 4111 item_inventory.c:7360;
	 *  НЕТ — окна 30015 ui_core.c:15737, 30016 :16149, 30027 :16193. Why — для лога.
	 *  true = отказ показан, действие НЕ выполнять. */
	bool RefuseIfSoulmateLocked(bool bDropTopMsgBox, const TCHAR* Why) const;

	// ═══ ★ 27.09.2026: ВХОДЯЩЕЕ ПРИГЛАШЕНИЕ В КЛАН — 4110 → значок 6 → окно (1, 44) loc 12430 + полоса 30 с → 4111 ═══
	//   [ОРИГИНАЛ] network.c:2149-2152 (`Recv_PartyMember` case 6 network.c:27378-27390 + `itemSetSplitMoveSource@0xBC9E40`),
	//   клик по значку — `uiNoticeButtonDispatch` case 6 (ui_core.c:69419-69424), «Да» 0x2C (ui_core.c:57432-57434),
	//   «Нет» (:57740-57742), таймер `uiProgressTickTimedAction@0xD78C20` (ui_core.c:58335-58379, вызов game_main.c:2057-2058).
//   Ещё один вызов 4111 «нет» — `uiProgressBar_TickElapsed@0xD78A50` (ui_core.c:58323) — НЕДОСТИЖИМ: его флаг
//   `g_bProgressBarTickActive` (0x2C02BD4) в образе только читается (2 ссылки, записей нет) — в порт не переносится.
	//   ★ ДЕФЕКТ ОРИГИНАЛА D8 (исправлен [ПОРТ]): признак «окно ответа открыто» `dword_2BE50FC` ставится только
	//   (0xDF50B6) и НЕ СНИМАЕТСЯ НИКОГДА (бинарь: 3 ссылки, одна запись `mov [..], 1`). Итог у оригинала: (а) после
	//   первого окна больше ни одно приглашение значка не даёт (case 6 гейтится этим признаком) — до перезапуска
	//   клиента; (б) таймер крутится вечно: каждые 30 с шлёт 4111 «нет» с полями −1 и закрывает ТЕКУЩЕЕ окно-сообщение
	//   (любое), а своё окно по таймауту не уходит (возвращается из очереди). Порт снимает признак ответом и таймаутом,
	//   по таймауту снимает СВОЮ запись (1, 44).
	/** Клик по значку уведомления 6: окно (1, 44) с loc 12430 и полосой-таймером 30 с; признак «окно ответа открыто». */
	void OnInviteNoticeIconClicked();
	/** Порт `itemSendSplitOrMoveRequest@0xBC9ED0` (★ МИСНЕЙМ): гейт «Друг» (со снятием верхнего окна) → 4111 → сброс
	 *  полей приглашения. При отказе «Друг» поля НЕ сбрасываются (как эталон). true = 4111 ушёл. */
	bool SendInviteAnswer(bool bAccept);
	UFUNCTION(BlueprintPure, Category="Asda|Клан")
	bool IsInviteBoxActive() const { return bInviteBoxActive; }
	/** ★ 27.09.2026: офлайн-самотест гейта «Друг» во всех местах порта и потока приглашения 4110/4111
	 *  (команда `asda.Clan.FriendGateSelfTest`). Возвращает число проваленных проверок. */
	int32 RunFriendGateSelfTest();

private:
	// ── Жизненный цикл виджетов (два ОТДЕЛЬНЫХ окна `.JUI`, у каждого своя ручка) ──
	bool EnsureCreateWidget();
	bool EnsureDisbandWidget();
	bool EnsureIncreaseWidget();
	void ResetCreateWidget();
	void ResetDisbandWidget();
	void ResetIncreaseWidget();
	void SetupCreateControls();
	void SetupDisbandControls();
	void SetupIncreaseControls();

	// ── Окно расширения 14450: числа из блока клана и правил (шапка, «РАСШИРЕНИЕ КЛАНА») ──
	/** Прибавка мест за одно расширение: сервер 10, при `asda.Clan.IncreaseRule 1` — клиент 5. */
	int32 IncSeatsStep() const;
	/** Сколько расширений разрешено всего: сервер 5, при `asda.Clan.IncreaseRule 1` — клиент 4. */
	int32 IncStepsMax() const;
	/** Текущий лимит членов (блок+45) и число сделанных расширений (блок+47); −1 = блока нет. */
	int32 IncCurrentSeats() const;
	int32 IncDoneSteps() const;
	/** Порт ветки `a2 != 0` эталона: имя клана, «сейчас», «станет», цена ступени, золото. */
	void FillIncreaseNumbers();

	// ── Гейты (значения — зеркала полей `CCharacter`, см. шапку) ──
	int32 SelfClanId() const;        // char+2408 (−1 = клана нет)
	bool  IsSoulmateLocked() const;  // char+2241 != 0 → нотис 12435
	int32 SelfClanRank() const;      // char+2427 (1 = глава)
	FString SelfClanName() const;    // char+2410
	int64 SelfGold() const;          // g_pGameClient+770234

	// ── Нотисы ──
	FString Loc(int32 LocId) const;
	/** Эталон: `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст)` = `ONEBUT_MSGBOX` 1500. */
	//   EtalonNoticeId — id эталонного вызова для очереди окон-сообщений (−1 у всех, кроме 7009, ui_core.c:15361).
	void NoticeMsgBox(const FString& Text, int32 EtalonNoticeId = -1) const;
	void NoticeMsgBoxLoc(int32 LocId, int32 EtalonNoticeId = -1) const { NoticeMsgBox(Loc(LocId), EtalonNoticeId); }

	// ── Сеть ──
	void HandleCreateResult(const TArray<uint8>& Body);    // 4105, тело 196 б
	void HandleDisbandResult(const TArray<uint8>& Body);   // 4107, тело 1 б
	void HandleIncreaseResult(const TArray<uint8>& Body);  // 4137, тело 9 б (★ 27.09.2026)

	// ── Состояние: окно создания 14400 ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget>   CreateWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UEditableText> NameInput = nullptr;     // 14402
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtMakeTitle = nullptr;  // 14404
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtMakeNameLb = nullptr; // 14403
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtMakeTypeLb = nullptr; // 14405
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtMakeType1 = nullptr;  // 14407
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtMakeType2 = nullptr;  // 14408
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtMakeFeeLb = nullptr;  // 14409
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtMakeFeeVal = nullptr; // 14410
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtMakeMonLb = nullptr;  // 14412
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtMakeMonVal = nullptr; // 14413
	UPROPERTY(Transient) TObjectPtr<UButton>       BtnMakeOk = nullptr;     // 14215
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtMakeOk = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton>       BtnMakeCancel = nullptr; // 14416
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtMakeCancel = nullptr;

	// ── Состояние: окно роспуска 14470 ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> DisbandWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtDelName = nullptr;      // 14471
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtDelText = nullptr;      // 14272
	UPROPERTY(Transient) TObjectPtr<UButton>     BtnDelOk = nullptr;        // 14473
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtDelOk = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton>     BtnDelCancel = nullptr;    // 14274
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtDelCancel = nullptr;

	// ── Состояние: окно расширения 14450 (★ 27.09.2026; имена узлов — headless-дамп ассета) ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> IncWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtIncName = nullptr;      // 14451
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtIncNowLb = nullptr;     // 14452
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtIncNowVal = nullptr;    // 14453
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtIncNewLb = nullptr;     // 14454
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtIncNewVal = nullptr;    // 14455
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtIncCostLb = nullptr;    // 14456
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtIncCostVal = nullptr;   // 14457
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtIncMonLb = nullptr;     // 14458
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtIncMonVal = nullptr;    // 14459
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtIncText = nullptr;      // 14460
	UPROPERTY(Transient) TObjectPtr<UButton>     BtnIncOk = nullptr;        // 14461
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtIncOk = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton>     BtnIncCancel = nullptr;    // 14462
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtIncCancel = nullptr;

	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	bool bCreateReady = false;
	bool bDisbandReady = false;
	bool bCreateOpen = false;
	bool bDisbandOpen = false;
	/** Пакет ушёл, ждём ответа: у эталона своего флага нет (окно уже спрятано), у нас он
	 *  гасит повторные клики по кнопке до прихода 4105/4107. */
	bool bAwaitCreate = false;
	bool bAwaitDisband = false;
	bool bIncReady = false;     // окно 14450 разобрано
	bool bIncOpen = false;      // окно 14450 показано
	/** 4136 ушёл, ждём 4137. У эталона флага нет (окно висит до кода 1); у нас гасит повторные
	 *  клики «Подтвердить» — повторный 4136 списал бы золото за вторую ступень. */
	bool bAwaitIncrease = false;
	/** Ревизия блока клана, по которой окно 14450 заполнено (сдвинулась → перезаполнить). */
	int32 IncShownRevision = -1;
	/** Только `RunIncreaseSelfTest` (офлайн): пропустить гейт «клана нет» (12431). В игре всегда false. */
	bool bSelfTestSkipClanGate = false;

	FAsdaMouseFrame Mouse;
	// Закон `no-reentrant-net-callbacks`: колбэк только КОПИТ тело, разбор — в Tick.
	TAsdaNetQueue<TArray<uint8>> PendingCreate;
	TAsdaNetQueue<TArray<uint8>> PendingDisband;
	TAsdaNetQueue<TArray<uint8>> PendingIncrease;   // 4137
	FDelegateHandle HCreate, HDisband, HIncrease;

	// ── ★ 27.09.2026: входящее приглашение в клан (4110/4111) ──
	void HandleInviteRecv(const TArray<uint8>& Body);   // 4110, тело 23 б (из Tick)
	void TickInviteTimer();                             // порт `uiProgressTickTimedAction@0xD78C20`
	void EndInviteBox(const TCHAR* Why);                // [ПОРТ] снять признак `dword_2BE50FC` (дефект D8)
	TAsdaNetQueue<TArray<uint8>> PendingInvite;        // 4110 — копит колбэк сети
	FDelegateHandle HInvite;                            // подписка на `OnClanInviteRecv`
	FDelegateHandle HNoticeIcon;                        // подписка на клик по значку уведомления
	/** Поля приглашения `CCommunityWnd+0x768…+0x77E` (`itemSetSplitMoveSource`): clanId, имя (сырые 17 б),
	 *  userNum пригласившего и приглашённого. Заготовка эталона: −1 / нули / −1 / −1. */
	int32 InviteClanId = -1;
	TArray<uint8> InviteClanName;
	int32 InviteReqUser = -1;
	int32 InviteRespUser = -1;
	/** Признак `dword_2BE50FC` «окно ответа на приглашение открыто» — гейт значка и двигатель таймера. */
	bool bInviteBoxActive = false;
	/** FPlatformTime::Seconds() старта таймера окна (эталон `gap_3300[76]` = GetTickCount первого тика). */
	double InviteBoxStart = -1.0;
};
