// Copyright (c) 2026 @sitxovski (Discord)
// Реализация модели и протокола ремесла (создание снаряжения). Разбор эталона, раскладки и коды —
//   в шапке `AsdaCraftSubsystem.h`, в отчёте `Reports/craft/craft_re_report.md` и ревизии
//   `Reports/craft/craft_re_REVIEW.md`. Отчёт захода — `Reports/craft/wave0-2.md`.

#include "UI/AsdaCraftSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ В2-A дедупа №2: общая точка «это я?» (класс/уровень/charId/userNum)
#include "UI/AsdaCraftWindowSubsystem.h"   // закрытие окна 76000 из приёмников 6599/6601
#include "UI/AsdaNotice.h"                 // фасад нотисов (Loc / MsgBoxLoc / ChatColored)
#include "UI/AsdaMessageSubsystem.h"       // плашка WARMESSAGE (канал `WarWindow`)
#include "UI/AsdaInventorySubsystem.h"     // общая точка записей `ApplyExternalRecord` и `CountItemById`
#include "UI/AsdaWindowSubsystem.h"        // предикат «окно видно», окна-сообщения ONEBUT/TWOBUT
#include "UI/AsdaCraftMakeWindowSubsystem.h"  // окно изготовления 77500 (волна В4): сброс из приёмника 6603
#include "UI/AsdaCraftRecipeWindowSubsystem.h" // ключ К-7 (`IsStaleSnapshotFixEnabled`) — волна В8, С-2

#include "AsdaNetSubsystem.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд
#include "AsdaBytes.h"
#include "Net/NetOpcodes.h"
#include "Items/AsdaItemDbRows.h"
#include "Items/AsdaItemTableSubsystem.h"

#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Text/AsdaLangRows.h"   // [ПОРТ] 09.10.2026 (мультиязычность): RecipeName — имя рецепта на языке сессии

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCraft, Log, All);

// ═════════════════════════════ КЛЮЧИ ОТКАТА ИСПРАВЛЕНИЙ ═════════════════════════════════════════
//   Каждое видимое игроку отступление от оригинала — с ключом; значение по умолчанию = «исправлено».

// ★ К-8. Сервер (и 6597 @2) считает лимит рецептов как `lvl*(10*craftLvl/5)/10 + (char)byte[CUser+0x4090D] + 20`
//   (GameServer.c:121034), а клиент при открытии окна, повышении уровня персонажа и в 6603 перетирает его
//   формулой БЕЗ надбавки (ui_core.c:100595-100597, 100613-100614; item_enhance.c:10383-10385) — показ «N / M»
//   меньше настоящего лимита. Исправление: надбавка = 6597 @2 − формула на момент 6597, прибавляется при пересчётах.
static ASDA_DEV_CVAR(int32, CVarCraftFixLimitBonus,
	TEXT("asda.Craft.FixLimitBonus"), 1,
	TEXT("Дефект оригинала К-8: 1 = лимит рецептов «N / M» при пересчётах клиента учитывает серверную надбавку "
	     "(6597 @2 − формула клиента); 0 = как оригинал — формула без надбавки."),
	ECVF_Default);

// ★★★ [ОРИГИНАЛ] 08.11.2026 (пачка 15, баг 8 «уровень ремесла в окне персонажа не показан»). Строка 942 `STATUS_ST12`
//   «Ур. ремесла» окна 900 печатает `(int16)g_nShopValueA` (`shopUiWnd_setValueText@0xB94C50`, shop_trade.c:1911).
//   Младшее слово этого глобала эталон пишет УРОВНЕМ РЕМЕСЛА в трёх местах:
//     6597 `shopParseStockPacket@0xFDEEB0` — `LOWORD(g_nShopValueA) = *((_WORD*)v7 + 442)` = `v7[221]` (shop_trade.c:22251);
//     6603 и 6605 — через `sub_931538` → `sub_FE0DA0@0xFE0DA0` (`this[221] = a2; LOWORD(g_nShopValueA) = …`, ui_core.c:130174).
//   Порт переносил только 5036 (весь dword, `tagRefineShop_RecvPacket`), а 6597 приходит после КАЖДОГО входа — без него
//   строка оставалась «0» у любого персонажа. Окно эталон на эти пакеты НЕ перерисовывает: число видно при следующем
//   открытии окна или стат-пакете (тот же порядок у нас — `RefreshDataPage` читает зеркало).
static ASDA_DEV_CVAR(int32, CVarCraftLevelToCharInfo,
	TEXT("asda.Craft.LevelToCharInfo"), 1,
	TEXT("1 = как оригинал: 6597/6603/6605 пишут уровень ремесла в младшее слово очков ремесла (строка «Ур. ремесла» окна персонажа); "
	     "0 = прежний порт — строку пишет только 5036."),
	ECVF_Default);

// Младшее слово зеркала `g_nShopValueA` (`UAsdaNetSubsystem::SelfCraftSkill`) ← уровень ремесла (`sub_FE0DA0`).
//   −1 («ещё не приходило») = старшее слово 0. Why — источник для журнала.
static void AsdaCraftMirrorLevelToShopValueA(UGameInstance* GI, int32 Level, const TCHAR* Why)
{
	if (CVarCraftLevelToCharInfo.GetValueOnGameThread() == 0) { return; }
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	const uint32 Hi = (Net->SelfCraftSkill < 0) ? 0u : ((uint32)Net->SelfCraftSkill & 0xFFFF0000u);
	Net->SelfCraftSkill = (int32)(Hi | (uint32)(uint16)(int16)Level);
	UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] %s: уровень %d → младшее слово очков ремесла (окно персонажа), зеркало 0x%08X"),
		Why, Level, (uint32)Net->SelfCraftSkill);
}

// ★ К-5. `upgradeDlg_onResultPacket` знает коды 0,1,2,3,4,5,6,99; код 10 (материал пуст/не тот/мало в ЭТОМ стеке,
//   уровень ниже ступени 0 — GameServer.c гейты 4–7) уходит в `default: return` без сброса: окно 77500 висит
//   с выключенными кнопками (item_enhance.c:10489).
static ASDA_DEV_CVAR(int32, CVarCraftFixUnknownCode,
	TEXT("asda.Craft.FixUnknownCode"), 1,
	TEXT("Дефект оригинала К-5: 1 = ответ 6603 с кодом 10 — ONEBUT 5566 «Недостаточно материалов» и сброс окна "
	     "изготовления, с любым другим неизвестным кодом — окну возвращаются кнопки; 0 = как оригинал — такие коды молча игнорируются."),
	ECVF_Default);

// ★ К-1. Если окно изготовления сброшено ДО ответа (кнопка 77534 до анимации, `uiCloseAllWindows`, смерть),
//   эталон всё равно запускает анимацию на обнулённой записи rate и падает (AV), а продукт теряется
//   (`itemQtyDlgReset` уже прошёл, item_inventory.c:69560). Исправление: пришедший «без живой анимации» ответ
//   кладёт продукт в сумку сразу и анимацию не запускает. ★ Волна В4: окно 77500 есть — «живая анимация» = окно
//   открыто и ждёт ответа на свой 6602; ответ в сброшенное/закрытое окно окно не забирает, и продукт кладёт модель.
//   Тот же ключ (0) возвращает эталонный безусловный хвост сброса 77500 (`uiExpandPartyPanel` + восстановление окон
//   даже при уже закрытом окне — эталон зовёт `itemQtyDlgReset` из приёмника по каждому коду отказа).
static ASDA_DEV_CVAR(int32, CVarCraftFixLateResult,
	TEXT("asda.Craft.FixLateResult"), 1,
	TEXT("Дефект оригинала К-1: 1 = ответ 6603 в сброшенное/закрытое окно 77500 кладёт продукт в сумку сразу (без "
	     "анимации), а сброс уже закрытого окна не трогает HUD и снимок окон; 0 = как оригинал — продукт кладёт только "
	     "конец анимации (ответ после сброса продукт теряет; в оригинале клиент ещё и падал), сброс идёт всегда."),
	ECVF_Default);

// ★ Замок «одна операция ремесла в полёте» — клиентская пара серверного замка P35 (С-3/С-18,
//   `Reports/craft/wave6_server.md`): у оригинала замка нет (повторные 6602 до ответа БД = N попыток за один
//   набор материалов, повторные 6600 — обход лимита рецептов). С P35 дубль сервер отбрасывает БЕЗ ОТВЕТА ⇒ клиент
//   не шлёт следующий запрос ремесла до ответа на предыдущий, а ожидание снимает сам (ответ/сброс/таймаут).
static ASDA_DEV_CVAR(int32, CVarCraftSingleInFlight,
	TEXT("asda.Craft.SingleInFlight"), 1,
	TEXT("Дефект оригинала С-3/С-18 (клиентская часть): 1 = пока запрос ремесла (6598/6600/6602/6604) без ответа, "
	     "следующий не уходит (снимают ответ, сброс сессии и таймаут); 0 = как оригинал — замка нет."),
	ECVF_Default);

