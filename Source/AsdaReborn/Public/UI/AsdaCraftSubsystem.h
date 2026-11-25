// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaCraftSubsystem — МОДЕЛЬ И ПРОТОКОЛ РЕМЕСЛА («Создание снаряжения»), 25.09.2026, волны В0–В2.
//   Реверс: `Reports/craft/craft_re_report.md` (§5 протокол, §6 поток и нотисы, §9 дефекты) + РЕВИЗИЯ
//   `Reports/craft/craft_re_REVIEW.md` (её поправки важнее отчёта). Отчёт захода — `Reports/craft/wave0-2.md`.
//
// ═════════════════ ЧТО ЭТО В ОРИГИНАЛЕ ═══════════════════════════════════════════════════════════════
//   Своего «класса модели» у ремесла нет: всё живёт в полях окна списка `CItemMainWnd` (`g_pItemMainWnd`):
//     [218] лимит рецептов · [219] процент опыта ремесла · [220] опыт · [221] уровень ремесла ·
//     [222] число изученных · [223..802] флаги «изучен» по id (580) · [803..] список изученных id.
//   Порт держит эти поля здесь (GameInstance), а окно 76000 (`UAsdaCraftWindowSubsystem`) их только рисует —
//   так же, как модель питомцев отделена от их окон. Окна 77000/77500 (волны В3/В4) придут сюда же.
//
// ═════════════════ ПРИЁМ (все пять ответов сервера) ══════════════════════════════════════════════════
//   6597 (593 б) `shopParseStockPacket@0xFDEEB0` (миснейм, shop_trade.c:22235): @0 i16 изучено · @2 i16 лимит
//        (сервер: `lvl*(10*craftLvl/5)/10 + (char)byte[CUser+0x4090D] + 20`, GameServer.c:121034) · @4 u8×580
//        флаги по id · @584 u8 уровень · @585 i32 опыт · @589 i32 процент.
//        ★ Ревизия Р-1: 6597 шлёт GameServer ПОСЛЕ входа (DB 0xA64 → `recvDBCraftList`) и ЗАНОВО при каждом
//        портале (DBManager `Recv_PortalMoveUserInfo`). Модель чистится на входе/выходе/разрыве и НЕ чистится
//        на смене карты — иначе поздний сброс `MapChanged` стёр бы свежий 6597 (каждый 6597 и так пишет всё).
//   6599 (64 б) `netHandleInventoryUpdatePacket@0xFDF080` (network.c:49512): @0 u8 код · @1 i32 вес ·
//        @5 запись свитка (57) · @62 i16 id рецепта. Коды 1/10/11/12/13/99.
//   6601 (3 б) `netOnSubcodeNoticeHandler@0xFDF3E0` (network.c:49591): @0 u8 код · @1 i16 id. Коды 1/10/11/99.
//   6603 (525 б) `upgradeDlg_onResultPacket@0xFE65E0` (item_enhance.c:10335): @0 u8 код · @1 i16 вес · @3 u8
//        уровень · @4 i32 опыт · @8 i16 процент · @10 u8 достигнутая ступень · @11 u8 «+1 шт.» · @12 запись
//        продукта (57) · @69 8×запись (6 материалов + 2 катализатора). Коды 0/1/2/3/4/5/6/99 (+ 10 — К-5).
//   6605 (61 б) `netOnItemUseResult@0xFE0800` (network.c:50016): @0 u8 код · @1 u8 новый уровень ·
//        @2 i16 вес · @4 запись свитка. Коды 1/2/99 (0 эталон молча пропускает).
//   Записи инвентаря в ответах применяются режимом `CountOrClear` общей точки `ApplyExternalRecord`
//   (count > 0 — только количество, count <= 0 — удалить слот; контейнеры 1/2) — ровно как эталон.
//
// ═════════════════ ОТПРАВКА ══════════════════════════════════════════════════════════════════════════
//   6598/6600/6602/6604 — сборщики в `UAsdaNetSubsystem` поверх общего `SendSecured` (транспорт не
//   дублируется). В телах нет ни charId, ни userNum (сервер берёт пользователя по сокету).
//
// ═════════════════ КЛЮЧИ ОТКАТА (дефекты оригинала, `asda.Craft.*`, умолчание = «исправлено») ═════════
//   asda.Craft.FixLimitBonus   (К-8) — лимит «N / M» при пересчётах клиента учитывает серверную надбавку;
//   asda.Craft.FixUnknownCode  (К-5) — 6603 код 10: ONEBUT 5566 + сброс окна 77500; любой другой неизвестный код (В5) —
//                                      окну 77500 возвращаются кнопки (замок уже снят приёмом, таймаут не сработает);
//   asda.Craft.FixLateResult   (К-1) — ответ 6603 в сброшенное/закрытое окно 77500 кладёт продукт в сумку сразу
//                                      (эталон терял продукт и падал); сброс закрытого окна не трогает HUD/снимок.
//   asda.Craft.FixStackPick    (К-6) — «есть» материала и «хватает» — по наибольшему стеку; слот в 6602 — первый
//                                      стек, где хватает (сервер списывает только из указанного стека).
//   asda.Craft.SingleInFlight  (С-3/С-18, клиентская часть P35) — один запрос ремесла в полёте на все четыре
//                                      опкода; ожидание снимают ответ (любой код), сброс сессии и таймаут;
//   asda.Craft.ReplyTimeoutSec (20)  — секунд до снятия замка без ответа (дольше серверных 15 с P35; 0 = без таймаута).
//   Ключи окна 76000 — в `AsdaCraftWindowSubsystem.cpp`, окна состава 77000 — в `AsdaCraftRecipeWindowSubsystem.h`,
//   окна изготовления 77500 — в `AsdaCraftMakeWindowSubsystem.h` (волна В4).
//
// ═════════════════ ВОЛНА В4: СВИТКИ И ОКНО 77500 ═══════════════════════════════════════════════════════
//   Свитки ремесла (`TryUseCraftScroll`, зовёт двойной клик ОКНА СУМКИ — контейнер 2): linkCode 101 → TWOBUT 5571 (84)
//   → 6598; linkCode 102 и `LinkedGroupCode == уровень + 1` → TWOBUT 5582 (85) → 6604, иначе ONEBUT 5584.
//   Окно 77500 (`UAsdaCraftMakeWindowSubsystem`): приёмник 6603 сбрасывает его по кодам отказа, код 1/2 окно «забирает»
//   (`ClaimEnterResult`), продукт кладёт конец анимации. Косвенные звуки окон-сообщений (ревизия реверса МЕЛК-3):
//   открытие ONEBUT/TWOBUT — id 25 (`CItem_BuildTooltipText`, item_inventory.c:51589), кнопка — id 4, закрытие — id 5
//   (`OnSendItemUpgrade` msg 1503/1523/1524 → `CGameUI_CloseWindow`, ui_core.c:56957, game_main.c:9860/9888).
//   ★ Волна В5 (W4-1, 1:1 как оригинал): их играют ОБЩИЕ точки окон-сообщений (`ShowGameMessage`/`ShowGameConfirm`/
//   `CloseWindow` оконной подсистемы) — у ВСЕХ систем; собственные вызовы ремесла удалены (иначе звук был бы двойным).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"          // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaNetQueue.h"              // ★ U7: TAsdaNetQueue — очередь пакетов из сетевого колбэка
#include "AsdaReplyLatch.h"            // ★ S2 (В2-A дедупа №2): засов «операция ремесла в полёте» — FAsdaReplyLatch
#include "AsdaCraftSubsystem.generated.h"

