// Copyright (c) 2026 @sitxovski (Discord)
// Реализация предмета «Смена имени персонажа» (подтип 111): вопрос 65, окно 7900, 6138/6139, вопросы 66/67, 6140/6141.
//   Полный разбор оригинала и отличия порта — в шапке Public/UI/AsdaCharRenameSubsystem.h.
#include "UI/AsdaCharRenameSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"                  // ASDA_DEV_CVAR — дев-ключи вне Shipping

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Net/NetOpcodes.h"
#include "Login/AsdaLoginData.h"             // IsValidCharNameByLocale, WireTextByteLen, DecodeCp1251
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaJuiWindowHandle.h"          // каркас окна JUI (открыть WBP, привязать, скрыть)
#include "UI/AsdaInventorySubsystem.h"       // снятие предмета после успеха
#include "UI/AsdaChatWindowSubsystem.h"      // фильтр брани `Chat_MatchBannedWord`
#include "UI/AsdaSystemOptionSubsystem.h"    // возврат к выбору персонажа (`gameLeaveToCharSelect`)
#include "UI/AsdaEnterGuard.h"               // вопрос о необратимом — без Enter
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiHelpers.h"                // BindButton
#include "World/AsdaWorldSubsystem.h"        // своё имя
#include "Login/AsdaButtonRelay.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Kismet/KismetSystemLibrary.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaRename, Log, All);

static ASDA_DEV_CVAR(int32, CVarRenameFlow,
	TEXT("asda.Rename.Flow"), 1,
	TEXT("1 — предмет смены имени (подтип 111) как в оригинале: вопрос 65, окно 7900, 6138/6140; ")
	TEXT("0 — прежнее поведение порта: 5450 (сервер списывал предмет, имя не менялось)."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarRenameNameRule,
	TEXT("asda.Rename.NameRule"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — новое имя проверяется правилом создания персонажа (≤ 18 символов, ≤ 19 байт, без апострофа, ")
	TEXT("символы по asda.Text.NameLocale) → окно 9583; 0 — как оригинал: только совпадение со своим и брань."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarRenameAfterSuccess,
	TEXT("asda.Rename.AfterSuccess"), 1,
	TEXT("[ПОРТ] После успешной смены имени (6141 код 1): 1 — возврат к выбору персонажа (список с новым именем); ")
	TEXT("2 — закрыть клиент, как оригинал (g_GameState = 0); 0 — остаться в мире."),
	ECVF_Default);

const FName kRenameWindowId(TEXT("CharNameChange_Win"));

namespace
{
	const TCHAR* kWbpPath = TEXT("/Game/AsdaAssets/UI/Player_UI/Character/WBP_CharNameChange_Win");
	// z окна: `AlwaysTop = "1"` — поверх сумки, из которой предмет применён (полоса окон-вопросов, как 16500).
	constexpr int32 kWinZ = 8850;
	// Длины тел (сервер: `sub_4C28F0(…, 6139, 5, …)` у `sub_55DCB0`, `(…, 6141, 86, …)` у `RecvClient_CharNameChange`/`sub_55DD50`).
	constexpr int32 kLen6139 = 5;
	constexpr int32 kLen6141 = 86;
	// Цвета 109405 (`case 0x42`, :380153-380154): −11495681 и −380416.
	constexpr uint32 kOldNameColor = 0xFF5096FFu;
	constexpr uint32 kNewNameColor = 0xFFFA3200u;

	// sprintf оригинала для строк потока: `%08x` — цвет, `%s`/`%S` — строка, `%d` — число (по порядку аргументов).
	FString FormatC(const FString& Fmt, const TArray<FString>& Args)
	{
		FString Out; Out.Reserve(Fmt.Len() + 64);
		int32 A = 0;
		for (int32 i = 0; i < Fmt.Len(); ++i)
		{
			if (Fmt[i] != TEXT('%') || i + 1 >= Fmt.Len()) { Out.AppendChar(Fmt[i]); continue; }
			if (Fmt[i + 1] == TEXT('%')) { Out.AppendChar(TEXT('%')); ++i; continue; }
			int32 j = i + 1;
			while (j < Fmt.Len() && FChar::IsDigit(Fmt[j])) { ++j; }   // ширина `08`
			if (j < Fmt.Len() && (Fmt[j] == TEXT('s') || Fmt[j] == TEXT('S') || Fmt[j] == TEXT('d') || Fmt[j] == TEXT('x') || Fmt[j] == TEXT('X')))
			{
				Out += Args.IsValidIndex(A) ? Args[A] : FString();
				++A; i = j; continue;
			}
			Out.AppendChar(Fmt[i]);
		}
		return Out;
	}
	FString Hex8(uint32 C) { return FString::Printf(TEXT("%08x"), C); }
}

bool UAsdaCharRenameSubsystem::IsFlowOn()
{
	return CVarRenameFlow.GetValueOnGameThread() != 0;
}

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaCharRenameSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только кладём тело в очередь.
		HWave = Net->OnWave2DPacket.AddLambda([this](int32 Op, const TArray<uint8>& B)
		{
			if (Op == OP_ITEM_TOOLTIP_BUILD_OR_NOTICE || Op == OP_RECV_ITEM_ACTION_RESULT_SHOW_NOTICE)
			{
				Pending.Push(TPair<int32, TArray<uint8>>(Op, B));
			}
		});
	}
}

void UAsdaCharRenameSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr) { Net->OnWave2DPacket.Remove(HWave); }
	Relays.Reset();
	Widget = nullptr;
	Super::Deinitialize();
}

void UAsdaCharRenameSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// [ПОРТ] Оригинал при разрыве закрывает клиент; у порта запрос прежней сессии/карты не должен уйти (ячейка могла измениться).
	if (IsWindowShown()) { HideWindow(); }
	PendingContainer = -1; PendingCell = -1; PendingCode = 0;
	PendingName.Reset();
	bLeavePending = false;
	Pending.Clear();
	UE_LOG(LogAsdaRename, Verbose, TEXT("[смена имени] сброс сессии (%s)"), AsdaSessionResetReasonName(Reason));
}

void UAsdaCharRenameSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.CharRename");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	TArray<TPair<int32, TArray<uint8>>> Batch;
	if (Pending.Drain(Batch))
	{
		for (const TPair<int32, TArray<uint8>>& P : Batch)
		{
			if (P.Key == OP_ITEM_TOOLTIP_BUILD_OR_NOTICE) { OnCheckResult(P.Value); }
			else if (P.Key == OP_RECV_ITEM_ACTION_RESULT_SHOW_NOTICE) { OnChangeResult(P.Value); }
		}
	}
	if (bLeavePending)
	{
		bLeavePending = false;
		LeaveAfterSuccess();
	}
}

// ═════════════════════════════ ОКНО 7900 ═══════════════════════════════════════════════════════

bool UAsdaCharRenameSubsystem::EnsureWidget()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kRenameWindowId, kWbpPath, kWinZ).Ensure(GetGameInstance(), Widget, bReady,
		[this](UAsdaWindowSubsystem& WM)
		{
			WM.CloseWindow(kRenameWindowId);
			Widget = nullptr; bReady = false;
		},
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupControls();
			WM.SetWindowPriority(kRenameWindowId, true);   // `AlwaysTop = "1"`
		},
		[]()
		{
			UE_LOG(LogAsdaRename, Error, TEXT("[смена имени] WBP_CharNameChange_Win не загрузился (%s)"), kWbpPath);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bReady;
}

