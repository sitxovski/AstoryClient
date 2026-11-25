// Copyright (c) 2026 @sitxovski (Discord)
// ★ 23.10.2026 РАЗНОС ФАЙЛА (слово владельца «огромные файлы разделить … с гарантией 100 %»): методы UAsdaInventorySubsystem
//   разнесены по группам в AsdaInventorySubsystem_{Net,Use,View,Debug}.cpp; общее (LogAsdaInven, чистые константы и хелперы) —
//   в AsdaInventorySubsystemInternal.h. Строки перенесены БАЙТ В БАЙТ:
//   `python Tools/asda_split_cpp.py verify --plan Reports/split_2026-10-23/AsdaInventorySubsystem.plan.json`.
//   Здесь осталось ядро: FAsdaItemInstance, жизненный цикл, разбор дерева виджетов, окно, вкладки и прокрутка, модель, автосортировка (её ключ LegacyNoAutoArrange читает и SetupControls).
// UAsdaInventorySubsystem — реализация ядра инвентаря (окно `INVEN_WIN` 400 + модель контейнеров + сеть).
//   Полный разбор эталона — в шапке `AsdaInventorySubsystem.h`; источники: `Reports/inventory_re_A.md`
//   (структура/UI), `Reports/inventory_re_B.md` (данные/сеть); отчёт — `Reports/inventory_port.md`.
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой

#include "UI/AsdaShowOffSubsystem.h"   // ПКМ по ячейке → меню «похвастаться» (28000)
#include "UI/AsdaHudSubsystem.h"       // ★ анонс редкой добычи из КОРОБКИ (NotifyItemInfo → 6616), наше добавление
// ★ Волна 1 «применение предметов»: эффект/звук по `linkCode` и состояния 265/266 из 5029 —
//   через подсистему эффектов (своих копий спавна и поиска актора не заводим, [[port-shared-points]]).
#include "UI/AsdaSystemMenuSubsystem.h"
// ★ Только ради ОДНОЙ статической таблицы цветов заточки (`itemGetGradeColor@0xD29940`) — второй
//   копии палитры не заводим (закон «аудит дублей»). Сам файл тултипа отсюда не правится.
#include "UI/AsdaFishBookSubsystem.h"      // ★ книга рыбака 74000: использование книги-предмета (подтип 210) → диалог 79
#include "Npc/AsdaNpcDialogSubsystem.h"     // ★ обмен по таблице событий (двойной клик по предмету subType 202 → 6098)
#include "Pet/AsdaPetSubsystem.h"          // ★ волна 6: корм (subType 106 → 6108) и расширитель ячеек (104 → 6121)
#include "UI/AsdaSkillUseSubsystem.h"      // ★ волна 2: выбранная цель для поля @0 запроса 6096
#include "UI/AsdaPackagePanelSubsystem.h"  // ★ панель состава пакета 26500 (двойной клик открывает ЕЁ)
#include "UI/AsdaBoosterPanelSubsystem.h"  // ★ окно барабана гачи 10050 (двойной клик открывает ЕГО)
#include "UI/AsdaPremiumShopSubsystem.h"   // ★ 29.09.2026: `g_pPartyTargetObj` = элитный магазин (гейты №15/№15b лопат)
#include "World/AsdaWorldSubsystem.h"

// ★ Явно, а не «через unity»: файл зовёт `GI->GetSubsystem<>()` больше сотни раз. Без этого include
//   одиночная сборка файла (`Build.bat … -SingleFile=…`) не проходит — а она единственный способ
//   проверить компиляцию, пока в редакторе активен Live Coding и полная сборка запрещена.
#include "UI/AsdaScrollCallSubsystem.h"     // ★ волна 2-Г: свитки перемещения/призыва (окно 1552)
#include "AsdaInventorySubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaInventorySubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaInven);   // ★ 23.10.2026 разнос: было DEFINE_LOG_CATEGORY_STATIC; объявление — AsdaInventorySubsystemInternal.h (один экземпляр на все части)