class UDataTable;
struct FAsdaMakingRecipeRow;
struct FAsdaMakingRateRow;

// ── Нотисы ремесла. Каждый id — по СВОЕМУ обработчику эталона (закон `loc-per-handler-not-by-family`),
//    все сверены с `RawAssets/Text/GameText.csv`. ──
namespace AsdaCraftLoc
{
	// Подписи окна 76000 — `CItemMainWnd_InitControls@0xFDC6A0` (item_inventory.c:69298-69505).
	constexpr int32 kTitle        = 5500;    // 76001 «Создание предмета»
	constexpr int32 kBtnDelete    = 5522;    // 76096 «Удалить инструкцию»
	constexpr int32 kBtnClose     = 5035;    // 76097 «Закрыть»
	constexpr int32 kCraftLvl     = 5501;    // 76002 «Ур. ремесла»
	constexpr int32 kLvlShort     = 5502;    // 76003 «Ур.»
	constexpr int32 kTabBase      = 5540;    // 76007 вкладки 0..3 = 5540..5543 («Оружие/Доспехи/Материалы/Другое»),
	constexpr int32 kTabAll       = 5328;    //   вкладка 4 = «ВСЕ»; подпись категории строки — тоже 5540 + кат.
	constexpr int32 kColList      = 5510;    // 76011…76015 = 5510…5514 («Список/Ур./Тип/Ур. навыка/Название»)
	// Подсказки 76005/76006 — `shopGridWndProc` msg 8 (shop_trade.c:22089-22100): `tutorialPopupLayoutSmall`.
	constexpr int32 kTipTitle     = 12077;   // заголовок обеих подсказок
	constexpr int32 kTipLimit     = 115300;  // 76006 «N / M» — ★ К-4: в клиенте строки нет; с 26.09 в порту из AsdaFableClassic
	constexpr int32 kTipExp       = 115301;  // 76005 полоса опыта — ★ К-4: то же (решение владельца 26.09.2026)
	constexpr int32 kTipMissingIdx99 = 1005; // фолбэк `StringTable_GetById@0x111F4C0` для отсутствующего id —
	                                         //   строка ИНДЕКСА 99 таблицы MINE = id 1005 «Ты ранишь монстра.»
	// Гейт открытия `uiCheckPartyInviteInvLockGuard@0xFDD0F0` (ui_core.c:100482) — чат цветом −6881324.
	constexpr int32 kGateExchange = 5590;    // «Открыто окно обмена. Закрой его и повтори попытку.»
	constexpr int32 kGateDialog   = 5591;    // «Открыто окно диалога. Закрой его и повтори попытку.»
	// 6599 `netHandleInventoryUpdatePacket` (network.c:49531-49585).
	constexpr int32 kLearned      = 5572;    // код 1 — чат −11514 + плашка
	constexpr int32 kDataError    = 5560;    // код 10
	constexpr int32 kAlreadyReg   = 5561;    // код 11
	constexpr int32 kRegLimit     = 5562;    // код 12
	constexpr int32 kUnknownData  = 5563;    // код 99 (и 6601/6603/6605 99, 6603 0)
	constexpr int32 kSkillLow     = 5586;    // код 13
	// 6601 `netOnSubcodeNoticeHandler` (network.c:49605-49620): 10 → 5560, 11 → 5564, 99 → 5563 (ONEBUT, команда 24).
	constexpr int32 kNotRegistered= 5564;
	// 6603 `upgradeDlg_onResultPacket` (item_enhance.c:10371-10487).
	constexpr int32 kLackMaterials= 5566;    // код 3 (и исправление К-5 для кода 10)
	constexpr int32 kLackWeight   = 5567;    // код 4
	constexpr int32 kNoBagSlot    = 5568;    // код 5
	constexpr int32 kNoRecipe     = 5585;    // код 6
	// 6605 `netOnItemUseResult` (network.c:50037-50073).
	constexpr int32 kLevelUp      = 5583;    // код 1 — чат −11514 + плашка
	constexpr int32 kLevelNotMax  = 5581;    // код 2
	// Свитки ремесла в сумке — `CInventory_OnSlotUse@0xD07460` (item_inventory.c:37833-37852), волна В4.
	constexpr int32 kAskLearn     = 5571;    // linkCode 101 — TWOBUT «Прочитать инструкцию?», команда 84
	constexpr int32 kAskLevelUp   = 5582;    // linkCode 102 — TWOBUT «Повысить ур. ремесла?», команда 85
	constexpr int32 kWrongLevel   = 5584;    // linkCode 102 не того уровня — ONEBUT «Неверный ур. ремесла.»
	// Окно изготовления 77500 — `CMakeingItem_BindControls@0xFE47B0` / `upgradeDlg_onResultPacket` / `upgradeAnimDlg_tick`.
	constexpr int32 kMakeBtnCreate = 5533;   // 77533 «Создать»
	constexpr int32 kMakeBtnClose  = 5035;   // 77534 «Закрыть»
	constexpr int32 kMakeQtyLabel  = 5520;   // 77536 «Кол-во»
	constexpr int32 kMakeInProgress= 5575;   // 6603 код 1/2 — статус белым «Идет создание предмета»
	constexpr int32 kMakeTierDone  = 5576;   // ступень пройдена — «Создание '%s' завершено.»
	constexpr int32 kMakeGot       = 5577;   // итог — «Ты получил '%s'.»
	constexpr int32 kMakeTierFail  = 5578;   // ступень/итог провал — «Не удалось создать '%s'.»
	constexpr int32 kMakeBonusTier = 5579;   // бонус-ступень — «Создание 1 доп. '%s' завершено.»
	constexpr int32 kMakeGotBonus  = 5580;   // итог с бонусом — « Ты получил 2 доп. '%s'.»
}

