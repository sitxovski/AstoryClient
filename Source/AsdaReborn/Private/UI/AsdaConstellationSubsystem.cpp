// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окна `Constellation_S_WIN` (JUI 74500) — ВЫБОР ЗНАКА ЗОДИАКА.
//   Весь разбор эталона, адреса, дерево контролов, пакеты и дефекты — в шапке
//   `AsdaConstellationSubsystem.h` и в отчёте `Reports/npc_button_30073.md`.

#include "UI/AsdaConstellationSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaNetSubsystem.h"          // FAsdaCharRecord::ConstellationName, отправка 6586
#include "AsdaBytes.h"
#include "AsdaDataTableCache.h"
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaGameText.h"        // FAsdaTextRow — строка loose-таблицы `DT_Constellation`
#include "Text/AsdaLang.h"             // [ПОРТ] 09.10.2026 (мультиязычность): имя знака на языке сессии
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaCharaInfoSubsystem.h"
#include "UI/AsdaJuiComboBox.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд
#include "Test/AsdaSelfTestKit.h"           // ★ R5-fix-b 13.11.2026 (второй заход): разбор аргументов / итог самотеста рамки

DEFINE_LOG_CATEGORY_STATIC(LogAsdaConstellation, Log, All);

namespace
{
	// Окно и его виджет: ассет уже импортирован заходом JUI (закон `no-invented-ui`).
	const FName  kWindowId(TEXT("Constellation_S_WIN"));
	const TCHAR* kWindowWbp =
		TEXT("/Game/AsdaAssets/UI/Player_UI/Character/WBP_Constellation_S_WIN.WBP_Constellation_S_WIN_C");

	// Таблица знаков: импорт `./Data/Constellation.BIN` (`ConstellationTable_Load@0xF1A280`,
	//   записи по 65 б: u8 ключ + 64 б имя). Ключи 1…12, они же RowName.
	const TCHAR* kSignTablePath = TEXT("/Game/AsdaAssets/Text/World/DT_Constellation.DT_Constellation");

	// Таблица ВЛЕЧЕНИЯ: импорт `./Data/Constellation_Match.BIN` (`ConstellationMatchTable_Load
	//   @0xF1A3D0`, записи по 5 б). Собирается `Tools/asda_build_constellation_match_dt.py`.
	const TCHAR* kMatchTablePath =
		TEXT("/Game/AsdaAssets/Data/DT_ConstellationMatch.DT_ConstellationMatch");

	// idc контролов окна — ровно те, что эталон печатает в диагностических `MessageBoxA`.
	const TCHAR* kIdcWin        = TEXT("Constellation_S_WIN");
	const TCHAR* kIdcSel        = TEXT("Constellation_S_SEL");
	const TCHAR* kIdcText       = TEXT("Constellation_S_TEXT");
	const TCHAR* kIdcTitle      = TEXT("Constellation_S_TEXTMOVE");
	const TCHAR* kIdcBtnOk      = TEXT("Constellation_S_OK_btn");
	const TCHAR* kIdcBtnCancel  = TEXT("Constellation_S_XXbtn");
	const TCHAR* kIdcBtnClose   = TEXT("Constellation_S_Xbtn");

	// Планов у скина `Black_BottomRound_White2Border` ровно девять (`JuiSkin.ini`, ES=0…8).
	constexpr int32 kFramePlanes = 9;

	// ── Нотисы (все id сняты дизассемблером, см. шапку заголовка) ──
	namespace ConsLoc
	{
		constexpr int32 kBtnOk       = 2423;     // «ОК»                       — подпись 74504
		constexpr int32 kBtnCancel   = 2424;     // «Отмена»                   — подпись 74505
		constexpr int32 kTitle       = 2580;     // «Сменить знак зодиака»     — 74503
		constexpr int32 kAlreadySet  = 2581;     // «…сменить его невозможно.»  — гейт и код 2
		constexpr int32 kNotPicked   = 2582;     // «Знак зодиака не выбран.»   — ОК без выбора
		constexpr int32 kHintOnce    = 2583;     // «…выбирается раз и навсегда» — 74502
		constexpr int32 kAssigned    = 2584;     // «Твой знак зодиака: [ss1].» — код 1
		constexpr int32 kServerError = 117;      // «Произошла ошибка…»         — коды 98 и 99
		constexpr int32 kListCaption = 110801;   // «Знак зодиака» — заголовок списка 74501
		constexpr int32 kMsgBoxOk    = 2288;     // подпись «ОК» у общего `ONEBUT_MSGBOX` 1500

		// ── Влечение (`uiFormatPartyRelationNotice@0xF1A520`), ровно три исхода ──
		constexpr int32 kAttractBest   = 110632; // «Влечение: * (Максимальное)» — возврат 0, img[0]
		constexpr int32 kAttractStrong = 110633; // «Влечение: * (Сильное)»      — возврат 1, img[1]
		constexpr int32 kAttractNormal = 110634; // «Влечение: * (Обычное)»      — возврат 2, img[2]
	}

	// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА D1 (см. шапку заголовка): по умолчанию НЕ предвыбираем
	//   первый знак. Значение 1 возвращает поведение эталона 1:1 (`SetSelectedItem(list, 0)`).
	ASDA_DEV_CVAR(int32, CVarPreselectFirst,
		TEXT("asda.Constellation.PreselectFirst"), 0,
		TEXT("1 = как в оригинале: открытие окна 74500 сразу выбирает первый знак (Овен), хотя\n")
		TEXT("подпись списка показывает заглушку «Знак зодиака». 0 (по умолчанию) = выбор пуст,\n")
		TEXT("и кнопка ОК без выбора честно показывает нотис 2582."),
		ECVF_Default);

	// ★ ДЕФЕКТ ОРИГИНАЛА D5. `findRecord5ByFirstByte@0xF1A770` берёт ПЕРВУЮ запись с нужным знаком
	//   и байты месяца/дня НЕ читает нигде ⇒ игра всегда считает влечение по блоку 10/07, а 48
	//   записей остальных четырёх недель мертвы. По умолчанию (0) держим это поведение 1:1;
	//   значение 1 включает ИСПРАВЛЕННУЮ ветку — выбор недельного блока по текущей дате.
	ASDA_DEV_CVAR(int32, CVarUseDate,
		TEXT("asda.Constellation.UseDate"), 0,
		TEXT("0 (по умолчанию) = как в оригинале: влечение всегда берётся из ПЕРВОГО недельного\n")
		TEXT("блока таблицы Constellation_Match.BIN (10/07), поля месяца и дня игнорируются\n")
		TEXT("(дефект оригинала D5). 1 = исправлено: блок выбирается по текущей дате."),
		ECVF_Default);
}

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaConstellationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь.
		HResult = Net->OnConstellationResult.AddLambda(
			[this](const TArray<uint8>& B) { PendingResults.Push(B); });
	}
	BuildSignList();
	BuildMatchTable();
	UE_LOG(LogAsdaConstellation, Log,
		TEXT("[знак зодиака] подсистема поднята, знаков в таблице: %d, записей влечения: %d"),
		SignIds.Num(), MatchRows.Num());
	// ★ 10.10.2026 (язык на лету, до входа в мир): названия знаков — заново на новом языке.
	FAsdaLang::OnLanguageChanged().AddWeakLambda(this, [this]() { BuildSignList(); });
}

