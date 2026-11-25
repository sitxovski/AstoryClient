// Copyright (c) 2026 @sitxovski (Discord)
// Реализация «Книги рыбака» 74000 + окна регистрации 74200 + нумпада режима 10 (волна В3 рыбалки, 08.10.2026).
//   Реверс с адресами — шапка `UI/AsdaFishBookSubsystem.h` и `Reports/owner_1008/fishing_parts/ui_book.md`.
#include "UI/AsdaFishBookSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "AsdaWeightWire.h"   // ★ 29.10.2026 (вес ×2): AsdaWeightFromWire — вес персонажа по проводу u16
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "UI/AsdaShopDblClick.h"   // ★ U1 (В2-A дедупа №2): FAsdaDblClick::WindowSec — общий порог Windows
#include "UI/AsdaJuiWindowHandle.h"        // каркас окон JUI (Ensure / RegisterFrameZone)
#include "UI/AsdaNotice.h"                 // Loc / LocRaw / MsgBox / ChatColored
#include "UI/AsdaWindowSubsystem.h"        // реестр окон, TWOBUT, нумпад 1410, ESC
#include "UI/AsdaMessageSubsystem.h"       // EAsdaMsgCategory
#include "UI/AsdaChatSubsystem.h"          // ColGold — цвет −11514 (0xFFFFD306)
#include "UI/AsdaInventorySubsystem.h"     // контейнер 2, применение записей ответов, закрыть сумку
#include "UI/AsdaItemTooltipSubsystem.h"   // подсказка награды (событие 8)
#include "UI/AsdaTipPanel.h"               // EAsdaTipOwner::FishBook
#include "Items/AsdaItemTableSubsystem.h"  // item_list / special_itemlist / sowell_list / item_tooltip
#include "Items/AsdaItemDbRows.h"          // строки таблиц предметов
#include "Items/AsdaGradeColor.h"          // `CItemManager_GetDropColor@0x9C45C0`
#include "World/AsdaFishingSubsystem.h"    // автомат рыбалки (В1): FSM 101, улов, Fish_data
#include "World/AsdaWorldSubsystem.h"      // своё имя (111018)
#include "AsdaNetSubsystem.h"              // 6176/6178/6180/6182, приём 6174…6183
#include "AsdaDataTableCache.h"            // AsdaDt::Cached
#include "AsdaBytes.h"                     // RdI16 / RdI32
#include "Login/AsdaButtonRelay.h"         // реле кнопок

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Framework/Application/SlateApplication.h"   // курсор — хит-рамка 74029 (МЕЛК-5 ревизии В3)
#if PLATFORM_WINDOWS
#include "Windows/MinWindows.h"            // `GetDoubleClickTime` — окно WM_LBUTTONDBLCLK эталона (0xD), ВАЖН-6 ревизии В3
#include "Text/AsdaLangRows.h"   // [ПОРТ] 09.10.2026 (мультиязычность): текст строк таблицы на языке сессии (копия, не сама строка)
#endif

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFishBook, Log, All);
// ★ U21 (В1 дедупа №2): имя константы — по контейнеру; значение обязано совпадать с размером контейнера инвентаря.
static_assert(UAsdaFishBookSubsystem::kSpendCells == UAsdaInventorySubsystem::kSpendSlots, "размер контейнера разошёлся с инвентарём");

using AsdaBytes::RdI16;
using AsdaBytes::RdI32;

// ── Константы окна ──
static const FName  kBookWindowId(TEXT("FISH_BOOK_WIN"));   // корень 74000 в `.JUI` / реестре окон / таблице ESC
static const FName  kJoinModalId(TEXT("FISH_BOOK_JOINBOX"));  // окно регистрации 74200 — модальный контрол эталона (роспись замка ввода)
static const TCHAR* kBookWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Fishing/WBP_FISH_BOOK_WIN.WBP_FISH_BOOK_WIN_C");   // WBP окна
static const TCHAR* kBookDtPath = TEXT("/Game/AsdaAssets/Data/DT_FishBook.DT_FishBook");   // DT_FishBook (Tools/asda_build_fishbook_dt.py)

// ── Loc-идентификаторы (сняты с эталонных вызовов по обработчику, ui_book §4-§8) ──
namespace FishBookLoc
{
	constexpr int32 kTotalRate   = 111000;   // 74101 «Общий рейтинг» (`uiWindowInit_74000`)
	constexpr int32 kTotalKinds  = 111001;   // 74103 «Виды пойманной рыбы»
	constexpr int32 kCaught      = 111004;   // 74041 «Поймано»
	constexpr int32 kKind        = 111005;   // 74042 «вид»
	constexpr int32 kRating      = 111006;   // 74043 «рейтинг»
	constexpr int32 kRank        = 111007;   // 74061 «ранг»
	constexpr int32 kMaxSize     = 111008;   // 74063 «самая большая»
	constexpr int32 kMinSize     = 111009;   // 74065 «самая маленькая»
	constexpr int32 kRecords     = 111040;   // 74067 «Количество записей»
	constexpr int32 kDetails     = 111010;   // 74069 «подробнее»
	constexpr int32 kRewardCap   = 111011;   // 74080 «награда за коллекцию»
	constexpr int32 kRewardBtn   = 111012;   // 74090 «Получение награды»
	constexpr int32 kRewardDone  = 111013;   // 6183 код 1: «Добыча: [%s]. Награда - [%s] .» (имя тома, имя награды)
	constexpr int32 kJoinTitle   = 111014;   // 74201 «Зарегистрировать добычу: [%s].»
	constexpr int32 kJoinCaught  = 111015;   // 74210 «Пойманная рыба»
	constexpr int32 kJoinHint    = 111016;   // 74251 подсказка окна регистрации
	constexpr int32 kDeleteAsk   = 111017;   // диалог 77 «Удалить рыбу '[%s]'?…» (имя тома)
	constexpr int32 kRegistAsk   = 111018;   // диалог 79 «Зарегистрировать рыбу '[%s]' в руководстве '[%s]'?» (предмет-книга, игрок; ★ 09.10 порядок по дизасму)
	constexpr int32 kAlready     = 111020;   // 6177 код 5 — «уже зарегистрирована»
	constexpr int32 kNotDone     = 111023;   // 74090: «Рыболовное руководство не завершено.»
	constexpr int32 kDelBtn      = 111047;   // 74271 «Удалить книгу»
	constexpr int32 kNoFish      = 111060;   // 74252 без выбора: «Не выбрана рыба.»
	constexpr int32 kDelNotReg   = 111069;   // 74271 по незарегистрированному: «Можно удалить только…»
	constexpr int32 kNoReward    = 111071;   // 74090 без выбора: «Награда не выбрана.»
	constexpr int32 kDeleted     = 111072;   // 6179 код 1: «Изображение удалено.»
	constexpr int32 kRegistered  = 111073;   // 6177 код 1: «[%d]: зарегистрировано…» (индекс тома) → чат
	constexpr int32 kJoinOk      = 12863;    // 74252 «ОК»
	constexpr int32 kJoinNo      = 12864;    // 74253 «Отмена»
	constexpr int32 kGradeBase   = 1421;     // ранг = loc(1421 + itemGrade)
	constexpr int32 kDlgOk       = 108510;   // TWOBUT «ОК» диалогов 77/79
	constexpr int32 kDlgCancel   = 108511;   // TWOBUT «Отмена» диалогов 77/79
}

// ── Ключи отката (обоснование — шапка .h) ──
static ASDA_DEV_CVAR(int32, CVarFishBookClientTable,
	TEXT("asda.FishBook.ClientTable"), 0,
	TEXT("Р12. Книга рыбака: 0 — [ПОРТ] рыбы/требования/награды/книга-предмет по СЕРВЕРНОЙ таблице (Fishing_BookInfo.txt + item_table, ")
	TEXT("авторитет сервера); 1 — клиентская Fish_collection_data.BIN, как оригинал (у томов 1–3 награды и у томов 5–8 рыбы расходятся с сервером)."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarFishBookLegacyCursor,
	TEXT("asda.FishBook.LegacyCursor"), 0,
	TEXT("Р7/Д1. Окно регистрации 74200: 0 — [ИСПРАВЛЕНИЕ] курсор 74241 на ВЫБРАННОЙ стопке, ровно поверх ячейки; ")
	TEXT("1 — как оригинал: на ПОСЛЕДНЕЙ стопке с ДРУГИМ слотом (сравнение `!=`, 0xFD6DB5) и со сдвигом (+10,+7)."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarFishBookLegacyReward5,
	TEXT("asda.FishBook.LegacyReward5"), 0,
	TEXT("Р7/Д2. 0 — [ИСПРАВЛЕНИЕ] ЛКМ по подписи 74085 награду не выбирает; 1 — как оригинал: диапазон 74081…74085, ")
	TEXT("клик по подписи первой награды выбирает «5-ю» (код = количество[0])."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarFishBookLiveRefresh,
	TEXT("asda.FishBook.LiveRefresh"), 1,
	TEXT("1 — [ПОРТ] окно регистрации 74200 перерисовывается при изменении сумки (оригинал держит 74200 модальным, сумка под ним ")
	TEXT("не меняется; порт других окон не блокирует); 0 — только при открытии и ОК нумпада, как оригинал."),
	ECVF_Default);
// ★ fishfix 09.10 (ревизия `fishing_REVIEW.md`, МЕЛК-4): обрезка «13 знаков + ...» эталона рассчитана на БАЙТЫ (корейский
//   cp949 — 2 байта на знак, 13 байт ≈ 6 знаков ≈ ширина поля 108 px). Русский cp1251 — 1 байт на знак: те же 13 знаков вдвое
//   уже поля, и тома «Рыбалка на Альпеоне, том 1/2/3» на вкладках неразличимы. Окно книги расширено ЦЕЛИКОМ (правило
//   владельца 07.10, шаг `asda_ui_widen_windows`), поля вкладок/подписей наград шире; строка, которая и в расширенное поле не
//   входит, режется ПО ШИРИНЕ поля с «...» — приём самого эталона `textTruncateWithEllipsis@0xBC6E60` (g:235909, обрезка по
//   ширине в пикселях; им режутся имена в окнах сообщества). Многоточие Slate (Overflow Policy) у центрированного текста не
//   работает (`FSlateTextLayout::OnPaint`: Center → NoOverflow), поэтому обрезка — здесь, тем же шрифтом поля.
static ASDA_DEV_CVAR(int32, CVarFishBookTrunc13,
	TEXT("asda.FishBook.Trunc13"), 0,
	TEXT("[ПОРТ] Подписи вкладок томов и наград книги рыбака: 0 (по умолчанию) — строка целиком, а не входящая в поле — по ширине ")
	TEXT("поля + «...» (`textTruncateWithEllipsis` эталона; окно расширено под русский текст); 1 — как оригинал: первые 13 знаков + ")
	TEXT("«...» (`ii > 12`, g:612468; `k/m/n > 12`, g:610788-610845)."),
	ECVF_Default);

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaFishBookSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaInventorySubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaFishingSubsystem::StaticClass());

	CellUi.SetNum(kFish);
	RewardUi.SetNum(kRewards);
	InvUi.SetNum(kFish);
	TopText.SetNum(4);
	GgText.SetNum(4);
	Digit.SetNum(5);
	InfoCap.SetNum(5);
	InfoVal.SetNum(4);
	for (int32 i = 0; i < kActiveCap; ++i) { ActiveList[i] = -1; }
	for (int32 k = 0; k < kFish; ++k) { InvCell[k] = -1; }

	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь (разбор — в Tick).
		H6174 = Net->OnFishBookPage6174.AddLambda([this](const TArray<uint8>& B) { PendingPkts.Push({ 6174, B }); });
		H6175 = Net->OnFishBookListEnd6175.AddLambda([this](const TArray<uint8>& B) { PendingPkts.Push({ 6175, B }); });
		H6177 = Net->OnFishBookRegist6177.AddLambda([this](const TArray<uint8>& B) { PendingPkts.Push({ 6177, B }); });
		H6179 = Net->OnFishBookDelete6179.AddLambda([this](const TArray<uint8>& B) { PendingPkts.Push({ 6179, B }); });
		H6181 = Net->OnFishBookUpdate6181.AddLambda([this](const TArray<uint8>& B) { PendingPkts.Push({ 6181, B }); });
		H6183 = Net->OnFishBookReward6183.AddLambda([this](const TArray<uint8>& B) { PendingPkts.Push({ 6183, B }); });
	}
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		// [ПОРТ] LiveRefresh: из колбэка только МЕТИМ — перерисует Tick (один пакет двигает много ячеек подряд).
		HInvCell = Inv->OnCellChanged.AddLambda([this](int32 /*Container*/, int32 /*Cell*/) { if (bJoinOpen) { bJoinDirty = true; } });
	}
	if (UAsdaFishingSubsystem* Fish = GI ? GI->GetSubsystem<UAsdaFishingSubsystem>() : nullptr)
	{
		// Точки автомата В1: FSM 101 (`inventoryFindFirstValidItem` + диалог 79) и улов 6172 (`questTableUpdateKeyMinMax`).
		Fish->BookRegisterProbe.BindUObject(this, &UAsdaFishBookSubsystem::BookRegisterProbe);
		HCaught = Fish->OnFishCaught.AddUObject(this, &UAsdaFishBookSubsystem::OnFishCaught);
	}
	UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] подсистема поднята"));
}

void UAsdaFishBookSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnFishBookPage6174.Remove(H6174);
		Net->OnFishBookListEnd6175.Remove(H6175);
		Net->OnFishBookRegist6177.Remove(H6177);
		Net->OnFishBookDelete6179.Remove(H6179);
		Net->OnFishBookUpdate6181.Remove(H6181);
		Net->OnFishBookReward6183.Remove(H6183);
	}
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr) { Inv->OnCellChanged.Remove(HInvCell); }
	if (UAsdaFishingSubsystem* Fish = GI ? GI->GetSubsystem<UAsdaFishingSubsystem>() : nullptr)
	{
		if (Fish->BookRegisterProbe.IsBoundToObject(this)) { Fish->BookRegisterProbe.Unbind(); }
		Fish->OnFishCaught.Remove(HCaught);
	}
	ResetWidget();
	Super::Deinitialize();
}