// ★★★ [ПОРТ] 29.10.2026 (инвентарь ×2, решение владельца): СУМКА 60 ЯЧЕЕК.
//   Сервер P96 (`[P96_InventoryX2]`) держит контейнер 1 на 60 ячеек и шлёт слоты 30..59 в 4045/5001/5003/…/6570/6572.
//   1 (умолч.) — ёмкость сумки 60 (`kShellSlots`); 0 — прежние 30 (эталон, старый сервер): ячейки 30..59 сетки остаются
//   пустыми и недоступными — `GetContainerSize(1)` = 30, записи сервера в 30..59 не принимаются, перенос/свободная ячейка
//   туда не идут (`MoveItem`/`FindFreeSlot` считают по размеру контейнера). Окно 12×5 — от ассета, ключ его не трогает.
static ASDA_DEV_CVAR(int32, CVarInvenShellX2,
	TEXT("asda.Inven.ShellX2"),
	1,
	TEXT("Сумка (контейнер 1): 1 = 60 ячеек (инвентарь x2, сервер P96); 0 = прежние 30 ячеек (старый сервер) — ячейки 30..59 пустые и недоступные."),
	ECVF_Default);

int32 UAsdaInventorySubsystem::GetShellCapacity()
{
	return (CVarInvenShellX2.GetValueOnGameThread() != 0) ? kShellSlots : kShellSlotsLegacy;
}

// ═══════════════════════ КЛЮЧИ КОНСОЛИ: ВОЗВРАТ ДЕФЕКТОВ ОРИГИНАЛА ═════════════════════════════
//   Закон `fix-original-defects`: дефект чиним, но оставляем ключ «как было» для сверки с эталоном.
ASDA_DEV_CVAR(int32, CVarLegacyNoAutoArrange,
	TEXT("asda.Inven.LegacyNoAutoArrange"),
	0,
	TEXT("Автосортировка инвентаря: 0 = кнопка 80400 и пункты 80411-80413 РАБОТАЮТ (Д1, решение юзера); "
	     "1 = как в эталонной сборке — контролы скрыты и обработчика нет (живой код висел на id "
	     "мёртвого окна 83671/83691-83693, т.е. автосортировка была недостижима)."),
	ECVF_Default);

void FAsdaItemInstance::Clear()
{
	// ★★★ ПОПРАВКА РЕВИЗИИ ВОЛНЫ 8 (среднее 6): ЗДЕСЬ БЫЛ ВРУЩИЙ КОММЕНТАРИЙ.
	//   Было написано: «Эталон `CSlot_Init@0x974330`: вся запись нулями, кроме +5 и +7 = −1».
	//   ЭТО НЕВЕРНО. Эталон (`ui_core.c:1287-1307`) кладёт −1 в ШЕСТНАДЦАТЬ полей.
	//   ★ ПЕРЕСЧЁТ ПО ЗАМЕЧАНИЮ ФИНАЛЬНОЙ ПРИЁМКИ (мелкое 2): первая редакция этой поправки
	//     сама считала неверно — писала «пятнадцать» и «расхождение на тринадцати». Точный счёт:
	//       +5 и +7 (слот и второй индекс) .................................. 2
	//       +16/+18/+20/+22 (четыре гнезда самоцветов, они же `*((WORD*)this + 8..11)`) ... 4
	//       десять половинок заточки — `for (i = 0; i < 5; ++i)
	//         { *(WORD*)(this + 4*i + 29) = -1; *(WORD*)(this + 4*i + 31) = -1; }` ....... 10
	//       ИТОГО 2 + 4 + 10 = 16.
	//     Нулями эталон забивает остальное (+9 количество, +13 прочность, +14 вес стопки,
	//     +24 ранг, +27 процент и т. д.).
	//   ПОРТ КЛАДЁТ −1 ТОЛЬКО В ДВА ПОЛЯ (+5 и +7) ⇒ расхождение НА ЧЕТЫРНАДЦАТИ полях.
	//   ПОЧЕМУ ЭТО ОСТАВЛЕНО КАК ЕСТЬ, А НЕ ПРИВЕДЕНО К 1:1: все потребители этих полей в порту
	//   проверяют «> 0» либо «!= 0» (гнездо занято, заточка есть), и для них 0 и −1 значат одно
	//   и то же — «пусто». А вот смена значения сломала бы ДИАГНОСТИКУ: `ToLine()` показывает
	//   гнёзда по условию `if (Sowel0 || Sowel1 || Sowel2 || Sowel3)`, и при −1 КАЖДАЯ пустая
	//   запись начала бы печатать «гнёзда=[-1,-1,-1,-1]». Расхождение НАБЛЮДАЕМО нулевое,
	//   поэтому правится комментарий, а не код; решение о приведении к 1:1 — за владельцем.
	Raw.Reset();
	Raw.SetNumZeroed(kRecordSize);
	WrI16(Raw.GetData() + 5, -1);
	WrI16(Raw.GetData() + 7, -1);
	ItemCode = 0; ContainerType = 0; SlotIndex = -1; SecondIndex = -1; Amount = 0;
	Durability = 0; StackWeight = 0;
	Sowel0 = Sowel1 = Sowel2 = Sowel3 = 0; GemGrade = 0; EnchantPercent = 0;
	EnchantCode.Init(0, 5);
	EnchantValue.Init(0, 5);
}