// ★ Таймаут замка. Серверный замок P35 держится `LockTimeoutMs` = 15 000 мс — клиент обязан ждать ДОЛЬШЕ
//   (прецедент заточки, ревизия R-5: 15 с сервера + запас 5 с), иначе повтор уйдёт в ещё живой серверный замок.
static ASDA_DEV_CVAR(float, CVarCraftReplyTimeoutSec,
	TEXT("asda.Craft.ReplyTimeoutSec"), 20.f,
	TEXT("Секунд ожидания ответа на запрос ремесла, после которых замок «операция в полёте» снимается. "
	     "0 = без таймаута (снимают только ответ и сброс сессии)."),
	ECVF_Default);

// ★ К-6 (волна В3). Сервер списывает материал ТОЛЬКО из указанного стека (GameServer.c:121471-121473, гейт 7;
//   контейнеры 1/2 — P35 С-1/С-2), а клиент считает СУММУ стеков (`CInventory_CountItemById@0x9998E0`) и шлёт слот
//   ПОСЛЕДНЕГО стека (`itemTableFindRecordById@0x999C80`, цикл без `break`) ⇒ при двух неполных стеках окно показывает
//   «хватает», кнопка пропускает, а сервер отвечает кодом 10 (с P35 — 3 «Недостаточно материалов»).
static ASDA_DEV_CVAR(int32, CVarCraftFixStackPick,
	TEXT("asda.Craft.FixStackPick"), 1,
	TEXT("Дефект оригинала К-6: 1 = «есть» материала в окнах ремесла и проверка «хватает» — по НАИБОЛЬШЕМУ стеку, "
	     "а в 6602 уходит первый стек, где хватает (как считает сервер); 0 = как оригинал — сумма всех стеков и "
	     "слот последнего стека."),
	ECVF_Default);

// ★ К-9 (найден волной В8 самотестом МЕЛК-4, доказан эталоном). Блок смерти `CCharacter_SetAction` (entity_char.c:
//   16898-16911) зовёт 5045 `UI_resetAllWindowsAndLeave_5045` (16900) РАНЬШЕ, чем закрывает окна ремесла (16908-16911).
//   5045 восстанавливает снимок окон (`uiCloseAllToggleWindows`, ui_core.c:61245 → `uiDispatchWindowToggle` слот 0 →
//   `uiOpenInventoryWindow`), а открытие сумки при ВИДИМОМ 76000/77500 молча отказывает (ui_core.c:46608) — и флаги
//   снимка тут же обнуляются (`memset`, ui_core.c:60967). Итог: умер с открытым окном изготовления — сумка (и прочие
//   окна снимка) после смерти не возвращается, а сброс 77500 (16910) находит снимок уже пустым.
//   ★ W8-2 (26.09): то же при открытом окне состава 77000 без перехода в 77500 — у эталона 77000 открыто только при видимом
//   76000, и гейт «ремесло открыто» есть у открывателей почти всех окон снимка (сумка :46608, персонаж :74270, квест-гид
//   :33571, навыки skill_buff.c:32749, сообщество social.c:3787) — теряются все они. При 1 закрытие 76000/77000 до 5045
//   ещё и удерживает снимок 77000 от К-7 (`CloseAfterSelfDeath(true)`), и окна снимка возвращает 5045.
static ASDA_DEV_CVAR(int32, CVarCraftFixDeathRestoreOrder,
	TEXT("asda.Craft.FixDeathRestoreOrder"), 1,
	TEXT("Дефект оригинала К-9: 1 = при смерти окна ремесла (76000 с окном состава 77000, затем 77500 — их порядок эталона) "
	     "закрываются ДО 5045, снимок окна 77000 не снимается и окна снимка (сумка и др.) возвращаются — и при открытом 77500, "
	     "и при открытом 77000 (W8-2); 0 = как оригинал — 5045 первым, открытие окон снимка отказывает из-за видимого окна "
	     "ремесла, снимок теряется."),
	ECVF_Default);

namespace
{
	// Цвета эталона (int32 из `CChat_AddTokenizedLines` / `CControl_SetTooltipText`).
	const FColor kColCraftChat(0xFF, 0xD3, 0x06, 0xFF);   // −11514   = 0xFFFFD306 — 5572/5583 (и 5577/5580 в В4)
	const FColor kColGateChat (0x96, 0xFF, 0xD4, 0xFF);   // −6881324 = 0xFF96FFD4 — гейт 5590/5591

	// ★ Волна В5 (решение координатора по W4-1): звуки окон-сообщений 25/4/5 играют ОБЩИЕ точки `ShowGameMessage` /
	//   `ShowGameConfirm` / `CloseWindow` оконной подсистемы — у ремесла своих больше нет (иначе звук был бы двойным).
}

// ═════════════════════════════ ПОЛЯ ЗАПИСЕЙ ПО ИНДЕКСУ (одна точка) ════════════════════════════

int32 UAsdaCraftSubsystem::RecipeMatCode(const FAsdaMakingRecipeRow& R, int32 I)
{
	switch (I) { case 0: return R.Mat0; case 1: return R.Mat1; case 2: return R.Mat2; case 3: return R.Mat3; case 4: return R.Mat4; case 5: return R.Mat5; default: return -1; }
}

int32 UAsdaCraftSubsystem::RecipeMatQty(const FAsdaMakingRecipeRow& R, int32 I)
{
	switch (I) { case 0: return R.MatCount0; case 1: return R.MatCount1; case 2: return R.MatCount2; case 3: return R.MatCount3; case 4: return R.MatCount4; case 5: return R.MatCount5; default: return -1; }
}

int32 UAsdaCraftSubsystem::RecipeSubRecipe(const FAsdaMakingRecipeRow& R, int32 I)
{
	switch (I) { case 0: return R.SubRecipe0; case 1: return R.SubRecipe1; case 2: return R.SubRecipe2; case 3: return R.SubRecipe3; case 4: return R.SubRecipe4; case 5: return R.SubRecipe5; default: return -1; }
}

int32 UAsdaCraftSubsystem::RecipeReqLevel(const FAsdaMakingRecipeRow& R)
{
	return (int32)(int8)R.CraftLevel;   // `*(char *)(rec + 67)`
}

int32 UAsdaCraftSubsystem::RateOutItem(const FAsdaMakingRateRow& R, int32 K)
{
	switch (K) { case 0: return R.OutItem0; case 1: return R.OutItem1; case 2: return R.OutItem2; case 3: return R.OutItem3; case 4: return R.OutItem4; case 5: return R.OutItem5; case 6: return R.OutItem6; default: return -1; }
}

bool UAsdaCraftSubsystem::IsLateResultFixEnabled()
{
	return CVarCraftFixLateResult.GetValueOnGameThread() != 0;
}

bool UAsdaCraftSubsystem::IsDeathRestoreOrderFixEnabled()
{
	return CVarCraftFixDeathRestoreOrder.GetValueOnGameThread() != 0;
}

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaCraftSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	LearnedFlags.Init(0, kMaxRecipeId);
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь, разбор — в Tick.
		HCraftPacket = Net->OnCraftPacket.AddLambda([this](int32 Op, const TArray<uint8>& B)
		{
			Pending.Push(FPendingPacket{ Op, B });
		});
	}
	UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] подсистема модели поднята"));
}

void UAsdaCraftSubsystem::Deinitialize()
{
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnCraftPacket.Remove(HCraftPacket);
	}
	DebugDisarmCapture(TEXT("разборка подсистемы"));
	Super::Deinitialize();
}

// ★ Ревизия Р-1 (КРИТ): 6597 приходит от GameServer после входа и ЗАНОВО при портале. Поэтому модель — данные
//   персонажа — чистится на входе/выходе/разрыве и НЕ чистится на `MapChanged`: сброс смены карты идёт по
//   `PostLoadMapWithWorld` и мог бы прийти ПОСЛЕ нового 6597, стерев его (окна «счётчик = 0» быть не должно).
void UAsdaCraftSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Замок снимается при ЛЮБОМ сбросе: сервер P35 снимает свой на разрыве и на портале — ответа уже не будет.
	ReleaseInFlight(AsdaSessionResetReasonName(Reason));
	if (!AsdaIsFullSessionReset(Reason))
	{
		UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] сброс «%s»: модель СОХРАНЕНА (6597 и так придёт заново после портала, ревизия Р-1)"),
			AsdaSessionResetReasonName(Reason));
		return;
	}
	Pending.Clear();
	ResetModel(AsdaSessionResetReasonName(Reason));
	DebugDisarmCapture(TEXT("сброс сессии"));
}

