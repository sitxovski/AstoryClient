// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPetCageSubsystem_Events.cpp — ★ R2 10.11.2026 (разрез AsdaPetCageSubsystem.cpp, дословный перенос): события питомцев, ответы 6109/6122, стенды кормления/слота, нотисы, дампы, дев-команды.
//   Общее для частей — в AsdaPetCageSubsystemInternal.h; шапка и реверс — в AsdaPetCageSubsystem.cpp.
#include "UI/AsdaPetCageSubsystem.h"
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaCraftSubsystem.h"   // ★ гейты Г1/Г2: имена окон ремесла 76000/77500 (волна В2)

#include "UI/AsdaJuiWindowHandle.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaPetGrowthSubsystem.h"   // ★ волна 4: кнопка 43055 открывает окно 44200
#include "UI/AsdaPetEvolutionSubsystem.h" // ★ волна 5: кнопка 43056 открывает окно 43500
#include "UI/AsdaPetEvoTipSubsystem.h"   // ★ волна 4: наведение на слот показывает окно 43300
#include "UI/AsdaPetPreviewActor.h"      // ★ волна 9: превью 43052 — свой RT 240×170 и вращение
#include "Engine/TextureRenderTarget2D.h"   // дамп размера буфера превью
#include "Materials/MaterialInstanceDynamic.h"   //   кисть кадра для 43052 (и для 43576 окна 43500)
#include "Materials/MaterialInterface.h"        // ★ 24.09: мастер-материал кисти превью
#include "Kismet/KismetRenderingLibrary.h"      // ★ 24.09: выгрузка буфера превью в PNG (asda.Pet.PreviewDumpRt)
#include "Engine/World.h"                //   спавн актора сцены превью
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Pet/AsdaPetSubsystem.h"
#include "Net/NetOpcodes.h"          // OP_PET_SLOT_COUNT — диагностическая подача 6123
#include "AsdaNetSubsystem.h"        // ★ волна 6: SetSelfWeight — полоса веса из 6109/6122
#include "AsdaBytes.h"               // ★ волна 6: сборка тел 6109/6122 для стенда

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/EditableText.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Login/AsdaLoginData.h"   // [ПОРТ] 08.10.2026: лимиты ввода в байтах провода (TruncateToWireLimit)
#include "Test/AsdaSelfTestKit.h"   // ★ 21.10.2026: итог самотеста asda.Pet.SelfTestNameRule
#include "Text/AsdaLang.h"          // ★ 21.10.2026: английские названия пород (оверлей pettable) в самотесте кличек
#include "Text/AsdaLangRows.h"       // [ПОРТ] 09.10.2026 (мультиязычность): формат опции питомца на языке сессии
#include "AsdaPetCageSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaPetCageSubsystem*.cpp

// ── ★ ВОЛНА 6: номера контейнеров инвентаря, как их называет эталон в пет-приёмниках ──
//   Ровно те же числа, что у окна роста (`AsdaPetGrowthSubsystem.h:204-206`); дублировать
//   заголовок ради двух констант не стали — они локальны для файла.
static constexpr int32 kShellContainer = 1;   // эталонный `g_InventoryBag`, 30 ячеек
static constexpr int32 kSpendContainer = 2;   // эталонный `g_InventoryArray2`, 60 ячеек
/** Подтип корма питомца в `item_list` (`GameServer.c:35554, :35563`; item_inventory.c:37885). */
static constexpr int32 kFeedSubType = 106;
/** Подтип расширителя ячеек питомцев в `special_itemlist` (`GameServer.c:36454, :36478`). */
static constexpr int32 kSlotUpSubType = 104;

namespace PetCageLoc {
	// Приёмники — `item_enhance.c`
	constexpr int32 kCallOff    = 101112;    // :4508  6101, призыв снят
	constexpr int32 kCallOn     = 101111;    // :4515  6101, призыв поставлен
	constexpr int32 kHunger     = 101001;    // :4576  6107, спрятался от голода
	constexpr int32 kRenameBad  = 101120;    // :4959  6119 код 2
	constexpr int32 kRenameNoPet= 101121;    // :4969  6119 код 4
	constexpr int32 kRenameOk   = 101110;    // :4984  6119 код 1
	constexpr int32 kSlotUpOk   = 101116;    // :5034  6122 код 1
	constexpr int32 kSlotUpNone = 101114;    // :5045  6122 код 2
	constexpr int32 kSlotUpMax  = 101115;    // :5049  6122 код 3

	// ★ ВОЛНА 6 — приёмник 6109 `netOnRefineResultPacket@0xDC9710` (network.c:25868-25986).
	//   Функция лежит в `network.c`, но `this` у неё — `CPetData`, то есть ОКНО 43000: она
	//   заканчивается парой `CRefineUI_RefreshItemPanel` + `refineWnd_RefreshSelectedPanel`
	//   (:25984-25985), ровно как приёмники 6119/6122/6123 в `item_enhance.c`. Поэтому нотисы
	//   кормления — здесь, а не в «сетевой» подсистеме (закон `loc-per-handler-not-by-family`).
	constexpr int32 kFeedNotFood   = 101100;  // :25966 6109 коды 2 и 3 — «Это не корм»
	constexpr int32 kFeedNoActive  = 101101;  // :25971 6109 код 4 — «Питомец не призван»
	constexpr int32 kFeedNoTaste   = 101102;  // :25901 и :25976 6109 код 5 — «Не по вкусу» (ДВАЖДЫ, см. Д-КОРМ)
	constexpr int32 kFeedNoPet     = 101103;  // :25978 6109 код 6 — «Питомец не выбран»

	// Подпись «ОК» однокнопочного окна
	constexpr int32 kBtnOk      = 2288;
}

static ASDA_DEV_CVAR(int32, CVarRenameNoticeFallthrough,
	TEXT("asda.Pet.RenameNoticeFallthrough"), 1,
	TEXT("1 (по умолчанию) — воспроизводить дефект оригинала ПД3: при отказе 6119 нотисы сыплются ")
	TEXT("каскадом через goto (код 2 → 101120+101118+101121, код 3 → 101118+101121). 0 — ровно один."),
	ECVF_Default);

// ★ ВОЛНА 6, ДЕФЕКТ ОРИГИНАЛА Д-КОРМ. Приёмник 6109 показывает нотис 101102 ДВАЖДЫ на коде 5:
//   первый раз ДО сверки charId (`if (BYTE1(refineResult[16]) == 5)`, network.c:25900-25904),
//   второй — в `case 5` того же switch (:25974-25978). Игрок видит «Не по вкусу» два раза подряд.
//   Воспроизводим 1:1; 0 — ровно один нотис.
static ASDA_DEV_CVAR(int32, CVarFeedNoticeDouble,
	TEXT("asda.Pet.FeedNoticeDouble"), 1,
	TEXT("1 (по умолчанию) — воспроизводить дефект оригинала Д-КОРМ: при коде 5 ответа 6109 ")
	TEXT("нотис 101102 «Не по вкусу» показывается ДВАЖДЫ (network.c:25901 и :25976). 0 — один раз."),
	ECVF_Default);

// ★ ВОЛНА 6, ДЕФЕКТ ОРИГИНАЛА Д-ЯЧ. Приёмник 6122 чистит ячейку потраченного расширителя только
//   при `слот > 0` (item_enhance.c:5038 и :5040) — расширитель из ячейки 0 остаётся нарисованным.
//   По умолчанию ИСПРАВЛЕНО (чистим и ячейку 0); 1 — поведение оригинала.
static ASDA_DEV_CVAR(int32, CVarSlotUpLegacySkipCell0,
	TEXT("asda.Pet.SlotUpLegacySkipCell0"), 0,
	TEXT("0 (по умолчанию) — ответ 6122 чистит ячейку расширителя и при номере 0 (исправление ")
	TEXT("дефекта Д-ЯЧ). 1 — как эталон: ячейка 0 не чистится (item_enhance.c:5038)."),
	ECVF_Default);

// ══════════════════════════════ СОБЫТИЯ МОДЕЛИ (пакеты) ═══════════════════════════════════════
void UAsdaPetCageSubsystem::DrainPetEvents()
{
	// ★ ВОЛНА 8 (дубли, пункт 7): слив очереди — общая точка `AsdaPetUi::DrainEvents`
	//   (это тело было скопировано в четыре окна). Хвост у каждого окна свой — он ниже.
	if (PetEvents.Num() == 0) { return; }
	AsdaPetUi::DrainEvents(PetEvents,
		[this](int32 Op, int32 Res, int32 Uid) { OnPetEvent(Op, Res, Uid); });
	// Эталон в КАЖДОМ приёмнике заканчивает парой перерисовок — делаем то же один раз на пачку.
	RefreshItemPanel();
	if (Active() != nullptr) { OpenStatusPanel(); } else { HideStatusPanel(); }
	RefreshStatusPanel();
	// ★ КРАЙ 2 требования «окно следует за выбором»: питомца могли ВЫБРОСИТЬ (6110) или
	//   переименовать, пока окно открыто. Пачка событий — единственное место, где это видно.
	FollowSelectionInInfoWindow();
}