// Сброс сессии (обоснование — шапка .h): окно и ввод — всегда; тома/очередь — только при конце сессии персонажа.
void UAsdaFishBookSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Окно: виджеты снесёт травел (`CloseAll`) — флаги снимаем сами, иначе окно «воскреснет».
	if (bOpen || bJoinOpen)
	{
		bJoinOpen = false;
		bOpen = false;
		if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			WM->SetWindowVisible(kBookWindowId, false);
		}
	}
	HideRewardTip();
	Mouse.Reset();
	LastClickTime = 0.0; LastClickCell = -1;
	bJoinDirty = false;
	PickedCell = -1; ChosenCell = -1; Qty = 0; JoinFish = 0;
	for (int32 k = 0; k < kFish; ++k) { InvCell[k] = -1; }

	if (Reason == EAsdaSessionResetReason::MapChanged)
	{
		// ★ Ревизия В3, МЕЛК-2: диалоги 77/79 в очереди окон-сообщений НЕ снимаются — оригинал очередь при портале не трогает,
		//   а их ответ остаётся верным и после смены карты: [469] — ячейка сумки (сумка переживает портал), 77 — id тома.
		return;   // тома и очередь пакетов живут (сервер повторяет 6174 после портала, ответы 6177…6183 могут прийти позже)
	}
	if (Reason == EAsdaSessionResetReason::EnteringWorld)
	{
		// ★ 6174 нового персонажа могли прийти ДО 1021 (поток загрузки) — их НЕ выбрасываем (session-reset-drops-early-data).
		return;
	}
	// LeftWorld / Disconnected: сессия персонажа кончилась — следующий вход может быть другим персонажем.
	{
		TArray<FPkt> Drop;
		PendingPkts.Drain(Drop);
	}
	for (FState& S : St)
	{
		S.bReg = false; S.Id = -1; S.B8 = 0;
		for (int32 j = 0; j < kFish; ++j) { S.Cnt[j] = 0; S.MaxSz[j] = 0; S.MinSz[j] = 0; }
	}
	bAwaitRegist = false;
	BookCell = -1;
	Sel = 0; Page = 0; TabOfs = 0; RewardSel = -1; TabSel = 0;
	ActiveNum = 0;
	for (int32 i = 0; i < kActiveCap; ++i) { ActiveList[i] = -1; }
	UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] сброс сессии (%d): тома и очередь очищены"), (int32)Reason);
}

void UAsdaFishBookSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.FishBook");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	DrainPackets();
	if (bJoinDirty)
	{
		bJoinDirty = false;
		if (bJoinOpen && CVarFishBookLiveRefresh.GetValueOnGameThread() != 0) { RefreshJoin(JoinFish); }
	}
	TickInput();
}

// ═════════════════════════════ ДАННЫЕ ═════════════════════════════════════════════════════════

// `CFishCollection_LoadPictureBookData@0xFD5FD0`: один раз; шаблоны — по ключу Р12, состояния — нули, id −1
//   (`fishBookRecord_Reset@0xFD6690`), имя тома — из клиентской записи (+8 → state+12).
void UAsdaFishBookSubsystem::EnsureData()
{
	if (!bDataLoaded)
	{
		bDataLoaded = true;
		const TCHAR* Paths[] = { kBookDtPath };
		HeldTables.Hold(FName(TEXT("FishBook")), Paths);
		Rows.Reset();
		if (UDataTable* Dt = AsdaDt::Cached(kBookDtPath))
		{
			TArray<FAsdaFishBookRow*> All;
			Dt->GetAllRows<FAsdaFishBookRow>(TEXT("FishBook"), All);
			// ★ [ПОРТ] 09.10.2026 (мультиязычность): имя тома — на языке сессии (оверлей `FishBook`, ключ `<строка>.Name`).
			for (const FAsdaFishBookRow* R : All)
			{
				if (!R) { continue; }
				FAsdaFishBookRow& C = Rows.Add_GetRef(*R);
				C.Name = AsdaLangRows::Text(Dt, R, TEXT("Name"), R->Name);
			}
			Rows.Sort([](const FAsdaFishBookRow& A, const FAsdaFishBookRow& B) { return A.Tome < B.Tome; });
		}
		St.SetNum(Rows.Num());
		for (int32 i = 0; i < St.Num(); ++i)
		{
			FState& S = St[i];
			S.bReg = false; S.Id = -1; S.B8 = 0;
			S.Name = Rows[i].Name;
			for (int32 j = 0; j < kFish; ++j) { S.Cnt[j] = 0; S.MaxSz[j] = 0; S.MinSz[j] = 0; }
		}
		UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] DT_FishBook: томов %d%s"), Rows.Num(),
			Rows.Num() ? TEXT("") : TEXT(" — ★ НЕТ ДАННЫХ: собери Tools/asda_build_fishbook_dt.py"));
	}
	const int32 Want = (CVarFishBookClientTable.GetValueOnGameThread() != 0) ? 1 : 0;
	if (TableChoice != Want) { TableChoice = Want; ApplyTableChoice(); }
}

// Р12: шаблоны из выбранного набора. Сервер — `Fishing_BookInfo.txt` + книга-предмет из item_table; клиент — .BIN.
void UAsdaFishBookSubsystem::ApplyTableChoice()
{
	const bool bClient = (TableChoice == 1);
	Tpl.SetNum(Rows.Num());
	auto Arr = [](const TArray<int32>& Src, int32 i) { return Src.IsValidIndex(i) ? Src[i] : -1; };
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		const FAsdaFishBookRow& R = Rows[i];
		FTpl& T = Tpl[i];
		const bool bHave = bClient ? R.bClient : R.bServer;
		T.Id = bHave ? (bClient ? R.ClientTome : R.Tome) : -1;
		T.BookItemId = bClient ? R.ClientBookItemId : R.ServerBookItemId;
		T.Flag68 = bClient ? R.ClientFlag : R.ServerType;
		for (int32 j = 0; j < kFish; ++j)
		{
			T.Fish[j] = Arr(bClient ? R.ClientFish : R.ServerFish, j);
			T.Req[j] = Arr(bClient ? R.ClientReq : R.ServerReq, j);
		}
		for (int32 t = 0; t < kRewards; ++t)
		{
			T.Reward[t] = Arr(bClient ? R.ClientReward : R.ServerReward, t);
			T.RewardCnt[t] = Arr(bClient ? R.ClientRewardCnt : R.ServerRewardCnt, t);
		}
	}
	UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] шаблоны томов: %s таблица (%d томов)"),
		bClient ? TEXT("КЛИЕНТСКАЯ (как оригинал, asda.FishBook.ClientTable 1)") : TEXT("СЕРВЕРНАЯ ([ПОРТ] Р12)"), Tpl.Num());
}

// ═════════════════════════════ ОКНО ═══════════════════════════════════════════════════════════

bool UAsdaFishBookSubsystem::EnsureWidget()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kBookWindowId, kBookWbp, kZ).Ensure(GetGameInstance(), Widget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupControls();
			RegisterZone();
			// ESC (событие 21) → `itemCollectUiShowOrToggle` (сначала окно регистрации). Окно в таблице `kEscCloseWindows`.
			WM.RegisterEscCloser(kBookWindowId, FSimpleDelegate::CreateWeakLambda(this, [this]() { CloseOrToggle(); }));
			// ★ 10.10.2026 [ОРИГИНАЛ] (замок ввода, `owner_1010/modal_lock.md`): `fishCollectionOpenWindow@0xFD6720` показывает 74200
			//   МОДАЛЬНО — `helper_setupFields(g_pWindowMgr, 74200, 1)` (entity_char.c:64419) ставит `CWindowMgr+0x200`. Пока окно
			//   регистрации показано, ввод заперт как у оригинала: раскладка, хотбар, сумка, системное меню, ESC; мышь — только книге.
			TWeakObjectPtr<UAsdaFishBookSubsystem> WeakSelf(this);
			WM.RegisterOriginalModal(kJoinModalId, kBookWindowId, [WeakSelf]()
			{
				const UAsdaFishBookSubsystem* S = WeakSelf.Get();
				return S && S->bJoinOpen && S->Join && AsdaUi::IsShown(S->Join);
			},
			// ★ modal_lock2 (МЕЛК-2 ревизии): узел 74200 — мышь книге только в его прямоугольнике (эталон `+0x1A0` = контрол 74200,
			//   Origin 172 198, 222×492); заголовок `WinMd_CBar` книги при открытом 74200 мыши не видит, книгу не утащить.
			[WeakSelf]() -> const UWidget*
			{
				const UAsdaFishBookSubsystem* S = WeakSelf.Get();
				return S ? S->Join.Get() : nullptr;
			});
		},
		[]()
		{
			UE_LOG(LogAsdaFishBook, Error, TEXT("[книга рыбака] WBP_FISH_BOOK_WIN не загрузился (%s)"), kBookWbp);
		});
	if (R == EAsdaJuiEnsure::Created) { bOpen = false; bJoinOpen = false; }
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaFishBookSubsystem::ResetWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterEscCloser(kBookWindowId);
		WM->UnregisterOriginalModal(kJoinModalId);   // ★ 10.10.2026: роспись замка ввода окна регистрации 74200
		WM->UnregisterUiZone(kBookWindowId);
		WM->CloseWindow(kBookWindowId);
	}
	Widget = nullptr;
	for (FAsdaFishBookCellUi& U : CellUi) { U = FAsdaFishBookCellUi(); }
	for (FAsdaFishBookRewardUi& U : RewardUi) { U = FAsdaFishBookRewardUi(); }
	for (FAsdaFishBookInvUi& U : InvUi) { U = FAsdaFishBookInvUi(); }
	for (TObjectPtr<UTextBlock>& T : TopText) { T = nullptr; }
	for (TObjectPtr<UTextBlock>& T : GgText) { T = nullptr; }
	for (TObjectPtr<UImage>& T : Digit) { T = nullptr; }
	for (TObjectPtr<UTextBlock>& T : InfoCap) { T = nullptr; }
	for (TObjectPtr<UTextBlock>& T : InfoVal) { T = nullptr; }
	TabPlanes.Reset(); TabButtons.Reset(); TabTexts.Reset();
	TabIdleBrush.Reset(); TabIdleStyle.Reset(); TabIdleCaption.Reset();
	InfoDesc = FAsdaFmtText(); JoinTitle = FAsdaFmtText();
	CellTemplate = nullptr; CardName = nullptr; CardPic = nullptr; CardNo = nullptr; CardCnt = nullptr; RewardCap = nullptr;
	RewardLabel5 = nullptr; BtnReward = nullptr; BtnRewardText = nullptr; Join = nullptr; JoinPic = nullptr; JoinDone = nullptr;
	JoinK = nullptr; JoinR1 = nullptr; JoinR2 = nullptr; JoinR3 = nullptr; JoinCaught = nullptr; JoinCheck = nullptr; JoinHint = nullptr;
	BtnJoinOk = nullptr; BtnJoinOkText = nullptr; BtnJoinNo = nullptr; BtnJoinNoText = nullptr; DelPanel = nullptr; BtnDel = nullptr;
	BtnDelText = nullptr; BtnClose = nullptr; BtnLeft = nullptr; BtnRight = nullptr;
	Relays.Reset();
	bControlsReady = false;
	bOpen = false;
	bJoinOpen = false;
	TipReward = -1;
	Mouse.Reset();
}