void UAsdaConstellationSubsystem::Deinitialize()
{
	FAsdaLang::OnLanguageChanged().RemoveAll(this);   // ★ 10.10.2026: подписка «язык на лету»
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnConstellationResult.Remove(HResult);
	}
	ResetWidget();
	Super::Deinitialize();
}

// ★★★ К-3 / находка Р5 отчёта `14_soulmate_ревизия.md`: гейт «знак уже выбран» читает
//   `UAsdaNetSubsystem::SelfConstellation` — оно сбрасывается там же (`ResetSessionState`
//   сетевой подсистемы). Здесь снимаем СВОЁ состояние: открытое окно, выбор в списке и
//   защёлку «6586 отправлен, ждём 6587», которая иначе заблокировала бы выбор навсегда.
void UAsdaConstellationSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Защёлка ожидания 6587 и накопленные тела — при любой причине: соединение пересоздано.
	bAwaitingResult = false;
	PendingResults.Clear();
	PendingSign = -1;
	if (bOpen)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
			{
				WM->SetWindowVisible(kWindowId, false);
			}
		}
		bOpen = false;
		UE_LOG(LogAsdaConstellation, Log, TEXT("[знак зодиака] сброс сессии (%s): окно 74500 закрыто"),
			AsdaSessionResetReasonName(Reason));
	}
	Mouse.Reset();
}

void UAsdaConstellationSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.Constellation");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// Ответ 6587 может прийти и после закрытия окна — разбираем его ВСЕГДА.
	if (TArray<TArray<uint8>> Batch; PendingResults.Drain(Batch))   // ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива
	{
		for (const TArray<uint8>& B : Batch) { HandleResult(B); }
	}
	if (!bOpen || !bControlsReady) { return; }
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал. Своих защёлок у окна нет — только кадр мыши; раскрытый
	//   список комбобокса НЕ закрываем: у эталона захват модалки контролы ЗАМОРАЖИВАЕТ, а не
	//   сбрасывает их состояние.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		Mouse.Reset();
		return;
	}
	if (Combo) { Combo->Tick(Mouse); }
}

// ═════════════════════════════ ОКНО ═══════════════════════════════════════════════════════════

bool UAsdaConstellationSubsystem::EnsureWidget()
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
			UE_LOG(LogAsdaConstellation, Error,
				TEXT("[знак зодиака] WBP_Constellation_S_WIN не загрузился (%s)"), kWindowWbp);
		});
	// Скрытие окна (шаг каркаса): эталон: контрол 74500 в `.JUI` имеет Visible="0"
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaConstellationSubsystem::ResetWidget()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kWindowId);
		WM->CloseWindow(kWindowId);
	}
	if (Combo) { Combo->Reset(); }
	Widget = nullptr;
	Combo = nullptr;
	TxtHint = nullptr; TxtTitle = nullptr; TxtComboCaption = nullptr;
	BtnOk = nullptr; TxtBtnOk = nullptr;
	BtnCancel = nullptr; TxtBtnCancel = nullptr;
	BtnClose = nullptr;
	Relays.Reset();
	FramePlanes.Reset();
	bControlsReady = false;
	bOpen = false;
	Mouse.Reset();
}

void UAsdaConstellationSubsystem::RegisterZone()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Рамка окна — девять планов `Constellation_S_WIN_0_0…8` (скин `Black_BottomRound_White2Border`,
	//   Type = "ResizeWindow" из девяти кусков). Прямоугольник берём по ЖИВОЙ геометрии.
	// ★ Волна 3: зона по планам рамки — общая точка `FAsdaJuiWindowHandle::RegisterFrameZone` (`!bOpen || !Widget` → нет зоны).
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kWindowId, this, Widget, bOpen, TEXT("Constellation_S_WIN_0_%d"), kFramePlanes);
}

void UAsdaConstellationSubsystem::CollectByIdc(const FString& Idc, TArray<TObjectPtr<UWidget>>& Out) const
{
	// ★ R5-fix-b 13.11.2026: тело было дословной копией `AsdaUi::CollectByIdc` (третья копия группы Г1 R5-b) — зовём её.
	AsdaUi::CollectByIdc(Widget, Idc, Out);
}

void UAsdaConstellationSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }

	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == kIdcBtnOk)     { BtnOk     = Btn; continue; }
			if (N == kIdcBtnCancel) { BtnCancel = Btn; continue; }
			if (N == kIdcBtnClose)  { BtnClose  = Btn; continue; }
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == kIdcText)  { TxtHint  = Tb; continue; }
			if (N == kIdcTitle) { TxtTitle = Tb; continue; }
			if (N == FString(kIdcBtnOk)     + TEXT("_Text")) { TxtBtnOk     = Tb; continue; }
			if (N == FString(kIdcBtnCancel) + TEXT("_Text")) { TxtBtnCancel = Tb; continue; }
			if (N == FString(kIdcSel)       + TEXT("_Text")) { TxtComboCaption = Tb; continue; }
			continue;
		}
	}
	CollectByIdc(kIdcWin, FramePlanes);

	// ── Список знаков 74501: ОБЩАЯ точка `UAsdaJuiComboBox` (скин `Bk_ComboBox` + `DarkGrey_List`).
	//    Раскрываем ВНИЗ: контрол стоит у верхней кромки окна (Origin "31 18" внутри `Win_Tab`).
	Combo = NewObject<UAsdaJuiComboBox>(this);
	if (!Combo->Setup(Widget, kIdcSel, kWindowId, /*bOpenUp*/ false))
	{
		// Эталон в этом месте показывает `MessageBoxA(nullptr, "Point NULL", "Constellation_S_SEL", 0)`.
		UE_LOG(LogAsdaConstellation, Error,
			TEXT("[знак зодиака] контрол 74501 `Constellation_S_SEL` в дереве окна НЕ найден"));
	}
	else
	{
		Combo->SetItems(SignNames);
		Combo->OnPicked.BindWeakLambda(this, [this](int32 Idx)
		{
			UE_LOG(LogAsdaConstellation, Log, TEXT("[знак зодиака] выбран пункт %d («%s»)"),
				Idx, SignNames.IsValidIndex(Idx) ? *SignNames[Idx] : TEXT("?"));
		});
	}

	// ── Кнопки (1:1 ветки `uiUpgradeWndButtonHandler`) ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnOk,     kIdBtnOk,     [this](int32) { OnOkClicked(); });
	Bind(BtnCancel, kIdBtnCancel, [this](int32) { OnCancelClicked(); });
	Bind(BtnClose,  kIdBtnClose,  [this](int32) { OnCancelClicked(); });

	// ── Статичные подписи (эталон ставит их в `CConstellationSelWin_InitUI`) ──
	auto SetLoc = [this](UTextBlock* Tb, int32 LocId)
	{
		if (!Tb) { return; }
		const FString S = Loc(LocId);
		if (!S.IsEmpty()) { Tb->SetText(FText::FromString(S)); }
	};
	SetLoc(TxtHint,      ConsLoc::kHintOnce);    // 74502 ← 2583
	SetLoc(TxtTitle,     ConsLoc::kTitle);       // 74503 ← 2580
	SetLoc(TxtBtnOk,     ConsLoc::kBtnOk);       // 74504 ← 2423
	SetLoc(TxtBtnCancel, ConsLoc::kBtnCancel);   // 74505 ← 2424

	// ★ Тёмного текста в окне нет (все `EXS TextColor` = FFFFFFFF), обводку не снимаем:
	//   закон `ui-text-outline-readability` требует снимать её ТОЛЬКО у тёмных подписей.

	bControlsReady = (Combo != nullptr && Combo->IsReady());
	UE_LOG(LogAsdaConstellation, Log,
		TEXT("[знак зодиака] контролы разобраны: список=%s ОК=%s Отмена=%s ×=%s подсказка=%s заголовок=%s планов рамки=%d"),
		(Combo && Combo->IsReady()) ? TEXT("да") : TEXT("НЕТ"),
		BtnOk ? TEXT("да") : TEXT("НЕТ"), BtnCancel ? TEXT("да") : TEXT("НЕТ"),
		BtnClose ? TEXT("да") : TEXT("НЕТ"), TxtHint ? TEXT("да") : TEXT("НЕТ"),
		TxtTitle ? TEXT("да") : TEXT("НЕТ"), FramePlanes.Num());
}

// ═════════════════════════════ ДАННЫЕ ═════════════════════════════════════════════════════════

void UAsdaConstellationSubsystem::BuildSignList()
{
	SignIds.Reset();
	SignNames.Reset();
	// Порт цикла `for (i = 0; i < g_nConstellationCount; ++i) cfgFindRecordByKey65(ctx, i + 1)`:
	//   ключи записей `Constellation.BIN` идут подряд с единицы, поэтому берём строки таблицы
	//   по возрастанию ЧИСЛОВОГО имени.
	if (const UDataTable* Dt = AsdaDt::Cached(kSignTablePath))
	{
		TArray<int32> Keys;
		for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
		{
			const FString RowName = It.Key.ToString();
			if (RowName.IsNumeric()) { Keys.Add(FCString::Atoi(*RowName)); }
		}
		Keys.Sort();
		for (int32 Key : Keys)
		{
			const FAsdaTextRow* Row = AsdaDt::RowByInt<FAsdaTextRow>(kSignTablePath, Key, TEXT("Constellation"));
			if (!Row || Row->name.IsEmpty()) { continue; }
			SignIds.Add(Key);
			// ★ [ПОРТ] 09.10.2026 (мультиязычность): оверлей `Constellation`, ключ `<id>.name` — тот же, что у `FAsdaText::Get`.
			SignNames.Add(FAsdaLang::ResolveId(TEXT("Constellation"), Key, TEXT("name"), Row->name));
		}
	}
	if (SignIds.Num() == 0)
	{
		// Фолбэк — ровно тот же, что уже стоит на выборе знака при СОЗДАНИИ персонажа
		//   (`FAsdaCharRecord::ConstellationName`): 12 встроенных знаков.
		for (int32 Id = 1; Id <= 12; ++Id)
		{
			SignIds.Add(Id);
			SignNames.Add(FAsdaCharRecord::ConstellationName((uint8)Id));
		}
		UE_LOG(LogAsdaConstellation, Warning,
			TEXT("[знак зодиака] таблица %s пуста — список собран из встроенных 12 знаков"), kSignTablePath);
	}
}

// ═════════════════════════════ ТАБЛИЦА ВЛЕЧЕНИЯ ═══════════════════════════════════════════════

void UAsdaConstellationSubsystem::BuildMatchTable() const
{
	MatchRows.Reset();
	MatchBlocks.Reset();

	const UDataTable* Dt = AsdaDt::Cached(kMatchTablePath);
	if (!Dt)
	{
		// Ассета нет — данных нет. Ничего не выдумываем: `MatchIndex` вернёт −1, и окно просто
		//   не зажжёт ни одну из трёх картинок. Собрать таблицу — инструментом из подсказки ниже.
		UE_LOG(LogAsdaConstellation, Error,
			TEXT("[знак зодиака] таблицы влечения %s НЕТ — собери её ")
			TEXT("Tools/asda_build_constellation_match_dt.py из RawAssets/Data/Constellation_Match.BIN"),
			kMatchTablePath);
		return;
	}

	// Строки читаем сырым проходом (нужны ВСЕ 60, а не строка по ключу), поэтому сначала
	//   убеждаемся, что таблица собрана ИМЕННО этим RowStruct — иначе приведение указателя
	//   было бы чтением чужой памяти.
	if (Dt->GetRowStruct() != FAsdaConstellationMatchRow::StaticStruct())
	{
		UE_LOG(LogAsdaConstellation, Error,
			TEXT("[знак зодиака] %s собрана НЕ тем RowStruct (%s) — жду FAsdaConstellationMatchRow"),
			kMatchTablePath,
			Dt->GetRowStruct() ? *Dt->GetRowStruct()->GetName() : TEXT("нет"));
		return;
	}
	for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
	{
		if (It.Value)
		{
			MatchRows.Add(*reinterpret_cast<const FAsdaConstellationMatchRow*>(It.Value));
		}
	}
	// ★★★ ПОРЯДОК ФАЙЛА ВОССТАНАВЛИВАЕМ ЯВНО: строки `UDataTable` лежат в хеш-карте и приходят в
	//   произвольном порядке, а весь смысл 1:1-поиска эталона — «ПЕРВАЯ запись с этим знаком».
	MatchRows.Sort([](const FAsdaConstellationMatchRow& A, const FAsdaConstellationMatchRow& B)
	{ return A.Order < B.Order; });

	// Список недельных блоков — ИЗ ДАННЫХ (различные пары «месяц/день» в порядке файла).
	for (const FAsdaConstellationMatchRow& R : MatchRows)
	{
		const FIntPoint Block(R.Month, R.Day);
		MatchBlocks.AddUnique(Block);
	}

	UE_LOG(LogAsdaConstellation, Log,
		TEXT("[знак зодиака] таблица влечения загружена: записей %d, недельных блоков %d (первый %02d/%02d)"),
		MatchRows.Num(), MatchBlocks.Num(),
		MatchBlocks.Num() > 0 ? MatchBlocks[0].X : 0, MatchBlocks.Num() > 0 ? MatchBlocks[0].Y : 0);
}