void FAsdaItemInstance::SetRaw(const uint8* Src, int32 Len)
{
	if (!Src || Len < kRecordSize) { Clear(); return; }
	Raw.SetNumUninitialized(kRecordSize);
	FMemory::Memcpy(Raw.GetData(), Src, kRecordSize);
	SyncFromRaw();
}

void FAsdaItemInstance::SyncFromRaw()
{
	if (Raw.Num() < kRecordSize) { Clear(); return; }
	const uint8* P = Raw.GetData();
	ItemCode      = RdI32(P + 0);
	ContainerType = P[4];
	SlotIndex     = RdI16(P + 5);
	SecondIndex   = RdI16(P + 7);
	Amount        = RdI32(P + 9);
	Durability    = P[13];   // ★ +13 = прочность, а не «надет» (миснейм исправлен)
	StackWeight   = RdI16(P + 14);
	Sowel0 = RdI16(P + 16); Sowel1 = RdI16(P + 18); Sowel2 = RdI16(P + 20); Sowel3 = RdI16(P + 22);
	// ★ +24 = уровень заточки (`g_aInvSlots`, 0x2901968 − 0x2901950 = 0x18),
	//   +27 = процент заточки (`g_InvBag_ItemSubId`, 0x290196B − 0x2901950 = 0x1B).
	GemGrade       = P[24];
	EnchantPercent = RdI16(P + 27);
	EnchantCode.SetNum(5); EnchantValue.SetNum(5);
	for (int32 i = 0; i < 5; ++i)
	{
		// +29, шаг 4: i16 код + i16 значение (итератор эталона `iterator(this+29, elem=4, count=5)`).
		EnchantCode[i]  = RdI16(P + 29 + 4 * i);
		EnchantValue[i] = RdI16(P + 31 + 4 * i);
	}
}

void FAsdaItemInstance::SetAmount(int32 V)
{
	if (Raw.Num() < kRecordSize) { Clear(); }
	WrI32(Raw.GetData() + 9, V);
	Amount = V;
}

void FAsdaItemInstance::SetStackWeight(int32 V)
{
	if (Raw.Num() < kRecordSize) { Clear(); }
	WrI16(Raw.GetData() + 14, V);
	StackWeight = V;
}

void FAsdaItemInstance::SetSlotIndex(int32 V)
{
	if (Raw.Num() < kRecordSize) { Clear(); }
	WrI16(Raw.GetData() + 5, V);
	SlotIndex = V;
}

void FAsdaItemInstance::SetContainerType(uint8 V)
{
	if (Raw.Num() < kRecordSize) { Clear(); }
	Raw[4] = V;
	ContainerType = V;
}

FString FAsdaItemInstance::ToLine() const
{
	if (IsEmpty()) { return TEXT("—"); }
	// ★★★ ПОПРАВКА РЕВИЗИИ ВОЛНЫ 8 (среднее 4): ПРОЧНОСТЬ (+13) ТЕПЕРЬ ПЕЧАТАЕТСЯ.
	//   Байт +13 — один из пятидесяти трёх, которые терял `DebugPutItem` (блокер-2 волны 8), и
	//   единственный из них, которого НЕ БЫЛО в этой строке: проверить его живым прогоном было
	//   нечем, «доезжает» подтверждалось только чтением кода (`SyncFromRaw`, `Durability = P[13]`).
	//   Миснейм «надет» у этого байта снят раньше (память `item-record-plus13-durability`).
	FString S = FString::Printf(TEXT("code=%d кол-во=%d вес=%d конт=%d слот=%d(вт.%d) прочность=%d заточка=%d (+%d%%)"),
		ItemCode, Amount, StackWeight, (int32)ContainerType, SlotIndex, SecondIndex,
		(int32)Durability, (int32)GemGrade, EnchantPercent);
	if (Sowel0 || Sowel1 || Sowel2 || Sowel3)
	{
		S += FString::Printf(TEXT(" гнёзда=[%d,%d,%d,%d]"), Sowel0, Sowel1, Sowel2, Sowel3);
	}
	for (int32 i = 0; i < EnchantCode.Num(); ++i)
	{
		if (EnchantCode[i] != 0) { S += FString::Printf(TEXT(" зач%d=%d:%d"), i, EnchantCode[i], EnchantValue[i]); }
	}
	return S;
}