void UAsdaCraftSubsystem::ResetModel(const TCHAR* Why)
{
	CraftLevel = CraftExp = CraftPercent = RecipeLimit = LimitBonus = LearnedCount = 0;
	LearnedFlags.Init(0, kMaxRecipeId);
	LearnedList.Reset();
	bListReceived = false;
	LastEnter = FAsdaCraftEnterResult();
	bEnterResultClaimed = false;
	UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] модель сброшена (%s)"), Why);
	OnModelChanged.Broadcast(0);
}

void UAsdaCraftSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.Craft");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	DrainPending();
	// ★ Таймаут замка «операция в полёте» — работает и при закрытых окнах.
	if (ReplyLatch.IsExpired(CVarCraftReplyTimeoutSec.GetValueOnGameThread()))   // ★ S2 (В2-A дедупа №2, 11.10.2026): та же формула
	{
		ReleaseInFlight(TEXT("таймаут ответа"));
	}
}

void UAsdaCraftSubsystem::DrainPending()
{
	// ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива.
	TArray<FPendingPacket> Now;
	if (!Pending.Drain(Now)) { return; }
	for (const FPendingPacket& P : Now)
	{
		// Любой ответ ремесла (любой код) снимает замок своего запроса: 6599↔6598, 6601↔6600, 6603↔6602, 6605↔6604.
		if (ReplyLatch.IsArmed() && P.Opcode != 6597 && P.Opcode == ReplyLatch.Opcode + 1)
		{
			ReleaseInFlight(*FString::Printf(TEXT("ответ %d"), P.Opcode));
		}
		switch (P.Opcode)
		{
		case 6597: On6597(P.Body); break;
		case 6599: On6599(P.Body); break;
		case 6601: On6601(P.Body); break;
		case 6603: On6603(P.Body); break;
		case 6605: On6605(P.Body); break;
		default:
			UE_LOG(LogAsdaCraft, Warning, TEXT("[ремесло] в очереди чужой опкод %d — пропуск"), P.Opcode);
			break;
		}
	}
}

// ═════════════════════════════ ТАБЛИЦЫ ════════════════════════════════════════════════════════

bool UAsdaCraftSubsystem::EnsureTables() const
{
	if (bTablesTried && RecipeTable && RateTable) { return true; }
	bTablesTried = true;
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return false; }
	if (!RecipeTable)
	{
		RecipeTable = IT->GetDataTable(FName(TEXT("making_recipe")));
		RecipeById.Reset();
		if (RecipeTable)
		{
			TArray<FAsdaMakingRecipeRow*> Rows;
			RecipeTable->GetAllRows<FAsdaMakingRecipeRow>(TEXT("Craft"), Rows);
			// `findRecordById16`: первое совпадение по `*(i16*)rec` — у нас первая запись с этим Id.
			for (const FAsdaMakingRecipeRow* R : Rows) { if (R && !RecipeById.Contains(R->Id)) { RecipeById.Add(R->Id, R); } }
		}
	}
	if (!RateTable)
	{
		RateTable = IT->GetDataTable(FName(TEXT("making_rate")));
		RateById.Reset();
		if (RateTable)
		{
			TArray<FAsdaMakingRateRow*> Rows;
			RateTable->GetAllRows<FAsdaMakingRateRow>(TEXT("Craft"), Rows);
			// `shopFindRecordByItemId@0xFDEE30`: цикл по записям, первое совпадение `*(u16*)rate == MakeIndex`.
			for (const FAsdaMakingRateRow* R : Rows) { if (R && !RateById.Contains(R->MakeId)) { RateById.Add(R->MakeId, R); } }
		}
	}
	UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] таблицы: making_recipe %s (%d), making_rate %s (%d)"),
		RecipeTable ? TEXT("есть") : TEXT("НЕТ"), RecipeById.Num(), RateTable ? TEXT("есть") : TEXT("НЕТ"), RateById.Num());
	return RecipeTable && RateTable;
}

const FAsdaMakingRecipeRow* UAsdaCraftSubsystem::FindRecipe(int32 RecipeId) const
{
	EnsureTables();
	const FAsdaMakingRecipeRow* const* R = RecipeById.Find(RecipeId);
	return R ? *R : nullptr;
}

// ★ [ПОРТ] 09.10.2026 (мультиязычность): указатель `R` — строка самой таблицы (`GetAllRows`), имя строки ищет обратный индекс.
FString UAsdaCraftSubsystem::RecipeName(const FAsdaMakingRecipeRow* R) const
{
	if (!R) { return FString(); }
	return AsdaLangRows::Text(RecipeTable.Get(), R, TEXT("RecipeName"), R->RecipeName);
}

const FAsdaMakingRateRow* UAsdaCraftSubsystem::FindRate(int32 MakeIndex) const
{
	EnsureTables();
	const FAsdaMakingRateRow* const* R = RateById.Find(MakeIndex);
	return R ? *R : nullptr;
}

int32 UAsdaCraftSubsystem::GetRecipeCount() const { EnsureTables(); return RecipeById.Num(); }
int32 UAsdaCraftSubsystem::GetRateCount() const { EnsureTables(); return RateById.Num(); }

// ═════════════════════════════ МОДЕЛЬ ═════════════════════════════════════════════════════════

bool UAsdaCraftSubsystem::IsAnyCraftWindowOpen(const UGameInstance* GI)
{
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	return WM->IsWindowVisible(FName(kMainWindowId)) || WM->IsWindowVisible(FName(kMakeWindowId));
}

bool UAsdaCraftSubsystem::IsLearned(int32 RecipeId) const
{
	// `arrayContainsValue@0xFE0FD0`: поиск id в списке [803..] на длину [222].
	for (int32 I = 0; I < LearnedCount && I < LearnedList.Num(); ++I) { if (LearnedList[I] == RecipeId) { return true; } }
	return false;
}

int32 UAsdaCraftSubsystem::ClientLimitFormula() const
{
	// `uiCraftComputeSuccessRate@0xFDD510` / `tileInteractGateAndOpen` (ui_core.c:100595-100597):
	//   v8 = 10 * craftLvl / 5; [218] = charLevel * v8 / 10 + 20 — целочисленно, как в эталоне.
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const int32 CharLevel = AsdaSelf::GetLevel(Net);   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U5c): уровень — общая точка `AsdaSelf::GetLevel` (до 4005/4036 — уровень выбранного персонажа, а не 0)
	const int32 V8 = 10 * CraftLevel / 5;
	return CharLevel * V8 / 10 + 20;
}

void UAsdaCraftSubsystem::RecalcLimit(const TCHAR* Why)
{
	const int32 Base = ClientLimitFormula();
	const bool bFix = CVarCraftFixLimitBonus.GetValueOnGameThread() != 0;
	RecipeLimit = Base + (bFix ? LimitBonus : 0);
	UE_LOG(LogAsdaCraft, Verbose, TEXT("[ремесло] лимит пересчитан (%s): формула %d + надбавка %d%s = %d"),
		Why, Base, LimitBonus, bFix ? TEXT("") : TEXT(" (К-8 выкл — не учтена)"), RecipeLimit);
}

bool UAsdaCraftSubsystem::CheckRequiredItems(const FAsdaMakingRecipeRow& R) const
{
	// `craftCheckRequiredItems@0xFE0CF0` (item_enhance.c:10045-10054): для всех ШЕСТИ слотов
	//   `CInventory_CountItemById(код) < количество` ⇒ 0. Пустой слот (код −1, количество −1) проходит сам.
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return false; }
	for (int32 I = 0; I < 6; ++I)
	{
		// ★ К-6: `UsableMaterialCount` = эталонная сумма при `asda.Craft.FixStackPick 0`, наибольший стек при 1.
		if (UsableMaterialCount(RecipeMatCode(R, I)) < RecipeMatQty(R, I)) { return false; }
	}
	return true;
}

int32 UAsdaCraftSubsystem::CountMaterial(int32 ItemCode) const
{
	// Эталон `CInventory_CountItemById@0x9998E0` — общая точка сумки (второй копии не заводим).
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	return Inv ? Inv->CountItemById(ItemCode) : 0;
}