void UAsdaConstellationSubsystem::EnsureMatchTable() const
{
	// Пока таблицы нет — пробуем снова: инструмент мог собрать ассет уже после старта игры,
	//   а `AsdaDt::Cached` держит СЛАБУЮ ссылку и подхватит пересборку следующим вызовом.
	if (MatchRows.Num() == 0) { BuildMatchTable(); }
}

int32 UAsdaConstellationSubsystem::CurrentBlockIndex() const
{
	// ★ ЭТО ИСПРАВЛЕНИЕ, А НЕ РЕВЕРС: оригинал даты НЕ читает вовсе (D5), поэтому «правильного»
	//   поведения тут не существует и сверять не с чем. Реконструкция опирается только на то, что
	//   ДОКАЗАНО дампом: блоки идут ровно с шагом 7 дней ⇒ таблица задумана как недельная ротация.
	//   Берём якорем ПЕРВЫЙ блок файла (в живых данных 10/07) текущего года и считаем, сколько
	//   недель прошло с него, по кругу. Год в данных отсутствует — поэтому только ротация.
	const int32 Blocks = MatchBlocks.Num();
	if (Blocks <= 1) { return 0; }

	const FDateTime Now = FDateTime::Now();
	const FIntPoint First = MatchBlocks[0];
	if (!FDateTime::Validate(Now.GetYear(), First.X, First.Y, 0, 0, 0, 0))
	{
		// Дата якоря в данных битая — молча остаёмся на первом блоке (он же поведение 1:1).
		UE_LOG(LogAsdaConstellation, Warning,
			TEXT("[знак зодиака] дата первого блока %02d/%02d невалидна — беру блок 0"), First.X, First.Y);
		return 0;
	}
	const FDateTime Anchor(Now.GetYear(), First.X, First.Y);
	const FDateTime Today(Now.GetYear(), Now.GetMonth(), Now.GetDay());
	const double DeltaDays = (Today - Anchor).GetTotalDays();
	const int32  Weeks     = FMath::FloorToInt(DeltaDays / 7.0);
	return ((Weeks % Blocks) + Blocks) % Blocks;   // остаток строго неотрицательный
}

const FAsdaConstellationMatchRow* UAsdaConstellationSubsystem::FindMatchRecord(uint8 MySign) const
{
	const int32 Key = (int32)MySign;

	// ── ВЕТКА 1:1 (по умолчанию) — порт `findRecord5ByFirstByte@0xF1A770` дословно:
	//    линейный проход шагом 5 байт, первая запись с байтом[0] == мой знак, дата НЕ читается.
	//    Именно из-за этого в оригинале работает только блок 10/07 (дефект D5).
	auto FirstBySign = [this, Key]() -> const FAsdaConstellationMatchRow*
	{
		for (const FAsdaConstellationMatchRow& R : MatchRows)
		{
			if (R.MySign == Key) { return &R; }
		}
		return nullptr;   // эталон здесь возвращает 0 ⇒ вызывающий отдаёт индекс 2 без текста
	};

	if (CVarUseDate.GetValueOnGameThread() == 0) { return FirstBySign(); }

	// ── ВЕТКА ИСПРАВЛЕННАЯ (`asda.Constellation.UseDate 1`) — учитываем недельный блок.
	const int32 Block = CurrentBlockIndex();
	if (MatchBlocks.IsValidIndex(Block))
	{
		const FIntPoint Want = MatchBlocks[Block];
		for (const FAsdaConstellationMatchRow& R : MatchRows)
		{
			if (R.MySign == Key && R.Month == Want.X && R.Day == Want.Y) { return &R; }
		}
	}
	// В нужном блоке записи с таким знаком нет (в живых данных так не бывает — каждый блок полон).
	//   Откатываемся на поведение оригинала, а не выдумываем замену.
	return FirstBySign();
}

// ═════════════════════════════ КОНТРАКТ ДЛЯ ОКОН ══════════════════════════════════════════════

FString UAsdaConstellationSubsystem::SignName(uint8 SignId) const
{
	// Порт `cfgFindRecordByKey65@0xF1A670`: поиск по ключу записи `Constellation.BIN`.
	//   Ключ 0 в таблице отсутствует (ключи 1…12), поэтому «знак не выбран» → пустая строка.
	if (SignId == 0) { return FString(); }
	const int32 Idx = SignIds.IndexOfByKey((int32)SignId);
	return SignNames.IsValidIndex(Idx) ? SignNames[Idx] : FString();
}

// ★ Удержание DT_Constellation на сессию (15.09.2026, dt_gc_hold, хвост ревизии п.2) — обоснование в объявлении.
//   Загрузка — общая точка `AsdaDt::Cached` внутри `FAsdaHeldTables::Hold`; путь — `kSignTablePath` (второй копии нет).
int32 UAsdaConstellationSubsystem::HoldGameTables() const
{
	static const FName Group(TEXT("Constellation"));
	const TCHAR* const Paths[] = { kSignTablePath };
	return HeldTables.Hold(Group, Paths);
}

uint8 UAsdaConstellationSubsystem::SelfSign() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return 0; }
	// 0xFF — наша пометка «сервер знак ещё не присылал» (в эталоне поле просто нулевое до 1017).
	//   Наружу отдаём 0: для таблицы влечения это то же самое — записи с ключом 0 нет.
	return (Net->SelfConstellation == 0xFF) ? (uint8)0 : Net->SelfConstellation;
}