void UAsdaPetCageSubsystem::OnPetEvent(int32 Opcode, int32 Result, int32 PetUniqueId)
{
	UAsdaPetSubsystem* P = Pets();
	if (!P) { return; }
	const FAsdaPetSlot* S = P->FindByUniqueId(PetUniqueId);
	const FString Name = S ? S->Name : FString();

	switch (Opcode)
	{
		case 6101:
			// `CPetData::OnCallPetResult` (item_enhance.c:4497-4527): нотис ТОЛЬКО при коде 1,
			//   и он зависит от НОВОГО состояния признака: стало «призван» → 101111, иначе 101112.
			if (Result == 1 && S)
			{
				const int32 LocId = S->bIsActive ? PetCageLoc::kCallOn : PetCageLoc::kCallOff;
				const FString Text = AsdaUi::FormatPrintfArgs(Loc(LocId), { Name });
				if (!Text.IsEmpty()) { AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::ItemRefused, Text, PetCageLoc::kBtnOk); }
			}
			// Коды 2 (питомца нет) и 3 (сытость 0) НОТИСА НЕ ИМЕЮТ — эталон в этих ветках
			//   только перерисовывает окно (проверено построчно, item_enhance.c:4529-4532).
			break;

		case 6107:
			// Приём 6107 (item_enhance.c:4570-4581): «[%s] страдает от голода и прячется».
			if (!Name.IsEmpty())
			{
				const FString Text = AsdaUi::FormatPrintfArgs(Loc(PetCageLoc::kHunger), { Name });
				if (!Text.IsEmpty()) { AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::ItemRefused, Text, PetCageLoc::kBtnOk); }
			}
			break;

		case 6119:
			// ★ ДЕФЕКТ ОРИГИНАЛА ПД3 (`refineWndOnRefineResult`, item_enhance.c:4956-4974):
			//   ветки switch ПРОВАЛИВАЮТСЯ через `goto LABEL_10/LABEL_11`, поэтому код 2 даёт
			//   ТРИ нотиса подряд (101120 → 101118 → 101121), код 3 — ДВА (101118 → 101121),
			//   код 4 — один (101121). Воспроизводим 1:1; ключ отката к «по одному нотису» —
			//   `asda.Pet.RenameNoticeFallthrough 0`.
			{
				const bool bFall = CVarRenameNoticeFallthrough.GetValueOnGameThread() != 0;
				if (Result == 1)
				{
					// Успех: «[ %s ]'s name has been changed to [ %s ]» — СТАРОЕ имя и НОВОЕ
					//   (item_enhance.c:4983-4986: эталон печатает нотис ДО записи нового имени
					//   в слот). Модель волны 1 отдаёт прежнее имя через `GetLastRenamePrevName`.
					const FString PrevName = P->GetLastRenamePrevName();
					const FString Text = AsdaUi::FormatPrintfArgs(Loc(PetCageLoc::kRenameOk),
						{ PrevName.IsEmpty() ? Name : PrevName, Name });
					if (!Text.IsEmpty()) { AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::ItemRefused, Text, PetCageLoc::kBtnOk); }
					NameShownForUid = -1;   // поле имени перечитает новое имя
				}
				else if (Result == 2)
				{
					NoticeMsgBox(PetCageLoc::kRenameBad);
					if (bFall) { NoticeMsgBox(PetCageLoc::kNameLocked); NoticeMsgBox(PetCageLoc::kRenameNoPet); }
				}
				else if (Result == 3)
				{
					NoticeMsgBox(PetCageLoc::kNameLocked);
					if (bFall) { NoticeMsgBox(PetCageLoc::kRenameNoPet); }
				}
				else if (Result == 4)
				{
					NoticeMsgBox(PetCageLoc::kRenameNoPet);
				}
			}
			break;

		case 6109:
			// ★ ВОЛНА 6 — кормление. Разбор целиком в `OnFeedResult6109`.
			OnFeedResult6109(Result, PetUniqueId);
			break;

		case 6122:
			// ★ ВОЛНА 6 — расширение ячеек. Разбор целиком в `OnSlotUpResult6122`.
			OnSlotUpResult6122(Result);
			break;

		default:
			// 6103/6104/6105/6106/6111/6113/6123/6149 в окне 43000 СВОИХ нотисов не имеют:
			//   их обработчики только перерисовывают панель (6104/6105/6106/6111/6123) либо
			//   показывают нотисы в СВОИХ окнах — 42000 (волна 3), 44200 (волна 4), 43500 (волна 5).
			//   Закон `loc-per-handler-not-by-family`: чужие сюда не тянем.
			break;
	}
	(void)Result;
}

// ── ★ ВОЛНА 6: приём 6109 — ПОЛНЫЙ порт `netOnRefineResultPacket@0xDC9710` в части ОКНА ────────
//   (network.c:25868-25986). Запись питомца (сытость + три бонуса) уже применила модель волны 1
//   (`UAsdaPetSubsystem::OnFeedResult6109`, ветка `case 1` эталона :25914-25930) — второй раз не
//   трогаем. Здесь ровно то, что эталон делает ПОВЕРХ записи: полоса веса, ячейка корма и нотисы.
void UAsdaPetCageSubsystem::OnFeedResult6109(int32 Result, int32 PetUniqueId)
{
	UAsdaPetSubsystem* P = Pets();
	if (!P) { return; }
	const FAsdaPetFeedEcho& Echo = P->GetLastFeedEcho();

	// ★ ДЕФЕКТ ОРИГИНАЛА Д-КОРМ. Нотис «не по вкусу» 101102 стоит ДВА РАЗА: один — ДО сверки
	//   charId, прямо после разбора тела (`if (BYTE1(refineResult[16]) == 5)`, network.c:25900-25904),
	//   второй — в `case 5` (:25974-25978). На коде 5 игрок видит его дважды подряд.
	//   ★ ЧТО ЗДЕСЬ ИСПРАВЛЕНО ОСОЗНАННО: первый нотис эталона показывается даже тогда, когда
	//   `charId` тела НЕ СВОЙ, то есть чужой пакет мог бы напечатать нотис нашему игроку. Модель
	//   волны 1 чужие пакеты отбрасывает в `IsForSelf` и события не метит, поэтому у нас этой
	//   ветки нет вовсе. Это правка ПОВЕДЕНИЯ (закон `fix-original-defects`); на живом сервере
	//   разницы нет — 6109 адресный (`GameServer.c:35470` шлёт его одному пользователю.)
	if (Result == 5 && CVarFeedNoticeDouble.GetValueOnGameThread() != 0)
	{
		NoticeMsgBox(PetCageLoc::kFeedNoTaste);
	}

	switch (Result)
	{
		case 1:
		{
			// :25931-25959 — вес, ячейка корма, пересборка панели инвентаря. Нотиса НЕТ.
			UAsdaInventorySubsystem* IV = Inv();
			UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			// :25931 `HUD_UpdateMiniWeightBar(g_pHUD, *(int*)((char*)&refineResult[16] + 3))`.
			if (Net) { Net->SetSelfWeight(Echo.WeightCur, -1, TEXT("6109")); }
			// ★ ОДНА ПЕРЕСБОРКА КЭША ХОТБАРА НА ВЕСЬ БЛОК: эталон трогает одну ячейку и зовёт
			//   `CInventoryUI_ResetAndRefresh` ОДИН раз в конце (:25959). Батч повторяет это.
			if (IV) { IV->BeginCellBatch(); }
			// ★★★ ДВЕ ВЕТКИ ЭТАЛОНА СВОДЯТСЯ К ОДНОМУ ПРАВИЛУ — доказательство:
			//   • количество == 0 (:25932-25945): ячейка ОЧИЩАЕТСЯ (memset 57 б) в своём
			//     контейнере, затем `CClass_BuildItemRecord(g_pQuickSlotBar, 1, …)` — хотбар
			//     узнаёт, что предмет кончился;
			//   • количество > 0 (:25946-25958): контейнер 2 — `memcpy` записи в ячейку;
			//     контейнер 1 — `memset` + `UI_OnInventoryRemove(запись)`, а это, как уже
			//     разобрано в порту (`AsdaInventorySubsystem.h:738`), переходник на
			//     `Inventory_SetBagSlot@0xD61650`, который КЛАДЁТ запись при коде > 0.
			//     То есть обе ветки «количество > 0» пишут в ячейку ту же запись.
			//   ⇒ правило: `количество > 0` ⇒ положить запись, иначе очистить ячейку.
			//   Пересборка кэша хотбара у нас идёт из `NotifyCell`, поэтому отдельного аналога
			//   `CClass_BuildItemRecord` заводить не надо — это та же общая точка.
			if ((Echo.ItemBag == kShellContainer || Echo.ItemBag == kSpendContainer) && Echo.ItemSlot >= 0)
			{
				if (IV)
				{
					if (Echo.ItemQty > 0 && Echo.ItemCode != 0)
					{
						// ★★★ ВОЛНА 8 (БЛОКЕР-2 сквозной ревизии): КЛАДЁМ ЗАПИСЬ ЦЕЛИКОМ, 57 б.
						//   БЫЛО: `DebugPutItem(сумка, ячейка, код, штук)` — ДИАГНОСТИЧЕСКАЯ
						//   эмуляция «без сервера» (`AsdaInventorySubsystem.h:811`), которая
						//   делает `I.Clear()` и пишет ЧЕТЫРЕ поля: +0 код, +4 контейнер,
						//   +5 слот, +9 количество. Остальные 53 байта записи она ОБНУЛЯЛА.
						//   Прежнее оправдание «у корма всё равно нули» неверно как закон:
						//   ветка общая для контейнеров 1 и 2, а в контейнер 1 (премиум-сумка)
						//   сервер кладёт ту же 57-байтовую запись любого предмета, и его
						//   прочность (+13), вес стопки (+14), четыре гнезда самоцветов
						//   (+16/+18/+20/+22), ранг самоцвета (+24) и заточка (+27, +29…)
						//   уничтожались бы молча. Эталон копирует запись ОДНИМ куском:
						//   `memcpy(… + 57 * слот, &refineResult[2], 0x39u)` (network.c:25951).
						//   СТАЛО: общая точка применения записей `ApplyExternalRecord`
						//   (`AsdaInventorySubsystem.h:657`) — она сама маршрутизирует по полям
						//   записи (+4 контейнер, +5 слот) и зовёт приватный `PutRecord`.
						//   Второй копии модели контейнеров не заводим (закон `port-shared-points`).
						if (Echo.ItemRec57.Num() == 57)
						{
							IV->ApplyExternalRecord(Echo.ItemRec57);
						}
						else
						{
							// Сырой записи в снимке нет — значит тело 6109 было короче 89 б и до
							//   сюда не дошло бы вовсе. Ветка оставлена как честный след: молча
							//   класть обрезанную запись (закон `no-simplifications-stubs`) нельзя.
							UE_LOG(LogAsdaPetCage, Error,
								TEXT("[питомец-окно] 6109 код 1: в снимке нет сырой записи 57 б (есть %d) — ячейку (%d,%d) НЕ трогаю"),
								Echo.ItemRec57.Num(), Echo.ItemBag, Echo.ItemSlot);
						}
					}
					else
					{
						IV->ClearCell(Echo.ItemBag, Echo.ItemSlot);
					}
				}
			}
			else
			{
				// Сервер прислал ячейку вне (1,2)×[0..N) — эталон записал бы за границу массива
				//   (`memset(&g_InventoryArray2 + 57 * slot, …)` без проверки, :25937). В порту
				//   `ClearCell`/`DebugPutItem` отбили бы индекс сами; пишем след и ничего не трогаем.
				UE_LOG(LogAsdaPetCage, Warning,
					TEXT("[питомец-окно] 6109 код 1: ячейка корма вне диапазона (сумка %d, ячейка %d) — сумку не трогаю"),
					Echo.ItemBag, Echo.ItemSlot);
			}
			if (IV) { IV->EndCellBatch(); }   // ОДНА пересборка кэша хотбара на весь блок
			break;
		}
		case 2:
		case 3:
			// :25964-25968 — «Это не корм» (коды 2 «предмет не найден» и 3 «тип не 105/106»
			//   идут ОДНОЙ веткой — парные ветки при общем `case`, закон
			//   `notices-registry-lossy-on-ternary`).
			NoticeMsgBox(PetCageLoc::kFeedNotFood);    // 101100
			break;
		case 4:
			NoticeMsgBox(PetCageLoc::kFeedNoActive);   // :25969-25973  101101 «Питомец не призван»
			break;
		case 5:
			NoticeMsgBox(PetCageLoc::kFeedNoTaste);    // :25974-25978  101102 «Не по вкусу» (второй раз)
			break;
		case 6:
			NoticeMsgBox(PetCageLoc::kFeedNoPet);      // :25975-25979  101103 «Питомец не выбран»
			break;
		default:
			// :25980-25981 — `default: break;`. Код 7 сервера («расходник ни cash, ни обычный»,
			//   GameServer.c:35779) нотиса НЕ ИМЕЕТ — эталон в этой ветке молчит.
			UE_LOG(LogAsdaPetCage, Log,
				TEXT("[питомец-окно] 6109 код %d — у эталона `default`, нотиса нет"), Result);
			break;
	}
	// :25984-25985 — эталон в КОНЦЕ зовёт `CRefineUI_RefreshItemPanel` + `refineWnd_RefreshSelectedPanel`.
	//   Обе перерисовки делает `DrainPetEvents` один раз на всю пачку событий.
	UE_LOG(LogAsdaPetCage, Log,
		TEXT("[питомец-окно] 6109 код=%d pet=%d сытость=%d вес=%d корм=(код %d, сумка %d, ячейка %d, штук %d)"),
		Result, PetUniqueId, Echo.Fullness, Echo.WeightCur,
		Echo.ItemCode, Echo.ItemBag, Echo.ItemSlot, Echo.ItemQty);
}