// ★ Ревизия В3 МЕЛК-11: стеки обходит ОБЩАЯ точка сумки `UAsdaInventorySubsystem::ForEachItemStack` (тот же выбор
//   контейнера 1/2, 30/60 ячеек и «стек = количество», что у `CInventory_CountItemById`) — копии обхода здесь больше нет.
int32 UAsdaCraftSubsystem::MaxMaterialStack(int32 ItemCode) const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv || ItemCode < 0) { return 0; }
	int32 Best = 0;
	Inv->ForEachItemStack(ItemCode, [&Best](int32, int32, int32 Units) { Best = FMath::Max(Best, Units); });
	return Best;
}

int32 UAsdaCraftSubsystem::CraftableFromStacks(int32 ItemCode, int32 Need) const
{
	if (Need <= 0) { return MAX_int32; }
	if (CVarCraftFixStackPick.GetValueOnGameThread() == 0) { return CountMaterial(ItemCode) / Need; }   // эталон: сумма / Need
	const UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv || ItemCode < 0) { return 0; }
	int32 Total = 0;
	Inv->ForEachItemStack(ItemCode, [&Total, Need](int32, int32, int32 Units) { Total += Units / Need; });
	return Total;
}

int32 UAsdaCraftSubsystem::UsableMaterialCount(int32 ItemCode) const
{
	return CVarCraftFixStackPick.GetValueOnGameThread() != 0 ? MaxMaterialStack(ItemCode) : CountMaterial(ItemCode);
}

bool UAsdaCraftSubsystem::PickMaterialStack(int32 ItemCode, int32 Need, int32& OutSlot, uint8& OutContainer) const
{
	OutSlot = -1;
	OutContainer = 0;
	const UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv || ItemCode < 0) { return false; }
	const bool bFix = CVarCraftFixStackPick.GetValueOnGameThread() != 0;
	bool bPicked = false;
	int32 Slot = -1;
	uint8 Cont = 0;
	// ★ Волна В5 (ревизия В4 МЕЛК-5): слот и контейнер — из САМОЙ записи предмета (+5 i16 слот, +4 u8 контейнер; asm
	//   0xFE5F0A/0xFE5F16), как у эталона и у свитков (`TryUseCraftScroll`); номер ячейки и класс — только запас на
	//   пустую запись.
	auto FromRecord = [Inv](int32 C, int32 Cell, int32& OutS, uint8& OutC)
	{
		const FAsdaItemInstance* I = Inv->GetCell(C, Cell);
		OutS = (I && I->SlotIndex >= 0) ? I->SlotIndex : Cell;
		OutC = (I && I->ContainerType != 0) ? I->ContainerType : (uint8)C;
	};
	Inv->ForEachItemStack(ItemCode, [&](int32 C, int32 Cell, int32 Units)
	{
		if (bFix)
		{
			// ★ К-6: первый стек, из которого сервер реально спишет `Need`.
			if (!bPicked && Units >= Need) { FromRecord(C, Cell, Slot, Cont); bPicked = true; }
			return;
		}
		// Как эталон: цикл без `break` — побеждает ПОСЛЕДНИЙ стек с этим кодом.
		FromRecord(C, Cell, Slot, Cont);
	});
	OutSlot = Slot;
	OutContainer = Cont;
	return OutSlot >= 0;
}

void UAsdaCraftSubsystem::RebuildLearnedList()
{
	// `sub_FDF000` (он же `sub_93D0B8`, разрешён по `RE/ida/thunk_map.tsv`): [803..] заново по флагам 0..579.
	LearnedList.Reset();
	for (int32 I = 0; I < kMaxRecipeId && I < LearnedFlags.Num(); ++I)
	{
		if (LearnedFlags[I] == 1) { LearnedList.Add(I); }
	}
}

void UAsdaCraftSubsystem::LearnLocal(int32 RecipeId)
{
	// `craftPanelSelectItemSlot@0xFE0BE0` (item_enhance.c:10022-10029): флаг = 1, ++[222], пересборка страницы
	//   и сводки окна, затем список [803..] по флагам. ⚠ Эталон не проверяет id — порт держит границу массива.
	if (RecipeId < 0 || RecipeId >= kMaxRecipeId)
	{
		UE_LOG(LogAsdaCraft, Warning, TEXT("[ремесло] изучение: id %d вне 0..%d — пропуск"), RecipeId, kMaxRecipeId - 1);
		return;
	}
	LearnedFlags[RecipeId] = 1;
	++LearnedCount;
	RebuildLearnedList();
}

void UAsdaCraftSubsystem::ForgetLocal(int32 RecipeId)
{
	// `craftRemoveSelectedItemSlot@0xFE0C60` (item_enhance.c:10033-10041): флаг = 0, --[222], пересборка,
	//   сброс окна состава 77000 (`itemTooltipWndReset`), список [803..] по флагам.
	if (RecipeId < 0 || RecipeId >= kMaxRecipeId)
	{
		UE_LOG(LogAsdaCraft, Warning, TEXT("[ремесло] удаление: id %d вне 0..%d — пропуск"), RecipeId, kMaxRecipeId - 1);
		return;
	}
	LearnedFlags[RecipeId] = 0;
	--LearnedCount;
	RebuildLearnedList();
}

void UAsdaCraftSubsystem::ApplyRecord(const uint8* Rec57, const TCHAR* Src)
{
	UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv || !Rec57) { return; }
	TArray<uint8> R; R.Append(Rec57, 57);
	const bool bOk = Inv->ApplyExternalRecord(R, UAsdaInventorySubsystem::EExternalRecordMode::CountOrClear);
	UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] %s: запись код=%d к=%d сл=%d кол-во=%d → %s"), Src,
		AsdaBytes::RdI32(Rec57), (int32)Rec57[4], AsdaBytes::RdI16(Rec57 + 5), AsdaBytes::RdI32(Rec57 + 9), bOk ? TEXT("применена") : TEXT("пропущена"));
}

void UAsdaCraftSubsystem::ResetMakeWindow(const TCHAR* Why)
{
	// `itemQtyDlgReset(g_pItemQtyMakeDlg)` — эталон зовёт его из приёмника 6603 при каждом коде отказа (item_enhance.c:10377-10487).
	if (UAsdaCraftMakeWindowSubsystem* W = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftMakeWindowSubsystem>() : nullptr)
	{
		W->ResetDialog(Why);
	}
}

void UAsdaCraftSubsystem::CloseMainWindow(const TCHAR* Why)
{
	// `uiWindowHideAndCleanup(g_pItemMainWnd)` — эталон зовёт его ИЗ ПРИЁМНИКОВ 6599/6601 безусловно.
	if (UAsdaCraftWindowSubsystem* W = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftWindowSubsystem>() : nullptr)
	{
		// ★ Волна В8 (ревизия В7 С-2): 76000 уже закрыто (поздний 6599/6601 после ухода в 77500 или после «Закрыть») —
		//   повторный `uiWindowHideAndCleanup` закрытому окну ничего видимого не даёт (выбор строки снят прошлым закрытием,
		//   страницу пересобирает открытие `OpenAfterGates`), у эталона сброс 77000 и флаги снимка не трогает, а у порта при
		//   ключе К-7 он снял бы флаги снимка, которые ещё нужны 77500. При `asda.Craft.FixStaleSnapshot 0` — как эталон.
		if (!W->IsOpen() && UAsdaCraftRecipeWindowSubsystem::IsStaleSnapshotFixEnabled())
		{
			UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] окно 76000 уже закрыто — повторный uiWindowHideAndCleanup не нужен (%s, С-2)"), Why);
			return;
		}
		W->HideAndCleanup();
		UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] окно 76000 скрыто и сброшено (%s)"), Why);
	}
}

// ═════════════════════════════ ПРИЁМНИКИ ══════════════════════════════════════════════════════