void UAsdaCharRenameSubsystem::SetupControls()
{
	if (!Widget || !Widget->WidgetTree) { return; }
	Relays.Reset();
	BtnCheck = nullptr; BtnClose = nullptr; EdtInput = nullptr; EdtInputBox = nullptr;
	UTextBlock* TxtTitle = nullptr;    // 7901 `CharNameChange_Move`
	UTextBlock* TxtPrompt = nullptr;   // 7903 `CharNameChange_Text`
	UTextBlock* CapCheck = nullptr;    // подпись 7905
	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		const FString N = W->GetName();
		if (UButton* B = Cast<UButton>(W))
		{
			if (N == TEXT("CharNameChange_ChkBtn")) { BtnCheck = B; }
			else if (N == TEXT("CharNameChange_ClsBtn")) { BtnClose = B; }
			continue;
		}
		if (UEditableText* E = Cast<UEditableText>(W)) { if (N.StartsWith(TEXT("CharNameChange_Input"))) { EdtInput = E; } continue; }
		if (UEditableTextBox* E = Cast<UEditableTextBox>(W)) { if (N.StartsWith(TEXT("CharNameChange_Input"))) { EdtInputBox = E; } continue; }
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			if (N.StartsWith(TEXT("CharNameChange_Move"))) { TxtTitle = T; }
			else if (N.StartsWith(TEXT("CharNameChange_Text"))) { TxtPrompt = T; }
			else if (N.StartsWith(TEXT("CharNameChange_ChkBtn"))) { CapCheck = T; }
		}
	}
	// Подпись кнопки: узел текста внутри кнопки (импортёр кладёт подпись ребёнком кнопки).
	if (!CapCheck && BtnCheck)
	{
		if (UTextBlock* T = Cast<UTextBlock>(BtnCheck->GetContent())) { CapCheck = T; }
	}
	// Подписи инициализации `:338410-338424`: 7901 = 109400, 7903 = 109402, 7905 = 2516.
	if (TxtTitle)  { TxtTitle->SetText(FText::FromString(Loc(AsdaRenameLoc::kTitle))); }
	if (TxtPrompt) { TxtPrompt->SetText(FText::FromString(Loc(AsdaRenameLoc::kPrompt))); }
	if (CapCheck)  { CapCheck->SetText(FText::FromString(Loc(AsdaRenameLoc::kBtnCheck))); }
	AsdaUi::BindButton(this, BtnCheck.Get(), 7905, [this](int32) { OnCheckClicked(); }, Relays);
	AsdaUi::BindButton(this, BtnClose.Get(), 7902, [this](int32) { OnCloseClicked(); }, Relays);
	bReady = BtnCheck && BtnClose && (EdtInput || EdtInputBox);
	UE_LOG(LogAsdaRename, Log, TEXT("[смена имени] окно 7900 разобрано: 7905=%d 7902=%d поле=%d/%d заголовок=%d текст=%d подпись=%d"),
		BtnCheck != nullptr, BtnClose != nullptr, EdtInput != nullptr, EdtInputBox != nullptr, TxtTitle != nullptr, TxtPrompt != nullptr, CapCheck != nullptr);
	if (!bLoggedTree)
	{
		bLoggedTree = true;
		for (UWidget* W : All)
		{
			UE_LOG(LogAsdaRename, Verbose, TEXT("[смена имени] узел %s (%s)"), *W->GetName(), *W->GetClass()->GetName());
		}
	}
	if (!bReady) { UE_LOG(LogAsdaRename, Error, TEXT("[смена имени] окно 7900: нет кнопок 7905/7902 или поля 7904 — окно не используется")); }
}

void UAsdaCharRenameSubsystem::ShowWindow()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM || !EnsureWidget()) { return; }
	SetInputText(FString());
	WM->SetWindowVisible(kRenameWindowId, true);
	if (EdtInput) { EdtInput->SetKeyboardFocus(); }
	else if (EdtInputBox) { EdtInputBox->SetKeyboardFocus(); }
	UE_LOG(LogAsdaRename, Log, TEXT("[смена имени] окно 7900 открыто (предмет %d, контейнер %d, ячейка %d)"), PendingCode, PendingContainer, PendingCell);
}

void UAsdaCharRenameSubsystem::HideWindow()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kRenameWindowId, false);
	}
}

bool UAsdaCharRenameSubsystem::IsWindowShown() const
{
	const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && Widget && WM->IsWindowVisible(kRenameWindowId);
}

void UAsdaCharRenameSubsystem::SetInputText(const FString& S)
{
	if (EdtInput) { EdtInput->SetText(FText::FromString(S)); }
	if (EdtInputBox) { EdtInputBox->SetText(FText::FromString(S)); }
}