// ── ★ ВОЛНА 6: приём 6122 — ПОЛНЫЙ порт `refineWndOnRefineResultPacket@0xDCB9E0` ───────────────
//   (item_enhance.c:5015-5057). Счётчик доступных ячеек уже применила модель волны 1; здесь —
//   нотис своей ветки, полоса веса и очистка ячейки потраченного расширителя.
void UAsdaPetCageSubsystem::OnSlotUpResult6122(int32 Result)
{
	UAsdaPetSubsystem* P = Pets();
	if (!P) { return; }
	const FAsdaPetSlotUpEcho& Echo = P->GetLastSlotUpEcho();

	switch (Result)
	{
		case 1:
		{
			NoticeMsgBox(PetCageLoc::kSlotUpOk);       // :5033-5035  101116 «Ячейка увеличена»
			UAsdaInventorySubsystem* IV = Inv();
			UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			if (Net) { Net->SetSelfWeight(Echo.WeightCur, -1, TEXT("6122")); }   // :5037
			// ★★★ ДЕФЕКТ ОРИГИНАЛА Д-ЯЧ (найден волной 6, в прежних материалах его нет).
			//   Эталон чистит ячейку ТОЛЬКО при `слот > 0` — СТРОГО больше нуля
			//   (`if (BYTE2(v7[1]) == 2 && *(__int16*)((char*)&v7[1] + 3) > 0)`, item_enhance.c:5038,
			//   и такая же строка для сумки 1 на :5040). Расширитель, лежащий в ЯЧЕЙКЕ 0, сервер
			//   спишет, а клиент оставит его нарисованным до следующей полной перерисовки сумки.
			//   Контроль вторым методом: соседние приёмники того же семейства такой проверки НЕ
			//   имеют — 6109 чистит по `LOBYTE(refineResult[3]) == 2` без сравнения слота
			//   (network.c:25937), 6103 — тоже (npc_mob_pet.c:5098-5110). Расходится только 6122.
			//   ⇒ ИСПРАВЛЯЕМ (закон `fix-original-defects`): чистим и ячейку 0. Ключ отката к
			//   поведению оригинала — `asda.Pet.SlotUpLegacySkipCell0 1`.
			const bool bLegacySkipZero = CVarSlotUpLegacySkipCell0.GetValueOnGameThread() != 0;
			const bool bCellOk = bLegacySkipZero ? (Echo.ItemSlot > 0) : (Echo.ItemSlot >= 0);
			if ((Echo.ItemBag == kShellContainer || Echo.ItemBag == kSpendContainer) && bCellOk)
			{
				if (IV) { IV->ClearCell(Echo.ItemBag, Echo.ItemSlot); }   // :5038 / :5040
			}
			else if (Echo.ItemSlot == 0 && bLegacySkipZero)
			{
				UE_LOG(LogAsdaPetCage, Warning,
					TEXT("[питомец-окно] 6122 Д-ЯЧ: расширитель лежал в ячейке 0 сумки %d — ")
					TEXT("оригинал её НЕ чистит (item_enhance.c:5038), ключ asda.Pet.SlotUpLegacySkipCell0 = 1"),
					Echo.ItemBag);
			}
			break;
		}
		case 2:
			NoticeMsgBox(PetCageLoc::kSlotUpNone);     // :5044-5046  101114 «Нет увеличителя»
			break;
		case 3:
			NoticeMsgBox(PetCageLoc::kSlotUpMax);      // :5048-5050  101115 «Ячейка максимально увеличена»
			break;
		default:
			// У эталона switch на трёх ветках; прочие коды молчат (`case`-ов нет).
			UE_LOG(LogAsdaPetCage, Log,
				TEXT("[питомец-окно] 6122 код %d — веток у эталона нет, нотиса нет"), Result);
			break;
	}
	UE_LOG(LogAsdaPetCage, Log,
		TEXT("[питомец-окно] 6122 код=%d ячеек=%d расширитель=(сумка %d, ячейка %d) вес=%d"),
		Result, Echo.NewCount, Echo.ItemBag, Echo.ItemSlot, Echo.WeightCur);
}

// Общая точка доступа к инвентарю (сумка — его собственность, своей копии не заводим).
UAsdaInventorySubsystem* UAsdaPetCageSubsystem::Inv() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
}