// ── Итог последнего 6603 для окна изготовления 77500 (волна В4). Модель применяет в сумку всё, что
//    эталон применяет СРАЗУ (материалы, вес, уровень/опыт), и хранит то, что эталон кладёт в поля окна. ──
USTRUCT()
struct FAsdaCraftEnterResult
{
	GENERATED_BODY()
	UPROPERTY(Transient) int32 Code = -1;          // @0  → `[40]`
	UPROPERTY(Transient) int32 ReachedTier = 0;    // @10 → `[42]` (0 = провал на первой ступени)
	UPROPERTY(Transient) int32 BonusFlag = 0;      // @11 → `[43]` «+1 шт.» (тот же продукт, количество +1 — ревизия Р-2)
	UPROPERTY(Transient) TArray<uint8> Product57;  // @12 → `[47..61]` запись продукта (при провале — нули)
	UPROPERTY(Transient) bool bProductApplied = false;   // продукт уже положен (К-1: окно 77500 ответа не ждало)
};

UCLASS()
class ASDAREBORN_API UAsdaCraftSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable ── модель — данные ПЕРСОНАЖА: чистятся на входе/выходе/разрыве, НЕ на портале.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaCraftSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ═══ ИМЕНА ОКОН (единая точка — их читают гейты чужих систем) ═══
	static constexpr const TCHAR* kMainWindowId = TEXT("ITEM_making_WIN");       // 76000 `g_pItemMainWnd`
	static constexpr const TCHAR* kRecipeWindowId = TEXT("ITEM_making_R_BOX");   // 77000 (волна В3)
	static constexpr const TCHAR* kMakeWindowId = TEXT("ITEM_making_Mark_WIN");  // 77500 `g_pItemQtyMakeDlg` (волна В4)
	static constexpr int32 kMaxRecipeId = 580;     // флаги «изучен» по id — 580 байт (@4 пакета 6597)

	// ★ Единый предикат «окно ремесла открыто» — порт пары `tileInteract_isControlVisible(g_pItemMainWnd)` ||
	//   `isBoundControlVisible(g_pItemQtyMakeDlg)`, которую эталон проверяет в 28 чужих функциях (§4.3 отчёта).
	static bool IsAnyCraftWindowOpen(const UGameInstance* GI);

	// ═══ ДАННЫЕ ТАБЛИЦ (`CItemMainWnd_LoadDataAndInit@0xFDD630`) ═══
	// Запись рецепта по id (`findRecordById16@0xFDEDA0`: сравнение `*(i16*)rec` с id). nullptr — нет.
	const FAsdaMakingRecipeRow* FindRecipe(int32 RecipeId) const;
	// ★ [ПОРТ] 09.10.2026 (мультиязычность): имя рецепта (+2) для показа игроку — на языке сессии (оверлей `making_recipe`, ключ
	//   `<_row>.RecipeName`). `R` — запись из `FindRecipe`. Пусто при nullptr.
	FString RecipeName(const FAsdaMakingRecipeRow* R) const;
	// Запись исходов по MakeIndex (`shopFindRecordByItemId@0xFDEE30`, миснейм: сравнение `*(u16*)rate`).
	//   ⚠ MakeIndex — поле рецепта +124, в генерируемой строке DT оно пока называется `IdCopy`.
	const FAsdaMakingRateRow* FindRate(int32 MakeIndex) const;
	// Число записей таблиц (для дампа/самотеста).
	int32 GetRecipeCount() const;
	int32 GetRateCount() const;

	// ═══ МОДЕЛЬ (поля `CItemMainWnd`) ═══
	int32 GetCraftLevel() const { return CraftLevel; }        // [221]
	int32 GetCraftExp() const { return CraftExp; }            // [220]
	int32 GetCraftPercent() const { return CraftPercent; }    // [219]
	int32 GetRecipeLimit() const { return RecipeLimit; }      // [218]
	int32 GetLimitBonus() const { return LimitBonus; }        // К-8: надбавка сервера (6597 @2 − формула клиента)
	int32 GetLearnedCount() const { return LearnedCount; }    // [222]
	const TArray<int32>& GetLearnedList() const { return LearnedList; }   // [803..] (порядок = по id)
	bool IsLearned(int32 RecipeId) const;                     // `arrayContainsValue@0xFE0FD0`
	bool HasReceivedList() const { return bListReceived; }    // 6597 уже приходил в этой сессии

	// Пересчёт лимита клиентом: `uiCraftComputeSuccessRate@0xFDD510` (миснейм) / `tileInteractGateAndOpen` /
	//   6603 — `charLevel * (10*craftLvl/5) / 10 + 20` (+ надбавка при `asda.Craft.FixLimitBonus 1`).
	void RecalcLimit(const TCHAR* Why);
	// Формула клиента БЕЗ надбавки (для дампа и проверки К-8).
	int32 ClientLimitFormula() const;
	// `craftCheckRequiredItems@0xFE0CF0`: всех шести материалов хватает. Счёт — `UsableMaterialCount` (К-6: при
	//   `asda.Craft.FixStackPick 1` — наибольший стек, 0 — сумма по всем стекам, как эталон).
	bool CheckRequiredItems(const FAsdaMakingRecipeRow& R) const;

	// ═══ ★ К-6: МАТЕРИАЛЫ ПО СТЕКАМ СУМКИ (волна В3) ═══
	//   Сервер (`RecvUser_CraftEnter`, GameServer.c:121471-121473, гейт 7) списывает материал ТОЛЬКО из стека, чей
	//   слот пришёл в 6602, а контейнер принимает только 1/2 (патч P35 С-1/С-2). Клиент же считает сумму всех стеков
	//   (`CInventory_CountItemById@0x9998E0`) и шлёт слот ПОСЛЕДНЕГО стека с этим кодом (`itemTableFindRecordById@0x999C80`,
	//   цикл без `break`) — при двух неполных стеках окно обещает изготовление, а сервер отвечает кодом 10/3.
	//   Контейнер — как у эталона: снаряжение (`itemIsWeaponOrEquipClass`) — 1 (30 ячеек), прочее — 2 (60 ячеек).
	// Эталон: сумма по стекам (`CInventory_CountItemById`; нестакаемые — по ячейке).
	int32 CountMaterial(int32 ItemCode) const;
	// Наибольший ОДИН стек этого кода в его контейнере (нестакаемые — 1 за ячейку).
	int32 MaxMaterialStack(int32 ItemCode) const;
	// Сколько материала «есть» для окна и проверок: `FixStackPick` ? наибольший стек : сумма.
	int32 UsableMaterialCount(int32 ItemCode) const;
	// Слот/контейнер стека для тела 6602 (окно 77500 — волна В4). `FixStackPick 1`: ПЕРВЫЙ стек с `кол-во >= Need`
	//   (нет такого — false: эталон бы отправил, сервер отказал бы); 0: последний стек с кодом (`itemTableFindRecordById`).
	bool PickMaterialStack(int32 ItemCode, int32 Need, int32& OutSlot, uint8& OutContainer) const;
	// ★ К-6 для окна 77500 (волна В4): сколько штук можно изготовить из этого материала ПОДРЯД, если каждая штука
	//   списывается из одного стека (сервер). `FixStackPick 1`: Σ по стекам ⌊единиц / Need⌋; 0 — как эталон
	//   `craftCalcMaxMakeableCount`/`craftWndUpdateAffordability`: сумма всех стеков / Need. Need <= 0 — без ограничения.
	int32 CraftableFromStacks(int32 ItemCode, int32 Need) const;

	// ═══ ПОЛЯ РЕЦЕПТА ПО ИНДЕКСУ (ревизия В3 МЕЛК-11: одна точка на все окна ремесла) ═══
	//   Генерируемая строка DT держит 6 материалов/количеств/подрецептов отдельными свойствами; индекс вне 0..5 — −1.
	static int32 RecipeMatCode(const FAsdaMakingRecipeRow& R, int32 I);   // +76 + 4i
	static int32 RecipeMatQty(const FAsdaMakingRecipeRow& R, int32 I);    // +100 + 2i
	static int32 RecipeSubRecipe(const FAsdaMakingRecipeRow& R, int32 I); // +112 + 2i
	static int32 RecipeReqLevel(const FAsdaMakingRecipeRow& R);           // `(char)rec[67]`
	// Исходы ступеней по индексу (rate +8 + 4k), −1 вне 0..6.
	static int32 RateOutItem(const FAsdaMakingRateRow& R, int32 K);

	// ═══ СВИТКИ РЕМЕСЛА (волна В4) ═══
	// Порт веток `linkCode == 101/102` `CInventory_OnSlotUse@0xD07460` (item_inventory.c:37833-37852) — зовёт ТОЛЬКО
	//   двойной клик окна сумки (`UAsdaInventorySubsystem::UseFromBagWindow`), контейнер 2. true — предмет свиток ремесла
	//   и ветка отработала (подтверждение или отказ 5584); false — не свиток, идёт обычное использование.
	bool TryUseCraftScroll(int32 Container, int32 Cell);

	// Итог последнего 6603 (для окна 77500, волна В4).
	const FAsdaCraftEnterResult& GetLastEnterResult() const { return LastEnter; }

	// ═══ ОТПРАВИТЕЛИ (обёртки над `UAsdaNetSubsystem`; транспорт — общий `SendSecured`) ═══
	//   ★ Замок «одна операция ремесла в полёте» (`asda.Craft.SingleInFlight`, умолчание 1) — клиентская пара
	//   серверного замка патча P35 (`Reports/craft/wave6_server.md` С-3/С-18: ОДИН замок на все четыре опкода,
	//   дубль сервер отбрасывает БЕЗ ОТВЕТА). Пока ответа нет, следующий запрос ремесла не уходит; ожидание снимают
	//   ответ (любой код), сброс сессии (включая портал — сервер снимает замок на портале) и таймаут
	//   `asda.Craft.ReplyTimeoutSec` (дольше серверных 15 с) — молчание сервера не даёт вечного тупика.
	//   false — пакет не ушёл (нет сети или замок).
	bool SendLearnRecipe(int32 ScrollSlot, int32 ScrollContainer);        // 6598
	bool SendForgetRecipe(int32 RecipeId);                                // 6600
	bool SendCraftEnter(int32 RecipeId, const TArray<int32>& MatSlots, const TArray<uint8>& MatContainers);   // 6602
	bool SendLevelUpScroll(int32 ScrollSlot, int32 ScrollContainer);      // 6604
	// Идёт ли операция ремесла (запрос ушёл, ответа нет). Опкод запроса — `GetInFlightOpcode`.
	bool IsRequestInFlight() const { return ReplyLatch.IsArmed(); }
	int32 GetInFlightOpcode() const { return ReplyLatch.IsArmed() ? ReplyLatch.Opcode : 0; }
	// Самотест: снять замок без ответа сервера (перехваченный пакет ответа не получит).
	void DebugReleaseInFlight() { ReleaseInFlight(TEXT("самотест")); }

	// ═══ СОБЫТИЯ ═══
	// Модель изменилась (6597/6599/6601/6603/6605, сброс) — окно перерисовывает сводку/список.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaCraftModelChanged, int32 /*opcode или 0 — сброс*/);
	FOnAsdaCraftModelChanged OnModelChanged;
	// Пришёл 6603 с кодом 1/2 — для окна 77500 (волна В4). Если подписчик «забрал» результат
	//   (вернул true через `ClaimEnterResult`), продукт кладёт он в конце анимации; иначе — модель сразу (К-1).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaCraftEnterResult, const FAsdaCraftEnterResult&);
	FOnAsdaCraftEnterResult OnEnterResult;
	// Окно 77500 объявляет, что анимация идёт и продукт положит оно (порт `filler[256] = 1` живого окна).
	void ClaimEnterResult() { bEnterResultClaimed = true; }
	// Замок запроса снят ТАЙМАУТОМ (ответа не было): окно 77500, ждущее 6603, возвращает себе кнопки (волна В4) —
	//   иначе молчание сервера (дубль под замком P35 без ответа) оставило бы окно с выключенными кнопками навсегда.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaCraftRequestTimedOut, int32 /*опкод запроса*/);
	FOnAsdaCraftRequestTimedOut OnRequestTimedOut;
	// Значение `asda.Craft.FixLateResult` (К-1) — читает и окно 77500 (сброс до ответа).
	static bool IsLateResultFixEnabled();
	// Значение `asda.Craft.FixDeathRestoreOrder` (К-9, волна В8) — читает блок смерти диалога NPC: окна ремесла до 5045.
	static bool IsDeathRestoreOrderFixEnabled();

	// ═══ ДИАГНОСТИКА И САМОТЕСТ ═══
	// Текст состояния модели (уровень/опыт/лимит/изученные/замок) для `asda.Craft.State` и самотестов.
	FString DumpState() const;
	// ★ Волна В8 (финальная ревизия В7, С-3): самотесты и отладочные вбросы ремесла — ТОЛЬКО ОФЛАЙН, как команда заточки
	//   `asda.Upgrade.PanelShots` (AsdaItemUpgradeSubsystem.cpp, волна 14 МЕЛК-4). При живом соединении с сервером
	//   команда отказывается (true — отказ, строка Warning в лог); иначе взводит защиту отправителей на `GuardSeconds`.
	//   ★ С-Р1 (26.09.2026): проверку соединения делает общая точка порта `FAsdaOutgoingGate::RefuseDebugWhenOnline`
	//   (`AsdaOutgoingGate.h`); эта обёртка добавляет только окно защиты отправителей ремесла. Новые самотесты вне
	//   ремесла зовут общую точку напрямую.
	static bool RefuseDebugWhenOnline(UWorld* W, const TCHAR* Cmd, float GuardSeconds);
	// Второй барьер С-3: пока идёт самотест (окно защиты не истекло), настоящий 6598/6600/6602/6604 в ЖИВОЕ соединение
	//   не уходит — даже если соединение появилось посреди самотеста (перехват при соединении не ставится).
	bool IsSelfTestSendGuardActive() const;
	// Положить тело в очередь приёма — ТЕМ ЖЕ путём, что сеть (закон `diagnostic-must-use-real-path`).
	void DebugInject(int32 Opcode, const TArray<uint8>& Body) { Pending.Push(FPendingPacket{ Opcode, Body }); }
	// Синтез тела 6597 (593 б) из списка id / лимита / уровня / опыта / процента.
	static TArray<uint8> DebugBuild6597(const TArray<int32>& Learned, int32 Limit, int32 Level, int32 Exp, int32 Percent);
	// Узкий перехват ОДНОГО исходящего опкода ремесла (6598/6600/6602/6604): БОЕВАЯ отправка до `SendSecured`,
	//   тело кладётся в `DebugCaptured`, перехватчик снимается сам после первого пакета (урок волны 14).
	//   false — не поставлен (живое соединение / перехватчик занят).
	bool DebugCaptureOnce(int32 Opcode);
	// Снять перехватчик, если он ещё стоит (самотест в конце шага).
	void DebugDisarmCapture(const TCHAR* Why);
	bool DebugIsCaptureArmed() const { return bCaptureArmed; }
	// Пойманные тела (payload без конверта) по порядку.
	TArray<TPair<int32, TArray<uint8>>> DebugCaptured;
	// Журнал нотисов «loc → канал» (самотест сверяет каналы по обработчикам).
	const TArray<FString>& GetNoticeJournal() const { return NoticeJournal; }
	void ClearNoticeJournal() { NoticeJournal.Reset(); }

	// ═══ НОТИСЫ (общие для окон ремесла) ═══
	// ONEBUT 1500 (`CItem_BuildTooltipText(…, 0, −1|24, …)`); звуки 25 / 4 + 5 — у общей точки (см. шапку).
	//   EtalonNoticeId — id эталонного вызова для очереди окон-сообщений: −1, а у 6601 (коды 10/11/99) — 24 (network.c:49609-49617).
	void NoticeMsgBox(int32 LocId, int32 EtalonNoticeId = -1);
	// TWOBUT 1520 (`CItem_BuildTooltipText(…, 1, 84|85, …)`); звуки — у общей точки; `OnYes` — только «Да».
	//   EtalonNoticeId — 84 (изучить, item_inventory.c:37838) или 85 (повысить уровень, :37848) — ключ очереди.
	void NoticeConfirm(int32 LocId, TFunction<void()> OnYes, int32 EtalonNoticeId);
	void NoticeChatAndPlank(int32 LocId);               // чат −11514 (0xFFFFD306) + плашка WARMESSAGE (221)
	void NoticeGateChat(int32 LocId);                   // чат −6881324 (0xFF96FFD4) — гейт открытия
	// «Да» в 5571/5582, а 6598/6604 не ушёл (замок/шлюз): лог + журнал, нотиса нет — у эталона случая нет (МЕЛК-1 В7, W7-8).
	void NoteConfirmedButUnsent(int32 Opcode);