// Разбор ВСЕХ контролов окна (закон `full-structural-analysis-always`), 1:1 с `uiWindowInit_74000@0xFD2950` (g:610023-610359).
//   Клоны ячейки (30 × `FISH_BOOK_CELLnn_*`) и 4-я вкладка кладутся В АССЕТ шагом `Tools/asda_ui_fishbook_clones.py`
//   (эталон делает `CControl_InstantiateChild`/`uiListAppendClonedCell` — это код, а не данные `.JUI`).
void UAsdaFishBookSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }
	auto Txt = [this](const TCHAR* Name) { return Cast<UTextBlock>(Widget->GetWidgetFromName(FName(Name))); };
	auto Btn = [this](const TCHAR* Name) { return Cast<UButton>(Widget->GetWidgetFromName(FName(Name))); };
	auto Any = [this](const FString& Name) { return Widget->GetWidgetFromName(FName(*Name)); };

	// ── Кнопки и тексты по точным именам импортёра ──
	BtnClose = Btn(TEXT("FISH_BOOK_XX_0"));            // 74099
	BtnLeft = Btn(TEXT("FISH_BOOK_H_LEFT"));           // 74106
	BtnRight = Btn(TEXT("FISH_BOOK_H_RIGHT"));         // 74107
	BtnReward = Btn(TEXT("FISH_BOOK_OKK"));            // 74090
	BtnRewardText = Txt(TEXT("FISH_BOOK_OKK_Text"));
	BtnJoinOk = Btn(TEXT("FISH_BOOK_JOINBOX_OKbtn"));  // 74252
	BtnJoinOkText = Txt(TEXT("FISH_BOOK_JOINBOX_OKbtn_Text"));
	BtnJoinNo = Btn(TEXT("FISH_BOOK_JOINBOX_NObtn"));  // 74253
	BtnJoinNoText = Txt(TEXT("FISH_BOOK_JOINBOX_NObtn_Text"));
	BtnDel = Btn(TEXT("FISH_BOOK_DEL_btn"));           // 74271
	BtnDelText = Txt(TEXT("FISH_BOOK_DEL_btn_Text"));
	for (int32 i = 0; i < 4; ++i) { TopText[i] = Txt(*FString::Printf(TEXT("FISH_BOOK_TOPTEXT_%d"), i + 1)); }   // 74101…74104
	for (int32 i = 0; i < 4; ++i) { GgText[i] = Txt(*FString::Printf(TEXT("FISH_BOOK_GG_T%d"), i + 1)); }         // 74041…74044
	// 74061…74070: T1/T3/T5/T7/T9 — подписи, T2/T4/T6/T8 — значения, T10 — описание.
	for (int32 i = 0; i < 5; ++i) { InfoCap[i] = Txt(*FString::Printf(TEXT("FISH_BOOK_BIG_BOX_2_T%d"), 2 * i + 1)); }
	for (int32 i = 0; i < 4; ++i) { InfoVal[i] = Txt(*FString::Printf(TEXT("FISH_BOOK_BIG_BOX_2_T%d"), 2 * i + 2)); }
	UAsdaRichText::CaptureFmt(InfoDesc, Txt(TEXT("FISH_BOOK_BIG_BOX_2_T10")));
	CardName = Txt(TEXT("FISH_BOOK_BIG_BOX_BARD1TEXT"));   // 74051
	CardNo = Txt(TEXT("FISH_BOOK_BIG_BOX_NO_D1"));         // 74053
	CardCnt = Txt(TEXT("FISH_BOOK_BIG_BOX_K_D1"));         // 74054
	RewardCap = Txt(TEXT("FISH_BOOK_BIG_BOX_3_T3"));       // 74080
	RewardLabel5 = Txt(TEXT("FISH_BOOK_BIG_BOX_3_DT_1"));  // 74085 (Д2: «5-я награда»)
	UAsdaRichText::CaptureFmt(JoinTitle, Txt(TEXT("FISH_BOOK_JOINBOX_TFONT_1")));   // 74201
	JoinK = Txt(TEXT("FISH_BOOK_JOINBOX_K"));              // 74204
	JoinR1 = Txt(TEXT("FISH_BOOK_JOINBOX_R1"));            // 74205
	JoinR2 = Txt(TEXT("FISH_BOOK_JOINBOX_R2"));            // 74206
	JoinR3 = Txt(TEXT("FISH_BOOK_JOINBOX_R3"));            // 74207
	JoinCaught = Txt(TEXT("FISH_BOOK_JOINBOX_TFONT_2"));   // 74210
	JoinHint = Txt(TEXT("FISH_BOOK_JOINBOX_TFONT_3"));     // 74251
	Join = Any(TEXT("FISH_BOOK_JOINBOX_grp"));             // 74200
	DelPanel = Any(TEXT("FISH_BOOK_DEL_grp"));             // 74270
	CellTemplate = Any(TEXT("FISH_BOOK_00_grp"));          // 74020 (шаблон)

	// ── Вкладки 74105 (`TabBar_W108_3Btn` + 4-я ячейка из шага ассета) ──
	TabPlanes.SetNum(kMaxTabs); TabButtons.SetNum(kMaxTabs); TabTexts.SetNum(kMaxTabs);
	for (int32 n = 0; n < kMaxTabs; ++n)
	{
		TabButtons[n] = Btn(*FString::Printf(TEXT("FISH_BOOK_RADIO_btn_OPT%d"), n));
		TabTexts[n] = Txt(n == 0 ? TEXT("FISH_BOOK_RADIO_btn_Text") : *FString::Printf(TEXT("FISH_BOOK_RADIO_btn_Text%d"), n + 1));
	}

	// ── 30 клонов ячейки сетки (имена шага ассета `FISH_BOOK_CELLnn_*`) ──
	for (int32 i = 0; i < kFish; ++i)
	{
		const FString P = FString::Printf(TEXT("FISH_BOOK_CELL%02d_"), i);
		FAsdaFishBookCellUi& U = CellUi[i];
		U.Root = Any(P + TEXT("grp"));
		U.Name = Cast<UTextBlock>(Any(P + TEXT("NAME")));
		U.Que = Any(P + TEXT("Q"));
		U.Done = Any(P + TEXT("DONE"));
		U.Pic = Cast<UImage>(Any(P + TEXT("MARK")));
		U.No = Cast<UTextBlock>(Any(P + TEXT("NO")));
		U.Cnt = Cast<UTextBlock>(Any(P + TEXT("CNT")));
		U.Red = Any(P + TEXT("RED_grp"));
		U.Click.Reset();
		for (int32 k = 0; k < 8; ++k)   // 8 планов `Box_White3Stroke` клона 74029 (хит-рамка −1,−1 / 102×92)
		{
			if (UWidget* C = Any(P + FString::Printf(TEXT("CLICK_%d"), k))) { U.Click.Add(C); }
		}
	}

	// ── Планы (имя импортёра `<idc>_<jui>_<план>`): разбор по idc ──
	struct FRewardPlane { int32 Jui = 0; UImage* Img = nullptr; };
	TArray<FRewardPlane> RewardPlanes;
	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		FString Idc; int32 Jui = 0, Plane = 0;
		if (!AsdaUi::ParsePlaneName(W->GetName(), Idc, Jui, Plane)) { continue; }
		UImage* Img = Cast<UImage>(W);
		if (Idc == TEXT("FISH_BOOK_RADIO_btn") && Img && Plane >= 0 && Plane < kMaxTabs) { TabPlanes[Plane] = Img; continue; }
		if (Idc == TEXT("FISH_BOOK_BIG_BOX_MARK_D1") && Img) { CardPic = Img; continue; }     // 74052
		if (Idc == TEXT("FISH_BOOK_JOINBOX_BMARK") && Img) { JoinPic = Img; continue; }       // 74202
		if (Idc == TEXT("FISH_BOOK_JOINBOX_GFONT")) { JoinDone = W; continue; }               // 74203
		if (Idc == TEXT("FISH_BOOK_JOINBOX_check")) { JoinCheck = W; continue; }              // 74241 (Slot_AniSelect)
		if (Idc.StartsWith(TEXT("FISH_BOOK_GG_SS_")) && Img)                                   // 74045…74049
		{
			const int32 N = FCString::Atoi(*Idc.Mid(16)) - 1;
			if (N >= 0 && N < 5) { Digit[N] = Img; }
			continue;
		}
		if (Idc.StartsWith(TEXT("FISH_BOOK_JOINBOX_M")) && Img)                                // 74211…74240
		{
			const int32 K = FCString::Atoi(*Idc.Mid(19)) - 1;
			if (K >= 0 && K < kFish) { InvUi[K].Icon = Img; }
			continue;
		}
		if ((Idc == TEXT("FISH_BOOK_BIG_BOX_3_MARK_1") || Idc == TEXT("FISH_BOOK_BIG_BOX_3_MARK_2") || Idc == TEXT("FISH_BOOK_BIG_BOX_3_MARK_4")) && Img)
		{
			RewardPlanes.Add({ Jui, Img });   // 74081…74084 (у 74083/74084 в `.JUI` ОДИН idc — порядок по номеру импорта)
			continue;
		}
		if (Idc.StartsWith(TEXT("FISH_BOOK_BIG_BOX_3_MARK_A")))                                // 74111…74114
		{
			const int32 T = FCString::Atoi(*Idc.Mid(26)) - 1;
			if (T >= 0 && T < kRewards) { RewardUi[T].Frame = W; }
			continue;
		}
	}
	// Награды: иконки по порядку импорта; число (текст той же иконки) и имя — соседи по родительской группе.
	RewardPlanes.Sort([](const FRewardPlane& A, const FRewardPlane& B) { return A.Jui < B.Jui; });
	for (int32 t = 0; t < kRewards && t < RewardPlanes.Num(); ++t)
	{
		RewardUi[t].Icon = RewardPlanes[t].Img;
		if (UPanelWidget* Par = RewardPlanes[t].Img->GetParent())
		{
			for (int32 c = 0; c < Par->GetChildrenCount(); ++c)
			{
				if (UTextBlock* Tb = Cast<UTextBlock>(Par->GetChildAt(c)))
				{
					if (Tb->GetName().StartsWith(TEXT("FISH_BOOK_BIG_BOX_3_MARK_"))) { RewardUi[t].Qty = Tb; }
				}
			}
		}
		RewardUi[t].Name = Txt(*FString::Printf(TEXT("FISH_BOOK_BIG_BOX_3_DT_%d"), t + 1));   // 74085…74088
	}
	// Стопки окна регистрации: число — текст той же ячейки (`FISH_BOOK_JOINBOX_Mk`).
	for (int32 k = 0; k < kFish; ++k) { InvUi[k].Label = Txt(*FString::Printf(TEXT("FISH_BOOK_JOINBOX_M%d"), k + 1)); }

	// ── Кнопки (событие 66561 `uiItemWindow_WndProc@0xFD5A70`) ──
	auto Bind = [this](UButton* B, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!B) { return; }
		AsdaUi::BindButton(this, B, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	// [ПОРТ] модальность 74200 внутри книги: пока окно регистрации видно, прочие контролы книги молчат (эталон отдаёт
	//   мышь только модальному контролу — `helper_setupFields(g_pWindowMgr, 74200, 1)`).
	Bind(BtnClose,  74099, [this](int32) { if (!bJoinOpen) { CloseOrToggle(); } });
	Bind(BtnLeft,   74106, [this](int32) { if (!bJoinOpen) { OnArrow(-1); } });
	Bind(BtnRight,  74107, [this](int32) { if (!bJoinOpen) { OnArrow(+1); } });
	Bind(BtnReward, 74090, [this](int32) { if (!bJoinOpen) { OnRewardButton(); } });
	Bind(BtnDel,    74271, [this](int32) { if (!bJoinOpen) { OnDeleteClicked(); } });
	Bind(BtnJoinOk, 74252, [this](int32) { OnJoinOk(); });
	Bind(BtnJoinNo, 74253, [this](int32) { CloseJoin(); });
	for (int32 n = 0; n < kMaxTabs; ++n)
	{
		Bind(TabButtons[n], 74105, [this, n](int32) { if (!bJoinOpen) { OnTabSelected(n); } });   // 66817, a6 = n
	}

	// ── Статичные подписи (`uiWindowInit_74000`) ──
	auto Set = [](UTextBlock* T, const FString& S) { if (T) { T->SetText(FText::FromString(S)); } };
	Set(TopText[0], Loc(FishBookLoc::kTotalRate));     // 74101
	Set(TopText[2], Loc(FishBookLoc::kTotalKinds));    // 74103
	Set(GgText[0], Loc(FishBookLoc::kCaught));         // 74041
	Set(GgText[1], Loc(FishBookLoc::kKind));           // 74042
	Set(GgText[2], Loc(FishBookLoc::kRating));         // 74043 (74044 «%» — разметка)
	Set(InfoCap[0], Loc(FishBookLoc::kRank));          // 74061
	Set(InfoCap[1], Loc(FishBookLoc::kMaxSize));       // 74063
	Set(InfoCap[2], Loc(FishBookLoc::kMinSize));       // 74065
	Set(InfoCap[3], Loc(FishBookLoc::kRecords));       // 74067
	Set(InfoCap[4], Loc(FishBookLoc::kDetails));       // 74069
	Set(RewardCap, Loc(FishBookLoc::kRewardCap));      // 74080
	Set(BtnRewardText, Loc(FishBookLoc::kRewardBtn));  // 74090 (`CWidget_SetTextAll4`)
	Set(JoinCaught, Loc(FishBookLoc::kJoinCaught));    // 74210
	Set(JoinHint, Loc(FishBookLoc::kJoinHint));        // 74251
	Set(BtnJoinOkText, Loc(FishBookLoc::kJoinOk));     // 74252
	Set(BtnJoinNoText, Loc(FishBookLoc::kJoinNo));     // 74253
	Set(BtnDelText, Loc(FishBookLoc::kDelBtn));        // 74271
	// Шаблон ячейки прячется (`CControl_SetVisible(tplSlotRoot, 0)`); панель удаления — видима (`SetVisible(74270, 1)`;
	//   позиция (415,703) эталонного `SetField74_75` лежит в АССЕТЕ — шаг `asda_ui_fishbook_clones`).
	AsdaUi::SetShownDecor(CellTemplate, false);
	AsdaUi::SetShownDecor(DelPanel, true);
	AsdaUi::SetShownDecor(Join, false);
	AsdaUi::SetShownDecor(JoinCheck, false);
	for (FAsdaFishBookCellUi& U : CellUi) { AsdaUi::SetShownDecor(U.Root, false); }

	int32 Cells = 0, Inv = 0, Rew = 0, Tabs = 0;
	for (const FAsdaFishBookCellUi& U : CellUi) { Cells += (U.Root && U.Name && U.Pic && U.No && U.Cnt && U.Red && U.Done && U.Que) ? 1 : 0; }
	for (const FAsdaFishBookInvUi& U : InvUi) { Inv += (U.Icon && U.Label) ? 1 : 0; }
	for (const FAsdaFishBookRewardUi& U : RewardUi) { Rew += (U.Icon && U.Name && U.Frame) ? 1 : 0; }
	for (int32 n = 0; n < kMaxTabs; ++n) { Tabs += (TabPlanes[n] && TabButtons[n] && TabTexts[n]) ? 1 : 0; }
	bControlsReady = (Cells == kFish && Inv == kFish && Rew == kRewards && Tabs == kMaxTabs && Join && JoinCheck && BtnJoinOk && BtnReward);
	UE_LOG(LogAsdaFishBook, Log,
		TEXT("[книга рыбака] окно 74000 разобрано: ячеек %d/30, стопок %d/30, наград %d/4, вкладок %d/4, 74200 %s, 74241 %s, ")
		TEXT("74252 %s, 74090 %s, 74270 %s, цифр %d/5 → %s%s"),
		Cells, Inv, Rew, Tabs, Join ? TEXT("да") : TEXT("НЕТ"), JoinCheck ? TEXT("да") : TEXT("НЕТ"), BtnJoinOk ? TEXT("да") : TEXT("НЕТ"),
		BtnReward ? TEXT("да") : TEXT("НЕТ"), DelPanel ? TEXT("да") : TEXT("НЕТ"),
		(Digit[0] ? 1 : 0) + (Digit[1] ? 1 : 0) + (Digit[2] ? 1 : 0) + (Digit[3] ? 1 : 0) + (Digit[4] ? 1 : 0),
		bControlsReady ? TEXT("ГОТОВО") : TEXT("НЕ ГОТОВО"),
		(Cells < kFish || Tabs < kMaxTabs) ? TEXT(" — ★ клонов нет: прогони Tools/asda_ui_fishbook_clones.py") : TEXT(""));
}

void UAsdaFishBookSubsystem::RegisterZone()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Корпус окна — девять планов рамки `Win_Mg` (`FISH_BOOK_WIN_0_0…8`).
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kBookWindowId, this, Widget, bOpen, TEXT("FISH_BOOK_WIN_0_%d"), 9);
}

// ═════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ════════════════════════════════════════════

void UAsdaFishBookSubsystem::Open()
{
	EnsureData();
	if (!EnsureWidget()) { return; }
	if (bOpen) { return; }   // `if (*(BYTE*)(root + 292) != 1)` — повторное открытие ничего не делает
	Sel = 0; Page = 0; TabOfs = 0;   // [462] [463] [464]
	RewardSel = -1;                  // [470]
	// `sowelWindowClose(g_pInventoryWnd, 1)` — МИСНЕЙМ: закрыть сумку.
	if (UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		if (Inv->IsWindowOpen()) { Inv->CloseWindow(); }
	}
	TabSel = 0;                      // [573] (`CControl_SetSelectedIndex(74105, 0)` — ставит `RebuildTabs`)
	RefreshAll();
	bOpen = true;
	TipReward = -1;
	Mouse.Reset();
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kBookWindowId, true);   // показ = подъём поверх (общее правило «открытие поверх всех»)
	}
	CloseJoin();                     // `uiWindowMgr_HideControlAndClose` в конце `itemCollectUiOpen`
	UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] окно открыто: томов зарегистрировано %d, том %d"), ActiveNum, Page);
}