int32 UAsdaConstellationSubsystem::MatchIndex(uint8 MySign, uint8 OtherSign, FString& OutText) const
{
	OutText.Reset();
	EnsureMatchTable();

	// ★ СОСТОЯНИЕ ПОРТА, КОТОРОГО В ЭТАЛОНЕ НЕТ: таблица не собрана. В эталоне массив всегда есть
	//   (файл читается на старте), поэтому такого исхода там не бывает — отдаём −1, чтобы окно
	//   могло НЕ зажигать ни одну картинку, а не показывать ложное «Обычное».
	if (MatchRows.Num() == 0) { return -1; }

	const FAsdaConstellationMatchRow* Rec = FindMatchRecord(MySign);

	// ★ ДЕФЕКТ ОРИГИНАЛА D7 — ВОСПРОИЗВЕДЁН 1:1. `if (!rec) return 2;` — эталон отдаёт индекс
	//   «Обычное» (окно зажигает img[2]), но в буфер НИЧЕГО не пишет, и подпись остаётся ПУСТОЙ.
	//   Сюда же попадает свой знак 0 («не выбран»): записи с ключом 0 в таблице нет.
	if (!Rec) { return 2; }

	// Дальше — тело `uiFormatPartyRelationNotice@0xF1A520` дословно, три исхода.
	if (Rec->BestSign == (int32)OtherSign)
	{
		OutText = Loc(ConsLoc::kAttractBest);      // 110632
		return 0;
	}
	if (Rec->StrongSign == (int32)OtherSign)
	{
		OutText = Loc(ConsLoc::kAttractStrong);    // 110633
		return 1;
	}
	OutText = Loc(ConsLoc::kAttractNormal);        // 110634
	return 2;
}

// ═════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ════════════════════════════════════════════

bool UAsdaConstellationSubsystem::OpenWindow()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;

	// ★ ГЕЙТ 1:1 (`movzx eax, byte ptr [eax+0x460]; test eax,eax; jle …` @0xECBEE6):
	//   знак уже стоит (значение > 0) ⇒ нотис 2581 и окно НЕ открывается.
	//   Наше зеркало поля `CCharacter+0x460` — `UAsdaNetSubsystem::SelfConstellation`
	//   (0xFF = «сервер ещё не присылал», такое значение гейтом не считаем).
	const int32 Cur = Net ? (int32)Net->SelfConstellation : 0;
	if (Net && Net->SelfConstellation != 0xFF && Cur > 0)
	{
		NoticeMsgBoxLoc(ConsLoc::kAlreadySet);
		UE_LOG(LogAsdaConstellation, Log,
			TEXT("[знак зодиака] окно не открыто: знак уже установлен (%d) — нотис 2581"), Cur);
		return false;
	}

	if (!EnsureWidget()) { return false; }

	PendingSign = -1;                       // эталон: `[this+0x20] = -1`
	bAwaitingResult = false;
	if (Combo)
	{
		// D1: эталон здесь зовёт `SetSelectedItem(list, 0)` — предвыбор Овна. По умолчанию НЕ делаем.
		const bool bPreselect = CVarPreselectFirst.GetValueOnGameThread() != 0;
		Combo->SetOpen(false);
		Combo->SetSelectedIndex(bPreselect ? 0 : INDEX_NONE);
	}
	// Эталон: `cControlSetCaption(list, loc 110801)` — заголовок-заглушка ПОВЕРХ подписи выбора.
	//   При предвыборе (режим 1:1) он и создаёт дефект D1: выбор есть, а видно заглушку.
	if (TxtComboCaption)
	{
		TxtComboCaption->SetText(FText::FromString(Loc(ConsLoc::kListCaption)));
	}

	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWindowId, true);   // `CControl_SetVisible(win, 1)`
		WM->BringWindowToFront(kWindowId);       // `CClass_MapInsert(g_pWindowMgr, win)`
	}
	bOpen = true;
	Mouse.Reset();
	UE_LOG(LogAsdaConstellation, Log, TEXT("[знак зодиака] окно 74500 открыто, знаков в списке %d"),
		SignNames.Num());
	return true;
}

void UAsdaConstellationSubsystem::CloseWindow()
{
	if (Combo) { Combo->SetOpen(false); }
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->SetWindowVisible(kWindowId, false);
		}
	}
	bOpen = false;
}

// ═════════════════════════════ ДЕЙСТВИЯ ═══════════════════════════════════════════════════════

int32 UAsdaConstellationSubsystem::GetSelectedSign() const
{
	if (!Combo) { return 0; }
	const int32 Idx = Combo->GetSelectedIndex();
	return SignIds.IsValidIndex(Idx) ? SignIds[Idx] : 0;
}

void UAsdaConstellationSubsystem::SelectIndex(int32 Index)
{
	if (Combo) { Combo->SetSelectedIndex(Index); }
}

void UAsdaConstellationSubsystem::OnOkClicked()
{
	// ★ ПОРЯДОК 1:1 (дизасм 0xECC013…0xECC063), он же ДЕФЕКТ ОРИГИНАЛА D2:
	//   сперва окно ПРЯЧЕТСЯ, и только потом читается выбор.
	CloseWindow();

	const int32 Idx = Combo ? Combo->GetSelectedIndex() : INDEX_NONE;
	PendingSign = Idx + 1;                       // эталон: `[this+0x20] = getSelected() + 1`
	if (PendingSign < 1)
	{
		NoticeMsgBoxLoc(ConsLoc::kNotPicked);    // нотис 2582, пакет НЕ уходит
		UE_LOG(LogAsdaConstellation, Log, TEXT("[знак зодиака] ОК без выбора — нотис 2582"));
		return;
	}
	// Эталон шлёт ИМЕННО `индекс + 1`. У `Constellation.BIN` ключи идут подряд с единицы, поэтому
	//   это тот же ключ записи; если бы таблица была разрежена, эталон всё равно слал бы индекс+1 —
	//   воспроизводим его число, а не ключ (`SignIds` используем только для подписи в логе).
	const uint8 Sign = (uint8)FMath::Clamp(PendingSign, 0, 255);
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		bAwaitingResult = Net->SendConstellationSelect6586(Sign);
	}
	UE_LOG(LogAsdaConstellation, Log, TEXT("[знак зодиака] ОК: отправлен знак %d («%s»)"),
		(int32)Sign, SignNames.IsValidIndex(Idx) ? *SignNames[Idx] : TEXT("?"));
}

void UAsdaConstellationSubsystem::OnCancelClicked()
{
	// 74505 и 74509 в эталоне ведут в ОДНУ ветку `sub_91FE14` — просто спрятать окно.
	CloseWindow();
	UE_LOG(LogAsdaConstellation, Log, TEXT("[знак зодиака] окно закрыто без отправки"));
}