// ══════════════════════════════ МЕЛКИЕ ПОМОЩНИКИ ═══════════════════════════════════════════════
void UAsdaInventorySubsystem::SetVis(UWidget* W, bool bVisible)
{
	if (!W) { return; }
	// SelfHitTestInvisible: мышь ловим ОПРОСОМ (эталон получает готовые события от менеджера окон),
	//   поэтому узлы не должны отбирать клик у мира — за это отвечает зона в UAsdaWindowSubsystem.
	// ★ Волна 1 (14.09.2026, В3): правило видимости — общая точка `AsdaUi::SetShownDecor` (режим тот же).
	AsdaUi::SetShownDecor(W, bVisible);
}

UWidget* UAsdaInventorySubsystem::FindNode(const TCHAR* Exact, const TCHAR* Needle) const
{
	return AsdaUi::FindNodeByName(InvenWidget, Exact, Needle);   // D5: единый поиск «точное → подстрока»
}

void UAsdaInventorySubsystem::Notice(int32 LocId, const TCHAR* Fallback) const
{
	// ★ Волна 1 (14.09.2026, В2): маршрут — фасад `AsdaNotice` (категория та же, поведение то же).
	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): фолбэк необязателен; нет фолбэка и нет строки → плейсхолдер маршрутизатора «[#id]».
	AsdaNotice::Chat(GetGameInstance(), EAsdaMsgCategory::ItemRefused, LocId, Fallback ? FString(Fallback) : FString());
	UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] нотис %d: %s"), LocId, Fallback ? Fallback : TEXT("-"));
}

// ★★★ ВОЛНА 8 (пункт 5 ревизии питомцев) — разбор у объявления в `.h`.
//   Повторяет готовый образец `UAsdaWarehouseSubsystem::NoticePlank` (канал, цвет, TTL):
//   второго толкования `CControl_SetTooltipText` в порту заводить нельзя.
void UAsdaInventorySubsystem::NoticePlank(int32 LocId, const TCHAR* Fallback) const
{
	UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
	if (!Msg)
	{
		// Маршрутизатора нет (голый стенд) — лучше строка в чат, чем молчание.
		Notice(LocId, Fallback);
		return;
	}
	const FString S = LocText(LocId, Fallback);
	if (S.IsEmpty()) { return; }
	// ★ Волна В3 ремесла (ревизия В0–В2 М-2): пост — общая точка плашки `AsdaNotice::Plank` (цвет −11514, TTL 10 с).
	AsdaNotice::Plank(GetGameInstance(), EAsdaMsgCategory::ItemRefused, S, AsdaNoticeColor::Gold, 10000.f, LocId);
	UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] нотис-плашка %d: %s"), LocId, *S);
}

FString UAsdaInventorySubsystem::LocText(int32 LocId, const TCHAR* Fallback) const
{
	// ★ ГРАБЛЯ `ministate-win-port`: `GetTextById` СРЕЗАЕТ inline-теги. Для подписей окна нам
	//   нужна СЫРАЯ строка — цвет/кегль части текста задаёт сама игра.
	// ★ Дедуп №2, В1-B (U10b, 11.10.2026): свой `GetTextByIdRaw` → общая точка `AsdaNotice::LocTagged` (то же: теги
	//   сохранены, маркеры не тронуты, «» без подсистемы текста); фолбэк окна — ниже, как был.
	const FString S = AsdaNotice::LocTagged(GetGameInstance(), LocId);
	if (!S.IsEmpty()) { return S; }
	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): фолбэка нет (nullptr) → нейтральная метка «[#id]» (английский игрок русского не увидит);
	//   явный TEXT("") по-прежнему значит «пусто».
	return Fallback ? FString(Fallback) : FAsdaLang::MissingTag(LocId);
}

void UAsdaInventorySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaItemTableSubsystem::StaticClass());

	// Контейнеры существуют ВСЕГДА, независимо от окна: хотбар читает их через мост, даже когда
	//   инвентарь закрыт (эталон точно так же держит g_InventoryBag/g_InventoryArray2 глобально).
	Shell.SetNum(kShellSlots);
	Spend.SetNum(kSpendSlots);
	Equip.SetNum(kEquipSlotsTotal);   // 11 рабочих + 9 аватар-слотов (задел, см. шапку)
	Store.SetNum(kStoreSlots);
	for (FAsdaItemInstance& I : Shell) { I.Clear(); }
	for (FAsdaItemInstance& I : Spend) { I.Clear(); }
	for (FAsdaItemInstance& I : Equip) { I.Clear(); }
	for (FAsdaItemInstance& I : Store) { I.Clear(); }

	if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
	{
		Net->OnInventoryBatch.AddUObject(this, &UAsdaInventorySubsystem::OnBatchReceived);
		Net->OnInventoryBatchEnd.AddUObject(this, &UAsdaInventorySubsystem::OnBatchEnd);
		Net->OnEquipFull.AddUObject(this, &UAsdaInventorySubsystem::OnEquipFullReceived);
		Net->OnItemMoveResult.AddUObject(this, &UAsdaInventorySubsystem::OnMoveResult);
		Net->OnItemSlotUpdate.AddUObject(this, &UAsdaInventorySubsystem::OnSlotUpdate);
		Net->OnItemSlotRemoved.AddUObject(this, &UAsdaInventorySubsystem::OnSlotRemoved);
		Net->OnItemUseResult.AddUObject(this, &UAsdaInventorySubsystem::OnUseResult);
		Net->OnItemDumpResult.AddUObject(this, &UAsdaInventorySubsystem::OnDumpResult);
		// ★ 5013 уже несёт ПОЛНУЮ 57-байтовую запись подобранного предмета @3 — до появления
		//   инвентаря порт читал из неё только result/вес. Теперь предмет реально ложится в ячейку.
		Net->OnLootPickupResult.AddUObject(this, &UAsdaInventorySubsystem::OnLootPicked);
		// ★★★ ВОЛНА 2 — три канала применения, идущие МИМО 5028/5029.
		Net->OnPackageOpenResult.AddUObject(this, &UAsdaInventorySubsystem::OnPackageResult);
		Net->OnGachaResult.AddUObject(this, &UAsdaInventorySubsystem::OnGachaResult);
		Net->OnFunctionItemResult.AddUObject(this, &UAsdaInventorySubsystem::OnFunctionResult);
		Net->OnItemRepaired.AddUObject(this, &UAsdaInventorySubsystem::OnItemRepaired);
		// ★ 08.10.2026 (рыбалка В1): 6570 — износ предмета (удочка после улова, любой `ItemDuraDis` сервера).
		//   До этого приёмника в порту не было: прочность в записи после износа не менялась.
		Net->OnSlotDurability6570.AddUObject(this, &UAsdaInventorySubsystem::OnSlotDurability6570);
		// ★★★ 5456 — расширение склада. Раньше у этого делегата НЕ БЫЛО НИ ОДНОГО подписчика:
		//   сеть разбирала пакет и вещала в пустоту, поэтому предмет «Extend Storage Slot»
		//   списывался молча (жалоба владельца 18.08.2026).
		Net->OnBagExpand.AddUObject(this, &UAsdaInventorySubsystem::OnBagExpand);
	}
	UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] модель создана: контейнеры 1=%d, 2=%d, 3=%d (рабочих %d), 5=%d"),
		Shell.Num(), Spend.Num(), Equip.Num(), GetEquipSlotCount(), Store.Num());
}

void UAsdaInventorySubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnInventoryBatch.RemoveAll(this);
			Net->OnInventoryBatchEnd.RemoveAll(this);
			Net->OnEquipFull.RemoveAll(this);
			Net->OnItemMoveResult.RemoveAll(this);
			Net->OnItemSlotUpdate.RemoveAll(this);
			Net->OnItemSlotRemoved.RemoveAll(this);
			Net->OnItemUseResult.RemoveAll(this);
			Net->OnItemDumpResult.RemoveAll(this);
			Net->OnLootPickupResult.RemoveAll(this);
			Net->OnPackageOpenResult.RemoveAll(this);
			Net->OnGachaResult.RemoveAll(this);
			Net->OnFunctionItemResult.RemoveAll(this);
			Net->OnItemRepaired.RemoveAll(this);
			Net->OnSlotDurability6570.RemoveAll(this);
			Net->OnBagExpand.RemoveAll(this);
		}
		if (UAsdaSystemMenuSubsystem* Menu = GI->GetSubsystem<UAsdaSystemMenuSubsystem>())
		{
			Menu->OnMenuCommand.RemoveDynamic(this, &UAsdaInventorySubsystem::OnMenuCommand);
			Menu->UnregisterWindow(kMenuButtonIdNum);
		}
	}
	if (AAsdaPlayerController* Pc = BoundController.Get())
	{
		Pc->OnGameCommand.RemoveDynamic(this, &UAsdaInventorySubsystem::OnGameCommand);
		BoundController = nullptr;
	}
	UnregisterZone();
	ResetWidget();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