// ═══════════════ ★ ВОЛНА 6: СТЕНДЫ ОТПРАВКИ 6108 И 6121 ══════════════════════════════════════
// Образец — стенд 6148 волны 4 и стенд 6112 волны 5. ★ ЗАМЕЧАНИЕ РЕВИЗИИ ВОЛНЫ 5 УЧТЕНО:
//   `charId` входит в БУЛЕВ ВЕРДИКТ, а не только печатается. Это ровно то поле, по которому
//   сервер молча выбрасывает пакет (`if (*(_DWORD*)v12 != *(_DWORD*)(result + 372))` →
//   лог и выход, GameServer.c:35534-35538 для 6108 и :36440-36442 для 6121), то есть закон
//   `charid-vs-usernum`. Ширины полей сверены с СЕРВЕРНЫМ чтением, а не с нашим сборщиком:
//     6108 (11 б): +0 i32 charId, +4 i32 petUid (сервер сравнивает DWORD-ом, :35597 —
//                  обрезания до 16 бит, как у материалов 6112, здесь НЕТ),
//                  +8 u8 сумка (`(unsigned __int8)v12[8]`), +9 i16 ячейка (`*(__int16*)&v12[9]`);
//     6121 (7 б):  +0 i32 charId, +4 u8 сумка (`(unsigned __int8)v7[4]`),
//                  +5 i16 ячейка (`*(__int16*)&v7[5]`).
void UAsdaPetCageSubsystem::StartFeedStand(bool bFromWindowSlot, int32 TestCharId)
{
	UAsdaPetSubsystem* P = Pets();
	UAsdaInventorySubsystem* IV = Inv();
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!P || !IV || !IT) { return; }
	if (bStandArmed)
	{
		UE_LOG(LogAsdaPetCage, Warning, TEXT("[питомец-окно] СТЕНД уже взведён — повторный запуск игнорирую"));
		return;
	}
	// Ищем предмет ТЕМ ЖЕ правилом, каким его ищет боевой путь:
	//   путь «из сумки» — корм `item_list.itemSubType == 106` (item_inventory.c:37885);
	//   путь «кнопка окна» — пилюля `item_list.itemSubType == 105` (`invFindItemByType105`,
	//   item_inventory.c:54596: 60 ячеек контейнера 2).
	const int32 WantSub = bFromWindowSlot ? 105 : kFeedSubType;
	int32 Bag = -1, Cell = -1;
	for (int32 Cont = 2; Cont >= 1 && Cell < 0; --Cont)
	{
		const int32 N = IV->GetContainerSize(Cont);   // ★ [ПОРТ] 29.10.2026 (инвентарь ×2): было `(Cont == 2) ? 60 : 30`
		for (int32 i = 0; i < N; ++i)
		{
			const FAsdaItemInstance* It = IV->GetCell(Cont, i);
			if (!It || It->IsEmpty()) { continue; }
			const FAsdaItemRecord* Rec = IT->GetItem(It->ItemCode);
			if (Rec && Rec->ItemSubType == WantSub) { Bag = It->ContainerType; Cell = It->SlotIndex; break; }
		}
	}
	if (Cell < 0)
	{
		UE_LOG(LogAsdaPetCage, Error,
			TEXT("[питомец-окно] СТЕНД 6108: в сумке нет предмета подтипа %d — сперва `asda.Pet.FeedFill`"), WantSub);
		return;
	}
	// Ожидания снимаем ДО отправки — ровно по тем же правилам, по которым их считает модель.
	const FAsdaPetSlot* Act = Active();
	const FAsdaPetSlot* Sel = Selected();
	const FAsdaPetSlot* Who = bFromWindowSlot ? Sel : Act;
	if (!Who)
	{
		UE_LOG(LogAsdaPetCage, Error,
			TEXT("[питомец-окно] СТЕНД 6108 (%s): нужного питомца нет (%s). Пакет не уйдёт — это ГЕЙТ оригинала, а не сбой стенда"),
			bFromWindowSlot ? TEXT("кнопка окна") : TEXT("из сумки"),
			bFromWindowSlot ? TEXT("никто не ВЫБРАН") : TEXT("никто не ПРИЗВАН, ждите нотис 101105"));
		return;
	}
	// ★★★ ВОЛНА 8 (пункт 9 ревизии): ЧУЖОЙ СТЕНД УЖЕ ДЕРЖИТ ПЕРЕХВАТ.
	//   Перехватчик исходящих ОДИН на весь клиент. Без этой проверки второе окно
	//   взводило свой стенд, не получало тела (`Begin` второй раз игнорируется),
	//   печатало ЛОЖНЫЙ вердикт «тело НЕ СНЯТО» и по таймауту звало `EndSendCapture`,
	//   снимая перехват ПЕРВОГО — то есть ломало и свой вердикт, и чужой.
	if (P->IsSendCaptureArmed())
	{
		UE_LOG(LogAsdaPetCage, Error,
			TEXT("[питомец-окно] СТЕНД 6108 НЕ ЗАПУЩЕН: перехват исходящих уже держит ДРУГОЙ стенд. ")
			TEXT("Дождитесь его завершения либо снимите его — два стенда разом дают ложный вердикт."));
		return;
	}
	StandOpcode     = OP_PET_FEED_REQ;
	StandExpectUid  = Who->UniqueId;
	StandExpectBag  = Bag;
	StandExpectSlot = Cell;
	bStandDone  = false;
	bStandArmed = true;
	StandLeft   = 5.f;
	P->BeginSendCapture([this](int32 Op, const TArray<uint8>& Body, bool /*bPad*/, TCHAR Env)
	{
		(void)CheckStandBody(Op, Body, Env);
		bStandDone = true;
	}, TestCharId);

	// ★ БОЕВОЙ ПУТЬ. Из сумки — ровно тот же вызов, что делает двойной клик игрока
	//   (`UAsdaInventorySubsystem::UseByDoubleClick` → ветка `ItemSubType == 106`).
	//   Кнопкой окна — второй путь эталона; его развилку (подтверждение msgbox 56) делает
	//   `OnPanelCall`, и она проверена волной 2 живым нажатием; здесь проверяется ИМЕННО
	//   СБОРКА ПАКЕТА, поэтому зовём то, что стоит за «ОК» подтверждения.
	if (bFromWindowSlot) { (void)P->RequestFeed(Bag, Cell, /*bFromWindowSlot*/ true); }
	else                 { (void)IV->UseByDoubleClick(Bag, Cell); }

	UE_LOG(LogAsdaPetCage, Log,
		TEXT("[питомец-окно] СТЕНД 6108 запущен (%s): ждём тело (pet=%d, предмет=(%d,%d), charId=%d)"),
		bFromWindowSlot ? TEXT("кнопка окна, useEnhanceSlot=1") : TEXT("из сумки, useEnhanceSlot=0"),
		StandExpectUid, Bag, Cell, TestCharId);
}

void UAsdaPetCageSubsystem::StartSlotUpStand(int32 TestCharId)
{
	UAsdaPetSubsystem* P = Pets();
	UAsdaInventorySubsystem* IV = Inv();
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!P || !IV || !IT) { return; }
	if (bStandArmed)
	{
		UE_LOG(LogAsdaPetCage, Warning, TEXT("[питомец-окно] СТЕНД уже взведён — повторный запуск игнорирую"));
		return;
	}
	// Расширитель — `special_itemlist.subType == 104` (item_inventory.c:38108).
	int32 Bag = -1, Cell = -1;
	for (int32 Cont = 2; Cont >= 1 && Cell < 0; --Cont)
	{
		const int32 N = IV->GetContainerSize(Cont);   // ★ [ПОРТ] 29.10.2026 (инвентарь ×2): было `(Cont == 2) ? 60 : 30`
		for (int32 i = 0; i < N; ++i)
		{
			const FAsdaItemInstance* It = IV->GetCell(Cont, i);
			if (!It || It->IsEmpty()) { continue; }
			FAsdaSpecialItemlistRow Sp;
			if (IT->GetSpecialItem(It->ItemCode, Sp) && Sp.SubType == kSlotUpSubType)
			{
				Bag = It->ContainerType; Cell = It->SlotIndex; break;
			}
		}
	}
	if (Cell < 0)
	{
		UE_LOG(LogAsdaPetCage, Error,
			TEXT("[питомец-окно] СТЕНД 6121: в сумке нет расширителя (special_itemlist.subType 104) — сперва `asda.Pet.FeedFill`"));
		return;
	}
	// ★★★ ВОЛНА 8 (пункт 9 ревизии): ЧУЖОЙ СТЕНД УЖЕ ДЕРЖИТ ПЕРЕХВАТ.
	//   Перехватчик исходящих ОДИН на весь клиент. Без этой проверки второе окно
	//   взводило свой стенд, не получало тела (`Begin` второй раз игнорируется),
	//   печатало ЛОЖНЫЙ вердикт «тело НЕ СНЯТО» и по таймауту звало `EndSendCapture`,
	//   снимая перехват ПЕРВОГО — то есть ломало и свой вердикт, и чужой.
	if (P->IsSendCaptureArmed())
	{
		UE_LOG(LogAsdaPetCage, Error,
			TEXT("[питомец-окно] СТЕНД 6121 НЕ ЗАПУЩЕН: перехват исходящих уже держит ДРУГОЙ стенд. ")
			TEXT("Дождитесь его завершения либо снимите его — два стенда разом дают ложный вердикт."));
		return;
	}
	StandOpcode     = OP_PET_SLOT_UP_REQ;
	StandExpectUid  = -1;          // ★ поля питомца в теле 6121 НЕТ вовсе (7 б)
	StandExpectBag  = Bag;
	StandExpectSlot = Cell;
	bStandDone  = false;
	bStandArmed = true;
	StandLeft   = 5.f;
	P->BeginSendCapture([this](int32 Op, const TArray<uint8>& Body, bool /*bPad*/, TCHAR Env)
	{
		(void)CheckStandBody(Op, Body, Env);
		bStandDone = true;
	}, TestCharId);
	// ★ Боевой путь: двойной клик по расширителю в сумке.
	(void)IV->UseByDoubleClick(Bag, Cell);
	UE_LOG(LogAsdaPetCage, Log,
		TEXT("[питомец-окно] СТЕНД 6121 запущен: ждём тело (расширитель=(%d,%d), charId=%d)"),
		Bag, Cell, TestCharId);
}