FString UAsdaCharRenameSubsystem::GetInputText() const
{
	if (EdtInput) { return EdtInput->GetText().ToString(); }
	if (EdtInputBox) { return EdtInputBox->GetText().ToString(); }
	return FString();
}

// ═════════════════════════════ ПОТОК ══════════════════════════════════════════════════════════

bool UAsdaCharRenameSubsystem::BeginUse(int32 ContainerId, int32 CellIndex, int32 ItemCode)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	// Запрос (контейнер, ячейка) запоминается при применении — эталон берёт его в `case 0x43` (`sub_93D5AE`/`sub_92BA1B`).
	PendingContainer = ContainerId;
	PendingCell = CellIndex;
	PendingCode = ItemCode;
	TWeakObjectPtr<UAsdaCharRenameSubsystem> Self(this);
	const FString Text = Loc(AsdaRenameLoc::kAskUse);
	NoticeTrace.Add(AsdaRenameLoc::kAskUse);
	LastNoticeText = Text;
	// TWOBUT (1, 65): 2288 «ОК» / 2289 «Отмена». «Отмена» — ничего (у эталона ветки нет).
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Text, [Self](bool bYes)
	{
		UAsdaCharRenameSubsystem* S = Self.Get();
		if (!S) { return; }
		if (!bYes) { UE_LOG(LogAsdaRename, Log, TEXT("[смена имени] вопрос 65: «Отмена»")); return; }
		S->ShowWindow();   // `case 0x41`
	}, /*GaugeGateSec*/ 0.f, 2288, 2289, /*EtalonNoticeId*/ 65, /*EtalonKind*/ 1);
	UE_LOG(LogAsdaRename, Log, TEXT("[смена имени] предмет %d (контейнер %d, ячейка %d): вопрос 65"), ItemCode, ContainerId, CellIndex);
	return true;
}

void UAsdaCharRenameSubsystem::OnCloseClicked()
{
	// 7902 (:348362): только скрыть окно.
	HideWindow();
	UE_LOG(LogAsdaRename, Log, TEXT("[смена имени] 7902: окно закрыто"));
}

void UAsdaCharRenameSubsystem::OnCheckClicked()
{
	// 7905 (:348364-348384).
	if (!IsWindowShown()) { return; }
	const FString Typed = GetInputText().TrimStartAndEnd();
	PendingName = Typed;   // `uiEditGetWindowTextWithSuffix(+4373, 0x14)`
	if (Typed == SelfName())
	{
		NoticeMsgBox(AsdaRenameLoc::kSameName);   // (0, −1) 101118
		return;
	}
	// ★ [ИСПРАВЛЕНИЕ] `asda.Rename.NameRule`: правило создания персонажа (`AsdaLoginGameMode_Create.cpp`) — до сети.
	if (CVarRenameNameRule.GetValueOnGameThread() != 0)
	{
		if (Typed.IsEmpty() || Typed.Len() > 18 || FAsdaLoginData::WireTextByteLen(Typed) > 19 || Typed.Contains(TEXT("'"))
			|| !FAsdaLoginData::IsValidCharNameByLocale(Typed))
		{
			NoticeMsgBox(AsdaRenameLoc::kBadName);
			UE_LOG(LogAsdaRename, Log, TEXT("[смена имени] 7905: имя «%s» не проходит правило имени — окно 9583, 6138 не ушёл"), *Typed);
			return;
		}
	}
	UGameInstance* GI = GetGameInstance();
	if (const UAsdaChatWindowSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr)
	{
		if (Chat->IsCursed(Typed))
		{
			NoticeMsgBox(AsdaRenameLoc::kBadWords, /*эталон (0, 7009)*/ 7009);
			return;
		}
	}
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	Net->SendCharNameCheck6138(Net->SelectedCharId, Typed);
	UE_LOG(LogAsdaRename, Log, TEXT("[смена имени] 7905: проверка имени «%s» → 6138"), *Typed);
}