void UAsdaCraftSubsystem::On6597(const TArray<uint8>& B)
{
	// `shopParseStockPacket@0xFDEEB0` (shop_trade.c:22235-22259). ⚠ `Src` эталона — начало КАДРА (заголовок 9 б),
	//   у нас тело: @0 = Src+9.
	if (B.Num() < 593) { return; }
	CraftLevel   = (int32)(int8)B[584];                 // `v7[221] = Src[593]` (char)
	LearnedCount = AsdaBytes::RdI16(&B[0]);             // `v7[222] = *(i16*)(Src+9)`
	CraftExp     = AsdaBytes::RdI32(&B[585]);           // `v7[220] = *(i32*)(Src+594)`
	CraftPercent = AsdaBytes::RdI32(&B[589]);           // `v7[219] = *(i32*)(Src+598)`
	RecipeLimit  = AsdaBytes::RdI16(&B[2]);             // `v7[218] = *(i16*)(Src+11)`
	// `LOWORD(g_nShopValueA) = уровень` (shop_trade.c:22251) — ★ 08.11.2026 перенесено (пачка 15, баг 8; ключ asda.Craft.LevelToCharInfo).
	AsdaCraftMirrorLevelToShopValueA(GetGameInstance(), CraftLevel, TEXT("6597"));
	LearnedList.Reset();
	LearnedFlags.Init(0, kMaxRecipeId);
	for (int32 I = 0; I < kMaxRecipeId; ++I)
	{
		LearnedFlags[I] = B[4 + I];                     // `v7[i+223] = v5[i+4]`
		if (B[4 + I] == 1) { LearnedList.Add(I); }      // `if (v5[i+4] == 1) v7[v4++ + 803] = i`
	}
	// ★ К-8: запоминаем надбавку сервера — разницу его лимита и формулы клиента при ЭТОМ уровне.
	LimitBonus = RecipeLimit - ClientLimitFormula();
	bListReceived = true;
	UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] 6597: ур. %d, опыт %d (%d%%), изучено %d (по флагам %d) / лимит %d (формула клиента %d, надбавка %d)"),
		CraftLevel, CraftExp, CraftPercent, LearnedCount, LearnedList.Num(), RecipeLimit, ClientLimitFormula(), LimitBonus);
	OnModelChanged.Broadcast(6597);
}

void UAsdaCraftSubsystem::On6599(const TArray<uint8>& B)
{
	// `netHandleInventoryUpdatePacket@0xFDF080` (network.c:49512-49587).
	if (B.Num() < 64) { return; }
	const int32 Code = B[0];
	switch (Code)
	{
	case 1:
	{
		// Вес (@1 i32 > 0) уже поставил сетевой слой. Запись свитка @5: код >= 0 ⇒ count > 0 — количество, иначе удалить.
		if (AsdaBytes::RdI32(&B[5]) >= 0) { ApplyRecord(&B[5], TEXT("6599 свиток")); }
		const int32 RecipeId = AsdaBytes::RdI16(&B[62]);
		LearnLocal(RecipeId);                                   // `craftPanelSelectItemSlot(…, *(i16*)(p+62))`
		UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] 6599 код 1: рецепт %d изучен, изучено %d"), RecipeId, LearnedCount);
		NoticeChatAndPlank(AsdaCraftLoc::kLearned);             // 5572 чат −11514 + плашка
		OnModelChanged.Broadcast(6599);
		CloseMainWindow(TEXT("6599 код 1"));                    // `uiWindowHideAndCleanup(g_pItemMainWnd)`
		break;
	}
	case 10: NoticeMsgBox(AsdaCraftLoc::kDataError);   break;   // 5560
	case 11: NoticeMsgBox(AsdaCraftLoc::kAlreadyReg);  break;   // 5561
	case 12: NoticeMsgBox(AsdaCraftLoc::kRegLimit);    break;   // 5562
	case 13: NoticeMsgBox(AsdaCraftLoc::kSkillLow);    break;   // 5586
	case 99: NoticeMsgBox(AsdaCraftLoc::kUnknownData); break;   // 5563
	default:
		UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] 6599 код %d — эталон молча выходит (`default: return 1`)"), Code);
		break;
	}
}

void UAsdaCraftSubsystem::On6601(const TArray<uint8>& B)
{
	// `netOnSubcodeNoticeHandler@0xFDF3E0` (network.c:49591-49622). ONEBUT эталона — с командой 24
	//   (`CItem_BuildTooltipText(…, 0, 24, …)`): в диспетчере OK-кнопки `OnSendItemUpgrade` ветки 0x18 нет —
	//   поведение то же, что у команды −1 (просто закрыть).
	if (B.Num() < 3) { return; }
	const int32 Code = B[0];
	switch (Code)
	{
	case 1:
	{
		const int32 RecipeId = AsdaBytes::RdI16(&B[1]);
		ForgetLocal(RecipeId);                                 // `craftRemoveSelectedItemSlot(v7, Src[5])`
		UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] 6601 код 1: рецепт %d удалён, изучено %d"), RecipeId, LearnedCount);
		OnModelChanged.Broadcast(6601);
		CloseMainWindow(TEXT("6601 код 1"));
		break;
	}
	// Эталон зовёт эти три с id 24 (`CItem_BuildTooltipText(ui, 0, 24, …)`, network.c:49609-49617) — ключ очереди.
	case 10: NoticeMsgBox(AsdaCraftLoc::kDataError, 24);      break;   // 5560
	case 11: NoticeMsgBox(AsdaCraftLoc::kNotRegistered, 24);  break;   // 5564
	case 99: NoticeMsgBox(AsdaCraftLoc::kUnknownData, 24);    break;   // 5563
	default:
		UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] 6601 код %d — эталон молча выходит"), Code);
		break;
	}
}