void UAsdaFishBookSubsystem::CloseOrToggle()
{
	if (bJoinOpen) { CloseJoin(); return; }
	HideRewardTip();
	if (!bOpen) { return; }
	bOpen = false;
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kBookWindowId, false);   // `CControl_SetVisible(74000, 0)`
	}
	UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] окно закрыто"));
}

// `uiWindowMgr_HideControlAndClose@0xFD67C0`: спрятать 74200 + снять модальность (`sub_92E450`).
void UAsdaFishBookSubsystem::CloseJoin()
{
	bJoinOpen = false;
	AsdaUi::SetShownDecor(Join, false);
	// ★ 10.10.2026: снятие модальности (`sub_92E450`) — замок ввода пересчитывается сразу.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr) { WM->NotifyModalStateChanged(); }
}

// ═════════════════════════════ РЕНДЕР ═════════════════════════════════════════════════════════

FString UAsdaFishBookSubsystem::LocRaw(int32 Id) const { return AsdaNotice::LocRaw(GetGameInstance(), Id, /*bDebugFallback*/true); }
FString UAsdaFishBookSubsystem::Loc(int32 Id) const { return AsdaNotice::Loc(GetGameInstance(), Id); }

FString UAsdaFishBookSubsystem::Trunc13(const FString& S)
{
	// `for (ii = 0; ii < strlen; ++ii) if (ii > 12) { Buffer[ii..ii+3] = "...\0"; break; }` — первые 13 знаков + «...».
	return (S.Len() > 13) ? (S.Left(13) + TEXT("...")) : S;
}

FString UAsdaFishBookSubsystem::CaptionCut(const FString& S, const UTextBlock* Field, float SidePad)
{
	// ★ fishfix 09.10 (МЕЛК-4): ключ `asda.FishBook.Trunc13` (обоснование — у объявления ключа).
	if (CVarFishBookTrunc13.GetValueOnGameThread() != 0) { return Trunc13(S); }
	const UCanvasPanelSlot* Sl = Field ? AsdaUi::SlotOf(Field) : nullptr;
	if (!Sl || S.IsEmpty()) { return S; }
	// ★ fishfix2 09.10 (МЕЛК-1): полезная ширина = поле минус внутренние поля с обеих сторон (скругления скина вкладки).
	const float MaxW = Sl->GetSize().X - 2.f * FMath::Max(0.f, SidePad);   // ширина поля в разметке (как замер шрифта, масштаб 1)
	const FSlateFontInfo Font = Field->GetFont();
	if (AsdaUi::MeasureTextWidth(S, Font, 0.f) <= MaxW) { return S; }   // эталон: `if (ширина < a3) return Str`
	// Эталон копит ширину знаков, пока не превысит предел, и дописывает «...»; здесь предел — поле МИНУС «...», чтобы строка
	//   с многоточием сама входила в поле ([ПОРТ]: эталон мог вылезти на ширину «...»).
	const float Limit = MaxW - AsdaUi::MeasureTextWidth(TEXT("..."), Font, 0.f);
	int32 N = 0;
	while (N < S.Len() && AsdaUi::MeasureTextWidth(S.Left(N + 1), Font, 0.f) <= Limit) { ++N; }
	return S.Left(N).TrimEnd() + TEXT("...");
}

bool UAsdaFishBookSubsystem::DrawFishFrame(UImage* Img, int32 FishId) const
{
	// `fishGameFindRecordById` → `DrawTextureRect(ctrl, "%s.tex" (+48), 196·col (+112), 128·row (+116), 196, 128)`.
	const UAsdaFishingSubsystem* Fish = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaFishingSubsystem>() : nullptr;
	const FAsdaFishDataRow* R = Fish ? Fish->FindFishData(FishId) : nullptr;
	if (!Img || !R) { return false; }
	return AsdaUi::ApplyJuiIconRect(Img, R->Texture, 196 * R->TexCol, 128 * R->TexRow, 196, 128);
}

void UAsdaFishBookSubsystem::DrawDigit(UImage* Img, int32 D) const
{
	// `CControl_DrawTextureRect(ctrl, "premium_cc.TEX", 32·d, 0, 32, 32)`.
	if (Img) { AsdaUi::ApplyJuiIconCell(Img, TEXT("premium_cc"), 32 * D, 0, 32, 32.f); }
}

// `CItemCollectUI_RefreshAll@0xFD59F0` — порядок 1:1. Модель (вкладки/том) считается и при невидимом окне.
void UAsdaFishBookSubsystem::RefreshAll()
{
	EnsureData();
	RebuildTabs(-1);
	RefreshProgress();
	RefreshGrid();
	RefreshItemDetail();
	DrawCountAndRate();
	RefreshSelectedDetail();
	RefreshRewards();
}

// `gameUi_refreshPagedListBox@0xFDA5A0` (g:612386-612481).
void UAsdaFishBookSubsystem::RebuildTabs(int32 Pick)
{
	for (int32 i = 0; i < kActiveCap; ++i) { ActiveList[i] = -1; }
	ActiveNum = 0;
	for (int32 j = 0; j < St.Num() && ActiveNum < kActiveCap; ++j)
	{
		if (St[j].bReg) { ActiveList[ActiveNum++] = j; }
	}
	// Число ячеек списка: лишние снимаются с начала, пока их ≥ 2 (`list_moveEntryToFrontByKey`); недостающие клонируются до 4.
	if (TabCount >= ActiveNum || TabCount >= kMaxTabs)
	{
		for (int32 k = TabCount - ActiveNum - 1; k >= 0; --k) { if (TabCount >= 2) { --TabCount; } }
	}
	else
	{
		const int32 Add = ActiveNum - TabCount;
		for (int32 m = 0; m < Add; ++m) { if (TabCount < kMaxTabs) { ++TabCount; } }
	}
	if (TabSel > TabCount) { TabSel = TabCount; }   // [ОРИГИНАЛ] Д5: режется до числа вкладок, а не до «−1»
	const int32 Idx = ((Pick == -1) ? TabSel : Pick) + TabOfs;
	if (Idx >= 0 && Idx < kActiveCap && ActiveList[Idx] != -1) { Page = ActiveList[Idx]; }
	// UI: видимы ячейки 0…TabCount−1, подпись = имя тома (≤ 13 знаков + «...») или пусто; выбрана [573].
	for (int32 n = 0; n < kMaxTabs; ++n)
	{
		const bool bShow = n < TabCount;
		if (TabPlanes.IsValidIndex(n)) { AsdaUi::SetShownDecor(TabPlanes[n], bShow); }
		if (TabButtons.IsValidIndex(n)) { AsdaUi::SetShown(TabButtons[n], bShow); }
		if (TabTexts.IsValidIndex(n) && TabTexts[n])
		{
			AsdaUi::SetShownDecor(TabTexts[n], bShow);
			const int32 V = n + TabOfs;
			const bool bName = (V >= 0 && V < ActiveNum && ActiveList[V] != -1 && St.IsValidIndex(ActiveList[V]));
			TabTexts[n]->SetText(FText::FromString(bName ? CaptionCut(St[ActiveList[V]].Name, TabTexts[n], kTabCaptionPad) : FString()));
		}
	}
	if (TabPlanes.Num() == kMaxTabs && TabButtons.Num() == kMaxTabs && TabTexts.Num() == kMaxTabs)
	{
		AsdaUi::ApplyTabState(TabPlanes, TabTexts, TabButtons, TabSel, TabIdleBrush, TabIdleStyle, TabIdleCaption);
	}
}

// `itemCollectUiRefreshProgress@0xFD3B70`: по ВСЕМ зарегистрированным томам.
void UAsdaFishBookSubsystem::RefreshProgress()
{
	int32 Sp = 0, N = 0;
	// ★ Ревизия В3, МЕЛК-4: сумма копится во `float` (g:610411 `float v4`): каждое слагаемое считается в double и
	//   сумма тут же усекается до float — как эталон (`v4 = (double)cnt / (double)req + v4`).
	float Sum = 0.f;
	for (int32 i = 0; i < Tpl.Num() && i < St.Num(); ++i)
	{
		if (Tpl[i].Id == -1 || !St[i].bReg) { continue; }
		for (int32 j = 0; j < kFish; ++j)
		{
			if (Tpl[i].Fish[j] == -1) { continue; }
			Sum = (float)((double)St[i].Cnt[j] / (double)Tpl[i].Req[j] + (double)Sum);
			++N;
			if (St[i].Cnt[j]) { ++Sp; }
		}
	}
	const float Rate = N ? (float)((double)Sum / (double)N * 100.0) : 0.f;   // `v1 = v4 / (double)v6 * 100.0` (float)
	if (TopText.IsValidIndex(1) && TopText[1]) { TopText[1]->SetText(FText::FromString(FString::Printf(TEXT("%0.1f%%"), Rate))); }   // 74102
	if (TopText.IsValidIndex(3) && TopText[3]) { TopText[3]->SetText(FText::FromString(FString::Printf(TEXT("%d"), Sp))); }           // 74104
}

// `CItemCollectUI_RefreshGrid@0xFD3E40`.
void UAsdaFishBookSubsystem::RefreshGrid()
{
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	for (int32 i = 0; i < kFish; ++i)
	{
		FAsdaFishBookCellUi& U = CellUi[i];
		AsdaUi::SetShownDecor(U.Root, false);
		if (U.Name) { U.Name->SetText(FText::GetEmpty()); }
		AsdaUi::ClearImageBrush(U.Pic);   // пустая текстура 32×32
		AsdaUi::SetShownDecor(U.Done, false);
		if (U.No) { U.No->SetText(FText::GetEmpty()); }
		if (U.Cnt) { U.Cnt->SetText(FText::GetEmpty()); }
	}
	const FTpl* T = TplAt(Page);
	const FState* S = StAt(Page);
	if (!T || !S || !S->bReg) { return; }
	for (int32 j = 0; j < kFish; ++j)
	{
		if (T->Id == -1) { continue; }
		FAsdaFishBookCellUi& U = CellUi[j];
		if (T->Fish[j] == -1) { AsdaUi::SetShownDecor(U.Root, false); continue; }
		AsdaUi::SetShownDecor(U.Root, true);
		AsdaUi::SetShownDecor(U.Red, j == Sel);
		const FString No = FString::Printf(TEXT("%03d"), j + 1);
		if (S->Cnt[j] <= 0)
		{
			if (U.Name) { U.Name->SetText(FText::FromString(TEXT("?????"))); }
			if (U.No) { U.No->SetText(FText::FromString(No)); }
			if (U.Cnt) { U.Cnt->SetText(FText::GetEmpty()); }
			continue;
		}
		if (IT && IT->GetItem(T->Fish[j]) && U.Name) { U.Name->SetText(FText::FromString(IT->GetItemName(T->Fish[j]))); }
		DrawFishFrame(U.Pic, T->Fish[j]);
		if (S->Cnt[j] >= T->Req[j]) { AsdaUi::SetShownDecor(U.Done, true); }
		if (U.No) { U.No->SetText(FText::FromString(No)); }
		if (U.Cnt) { U.Cnt->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), S->Cnt[j], T->Req[j]))); }
	}
}

// `CItemCollectUI_RefreshItemDetail@0xFD4440`: ранг (цвет грейда), макс./мин. размер, «%d/%d», описание.
void UAsdaFishBookSubsystem::RefreshItemDetail()
{
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FTpl* T = TplAt(Page);
	const FState* S = StAt(Page);
	auto Set = [](UTextBlock* Tb, const FString& V) { if (Tb) { Tb->SetText(FText::FromString(V)); } };
	const bool bHas = T && S && Sel >= 0 && Sel < kFish && T->Id != -1 && T->Fish[Sel] != -1 && S->Cnt[Sel] > 0 && S->bReg;
	if (!bHas)
	{
		Set(InfoVal[0], FString()); Set(InfoVal[1], FString()); Set(InfoVal[2], FString()); Set(InfoVal[3], FString());
		UAsdaRichText::SetFmt(InfoDesc, FString());
		return;
	}
	if (const FAsdaItemRecord* R = IT ? IT->GetItem(T->Fish[Sel]) : nullptr)
	{
		Set(InfoVal[0], Loc(FishBookLoc::kGradeBase + R->ItemGrade));   // loc(1421 + itemGrade)
		if (InfoVal[0]) { InfoVal[0]->SetColorAndOpacity(FSlateColor(FAsdaGradeColor::Get(R->ItemGrade))); }   // `SetField923(DropColor)`
		FAsdaItemTooltipRow Tip;   // `itemTemplateFindByCode(code) + 4`
		if (IT->GetItemTooltip(T->Fish[Sel], Tip)) { UAsdaRichText::SetFmt(InfoDesc, Tip.Description); }
	}
	Set(InfoVal[1], FString::Printf(TEXT("%dcm"), S->MaxSz[Sel]));
	Set(InfoVal[2], FString::Printf(TEXT("%dcm"), S->MinSz[Sel]));
	Set(InfoVal[3], FString::Printf(TEXT("%d/%d"), S->Cnt[Sel], T->Req[Sel]));
}

// `CItemCollectUI_DrawCountAndRate@0xFD4880`: текущий том (без проверки регистрации — как эталон).
void UAsdaFishBookSubsystem::DrawCountAndRate()
{
	const FTpl* T = TplAt(Page);
	const FState* S = StAt(Page);
	int32 Valid = 0, Collected = 0;
	float Sum = 0.f;   // ★ МЕЛК-4 ревизии В3: `float rateSum` эталона (g:610597), слагаемое в double, сумма усекается до float
	if (T && S)
	{
		for (int32 i = 0; i < kFish; ++i)
		{
			if (T->Fish[i] == -1) { continue; }
			Sum = (float)((double)S->Cnt[i] / (double)T->Req[i] + (double)Sum);
			++Valid;
			if (S->Cnt[i]) { ++Collected; }
		}
	}
	const FString CountStr = FString::Printf(TEXT("%02d"), FMath::Min(Collected, 99));
	for (int32 j = 0; j < 2; ++j) { DrawDigit(Digit[j], CountStr[j] - TEXT('0')); }
	// Д8: том без рыб — у эталона деление 0/0 (NaN → 0 после усечения); здесь явно 0 (вид тот же).
	int32 Rate = 0;
	if (Valid > 0)
	{
		const double V = (double)Sum / (double)Valid * 1000.0;   // `(unsigned __int64)(rateSum / (double)validCount * 1000.0)`
		Rate = (V >= 0.0 && V < 2147483647.0) ? (int32)(uint64)V : 0;
	}
	if (Rate > 999) { Rate = 999; }
	const FString RateStr = FString::Printf(TEXT("%03d"), Rate);
	for (int32 k = 0; k < 3; ++k) { DrawDigit(Digit[k + 2], RateStr[k] - TEXT('0')); }
}