bool UAsdaPetCageSubsystem::CheckStandBody(int32 Opcode, const TArray<uint8>& Body, TCHAR Envelope) const
{
	// ★ ВОЛНА 8 (дубли, пункт 7): безопасное чтение полей тела — общая точка
	//   `AsdaPetUi::StandI32/StandI16/StandU8` (эти три лямбды были скопированы байт в байт
	//   в два стенда). Сами вердикты не дублируются: у каждого пакета своя раскладка.
	auto I32 = [&Body](int32 O) { return AsdaPetUi::StandI32(Body, O); };
	auto I16 = [&Body](int32 O) { return AsdaPetUi::StandI16(Body, O); };
	auto U8b = [&Body](int32 O) { return AsdaPetUi::StandU8 (Body, O); };
	// ★ Ожидаемый charId читаем ИЗ СЕТИ, а не из своей копии: пока перехват взведён, модель
	//   волны 1 подменяет `UAsdaNetSubsystem::SelectedCharId` тестовым значением
	//   (`BeginSendCapture`), и сборщик тела берёт его же. Так проверка остаётся сверкой
	//   «что в теле» против «что в источнике», а не против собственной переменной стенда.
	const UAsdaNetSubsystem* NetS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const int32 ExpectCharId = NetS ? (int32)NetS->SelectedCharId : 0;

	const bool bFeed  = (StandOpcode == OP_PET_FEED_REQ);
	const int32 ExpectLen = bFeed ? 11 : 7;
	const bool bOp   = (Opcode == StandOpcode);
	const bool bLen  = (Body.Num() == ExpectLen);
	const bool bEnv  = (Envelope == TEXT('S'));          // «с секретом»: 8 QPC + 16 MD5 + тело
	// ★★★ charId — В ВЕРДИКТЕ (замечание ревизии волны 5). Сервер сравнивает его DWORD-ом и при
	//   расхождении ТОЛЬКО пишет в лог и выходит (GameServer.c:35537 / :36441) — пакет пропал бы молча.
	const bool bChar = (I32(0) == ExpectCharId) && (ExpectCharId != 0);
	const bool bUid  = bFeed ? (I32(4) == StandExpectUid) : true;
	const bool bBag  = (U8b(bFeed ? 8 : 4) == StandExpectBag);
	const bool bSlot = (I16(bFeed ? 9 : 5) == StandExpectSlot);
	const bool bAll  = bOp && bLen && bEnv && bChar && bUid && bBag && bSlot;

	// «кадр» — МИНИМУМ БЕЗ ПАДДИНГА (10 заголовка + 24 конверта + тело): отправка идёт с `a6 = 1`,
	//   и хвостовой паддинг дописывает сеть, здесь он не измеряется.
	// ★ Честно: ожидаемые СУМКА/ЯЧЕЙКА взяты тем же поиском, каким ими пользуется путь отправки,
	//   и сверяются сами с собой. Независимо проверена РАСКЛАДКА ТЕЛА (смещения и ширины сняты у
	//   СЕРВЕРА, мимо нашего сборщика) и charId. Чтобы и номера не были тавтологией, в прогонах
	//   предмет кладётся в разные ячейки (см. `asda.Pet.FeedFill <ячейка>`).
	UE_LOG(LogAsdaPetCage, Log,
		TEXT("[питомец-окно] СТЕНД %d: опкод=%d(%s) тело=%d б(%s, ждали %d) конверт=%c(%s) кадр≥%d б (без паддинга)\n")
		TEXT("    +0 charId = %d (ждали %d) %s\n")
		TEXT("    %s\n")
		TEXT("    +%d сумка  = %d (ждали %d) %s\n")
		TEXT("    +%d ячейка = %d (ждали %d) %s\n")
		TEXT("    ИТОГ: %s"),
		StandOpcode,
		Opcode, bOp ? TEXT("OK") : TEXT("ОШИБКА"),
		Body.Num(), bLen ? TEXT("OK") : TEXT("ОШИБКА"), ExpectLen,
		Envelope, bEnv ? TEXT("OK") : TEXT("ОШИБКА"), 10 + 24 + Body.Num(),
		I32(0), ExpectCharId, bChar ? TEXT("OK") : TEXT("ОШИБКА"),
		bFeed ? *FString::Printf(TEXT("+4 petUid = %d (ждали %d) %s"), I32(4), StandExpectUid,
			bUid ? TEXT("OK") : TEXT("ОШИБКА"))
			  : TEXT("поля питомца в теле 6121 НЕТ (7 б) — проверять нечего"),
		bFeed ? 8 : 4, U8b(bFeed ? 8 : 4), StandExpectBag, bBag ? TEXT("OK") : TEXT("ОШИБКА"),
		bFeed ? 9 : 5, I16(bFeed ? 9 : 5), StandExpectSlot, bSlot ? TEXT("OK") : TEXT("ОШИБКА"),
		bAll ? TEXT("ПАКЕТ СОБРАН ВЕРНО") : TEXT("ПАКЕТ СОБРАН НЕВЕРНО"));
	return bAll;
}