private:
	// Пакет ремесла, принятый сетевым колбэком и ждущий разбора в `Tick` (закон `no-reentrant-net-callbacks`).
	struct FPendingPacket
	{
		int32 Opcode = 0;          // 6597/6599/6601/6603/6605
		TArray<uint8> Body;        // тело без заголовка кадра
	};

	bool EnsureTables() const;             // загрузить `DT_making_recipe`/`DT_making_rate` и индексы по id (один раз)
	void DrainPending();                   // разобрать очередь `Pending` в порядке прихода (из `Tick`)
	void On6597(const TArray<uint8>& B);   // `shopParseStockPacket@0xFDEEB0`: список изученных, лимит, уровень/опыт/процент
	void On6599(const TArray<uint8>& B);   // ответ на 6598 «изучить рецепт свитком»
	void On6601(const TArray<uint8>& B);   // ответ на 6600 «удалить рецепт»
	void On6603(const TArray<uint8>& B);   // `upgradeDlg_onResultPacket@0xFE65E0`: итог изготовления (коды 1/2 и отказы)
	void On6605(const TArray<uint8>& B);   // `netOnItemUseResult@0xFE0800`: итог свитка уровня ремесла
	void LearnLocal(int32 RecipeId);     // `craftPanelSelectItemSlot@0xFE0BE0`: флаг 1, ++счётчик, пересборка
	void ForgetLocal(int32 RecipeId);    // `craftRemoveSelectedItemSlot@0xFE0C60`: флаг 0, --счётчик, пересборка
	void RebuildLearnedList();           // `sub_FDF000` (он же `sub_93D0B8`): [803..] заново по флагам
	void ApplyRecord(const uint8* Rec57, const TCHAR* Src);   // запись ответа → `ApplyExternalRecord(CountOrClear)`
	void ResetModel(const TCHAR* Why);
	void CloseMainWindow(const TCHAR* Why);   // `uiWindowHideAndCleanup(g_pItemMainWnd)` из приёмников
	void ResetMakeWindow(const TCHAR* Why);   // `itemQtyDlgReset(g_pItemQtyMakeDlg)` из приёмника 6603 (окно 77500)
	void JournalNotice(const FString& Line);
	bool BeginRequest(int32 Opcode);          // замок: можно ли слать (и не занят ли другим запросом)
	void MarkSent(int32 Opcode, bool bSent);  // пакет ушёл — взвести замок
	void ReleaseInFlight(const TCHAR* Why);   // снять замок (ответ / сброс / таймаут)

	// ── Таблицы (сильная ссылка — DT не выгружается, пока жива подсистема) ──
	UPROPERTY(Transient) mutable TObjectPtr<UDataTable> RecipeTable = nullptr;   // `DT_making_recipe` (making_recipe.bin)
	UPROPERTY(Transient) mutable TObjectPtr<UDataTable> RateTable = nullptr;     // `DT_making_rate` (making_rate.bin, 249 строк)
	mutable TMap<int32, const FAsdaMakingRecipeRow*> RecipeById;                 // id рецепта (+0 i16) → строка
	mutable TMap<int32, const FAsdaMakingRateRow*> RateById;                     // MakeIndex (+0 u16) → строка исходов
	mutable bool bTablesTried = false;                                            // загрузка уже пробовалась (не повторять)

	// ── Модель (поля `CItemMainWnd`) ──
	int32 CraftLevel = 0;        // [221]
	int32 CraftExp = 0;          // [220]
	int32 CraftPercent = 0;      // [219]
	int32 RecipeLimit = 0;       // [218]
	int32 LimitBonus = 0;        // К-8: 6597 @2 − формула клиента на момент 6597
	int32 LearnedCount = 0;      // [222]
	TArray<uint8> LearnedFlags;  // [223..802]
	TArray<int32> LearnedList;   // [803..]
	bool bListReceived = false;  // 6597 уже приходил в этой сессии

	FAsdaCraftEnterResult LastEnter;       // итог последнего 6603 (читает окно 77500)
	bool bEnterResultClaimed = false;      // окно 77500 забрало продукт (кладёт его в конце анимации)

	// ── Замок «операция ремесла в полёте» (пара серверного замка P35) ──
	//   ★ S2 (В2-A дедупа №2, 11.10.2026): общая точка `FAsdaReplyLatch` вместо `bInFlight + InFlightOpcode + InFlightSince`; опкод запроса
	//   (6598/6600/6602/6604) — `ReplyLatch.Opcode`, момент отправки — `ReplyLatch.ArmedAt`.
	FAsdaReplyLatch ReplyLatch;

	TAsdaNetQueue<FPendingPacket> Pending;   // закон `no-reentrant-net-callbacks`
	FDelegateHandle HCraftPacket;            // подписка на пакеты ремесла сетевого слоя (снимается в Deinitialize)
	TArray<FString> NoticeJournal;           // журнал нотисов «loc → канал» (самотест)

	// Перехват самотеста.
	bool bCaptureArmed = false;              // узкий перехват исходящего стоит
	uint32 CaptureGen = 0;                   // поколение перехватчика `ArmSendCapture` (снимается только свой)
	int32 CaptureOpcode = 0;                 // перехватываемый опкод (6598/6600/6602/6604)
};