void UAsdaCharRenameSubsystem::OnCheckResult(const TArray<uint8>& B)
{
	// `itemTooltipBuildOrNotice@0xF34EC0`: `*(BYTE*)(a1 + 4) == 1` → TWOBUT 66, иначе ONEBUT 109404.
	if (B.Num() < kLen6139) { return; }
	const int32 Code = B[4];
	UE_LOG(LogAsdaRename, Log, TEXT("[смена имени] ←6139 код %d для «%s»"), Code, *PendingName);
	if (Code == 1) { AskAvailable(); }
	else { NoticeMsgBox(AsdaRenameLoc::kTaken); }
}

void UAsdaCharRenameSubsystem::AskAvailable()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	const FString Text = FormatC(Loc(AsdaRenameLoc::kAvailable), { PendingName });   // `sprintf(109403, буфер имени)`
	NoticeTrace.Add(AsdaRenameLoc::kAvailable);
	LastNoticeText = Text;
	TWeakObjectPtr<UAsdaCharRenameSubsystem> Self(this);
	WM->ShowGameConfirm(Text, [Self](bool bYes)
	{
		UAsdaCharRenameSubsystem* S = Self.Get();
		if (S && bYes) { S->AskConfirm(); }   // `case 0x42`; «Отмена» — окно 7900 остаётся
	}, 0.f, 2288, 2289, /*EtalonNoticeId*/ 66, /*EtalonKind*/ 1);
}

void UAsdaCharRenameSubsystem::AskConfirm()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// `case 0x42` (:380151-380159): sprintf(109405, −11495681, своё имя, −380416, новое имя) → TWOBUT (1, 67).
	//   `AsdaNotice::Loc` срезает теги `<FC=%08x>…</F>` строки (остаётся «[%s] -> [%s]») — тогда цвет ставим вокруг имён сами
	//   (тело окна-вопроса разбирает `<FC=…>`). Строка с тегами (4 аргумента) — подстановка как у оригинала.
	const FString Fmt = Loc(AsdaRenameLoc::kConfirm);
	const FString Text = Fmt.Contains(TEXT("%08x"))
		? FormatC(Fmt, { Hex8(kOldNameColor), SelfName(), Hex8(kNewNameColor), PendingName })
		: FormatC(Fmt, { FString::Printf(TEXT("<FC=%s>%s</F>"), *Hex8(kOldNameColor), *SelfName()),
		                 FString::Printf(TEXT("<FC=%s>%s</F>"), *Hex8(kNewNameColor), *PendingName) });
	NoticeTrace.Add(AsdaRenameLoc::kConfirm);
	LastNoticeText = Text;
	TWeakObjectPtr<UAsdaCharRenameSubsystem> Self(this);
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Text, [Self](bool bYes)
	{
		UAsdaCharRenameSubsystem* S = Self.Get();
		if (S && bYes) { S->SendChange(); }   // `case 0x43`
	}, 0.f, 2288, 2289, /*EtalonNoticeId*/ 67, /*EtalonKind*/ 1);
}

void UAsdaCharRenameSubsystem::SendChange()
{
	// `case 0x43` (:380160-380165): поле очищено, окно скрыто, 6140.
	SetInputText(FString());
	HideWindow();
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	// ★ [ПОРТ] Предмет мог уйти из ячейки, пока окно было открыто: без него сервер ответит кодом 5 (109408) — отправка та же,
	//   что у оригинала (сервер — источник истины), в журнал только пометка.
	const UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>();
	const FAsdaItemInstance* Cell = Inv ? Inv->GetCell(PendingContainer, PendingCell) : nullptr;
	if (!Cell || Cell->IsEmpty() || Cell->ItemCode != PendingCode)
	{
		UE_LOG(LogAsdaRename, Warning, TEXT("[смена имени] в ячейке %d/%d уже нет предмета %d — сервер ответит отказом"), PendingContainer, PendingCell, PendingCode);
	}
	Net->SendCharNameChange6140(Net->SelectedCharId, PendingName, PendingCell, PendingContainer);
	UE_LOG(LogAsdaRename, Log, TEXT("[смена имени] вопрос 67 «ОК»: «%s» → «%s», ячейка %d/%d → 6140"), *SelfName(), *PendingName, PendingContainer, PendingCell);
}