// ═════════════════════════════ ПРИЁМ 6587 ═════════════════════════════════════════════════════

void UAsdaConstellationSubsystem::HandleResult(const TArray<uint8>& Body)
{
	if (Body.Num() < 6) { return; }
	const int32 Code = (int32)Body[0];
	const int32 Sign = (int32)Body[5];
	bAwaitingResult = false;

	UGameInstance* GI = GetGameInstance();
	switch (Code)
	{
	case 1:
	{
		// Эталон: `localPlayer->[0x460] = body[5]` (это делает `UAsdaNetSubsystem` при приёме),
		//   затем `UI_FillEquipDetailPanel(&g_pCharInfoWnd)` — перерисовка окна характеристик 900,
		//   затем нотис 2584 с именем знака в слоте 0 (`CObject_SetSlotString(ctx, 0, имя)`).
		if (UAsdaCharaInfoSubsystem* Info = GI ? GI->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr)
		{
			Info->RefreshAll();
		}
		// Имя знака — ИЗ ТАБЛИЦЫ `Constellation.BIN` (тот же путь, что у списка окна).
		//   Встроенные 12 имён остаются лишь запасным вариантом, если таблицы нет вовсе.
		HoldGameTables();   // ★ 15.09 (dt_gc_hold, хвост п.2): DT_Constellation — сильно у подсистемы на сессию (запас ниже идёт через FAsdaText)
		FString Name = SignName((uint8)Sign);
		if (Name.IsEmpty()) { Name = FAsdaCharRecord::ConstellationName((uint8)Sign); }
		FString Text;
		if (UAsdaTextSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
		{
			Text = TS->FormatById(ConsLoc::kAssigned, { Name }, {});
		}
		if (Text.IsEmpty()) { Text = Loc(ConsLoc::kAssigned).Replace(TEXT("[ss1]"), *Name); }
		NoticeMsgBox(Text);
		UE_LOG(LogAsdaConstellation, Log, TEXT("[знак зодиака] 6587 код 1: знак %d («%s») установлен"),
			Sign, *Name);
		break;
	}
	case 2:
		// Сервер: знак уже стоял (`Recv_UserStarPointUpdate`, ветка «поле не ноль»).
		NoticeMsgBoxLoc(ConsLoc::kAlreadySet);
		UE_LOG(LogAsdaConstellation, Log, TEXT("[знак зодиака] 6587 код 2: знак уже установлен — нотис 2581"));
		break;
	case 98:   // ★ ДЕФЕКТ ОРИГИНАЛА D4: этот код НЕ шлёт ни один отправитель GameServer.
	case 99:   // `Recv_DBStarPointUpdate DB Error`
		NoticeMsgBoxLoc(ConsLoc::kServerError);
		UE_LOG(LogAsdaConstellation, Warning, TEXT("[знак зодиака] 6587 код %d: ошибка — нотис 117"), Code);
		break;
	default:
		// 1:1 эталон: `default: return;` — прочие коды молча игнорируются.
		UE_LOG(LogAsdaConstellation, Verbose, TEXT("[знак зодиака] 6587 код %d: эталон его игнорирует"), Code);
		break;
	}
}

// ═════════════════════════════ НОТИСЫ ═════════════════════════════════════════════════════════

FString UAsdaConstellationSubsystem::Loc(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

void UAsdaConstellationSubsystem::NoticeMsgBox(const FString& Text) const
{
	// Эталон: `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст)` = `ONEBUT_MSGBOX` 1500.
	// ★ Волна 1 (14.09.2026, В2): единый маршрут `AsdaNotice::MsgBox` — окно 1500, без оконной подсистемы —
	//   строка в ленту той же категорией. Категория/подпись кнопки — прежние.
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::Social, Text, ConsLoc::kMsgBoxOk);
}

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════

FString UAsdaConstellationSubsystem::DumpControls() const
{
	FString S = TEXT("=== окно знака зодиака 74500: разобранные контролы ===\n");
	auto Line = [&S](const TCHAR* Id, const UObject* W)
	{ S += FString::Printf(TEXT("  %-44s %s\n"), Id, W ? TEXT("есть") : TEXT("НЕТ")); };
	Line(TEXT("74500 Constellation_S_WIN (окно)"),      Widget);
	Line(TEXT("74501 Constellation_S_SEL (список)"),    Combo);
	Line(TEXT("74501 Constellation_S_SEL_Text"),        TxtComboCaption);
	Line(TEXT("74502 Constellation_S_TEXT (2583)"),     TxtHint);
	Line(TEXT("74503 Constellation_S_TEXTMOVE (2580)"), TxtTitle);
	Line(TEXT("74504 Constellation_S_OK_btn"),          BtnOk);
	Line(TEXT("74504 подпись (2423)"),                  TxtBtnOk);
	Line(TEXT("74505 Constellation_S_XXbtn"),           BtnCancel);
	Line(TEXT("74505 подпись (2424)"),                  TxtBtnCancel);
	Line(TEXT("74509 Constellation_S_Xbtn"),            BtnClose);
	S += FString::Printf(TEXT("  планов рамки (из 9): %d\n"), FramePlanes.Num());
	return S;
}

FString UAsdaConstellationSubsystem::DumpState() const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	FString S = TEXT("=== окно знака зодиака 74500: состояние ===\n");
	S += FString::Printf(TEXT("  открыто=%s готово=%s ждём 6587=%s предвыбор(D1)=%s\n"),
		bOpen ? TEXT("да") : TEXT("нет"), bControlsReady ? TEXT("да") : TEXT("нет"),
		bAwaitingResult ? TEXT("да") : TEXT("нет"),
		CVarPreselectFirst.GetValueOnGameThread() ? TEXT("1:1 оригинал") : TEXT("исправлено"));
	S += FString::Printf(TEXT("  свой знак (char+1120) = %d%s\n"),
		Net ? (int32)Net->SelfConstellation : -1,
		(Net && Net->SelfConstellation == 0xFF) ? TEXT(" (сервер ещё не присылал)") : TEXT(""));
	S += FString::Printf(TEXT("  выбор в списке = %d, знак = %d, PendingSign = %d\n"),
		Combo ? Combo->GetSelectedIndex() : INDEX_NONE, GetSelectedSign(), PendingSign);
	S += FString::Printf(TEXT("  знаков в списке: %d\n"), SignNames.Num());
	for (int32 i = 0; i < SignNames.Num(); ++i)
	{
		S += FString::Printf(TEXT("    [%2d] ключ=%d  «%s»\n"), i,
			SignIds.IsValidIndex(i) ? SignIds[i] : -1, *SignNames[i]);
	}
	return S;
}