// `CItemCollectUI_RefreshSelectedDetail@0xFD4C40`: крупная карточка (74051…74054).
void UAsdaFishBookSubsystem::RefreshSelectedDetail()
{
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FTpl* T = TplAt(Page);
	const FState* S = StAt(Page);
	const bool bHas = T && S && Sel >= 0 && Sel < kFish && T->Id != -1 && T->Fish[Sel] != -1 && S->Cnt[Sel] > 0 && S->bReg;
	const FString No = FString::Printf(TEXT("%03d"), Sel + 1);
	if (!bHas)
	{
		if (CardName) { CardName->SetText(FText::FromString(TEXT("?????"))); }
		AsdaUi::ClearImageBrush(CardPic);
		if (CardNo) { CardNo->SetText(FText::FromString(No)); }
		if (CardCnt) { CardCnt->SetText(FText::GetEmpty()); }
		return;
	}
	if (IT && IT->GetItem(T->Fish[Sel]) && CardName) { CardName->SetText(FText::FromString(IT->GetItemName(T->Fish[Sel]))); }
	DrawFishFrame(CardPic, T->Fish[Sel]);
	if (CardNo) { CardNo->SetText(FText::FromString(No)); }
	if (CardCnt) { CardCnt->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), S->Cnt[Sel], T->Req[Sel]))); }
}

// `itemCollectUI_RefreshInputSlots@0xFD5020`: 4 награды тома (только у зарегистрированного).
void UAsdaFishBookSubsystem::RefreshRewards()
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	for (int32 t = 0; t < kRewards; ++t)
	{
		FAsdaFishBookRewardUi& U = RewardUi[t];
		AsdaUi::ClearImageBrush(U.Icon);
		if (U.Qty) { U.Qty->SetText(FText::GetEmpty()); }
		if (U.Name) { U.Name->SetText(FText::GetEmpty()); }
		AsdaUi::SetShownDecor(U.Frame, false);
	}
	const FTpl* T = TplAt(Page);
	const FState* S = StAt(Page);
	if (!T || !S || !S->bReg || !IT) { return; }
	for (int32 j = 0; j < kRewards; ++j)
	{
		const int32 Id = T->Reward[j];
		if (Id == -1) { continue; }
		FAsdaFishBookRewardUi& U = RewardUi[j];
		if (j == RewardSel) { AsdaUi::SetShownDecor(U.Frame, true); }
		const FString QtyStr = (T->RewardCnt[j] > 1) ? FString::Printf(TEXT("%d"), T->RewardCnt[j]) : FString();
		// Обычный предмет (`ImgOption`), спец-предмет (64×64 `Grid_IndexToCellOffset8x64`), самоцвет (32×32) — общая точка иконки.
		const bool bItem = IT->GetItem(Id) != nullptr;
		FAsdaSpecialItemlistRow Sp;
		const bool bSpecial = IT->GetSpecialItem(Id, Sp);
		FAsdaSowellRow Sw;
		const bool bSowel = IT->GetSowell(Id, Sw);
		if (bItem || bSpecial || bSowel) { AsdaUi::ApplyItemIcon(GI, U.Icon, Id, FVector2D(64.f, 64.f)); }
		if (U.Qty && !QtyStr.IsEmpty()) { U.Qty->SetText(FText::FromString(QtyStr)); }
		FString Name;
		if (bItem) { Name = IT->GetItemName(Id); }
		if (bSpecial)
		{
			FAsdaSpecialitemTooltiplistRow Tip;   // `itemGetTooltipById(code)->name`
			if (IT->GetSpecialItemTooltip(Id, Tip)) { Name = Tip.Name; }
		}
		if (bSowel) { Name = Sw.Name; }        // `sowelTableFindRecordByCode`
		if (U.Name && (bItem || bSpecial || bSowel)) { U.Name->SetText(FText::FromString(CaptionCut(Name, U.Name, kRewardCaptionPad))); }
	}
}

// `fishCollectionOpenWindow@0xFD6720`.
void UAsdaFishBookSubsystem::OpenJoin(int32 FishId)
{
	bJoinOpen = true;
	AsdaUi::SetShownDecor(Join, true);
	// ★ 10.10.2026: `helper_setupFields(g_pWindowMgr, 74200, 1)` — замок ввода модальным контролом (роспись в `EnsureWidget`).
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr) { WM->NotifyModalStateChanged(); }
	PickedCell = -1; ChosenCell = -1; Qty = 0;   // [465] [466] [467]
	JoinFish = FishId;                           // [468]
	RefreshJoin(JoinFish);
	UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] окно регистрации 74200: рыба %d (том %d, ячейка %d)"), FishId, Page, Sel);
}

// `CFishCollection_RefreshDetailUI@0xFD6800` (g:611193-611309).
void UAsdaFishBookSubsystem::RefreshJoin(int32 FishId)
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const FTpl* T = TplAt(Page);
	const FState* S = StAt(Page);
	if (!T || !S || Sel < 0 || Sel >= kFish) { return; }
	// ★ [ПОРТ] Ревизия В3, ВАЖН-3 (ключ `asda.FishBook.LiveRefresh`) — сверка выбора с сумкой; правило — `RevalidateJoinChoice`.
	RevalidateJoinChoice();
	const int32 Fish = T->Fish[Sel];
	DrawFishFrame(JoinPic, Fish);   // 74202 — кадр `tbl[463].fish[462]`
	if (IT && IT->GetItem(Fish))
	{
		UAsdaRichText::SetFmt(JoinTitle, AsdaUi::FormatPrintfArgs(LocRaw(FishBookLoc::kJoinTitle), { IT->GetItemName(Fish) }));   // 74201
	}
	AsdaUi::SetShownDecor(JoinDone, S->Cnt[Sel] >= T->Req[Sel]);   // 74203 «собрано»
	if (JoinK) { JoinK->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), S->Cnt[Sel], T->Req[Sel]))); }   // 74204
	if (JoinR1) { JoinR1->SetText(FText::FromString(FString::Printf(TEXT("%d"), S->Cnt[Sel]))); }                  // 74205
	if (JoinR2) { JoinR2->SetText(FText::FromString(FString::Printf(TEXT("%d"), Qty))); }                          // 74206 (без «+»)
	if (JoinR3) { JoinR3->SetText(FText::FromString(FString::Printf(TEXT("%d"), T->Req[Sel]))); }                  // 74207
	for (int32 k = 0; k < kFish; ++k)
	{
		AsdaUi::ClearImageBrush(InvUi[k].Icon);
		if (InvUi[k].Label) { InvUi[k].Label->SetText(FText::GetEmpty()); }
		InvCell[k] = -1;
	}
	AsdaUi::SetShownDecor(JoinCheck, false);
	if (!Inv || !IT) { return; }
	const bool bLegacy = CVarFishBookLegacyCursor.GetValueOnGameThread() != 0;
	const FAsdaItemInstance* Chosen = (ChosenCell >= 0) ? Inv->GetCell(kBagContainer, ChosenCell) : nullptr;
	int32 N = 0;
	for (int32 j = 0; j < kSpendCells; ++j)
	{
		const FAsdaItemInstance* It = Inv->GetCell(kBagContainer, j);
		if (!It) { continue; }
		const FAsdaItemRecord* R = IT->GetItem(It->ItemCode);   // `CItemManager_GetItemRecord(*(DWORD*)запись)`
		if (!R || R->ItemCode != FishId) { continue; }
		InvCell[N] = j;
		AsdaUi::ApplyItemIcon(GI, InvUi[N].Icon, It->ItemCode, FVector2D(32.f, 32.f));
		if (It->Amount > 0 && InvUi[N].Label) { InvUi[N].Label->SetText(FText::FromString(FString::Printf(TEXT("%d"), It->Amount))); }
		if (Chosen)
		{
			// ★ Р7/Д1: эталон ставит курсор при `slot(выбранной) != slot(этой)` (0xFD6DB3 `cmp` / 0xFD6DB5 `je`).
			const bool bSame = (Chosen->SlotIndex == It->SlotIndex);
			if (bLegacy ? !bSame : bSame) { PlaceCursorOn(InvUi[N].Icon, bLegacy); }
		}
		if (++N >= kFish) { break; }
	}
}

// [ПОРТ] ВАЖН-3: ячейка сумки Cell (контейнер 2) непуста, её предмет — рыба окна 74200 (тот же признак, что у перебора
//   `RefreshJoin`: `CItemManager_GetItemRecord(код)->code == [468]`) и стопка не меньше MinAmount.
bool UAsdaFishBookSubsystem::IsJoinCellValid(int32 Cell, int32 MinAmount) const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemInstance* It = (Inv && Cell >= 0) ? Inv->GetCell(kBagContainer, Cell) : nullptr;
	if (!It || It->IsEmpty() || !IT) { return false; }
	const FAsdaItemRecord* R = IT->GetItem(It->ItemCode);
	return R && R->ItemCode == JoinFish && It->Amount >= FMath::Max(1, MinAmount);
}

// ★ [ПОРТ] Ревизия В3, ВАЖН-3 (ключ `asda.FishBook.LiveRefresh`): 74200 у порта немодально к ЧУЖИМ окнам — сумка под ним
//   может измениться. Выбранная [466] / нажатая [465] стопка, в которой теперь не рыба этого окна (или пусто), снимается
//   (иначе 6180 ушёл бы с ДРУГОЙ рыбой того же тома — сервер ищет индекс по коду предмета, GS:8080); стопка стала меньше
//   введённого — количество [467] режется до стопки. У оригинала 74200 модально (g:611180) — ситуация недостижима.
//   ★ 10.10.2026: с замком ввода (`asda.UI.ModalLock 1`, роспись `RegisterOriginalModal`) 74200 модально и у порта — сумка под
//   ним мышью и клавишами не меняется; сверка остаётся страховкой на изменения от сервера и для `asda.UI.ModalLock 0`.
// ★ W3fix2 (ревизия `fishing_impl_W3fix_W6_REVIEW.md`, МЕЛК-2): ОДНО правило для перерисовки (`RefreshJoin`) и «ОК» 74252
//   (`OnJoinOk`). Прежде «ОК» при стопке меньше введённого снимал выбор (111060), а перерисовка урезала количество — расхождение
//   в пределах кадра (сумка поменялась, `Tick` с `bJoinDirty` ещё не прошёл). Принято правило перерисовки: урезать до стопки.
bool UAsdaFishBookSubsystem::RevalidateJoinChoice()
{
	if (CVarFishBookLiveRefresh.GetValueOnGameThread() == 0) { return false; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	bool bChanged = false;
	if (PickedCell >= 0 && !IsJoinCellValid(PickedCell, 1)) { PickedCell = -1; bChanged = true; }
	if (ChosenCell >= 0)
	{
		const FAsdaItemInstance* Ch = (Inv && IsJoinCellValid(ChosenCell, 1)) ? Inv->GetCell(kBagContainer, ChosenCell) : nullptr;
		if (!Ch)
		{
			UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] [ПОРТ] выбранная стопка %d больше не рыба %d — выбор снят"), ChosenCell, JoinFish);
			ChosenCell = -1; Qty = 0; bChanged = true;
		}
		else if (Ch->Amount < Qty)
		{
			UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] [ПОРТ] стопка %d уменьшилась до %d — к сдаче %d → %d"), ChosenCell, Ch->Amount, Qty, Ch->Amount);
			Qty = Ch->Amount; bChanged = true;
		}
	}
	return bChanged;
}

// Курсор 74241 — сосед стопок по родителю (`FISH_BOOK_JOINBOX_MARK00`): эталон `SetField74_75(курсор, ячейка + (10,7))`.
// ★ Р7/Д1 + МЕЛК-1 ревизии В3 — довод за снятие сдвига (+10,+7) ВМЕСТЕ с инверсией (не только «исправить `!=`»): в той же
//   разметке рамки выбора того же скина `Slot_AniSelect` 74111…74114 лежат РОВНО на своих иконках 74081…74084 (Origin 0,0,
//   64×64 у обеих — ui_book §3, строки 72-88), и сам курсор 74241 в `.JUI` стоит ровно на 74211 (0,0 / 32×32, строки 146/176).
//   Разметка задаёт рамку выбора поверх ячейки без сдвига — так и ставим курсор на выбранную стопку.
//   Оригинал (инверсия + сдвиг) — `asda.FishBook.LegacyCursor 1`.
void UAsdaFishBookSubsystem::PlaceCursorOn(const UWidget* Cell, bool bLegacyOffset)
{
	UCanvasPanelSlot* Dst = JoinCheck ? AsdaUi::SlotOf(JoinCheck) : nullptr;
	const UCanvasPanelSlot* Src = Cell ? AsdaUi::SlotOf(Cell) : nullptr;
	if (!Dst || !Src) { return; }
	const FMargin O = Src->GetOffsets();
	const FVector2D Shift = bLegacyOffset ? FVector2D(10.f, 7.f) : FVector2D::ZeroVector;
	Dst->SetPosition(FVector2D(O.Left, O.Top) + Shift);
	AsdaUi::SetShownDecor(JoinCheck, true);
}

// ═════════════════════════════ ДЕЙСТВИЯ ═══════════════════════════════════════════════════════

// `itemCollectUISelectColAndRefresh@0xFD5DB0`.
void UAsdaFishBookSubsystem::OnCellClick(int32 Cell)
{
	const FTpl* T = TplAt(Page);
	if (!T || T->Id == -1 || Cell < 0 || Cell >= kFish || T->Fish[Cell] == -1) { return; }
	Sel = Cell;
	RefreshAll();
}

// `itemCollectUiSelectSlot@0xFD5E40`: рыба есть и поймана → окно регистрации.
void UAsdaFishBookSubsystem::OnCellDoubleClick(int32 Cell)
{
	const FTpl* T = TplAt(Page);
	const FState* S = StAt(Page);
	if (!T || !S || T->Id == -1 || Cell < 0 || Cell >= kFish || T->Fish[Cell] == -1 || S->Cnt[Cell] <= 0) { return; }
	Sel = Cell;
	OpenJoin(T->Fish[Cell]);
}

// `itemCollectUiSelectSlot_5F20@0xFD5F20`: награда с кодом и том зарегистрирован → [470] = t.
//   ★ T = 4 (Д2, только при `asda.FishBook.LegacyReward5 1`): эталон читает `+316 + 4·4` = `кол-во[0]`.
void UAsdaFishBookSubsystem::OnRewardClick(int32 T)
{
	const FTpl* P = TplAt(Page);
	const FState* S = StAt(Page);
	if (!P || !S || P->Id == -1 || T < 0 || T > 4) { return; }
	const int32 Code = (T < kRewards) ? P->Reward[T] : P->RewardCnt[0];
	if (Code == -1 || !S->bReg) { return; }
	RewardSel = T;
	RefreshAll();
}