void UAsdaCraftSubsystem::On6603(const TArray<uint8>& B)
{
	// `upgradeDlg_onResultPacket@0xFE65E0` (item_enhance.c:10335-10490). Модель применяет всё, что эталон применяет СРАЗУ,
	//   и хранит поля окна в `LastEnter`; анимацию ведёт окно 77500 (`UAsdaCraftMakeWindowSubsystem`, волна В4).
	if (B.Num() < 525) { return; }
	const int32 Code = B[0];
	// Первым делом эталон обнуляет полосу окна (`CProgressBar_SetValue(77531, 0)`, item_enhance.c:10366) — при коде 1/2
	//   это делает само окно, забирая результат; при отказе — сброс окна ниже (`ResetMakeWindow`).
	// ★ ОСОБЕННОСТЬ ЭТАЛОНА, сохранена 1:1 (ревизия В7 МЕЛК-5, пометка волны В8). Коды отказа сбрасывают окно 77500
	//   БЕЗУСЛОВНО (`itemQtyDlgReset(g_pItemQtyMakeDlg)`, item_enhance.c:10377-10487 — проверки «ждёт ли окно ответа» нет).
	//   Игрок нажал «Закрыть» до ответа на 6602, открыл 77500 ДРУГОГО рецепта — и поздний отказ старого запроса сбрасывает
	//   уже новое окно (с нотисом отказа старого запроса). При `asda.Craft.SingleInFlight 1` новое окно к этому моменту
	//   отправить ничего не могло (замок ещё держит старый 6602) — потерь нет, только лишнее закрытие. Ключа нет.
	switch (Code)
	{
	case 0:
	case 99: NoticeMsgBox(AsdaCraftLoc::kUnknownData);   ResetMakeWindow(TEXT("код 0/99")); return;   // 5563
	case 3:  NoticeMsgBox(AsdaCraftLoc::kLackMaterials); ResetMakeWindow(TEXT("код 3"));    return;   // 5566
	case 4:  NoticeMsgBox(AsdaCraftLoc::kLackWeight);    ResetMakeWindow(TEXT("код 4"));    return;   // 5567
	case 5:  NoticeMsgBox(AsdaCraftLoc::kNoBagSlot);     ResetMakeWindow(TEXT("код 5"));    return;   // 5568
	case 6:  NoticeMsgBox(AsdaCraftLoc::kNoRecipe);      ResetMakeWindow(TEXT("код 6"));    return;   // 5585
	case 10:
		// ★ К-5 (см. ключ): эталон `default: return 1` — окно висит с выключенными кнопками.
		if (CVarCraftFixUnknownCode.GetValueOnGameThread() != 0)
		{
			NoticeMsgBox(AsdaCraftLoc::kLackMaterials);
			ResetMakeWindow(TEXT("код 10 (исправление К-5)"));
		}
		else
		{
			UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] 6603 код 10 — как оригинал: молча (asda.Craft.FixUnknownCode 0)"));
		}
		return;
	case 1:
	case 2:
		break;   // ниже — общая ветка 1/2 (эталон дословно дублирует её)
	default:
		// ★ Волна В5 (ревизия В4 МЕЛК-3, закон `no-state-latch-before-shown`): эталон молча выходит, а замок запроса уже
		//   снят приёмом — таймаут не сработает, и окно 77500 висело бы с выключенными кнопками до «Закрыть». Честный
		//   сервер таких кодов не шлёт (сток и P35 сверены). Под тем же ключом К-5: 1 = кнопки назад, 0 = как оригинал.
		if (CVarCraftFixUnknownCode.GetValueOnGameThread() != 0)
		{
			if (UAsdaCraftMakeWindowSubsystem* W = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftMakeWindowSubsystem>() : nullptr)
			{
				W->ReturnButtons(*FString::Printf(TEXT("6603 неизвестный код %d (исправление К-5)"), Code));
			}
			UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] 6603 код %d — неизвестен: окну 77500 возвращены кнопки (К-5)"), Code);
		}
		else
		{
			UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] 6603 код %d — эталон молча выходит (asda.Craft.FixUnknownCode 0)"), Code);
		}
		return;
	}
	// ── Ветка 1/2 (item_enhance.c:10378-10420) ──
	CraftLevel   = (int32)(int8)B[3];                      // `sub_931538(g_pItemMainWnd, p[3])`
	AsdaCraftMirrorLevelToShopValueA(GetGameInstance(), CraftLevel, TEXT("6603"));   // `sub_FE0DA0`: LOWORD(g_nShopValueA) (баг 8 пачки 15)
	CraftExp     = AsdaBytes::RdI32(&B[4]);                // `sub_925CDD(…, *(i32*)(p+4))`
	CraftPercent = AsdaBytes::RdI16(&B[8]);                // `sub_92AB61(…, *(i16*)(p+8))`
	RecalcLimit(TEXT("6603"));                             // `[218] = lvl*(10*craftLvl/5)/10 + 20` (+ К-8)
	// Вес (@1 i16 > 0) уже поставил сетевой слой. Восемь записей @69: код > 0 ⇒ count > 0 — количество, иначе удалить.
	UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (Inv) { Inv->BeginCellBatch(); }
	for (int32 K = 0; K < 8; ++K)
	{
		const uint8* Rec = &B[69 + 57 * K];
		if (AsdaBytes::RdI32(Rec) > 0) { ApplyRecord(Rec, K < 6 ? TEXT("6603 материал") : TEXT("6603 катализатор")); }
	}
	if (Inv) { Inv->EndCellBatch(); }
	LastEnter = FAsdaCraftEnterResult();
	LastEnter.Code = Code;                                  // `filler[160] = p[0]`
	LastEnter.BonusFlag = B[11];                            // `filler[172] = p[11]`
	LastEnter.Product57.Append(&B[12], 57);                 // `qmemcpy(filler+188, p+12, 57)`
	LastEnter.ReachedTier = B[10];                          // `filler[168] = p[10]`
	bEnterResultClaimed = false;
	UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] 6603 код %d: ур. %d опыт %d (%d%%) лимит %d · ступень %d · +1 шт. %d · продукт код %d кол-во %d"),
		Code, CraftLevel, CraftExp, CraftPercent, RecipeLimit, LastEnter.ReachedTier, LastEnter.BonusFlag,
		AsdaBytes::RdI32(&B[12]), AsdaBytes::RdI32(&B[12 + 9]));
	OnEnterResult.Broadcast(LastEnter);
	// ★ К-1: анимации-«получателя» нет ⇒ продукт кладём сразу (как делает сброс окна `itemQtyDlgReset`:
	//   `if (filler[256] && запись+0 > 0 && [40] == 1) OnRecv_ItemUpdate(запись)`, item_inventory.c:69560-69563).
	if (!bEnterResultClaimed)
	{
		const int32 ProductCode = AsdaBytes::RdI32(&B[12]);
		if (CVarCraftFixLateResult.GetValueOnGameThread() != 0 && ProductCode > 0 && Code == 1 && Inv)
		{
			TArray<uint8> R; R.Append(&B[12], 57);
			LastEnter.bProductApplied = Inv->ApplyExternalRecord(R);   // `OnRecv_ItemUpdate` — запись целиком
			UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] 6603: продукт %d положен сразу — окно 77500 ответа не ждёт (сброшено/закрыто, К-1)"), ProductCode);
		}
		else if (ProductCode > 0)
		{
			UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] 6603: продукт %d НЕ положен — как оригинал его кладёт только конец анимации 77500 (asda.Craft.FixLateResult 0)"), ProductCode);
		}
	}
	OnModelChanged.Broadcast(6603);
}

void UAsdaCraftSubsystem::On6605(const TArray<uint8>& B)
{
	// `netOnItemUseResult@0xFE0800` (network.c:50016-50076).
	if (B.Num() < 61) { return; }
	const int32 Code = B[0];
	switch (Code)
	{
	case 1:
		CraftLevel = (int32)(int8)B[1];                     // `sub_931538(v15, v13[1])`
		AsdaCraftMirrorLevelToShopValueA(GetGameInstance(), CraftLevel, TEXT("6605"));   // `sub_FE0DA0`: LOWORD(g_nShopValueA) (баг 8 пачки 15)
		// Вес (@2 i16 > 0) уже поставил сетевой слой. Запись свитка @4: код > 0 ⇒ количество / удалить.
		if (AsdaBytes::RdI32(&B[4]) > 0) { ApplyRecord(&B[4], TEXT("6605 свиток")); }
		UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] 6605 код 1: уровень ремесла %d"), CraftLevel);
		NoticeChatAndPlank(AsdaCraftLoc::kLevelUp);          // 5583 чат −11514 + плашка
		OnModelChanged.Broadcast(6605);                     // `uiCraftPanelRefreshSummaryGated(v15)`
		break;
	case 2:  NoticeMsgBox(AsdaCraftLoc::kLevelNotMax); break;   // 5581
	case 99: NoticeMsgBox(AsdaCraftLoc::kUnknownData); break;   // 5563
	default:
		// Код 0 (не свиток уровня) эталон не разбирает — молча; окна нет, зависания нет (§5.5 отчёта).
		UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] 6605 код %d — эталон молча выходит"), Code);
		break;
	}
}

// ═════════════════════════════ ОТПРАВИТЕЛИ ════════════════════════════════════════════════════

bool UAsdaCraftSubsystem::BeginRequest(int32 Opcode)
{
	// ★ Волна В8 (С-3), второй барьер: идёт самотест, а соединение живое (появилось посреди прохода) — настоящий запрос
	//   ремесла не уходит. Перехват при соединении не ставится (`DebugCaptureOnce`), значит пакет ушёл бы на сервер.
	if (IsSelfTestSendGuardActive())
	{
		const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (Net && Net->IsConnected())
		{
			UE_LOG(LogAsdaCraft, Error, TEXT("[ремесло] запрос %d НЕ отправлен: идёт самотест ремесла, а соединение с сервером живое (С-3)"), Opcode);
			return false;
		}
	}
	if (!ReplyLatch.IsArmed() || CVarCraftSingleInFlight.GetValueOnGameThread() == 0) { return true; }
	UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] запрос %d НЕ отправлен: запрос %d ещё без ответа (%.1f с; asda.Craft.SingleInFlight 1)"),
		Opcode, ReplyLatch.Opcode, ReplyLatch.ElapsedSec());
	return false;
}

void UAsdaCraftSubsystem::MarkSent(int32 Opcode, bool bSent)
{
	if (!bSent) { return; }
	ReplyLatch.Arm(Opcode);   // ★ S2 (В2-A дедупа №2, 11.10.2026)
}

void UAsdaCraftSubsystem::ReleaseInFlight(const TCHAR* Why)
{
	if (!ReplyLatch.IsArmed()) { return; }
	UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] замок запроса %d снят: %s"), ReplyLatch.Opcode, Why);
	const int32 Op = ReplyLatch.Opcode;
	const bool bTimeout = FCString::Strcmp(Why, TEXT("таймаут ответа")) == 0;
	ReplyLatch.Disarm();
	ReplyLatch.Opcode = 0;   // как прежде: опкод снятого замка не хранится
	// ★ Волна В4: снятие ТАЙМАУТОМ — ответа не будет; окно 77500, ждущее 6603, вернёт себе кнопки.
	if (bTimeout) { OnRequestTimedOut.Broadcast(Op); }
}

bool UAsdaCraftSubsystem::SendLearnRecipe(int32 ScrollSlot, int32 ScrollContainer)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !BeginRequest(6598)) { return false; }
	const bool bOk = Net->SendCraftRecipeRegist6598(ScrollSlot, (uint8)ScrollContainer);
	MarkSent(6598, bOk);
	return bOk;
}

bool UAsdaCraftSubsystem::SendForgetRecipe(int32 RecipeId)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !BeginRequest(6600)) { return false; }
	const bool bOk = Net->SendCraftRecipeDelete6600(RecipeId);
	MarkSent(6600, bOk);
	return bOk;
}