FString UAsdaConstellationSubsystem::DumpMatchMatrix() const
{
	EnsureMatchTable();

	FString S = TEXT("=== ВЛЕЧЕНИЕ: матрица «мой знак × чужой знак» ===\n");
	S += FString::Printf(TEXT("  таблица: %s\n"), kMatchTablePath);
	if (MatchRows.Num() == 0)
	{
		S += TEXT("  ★ ЗАПИСЕЙ НЕТ. Собери таблицу: python Tools/run_headless_safe.py ")
			 TEXT("Tools/asda_build_constellation_match_dt.py --idle 200\n");
		return S;
	}

	const bool bUseDate = CVarUseDate.GetValueOnGameThread() != 0;
	const int32 Block = bUseDate ? CurrentBlockIndex() : 0;
	const FIntPoint BlockDate = MatchBlocks.IsValidIndex(Block) ? MatchBlocks[Block] : FIntPoint(0, 0);
	S += FString::Printf(
		TEXT("  записей %d, недельных блоков %d; режим: %s, действующий блок %02d/%02d\n"),
		MatchRows.Num(), MatchBlocks.Num(),
		bUseDate ? TEXT("asda.Constellation.UseDate 1 — ИСПРАВЛЕНО (учёт даты)")
				 : TEXT("asda.Constellation.UseDate 0 — 1:1 оригинал (дефект D5: всегда первый блок)"),
		BlockDate.X, BlockDate.Y);
	S += FString::Printf(TEXT("  мой знак сейчас = %d («%s»)\n"),
		(int32)SelfSign(), *SignName(SelfSign()));
	S += TEXT("  индексы: 0 = максимальное (110632), 1 = сильное (110633), 2 = обычное (110634)\n");
	S += TEXT("  ⚠ матрица НЕсимметрична по замыслу данных: влечение считается ОТ моего знака.\n\n");

	// Шапка: чужие знаки столбцами (номера 1…12, имена — ниже в легенде, иначе строка не влезает).
	S += TEXT("  мой \\ чужой |");
	for (int32 Id : SignIds) { S += FString::Printf(TEXT("%3d"), Id); }
	S += TEXT("   | максимальное / сильное\n");
	S += TEXT("  ------------+");
	for (int32 i = 0; i < SignIds.Num(); ++i) { S += TEXT("---"); }
	S += TEXT("---+------------------------\n");

	for (int32 MyId : SignIds)
	{
		S += FString::Printf(TEXT("  %-11s |"), *SignName((uint8)MyId));
		for (int32 OtherId : SignIds)
		{
			FString Text;
			const int32 Idx = MatchIndex((uint8)MyId, (uint8)OtherId, Text);
			S += (Idx < 0) ? FString(TEXT("  -")) : FString::Printf(TEXT("%3d"), Idx);
		}
		const FAsdaConstellationMatchRow* Rec = FindMatchRecord((uint8)MyId);
		S += FString::Printf(TEXT("   | %s / %s%s\n"),
			Rec ? *SignName((uint8)Rec->BestSign)   : TEXT("—"),
			Rec ? *SignName((uint8)Rec->StrongSign) : TEXT("—"),
			(Rec && Rec->SelfMatch) ? TEXT("   ← самосовпадение (дефект данных D9, НЕ чиним)") : TEXT(""));
	}

	// Тексты нотисов — целиком, чтобы дамп проверялся без похода в таблицу строк.
	S += TEXT("\n  тексты:\n");
	S += FString::Printf(TEXT("    0 → 110632 «%s»\n"), *Loc(ConsLoc::kAttractBest));
	S += FString::Printf(TEXT("    1 → 110633 «%s»\n"), *Loc(ConsLoc::kAttractStrong));
	S += FString::Printf(TEXT("    2 → 110634 «%s»\n"), *Loc(ConsLoc::kAttractNormal));

	// ★ ДЕФЕКТ D7 — показываем его прямо в дампе, чтобы его нельзя было принять за баг порта.
	{
		FString Text;
		const int32 Idx = MatchIndex(0, SignIds.Num() > 0 ? (uint8)SignIds[0] : 1, Text);
		S += FString::Printf(
			TEXT("\n  ★ дефект D7 (свой знак 0): индекс=%d, текст=«%s» — картинка «Обычное» горит, ")
			TEXT("а подпись пуста; воспроизведено 1:1\n"), Idx, *Text);
	}
	return S;
}

// ═════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ (живая проверка) ════════════════════════════