void UAsdaCharRenameSubsystem::OnChangeResult(const TArray<uint8>& B)
{
	// `recvItemActionResult_ShowNotice@0xF34CE0`: код — байт +24.
	if (B.Num() < kLen6141) { return; }
	const int32 Code = B[24];
	UE_LOG(LogAsdaRename, Log, TEXT("[смена имени] ←6141 код %d"), Code);
	switch (Code)
	{
	case 0: NoticeMsgBox(AsdaRenameLoc::kError); break;
	case 1:
	{
		// `OnRecv_RemoveSlot(a1 + 25)`: запись предмета 57 б (+4 контейнер, +5 слот). Затем `g_GameState = 0`.
		const uint8* Rec = B.GetData() + 25;
		int32 Cont = Rec[4];
		int32 Slot = AsdaBytes::RdI16(Rec + 5);
		if (Cont != 1 && Cont != 2) { Cont = PendingContainer; Slot = PendingCell; }
		if (UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
		{
			if (Cont >= 1 && Slot >= 0) { Inv->ClearCell(Cont, Slot); }
		}
		NoticeTrace.Add(0);   // след: «успех» (окна у оригинала нет)
		const FString NewName = FAsdaLoginData::DecodeCp1251(B.GetData() + 4, 20).TrimStartAndEnd();
		UE_LOG(LogAsdaRename, Log, TEXT("[смена имени] успех: новое имя «%s», предмет снят (%d/%d)"), *NewName, Cont, Slot);
		bLeavePending = true;   // уход — в Tick (не из разбора пакета)
		break;
	}
	case 2: case 3: NoticeMsgBox(AsdaRenameLoc::kInClan); break;
	case 4: NoticeMsgBox(AsdaRenameLoc::kOnAuction); break;
	case 5: NoticeMsgBox(AsdaRenameLoc::kNoItem); break;
	case 6: NoticeMsgBox(AsdaRenameLoc::kWrongItem); break;
	case 7: NoticeMsgBox(AsdaRenameLoc::kInParty); break;
	default: UE_LOG(LogAsdaRename, Log, TEXT("[смена имени] 6141 код %d — у эталона ветки нет (молча)"), Code); break;
	}
}

void UAsdaCharRenameSubsystem::LeaveAfterSuccess()
{
	++LeaveCount;
	const int32 Mode = CVarRenameAfterSuccess.GetValueOnGameThread();
	if (bSuppressLeave || Mode == 0)
	{
		UE_LOG(LogAsdaRename, Log, TEXT("[смена имени] уход после успеха пропущен (%s)"), bSuppressLeave ? TEXT("самотест") : TEXT("ключ 0"));
		return;
	}
	UGameInstance* GI = GetGameInstance();
	if (Mode == 2)
	{
		// Оригинал: `g_GameState = 0` ⇒ главный цикл возвращает 0, клиент завершается.
		UKismetSystemLibrary::QuitGame(GI ? GI->GetWorld() : nullptr, nullptr, EQuitPreference::Quit, false);
		return;
	}
	// [ПОРТ] Порт не закрывает клиент (как и при разрыве): возврат к выбору персонажа — список придёт с новым именем.
	if (UAsdaSystemOptionSubsystem* SO = GI ? GI->GetSubsystem<UAsdaSystemOptionSubsystem>() : nullptr)
	{
		SO->DoReturnToCharSelect();
	}
}

// ═════════════════════════════ НОТИСЫ ══════════════════════════════════════════════════════════

void UAsdaCharRenameSubsystem::NoticeMsgBox(int32 LocId, int32 EtalonId)
{
	NoticeTrace.Add(LocId);
	LastNoticeText = Loc(LocId);
	AsdaNotice::MsgBoxLoc(GetGameInstance(), EAsdaMsgCategory::ItemRefused, LocId, AsdaLoc::kBtnOk, EtalonId);
}

FString UAsdaCharRenameSubsystem::Loc(int32 LocId) const
{
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

FString UAsdaCharRenameSubsystem::SelfName() const
{
	const UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	return WS ? WS->GetSelfCharName() : FString();
}