bool UAsdaCraftSubsystem::SendCraftEnter(int32 RecipeId, const TArray<int32>& MatSlots, const TArray<uint8>& MatContainers)
{
	// ⚠ Для окна 77500 (волна В4): материалы — ТОЛЬКО контейнеры 1/2 (P35 отвергает надетое/склад, С-1/С-2), и
	//   следующий 6602 серии «N раз по одному» уходит лишь после ответа 6603 на предыдущий (замок выше).
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !BeginRequest(6602)) { return false; }
	const bool bOk = Net->SendCraftEnter6602(RecipeId, MatSlots, MatContainers);
	MarkSent(6602, bOk);
	return bOk;
}

bool UAsdaCraftSubsystem::SendLevelUpScroll(int32 ScrollSlot, int32 ScrollContainer)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !BeginRequest(6604)) { return false; }
	const bool bOk = Net->SendCraftScrollLevelUp6604(ScrollSlot, (uint8)ScrollContainer);
	MarkSent(6604, bOk);
	return bOk;
}

// ═════════════════════════════ НОТИСЫ ═════════════════════════════════════════════════════════

void UAsdaCraftSubsystem::JournalNotice(const FString& Line)
{
	NoticeJournal.Add(Line);
	UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] нотис: %s"), *Line);
}

void UAsdaCraftSubsystem::NoticeMsgBox(int32 LocId, int32 EtalonNoticeId)
{
	// `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1|24, текст, 1)` → ONEBUT_MSGBOX 1500 (миснейм IDA). Тот же маршрут,
	//   что `AsdaNotice::MsgBoxLoc` (окно; нет строки/окна — лента). Звуки 25 / 4 + 5 — у общей точки (волна В5, W4-1).
	//   U2 (дедуп №2, В2-A, 11.10.2026): построчная копия заменена вызовом общей точки — поведение то же.
	UGameInstance* GI = GetGameInstance();
	const FString S = AsdaNotice::Loc(GI, LocId);   // для журнала ремесла (самотесты читают «msgbox N «текст»»)
	AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::ItemRefused, LocId, AsdaLoc::kBtnOk, EtalonNoticeId);
	JournalNotice(FString::Printf(TEXT("msgbox %d «%s»"), LocId, *S));
}

void UAsdaCraftSubsystem::NoticeConfirm(int32 LocId, TFunction<void()> OnYes, int32 EtalonNoticeId)
{
	// `CItem_BuildTooltipText(g_pItemTooltipUI, 1, 84|85, текст, 1)` → TWOBUT_MSGBOX 1520 (общая точка `ShowGameConfirm`).
	//   «Да» (msg 1523) → `OnSendItemUpgrade` case 0x54/0x55; «Нет» (1524) — только закрыть. Звуки — у общей точки (В5).
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const FString S = AsdaNotice::Loc(GI, LocId);
	if (!WM || S.IsEmpty())
	{
		UE_LOG(LogAsdaCraft, Warning, TEXT("[ремесло] подтверждение %d не показано: %s"), LocId, WM ? TEXT("строки нет") : TEXT("оконной подсистемы нет"));
		return;
	}
	TWeakObjectPtr<UAsdaCraftSubsystem> WeakThis(this);
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(S, [WeakThis, LocId, OnYes = MoveTemp(OnYes)](bool bYes)
	{
		UAsdaCraftSubsystem* Self = WeakThis.Get();
		if (!Self) { return; }
		Self->JournalNotice(FString::Printf(TEXT("confirm %d → %s"), LocId, bYes ? TEXT("да") : TEXT("нет")));
		if (bYes && OnYes) { OnYes(); }
	}, 0.f, 2288, 2289, EtalonNoticeId, /*EtalonKind*/1);
	JournalNotice(FString::Printf(TEXT("confirm %d «%s»"), LocId, *S));
}

bool UAsdaCraftSubsystem::TryUseCraftScroll(int32 Container, int32 Cell)
{
	// `CInventory_OnSlotUse@0xD07460` (item_inventory.c:37833-37852). Только расходники (контейнер 2, `m_nSelKind == 0`).
	if (Container != 2) { return false; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemInstance* Src = Inv ? Inv->GetCell(Container, Cell) : nullptr;
	if (!Src || Src->IsEmpty() || !IT) { return false; }
	const FAsdaItemRecord* Rec = IT->GetItem(Src->ItemCode);
	if (!Rec) { return false; }
	// `g_nUpgradeItemSlotId = *(i16*)(pSlotItem + 5)`, `g_nUpgradeItemType = *(u8*)(pSlotItem + 4)` — из САМОЙ записи.
	const int32 Slot = Src->SlotIndex;
	const int32 Cont = Src->ContainerType;
	if (Rec->LinkCode == 101)
	{
		UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] свиток рецепта %d (рецепт %d) — подтверждение 5571 (команда 84)"), Src->ItemCode, Rec->LinkedGroupCode);
		TWeakObjectPtr<UAsdaCraftSubsystem> WeakThis(this);
		NoticeConfirm(AsdaCraftLoc::kAskLearn, [WeakThis, Slot, Cont]()
		{
			// `OnSendItemUpgrade` case 0x54: `netSendMaskedPacket_op6598(g_pItemMainWnd, слот, контейнер)`, глобалы → −1.
			if (UAsdaCraftSubsystem* Self = WeakThis.Get())
			{
				if (!Self->SendLearnRecipe(Slot, Cont)) { Self->NoteConfirmedButUnsent(6598); }   // МЕЛК-1 В7: отступление (см. там)
			}
		}, /*эталон (1, 84)*/84);
		return true;
	}
	if (Rec->LinkCode == 102)
	{
		// `itemRec->linkedGroupCode == sub_929CF7(g_pItemMainWnd) + 1` — свиток СЛЕДУЮЩЕГО уровня ремесла.
		if (Rec->LinkedGroupCode == CraftLevel + 1)
		{
			UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] свиток уровня %d (ур. %d → %d) — подтверждение 5582 (команда 85)"), Src->ItemCode, CraftLevel, Rec->LinkedGroupCode);
			TWeakObjectPtr<UAsdaCraftSubsystem> WeakThis(this);
			NoticeConfirm(AsdaCraftLoc::kAskLevelUp, [WeakThis, Slot, Cont]()
			{
				// `OnSendItemUpgrade` case 0x55: `netSendMaskedPacket_op6604(g_pItemMainWnd, слот, контейнер)`.
				if (UAsdaCraftSubsystem* Self = WeakThis.Get())
				{
					if (!Self->SendLevelUpScroll(Slot, Cont)) { Self->NoteConfirmedButUnsent(6604); }   // МЕЛК-1 В7
				}
			}, /*эталон (1, 85)*/85);
		}
		else
		{
			UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] свиток уровня %d: нужен ур. %d, текущий %d — ONEBUT 5584"), Src->ItemCode, Rec->LinkedGroupCode - 1, CraftLevel);
			NoticeMsgBox(AsdaCraftLoc::kWrongLevel);
		}
		return true;
	}
	return false;
}

void UAsdaCraftSubsystem::NoteConfirmedButUnsent(int32 Opcode)
{
	// ★ Волна В8 (ревизия В7 МЕЛК-1, реестр W7-8). ОТСТУПЛЕНИЕ ПОРТА, описано, нотиса нет — и это осознанно:
	//   у эталона «Да» в 5571/5582 шлёт 6598/6604 ВСЕГДА (`OnSendItemUpgrade` case 0x54/0x55 — ни замка, ни шлюза), поэтому
	//   строки на случай «не отправлено» в его данных нет; выдумывать текст нельзя (закон `never-invent-only-original`).
	//   Отказ даёт только порт: замок «одна операция в полёте» (`asda.Craft.SingleInFlight`, снимется ответом или таймаутом
	//   `asda.Craft.ReplyTimeoutSec`) либо общий шлюз `SendSecured` (нет сети / переход / одержимость / «душа» — у двух
	//   последних СВОЙ нотис оригинала выдаёт сам шлюз). Свиток НЕ расходуется (сервер ничего не получил) — игрок может
	//   повторить двойной клик. Та же политика у удаления 76096 (ревизия В0–В2 М-4) и у «Улучшить» заточки (N-2).
	UE_LOG(LogAsdaCraft, Warning, TEXT("[ремесло] «Да» в подтверждении свитка: %d НЕ отправлен (замок «в полёте» %d / шлюз SendSecured) — "
		"свиток не израсходован, нотиса у эталона для этого случая нет (отступление, W7-8)"), Opcode, ReplyLatch.Opcode);
	JournalNotice(FString::Printf(TEXT("unsent %d (замок/шлюз) — без нотиса"), Opcode));
}