namespace
{
	UAsdaConstellationSubsystem* ConsSub(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaConstellationSubsystem>() : nullptr;
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GConsOpen(
	TEXT("asda.Constellation.Open"),
	TEXT("asda.Constellation.Open — открыть окно 74500 так же, как код диалога NPC 30073"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaConstellationSubsystem* S = ConsSub(W);
		if (!S) { UE_LOG(LogAsdaConstellation, Warning, TEXT("подсистема недоступна")); return; }
		S->OpenWindow();
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GConsClose(
	TEXT("asda.Constellation.Close"),
	TEXT("asda.Constellation.Close — спрятать окно 74500 (ветка 74505/74509 эталона)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaConstellationSubsystem* S = ConsSub(W)) { S->OnCancelClicked(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GConsPick(
	TEXT("asda.Constellation.Pick"),
	TEXT("asda.Constellation.Pick <индекс 0…11> — выбрать пункт списка тем же путём, что и клик мышью"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaConstellationSubsystem* S = ConsSub(W);
		if (!S) { UE_LOG(LogAsdaConstellation, Warning, TEXT("подсистема недоступна")); return; }
		S->SelectIndex(A.Num() > 0 ? FCString::Atoi(*A[0]) : 0);
		UE_LOG(LogAsdaConstellation, Log, TEXT("выбран знак %d"), S->GetSelectedSign());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GConsOk(
	TEXT("asda.Constellation.Ok"),
	TEXT("asda.Constellation.Ok [online] — нажать 74504 «ОК» (окно прячется, уходит пакет 6586). На живом сервере — только с `online`"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1, редакция 1 (СРЕДН-4): НЕОБРАТИМЫЙ выбор знака (6586) без открытого окна, по выбору, оставшемуся от
		//   прошлого открытия, — онлайн только с явным `online`.
		if (FAsdaOutgoingGate::RefuseRiskyWithoutOnlineOptIn(W, TEXT("asda.Constellation.Ok"), A, TEXT("необратимый выбор знака зодиака 6586 без открытого окна"))) { return; }
		if (UAsdaConstellationSubsystem* S = ConsSub(W)) { S->OnOkClicked(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GConsDump(
	TEXT("asda.Constellation.Dump"),
	TEXT("asda.Constellation.Dump — дамп КАЖДОГО контрола окна 74500 и всего состояния"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaConstellationSubsystem* S = ConsSub(W);
		if (!S) { UE_LOG(LogAsdaConstellation, Warning, TEXT("подсистема недоступна")); return; }
		UE_LOG(LogAsdaConstellation, Log, TEXT("\n%s\n%s"), *S->DumpControls(), *S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

// ★ САМОПРОВЕРКА ВЛЕЧЕНИЯ ПУТЁМ ИГРОКА: печатает матрицу 12×12 (мой знак × чужой → индекс и текст),
//   действующий недельный блок и оба дефекта (D7 и пометки D9). Работает и без открытого окна.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GConsMatchDump(
	TEXT("asda.Constellation.MatchDump"),
	TEXT("asda.Constellation.MatchDump — матрица влечения 12×12 (индекс 0/1/2 + тексты 110632/3/4).\n")
	TEXT("Режим переключается CVar asda.Constellation.UseDate (0 = 1:1 оригинал, 1 = с учётом даты)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaConstellationSubsystem* S = ConsSub(W);
		if (!S) { UE_LOG(LogAsdaConstellation, Warning, TEXT("подсистема недоступна")); return; }
		UE_LOG(LogAsdaConstellation, Log, TEXT("\n%s"), *S->DumpMatchMatrix());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★ ★ R5-fix-b 13.11.2026 (второй заход) САМОТЕСТ «ПЛАНЫ РАМКИ ОКНА 74500» (К1 R5-fix-b: тело `CollectByIdc` созвездия заменено общей точкой
//   `AsdaUi::CollectByIdc`, своего самотеста у окна не было). Без сети: окно открывается штатной `OpenWindow` (тот же путь, что
//   код диалога NPC 30073 и `asda.Constellation.Open`; офлайн знак не стоит — гейт 2581 не срабатывает), затем:
//     F1 `FramePlanes` (собирает `SetupControls` → `CollectByIdc("Constellation_S_WIN")`) = НЕЗАВИСИМЫЙ перебор дерева окна по
//        образцу имён `Constellation_S_WIN`, `…_grp`, `…_Text`, `…_<число>_<число>` (то же множество, без общей точки);
//     F2 все 9 планов рамки `Constellation_S_WIN_0_0…8` (зона перетаскивания `RegisterFrameZone`) — в наборе.
//   Окно закрывается; если было открыто игроком — отказ. Итог `САМОТЕСТ-СОЗВЕЗДИЕ-РАМКА ИТОГ: ok=N fail=M`.
static FAutoConsoleCommandWithWorldAndArgs GConsSelfTestFrame(
	TEXT("asda.Constellation.SelfTestFramePlanes"),
	TEXT("asda.Constellation.SelfTestFramePlanes [quit] — окно знака зодиака 74500 офлайн: набор планов рамки (CollectByIdc) "
	     "= независимый перебор дерева, все 9 планов рамки на месте"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		static const TCHAR* kCmd = TEXT("asda.Constellation.SelfTestFramePlanes");
		static const TCHAR* kWin = TEXT("Constellation_S_WIN");   // IDC окна (`kIdcWin`)
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args, 20.f)) { return; }
		UAsdaConstellationSubsystem* S = ConsSub(W);
		if (!S) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет подсистемы окна знака зодиака")); return; }
		if (S->IsOpen()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("окно 74500 открыто игроком")); return; }
		int32 Ok = 0, Fail = 0;   // счёт проверок
		auto Check = [&Ok, &Fail](bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaConstellation, Display, TEXT("САМОТЕСТ-СОЗВЕЗДИЕ-РАМКА %s %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
		};
		const bool bOpened = S->OpenWindow();
		UUserWidget* Win = S->DebugWidget();
		Check(bOpened && S->IsOpen() && Win && Win->WidgetTree, FString::Printf(TEXT("F0 окно 74500 открыто штатной OpenWindow (%d), дерево есть"), bOpened ? 1 : 0));
		if (Win && Win->WidgetTree)
		{
			// Независимый перебор: имя = IDC, IDC_grp, IDC_Text или IDC_<цифры>_<цифры> (без ParsePlaneName/CollectByIdc).
			auto Digits = [](const FString& X) { if (X.IsEmpty()) { return false; } for (TCHAR C : X) { if (!FChar::IsDigit(C)) { return false; } } return true; };
			TSet<const UWidget*> Want;
			TArray<UWidget*> All;
			Win->WidgetTree->GetAllWidgets(All);
			const FString Pre = FString(kWin) + TEXT("_");
			for (UWidget* Wd : All)
			{
				if (!Wd) { continue; }
				const FString N = Wd->GetName();
				bool bIn = N == kWin || N == Pre + TEXT("grp") || N == Pre + TEXT("Text");
				if (!bIn && N.StartsWith(Pre))
				{
					FString L, R;
					bIn = N.Mid(Pre.Len()).Split(TEXT("_"), &L, &R) && Digits(L) && Digits(R);
				}
				if (bIn) { Want.Add(Wd); }
			}
			const TArray<TObjectPtr<UWidget>>& Got = S->DebugFramePlanes();
			TSet<const UWidget*> GotSet;
			for (const TObjectPtr<UWidget>& G : Got) { GotSet.Add(G.Get()); }
			Check(GotSet.Num() == Got.Num() && GotSet.Num() == Want.Num() && GotSet.Includes(Want),
				FString::Printf(TEXT("F1 FramePlanes (%d узлов) = независимый перебор дерева (%d узлов)"), Got.Num(), Want.Num()));
			int32 Frame = 0;
			for (int32 I = 0; I < 9; ++I)
			{
				const FString N = FString::Printf(TEXT("%s_0_%d"), kWin, I);
				for (const UWidget* G : GotSet) { if (G && G->GetName() == N) { ++Frame; break; } }
			}
			Check(Frame == 9, FString::Printf(TEXT("F2 все 9 планов рамки %s_0_0…8 в наборе (%d)"), kWin, Frame));
		}
		S->CloseWindow();
		Check(!S->IsOpen(), TEXT("F3 окно закрыто"));
		AsdaSelfTestKit::Finish(W, kCmd, Args.bQuit, LogAsdaConstellation, TEXT("САМОТЕСТ-СОЗВЕЗДИЕ-РАМКА"), Ok, Fail);
	}));
#endif // ASDA_DEV_CONSOLE