void UAsdaFishBookSubsystem::OnTabSelected(int32 N)
{
	RewardSel = -1;      // [470] = −1
	TabSel = N;          // [573] = a6
	RebuildTabs(N);
	RefreshAll();
}

// `uiRebuildSelectableListPaged@0xFDA430` (Д6 — все индексы в [472] — безвреден: список тут же пересчитывается).
void UAsdaFishBookSubsystem::OnArrow(int32 Dir)
{
	RewardSel = -1;
	int32 Reg = 0;
	for (const FState& S : St) { Reg += S.bReg ? 1 : 0; }
	if (Dir > 0 && Reg - kMaxTabs > TabOfs) { TabOfs += Dir; }
	if (Dir < 0 && TabOfs > 0) { TabOfs += Dir; }
	RebuildTabs(-1);
	RefreshAll();
}

// 74271: том зарегистрирован → диалог 77 (111017 с именем тома); иначе msgbox 111069.
void UAsdaFishBookSubsystem::OnDeleteClicked()
{
	const FState* S = StAt(Page);
	if (S && S->bReg)
	{
		ShowConfirm(77, AsdaUi::FormatPrintfArgs(LocRaw(FishBookLoc::kDeleteAsk), { S->Name }));   // `itemTooltipBuild@0xFD71A0`
		return;
	}
	MsgBox(LocRaw(FishBookLoc::kDelNotReg), FishBookLoc::kDelNotReg);
}

// `item_sendConfirmWithCostCheck@0xFD94A0`: без подтверждения — сразу 6182.
void UAsdaFishBookSubsystem::OnRewardButton()
{
	const FTpl* T = TplAt(Page);
	const FState* S = StAt(Page);
	if (!T || !S) { return; }
	const int32 Code = (RewardSel >= 0 && RewardSel < kRewards) ? T->Reward[RewardSel] : (RewardSel == 4 ? T->RewardCnt[0] : -1);
	if (Code < 0 || RewardSel == -1) { MsgBox(LocRaw(FishBookLoc::kNoReward), FishBookLoc::kNoReward); return; }
	for (int32 j = 0; j < kFish; ++j)
	{
		if (T->Fish[j] > 0 && S->Cnt[j] < T->Req[j]) { MsgBox(LocRaw(FishBookLoc::kNotDone), FishBookLoc::kNotDone); return; }
	}
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Net) { Net->SendFishBookReward6182(Net->SelectedCharId, S->Id, Code); }
}

// `uiOpenFishDetailContextPopup@0xFD6FC0`: ЛКМ по непустой стопке → нумпад режима 10 у курсора,
//   максимум = min(стопка, требуется − сдано) (может быть ≤ 0 — нумпад всё равно открывается, ОК не пройдёт).
// ★ Ревизия В3, ВАЖН-1 (08.10.2026): общая точка `ShowNumPad` прежде считала максимум ≤ 0 «без предела» и пропускала
//   любое количество (рыба уже собрана → ввод 5 → 6180 с 5 → сервер код 7). Эталон: `uiClampQuantityToMax@0xDA6410`
//   (g:400586-400594) режет поле при `введённое > максимум` без условия `max > 0` (→ «0», нотис 6835 в чат), а
//   `uiUpgradeDlgOnOk@0xDA6850` (g:400726-400731) при `qty > 0` ложно ничего не делает ⇒ «ОК» 74252 → 111060.
//   Теперь: строгий режим общей точки (`bStrictMax`) + та же граница в колбэке (`0 < Q <= Max`).
void UAsdaFishBookSubsystem::OnInvClick(int32 K)
{
	if (K < 0 || K >= kFish || InvCell[K] < 0) { return; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const FAsdaItemInstance* It = Inv ? Inv->GetCell(kBagContainer, InvCell[K]) : nullptr;
	const FTpl* T = TplAt(Page);
	const FState* S = StAt(Page);
	if (!It || !WM || !T || !S || Sel < 0 || Sel >= kFish) { return; }
	PickedCell = InvCell[K];   // [465]
	int32 Max = It->Amount;
	const int32 Need = T->Req[Sel] - S->Cnt[Sel];
	if (Max > Need) { Max = Need; }
	DebugLastNumPadMax = Max;
	TWeakObjectPtr<UAsdaFishBookSubsystem> WeakThis(this);
	// Режим 10: OK → `uiUpgradeDlgOnOk@0xDA6850` (`qty > 0` ⇒ `fishCollectionSelectDetail`), Cancel → только закрыть.
	//   Поле режет СТРОГИЙ максимум общей точки (`uiClampQuantityToMax` дословно, нотис 6835); колбэк дополнительно
	//   принимает только `0 < Q <= Max` (при максимуме ≤ 0 — никогда).
	WM->ShowNumPad(Max, [WeakThis, Max](bool bOk, int32 Q)
	{
		if (bOk && Q > 0 && Q <= Max && WeakThis.IsValid()) { WeakThis->OnNumPadAccepted(Q); }
	}, /*bCenterOnScreen*/ false, /*bStrictMax*/ true);
	UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] стопка %d (сумка %d, код %d, стопка %d) → нумпад режима 10, максимум %d"),
		K, InvCell[K], It->ItemCode, It->Amount, Max);
}

// `fishCollectionSelectDetail@0xFD70F0`: [467] = кол-во, [466] = [465], перерисовка окна регистрации. Пакета НЕТ.
void UAsdaFishBookSubsystem::OnNumPadAccepted(int32 InQty)
{
	// [ПОРТ] ВАЖН-3 (LiveRefresh): пока нумпад был открыт, стопку могли переложить — её место заняла не рыба этого окна.
	if (CVarFishBookLiveRefresh.GetValueOnGameThread() != 0 && !IsJoinCellValid(PickedCell, 1))
	{
		UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] [ПОРТ] ОК нумпада: в ячейке %d уже не рыба %d — не принято"), PickedCell, JoinFish);
		PickedCell = -1;
		RefreshJoin(JoinFish);
		return;
	}
	Qty = InQty;
	ChosenCell = PickedCell;
	RefreshJoin(JoinFish);
	UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] к сдаче: стопка сумки %d × %d"), ChosenCell, Qty);
}

// `netSendPacket6180@0xFD8800`: нет выбранной стопки → 111060; иначе 6180 (индекс тома, введённое количество).
void UAsdaFishBookSubsystem::OnJoinOk()
{
	// [ПОРТ] ВАЖН-3 (LiveRefresh): выбранная стопка обязана всё ещё быть рыбой окна — иначе выбор снимается и показывается тот же
	//   отказ 111060 («не выбрана рыба»), что у эталона без выбора. ★ W3fix2 МЕЛК-2: стопка меньше введённого — количество
	//   урезается до стопки (то же правило, что у перерисовки, `RevalidateJoinChoice`), и 6180 уходит с урезанным.
	if (ChosenCell >= 0 && RevalidateJoinChoice())
	{
		UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] [ПОРТ] 74252: выбор сверен с сумкой — стопка %d, к сдаче %d"), ChosenCell, Qty);
		RefreshJoin(JoinFish);
	}
	if (ChosenCell < 0) { MsgBox(LocRaw(FishBookLoc::kNoFish), FishBookLoc::kNoFish); return; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const FAsdaItemInstance* It = Inv ? Inv->GetCell(kBagContainer, ChosenCell) : nullptr;
	if (!Net) { return; }
	// Поля берутся из САМОЙ записи (+4/+5/+14) — как эталон, даже если стопка с тех пор опустела (ответит сервер).
	TArray<uint8> Rec = It ? It->Raw : TArray<uint8>();
	if (Rec.Num() < FAsdaItemInstance::kRecordSize) { Rec.SetNumZeroed(FAsdaItemInstance::kRecordSize); }
	Net->SendFishBookUpdate6180(Net->SelectedCharId, Page, Rec, Qty);
}

// ═════════════════════════════ ДИАЛОГИ 77 / 79 ════════════════════════════════════════════════

void UAsdaFishBookSubsystem::MsgBox(const FString& Text, int32 LocId)
{
	// `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)` = ONEBUT_MSGBOX 1500.
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::System, Text);
	UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] msgbox (%d): %s"), LocId, *Text);
}

// TWOBUT (пара (1, Ctx)) — повтор той же пары в очереди отбрасывается (`objList_ContainsPair`).
void UAsdaFishBookSubsystem::ShowConfirm(int32 Ctx, const FString& Text)
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	TWeakObjectPtr<UAsdaFishBookSubsystem> WeakThis(this);
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Text, [WeakThis, Ctx](bool bOk)
	{
		if (UAsdaFishBookSubsystem* S = WeakThis.Get()) { S->OnConfirmAnswer(Ctx, bOk); }
	}, 0.f, FishBookLoc::kDlgOk, FishBookLoc::kDlgCancel, Ctx, 1);
	UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] диалог %d: %s"), Ctx, *Text);
}

// `OnSendItemUpgrade@0xD74180`: OK 0x4D → 6178 (g:380097), OK 0x4F → 6176 (g:380104); Отмена 79 при fishState ≠ 0 → 100 (g:380708).
void UAsdaFishBookSubsystem::OnConfirmAnswer(int32 Ctx, bool bOk)
{
	UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] ответ диалога %d: %s"), Ctx, bOk ? TEXT("OK") : TEXT("Отмена"));
	if (bOk)
	{
		if (Ctx == 77) { Send6178(); }
		if (Ctx == 79) { Send6176(); }
		return;
	}
	if (Ctx == 79)
	{
		if (UAsdaFishingSubsystem* Fish = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaFishingSubsystem>() : nullptr)
		{
			Fish->OnBookDialogCancelled();
		}
	}
}

// `CItem_BuildTooltipText` case 'O' (g:378895-378910): запись [469] есть в item_list → 111018 (имя предмета-книги, имя игрока); иначе пусто.
// ★ 09.10.2026 [ОРИГИНАЛ] Порядок аргументов — по дизасму 0xD7049D…0xD704DC: первым кладётся `GetLocalPlayer()+0x420` (последний %s),
//   вторым — `ItemData_FindByCode(itemCode)` (первый %s). Декомпил подписал их fmtArg0/fmtArg1 наоборот — порт ставил имя игрока
//   на место «рыбы». Ручная сверка: Reports/owner_1009/fish111018.md.
FString UAsdaFishBookSubsystem::Dialog79Text() const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UAsdaWorldSubsystem* World = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const FAsdaItemInstance* It = (Inv && BookCell >= 0) ? Inv->GetCell(kBagContainer, BookCell) : nullptr;
	if (!It || !IT || !IT->GetItem(It->ItemCode)) { return FString(); }
	const FString Self = World ? World->GetSelfCharName() : FString();   // `CharManager_GetLocalPlayer()->name` — второй %s
	const FString ItemName = IT->GetItemName(It->ItemCode);             // `ItemData_FindByCode(itemRec->itemCode)` — первый %s
	return AsdaUi::FormatPrintfArgs(LocRaw(FishBookLoc::kRegistAsk), { ItemName, Self });
}

// `acSendChallenge6176@0xFD7B20`: запись [469] → 6176; затем байт +1884 := 1 (ждём 6177).
bool UAsdaFishBookSubsystem::Send6176()
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return false; }
	const FAsdaItemInstance* It = (Inv && BookCell >= 0) ? Inv->GetCell(kBagContainer, BookCell) : nullptr;
	TArray<uint8> Rec = It ? It->Raw : TArray<uint8>();
	if (Rec.Num() < FAsdaItemInstance::kRecordSize) { Rec.SetNumZeroed(FAsdaItemInstance::kRecordSize); }
	const bool bOk = Net->SendFishBookRegist6176(Net->SelectedCharId, Rec);
	bAwaitRegist = true;
	return bOk;
}

// `netSendSecuredOp6178@0xFD7360`: id тома `g_nUpgradeListIndex` (= [463]).
bool UAsdaFishBookSubsystem::Send6178()
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const FState* S = StAt(Page);
	return (Net && S) ? Net->SendFishBookDelete6178(Net->SelectedCharId, S->Id) : false;
}

// `CInventory_OnSlotUse@0xD07460` ветка подтипа 210 → `itemTooltipSetCountAndRefresh@0xFD7150`.
bool UAsdaFishBookSubsystem::OnUseBookItem(int32 Container, int32 Cell)
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemInstance* It = (Inv && Container == kBagContainer) ? Inv->GetCell(Container, Cell) : nullptr;
	const FAsdaItemRecord* R = (It && IT) ? IT->GetItem(It->ItemCode) : nullptr;
	if (!R || R->ItemSubType != kBookSubType) { return false; }
	EnsureData();
	BookCell = Cell;   // [469] = запись
	ShowConfirm(79, Dialog79Text());
	return true;
}

// ═════════════════════════════ АВТОМАТ РЫБАЛКИ (В1) ═══════════════════════════════════════════

// FSM 101 (g:608344-608352): нет диалога (1, 79) в очереди И не ждём 6177 → найти книгу НЕзарегистрированного тома:
//   есть → диалог 79 (остаёмся в 101), нет → false (автомат уходит в 100). Иначе — остаёмся в 101.
bool UAsdaFishBookSubsystem::BookRegisterProbe()
{
	EnsureData();
	const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const bool bQueued = WM && WM->IsMsgBoxQueued(1, 79);   // `listContainsId(g_pItemTooltipUI, 1, 79)`
	if (!bQueued && !bAwaitRegist)
	{
		if (!FindFirstValidItem()) { return false; }
		ShowConfirm(79, Dialog79Text());
	}
	return true;
}

// `inventoryFindFirstValidItem@0xFDAF40` + `inventoryArray2_FindById@0xFDB010` (первая ячейка контейнера 2 с кодом +0).
bool UAsdaFishBookSubsystem::FindFirstValidItem()
{
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return false; }
	for (int32 i = 0; i < Tpl.Num() && i < St.Num(); ++i)
	{
		if (Tpl[i].Id == -1 || St[i].bReg) { continue; }
		// ★ Ревизия В3, МЕЛК-3: Р12 открыл путь `BookItemId == −1` (серверные тома 5…8: шаблон есть, книги-предмета нет) —
		//   у эталона книга −1 не бывала; без пропуска пустая запись с кодом −1/0 «нашлась» бы как книга.
		if (Tpl[i].BookItemId <= 0) { continue; }
		for (int32 j = 0; j < kSpendCells; ++j)
		{
			const FAsdaItemInstance* It = Inv->GetCell(kBagContainer, j);
			if (It && !It->IsEmpty() && It->ItemCode == Tpl[i].BookItemId) { BookCell = j; return true; }
		}
	}
	return false;
}