// ══════════════════════════════════════ НОТИСЫ ════════════════════════════════════════════════
FString UAsdaPetCageSubsystem::Loc(int32 LocId) const
{
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

void UAsdaPetCageSubsystem::NoticeMsgBox(int32 LocId, int32 EtalonNoticeId) const
{
	// Канал A отчёта §6.1: `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)` —
	//   однокнопочное модальное окно ONEBUT_MSGBOX 1500, кнопка = loc 2288 «ОК».
	//   ★ Чат у питомцев НЕ используется (доказано двумя методами, отчёт §6.1).
	// ★ ВОЛНА 8 (дубли, пункт 7): тело — в общей точке `AsdaPetUi::NoticeMsgBox`
	//   (там же и единственная константа подписи кнопки 2288).
	AsdaPetUi::NoticeMsgBox(GetGameInstance(), LocId, EtalonNoticeId);
}

// ════════════════════════════════════ ДИАГНОСТИКА ═════════════════════════════════════════════
FString UAsdaPetCageSubsystem::DumpControls() const
{
	FString S = TEXT("=== окно питомца: разобранные контролы ===\n");
	auto Line = [&S](const TCHAR* Id, const UObject* W) { S += FString::Printf(TEXT("  %-40s %s\n"), Id, W ? TEXT("есть") : TEXT("НЕТ")); };
	S += TEXT("-- 43000 Pet_Cage_Win --\n");
	Line(TEXT("43001 Pet_Cage_Title"),        TxtTitle);
	Line(TEXT("43002 Pet_Cage_ClsBtn"),       BtnClose);
	Line(TEXT("43005 Pet_Cage_SelTitle_Text"),TxtListTitle);
	for (int32 j = 0; j < kRowContainers; ++j)
	{
		Line(*FString::Printf(TEXT("%d PetCage_ListBase%d_grp"), 43020 + j, j + 1), RowGroups[j]);
	}
	Line(TEXT("43050 Pet_Cage_PetSel"),       SelMarker);
	Line(TEXT("43051 PetCage_DelBtn"),        BtnTrash);
	Line(TEXT("43053 WkBtn_W100_148_txt"),    Txt43053);
	Line(TEXT("43054 RdBtn_W100_153_txt"),    Txt43054);
	Line(TEXT("43061 Btn_CBase_grp"),         Grp43061);
	Line(TEXT("43062 Btn_CBase_grp_1"),       Grp43062);
	Line(TEXT("43063 Pet_Cage_Win_TXT00"),    TxtHint);
	Line(TEXT("43070 Pet_Cage_NameTxt"),      TxtNameLabel);
	Line(TEXT("43071 Pet_Cage_NameInput"),    EdtName);
	Line(TEXT("43072 Pet_Cage_StatTxt5"),     TxtStageLbl);
	Line(TEXT("43073 Pet_Cage_Stat5"),        TxtStage);
	Line(TEXT("43074 Pet_Cage_StatTxt6"),     TxtExpLbl);
	Line(TEXT("43075 Pet_Cage_EXPBar"),       GaugeExp.Track);
	Line(TEXT("43076 Pet_Cage_EXP"),          TxtExp);
	Line(TEXT("43077 Pet_Cage_StatTxt8"),     TxtGrowLbl);
	Line(TEXT("43079 Pet_Cage_Stat8"),        TxtGrow);
	Line(TEXT("43080 Pet_Cage_StatTxt7"),     TxtNutLbl);
	Line(TEXT("43081 Pet_Cage_NutBar"),       GaugeNut.Track);
	Line(TEXT("43082 Pet_Cage_Nut"),          TxtNut);
	Line(TEXT("43083 Pet_Cage_NameBtn"),      BtnName);
	Line(TEXT("43100 Pet_Cage_StatTxt1"),     TxtStat1Lbl);
	Line(TEXT("43101 Pet_Cage_Stat1"),        TxtStat1);
	Line(TEXT("43102 Pet_Cage_StatTxt2"),     TxtStat2Lbl);
	Line(TEXT("43103 Pet_Cage_Stat2"),        TxtStat2);
	Line(TEXT("43104 Pet_Cage_StatTxt3"),     TxtStat3Lbl);
	Line(TEXT("43105 Pet_Cage_Stat3"),        TxtStat3);
	Line(TEXT("43106 Pet_Cage_StatTxt4"),     TxtFuncLbl);
	Line(TEXT("43107 Pet_Cage_Option0"),      TxtOpt0);
	Line(TEXT("43108 Pet_Cage_Option1"),      TxtOpt1);
	Line(TEXT("43109 Pet_Cage_Option2"),      TxtOpt2);
	Line(TEXT("43110 Pet_Cage_StatTxt6_1"),   TxtGradeLbl);
	Line(TEXT("43111 Pet_Cage_Grade"),        TxtGrade);
	Line(TEXT("43112 Pet_Cage_StatTxt5_1"),   TxtFeedLbl);
	Line(TEXT("43113 Pet_Cage_Taste0"),       TxtTaste0);
	Line(TEXT("43114 Pet_Cage_Taste1"),       TxtTaste1);
	Line(TEXT("43115 Pet_Cage_Taste2"),       TxtTaste2);
	int32 Icons = 0;
	for (const FAsdaPetCageCell& C : Cells) { if (C.Icon) { ++Icons; } }
	S += FString::Printf(TEXT("  43140…43169 иконок слотов: %d из %d\n"), Icons, kSlots);
	// ★ Панель 43058 — отдельный виджет, он поднимается ПО ПЕРВОМУ правому клику по слоту.
	//   Пока его нет, «НЕТ» ниже означает «нечего разбирать», а не «контрола не нашлось».
	S += FString::Printf(TEXT("-- 43058 Pet_Button_Win (%s) --\n"),
		Panel ? TEXT("виджет создан") : TEXT("★ ВИДЖЕТ ЕЩЁ НЕ СОЗДАН — поднимается по первому правому клику по слоту"));
	Line(TEXT("43055 Pet_Button_3"),  BtnGrowth);
	Line(TEXT("43056 Pet_Button_4"),  BtnEvolution);
	Line(TEXT("43057 Pet_Button_NO"), BtnPanelNo);
	Line(TEXT("43065 Pet_Button_1"),  BtnCall);
	Line(TEXT("43066 Pet_Button_2"),  BtnRecall);
	S += TEXT("-- 43200 Pet_Status_Win --\n");
	Line(TEXT("43201 PetStatus_Move"),      TxtStatusName);
	Line(TEXT("43202 Box_Black_4_txt"),     TxtStatusStage);
	Line(TEXT("43203 Green_Bar"),           GaugeStatusExp.Track);
	Line(TEXT("43204 Orange_Bar"),          GaugeStatusNut.Track);
	Line(TEXT("43205 PetStatus_Icon_13_0"), ImgStatusIcon);
	return S;
}

FString UAsdaPetCageSubsystem::DumpState() const
{
	const UAsdaPetSubsystem* P = Pets();
	FString S = TEXT("=== окно питомца: состояние ===\n");
	S += FString::Printf(TEXT("  43000 открыто=%s · 43058 видна=%s · 43200 видна=%s\n"),
		bCageOpen ? TEXT("да") : TEXT("нет"), bPanelShown ? TEXT("да") : TEXT("нет"),
		bStatusShown ? TEXT("да") : TEXT("нет"));
	if (!P) { S += TEXT("  модель питомцев не поднята\n"); return S; }
	S += FString::Printf(TEXT("  питомцев в списке %d · ячеек %d · показываем %d · рядов видно %d\n"),
		P->GetSlots().Num(), P->GetPossessionCount(), VisibleCount(),
		FMath::Max(0, P->GetPossessionCount() / kSlotsPerRow));
	S += FString::Printf(TEXT("  выбран uid=%d · призван uid=%d · «6100 в полёте»=%s\n"),
		P->GetSelectedUniqueId(), P->GetActiveUniqueId(),
		P->IsCallRequestInFlight() ? TEXT("да") : TEXT("нет"));
	const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (WM)
	{
		S += FString::Printf(TEXT("  гейты: Г1 создание=%s · Г2 количество=%s · Г3 43500=%s · Г4 44200=%s\n"),
			WM->IsWindowVisible(kGateCraftWindow)   ? TEXT("открыто") : TEXT("нет"),
			WM->IsWindowVisible(kGateQtyWindow)     ? TEXT("открыто") : TEXT("нет"),
			WM->IsWindowVisible(kGateEvolutionWin)  ? TEXT("открыто") : TEXT("нет"),
			WM->IsWindowVisible(kGateLimitBreakWin) ? TEXT("открыто") : TEXT("нет"));
	}
	int32 i = 0;
	for (const FAsdaPetSlot& Sl : P->GetSlots())
	{
		S += FString::Printf(TEXT("   [%02d] uid=%d порода=%d «%s» ур=%d/%d опыт=%d сытость=%d %s\n"),
			i++, Sl.UniqueId, Sl.PetIndex, *Sl.Name, Sl.Level, Sl.MaxLevel, Sl.Exp, Sl.Fullness,
			Sl.bIsActive ? TEXT("ПРИЗВАН") : TEXT(""));
	}
	return S;
}

// ═══════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ═══════════════════════════════════════════
static UAsdaPetCageSubsystem* PetCageFromWorld(UWorld* W)
{
	return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaPetCageSubsystem>() : nullptr;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetWinOpen(
	TEXT("asda.Pet.Window"),
	TEXT("asda.Pet.Window — тумблер окна 43000 «Дом для питомца» (порт CRefineWnd_Open, все 4 гейта)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaPetCageSubsystem* S = PetCageFromWorld(W)) { S->OpenCage(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetWinControls(
	TEXT("asda.Pet.WindowControls"),
	TEXT("asda.Pet.WindowControls — печать КАЖДОГО разобранного контрола 43000/43058/43200."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaPetCageSubsystem* S = PetCageFromWorld(W))
		{
			UE_LOG(LogAsdaPetCage, Log, TEXT("%s"), *S->DumpControls());
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetWinClick(
	TEXT("asda.Pet.WindowClick"),
	TEXT("asda.Pet.WindowClick <слот 0..29> [1] — нажатие по слоту списка: без второго аргумента ")
	TEXT("ЛЕВОЕ (только выбрать, событие 0xB), с «1» — ПРАВОЕ (выбрать и открыть панель 43058, ")
	TEXT("событие 0xE). Зовёт БОЕВЫЕ обработчики, а не пишет в поля (закон diagnostic-must-use-real-path)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaPetCageSubsystem* S = PetCageFromWorld(W);
		if (!S || Args.Num() < 1) { return; }
		const int32 Slot = FCString::Atoi(*Args[0]);
		const bool bRight = (Args.Num() >= 2) && (FCString::Atoi(*Args[1]) != 0);
		if (bRight) { S->OnSlotRightClicked(Slot); } else { S->OnSlotLeftClicked(Slot); }
		UE_LOG(LogAsdaPetCage, Log, TEXT("[питомец-окно] клик по слоту %d, кнопка=%s"),
			Slot, bRight ? TEXT("ПКМ") : TEXT("ЛКМ"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetWinCells(
	TEXT("asda.Pet.WindowCells"),
	TEXT("asda.Pet.WindowCells <6..30> — задать число доступных ячеек СИНТЕЗОМ настоящего тела 6123 ")
	TEXT("и прогоном через боевой приёмник (закон diagnostic-must-use-real-path). Нужно для проверки ")
	TEXT("видимости рядов 43020…43023 при 6/12/18/24/30."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 6123 (число ячеек клетки) — клики по несуществующим ячейкам.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Pet.WindowCells"))) { return; }
		UAsdaPetCageSubsystem* S = PetCageFromWorld(W);
		UAsdaPetSubsystem* P = (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
		if (!S || !P || Args.Num() < 1) { return; }
		const int32 N = FCString::Atoi(*Args[0]);
		TArray<uint8> Body;
		Body.SetNumZeroed(5);
		// Тело 6123: `+0 i32 charId`, `+4 u8 count`. charId подставит сам приёмник через `IsForSelf`,
		//   а до входа в мир свой charId равен нулю и сверка пропускает — этого достаточно.
		Body[4] = (uint8)FMath::Clamp(N, 0, 255);
		P->OnPetPacket(OP_PET_SLOT_COUNT, Body);
		UE_LOG(LogAsdaPetCage, Log, TEXT("[питомец-окно] ячеек задано %d, теперь доступно %d"),
			N, P->GetPossessionCount());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetWinHitTest(
	TEXT("asda.Pet.WindowHitTest"),
	TEXT("asda.Pet.WindowHitTest <x> <y> [1] — найти слот ПО ЖИВОЙ ГЕОМЕТРИИ в точке экрана и ")
	TEXT("нажать по нему (без «1» — левой, с «1» — правой). Идёт тем же хит-тестом и теми же ")
	TEXT("обработчиками, что и покадровый опрос мыши; печатает найденный слот."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaPetCageSubsystem* S = PetCageFromWorld(W);
		if (!S || Args.Num() < 2) { return; }
		const FVector2D Pt(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]));
		const bool bRight = (Args.Num() >= 3) && (FCString::Atoi(*Args[2]) != 0);
		const int32 Slot = S->SlotUnderPoint(Pt);
		UE_LOG(LogAsdaPetCage, Log, TEXT("[питомец-окно] хит-тест (%.0f,%.0f) → слот %d, кнопка=%s"),
			Pt.X, Pt.Y, Slot, bRight ? TEXT("ПКМ") : TEXT("ЛКМ"));
		if (Slot < 0) { return; }
		if (bRight) { S->OnSlotRightClicked(Slot); } else { S->OnSlotLeftClicked(Slot); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetWinPanel(
	TEXT("asda.Pet.WindowPanel"),
	TEXT("asda.Pet.WindowPanel <43065|43066|43055|43056|43057|43067> — нажатие кнопки панели ")
	TEXT("43058 боевым обработчиком (призыв / отзыв / рост / развитие / закрыть / ★ О питомце)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaPetCageSubsystem* S = PetCageFromWorld(W);
		if (!S || Args.Num() < 1) { return; }
		switch (FCString::Atoi(*Args[0]))
		{
			case 43065: S->OnPanelCall();      break;
			case 43066: S->OnPanelRecall();    break;
			case 43055: S->OnPanelGrowth();    break;
			case 43056: S->OnPanelEvolution(); break;
			case 43057: S->OnPanelCancel();    break;
			case 43067: S->OnPanelInfo();      break;   // ★ наш номер (в .JUI его нет)
			default: UE_LOG(LogAsdaPetCage, Warning, TEXT("[питомец-окно] нет такой кнопки панели")); break;
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ═══════════════ ★ ВОЛНА 6: КОМАНДЫ КОРМЛЕНИЯ И РАСШИРЕНИЯ ЯЧЕЕК ═════════════════════════════
static FAutoConsoleCommandWithWorldAndArgs GCmdPetFeedFill(
	TEXT("asda.Pet.FeedFill"),
	TEXT("asda.Pet.FeedFill [ячейка] — положить в сумку корм (item_list subType 106), пилюлю ")
	TEXT("(subType 105) и расширитель ячеек (special_itemlist subType 104). Печатает найденные коды. ")
	TEXT("Номер ячейки меняйте между прогонами, чтобы сверка тела не была тавтологией."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: кладёт поддельные предметы в сумку — 6108/6121 ушли бы на поддельные ячейки.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Pet.FeedFill"))) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaInventorySubsystem* IV = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		if (!IV || !IT) { return; }
		const int32 Base = (Args.Num() >= 1) ? FCString::Atoi(*Args[0]) : 30;
		int32 FeedCode = 0, PillCode = 0, UpCode = 0;
		for (const FAsdaItemRecord& R : IT->GetAllItems())
		{
			if (!FeedCode && R.ItemSubType == 106) { FeedCode = R.ItemCode; }
			if (!PillCode && R.ItemSubType == 105) { PillCode = R.ItemCode; }
			if (FeedCode && PillCode) { break; }
		}
		for (int32 Code = 1; Code <= 600 && !UpCode; ++Code)
		{
			FAsdaSpecialItemlistRow Sp;
			if (IT->GetSpecialItem(Code, Sp) && Sp.SubType == 104) { UpCode = Code; }
		}
		if (FeedCode) { IV->DebugPutItem(2, Base + 0, FeedCode, 5); }
		if (PillCode) { IV->DebugPutItem(2, Base + 1, PillCode, 1); }
		if (UpCode)   { IV->DebugPutItem(2, Base + 2, UpCode,   1); }
		UE_LOG(LogAsdaPetCage, Log,
			TEXT("[питомец-окно] FeedFill: корм 106=%d в (2,%d)×5, пилюля 105=%d в (2,%d), расширитель 104=%d в (2,%d)"),
			FeedCode, Base + 0, PillCode, Base + 1, UpCode, Base + 2);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetFeedStand(
	TEXT("asda.Pet.FeedCapture"),
	TEXT("asda.Pet.FeedCapture [путь 0|1] [charId] — стенд ОТПРАВКИ 6108. 0 (умолчание) — путь «предмет ")
	TEXT("из сумки» (useEnhanceSlot 0, гейт «нужен ПРИЗВАННЫЙ»); 1 — путь «кнопка окна» (useEnhanceSlot 1, ")
	TEXT("гейт «нужен ВЫБРАННЫЙ»). Тело разбирается по СЕРВЕРНЫМ смещениям, charId входит в вердикт."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: стенд перехватывает весь SendSecured на 5 с и подменяет charId; при сбое перехвата ушёл бы настоящий 6108.
		static const uint16 kGuardOps[] = { 6108 };
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Pet.FeedCapture"), 10.f, MakeArrayView(kGuardOps))) { return; }
		if (UAsdaPetCageSubsystem* S = PetCageFromWorld(W))
		{
			const bool bWnd = (Args.Num() >= 1) && (FCString::Atoi(*Args[0]) != 0);
			S->StartFeedStand(bWnd, (Args.Num() >= 2) ? FCString::Atoi(*Args[1]) : 777);
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetSlotUpStand(
	TEXT("asda.Pet.SlotUpCapture"),
	TEXT("asda.Pet.SlotUpCapture [charId] — стенд ОТПРАВКИ 6121 (двойной клик по расширителю ")
	TEXT("special_itemlist subType 104). Тело 7 б: charId i32, сумка u8, ячейка i16."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: стенд перехватывает весь SendSecured и подменяет charId; при сбое перехвата ушёл бы настоящий 6121.
		static const uint16 kGuardOps[] = { 6121 };
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Pet.SlotUpCapture"), 10.f, MakeArrayView(kGuardOps))) { return; }
		if (UAsdaPetCageSubsystem* S = PetCageFromWorld(W))
		{
			S->StartSlotUpStand((Args.Num() >= 1) ? FCString::Atoi(*Args[0]) : 777);
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetFeedResult(
	TEXT("asda.Pet.FeedResult"),
	TEXT("asda.Pet.FeedResult <код 1..7> [uid] [сытость] [вес] [сумка] [ячейка] [кодПредмета] [штук] — ")
	TEXT("синтезировать НАСТОЯЩЕЕ тело 6109 (89 б) и скормить его БОЕВОМУ приёмнику ")
	TEXT("UAsdaPetSubsystem::OnPetPacket. `штук` = 0 ⇒ ветка «ячейка чистится», > 0 ⇒ «запись кладётся»."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 6109: при штук>0 кладёт запись предмета в ячейку из аргументов — следующий Inven.Use/Move ушёл бы по ней.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Pet.FeedResult"))) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaPetSubsystem* P = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
		UAsdaNetSubsystem* N = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!P || Args.Num() < 1) { return; }
		const int32 Code = FCString::Atoi(*Args[0]);
		const int32 Uid  = (Args.Num() >= 2) ? FCString::Atoi(*Args[1]) : P->GetSelectedUniqueId();
		const int32 Full = (Args.Num() >= 3) ? FCString::Atoi(*Args[2]) : 100;
		const int32 Wgt  = (Args.Num() >= 4) ? FCString::Atoi(*Args[3]) : 0;
		const int32 Bag  = (Args.Num() >= 5) ? FCString::Atoi(*Args[4]) : 2;
		const int32 Cell = (Args.Num() >= 6) ? FCString::Atoi(*Args[5]) : 30;
		const int32 ICod = (Args.Num() >= 7) ? FCString::Atoi(*Args[6]) : 0;
		const int32 Qty  = (Args.Num() >= 8) ? FCString::Atoi(*Args[7]) : 0;
		// ★ волна 8: «1» девятым аргументом ⇒ заполнить ВСЮ 57-байтовую запись узором.
		const bool bFullRec = (Args.Num() >= 9) && (FCString::Atoi(*Args[8]) != 0);
		constexpr int32 IOfs = 8;   // Item(57) в теле 6109 начинается с +8 (network.c:25951)

		// Тело 89 б — раскладка из шапки `FAsdaPetFeedEcho`.
		TArray<uint8> Body;
		Body.SetNumZeroed(89);
		AsdaBytes::WrI32(Body.GetData() + 0, N ? (int32)N->SelectedCharId : 0);   // +0  charId
		AsdaBytes::WrI32(Body.GetData() + 4, Uid);                                // +4  petUid
		AsdaBytes::WrI32(Body.GetData() + 8, ICod);                               // +8  Item+0 код
		Body[12] = (uint8)FMath::Clamp(Bag, 0, 255);                              // +12 Item+4 сумка
		AsdaBytes::WrI16(Body.GetData() + 13, (int16)Cell);                       // +13 Item+5 ячейка
		AsdaBytes::WrI32(Body.GetData() + 17, Qty);                               // +17 Item+9 количество
		// ★★★ ВОЛНА 8 (блокер-2): ОСТАЛЬНЫЕ 53 БАЙТА ЗАПИСИ.
		//   Стенд писал в тело ТОЛЬКО четыре поля (код/сумка/ячейка/количество),
		//   поэтому потерю остальных байт он в принципе не мог показать — в ячейке они были
		//   нулями и ДО, и ПОСЛЕ. Теперь стенд кладёт УЗНАВАЕМЫЙ узор во все поля,
		//   которые терял `DebugPutItem`, и `asda.Inven.Model` печатает их строкой `ToLine()`.
		//   Раскладка — `FAsdaItemInstance::SyncFromRaw` (`AsdaInventorySubsystem.cpp:324-347`).
		if (bFullRec && Qty > 0)
		{
			AsdaBytes::WrI16(Body.GetData() + IOfs + 7,  1);      // +7  второй индекс
			Body[IOfs + 13] = 77;                                  // +13 ПРОЧНОСТЬ
			AsdaBytes::WrI16(Body.GetData() + IOfs + 14, 55);      // +14 вес стопки
			AsdaBytes::WrI16(Body.GetData() + IOfs + 16, 201);     // +16 гнездо самоцвета 0
			AsdaBytes::WrI16(Body.GetData() + IOfs + 18, 202);     // +18 гнездо 1
			AsdaBytes::WrI16(Body.GetData() + IOfs + 20, 203);     // +20 гнездо 2
			AsdaBytes::WrI16(Body.GetData() + IOfs + 22, 204);     // +22 гнездо 3
			Body[IOfs + 24] = 6;                                   // +24 ранг самоцвета
			AsdaBytes::WrI16(Body.GetData() + IOfs + 27, 88);      // +27 процент заточки
			for (int32 e = 0; e < 5; ++e)                          // +29 пять пар заточки
			{
				AsdaBytes::WrI16(Body.GetData() + IOfs + 29 + 4 * e, (int16)(310 + e));
				AsdaBytes::WrI16(Body.GetData() + IOfs + 31 + 4 * e, (int16)(410 + e));
			}
		}
		Body[65] = (uint8)FMath::Clamp(Code, 0, 255);                             // +65 код ответа
		Body[66] = (uint8)FMath::Clamp(Full, 0, 255);                             // +66 сытость
		AsdaBytes::WrI32(Body.GetData() + 67, Wgt);                               // +67 вес
		for (int32 j = 0; j < 3; ++j)                                             // +71 три бонуса
		{
			AsdaBytes::WrI16(Body.GetData() + 71 + 6 * j, (int16)(j + 1));
			AsdaBytes::WrI32(Body.GetData() + 73 + 6 * j, 10 * (j + 1));
		}
		P->OnPetPacket(OP_PET_FEED_RESULT, Body);
		UE_LOG(LogAsdaPetCage, Log,
			TEXT("[питомец-окно] синтез 6109: код=%d uid=%d сытость=%d вес=%d корм=(код %d, сумка %d, ячейка %d, штук %d), полная запись=%s"),
			Code, Uid, Full, Wgt, ICod, Bag, Cell, Qty, bFullRec ? TEXT("да") : TEXT("нет"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetSlotUpResult(
	TEXT("asda.Pet.SlotUpResult"),
	TEXT("asda.Pet.SlotUpResult <код 1..3> [ячеек] [сумка] [ячейка] [вес] — синтезировать НАСТОЯЩЕЕ ")
	TEXT("тело 6122 (13 б) и скормить его БОЕВОМУ приёмнику UAsdaPetSubsystem::OnPetPacket."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 6122 меняет число ячеек и расход предмета локально.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Pet.SlotUpResult"))) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaPetSubsystem* P = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
		UAsdaNetSubsystem* N = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!P || Args.Num() < 1) { return; }
		const int32 Code  = FCString::Atoi(*Args[0]);
		const int32 Count = (Args.Num() >= 2) ? FCString::Atoi(*Args[1]) : 12;
		const int32 Bag   = (Args.Num() >= 3) ? FCString::Atoi(*Args[2]) : 2;
		const int32 Cell  = (Args.Num() >= 4) ? FCString::Atoi(*Args[3]) : 32;
		const int32 Wgt   = (Args.Num() >= 5) ? FCString::Atoi(*Args[4]) : 0;

		// Тело 13 б — раскладка из шапки `FAsdaPetSlotUpEcho`.
		TArray<uint8> Body;
		Body.SetNumZeroed(13);
		AsdaBytes::WrI32(Body.GetData() + 0, N ? (int32)N->SelectedCharId : 0);   // +0 charId
		Body[4] = (uint8)FMath::Clamp(Code, 0, 255);                              // +4 код
		Body[5] = (uint8)FMath::Clamp(Count, 0, 255);                             // +5 новое число ячеек
		Body[6] = (uint8)FMath::Clamp(Bag, 0, 255);                               // +6 сумка
		AsdaBytes::WrI16(Body.GetData() + 7, (int16)Cell);                        // +7 ячейка
		AsdaBytes::WrI32(Body.GetData() + 9, Wgt);                                // +9 вес
		P->OnPetPacket(OP_PET_SLOT_UP_RESULT, Body);
		UE_LOG(LogAsdaPetCage, Log,
			TEXT("[питомец-окно] синтез 6122: код=%d ячеек=%d расширитель=(%d,%d) вес=%d"),
			Code, Count, Bag, Cell, Wgt);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★★ ВЫГРУЗКА САМОГО БУФЕРА ПРЕВЬЮ В PNG (24.09.2026).
//   ЗАЧЕМ ОТДЕЛЬНО ОТ КАДРА ЭКРАНА. Кадр экрана показывает КОНЕЦ цепочки «капчер → RT → кисть →
//   контрол 43052». Если на нём пусто, виноват может быть любой её участок, и гадать нельзя
//   (закон `false-negatives-method`). Выгрузка буфера разрезает цепочку ровно пополам: если в
//   PNG буфера модель ЕСТЬ, а на экране её нет — виновата кисть/контрол; если и в буфере пусто —
//   капчер или сцена. Именно эта команда и развела две беды 24.09.2026: модель в буфер попала,
//   а до контрола кадр не дошёл.
// * ПОДБОР БАЗОВОГО РАЗВОРОТА ПРЕВЬЮ. Число `FacingYawDeg` - ЧИСЛО ПОРТА: в эталоне угол 0 = базовая
//   поза меша, а позиция глаза камеры потеряна в инлайн-asm, так что вывести «лицом» из кода
//   нельзя (разбор - у `AAsdaPetPreviewActor::FacingYawDeg`). Значит значение СНИМАЕТСЯ ЗАМЕРОМ:
//   эта команда крутит базу, прогон снимает кадры, нужный угол выбирается по картинке.
static FAutoConsoleCommandWithWorldAndArgs GCmdPetPreviewFaceYaw(
	TEXT("asda.Pet.PreviewFaceYaw"),
	TEXT("asda.Pet.PreviewFaceYaw <градусы> - базовый разворот модели в превью 43052/43576 (подбор числа порта)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaPetCageSubsystem* S = PetCageFromWorld(W);
		if (!S || Args.Num() < 1) { UE_LOG(LogAsdaPetCage, Warning, TEXT("нужно: asda.Pet.PreviewFaceYaw <градусы>")); return; }
		S->SetPreviewFacingYaw(FCString::Atof(*Args[0]));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetPreviewDumpRt(
	TEXT("asda.Pet.PreviewDumpRt"),
	TEXT("asda.Pet.PreviewDumpRt <имя> — выгрузить САМ буфер превью 43052 в <Saved>/Screenshots/<имя>.png."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaPetCageSubsystem* S = PetCageFromWorld(W);
		if (!S || Args.Num() < 1) { UE_LOG(LogAsdaPetCage, Warning, TEXT("нужно: asda.Pet.PreviewDumpRt <имя>")); return; }
		UTextureRenderTarget2D* Rt = S->GetPreviewRenderTarget();
		if (!Rt) { UE_LOG(LogAsdaPetCage, Warning, TEXT("[питомец-окно] буфера превью нет")); return; }
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("Screenshots");
		UKismetRenderingLibrary::ExportRenderTarget(W, Rt, Dir, Args[0] + TEXT(".png"));
		UE_LOG(LogAsdaPetCage, Display, TEXT("[питомец-окно] буфер превью выгружен: %s/%s.png"), *Dir, *Args[0]);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetPreviewState(
	TEXT("asda.Pet.PreviewState"),
	TEXT("asda.Pet.PreviewState — превью 43052: сцена, порода, буфер, кисть, угол вращения."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaPetCageSubsystem* S = PetCageFromWorld(W))
		{
			UE_LOG(LogAsdaPetCage, Log, TEXT("%s"), *S->DumpPreview());
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetWinState(
	TEXT("asda.Pet.WindowState"),
	TEXT("asda.Pet.WindowState — состояние окна: список, выбранный, призванный, ячейки, гейты."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaPetCageSubsystem* S = PetCageFromWorld(W))
		{
			UE_LOG(LogAsdaPetCage, Log, TEXT("%s"), *S->DumpState());
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★ [ПОРТ] 21.10.2026 САМОТЕСТ ПРАВИЛА КЛИЧЕК `asda.Pet.SelfTestNameRule [quit]` (без сети и окна):
//   1) `IsLatinPetName` — латиница/цифры да; кириллица, арабица, пустая — нет;
//   2) серверная копия таблицы пород `DT_PetInfo` (из `PetInfo_Table.txt`): у пород 1…31 клички по умолчанию латинские и совпадают
//      с английскими названиями пород клиента (`DT_Loc_pettable_en`, ключ `<idx>.Name`) — так задано на сервере 21.10.2026;
//   3) гейт окна «кличка = по умолчанию» берёт имя из этой таблицы: «Tiga» у породы 13 — по умолчанию, «Aizen» — нет.
static FAutoConsoleCommandWithWorldAndArgs GCmdPetNameRule(
	TEXT("asda.Pet.SelfTestNameRule"),
	TEXT("[ПОРТ] Самотест правила кличек питомцев: только латиница; клички пород по умолчанию из серверной таблицы (DT_PetInfo). [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		int32 Ok = 0, Fail = 0;
		auto Check = [&](bool b, const FString& What)
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaPetCage, Warning, TEXT("САМОТЕСТ-КЛИЧКИ %s %s"), b ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
		};
		Check(IsLatinPetName(TEXT("Tiga2")) && IsLatinPetName(TEXT("Aizen")), TEXT("латиница и цифры — можно"));
		Check(!IsLatinPetName(TEXT("Барсик")) && !IsLatinPetName(TEXT("قطة")) && !IsLatinPetName(TEXT("")) && !IsLatinPetName(TEXT("TigaЖ")),
			TEXT("кириллица, арабица, пустая, смешанная — нельзя"));
		const UAsdaPetSubsystem* PS = (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
		// ★ 29.10.2026: 32 строки — на сервере добавлена порода 31 «SnowLion» (яйцо 76338; build_pet31.py), как у клиентской pettable.
		Check(PS && PS->GetServerPetKindCount() == 32, FString::Printf(TEXT("серверная таблица пород загружена: %d строк (ждали 32)"), PS ? PS->GetServerPetKindCount() : -1));
		int32 Bad = 0;
		for (int32 Idx = 1; PS && Idx <= 31; ++Idx)
		{
			const FAsdaPetInfoRow* R = PS->GetPetInfo(Idx);
			FString En;
			const bool bEn = FAsdaLang::FindOverlay(EAsdaLang::En, TEXT("pettable"), FString::Printf(TEXT("%d.Name"), Idx), En);
			const bool bGood = R && IsLatinPetName(R->DefaultName.Replace(TEXT("-"), TEXT(""))) && bEn && R->DefaultName == En;
			if (!bGood)
			{
				++Bad;
				UE_LOG(LogAsdaPetCage, Warning, TEXT("САМОТЕСТ-КЛИЧКИ   порода %d: сервер «%s», английское название «%s»"), Idx,
					R ? *R->DefaultName : TEXT("—"), bEn ? *En : TEXT("—"));
			}
		}
		Check(Bad == 0, FString::Printf(TEXT("клички по умолчанию пород 1…31 — латинские и = английским названиям пород; расхождений %d"), Bad));
		const FAsdaPetInfoRow* T13 = PS ? PS->GetPetInfo(13) : nullptr;
		Check(T13 && T13->DefaultName == TEXT("Tiga") && T13->DefaultName != TEXT("Aizen"),
			FString::Printf(TEXT("гейт окна: порода 13 по умолчанию «%s» (ждали Tiga); «Aizen» — не по умолчанию"), T13 ? *T13->DefaultName : TEXT("—")));
		AsdaSelfTestKit::Finish(W, TEXT("asda.Pet.SelfTestNameRule"), KA.bQuit, LogAsdaPetCage, TEXT("САМОТЕСТ-КЛИЧКИ"), Ok, Fail, TEXT(""), true);
	}));
#endif // ASDA_DEV_CONSOLE