// ★★★ [ИСПРАВЛЕНИЕ] 25.10.2026 — «инвентарь показывает вещи ПРЕЖНЕЙ учётки после перезахода другой учёткой»
//   (жалоба игроков, отчёт `Reports/inventory_relog_leak/REPORT.md`).
//   ПРИЧИНА. Эталон раскладывает батчи 4045/4048/4051 ТОЧЕЧНО — `memcpy(&g_InventoryBag + 57 * slot, …)` только по
//   пришедшим записям (`recv_InventoryBagFill@0xD006E0`, `recv_InventoryFullRefresh@0xD007C0`, network.c:17243/17277):
//   пустые ячейки сервер не шлёт вовсе. У оригинала это безопасно — при выходе процесс завершается
//   ([[original-quits-client-on-disconnect]]), глобальные массивы рождаются нулевыми. Подсистема порта живёт на
//   `GameInstance` и переживает выход на логин, а полный сброс снимал только маску «получено» — содержимое прошлой
//   учётки оставалось в ячейках, которые у новой пусты, и окно рисовало его иконками.
//   РЕШЕНИЕ. Модель (контейнеры 1/2/3/5) опустошается на НАЧАЛЕ новой сессии — `LeftWorld` (травел на сцену входа) и
//   `EnteringWorld` (1021). Ровно эти две причины гарантируют, что дальше сервер пришлёт полный набор заново.
//   ⚠ Батч надетого 1027 приходит РАНЬШЕ 1021 — он лежит в очереди `PendingBatches` (сохраняется выше) и в модель
//     попадает ПОСЛЕ этой очистки, в Tick мира ([[session-reset-drops-early-data]]).
//   ⚠ `Disconnected` модель НЕ трогает: уровень не выгружался, персонаж стоит в мире под попапом обрыва; следующий шаг
//     игрока — ОК → сцена входа → `LeftWorld` (там и очистка). `MapChanged` — данные персонажа на сервере целы.
//   ⚠ Длина контейнеров фиксированная (`SetNum` в `Initialize`) — чистим ЗНАЧЕНИЯ, длину не трогаем
//     ([[tarray-reset-breaks-fixed-length]]).
//   Ключ `asda.Session.RelogClearItems`: 1 — очищать (по умолчанию); 0 — прежнее поведение (содержимое переживало перезаход).
//   Им же пользуются панель (`UAsdaHotbarSubsystem`) и склад (`UAsdaWarehouseSubsystem`) — через аксессор ниже.
static ASDA_DEV_CVAR(int32, CVarSessionRelogClearItems,
	TEXT("asda.Session.RelogClearItems"),
	1,
	TEXT("Очистка вещей прошлой сессии при выходе на логин / входе в мир (инвентарь, склад, панель быстрого доступа): "
	     "1 = да (исправление перезахода другой учёткой, 25.10.2026); 0 = как было — содержимое переживало перезаход."),
	ECVF_Default);

// Аксессор ключа для соседних .cpp (имя ключа в рабочем коде других файлов не повторяется — правило Shipping-проверки).
bool AsdaSessionRelogClearItemsOn()
{
	return CVarSessionRelogClearItems.GetValueOnGameThread() != 0;
}

// true — эта причина сброса начинает НОВУЮ сессию персонажа и вещи прошлой обязаны уйти (см. шапку ключа выше).
bool AsdaSessionResetClearsItems(EAsdaSessionResetReason Reason)
{
	return AsdaIsFullSessionReset(Reason) && !AsdaSessionResetKeepsWorldAlive(Reason) && AsdaSessionRelogClearItemsOn();
}

void UAsdaInventorySubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Защёлки и очереди снимаем при ЛЮБОЙ причине: соединение пересоздаётся и при смене карты,
	// поэтому ответа на «действие в полёте» уже не будет, а таймаут ждал бы впустую.
	bActionPending = false;
	PendingUnequipSlot = -1;   // 07.11.2026: заявка «снять» — данные сессии
	ActionSentAtS = 0.0;
	PremiumActionAtS = 0.0;
	// ★ 09.10.2026 (автолопата): таймер цикла — при ЛЮБОЙ причине. Эталон: смена карты — `resetActiveAndCounter` +
	//   «AUTOSABZIL : END : RESET» (game_main.c:3361); разрыв — клиент закрывается (у порта сброс обязателен). Режим/слот/
	//   контейнер тоже обнуляем: они принадлежат сессии (слот прошлой сессии в новой указывал бы на чужое).
	AutoDigReset(*FString::Printf(TEXT("сброс сессии (%s)"), AsdaSessionResetReasonName(Reason)));
	AutoDig = FAsdaAutoDigTimer();
	bDumpPending = false;
	bArrangeRunning = false;
	ArrangeQueue.Reset();
	ArrangeRefused = 0;
	// ★★★ ИСПРАВЛЕНО 04.09.2026 — «панель надетого в рюкзаке всегда пуста».
	//   БЫЛО: `PendingBatches.Clear()` безусловно, при ЛЮБОЙ причине сброса.
	//   ПОЧЕМУ ЭТО ЛОМАЛО НАДЕТОЕ: контейнер 3 приходит ЕДИНСТВЕННЫМ пакетом 1027 (0x403) от
	//   ЛОГИН-сервера, и по проводу он идёт СТРОГО РАНЬШЕ 1021 «вход в мир» — живой лог входа
	//   (`Saved/Logs/AsdaReborn_4.log:2529…2541`): 1016 → 1017 → **1027** → 1022 → 1021 → сброс
	//   «вход в мир». Сброс `EnteringWorld` рассылает как раз обработчик 1021, то есть УЖЕ ПОСЛЕ
	//   того, как надетое легло в очередь: `Clear()` выбрасывал данные ТОГО САМОГО персонажа,
	//   который входит. Второй раз сервер 1027 не шлёт, других источников контейнера 3 нет ⇒
	//   панель оставалась пустой всю сессию (в логе НЕТ строки «надетое (1027) слоты»).
	//   ЭТО ТА ЖЕ ОГОВОРКА, что уже сделана для зеркал 1017 в `UAsdaNetSubsystem`
	//   (`Reports/audit_2026-09-02/fix_disconnect_reset.md`, раздел «Особый случай
	//   UAsdaNetSubsystem при EnteringWorld») — там её учли, здесь пропустили.
	//   СТАЛО: при `EnteringWorld` очередь фильтруется — переживает ТОЛЬКО батч контейнера 3.
	//   Всё прочее содержимое сумок (контейнеры 1/2/5, опкоды 4045/4048/4051) присылает
	//   game-сервер уже ПОСЛЕ входа, поэтому старые записи в очереди были бы чужими и уходят.
	if (Reason == EAsdaSessionResetReason::EnteringWorld)
	{
		// ★ N10 (В1 дедупа №2): фильтр на месте под замком очереди (`RemoveIf`) вместо «Drain → RemoveAll → Push» — итог тот же.
		if (PendingBatches.Num() > 0)
		{
			// `bEnd` (маркер конца батча) у контейнера 3 не бывает: 1027 приходит одним куском.
			PendingBatches.RemoveIf([](const FPendingBatch& B) { return B.bEnd || B.ContainerId != 3; });
			const int32 KeptNum = PendingBatches.Num();
			UE_LOG(LogAsdaInven, Log,
				TEXT("[сумка] сброс «вход в мир»: батчей надетого (1027) сохранено %d — они приходят РАНЬШЕ 1021"),
				KeptNum);
		}
	}
	else
	{
		PendingBatches.Clear();
	}
	PendingPackets.Clear();
	PendingWindowAction = 0;
	bDragActive = false; DragContainer = 0; DragCell = -1;
	DragArm.Disarm();
	Mouse.Reset();
	if (!AsdaIsFullSessionReset(Reason)) { return; }

	// ПОЛНЫЙ СБРОС: содержимое сумок принадлежит персонажу и приходит бурстом на входе в мир.
	// `ContainerReceivedMask` — «какие контейнеры уже пришли»; без сброса окно нового персонажа
	// считалось бы заполненным и ждало бы обновлений, которых сервер уже не пришлёт.
	UE_LOG(LogAsdaInven, Log, TEXT("[сумка] сброс сессии (%s): маска принятых контейнеров 0x%X"),
		AsdaSessionResetReasonName(Reason), ContainerReceivedMask);
	CloseWindow();
	ContainerReceivedMask = 0;
	bWasInWorld = false;
	CurrentTab = 0;
	SelEquipSlot = -1;
	GoldDisplay = -1;
	HintShown = 0;
	HoverContainer = 0; HoverCell = -1;
	CellDbl.Reset();
	// ★ [ИСПРАВЛЕНИЕ] 25.10.2026: содержимое прошлой учётки — вон (обоснование и ключ — у `CVarSessionRelogClearItems`).
	if (!AsdaSessionResetClearsItems(Reason)) { return; }
	int32 Dropped = 0;   // сколько непустых ячеек прошлой сессии снято — для журнала
	auto ClearContainer = [&Dropped](TArray<FAsdaItemInstance>& C, uint8 ContainerId)
	{
		for (int32 i = 0; i < C.Num(); ++i)
		{
			if (!C[i].IsEmpty()) { ++Dropped; }
			C[i].Clear();
			C[i].SetContainerType(ContainerId);   // как `ClearCell`: пустая ячейка помнит свой контейнер и номер
			C[i].SetSlotIndex(i);
		}
	};
	ClearContainer(Shell, 1);
	ClearContainer(Spend, 2);
	ClearContainer(Equip, 3);
	ClearContainer(Store, 5);
	ScrollRow[0] = 0; ScrollRow[1] = 0;   // прокрутка сетки — по содержимому прошлой учётки
	SelCell = -1;
	UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] сброс сессии (%s): контейнеры 1/2/3/5 опустошены, снято вещей прошлой сессии %d"),
		AsdaSessionResetReasonName(Reason), Dropped);
	// ⚠ `OnCellChanged` отсюда НЕ рассылаем: порядок сброса подсистем произволен, и подписчик (инфо-панель подсказки)
	//   мог бы тронуть виджеты умершего мира раньше своего сброса. «Перестроить всё» новой сессии разошлёт маркер конца
	//   первого же батча (4046/4049/4052 — `ApplyPendingBatches`), даже если контейнер у новой учётки пуст.
}