// `questTableUpdateKeyMinMax@0xFDACF0` (из 6172 код 1, g:607456): первая поимка вида → счёт 1, макс. = мин. = размер;
//   повторная — только макс./мин. Перерисовки нет (как эталон).
void UAsdaFishBookSubsystem::OnFishCaught(int32 ItemId, int32 SizeCm)
{
	EnsureData();
	for (int32 i = 0; i < Tpl.Num() && i < St.Num(); ++i)
	{
		if (Tpl[i].Id == -1 || !St[i].bReg) { continue; }
		for (int32 j = 0; j < kFish; ++j)
		{
			if (Tpl[i].Fish[j] == -1 || Tpl[i].Fish[j] != ItemId) { continue; }
			FState& S = St[i];
			if (S.Cnt[j])
			{
				if (SizeCm > S.MaxSz[j]) { S.MaxSz[j] = SizeCm; }
				if (SizeCm < S.MinSz[j]) { S.MinSz[j] = SizeCm; }
			}
			else
			{
				S.Cnt[j] = 1; S.MaxSz[j] = SizeCm; S.MinSz[j] = SizeCm;
			}
		}
	}
	UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] улов %d (%d см) учтён в зарегистрированных томах"), ItemId, SizeCm);
}

// ═════════════════════════════ ПРИЁМ ══════════════════════════════════════════════════════════

void UAsdaFishBookSubsystem::DrainPackets()
{
	TArray<FPkt> Q;
	PendingPkts.Drain(Q);
	if (Q.Num() == 0) { return; }
	EnsureData();
	for (const FPkt& P : Q)
	{
		switch (P.Op)
		{
		case 6174: Apply6174(P.Body); break;
		case 6175: UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] 6175 — конец списка томов (у оригинала обработчика нет)")); break;
		case 6177: Apply6177(P.Body); break;
		case 6179: Apply6179(P.Body); break;
		case 6181: Apply6181(P.Body); break;
		case 6183: Apply6183(P.Body); break;
		default: break;
		}
	}
}

// `listParseRosterEntryPacket@0xFD71E0`: 305 б, индекс тома = @0. Окно НЕ перерисовывается.
void UAsdaFishBookSubsystem::Apply6174(const TArray<uint8>& B)
{
	if (B.Num() < 305) { return; }
	SetRosterEntry(RdI32(B.GetData()), B.GetData());
	UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] 6174 том %d (тип %d)"), RdI32(B.GetData()), (int32)(int8)B[4]);
}

// `listSetRosterEntry@0xFD86A0`: +0 = 1, +4 = @0, +8 = (i8)@4, счёт/макс./мин. из i16[30] @125/@185/@245 (@5 i32[30] — не читается).
//   ★ Р6/Д3: граница `idx < число томов` (эталон `<=` — запись на одну за массив).
void UAsdaFishBookSubsystem::SetRosterEntry(int32 Idx, const uint8* Rec)
{
	if (!Rec || Idx < 0 || Idx >= St.Num()) { UE_LOG(LogAsdaFishBook, Warning, TEXT("[книга рыбака] запись тома %d вне таблицы (%d) — пропуск"), Idx, St.Num()); return; }
	FState& S = St[Idx];
	S.bReg = true;
	S.Id = RdI32(Rec);
	S.B8 = (int8)Rec[4];
	for (int32 i = 0; i < kFish; ++i)
	{
		S.Cnt[i] = RdI16(Rec + 125 + 2 * i);
		S.MaxSz[i] = RdI16(Rec + 185 + 2 * i);
		S.MinSz[i] = RdI16(Rec + 245 + 2 * i);
	}
}

void UAsdaFishBookSubsystem::SetWeight(int32 W, const TCHAR* Src)
{
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->SetSelfWeight(AsdaWeightFromWire(W), -1, Src);   // `HUD_UpdateMiniWeightBar(g_pHUD, вес)`; ★ 29.10.2026: вес u16 (6177/6181/6183)
	}
}

// Запись 57 б ответа: кол-во −1 → ячейка очищается, ≥ 0 → кладётся целиком; < −1 — эталон не трогает (g:611747-611788).
void UAsdaFishBookSubsystem::ApplyBagRecord(const uint8* Rec57)
{
	const int32 Amount = RdI32(Rec57 + 9);
	if (Amount < -1) { return; }
	if (UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		TArray<uint8> R;
		R.Append(Rec57, FAsdaItemInstance::kRecordSize);
		Inv->ApplyExternalRecord(R);   // общий путь (`invHandleItemSlotSyncPacket`): −1 ⇒ очистить, иначе положить
	}
}

// `invHandleItemUseResultPacket@0xFD81C0`: ВСЕГДА байт +1884 := 0 и `itemCollectUiShowOrToggle`; код 1 → чат 111073 (%d = @60),
//   вес, запись тома, запись рюкзака; код 5 → msgbox 111020; прочие — молча.
void UAsdaFishBookSubsystem::Apply6177(const TArray<uint8>& B)
{
	if (B.Num() < 365) { return; }
	bAwaitRegist = false;
	CloseOrToggle();
	const uint8 Code = B[0];
	if (Code == 1)
	{
		const FString Txt = AsdaUi::FormatPrintfArgs(LocRaw(FishBookLoc::kRegistered), { FString::FromInt(RdI32(B.GetData() + 60)) });
		AsdaNotice::ChatColored(GetGameInstance(), EAsdaMsgCategory::System, Txt, UAsdaChatSubsystem::ColGold(), 10000.f);
		SetWeight(RdI16(B.GetData() + 1), TEXT("6177"));
		SetRosterEntry(RdI32(B.GetData() + 60), B.GetData() + 60);
		ApplyBagRecord(B.GetData() + 3);
		UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] 6177 код 1: том %d зарегистрирован"), RdI32(B.GetData() + 60));
	}
	else if (Code == 5)
	{
		MsgBox(LocRaw(FishBookLoc::kAlready), FishBookLoc::kAlready);
	}
	else
	{
		UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] 6177 код %d — эталон молчит"), (int32)Code);
	}
}

// `itemCollectOnRemovePacket@0xFD79B0`: код 1 → тома с id == @1 снимаются (перерисовка на каждый), msgbox 111072.
void UAsdaFishBookSubsystem::Apply6179(const TArray<uint8>& B)
{
	if (B.Num() < 5) { return; }
	if (B[0] != 1) { UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] 6179 код %d — эталон молчит"), (int32)B[0]); return; }
	const int32 Id = RdI32(B.GetData() + 1);
	for (int32 i = 0; i < St.Num(); ++i)
	{
		if (St[i].Id == Id) { St[i].bReg = false; RefreshAll(); }
	}
	MsgBox(LocRaw(FishBookLoc::kDeleted), FishBookLoc::kDeleted);
}

// `netHandleInventoryUpdate_9020@0xFD9020`: код 1 → вес, том, запись; код ≠ 0,1 → msgbox «%d» ([ОРИГИНАЛ] Д7);
//   ВСЕГДА — перерисовка и закрытие окна регистрации.
void UAsdaFishBookSubsystem::Apply6181(const TArray<uint8>& B)
{
	if (B.Num() < 365) { return; }
	const uint8 Code = B[0];
	if (Code == 1)
	{
		SetWeight(RdI16(B.GetData() + 1), TEXT("6181"));
		SetRosterEntry(RdI32(B.GetData() + 60), B.GetData() + 60);
		ApplyBagRecord(B.GetData() + 3);
	}
	else if (Code != 0)
	{
		MsgBox(FString::Printf(TEXT("%d"), (int32)Code), 0);
	}
	RefreshAll();
	CloseJoin();
	UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] 6181 код %d"), (int32)Code);
}

// `netHandleInventoryUpdate@0xFD9DF0`: только код 1 — нотис 111013 по КАЖДОЙ таблице, где нашёлся код награды
//   (предметы / самоцветы / вторая таблица), вес, тома с id == @1 снимаются, запись рюкзака. Иные коды — молча
//   (коды 4/6 из `Recv_EndFishing` разбирает автомат рыбалки — Р2).
void UAsdaFishBookSubsystem::Apply6183(const TArray<uint8>& B)
{
	if (B.Num() < 64) { return; }
	if (B[0] != 1) { return; }
	const int32 PageId = RdI32(B.GetData() + 1);
	const int32 Code = RdI32(B.GetData() + 7);
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	// Имя тома — `state + 436·pageId + 12` (по ИНДЕКСУ, не по id); вне таблицы — пусто (эталон читал бы мусор).
	const FString Tome = St.IsValidIndex(PageId) ? St[PageId].Name : FString();
	if (IT)
	{
		if (IT->GetItem(Code))
		{
			MsgBox(AsdaUi::FormatPrintfArgs(LocRaw(FishBookLoc::kRewardDone), { Tome, IT->GetItemName(Code) }), FishBookLoc::kRewardDone);
		}
		FAsdaSowellRow Sw;
		if (IT->GetSowell(Code, Sw))
		{
			MsgBox(AsdaUi::FormatPrintfArgs(LocRaw(FishBookLoc::kRewardDone), { Tome, Sw.Name }), FishBookLoc::kRewardDone);
		}
		FAsdaSpecialItemlistRow Sp;
		if (IT->GetSpecialItem(Code, Sp))
		{
			FAsdaSpecialitemTooltiplistRow Tip;
			const FString Nm = IT->GetSpecialItemTooltip(Code, Tip) ? Tip.Name : FString();
			MsgBox(AsdaUi::FormatPrintfArgs(LocRaw(FishBookLoc::kRewardDone), { Tome, Nm }), FishBookLoc::kRewardDone);
		}
	}
	SetWeight(RdI16(B.GetData() + 5), TEXT("6183"));
	for (int32 i = 0; i < St.Num(); ++i)
	{
		if (St[i].Id == PageId) { St[i].bReg = false; RefreshAll(); }
	}
	ApplyBagRecord(B.GetData() + 7);
	UE_LOG(LogAsdaFishBook, Log, TEXT("[книга рыбака] 6183 код 1: том %d, награда %d"), PageId, Code);
}

// ═════════════════════════════ ПОКАДРОВЫЙ ОПРОС ═══════════════════════════════════════════════

void UAsdaFishBookSubsystem::HideRewardTip()
{
	if (TipReward < 0) { return; }
	TipReward = -1;
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::FishBook);   // `CControl_KillFocus(&g_ItemTooltip)`
	}
}

// Окно двойного щелчка (0xD = `WM_LBUTTONDBLCLK`): системный интервал Windows — то же значение, что у оригинала.
double UAsdaFishBookSubsystem::DoubleClickSec()
{
	return FAsdaDblClick::WindowSec();   // ★ U1 (В2-A дедупа №2): общая точка порога Windows (то же значение)
}