void UAsdaCraftSubsystem::NoticeChatAndPlank(int32 LocId)
{
	// `CChat_AddTokenizedLines(g_pChatWnd, текст, −11514, 10000, 0)` + `CControl_SetTooltipText(g_pChatWnd, текст,
	//   −11514, 10000)` — второе — миснейм: строка в плашке WARMESSAGE_WIN 221 (как у склада/инвентаря).
	UGameInstance* GI = GetGameInstance();
	// ★ 27.09.2026: пара «журнал + плашка 221» — общая точка `AsdaNotice::PlankAndChatLoc` (та же связка у отказов
	//   портала 5055/6201). Поведение прежнее: строки нет ⇒ плейсхолдер в ленту; иначе журнал, затем плашка
	//   (`bChatFirst` — порядок эталона ремесла, ревизия 5055 М-1), −11514, 10 с; строка самотеста — всегда (М-2).
	const FString S = AsdaNotice::Loc(GI, LocId);
	if (S.IsEmpty()) { AsdaNotice::Chat(GI, EAsdaMsgCategory::System, LocId); return; }
	AsdaNotice::PlankAndChatLoc(GI, EAsdaMsgCategory::System, LocId, kColCraftChat, 10000.f, LocId, /*bChatFirst=*/true);
	JournalNotice(FString::Printf(TEXT("chat+plank %d «%s»"), LocId, *S));
}

void UAsdaCraftSubsystem::NoticeGateChat(int32 LocId)
{
	// `uiCheckPartyInviteInvLockGuard@0xFDD0F0`: `CChat_AddTokenizedLines(g_pChatWnd, текст, −6881324, 10000, 0)`.
	UGameInstance* GI = GetGameInstance();
	const FString S = AsdaNotice::Loc(GI, LocId);
	if (S.IsEmpty()) { AsdaNotice::Chat(GI, EAsdaMsgCategory::System, LocId); }
	else { AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, S, kColGateChat, 10000.f); }
	JournalNotice(FString::Printf(TEXT("chat %d «%s»"), LocId, *S));
}

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════

TArray<uint8> UAsdaCraftSubsystem::DebugBuild6597(const TArray<int32>& Learned, int32 Limit, int32 Level, int32 Exp, int32 Percent)
{
	// Раскладка тела 6597 = серверный `recvDBCraftList@0x53C7E0` (GameServer.c:120860-120942).
	TArray<uint8> B; B.SetNumZeroed(593);
	int32 N = 0;
	for (int32 Id : Learned) { if (Id >= 0 && Id < kMaxRecipeId && B[4 + Id] == 0) { B[4 + Id] = 1; ++N; } }
	AsdaBytes::WrI16(&B[0], N);
	AsdaBytes::WrI16(&B[2], Limit);
	B[584] = (uint8)Level;
	AsdaBytes::WrI32(&B[585], Exp);
	AsdaBytes::WrI32(&B[589], Percent);
	return B;
}

bool UAsdaCraftSubsystem::DebugCaptureOnce(int32 Opcode)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return false; }
	if (Opcode != 6598 && Opcode != 6600 && Opcode != 6602 && Opcode != 6604) { return false; }
	if (Net->IsConnected())
	{
		UE_LOG(LogAsdaCraft, Warning, TEXT("[ремесло] перехват %d НЕ поставлен: есть живое соединение с сервером"), Opcode);
		return false;
	}
	if (Net->SendCaptureForStand)
	{
		UE_LOG(LogAsdaCraft, Warning, TEXT("[ремесло] перехват %d НЕ поставлен: перехватчик уже занят"), Opcode);
		return false;
	}
	TWeakObjectPtr<UAsdaCraftSubsystem> WeakThis(this);
	CaptureOpcode = Opcode;
	CaptureGen = Net->ArmSendCapture([WeakThis](uint16 Op, const uint8* P, int32 Len, bool /*bPad*/, TCHAR /*Env*/)
	{
		UAsdaCraftSubsystem* Self = WeakThis.Get();
		if (!Self || (int32)Op != Self->CaptureOpcode) { return; }
		TArray<uint8> Body;
		if (P && Len > 0) { Body.Append(P, Len); }
		Self->DebugCaptured.Add(TPair<int32, TArray<uint8>>((int32)Op, Body));
		// Первый пакет пойман — перехватчик снимается СРАЗУ (отправитель зовёт копию — снимать изнутри безопасно).
		Self->DebugDisarmCapture(TEXT("пойман первый пакет"));
	}, (uint16)Opcode);
	bCaptureArmed = true;
	UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] перехват %d поставлен (узкий, снимется сам)"), Opcode);
	return true;
}

bool UAsdaCraftSubsystem::RefuseDebugWhenOnline(UWorld* W, const TCHAR* Cmd, float GuardSeconds)
{
	// ★ Волна В8 (С-3). Образец — `asda.Upgrade.PanelShots` (AsdaItemUpgradeSubsystem.cpp, волна 14 МЕЛК-4): при живом
	//   соединении отказ. Самотест ремесла жмёт боевые кнопки (76096 → 6600 удаляет рецепт на сервере НЕОБРАТИМО, 77533 →
	//   6602 расходует материалы), затирает модель сумки и вбрасывает 6597 — на живом персонаже это порча данных.
	//   ★ С-Р1 (26.09.2026): сама проверка соединения — общая точка порта `FAsdaOutgoingGate::RefuseDebugWhenOnline`
	//   (одна на все самотесты); её же окно — второй барьер `BeginRequest` (редакция 1, МЕЛК-1).
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaCraftSubsystem* Self = GI ? GI->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
	if (!Self) { return true; }
	// ★ С-Р1, редакция 1 (МЕЛК-1): окно защиты отправителей ремесла — ОБЩЕЕ окно порта (`UAsdaNetSubsystem::DebugSendGuard*`)
	//   с опкодами 6598/6600/6602/6604. Прежнее своё поле `SelfTestGuardUntil` снято; поведение то же: окно не короче уже
	//   взведённого (проходы самотеста запускаются одной строкой с разными delay=), при GuardSeconds = 0 не взводится.
	static const uint16 kCraftOps[] = { 6598, 6600, 6602, 6604 };
	return FAsdaOutgoingGate::RefuseDebugWhenOnline(W, Cmd, GuardSeconds, MakeArrayView(kCraftOps));
}

bool UAsdaCraftSubsystem::IsSelfTestSendGuardActive() const
{
	// Редакция 1 (МЕЛК-1): общее окно порта; все четыре опкода ремесла взводятся вместе, поэтому достаточно одного.
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	return FAsdaOutgoingGate::IsDebugSendWindowOpen(Net, 6598);
}

void UAsdaCraftSubsystem::DebugDisarmCapture(const TCHAR* Why)
{
	if (!bCaptureArmed) { return; }
	bCaptureArmed = false;
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bMine = Net && Net->DisarmSendCapture(CaptureGen);
	CaptureGen = 0;
	UE_LOG(LogAsdaCraft, Log, TEXT("[ремесло] перехват %d снят: %s%s"), CaptureOpcode, Why,
		bMine ? TEXT("") : TEXT(" (перехватчик уже сменён другим — чужой не трогаю)"));
}

FString UAsdaCraftSubsystem::DumpState() const
{
	FString S = FString::Printf(TEXT("[ремесло] модель: 6597 пришёл=%d · ур.=%d опыт=%d процент=%d · изучено=%d (список %d) лимит=%d "
		"(формула клиента %d, надбавка %d) · таблицы: рецептов %d, rate %d\n"),
		bListReceived ? 1 : 0, CraftLevel, CraftExp, CraftPercent, LearnedCount, LearnedList.Num(), RecipeLimit,
		ClientLimitFormula(), LimitBonus, GetRecipeCount(), GetRateCount());
	S += TEXT("  изученные id:");
	for (int32 Id : LearnedList) { S += FString::Printf(TEXT(" %d"), Id); }
	S += FString::Printf(TEXT("\n  замок «в полёте»: %d (запрос %d)"), ReplyLatch.IsArmed() ? 1 : 0, ReplyLatch.Opcode);
	S += FString::Printf(TEXT("\n  последний 6603: код=%d ступень=%d +1шт=%d продукт=%d положен=%d\n"),
		LastEnter.Code, LastEnter.ReachedTier, LastEnter.BonusFlag,
		LastEnter.Product57.Num() >= 4 ? AsdaBytes::RdI32(LastEnter.Product57.GetData()) : 0, LastEnter.bProductApplied ? 1 : 0);
	return S;
}