void UAsdaInventorySubsystem::ResetWidget()
{
	InvenWidget = nullptr;
	GridUi.Reset();
	EquipUi.Reset();
	GridSel = nullptr; EquipSel = nullptr;
	MoneyText = nullptr; TabText = nullptr; TabText1 = nullptr;
	TabOpt0 = nullptr; TabOpt1 = nullptr; TabPlane0 = nullptr; TabPlane1 = nullptr;
	GridSelParentRoot = FVector2D::ZeroVector; EquipSelParentRoot = FVector2D::ZeroVector;
	BtnClose = nullptr; BtnExpand = nullptr; BtnCollapse = nullptr; BtnTrash = nullptr;
	RightPanel = nullptr; Gear = nullptr; AniGear = nullptr;
	BtnAutoArrange = nullptr; AutoArrangeLabel = nullptr; AutoArrangeBox = nullptr;
	AutoArrangeItems.Reset(); AutoArrangeItemText.Reset();
	// ★ Аудит №3 (B4): призрак — ребёнок дерева ЭТОГО виджета, поэтому забываем структуру целиком
	//   (по той же причине, что и полосу прокрутки ниже).
	DragGhost = FAsdaDragGhost();
	// Планы полосы прокрутки живут в дереве ЭТОГО виджета — при пересоздании окна их надо забыть,
	//   иначе `BuildScrollBar` увидит уже собранную полосу (`IsBuilt()`) и не соберёт её заново.
	ScrollBar = FAsdaScrollBarNomal();
	HintUpgradeBtn = nullptr; HintDuriBar = nullptr; HintShown = 0;
	Relays.Reset();
	bControlsReady = false;
	bDragActive = false; DragCell = -1; DragContainer = 0;
	// ★ Взвод тоже снимаем: иначе после пересборки окна удержанная с прошлого раза кнопка могла бы
	//   на первом же кадре пройти порог и начать захват ячейки, которую уже никто не нажимал.
	DragArm.Disarm();
	HoverCell = -1; HoverContainer = 0; bTipShown = false;
}