// Курсор над хит-рамкой клона 74029 ячейки i (объединение 8 планов `_CLICK_k`, −1,−1 / 102×92 от рамки 74020);
//   планов нет (старый ассет) — прямоугольник группы.
bool UAsdaFishBookSubsystem::IsCursorOverCellHit(int32 I) const
{
	if (!CellUi.IsValidIndex(I)) { return false; }
	const FAsdaFishBookCellUi& U = CellUi[I];
	FVector2D Mn(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
	FVector2D Mx(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());
	bool bAny = false;
	for (const TObjectPtr<UWidget>& W : U.Click)
	{
		FVector2D A, B;
		if (W && AsdaUi::WidgetAbsRect(W, A, B) && !(B - A).IsNearlyZero())
		{
			Mn = FVector2D(FMath::Min(Mn.X, A.X), FMath::Min(Mn.Y, A.Y));
			Mx = FVector2D(FMath::Max(Mx.X, B.X), FMath::Max(Mx.Y, B.Y));
			bAny = true;
		}
	}
	if (!bAny) { return AsdaUi::IsCursorOverWidget(U.Root); }
	if (!FSlateApplication::IsInitialized()) { return false; }
	const FVector2D C = FSlateApplication::Get().GetCursorPos();
	return C.X >= Mn.X && C.X <= Mx.X && C.Y >= Mn.Y && C.Y <= Mx.Y;
}

// События мыши `uiItemWindow_WndProc@0xFD5A70`: 8 наведение (награды), 0xA уход, 0xB ЛКМ, 0xD двойной (по живой геометрии).
void UAsdaFishBookSubsystem::TickInput()
{
	if (!bOpen || !bControlsReady) { return; }
	Mouse.Update();
	UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	// Модальный гейт (нумпад/нотис): эталон мышь окну не отдаёт.
	// ★ Ревизия В3, ВАЖН-2 (08.10.2026): + обязательные первая и вторая строки опроса по контракту
	//   `AsdaWindowSubsystem.h` (`IsPointerBlockedByTopmost` — всплывающий слой чужого окна, `IsCursorOverHigherWindow` — обычное
	//   окно выше книги): эталон `uiWindowMgrDispatchMouse@0x12AF540` отдаёт мышь ТОЛЬКО верхнему корню под курсором — сквозь
	//   сумку/справку/список каналов поверх книги (810×756 — почти весь экран 1024×768) ячейки и награды не выбираются.
	// ★ 10.10.2026: гейт «для окна» — собственный модальный контрол 74200 опрос книги не запирает (замок ввода эталона).
	if (AsdaUi::IsModalDispatchBlockedFor(GI, kBookWindowId)
		|| (WM && (WM->IsPointerBlockedByTopmost(kBookWindowId) || WM->IsCursorOverHigherWindow(kBookWindowId))))
	{
		HideRewardTip(); Mouse.Reset(); return;
	}
	// [ПОРТ] модальность 74200 внутри книги: под окном регистрации отвечают только его стопки.
	if (bJoinOpen)
	{
		HideRewardTip();
		if (Mouse.bLmbDown)
		{
			for (int32 k = 0; k < kFish; ++k)
			{
				if (InvUi[k].Icon && AsdaUi::IsShown(InvUi[k].Icon) && AsdaUi::IsCursorOverWidget(InvUi[k].Icon)) { OnInvClick(k); break; }
			}
		}
		return;
	}
	// Наведение на награду (событие 8 → `itemSlot_BuildTooltipForEquip`, только при коде ≠ −1); уход (0xA).
	int32 Over = -1;
	for (int32 t = 0; t < kRewards && Over < 0; ++t)
	{
		if (RewardUi[t].Icon && AsdaUi::IsCursorOverWidget(RewardUi[t].Icon)) { Over = t; }
	}
	const FTpl* T = TplAt(Page);
	const FState* S = StAt(Page);
	const int32 TipCode = (Over >= 0 && T && S && S->bReg) ? T->Reward[Over] : -1;
	const int32 TipNow = (TipCode != -1) ? Over : -1;
	if (TipNow != TipReward)
	{
		HideRewardTip();
		if (TipNow >= 0)
		{
			TipReward = TipNow;
			if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
			{
				Tip->ShowGachaSlotTooltip(TipCode, EAsdaTipOwner::FishBook);   // ветки `cItemBar_BuildSlotTooltip` = `itemSlot_BuildTooltipForEquip`
			}
		}
	}
	if (!Mouse.bLmbDown) { return; }
	// ЛКМ по ячейке (клон 74029 — 99999900+i): 0xB; повторное НАЖАТИЕ по той же ячейке в пределах системного интервала — 0xD.
	// ★ Ревизия В3, ВАЖН-6: 0xD эталона — `WM_LBUTTONDBLCLK`, окно — `GetDoubleClickTime` Windows (правило ревизии О2, МЕЛК-6:
	//   `AsdaExchangeP2PWindow.cpp` `DoubleClickSec`, `AsdaSkillWindowSubsystem.cpp`), а не свой порог 0,35 с.
	// ★ МЕЛК-5: зона щелчка — хит-рамка клона 74029 (−1,−1 / 102×92) по её 8 планам. Проверено геометрией (самотест E0a):
	//   группа клона, которую строит импортёр, уже совпадает с 74029 (рамка 74020 100×90 лежит в ней с +1,+1), так что
	//   прежняя зона «прямоугольник группы» по факту была той же; зона задана явно, чтобы не зависеть от состава группы.
	for (int32 i = 0; i < kFish; ++i)
	{
		const UWidget* R = CellUi[i].Root;
		if (!R || !AsdaUi::IsShown(R) || !IsCursorOverCellHit(i)) { continue; }
		const double Now = FPlatformTime::Seconds();
		DebugLastClickDelta = (LastClickCell == i) ? (Now - LastClickTime) : -1.0;   // самотест E0c (W3fix2 МЕЛК-3)
		const bool bDouble = (LastClickCell == i) && ((Now - LastClickTime) <= DoubleClickSec());
		LastClickTime = Now;
		LastClickCell = bDouble ? -1 : i;   // третий щелчок подряд — снова первый
		if (bDouble) { OnCellDoubleClick(i); } else { OnCellClick(i); }
		return;
	}
	// ЛКМ по награде 74081…74084 (Д2: и по подписи 74085 при ключе оригинала).
	for (int32 t = 0; t < kRewards; ++t)
	{
		if (RewardUi[t].Icon && AsdaUi::IsCursorOverWidget(RewardUi[t].Icon)) { OnRewardClick(t); return; }
	}
	if (CVarFishBookLegacyReward5.GetValueOnGameThread() != 0 && RewardLabel5 && AsdaUi::IsCursorOverWidget(RewardLabel5))
	{
		OnRewardClick(4);
	}
}

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════

FString UAsdaFishBookSubsystem::DumpControls() const
{
	FString S = TEXT("=== окно «Книга рыбака» 74000: разобранные контролы (живая геометрия) ===\n");
	auto Line = [&S](const FString& Id, const UWidget* W)
	{
		FVector2D Mn = FVector2D::ZeroVector, Mx = FVector2D::ZeroVector;
		const bool bGeo = W && AsdaUi::WidgetAbsRect(W, Mn, Mx);
		const FString Geo = bGeo ? FString::Printf(TEXT("rect=(%.0f,%.0f)-(%.0f,%.0f)"), Mn.X, Mn.Y, Mx.X, Mx.Y) : FString(TEXT("rect=нет"));
		S += FString::Printf(TEXT("  %-40s %s %s %s\n"), *Id, W ? TEXT("есть") : TEXT("НЕТ"),
			(W && AsdaUi::IsShown(W)) ? TEXT("видим") : TEXT("скрыт"), *Geo);
	};
	Line(TEXT("74099 FISH_BOOK_XX_0"), BtnClose);
	Line(TEXT("74106 FISH_BOOK_H_LEFT"), BtnLeft);
	Line(TEXT("74107 FISH_BOOK_H_RIGHT"), BtnRight);
	for (int32 n = 0; n < TabPlanes.Num(); ++n)
	{
		Line(FString::Printf(TEXT("74105 вкладка %d план"), n), TabPlanes[n]);
		Line(FString::Printf(TEXT("74105 вкладка %d кнопка"), n), TabButtons.IsValidIndex(n) ? TabButtons[n].Get() : nullptr);
		Line(FString::Printf(TEXT("74105 вкладка %d подпись"), n), TabTexts.IsValidIndex(n) ? TabTexts[n].Get() : nullptr);
	}
	for (int32 i = 0; i < TopText.Num(); ++i) { Line(FString::Printf(TEXT("%d FISH_BOOK_TOPTEXT_%d"), 74101 + i, i + 1), TopText[i]); }
	for (int32 i = 0; i < GgText.Num(); ++i) { Line(FString::Printf(TEXT("%d FISH_BOOK_GG_T%d"), 74041 + i, i + 1), GgText[i]); }
	for (int32 i = 0; i < Digit.Num(); ++i) { Line(FString::Printf(TEXT("%d FISH_BOOK_GG_SS_%d"), 74045 + i, i + 1), Digit[i]); }
	for (int32 i = 0; i < InfoCap.Num(); ++i) { Line(FString::Printf(TEXT("%d подпись инфо"), 74061 + 2 * i), InfoCap[i]); }
	for (int32 i = 0; i < InfoVal.Num(); ++i) { Line(FString::Printf(TEXT("%d значение инфо"), 74062 + 2 * i), InfoVal[i]); }
	Line(TEXT("74070 описание"), InfoDesc.Node);
	Line(TEXT("74051 имя карточки"), CardName);
	Line(TEXT("74052 картинка карточки"), CardPic);
	Line(TEXT("74053 номер карточки"), CardNo);
	Line(TEXT("74054 счёт карточки"), CardCnt);
	Line(TEXT("74080 подпись наград"), RewardCap);
	for (int32 t = 0; t < RewardUi.Num(); ++t)
	{
		Line(FString::Printf(TEXT("%d награда %d иконка"), 74081 + t, t), RewardUi[t].Icon);
		Line(FString::Printf(TEXT("%d награда %d имя"), 74085 + t, t), RewardUi[t].Name);
		Line(FString::Printf(TEXT("%d награда %d рамка"), 74111 + t, t), RewardUi[t].Frame);
	}
	Line(TEXT("74090 FISH_BOOK_OKK"), BtnReward);
	Line(TEXT("74270 FISH_BOOK_DEL"), DelPanel);
	Line(TEXT("74271 FISH_BOOK_DEL_btn"), BtnDel);
	Line(TEXT("74020 шаблон ячейки"), CellTemplate);
	for (int32 i = 0; i < CellUi.Num(); ++i)
	{
		Line(FString::Printf(TEXT("ячейка %02d (99999900+%d)"), i, i), CellUi[i].Root);
		Line(FString::Printf(TEXT("ячейка %02d имя/картинка"), i), CellUi[i].Pic);
	}
	Line(TEXT("74200 FISH_BOOK_JOINBOX"), Join);
	Line(TEXT("74201 заголовок"), JoinTitle.Node);
	Line(TEXT("74202 картинка"), JoinPic);
	Line(TEXT("74203 собрано"), JoinDone);
	Line(TEXT("74204 счёт"), JoinK);
	Line(TEXT("74205 сдано"), JoinR1);
	Line(TEXT("74206 к сдаче"), JoinR2);
	Line(TEXT("74207 требуется"), JoinR3);
	Line(TEXT("74210 подпись"), JoinCaught);
	for (int32 k = 0; k < InvUi.Num(); ++k) { Line(FString::Printf(TEXT("%d стопка %d"), kFirstCellId + k, k), InvUi[k].Icon); }
	Line(TEXT("74241 курсор стопки"), JoinCheck);
	Line(TEXT("74251 подсказка"), JoinHint);
	Line(TEXT("74252 ОК"), BtnJoinOk);
	Line(TEXT("74253 Отмена"), BtnJoinNo);
	return S;
}

FString UAsdaFishBookSubsystem::DumpState() const
{
	FString S = FString::Printf(TEXT("=== книга рыбака: открыто=%d регистрация=%d привязано=%d набор=%s том[463]=%d ячейка[462]=%d вкладки=%d (сдвиг %d, выбрана %d) ")
		TEXT("награда[470]=%d книга[469]=%d ждём6177=%d стопка[465]=%d выбрана[466]=%d кол-во[467]=%d рыба[468]=%d ===\n"),
		bOpen ? 1 : 0, bJoinOpen ? 1 : 0, bControlsReady ? 1 : 0, TableChoice == 1 ? TEXT("клиент") : TEXT("сервер"), Page, Sel, TabCount,
		TabOfs, TabSel, RewardSel, BookCell, bAwaitRegist ? 1 : 0, PickedCell, ChosenCell, Qty, JoinFish);
	for (int32 i = 0; i < St.Num() && i < Tpl.Num(); ++i)
	{
		const FState& St0 = St[i];
		FString Cnt;
		for (int32 j = 0; j < kFish; ++j) { if (Tpl[i].Fish[j] != -1) { Cnt += FString::Printf(TEXT(" %d/%d"), St0.Cnt[j], Tpl[i].Req[j]); } }
		S += FString::Printf(TEXT("  том %d «%s» рег=%d id=%d тип=%d книга=%d награды=[%d %d %d %d]:%s\n"), i, *St0.Name, St0.bReg ? 1 : 0,
			St0.Id, St0.B8, Tpl[i].BookItemId, Tpl[i].Reward[0], Tpl[i].Reward[1], Tpl[i].Reward[2], Tpl[i].Reward[3], *Cnt);
	}
	return S;
}

// ═══════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ═══════════════════════════════════════════════
namespace
{
	// Подсистема книги мира W (nullptr — нет).
	UAsdaFishBookSubsystem* FishBookSub(UWorld* W)
	{
		return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaFishBookSubsystem>() : nullptr;
	}
	// Первый целый аргумент (Def — нет аргумента).
	int32 FishBookArg(const TArray<FString>& A, int32 Def) { return A.Num() > 0 ? FCString::Atoi(*A[0]) : Def; }
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GFishBookOpen(TEXT("asda.FishBook.Open"),
	TEXT("asda.FishBook.Open — открыть книгу рыбака 74000 (как пункт доп-меню cmd 9, `itemCollectUiOpen`)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W) { if (UAsdaFishBookSubsystem* S = FishBookSub(W)) { S->Open(); } }));
static FAutoConsoleCommandWithWorldAndArgs GFishBookClose(TEXT("asda.FishBook.Close"),
	TEXT("asda.FishBook.Close — крестик 74099 / ESC (`itemCollectUiShowOrToggle`)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W) { if (UAsdaFishBookSubsystem* S = FishBookSub(W)) { S->CloseOrToggle(); } }));
static FAutoConsoleCommandWithWorldAndArgs GFishBookDump(TEXT("asda.FishBook.Dump"),
	TEXT("asda.FishBook.Dump — дамп КАЖДОГО контрола окна 74000 (живая геометрия) и состояния томов"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaFishBookSubsystem* S = FishBookSub(W)) { UE_LOG(LogAsdaFishBook, Log, TEXT("\n%s\n%s"), *S->DumpControls(), *S->DumpState()); }
	}));
static FAutoConsoleCommandWithWorldAndArgs GFishBookCell(TEXT("asda.FishBook.Cell"),
	TEXT("asda.FishBook.Cell <0..29> [dbl] — ЛКМ (0xB) или двойной (0xD) по ячейке сетки"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (UAsdaFishBookSubsystem* S = FishBookSub(W)) { if (A.Contains(TEXT("dbl"))) { S->OnCellDoubleClick(FishBookArg(A, 0)); } else { S->OnCellClick(FishBookArg(A, 0)); } }
	}));
static FAutoConsoleCommandWithWorldAndArgs GFishBookTab(TEXT("asda.FishBook.Tab"),
	TEXT("asda.FishBook.Tab <0..3> — выбор вкладки тома (66817 по 74105)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { if (UAsdaFishBookSubsystem* S = FishBookSub(W)) { S->OnTabSelected(FishBookArg(A, 0)); } }));
static FAutoConsoleCommandWithWorldAndArgs GFishBookReward(TEXT("asda.FishBook.Reward"),
	TEXT("asda.FishBook.Reward <0..3> | btn — выбрать награду / нажать 74090 «Получение награды»"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (UAsdaFishBookSubsystem* S = FishBookSub(W)) { if (A.Contains(TEXT("btn"))) { S->OnRewardButton(); } else { S->OnRewardClick(FishBookArg(A, 0)); } }
	}));
static FAutoConsoleCommandWithWorldAndArgs GFishBookInv(TEXT("asda.FishBook.Inv"),
	TEXT("asda.FishBook.Inv <0..29> | ok | cancel — ЛКМ по стопке окна регистрации (нумпад 10) / 74252 / 74253"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (UAsdaFishBookSubsystem* S = FishBookSub(W))
		{
			if (A.Contains(TEXT("ok"))) { S->OnJoinOk(); } else if (A.Contains(TEXT("cancel"))) { S->CloseJoin(); } else { S->OnInvClick(FishBookArg(A, 0)); }
		}
	}));
static FAutoConsoleCommandWithWorldAndArgs GFishBookDelete(TEXT("asda.FishBook.Delete"),
	TEXT("asda.FishBook.Delete — нажать 74271 «Удалить книгу» (диалог 77 / нотис 111069)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W) { if (UAsdaFishBookSubsystem* S = FishBookSub(W)) { S->OnDeleteClicked(); } }));
static FAutoConsoleCommandWithWorldAndArgs GFishBookArrow(TEXT("asda.FishBook.Arrow"),
	TEXT("asda.FishBook.Arrow <-1|1> — стрелка ленты вкладок 74106/74107"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { if (UAsdaFishBookSubsystem* S = FishBookSub(W)) { S->OnArrow(FishBookArg(A, 1)); } }));
static FAutoConsoleCommandWithWorldAndArgs GFishBookUse(TEXT("asda.FishBook.UseBook"),
	TEXT("asda.FishBook.UseBook <ячейка 0..59> — использовать книгу-предмет (подтип 210) из контейнера 2 → диалог 79"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { if (UAsdaFishBookSubsystem* S = FishBookSub(W)) { S->OnUseBookItem(2, FishBookArg(A, 0)); } }));
#endif // ASDA_DEV_CONSOLE
