// Copyright (c) 2026 @sitxovski (Discord)
// Реализация МЕНЮ ЗВАНИЙ (окно `Title_Win` 18700 + значок `Title_Notice_BG` 50000).
//   Весь реверс, адреса, раскладки пакетов, таблица нотисов и дефекты — в шапке
//   `AsdaTitleSubsystem.h` и в отчёте `Reports/title_system.md`.
#include "UI/AsdaTitleSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaDataTableCache.h"
#include "Items/AsdaItemDbRows.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaHudSubsystem.h"       // ★ ShowTitleBanner — баннер «звание получено» (тип 2)
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaMarkup.h"          // UAsdaRichText::ApplyFormatted — единственный показ тегов <FC=..>
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaNameplateComponent.h"   // ★ D8: читаем ФАКТИЧЕСКИЕ поля плашки в проверке снятия
#include "World/AsdaWorldSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"   // FScreenshotRequest — именованный кадр вьюпорта ВМЕСТЕ со слоем Slate
#include "UI/AsdaCraftSubsystem.h"   // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "Items/AsdaItemTableSubsystem.h"   // самотест звёздочки: GetTitle
#include "World/AsdaCoord.h"                // самотест звёздочки: позиция соседа
#include "Text/AsdaLangRows.h"     // [ПОРТ] 09.10.2026 (мультиязычность): имена/описания званий на языке сессии
#include "Text/AsdaLang.h"         // ★ 10.10.2026: FAsdaLang::OnLanguageChanged — пересборка званий при смене языка на лету
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`
#include "AsdaTitleSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaTitleSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaTitle);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

namespace {
	const TCHAR* kWindowWbp =
		TEXT("/Game/AsdaAssets/UI/Player_UI/Character/WBP_Title_Win.WBP_Title_Win_C");

	const TCHAR* kDtTitleList   = TEXT("/Game/AsdaAssets/Data/ItemDB/DT_title_list.DT_title_list");
	const TCHAR* kDtTitleOption = TEXT("/Game/AsdaAssets/Data/ItemDB/DT_title_option.DT_title_option");

	// ★ D3 — РАСШИРЕНИЕ МАСКИ СЛАВЫ ДО 512 БИТ (2026-08-29).
	//   Что было в оригинале: маска ровно 256 бит с обеих сторон, звания id 256…279 (их 24)
	//   получить было НЕЛЬЗЯ — бита для них не существовало, а `charTestStateFlagA@0xE9C240`
	//   для таких id читал за границей маски (соседнюю маску «найдено»).
	ASDA_DEV_CVAR(int32, CVarMask512,
		TEXT("asda.Title.Mask512"), 1,
		TEXT("D3. 1 (по умолчанию) = маска славы 512 бит: расширенные тела 6066 (74 б) и 6086\n")
		TEXT("(70 б) разбираются, звания с id 256…511 доступны.\n")
		TEXT("0 = как в оригинале: 256 бит, хвост расширенного пакета игнорируется, бит вне\n")
		TEXT("0…255 всегда «не получено» (в лог пишется предупреждение)."),
		ECVF_Default);
}

// ★ СВЕДЕНО (`fix_helpers_dedup.md`): локальный `Thousands` был 3-й копией `String_FormatThousands`.
//   Общая точка — `AsdaUi::FormatThousands` (тот же вывод: запятая каждые 3 разряда, минус вперёд).
// ═════════════════════════ ФОРМАТИРОВАННЫЙ ТЕКСТ И ГЕОМЕТРИЯ СЛОТОВ ═══════════════════════════
// Размер canvas-слота узла — ИЗ АССЕТА (а не из живой геометрии прошлого кадра).
FVector2D UAsdaTitleSubsystem::SlotSize(const UWidget* W)
{
	if (const UCanvasPanelSlot* Cs = AsdaUi::SlotOf(W))
	{
		const FMargin O = Cs->GetOffsets();
		return FVector2D(O.Right, O.Bottom);   // импортёр кладёт size в Right/Bottom (auto-size выключен)
	}
	return FVector2D::ZeroVector;
}

// Смещение узла в координатах КОРНЕВОЙ канвы: импортёр расплющил дерево во вложенные канвасы
//   (закон `jui-flattened-hierarchy-geometry`), поэтому складываем слоты вверх по родителям.
FVector2D UAsdaTitleSubsystem::OffsetInRoot(const UWidget* W)
{
	FVector2D Acc = FVector2D::ZeroVector;
	for (const UWidget* Cur = W; Cur; Cur = Cur->GetParent())
	{
		if (const UCanvasPanelSlot* Cs = AsdaUi::SlotOf(Cur))
		{
			const FMargin O = Cs->GetOffsets();
			Acc += FVector2D(O.Left, O.Top);
		}
	}
	return Acc;
}

// Снять стиль ИЗ АССЕТА, пока узел ещё `UTextBlock` (после первой подмены его не прочитать).
void UAsdaTitleSubsystem::CaptureRich(FAsdaTitleRichSlot& S, UWidget* Node)
{
	S.Node = Node;
	S.bCaptured = false;
	if (UTextBlock* Tb = Cast<UTextBlock>(Node))
	{
		S.Style = FTextBlockStyle()
			.SetFont(Tb->GetFont())
			.SetColorAndOpacity(Tb->GetColorAndOpacity())
			.SetShadowOffset(Tb->GetShadowOffset())
			.SetShadowColorAndOpacity(Tb->GetShadowColorAndOpacity());
		S.Justify = AsdaUi::ReadJustification(Tb);
		// ★ ЗАКОН `ui-autowrap-canvas-gotcha`: автоперенос в канвасе берёт ширину из кэша прошлого
		//   кадра и даёт дёрганье. Переносим только там, где авторский слот РАССЧИТАН на несколько
		//   строк (высота >= 28 = две строки по 14), и ширину фиксируем по слоту ИЗ АССЕТА.
		const FVector2D Sz = SlotSize(Node);
		S.bWrap  = (Sz.Y >= 28.0);
		S.WrapAt = S.bWrap ? (float)Sz.X : 0.f;
		S.bCaptured = true;
	}
}

// Показать СЫРОЙ текст с тегами. Узел после первого показа становится `URichTextBlock` — кэш
//   переприсваиваем (иначе осиротеет и текст перестанет обновляться).
void UAsdaTitleSubsystem::SetRich(FAsdaTitleRichSlot& S, const FString& Raw)
{
	if (!S.Node) { return; }
	FAsdaRichApplyOpts Opts;
	if (S.bCaptured)
	{
		Opts.BaseStyle   = &S.Style;
		Opts.bSetJustify = true;
		Opts.Justify     = S.Justify;
		Opts.bWrap       = S.bWrap;
		Opts.WrapAt      = S.WrapAt;
	}
	if (UWidget* New = UAsdaRichText::ApplyFormatted(S.Node, Raw, S.bCaptured ? &Opts : nullptr))
	{
		S.Node = New;
	}
}

FString UAsdaTitleSubsystem::NodeText(const UWidget* W)
{
	if (const UTextBlock* Tb = Cast<UTextBlock>(W)) { return Tb->GetText().ToString(); }
	if (const URichTextBlock* Rb = Cast<URichTextBlock>(W)) { return Rb->GetText().ToString(); }
	return FString(TEXT("-"));
}

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════
void UAsdaTitleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★★★ РОСПИСЬ «КАК ЗАКРЫТЬ СЕБЯ ПО ESC» — порт события 21 (`CControl_OnEvent21@0x12DCC30`).
	//   Окно `Title_Win` (IDNum 18700) стоит в эталонной таблице `kEscCloseWindows`: признак
	//   `CControl+0x228` ему ставит `CUITitle_InitControls@0xE96430`. Проход
	//   `UAsdaWindowSubsystem::CloseTopmostOnEsc` закрывает верхнее окно ИМЕННО ЭТИМ вызовом,
	//   а не снятием виджета: снятие оставило бы подсистеме состояние «открыто», и окно
	//   больше не открылось бы. Закрыватель идемпотентен — повторный вызов ничего не делает.
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	if (UAsdaWindowSubsystem* EscWM = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		EscWM->RegisterEscCloser(kWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaTitleSubsystem::CloseTitleWindow));
		// ★ Волна В5 ремесла (ревизия В4 С-2): `uiCloseAllWindows` (ui_core.c:54376) безусловно зовёт `sub_924419` =
		//   `set_o370` — гасит значок 50000; вернуть его может восстановление снимка (слот 8).
		EscWM->RegisterWindowCloser(kNoticeId, FSimpleDelegate::CreateUObject(this, &UAsdaTitleSubsystem::HideNoticeIconForCloseAll));
	}

	FMemory::Memzero(MaskOwned, sizeof(MaskOwned));
	FMemory::Memzero(MaskFound, sizeof(MaskFound));

	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь.
		HGranted   = Net->OnTitleGranted  .AddLambda([this](const TArray<uint8>& B) { QGranted.Add(B); });
		HMaskOwned = Net->OnTitleMaskOwned.AddLambda([this](const TArray<uint8>& B) { QMaskOwned.Add(B); });
		HDeleted   = Net->OnTitleDeleted  .AddLambda([this](const TArray<uint8>& B) { QDeleted.Add(B); });
		HFound     = Net->OnTitleFound    .AddLambda([this](const TArray<uint8>& B) { QFound.Add(B); });
		HMaskFound = Net->OnTitleMaskFound.AddLambda([this](const TArray<uint8>& B) { QMaskFound.Add(B); });
		HStatBatch = Net->OnTitleStatBatch.AddLambda([this](const TArray<uint8>& B) { QStatBatch.Add(B); });
		HSlots     = Net->OnCharTitle.AddLambda(
			[this](int32 U, int32 T0, int32 T1, int32 R) { QSlots.Add({ U, T0, T1, R }); });
	}

	BuildDefs();
	// ★ 10.10.2026 (язык на лету, до входа в мир): названия/описания званий — заново на новом языке.
	FAsdaLang::OnLanguageChanged().AddWeakLambda(this, [this]() { BuildDefs(); });

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("asda.Title.Open"),
		TEXT("Открыть/закрыть окно званий 18700 (тот же путь, что пункт «Меню званий»)."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			UE_LOG(LogAsdaTitle, Log, TEXT("[звания] asda.Title.Open → %s"),
				OpenTitleWindow() ? TEXT("окно открыто/закрыто") : TEXT("ОТКАЗ (гейт или нет окна)"));
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("asda.Title.State"),
		TEXT("Состояние системы званий: маски, очки, ранг, слоты, вкладка, сортировка, кулдаун."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{ UE_LOG(LogAsdaTitle, Log, TEXT("%s"), *DumpState()); }), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("asda.Title.Dump"),
		TEXT("Дамп ВСЕХ разобранных контролов окна званий."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{ UE_LOG(LogAsdaTitle, Log, TEXT("%s"), *DumpControls()); }), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("asda.Title.List"),
		TEXT("Что РЕАЛЬНО стоит в 12 строках списка (читается с виджетов, а не из своих переменных)."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{ UE_LOG(LogAsdaTitle, Log, TEXT("%s"), *DumpList()); }), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

	// ★ Диагностика ОБЯЗАНА жать саму кнопку и читать состояние виджета (закон проекта):
	//   команда дёргает те же ветки, что и мышь, и печатает список ПОСЛЕ действия.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("asda.Title.Click"),
		TEXT("asda.Title.Click <tab N|row N|sort IDNUM|apply|delete|ok|cancel|release|notice> — нажать ")
		TEXT("элемент окна. `notice` = клик по ЗНАЧКУ HUD 50000 (жмёт ту же ветку, что и мышь)."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (Args.Num() == 0) { UE_LOG(LogAsdaTitle, Warning, TEXT("[звания] нужен аргумент")); return; }
			const FString A = Args[0].ToLower();
			if      (A == TEXT("tab")    && Args.Num() > 1) { SelectTab(FCString::Atoi(*Args[1])); }
			else if (A == TEXT("row")    && Args.Num() > 1) { SelectRow(FCString::Atoi(*Args[1])); }
			else if (A == TEXT("sort")   && Args.Num() > 1) { SortByColumn(FCString::Atoi(*Args[1])); }
			else if (A == TEXT("apply"))   { OnApplyClicked(); }
			else if (A == TEXT("delete"))  { OnDeleteClicked(); }
			else if (A == TEXT("ok"))      { OnOkClicked(); }
			else if (A == TEXT("cancel"))  { OnCancelClicked(); }
			else if (A == TEXT("release")) { OnReleaseClicked(); }
			// ★ Значок HUD: ТА ЖЕ точка, что и у мыши (`TickNoticeIcon` → `OnNoticeIconClicked`).
			else if (A == TEXT("notice"))  { OnNoticeIconClicked(); }
			else { UE_LOG(LogAsdaTitle, Warning, TEXT("[звания] неизвестный аргумент «%s»"), *A); return; }
			UE_LOG(LogAsdaTitle, Log, TEXT("%s"), *DumpList());
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

	// ★★★ САМОПРОВЕРКА ВКЛАДОК ПУТЁМ ИГРОКА (2026-08-30). Тот же приём, что у аукциона
	//   (`asda.Exchange.SelfTest`): окно открывается по таймеру уже в отрисованном мире, вкладки
	//   переключаются ТЕМ ЖЕ методом, что и мышь (`SelectTab` — на нём висит `Bind` кнопок), и
	//   после каждого переключения снимается ЭКРАННАЯ геометрия. Без этого «активная вкладка
	//   светлая» пришлось бы принимать на веру: слот из ассета и PIE-дамп уже врали.
	//   Запуск: UnrealEditor-Cmd.exe <uproject> /Game/Map/Login -game -ExecCmds="asda.Title.SelfTest quit"
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("asda.Title.SelfTest"),
		TEXT("Звания (18700): открыть окно и обойти вкладки 0→1→2, сняв ЭКРАННУЮ геометрию (для -game). ")
		TEXT("Аргумент `quit` — выйти после дампа; `shot` — снять КАДР на каждом шаге."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			bool bQuit = false;
			// ★★★ КАДР — ЕДИНСТВЕННОЕ ЧЕСТНОЕ ДОКАЗАТЕЛЬСТВО ВИДА (2026-08-30). Числа
			//   `GetCachedGeometry` доказывают, ЧТО МЫ ПОСТАВИЛИ, но не то, что вышло на экран:
			//   на вкладках 18713 они трижды рапортовали «всё верно», пока владелец видел
			//   плоские вкладки. `shot showui` = `FScreenshotRequest::RequestScreenshot(bShowUI=true)`
			//   — единственная команда, пишущая в PNG слой Slate (HighResShot интерфейс НЕ пишет).
			bool bShot = false;
			for (const FString& A : Args)
			{
				if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
				if (A.Equals(TEXT("shot"), ESearchCase::IgnoreCase)) { bShot = true; }
			}
			UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
			if (!W) { UE_LOG(LogAsdaTitle, Warning, TEXT("[звания] самотест: мира нет")); return; }

			// Шаг «открыть»: печатаем РЕШЕНИЕ, а не молчим — «окно не открылось» и «открылось,
			//   но пустое» это разные исходы, и различать их надо числами.
			FTimerHandle H0;
			W->GetTimerManager().SetTimer(H0, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				const bool bOk = OpenTitleWindow();
				UE_LOG(LogAsdaTitle, Log, TEXT("САМОТЕСТ: открытие окна → %s, контролы=%d"),
					bOk ? TEXT("ОК") : TEXT("ОТКАЗ"), bControlsReady ? 1 : 0);
				DumpTabsOnScreen(TEXT("после открытия"));
			}), 2.0f, false);

			// Шаги «нажать вкладку N» — ровно тот путь, что и мышью. Кадр снимается ОТДЕЛЬНЫМ
			//   таймером через полсекунды после клика: `shot showui` пишет файл в конце КАДРА,
			//   и без паузы снимок успевает застать состояние ДО перерисовки.
			for (int32 I = 0; I < 3; ++I)
			{
				FTimerHandle H;
				W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, I]()
				{
					SelectTab(I);
					DumpTabsOnScreen(*FString::Printf(TEXT("после клика по вкладке %d"), I));
				}), 3.0f + 2.0f * (float)I, false);

				if (bShot)
				{
					FTimerHandle HS;
					W->GetTimerManager().SetTimer(HS, FTimerDelegate::CreateWeakLambda(this, [this, I]()
					{
						const FString Name = FString::Printf(TEXT("asda_title_tab%d"), I);
						UE_LOG(LogAsdaTitle, Log, TEXT("САМОТЕСТ: КАДР вкладки %d → %s.png"), I, *Name);
						FScreenshotRequest::RequestScreenshot(Name, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
					}), 4.0f + 2.0f * (float)I, false);
					// ★ ИМЯ КАДРА ЗАДАЁМ САМИ. Безымянный `shot showui` пишет `ScreenShotNNNNN.png`
					//   в общий `Saved/Screenshots/WindowsEditor`, и параллельный прогон другого
					//   агента (редактор пишет туда же) перетирает наши кадры — 2026-08-30 именно
					//   так вместо вкладок пришли снимки чужого редактора. Именованный запрос
					//   `FScreenshotRequest::RequestScreenshot(имя, bShowUI=true)` даёт файл
					//   `asda_title_tab<N>.png`, который спутать не с чем.
				}
			}

			if (bQuit)
			{
				FTimerHandle HQ;
				W->GetTimerManager().SetTimer(HQ, FTimerDelegate::CreateWeakLambda(this, [this]()
				{
					UE_LOG(LogAsdaTitle, Log, TEXT("САМОТЕСТ: завершён"));
					if (GEngine) { GEngine->DeferredCommands.Add(TEXT("quit")); }
				}), 11.0f, false);
			}
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

	// ★★★ САМОТЕСТ ЗНАЧКА HUD 50000 — ПУТЁМ ИГРОКА (2026-08-30, замечание владельца «иконка
	//   ничего не делает»). Устроен как `asda.Title.SelfTest`: шаги разнесены таймерами, потому что
	//   `-ExecCmds` выполняет ВСЕ команды на нулевом кадре, а живая геометрия готова только после
	//   отрисовки — иначе прямоугольник значка будет 0×0 и проверка «подтвердит» пустыми числами
	//   ([[ui-check-game-path-geometry]]).
	//   ★ ПОЧЕМУ НЕ PIE: значок сидит на высоте Y=381 базовых 768, а PIE-вьюпорт редактора в
	//     headless — 700×201; узел ниже нижней кромки НЕ ПЕРЕРИСОВЫВАЕТСЯ, и `GetCachedGeometry`
	//     честно отдаёт нули. В `-game` вьюпорт настоящий (`-ResX/-ResY`) и значок попадает в кадр.
	//   Сценарий (каждый шаг оставляет строку-доказательство):
	//     1) 6085 → значок зажёгся, его ЖИВОЙ прямоугольник ненулевой и видимость `Visible`;
	//     2) клик по значку ТОЙ ЖЕ веткой, что и мышь → окно открылось, значок погас;
	//     3) маркер «новое» у звания ОСТАЛСЯ (клик по значку строки не гасит — так в эталоне);
	//     4) только маска 6066 (без опкода) → значок НЕ зажигается и эффект НЕ спавнится;
	//     5) только маска 6086 (без опкода) → то же.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("asda.Title.NoticeTest"),
		TEXT("Звания: самотест ЗНАЧКА HUD 50000 путём игрока — зажечь (6085), снять живую геометрию, ")
		TEXT("нажать, проверить гашение и маркер, затем прогнать «только маска» (6066/6086) и убедиться, ")
		TEXT("что значок и эффект НЕ появляются. Аргумент `quit` — выйти после прогона."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельные 6085/6066/6086 портят маску званий живой сессии.
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(this, TEXT("asda.Title.NoticeTest"), KitArgs)) { return; }
			bool bQuit = false;
			for (const FString& A : Args)
			{
				if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
			}
			UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
			if (!W) { UE_LOG(LogAsdaTitle, Warning, TEXT("[звания] самотест значка: мира нет")); return; }

			// Живой прямоугольник и видимость фона значка — ФАКТ ПОСЛЕ ОТРИСОВКИ, а не намерение.
			auto DumpIcon = [this](const TCHAR* When)
			{
				if (!NoticeBg)
				{
					UE_LOG(LogAsdaTitle, Warning, TEXT("ЗНАЧОК (%s): виджета фона НЕТ"), When);
					return;
				}
				const FGeometry& G = NoticeBg->GetCachedGeometry();
				const FVector2D P = G.GetAbsolutePosition();
				const FVector2D S = G.GetLocalSize() * G.Scale;
				UE_LOG(LogAsdaTitle, Log,
					TEXT("ЗНАЧОК (%s): активен=%d вид_иконки=%d подсказка=%d | фон: видимость=%d ")
					TEXT("(ловит мышь: %s) рект=(%.0f,%.0f) %.0fx%.0f"),
					When, bNoticeActive ? 1 : 0, NoticeIconKind, NoticeTipKind,
					(int32)NoticeBg->GetVisibility(),
					NoticeBg->GetVisibility() == ESlateVisibility::Visible ? TEXT("ДА") : TEXT("нет"),
					P.X, P.Y, S.X, S.Y);
			};
			// Тело 6085 собирается ровно так же, как в `asda.Title.Sim secret` — своей ветки нет.
			auto Sim6085 = [this](int32 Id)
			{
				const UAsdaNetSubsystem* N = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
				TArray<uint8> B; B.SetNumZeroed(8);
				AsdaBytes::WrI32(&B[0], N ? N->SelectedCharId : 0);
				AsdaBytes::WrI16(&B[4], N ? (int32)N->SelfUserNum : 0);
				AsdaBytes::WrI16(&B[6], Id);
				HandleFound(B);
			};

			const int32 kProbeId = 65;   // «Впереди планеты всей» — бит есть у ADM_sitxovski
			// ★ СРЕДН-1 (ревизия shop_offline_fix, 10.10.2026): офлайн повторно — на отложенном старте — общий флаг
			//   шагов 1–5 (проверку делает первый сработавший таймер, при отказе пропускаются все, кроме выхода).
			TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(TEXT("asda.Title.NoticeTest"), bQuit);

			// 1. Зажечь значок пакетом 6085 и дать кадру отрисоваться.
			FTimerHandle H1;
			W->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateWeakLambda(this, [this, Sim6085, kProbeId, Gate]()
			{
				if (!Gate->Pass(this)) { return; }
				UE_LOG(LogAsdaTitle, Log, TEXT("САМОТЕСТ ЗНАЧКА: шаг 1 — пакет 6085 «найдено» id=%d"), kProbeId);
				Sim6085(kProbeId);
			}), 2.0f, false);

			FTimerHandle H2;
			W->GetTimerManager().SetTimer(H2, FTimerDelegate::CreateWeakLambda(this, [this, DumpIcon, Gate]()
			{
				if (!Gate->Pass(this)) { return; }
				DumpIcon(TEXT("после 6085, до клика"));
			}), 3.5f, false);

			// 2. Клик — ТОЙ ЖЕ веткой, что и мышь.
			FTimerHandle H3;
			W->GetTimerManager().SetTimer(H3, FTimerDelegate::CreateWeakLambda(this, [this, DumpIcon, kProbeId, Gate]()
			{
				if (!Gate->Pass(this)) { return; }
				UE_LOG(LogAsdaTitle, Log, TEXT("САМОТЕСТ ЗНАЧКА: шаг 2 — КЛИК по значку"));
				OnNoticeIconClicked();
				DumpIcon(TEXT("после клика"));
				UE_LOG(LogAsdaTitle, Log,
					TEXT("САМОТЕСТ ЗНАЧКА: шаг 3 — окно открыто=%d, маркер «новое» у id=%d %s ")
					TEXT("(эталон гасит его только кликом ПО СТРОКЕ)"),
					bOpen ? 1 : 0, kProbeId,
					FlagNew.Contains(kProbeId) ? TEXT("НА МЕСТЕ") : TEXT("ПОТЕРЯН ← дефект"));
			}), 4.5f, false);

			FTimerHandle H4;
			W->GetTimerManager().SetTimer(H4, FTimerDelegate::CreateWeakLambda(this, [this, Gate]()
			{
				if (!Gate->Pass(this)) { return; }
				FScreenshotRequest::RequestScreenshot(TEXT("asda_title_notice_clicked"),
					/*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
				UE_LOG(LogAsdaTitle, Log, TEXT("САМОТЕСТ ЗНАЧКА: КАДР → asda_title_notice_clicked.png"));
			}), 5.5f, false);

			// 4-5. «Только маска»: ни значка, ни эффекта быть не должно.
			FTimerHandle H5;
			W->GetTimerManager().SetTimer(H5, FTimerDelegate::CreateWeakLambda(this, [this, DumpIcon, Gate]()
			{
				if (!Gate->Pass(this)) { return; }
				if (bOpen) { CloseTitleWindow(); }
				// ★ МАСКА ШЛЁТСЯ ДВАЖДЫ, И ЭТО ВАЖНО. Первая маска сессии в дифере не считается
				//   приобретениями (`bHadMask == false`) — на ней ветка `0→1` просто не исполняется,
				//   и проверка «диффер не празднует» была бы ПУСТОЙ. Вторая маска с ДРУГИМ битом
				//   даёт настоящий переход `0→1` при `bHadMask == true` — то есть ровно ту ветку,
				//   из-за которой и возник вопрос владельца.
				UE_LOG(LogAsdaTitle, Log,
					TEXT("САМОТЕСТ ЗНАЧКА: шаг 4 — ТОЛЬКО маска 6066 (без опкода 6065), ДВА раза: ")
					TEXT("первая заводит базу, вторая даёт настоящее приобретение 0→1"));
				HandleMaskOwned(BuildMaskBodyForTest(/*получено*/ true, /*бит*/ 70));
				MaskSet(MaskOwned, 70, true);   // база: бит 70 теперь «был», как после первой маски
				HandleMaskOwned(BuildMaskBodyForTest(/*получено*/ true, /*бит*/ 71));
				DumpIcon(TEXT("после ДВУХ масок 6066 (приобретение 0→1 без опкода)"));
			}), 7.0f, false);

			FTimerHandle H6;
			W->GetTimerManager().SetTimer(H6, FTimerDelegate::CreateWeakLambda(this, [this, DumpIcon, Gate]()
			{
				if (!Gate->Pass(this)) { return; }
				UE_LOG(LogAsdaTitle, Log,
					TEXT("САМОТЕСТ ЗНАЧКА: шаг 5 — ТОЛЬКО маска 6086 (без опкода 6085)"));
				HandleMaskFound(BuildMaskBodyForTest(/*найдено*/ false, /*бит*/ 71));
				DumpIcon(TEXT("после ОДНОЙ ТОЛЬКО маски 6086"));
			}), 8.5f, false);

			if (bQuit)
			{
				FTimerHandle HQ;
				W->GetTimerManager().SetTimer(HQ, FTimerDelegate::CreateWeakLambda(this, [this]()
				{
					UE_LOG(LogAsdaTitle, Log, TEXT("САМОТЕСТ ЗНАЧКА: завершён"));
					if (GEngine) { GEngine->DeferredCommands.Add(TEXT("quit")); }
				}), 10.0f, false);
			}
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

	// ★ ДИАГНОСТИКА БЕЗ СЕРВЕРА. Команда собирает НАСТОЯЩЕЕ тело пакета и гонит его через тот же
	//   разбор, что и сеть, — то есть проверяет раскладку, а не «свои переменные». В игровой путь
	//   она НИЧЕГО не добавляет: сервер эти же байты присылает сам.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("asda.Title.Sim"),
		TEXT("asda.Title.Sim <owned N [256|512]|found N [256|512]|slots P S R|grant ID|lost ID|del ID|")
		TEXT("secret ID|stats ID V|revoke ID> — собрать тело соответствующего пакета ")
		TEXT("(6066/6086/6068/6065/6070/6085/2255) и прогнать его через штатный разбор. ")
		TEXT("У owned/found третий аргумент задаёт ФОРМАТ тела: 256 = как шлёт необновлённый сервер ")
		TEXT("(42/38 б), 512 = как шлёт обновлённый (74/70 б). Только для проверки без сервера."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельные 6065/6066/6068/6070/6085/6086/2255 — «Применить» отправил бы 6067 по поддельному владению.
			if (FAsdaOutgoingGate::RefuseDebugWhenOnline(this, TEXT("asda.Title.Sim"))) { return; }
			const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			const int32 CharId = Net ? Net->SelectedCharId : 0;
			const int32 Uidx   = Net ? (int32)Net->SelfUserNum : 0;
			if (Args.Num() < 2) { UE_LOG(LogAsdaTitle, Warning, TEXT("[звания] мало аргументов")); return; }
			const FString A = Args[0].ToLower();
			const int32 P1 = FCString::Atoi(*Args[1]);
			TArray<uint8> B;
			if (A == TEXT("owned") || A == TEXT("found"))
			{
				// ★ Третий аргумент — В КАКОМ ФОРМАТЕ собрать тело: «256» = как шлёт НЕ обновлённый
				//   сервер (42/38 б), «512» = как шлёт обновлённый (74/70 б). По умолчанию — тот,
				//   что действует сейчас (`asda.Title.Mask512`). Так одной командой проверяются ОБА
				//   формата приёмника, а не только новый.
				const bool bOwned = (A == TEXT("owned"));
				bool bExtBody = (EffectiveMaskBits() > kMaskBitsLegacy);
				if (Args.Num() >= 3)
				{
					if      (Args[2] == TEXT("256")) { bExtBody = false; }
					else if (Args[2] == TEXT("512")) { bExtBody = true;  }
				}
				const int32 Len = bOwned ? (bExtBody ? kBody6066Ext : kBody6066Legacy)
				                         : (bExtBody ? kBody6086Ext : kBody6086Legacy);
				B.SetNumZeroed(Len);
				if (bOwned) { AsdaBytes::WrI16(&B[0], Uidx); AsdaBytes::WrI32(&B[2], CharId); AsdaBytes::WrI32(&B[6], 12345); }
				else        { AsdaBytes::WrI32(&B[0], CharId); AsdaBytes::WrI16(&B[4], Uidx); }
				// Старая часть с Off, хвост — сразу за ней, поэтому байт бита i один и тот же
				// выражением `Off + (i >> 3)`: для i = 256 это ровно Off + 32, начало хвоста.
				const int32 Off = bOwned ? 10 : 6;
				const int32 Cap = bExtBody ? kMaskBits : kMaskBitsLegacy;
				for (int32 i = 0; i < P1 && i < Cap; ++i)
				{
					B[Off + (i >> 3)] |= (uint8)(1u << (i & 7));
				}
				if (bOwned) { HandleMaskOwned(B); } else { HandleMaskFound(B); }
			}
			else if (A == TEXT("slots") && Args.Num() >= 4)
			{
				HandleSlots(Uidx, P1, FCString::Atoi(*Args[2]), FCString::Atoi(*Args[3]));
			}
			else if (A == TEXT("grant") || A == TEXT("lost"))
			{
				B.SetNumZeroed(49);
				AsdaBytes::WrI16(&B[0], Uidx); AsdaBytes::WrI32(&B[2], CharId);
				AsdaBytes::WrI16(&B[6], P1); B[8] = (A == TEXT("lost")) ? 1 : 0;
				for (int32 s = 0; s < kOptionSlots; ++s)
				{
					AsdaBytes::WrI16(&B[9 + 6 * s + 0], s);          // «тройка занята» (не −1)
					AsdaBytes::WrI16(&B[9 + 6 * s + 2], 10 + s);     // значение бонуса
				}
				AsdaBytes::WrI32(&B[45], 4242);
				HandleGranted(B);
			}
			else if (A == TEXT("del"))
			{
				B.SetNumZeroed(13);
				AsdaBytes::WrI16(&B[0], Uidx); AsdaBytes::WrI32(&B[2], CharId);
				B[6] = 1; AsdaBytes::WrI16(&B[7], P1); AsdaBytes::WrI32(&B[9], 4200);
				HandleDeleted(B);
			}
			else if (A == TEXT("secret"))
			{
				B.SetNumZeroed(8);
				AsdaBytes::WrI32(&B[0], CharId); AsdaBytes::WrI16(&B[4], Uidx); AsdaBytes::WrI16(&B[6], P1);
				HandleFound(B);
			}
			else if (A == TEXT("stats") && Args.Num() >= 3)
			{
				const int32 V = FCString::Atoi(*Args[2]);
				B.SetNumZeroed(6 + 46);
				AsdaBytes::WrI16(&B[0], Uidx); AsdaBytes::WrI32(&B[2], CharId);
				AsdaBytes::WrI16(&B[6], P1);
				for (int32 s = 0; s < kOptionSlots; ++s)
				{
					AsdaBytes::WrI16(&B[6 + 10 + 6 * s + 0], s);
					AsdaBytes::WrI16(&B[6 + 10 + 6 * s + 2], V + s);
				}
				HandleStatBatch(B);
			}
			else if (A == TEXT("revoke"))
			{
				// ★★★ D8. СНЯТИЕ ЗВАНИЯ СЕРВЕРОМ: маска 6066 = ТЕКУЩАЯ минус один бит.
				//   Именно так выглядит пакет от серверного патча снятия рейтингового звания.
				MaskSet(MaskOwned, P1, false);                 // готовим ожидаемую маску…
				B = BuildMaskBody(/*получено*/ true, EffectiveMaskBits() > kMaskBitsLegacy);
				MaskSet(MaskOwned, P1, true);                  // …и возвращаем бит, чтобы диффер увидел 1→0
				HandleMaskOwned(B);
			}
			else { UE_LOG(LogAsdaTitle, Warning, TEXT("[звания] неизвестная форма «%s»"), *A); return; }
			UE_LOG(LogAsdaTitle, Log, TEXT("%s"), *DumpState());
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

	// ★★★ D8. ПРОВЕРКА «УЛЬТИМАТИВНОГО СНЯТИЯ» ОДНОЙ СТРОКОЙ — ПУТЁМ ИГРОКА.
	//   Сценарий владельца: игрок стоит в мире, ОКНО ЗВАНИЙ ЗАКРЫТО, звание надето — сервер его
	//   отбирает. Всё идёт через ШТАТНЫЕ обработчики (`HandleSlots`/`HandleMaskOwned`/
	//   `HandleMaskFound`/`HandleDeleted`), своих «тестовых» путей не заводим (закон
	//   `diagnostic-must-use-real-path`). Печатается состояние ДО/ПОСЛЕ, включая ФАКТИЧЕСКИЕ поля
	//   плашки над головой, — проверяется результат, а не намерение.
	//
	//   ★ ТРИ ВАРИАНТА, и все обязаны дать ОДИН И ТОТ ЖЕ итог (контракт P24 + устойчивость к сети):
	//     «полный»   — 6068 → 6066 → 6086 → 6070(код 1), ровно порядок серверного патча P24;
	//     «обратный» — 6070 приходит РАНЬШЕ 6066 (сеть порядок не гарантирует);
	//     «худший»   — сервер прислал ТОЛЬКО маску 6066, ни 6070, ни 6068.
	//   Нотис об утрате в каждом варианте обязан уйти РОВНО ОДИН раз — это и есть доказательство
	//   идемпотентности `ApplyTitleLoss` (счётчик `LossNoticeCount`).
	//   Запуск: `-game -ExecCmds="asda.Title.RevokeTest 65"`.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("asda.Title.RevokeTest"),
		TEXT("asda.Title.RevokeTest <id> — прогнать снятие звания при ЗАКРЫТОМ окне в ТРЁХ ")
		TEXT("вариантах контракта: полный (6068→6066→6086→6070), обратный (6070 раньше 6066) и ")
		TEXT("худший (только 6066). Итог всех трёх обязан совпасть, нотис — ровно один на вариант."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельные слоты/маски/удаление званий, фальшивый «надетый» слот.
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(this, TEXT("asda.Title.RevokeTest"), KitArgs)) { return; }
			if (Args.Num() < 1) { AsdaSelfTestKit::Refused(this, TEXT("asda.Title.RevokeTest"), KitArgs.bQuit, TEXT("нужен id")); return; }
			const int32 Id = FCString::Atoi(*Args[0]);
			if (Id < 0 || Id >= EffectiveMaskBits())
			{
				UE_LOG(LogAsdaTitle, Warning, TEXT("[звания] RevokeTest: id=%d вне маски (0…%d)"),
					Id, EffectiveMaskBits() - 1);
				AsdaSelfTestKit::Refused(this, TEXT("asda.Title.RevokeTest"), KitArgs.bQuit, TEXT("id вне маски"));
				return;
			}
			const FAsdaTitleDef* D = FindDef(Id);
			// ★ 18.10.2026: офлайн свой charId/userNum неизвестны, и штатные приёмники (`IsSelfCharId`, гейт 11.10 N2) молча
			//   отбрасывали ВСЕ пакеты теста — три варианта падали, а «успех» был пустым. Подставляем «своего» на время
			//   прогона (как самотесты рыбалки/PvP) и возвращаем по выходу (`FAsdaNetSelfScope`).
			UAsdaNetSubsystem* NetW = AsdaSelfTestKit::NetOf(this);
			FAsdaNetSelfScope SelfScope(NetW, TEXT("asda.Title.RevokeTest"));
			if (NetW && !NetW->bSelfUserNumKnown) { NetW->SelfUserNum = 77; NetW->bSelfUserNumKnown = true; }
			if (NetW && NetW->SelectedCharId == 0) { NetW->SelectedCharId = 0x00ABC279; }
			const UAsdaNetSubsystem* Net = NetW;
			const int32 Uidx = Net ? (int32)Net->SelfUserNum : 0;
			const bool  bExt = (EffectiveMaskBits() > kMaskBitsLegacy);
			const bool  bSuffix = (D && D->Position == 1);

			// ── Окно должно быть ЗАКРЫТО — в этом весь смысл проверки. ──
			if (bOpen) { CloseTitleWindow(); }
			UE_LOG(LogAsdaTitle, Log, TEXT("[звания] RevokeTest id=%d «%s»: окно %s"),
				Id, D ? *D->Name : TEXT("<нет в таблице>"), bOpen ? TEXT("ОТКРЫТО (!)") : TEXT("закрыто"));

			auto PlateStr = [this, Uidx]() -> FString
			{
				UAsdaWorldSubsystem* W = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
				AActor* A = W ? W->GetPlayerActorByUserNum(Uidx) : nullptr;
				const UAsdaNameplateComponent* NP = A ? A->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
				return NP ? FString::Printf(TEXT("плашка: title0=%d title1=%d ранг=%d"),
				                            NP->TitleId0, NP->TitleId1, NP->RankValue)
				          : FString(TEXT("плашка: НЕТ (свой павн ещё не заспавнен)"));
			};

			// ── ПОДГОТОВКА ВАРИАНТА: звание получено и НАДЕТО, маркеры чисты. ──
			auto Arm = [&]()
			{
				FlagLost.Remove(Id);
				MaskSet(MaskOwned, Id, true);
				HandleMaskOwned(BuildMaskBody(/*получено*/ true, bExt));   // 6066 с битом
				HandleSlots(Uidx, bSuffix ? WornPrefix : Id, bSuffix ? Id : WornSuffix, FameRank);
				// ★ БЕЗ СЕССИИ (headless без сервера) свой userNum ещё не пришёл пакетом 4003, и
				//   `HandleSlots` законно отбрасывает 6068 гейтом `IsSelfUserIdx` (закон
				//   `self-usernum-zero-is-valid`). Тогда ставим ПРЕДУСЛОВИЕ напрямую и говорим об
				//   этом вслух: проверяемая часть проходит штатными функциями, подменяется только
				//   вход, которого офлайн взяться неоткуда.
				if ((bSuffix ? WornSuffix : WornPrefix) != Id)
				{
					UE_LOG(LogAsdaTitle, Warning,
						TEXT("[звания] RevokeTest: 6068 не принят (нет сессии) — надетый слот ставим напрямую, ОФЛАЙН-ПОДСТАНОВКА"));
					if (bSuffix) { WornSuffix = Id; } else { WornPrefix = Id; }
				}
				PushSelfTitlesToNameplate();
			};

			// ── ПАКЕТЫ СНЯТИЯ (тела ровно по контракту P24) ──
			//   6068 владельцу (14 б): активные слоты уже БЕЗ снятого звания.
			auto Send6068Cleared = [&]()
			{
				HandleSlots(Uidx, (WornPrefix == Id) ? -1 : WornPrefix,
				                  (WornSuffix == Id) ? -1 : WornSuffix, FameRank);
			};
			//   6066: та же маска, но без бита.
			auto Send6066Without = [&]()
			{
				MaskSet(MaskOwned, Id, false);
				TArray<uint8> Body = BuildMaskBody(true, bExt);
				MaskSet(MaskOwned, Id, true);      // вернуть, чтобы диффер увидел переход 1→0
				HandleMaskOwned(Body);
			};
			//   6086: маска «найдено» — сервер ставит бит снятого звания в «найдено».
			auto Send6086 = [&]()
			{
				MaskSet(MaskFound, Id, true);
				HandleMaskFound(BuildMaskBody(/*найдено*/ false, bExt));
			};
			//   6070 код 1 (13 б): [0] userNum · [2] UserIndex · [6] код · [7] titleId · [9] очки.
			auto Send6070 = [&]()
			{
				TArray<uint8> B; B.SetNumZeroed(13);
				AsdaBytes::WrI16(&B[0], Uidx);
				AsdaBytes::WrI32(&B[2], Net ? Net->SelectedCharId : 0);
				B[6] = 1;
				AsdaBytes::WrI16(&B[7], Id);
				AsdaBytes::WrI32(&B[9], FamePoints);
				HandleDeleted(B);
			};

			// ── ОДИН ПРОГОН ВАРИАНТА ──
			int32 Passed = 0;
			auto RunCase = [&](const TCHAR* Name, TFunctionRef<void()> Sequence)
			{
				Arm();
				const int32 NoticeBefore = LossNoticeCount;
				UE_LOG(LogAsdaTitle, Log,
					TEXT("[звания] RevokeTest «%s» ДО: получено=%d надето(приставка=%d окончание=%d) маркер«потеряно»=%d | %s"),
					Name, IsOwned(Id) ? 1 : 0, WornPrefix, WornSuffix, FlagLost.Contains(Id) ? 1 : 0, *PlateStr());

				Sequence();

				const int32 Notices = LossNoticeCount - NoticeBefore;
				const bool bGone = !IsOwned(Id) && WornPrefix != Id && WornSuffix != Id && FlagLost.Contains(Id);
				const bool bIcon = bNoticeActive && NoticeIconKind == 0;
				const bool bOnce = (Notices == 1);
				const bool bOk   = bGone && bIcon && bOnce;
				if (bOk) { ++Passed; }
				UE_LOG(LogAsdaTitle, Log,
					TEXT("[звания] RevokeTest «%s» ПОСЛЕ: получено=%d надето(приставка=%d окончание=%d) маркер«потеряно»=%d значокHUD=%d(вид=%d) нотисов=%d найдено=%d | %s → %s"),
					Name, IsOwned(Id) ? 1 : 0, WornPrefix, WornSuffix, FlagLost.Contains(Id) ? 1 : 0,
					bNoticeActive ? 1 : 0, NoticeIconKind, Notices, IsFound(Id) ? 1 : 0, *PlateStr(),
					bOk ? TEXT("★ УСПЕХ") : TEXT("★ ПРОВАЛ"));
				if (!bOk)
				{
					UE_LOG(LogAsdaTitle, Warning,
						TEXT("[звания] RevokeTest «%s» ПРОВАЛ: ушло=%d значок=%d нотисРовноОдин=%d(факт %d)"),
						Name, bGone ? 1 : 0, bIcon ? 1 : 0, bOnce ? 1 : 0, Notices);
				}
			};

			// 1) ПОЛНЫЙ контракт P24: 6068 → 6066 → 6086 → 6070(код 1).
			RunCase(TEXT("полный контракт P24"), [&]() { Send6068Cleared(); Send6066Without(); Send6086(); Send6070(); });
			// 2) ОБРАТНЫЙ порядок: 6070 доехал раньше маски.
			RunCase(TEXT("6070 раньше 6066"),    [&]() { Send6070(); Send6066Without(); Send6086(); Send6068Cleared(); });
			// 3) ХУДШИЙ: только маска, ни 6070, ни 6068.
			RunCase(TEXT("только маска 6066"),   [&]() { Send6066Without(); });

			// 4) ★ 18.10.2026 ЛОЖНАЯ УТРАТА ПРИ ПОРТАЛЕ: смена карты → короткая 6066 (без бита, P21 ещё не дождался БД) →
			//   полная 6066 (с битом). Звание НЕ теряется: нотисов 0, значка «потеряно» нет, звание по-прежнему получено.
			{
				Arm();
				const int32 NoticeBefore = LossNoticeCount;
				ResetSessionState(EAsdaSessionResetReason::MapChanged);
				Send6066Without();
				// Полная 6066. Старшее звание (id ≥ 256): короткая маска его не несёт, полная — С битом → утраты нет.
				//   Младшее (id < 256): короткая маска несёт младшие биты верно — пропавший бит = НАСТОЯЩАЯ утрата, полная тоже без
				//   бита → ровно одно уведомление (утрата через портал без 6070/6065 не теряется).
				const bool bHigh = (Id >= kMaskBitsLegacy);
				MaskSet(MaskOwned, Id, bHigh);
				TArray<uint8> Full = BuildMaskBody(/*получено*/ true, bExt);
				MaskSet(MaskOwned, Id, false);
				HandleMaskOwned(Full);
				const int32 Notices = LossNoticeCount - NoticeBefore;
				const bool bOk = bHigh ? ((Notices == 0) && IsOwned(Id) && !FlagLost.Contains(Id))
				                       : ((Notices == 1) && !IsOwned(Id) && FlagLost.Contains(Id));
				if (bOk) { ++Passed; }
				UE_LOG(LogAsdaTitle, Log,
					TEXT("[звания] RevokeTest «портал: короткая+полная 6066» ПОСЛЕ: получено=%d маркер«потеряно»=%d нотисов=%d → %s"),
					IsOwned(Id) ? 1 : 0, FlagLost.Contains(Id) ? 1 : 0, Notices, bOk ? TEXT("★ УСПЕХ") : TEXT("★ ПРОВАЛ"));
			}

			UE_LOG(LogAsdaTitle, Log, TEXT("[звания] RevokeTest ИТОГ: пройдено %d из 4 вариантов — %s"),
				Passed, (Passed == 4) ? TEXT("★ ВСЕ СОВПАЛИ, звание уходит при ЗАКРЫТОМ окне, портал не даёт ложной утраты")
				                      : TEXT("★ ЕСТЬ ПРОВАЛ (см. строки выше)"));
			UE_LOG(LogAsdaTitle, Log, TEXT("%s"), *DumpState());
			AsdaSelfTestKit::Finish(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr, TEXT("asda.Title.RevokeTest"),
				KitArgs.bQuit, LogAsdaTitle, TEXT("asda.Title.RevokeTest"), Passed, 4 - Passed);
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

	// ★★★ САМОТЕСТ «ТИТУЛЫ БЕЗ ЗВЁЗДОЧКИ» (07.11.2026, баг владельца «*Occupation land boss»): `asda.Title.SelfTestStar [shotdir=<папка>] [quit]`.
	//   Данные: все записи окна и `GetTitle` без ведущей «*» на языке сессии, в таблице таких ровно 18 (id 99–116); ключ 0 возвращает «*».
	//   Окно 18700: маска 6066 со званиями 99–116, каждая строка читается С ВИДЖЕТА. Баннер и нотис: получение 6065 и утрата.
	//   Плашка: сосед (снапшот 4005), настоящий 6068 со слотами 106 и 99 → имена плашки без «*». Кадры в `shotdir=`.
#if ASDA_DEV_CONSOLE   // дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("asda.Title.SelfTestStar"),
		TEXT("Титулы 99–116 без служебной «*»: данные на языке сессии, окно 18700, баннер, нотис, плашка соседа (6068). Аргументы: shotdir=<папка>, quit."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			static const TCHAR* kCmdStar = TEXT("asda.Title.SelfTestStar");
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			if (AsdaSelfTestKit::RefuseStart(this, kCmdStar, KitArgs)) { return; }
			UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
			UAsdaNetSubsystem* NetW = AsdaSelfTestKit::NetOf(this);
			UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			const UAsdaItemTableSubsystem* Items = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
			if (!W || !NetW || !WS || !Items) { AsdaSelfTestKit::Refused(this, kCmdStar, KitArgs.bQuit, TEXT("нет мира/подсистем")); return; }
			FString ShotDir;
			for (const FString& A : KitArgs.Pos) { if (A.StartsWith(TEXT("shotdir="))) { ShotDir = A.Mid(8).Replace(TEXT("\\"), TEXT("/")); } }
			TSharedRef<int32> Passed = MakeShared<int32>(0);
			TSharedRef<int32> Failed = MakeShared<int32>(0);
			auto Verdict = [Passed, Failed](bool bOk, const FString& What)
			{
				++(bOk ? *Passed : *Failed);
				UE_LOG(LogAsdaTitle, Log, TEXT("ТИТУЛЫ-ЗВЕЗДА %s %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
			};
			auto Shot = [W, ShotDir](const TCHAR* Name)
			{
				if (!ShotDir.IsEmpty() && GEngine) { GEngine->Exec(W, *FString::Printf(TEXT("asda.UI.Shot %s/%s"), *ShotDir, Name)); }
			};
			if (!::IsValid(WS->LocalPlayerActor.Get()) && GEngine)
			{
				GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
				WS->DebugSpawnSelfAt(256, 256, TEXT("СамТест"));
			}
			// ★ R5-fix6-c (13.11.2026): подменённые «свои» поля сети возвращает сторож (как у `asda.Title.RevokeTest` ниже). Прежде userNum 77 и
			//   charId 0xABC289 оставались соседям пачки (в сводке прогона — «свой изменён: … charId 0→11256457»).
			const TSharedRef<FAsdaNetSelfScope> SelfScope = MakeShared<FAsdaNetSelfScope>(NetW, kCmdStar);
			if (!NetW->bSelfUserNumKnown) { NetW->SetSelfUserNumForDebug(77); }
			if (NetW->SelectedCharId == 0) { NetW->SelectedCharId = 0x00ABC289; }
			// ── 1. данные: все записи ──
			int32 NRawStar = 0, NShownStar = 0, NGetStar = 0;
			for (const FAsdaTitleDef& D : Defs)
			{
				NRawStar += D.RawName.StartsWith(TEXT("*")) ? 1 : 0;
				NShownStar += D.Name.Contains(TEXT("*")) ? 1 : 0;
				FAsdaTitleRow Row;
				if (Items->GetTitle(D.Id, Row)) { NGetStar += Row.Name.Contains(TEXT("*")) ? 1 : 0; }
			}
			Verdict(Defs.Num() > 0 && NRawStar == 18 && NShownStar == 0 && NGetStar == 0, FString::Printf(TEXT("записей %d: в таблице со «*» %d (ждём 18), в показе %d, в GetTitle %d (ждём 0), язык %s"),
				Defs.Num(), NRawStar, NShownStar, NGetStar, FAsdaLang::Current() == EAsdaLang::En ? TEXT("en") : TEXT("ru")));
			IConsoleVariable* Key = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Title.StripStar"));
			if (Key)
			{
				Key->Set(0, ECVF_SetByConsole);
				FAsdaTitleRow Row;
				const bool bRaw = Items->GetTitle(106, Row) && Row.Name.StartsWith(TEXT("*"));
				Key->Set(1, ECVF_SetByConsole);
				Verdict(bRaw, FString::Printf(TEXT("контроль: ключ 0 — id 106 «%s» со «*» (дефект воспроизводится)"), *Row.Name));
			}
			// ── 2. окно: все 18 строк с виджета ──
			const bool bExt = (EffectiveMaskBits() > kMaskBitsLegacy);
			for (int32 Id = 99; Id <= 116; ++Id) { MaskSet(MaskOwned, Id, true); }
			HandleMaskOwned(BuildMaskBody(/*получено*/ true, bExt));
			if (!bOpen) { OpenTitleWindow(); }
			SelectTab(0);
			for (int32 Id = 99; Id <= 116; ++Id)
			{
				const FAsdaTitleDef* D = FindDef(Id);
				const int32 Idx = Order.IndexOfByKey(Id);
				bool bRow = false, bName = false, bStar = false;
				if (D && Idx != INDEX_NONE)
				{
					ScrollTop = Idx; SelectedRow = -1;
					RefreshList();
					SelectRow(0);
					bRow = RowTitleId.IsValidIndex(0) && RowTitleId[0] == Id;
					for (int32 C = 0; bRow && C < 7; ++C)
					{
						const FString T = NodeText(Rows[0].Cells[C].Node);
						bName |= T.Contains(D->Name);
						bStar |= T.Contains(TEXT("*"));
					}
				}
				Verdict(bRow && bName && !bStar, FString::Printf(TEXT("окно: id %d «%s» строка=%d имя=%d звезда=%d"), Id, D ? *D->Name : TEXT("НЕТ"), bRow, bName, bStar));
			}
			{
				const int32 Idx = Order.IndexOfByKey(106);
				ScrollTop = FMath::Max(0, Idx); SelectedRow = -1;
				RefreshList();
				SelectRow(0);
			}
			// ── 3. баннер и нотисы: получение 6065 и утрата — те же записи ──
			NoticeTitleLost({ 99 });
			{
				TArray<uint8> B; B.SetNumZeroed(49);
				AsdaBytes::WrI16(&B[0], (int32)NetW->SelfUserNum); AsdaBytes::WrI32(&B[2], NetW->SelectedCharId); AsdaBytes::WrI16(&B[6], 106); B[8] = 0;
				for (int32 i = 0; i < 18; i += 3) { AsdaBytes::WrI16(&B[9 + 2 * i], -1); }
				AsdaBytes::WrI32(&B[45], FamePoints + 1);
				HandleGranted(B);
			}
			// ── 4. плашка соседа: снапшот и настоящий 6068 ──
			constexpr int32 kPeerStar = 64;
			const AActor* Self = WS->LocalPlayerActor.Get();
			const FVector2D G0 = Self ? AsdaCoord::UeToGame2D(Self->GetActorLocation()) : FVector2D(25600.f, 25600.f);
			WS->DebugSpawnFakePeer(kPeerStar, TEXT("Волчатник"), G0 + FVector2D(250.f, 0.f));
			FTimerHandle H1;
			W->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateWeakLambda(this, [this, W, NetW, WS, Verdict, Shot, Passed, Failed, KitArgs, SelfScope]()
			{
				TArray<uint8> B; B.SetNumZeroed(14);
				AsdaBytes::WrI16(&B[0], kPeerStar); AsdaBytes::WrI32(&B[6], 0); AsdaBytes::WrI16(&B[10], 106); AsdaBytes::WrI16(&B[12], 99);
				NetW->DebugFeedPacket(6068, B);
				Shot(TEXT("title_window_banner"));
				FTimerHandle H2;
				W->GetTimerManager().SetTimer(H2, FTimerDelegate::CreateWeakLambda(this, [this, W, WS, Verdict, Shot, Passed, Failed, KitArgs, SelfScope]()
				{
					AActor* A = WS->ActorByUserIdx(kPeerStar);
					const UAsdaNameplateComponent* NP = A ? A->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
					const FAsdaTitleDef* D0 = FindDef(106);
					const FAsdaTitleDef* D1 = FindDef(99);
					Verdict(NP && D0 && D1 && NP->TitleName == D0->Name && NP->TitleName1 == D1->Name && !NP->TitleName.Contains(TEXT("*")) && !NP->TitleName1.Contains(TEXT("*")),
						FString::Printf(TEXT("плашка соседа по 6068: «%s» «%s»"), NP ? *NP->TitleName : TEXT("НЕТ"), NP ? *NP->TitleName1 : TEXT("НЕТ")));
					CloseTitleWindow();
					Shot(TEXT("title_plate"));
					FTimerHandle H3;
					W->GetTimerManager().SetTimer(H3, FTimerDelegate::CreateWeakLambda(this, [this, W, Passed, Failed, KitArgs, SelfScope]()
					{
						for (int32 Id = 99; Id <= 116; ++Id) { MaskSet(MaskOwned, Id, false); }
						SelfScope->Restore();   // «свой» — как был до прогона
						AsdaSelfTestKit::Finish(W, TEXT("asda.Title.SelfTestStar"), KitArgs.bQuit, LogAsdaTitle, TEXT("ТИТУЛЫ-ЗВЕЗДА"), *Passed, *Failed);
					}), 0.5f, false);
				}), 1.0f, false);
			}), 1.5f, false);
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

	// ★ [ПОРТ] 26.10.2026 (задача владельца «включи звания 187–205, получить могут все»): самотест ПУТЁМ ИГРОКА для 18 званий,
	//   включённых в данных (title_list Enabled 0→2, Point — исходный клиент; сервер — FameInfo + AsdaPatch P88). Для каждого:
	//   строка таблицы включена, класс 0, очки = исходному клиенту, имя/описание на языке сессии (RU — кириллица, EN — латиница);
	//   маска 6066 со всеми битами, кроме 199 → окно 18700, вкладка 0: строка ЧИТАЕТСЯ С ВИДЖЕТА (имя в ячейке, описание в 18723,
	//   очки в 18718); 199 до пакета в списке нет (гейт «получено/найдено»), после 6065 «получено» — есть, помечено новым.
	//   Запуск: asda_selftest_batch.py asda.Title.EventTitlesTest [--lang en]. Офлайн-гейт — как у RevokeTest.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("asda.Title.EventTitlesTest"),
		TEXT("Звания 187–205 (кроме 204): данные (включено, класс 0, очки, тексты на языке сессии), показ в окне 18700 с виджетов ")
		TEXT("и получение пакетом 6065. Аргумент `quit` — выйти после прогона."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			if (AsdaSelfTestKit::RefuseStart(this, TEXT("asda.Title.EventTitlesTest"), KitArgs)) { return; }
			UAsdaNetSubsystem* NetW = AsdaSelfTestKit::NetOf(this);
			FAsdaNetSelfScope SelfScope(NetW, TEXT("asda.Title.EventTitlesTest"));
			if (NetW && !NetW->bSelfUserNumKnown) { NetW->SelfUserNum = 77; NetW->bSelfUserNumKnown = true; }
			if (NetW && NetW->SelectedCharId == 0) { NetW->SelectedCharId = 0x00ABC288; }
			const int32 Uidx = NetW ? (int32)NetW->SelfUserNum : 0;
			const int32 CharId = NetW ? NetW->SelectedCharId : 0;
			const bool bEn = FAsdaLang::Current() == EAsdaLang::En;
			// id → очки исходного клиента Interlude (Reports/merged_titles/title_conflicts.csv, кол. 42 сервера — то же)
			static const int32 kIds[18][2] = {
				{ 187, 9 }, { 188, 19 }, { 189, 53 }, { 190, 23 }, { 191, 34 }, { 192, 28 }, { 193, 17 }, { 194, 22 }, { 195, 57 },
				{ 196, 29 }, { 197, 34 }, { 198, 102 }, { 199, 21 }, { 200, 21 }, { 201, 35 }, { 202, 77 }, { 203, 113 }, { 205, 67 } };
			int32 Passed = 0, Failed = 0;
			auto Verdict = [&](bool bOk, const FString& What)
			{
				if (bOk) { ++Passed; } else { ++Failed; }
				UE_LOG(LogAsdaTitle, Log, TEXT("[звания] EventTitlesTest %s: %s"), bOk ? TEXT("OK") : TEXT("FAIL"), *What);
			};
			auto Script = [bEn](const FString& S) -> bool
			{
				bool bCyr = false, bLat = false;
				for (TCHAR C : S) { if (C >= 0x0400 && C <= 0x04FF) { bCyr = true; } if ((C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z')) { bLat = true; } }
				return bEn ? (bLat && !bCyr) : bCyr;
			};
			// ── 1. данные ──
			for (const auto& P : kIds)
			{
				const FAsdaTitleDef* D = FindDef(P[0]);
				const bool bOk = D && D->Enabled != 0 && D->ClassReq == 0 && D->Group == 0 && D->Point == P[1]
					&& !D->Name.IsEmpty() && D->Name != TEXT("-") && !D->Desc.IsEmpty() && D->Desc != TEXT("-")
					&& Script(D->Name) && Script(D->Desc);
				Verdict(bOk, FString::Printf(TEXT("данные %d «%s» — «%s»: включено=%d класс=%d вкладка=%d очки=%d (ждём %d), язык %s"),
					P[0], D ? *D->Name : TEXT("НЕТ"), D ? *D->Desc : TEXT(""), D ? D->Enabled : -1, D ? D->ClassReq : -1,
					D ? D->Group : -1, D ? D->Point : -1, P[1], bEn ? TEXT("en") : TEXT("ru")));
			}
			// ── 2. окно: маска «получено» со всеми битами, кроме 199 ──
			const bool bExt = (EffectiveMaskBits() > kMaskBitsLegacy);
			for (const auto& P : kIds) { MaskSet(MaskOwned, P[0], P[0] != 199); }
			MaskSet(MaskFound, 199, false);
			HandleMaskOwned(BuildMaskBody(/*получено*/ true, bExt));
			if (!bOpen) { OpenTitleWindow(); }
			Verdict(bOpen && bControlsReady, FString::Printf(TEXT("окно 18700 открыто (контролы %d)"), bControlsReady ? 1 : 0));
			SelectTab(0);
			auto ShowRow = [&](int32 Id) -> bool
			{
				const int32 Idx = Order.IndexOfByKey(Id);
				if (Idx == INDEX_NONE) { return false; }
				ScrollTop = Idx; SelectedRow = -1;
				RefreshList();
				SelectRow(0);
				return RowTitleId.IsValidIndex(0) && RowTitleId[0] == Id;
			};
			for (const auto& P : kIds)
			{
				if (P[0] == 199) { continue; }
				const FAsdaTitleDef* D = FindDef(P[0]);
				const bool bRow = D && ShowRow(P[0]);
				FString Cells;
				bool bName = false;
				if (bRow) { for (int32 C = 0; C < 7; ++C) { const FString T = NodeText(Rows[0].Cells[C].Node); Cells += TEXT("[") + T + TEXT("]"); bName |= T.Contains(D->Name); } }
				const FString Cond  = TxtInfoCondition ? TxtInfoCondition->GetText().ToString() : FString();
				const FString Point = TxtInfoPoint ? TxtInfoPoint->GetText().ToString() : FString();
				const bool bOk = bRow && bName && D && Cond == D->Desc && Point.Contains(FString::FromInt(P[1]));
				Verdict(bOk, FString::Printf(TEXT("окно: %d в списке вкладки 0, строка %s, подсказка 18723 «%s», очки 18718 «%s»"),
					P[0], *Cells, *Cond, *Point));
			}
			// ── 3. 199: до пакета нет, после 6065 «получено» — есть, помечено новым ──
			Verdict(!Order.Contains(199), TEXT("199 до пакета 6065 — нет в списке вкладки 0 (не получено и не найдено)"));
			{
				TArray<uint8> B; B.SetNumZeroed(49);
				AsdaBytes::WrI16(&B[0], Uidx); AsdaBytes::WrI32(&B[2], CharId); AsdaBytes::WrI16(&B[6], 199); B[8] = 0;
				for (int32 i = 0; i < 18; i += 3) { AsdaBytes::WrI16(&B[9 + 2 * i], -1); }
				AsdaBytes::WrI32(&B[45], FamePoints + 21);
				HandleGranted(B);
			}
			const bool bNew = FlagNew.Contains(199);
			const FAsdaTitleDef* D199 = FindDef(199);
			const bool bRow199 = ShowRow(199);
			const FString Cond199 = TxtInfoCondition ? TxtInfoCondition->GetText().ToString() : FString();
			Verdict(IsOwned(199) && bNew && bRow199 && D199 && Cond199 == D199->Desc,
				FString::Printf(TEXT("199 после 6065: получено=%d новое=%d в списке=%d, подсказка «%s»"), IsOwned(199) ? 1 : 0, bNew ? 1 : 0,
					bRow199 ? 1 : 0, *Cond199));
			Verdict(TabTotalCount >= 18 && TabOwnedCount >= 18, FString::Printf(TEXT("вкладка 0: всего %d, получено %d (все 18 — в счёте)"),
				TabTotalCount, TabOwnedCount));
			if (bOpen) { CloseTitleWindow(); }
			UE_LOG(LogAsdaTitle, Log, TEXT("[звания] EventTitlesTest ИТОГ: %d OK, %d FAIL"), Passed, Failed);
			AsdaSelfTestKit::Finish(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr, TEXT("asda.Title.EventTitlesTest"),
				KitArgs.bQuit, LogAsdaTitle, TEXT("asda.Title.EventTitlesTest"), Passed, Failed);
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

	UE_LOG(LogAsdaTitle, Log, TEXT("[звания] подсистема поднята: званий в таблице %d (id 0…%d), статов %d"),
		Defs.Num(), MaxTitleId, OptionNames.Num());
}

void UAsdaTitleSubsystem::Deinitialize()
{
	FAsdaLang::OnLanguageChanged().RemoveAll(this);   // ★ 10.10.2026: подписка «язык на лету»
	if (UAsdaWindowSubsystem* WMc = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WMc->UnregisterWindowCloser(kNoticeId);   // волна В5: закрыватель значка в «закрыть всё»
	}
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnTitleGranted.Remove(HGranted);
		Net->OnTitleMaskOwned.Remove(HMaskOwned);
		Net->OnTitleDeleted.Remove(HDeleted);
		Net->OnTitleFound.Remove(HFound);
		Net->OnTitleMaskFound.Remove(HMaskFound);
		Net->OnTitleStatBatch.Remove(HStatBatch);
		Net->OnCharTitle.Remove(HSlots);
	}
	for (IConsoleObject* C : ConsoleCmds) { if (C) { IConsoleManager::Get().UnregisterConsoleObject(C); } }
	ConsoleCmds.Reset();
	ResetWidget();
	Super::Deinitialize();
}

// ★★★ [ИСПРАВЛЕНИЕ] 25.10.2026 — МАРКЕРЫ «НОВОЕ»/«ПОТЕРЯНО» И НАДЕТАЯ ПРИСТАВКА НЕ ПЕРЕЖИВАЮТ ВЫХОД НА ЛОГИН (ревизия
//   `Reports/inventory_relog_leak/REVIEW.md`, Н2). Было: полный сброс снимал окончание (`WornSuffix`), но не приставку
//   (`WornPrefix`), и не чистил `FlagNew`/`FlagLost` — у учётки Б в окне званий мигали маркеры на id, полученных/утраченных
//   учёткой А, а плашка приставки до прихода 6068/6086 показывала звание А. У оригинала этого класса дефекта нет: при разрыве
//   клиент закрывается ([[original-quits-client-on-disconnect]]); подсистема порта живёт на `GameInstance`.
//   Точки — те же, что у очистки вещей: `LeftWorld` / `EnteringWorld`; `MapChanged` и `Disconnected` не трогаются
//   (там маркеры и надетое — по-прежнему данные текущего персонажа).
//   Ключ: 1 — очищать (по умолчанию); 0 — прежнее поведение (маркеры и приставка переживали перезаход).
static ASDA_DEV_CVAR(int32, CVarTitleRelogClearFlags,
	TEXT("asda.Title.RelogClearFlags"),
	1,
	TEXT("Очистка маркеров «новое»/«потеряно» и надетой приставки званий при выходе на логин / входе в мир: 1 = да "
	     "(исправление перезахода, 25.10.2026); 0 = как было — переживали перезаход."),
	ECVF_Default);

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaTitleSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Очереди пакетов званий: тела прошлой сессии применять к новому персонажу нельзя.
	QGranted.Reset(); QMaskOwned.Reset(); QDeleted.Reset();
	QFound.Reset(); QMaskFound.Reset(); QStatBatch.Reset(); QSlots.Reset();
	PendingDeleteId = -1;
	// ★★★ 18.10.2026 (ложная «утрата» при каждом портале): сервер (наш P21/P22) на загрузке персонажа — и при СМЕНЕ КАРТЫ —
	//   шлёт сначала 6066 со старшей половиной маски в нулях, затем вторую, полную (ответ БД 2332). Сравнение первой с маской
	//   прошлой карты давало «утрачено» для званий id ≥ 256 (Зомби 279) — значок «потеряно» и нотис 990110 на каждом
	//   переходе. Первая 6066 на новой карте — точка отсчёта, как при входе в мир. Настоящие утраты несут 6070 и 6065.
	bMaskOwnedSeen = false;
	bCompareLowAfterMapChange = !AsdaIsFullSessionReset(Reason);   // портал: младшие биты сравнимы с прошлой картой
	if (!AsdaIsFullSessionReset(Reason)) { return; }
	UE_LOG(LogAsdaTitle, Log, TEXT("[звания] сброс сессии (%s): очередь и надетые звания сняты"),
		AsdaSessionResetReasonName(Reason));
	ResetWidget();
	bOpen = false;
	FamePoints = 0; FameRank = 0;
	WornSuffix = -1; SelPrefix = -1; SelSuffix = -1;
	Tab = 0; ScrollTop = 0; SelectedRow = -1;
	Order.Reset();
	TotalRatePercent = 0.f;
	CooldownMs = 0; LastCooldownTickS = 0.0;
	bNoticeActive = false; NoticeIconKind = 1; NoticeTipKind = 0;
	bMaskOwnedSeen = false; LossNoticeCount = 0;
	HoverRow = -1;
	Mouse.Reset();
	// ★ [ИСПРАВЛЕНИЕ] 25.10.2026 (Н2): начало новой сессии — шапка ключа `asda.Title.RelogClearFlags` выше.
	if (!AsdaSessionResetKeepsWorldAlive(Reason) && CVarTitleRelogClearFlags.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogAsdaTitle, Log, TEXT("[звания] сброс сессии (%s): маркеров «новое» %d, «потеряно» %d, приставка %d — сняты"),
			AsdaSessionResetReasonName(Reason), FlagNew.Num(), FlagLost.Num(), WornPrefix);
		FlagNew.Reset();
		FlagLost.Reset();
		WornPrefix = -1;
	}
}

void UAsdaTitleSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.Title");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// ── Сеть разбирается ВСЕГДА (значок HUD и плашка над головой живут без окна) ──
	if (QMaskOwned.Num() || QMaskFound.Num() || QStatBatch.Num() || QGranted.Num()
		|| QDeleted.Num() || QFound.Num() || QSlots.Num())
	{
		TArray<TArray<uint8>> A = MoveTemp(QMaskOwned); QMaskOwned.Reset();
		for (const TArray<uint8>& B : A) { HandleMaskOwned(B); }
		TArray<TArray<uint8>> C = MoveTemp(QMaskFound); QMaskFound.Reset();
		for (const TArray<uint8>& B : C) { HandleMaskFound(B); }
		TArray<TArray<uint8>> D = MoveTemp(QStatBatch); QStatBatch.Reset();
		for (const TArray<uint8>& B : D) { HandleStatBatch(B); }
		TArray<TArray<uint8>> E = MoveTemp(QGranted); QGranted.Reset();
		for (const TArray<uint8>& B : E) { HandleGranted(B); }
		TArray<TArray<uint8>> F = MoveTemp(QDeleted); QDeleted.Reset();
		for (const TArray<uint8>& B : F) { HandleDeleted(B); }
		TArray<TArray<uint8>> G = MoveTemp(QFound); QFound.Reset();
		for (const TArray<uint8>& B : G) { HandleFound(B); }
		TArray<FSlotsMsg> H = MoveTemp(QSlots); QSlots.Reset();
		for (const FSlotsMsg& S : H) { HandleSlots(S.UserIdx, S.T0, S.T1, S.Rank); }
	}

	// ── Кулдаун: эталонный `SetTimer(0x11, 1000)` → раз в секунду минус 1000 мс ──
	if (CooldownMs > 0)
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - LastCooldownTickS >= 1.0)
		{
			LastCooldownTickS = Now;
			CooldownMs -= 1000;
			if (CooldownMs < 0)
			{
				CooldownMs = 0;
				SetTxt(TxtLeftTime, Loc(TLoc::kLeftTimeNone));
			}
			else if (TxtLeftTime)
			{
				UAsdaTextSubsystem::FNoticeSlots S;
				S.Num[0] = CooldownMs / 60000;
				S.Num[1] = (CooldownMs % 60000) / 1000;
				SetTxt(TxtLeftTime, UAsdaTextSubsystem::FormatNoticeText(LocTagged(TLoc::kLeftTimeFmt), S));
			}
		}
	}

	// ★ ОПРОС МЫШИ ПЕРЕЕХАЛ СЮДА ИЗ `TickMouse` (2026-08-30). `TickMouse` гейтится `bOpen`, а значок
	//   HUD надо кликать именно ПРИ ЗАКРЫТОМ окне. `Update()` обязан звучать РОВНО ОДИН раз за кадр
	//   и ДО любого разбора (он считает фронты по латчу прошлого кадра) — см. шапку `FAsdaMouseFrame`.
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал.
	//   ★ Гейт стоит ДО `TickNoticeIcon` намеренно: значок 50000 живёт вне гейта `bOpen`, но мышь
	//     ему под модалкой эталон тоже не отдаёт. Мигание значка — не ввод, его оставляем.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		HideHoverTip();          // висящую подсказку кнопки/значка ГАСИМ, а не просто не показываем
		if (ScrollBar.IsBuilt()) // латч захвата бегунка снимает кадр «кнопка отпущена»
		{
			int32 Dummy = 0;
			ScrollBar.Tick(false, false, FMath::Max(0, Order.Num() - kRows), Dummy);
		}
		UpdateBlink();
		Mouse.Reset();
		return;
	}

	UpdateBlink();
	TickNoticeIcon();   // клик/наведение по значку 50000 — вне гейта `bOpen`

	if (!bOpen || !bControlsReady) { return; }
	// Живая геометрия готова только со второго кадра, поэтому полосу перекладываем каждый тик:
	//   `Layout` идемпотентен и стоит четыре присваивания слотов.
	LayoutScrollBar();
	TickMouse();
}

// ═════════════════════════════ ДАННЫЕ ═════════════════════════════════════════════════════════
void UAsdaTitleSubsystem::BuildDefs()
{
	Defs.Reset();
	DefIndexById.Reset();
	OptionNames.Reset();
	MaxTitleId = 0;

	if (const UDataTable* Dt = AsdaDt::Cached(kDtTitleList))
	{
		TArray<int32> Keys;
		for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
		{
			const FString Rn = It.Key.ToString();
			if (Rn.IsNumeric()) { Keys.Add(FCString::Atoi(*Rn)); }
		}
		Keys.Sort();
		for (int32 K : Keys)
		{
			const FAsdaTitleRow* R = AsdaDt::RowByInt<FAsdaTitleRow>(kDtTitleList, K, TEXT("title_list"));
			if (!R) { continue; }
			FAsdaTitleDef D;
			D.Id = R->Id;  D.SortNum = R->SortNum;  D.ClassReq = R->ClassReq;
			// ★ [ПОРТ] 09.10.2026 (мультиязычность): имя и описание звания — на языке сессии (оверлей `title_list`, ключи `<_row>.Name`/`<_row>.Desc`).
			D.RawName = AsdaLangRows::Text(Dt, R, TEXT("Name"), R->Name);
			D.Name = AsdaLangRows::TitleDisplayName(D.RawName);   // ★ [ИСПРАВЛЕНИЕ] 07.11.2026: окно, баннер, нотисы — без служебной «*»
			D.Grade = R->ColorIndex;  D.Group = R->Group;
			D.Position = R->Position;  D.CategoryLoc = R->CategoryLocIndex;
			D.Point = R->Point;  D.Desc = AsdaLangRows::Text(Dt, R, TEXT("Desc"), R->Desc);  D.Enabled = R->Enabled;
			D.OptionTypeId = { R->OptionTypeId0, R->OptionTypeId1, R->OptionTypeId2,
			                   R->OptionTypeId3, R->OptionTypeId4, R->OptionTypeId5 };
			D.IsPercent    = { R->IsPercent0, R->IsPercent1, R->IsPercent2,
			                   R->IsPercent3, R->IsPercent4, R->IsPercent5 };
			DefIndexById.Add(D.Id, Defs.Num());
			MaxTitleId = FMath::Max(MaxTitleId, D.Id);
			Defs.Add(MoveTemp(D));
		}
	}
	if (Defs.Num() == 0)
	{
		UE_LOG(LogAsdaTitle, Error, TEXT("[звания] таблица %s пуста — окно будет без списка"), kDtTitleList);
	}

	// Словарь имён статов бонусов (`title_option`, ссылка из `OptionTypeId`).
	if (const UDataTable* Dt = AsdaDt::Cached(kDtTitleOption))
	{
		for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
		{
			const FString Rn = It.Key.ToString();
			if (!Rn.IsNumeric()) { continue; }
			const int32 K = FCString::Atoi(*Rn);
			if (const FAsdaTitleOptionRow* R =
				AsdaDt::RowByInt<FAsdaTitleOptionRow>(kDtTitleOption, K, TEXT("title_option")))
			{
				// ★ [ПОРТ] 09.10.2026 (мультиязычность): оверлей `title_option`, ключ `<_row>.OptionStatName`.
				OptionNames.Add(R->Id, AsdaLangRows::Text(Dt, R, TEXT("OptionStatName"), R->OptionStatName));
			}
		}
	}

	// ★ D3: предупредить о званиях, которые не влезают в ДЕЙСТВУЮЩУЮ маску.
	//   До расширения (и при `asda.Title.Mask512 0`) таких 24 — id 256…279.
	const int32 Bits = EffectiveMaskBits();
	int32 Unreachable = 0;
	for (const FAsdaTitleDef& D : Defs) { if (D.Id >= Bits) { ++Unreachable; } }
	if (Unreachable > 0)
	{
		UE_LOG(LogAsdaTitle, Warning,
			TEXT("[звания] %d званий с id >= %d — действующая маска славы вмещает ровно %d бит, ")
			TEXT("такие звания получить НЕЛЬЗЯ. (Маска 512 бит включается `asda.Title.Mask512 1`.)"),
			Unreachable, Bits, Bits);
	}
	else
	{
		UE_LOG(LogAsdaTitle, Log,
			TEXT("[звания] маска славы %d бит; все %d званий таблицы (максимальный id %d) достижимы."),
			Bits, Defs.Num(), MaxTitleId);
	}
}

const FAsdaTitleDef* UAsdaTitleSubsystem::FindDef(int32 TitleId) const
{
	const int32* Idx = DefIndexById.Find(TitleId);
	return Idx ? &Defs[*Idx] : nullptr;
}

// ═════════════════════════════ МАСКИ ══════════════════════════════════════════════════════════
// Сколько бит маски действует сейчас. 512 — расширенная маска; 256 — откат к оригиналу.
int32 UAsdaTitleSubsystem::EffectiveMaskBits()
{
	return (CVarMask512.GetValueOnAnyThread() != 0) ? kMaskBits : kMaskBitsLegacy;
}

bool UAsdaTitleSubsystem::MaskGet(const uint8* Mask, int32 Bit)
{
	// ★ Границу маски НЕ переходим (эталон читает за её пределы — чужую память).
	//   При `asda.Title.Mask512 0` граница возвращается к оригинальным 256 битам.
	if (Bit < 0 || Bit >= EffectiveMaskBits()) { return false; }
	return (Mask[Bit >> 3] & (1u << (Bit & 7))) != 0;
}

void UAsdaTitleSubsystem::MaskSet(uint8* Mask, int32 Bit, bool bOn)
{
	if (Bit < 0 || Bit >= EffectiveMaskBits()) { return; }
	if (bOn) { Mask[Bit >> 3] |= (uint8)(1u << (Bit & 7)); }
	else     { Mask[Bit >> 3] &= (uint8)~(1u << (Bit & 7)); }
}

bool UAsdaTitleSubsystem::IsOwned(int32 TitleId) const { return MaskGet(MaskOwned, TitleId); }
bool UAsdaTitleSubsystem::IsFound(int32 TitleId) const { return MaskGet(MaskFound, TitleId); }

// ═════════════════════════════ ОКНО ═══════════════════════════════════════════════════════════
bool UAsdaTitleSubsystem::EnsureWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWindowWbp, kWinZ).Ensure(GetGameInstance(), Widget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem&)
		{
			SetupControls();
			RegisterZone();
		},
		[]()
		{
			UE_LOG(LogAsdaTitle, Error, TEXT("[звания] WBP_Title_Win не загрузился (%s)"), kWindowWbp);
		});
	// Скрытие окна (шаг каркаса): в .JUI у 18700 Visible="0"
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaTitleSubsystem::ResetWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterWheelHandler(kWindowId);
		WM->UnregisterUiZone(kWindowId);
		WM->CloseWindow(kWindowId);
	}
	HideHoverTip();
	Widget = nullptr; RootCanvas = nullptr;
	BtnClose = BtnApply = BtnDelete = BtnOk = BtnCancel = BtnRelease = nullptr;
	TabButtons.Reset();
	TxtRanking = TxtTotalPoint = TxtTotalRate = nullptr;
	TxtRankDelta = TxtPointDelta = TxtRateDelta = nullptr;
	TxtCharName = TxtTabRate = nullptr;
	TxtLeftTimeCap = TxtLeftTime = nullptr;
	TxtInfoCap = TxtInfoGrade = TxtInfoPoint = nullptr;
	TxtInfoLocation = TxtInfoRandom = TxtInfoType = TxtInfoTypeName = TxtInfoCondition = nullptr;
	RsFirstTitle = FAsdaTitleRichSlot(); RsSecondTitle = FAsdaTitleRichSlot();
	RsInfoName = FAsdaTitleRichSlot(); RsInfoOption.Reset();
	ColumnCaptions.Reset(); ColumnZones.Reset();
	TabPlanes.Reset(); TabCaptions.Reset();
	TabPlaneIdle.Reset(); TabBtnIdle.Reset(); TabCaptionIdle.Reset();
	DeltaArrows.Reset(); RowPlanes.Reset(); SelectedFrame.Reset(); FramePlanes.Reset();
	Rows.Reset(); Relays.Reset();
	ScrollBar = FAsdaScrollBarNomal();
	bControlsReady = false;
	bOpen = false;
	SelectedRow = -1;
	Mouse.Reset();
}
