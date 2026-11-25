// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ОКНА РЫБАЛКИ 71300 И МИНИ-ИГРЫ, ВОЛНА В2 (08.10.2026). [ПОРТ]
//
// ★ ЗАЧЕМ. Сервера офлайн нет, поэтому путь игрока проверяется так:
//   • ПРИЁМЫ — штатный разбор `UAsdaNetSubsystem::DebugFeedPacket` → `HandlePacket` → делегат → очередь автомата;
//   • ОТПРАВКИ — БОЕВЫЕ отправители под перехватом `UAsdaPetSubsystem::BeginSendCapture` (в сеть не уходит ничего, charId
//     подменён тестовым): опкод, конверт 'S', каждый байт тела 6169 {charId i32, шкала i16, всего u8, верно u8};
//   • ВВОД — нажатия идут ТЕМ ЖЕ путём, что от клавиатуры: `APlayerController::InputKey` (вьюпорт зовёт его же) —
//     фильтр `AAsdaPlayerController::FishFilterKey` → `UAsdaFishWindowSubsystem::OnGuideKeyDown`;
//   • ВРЕМЯ — часы автомата (их же читает гайд) сдвигаются `TestClockOffsetMs`; клипы закрывает мост В1 (`ClipBridge 1`);
//   • ГЕОМЕТРИЯ — второй этап через ~1 с: живая `GetCachedGeometry` всех ключевых контролов в `-game` (не PIE-дамп).
//   Контрольные пары — что НЕ должно сработать: клавиша вне состояния 4 не засчитывается; при рыбалке W/A/S/D и цифры
//   хотбара съедены (клавиша не «зажата»), Esc/Enter проходят; Alt не засчитывается; пауза вне (115, гайд 2) — «нельзя»;
//   Р8 при ключе 1 — инверсия; без рыбалки клавиши не трогаются.
//
// Консоль: `asda.Fish.WinSelfTest [delay=N] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки — «САМОТЕСТ-ОКНО-РЫБЫ ok/FAIL …», итог — «САМОТЕСТ-ОКНО-РЫБЫ ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING
#include "UI/AsdaUiHelpers.h"   // ★ R1: прямое включение (раньше приходило транзитивно)

#include "UI/AsdaFishWindowSubsystem.h"
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "World/AsdaFishingSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaBytes.h"
#include "AsdaSessionReset.h"
#include "Pet/AsdaPetSubsystem.h"            // BeginSendCapture / EndSendCapture — перехват исходящих
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Components/Image.h"
#include "Components/CheckBox.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Widgets/SViewport.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFishWinTest, Log, All);

// Друг `UAsdaFishWindowSubsystem` и `UAsdaFishingSubsystem` (объявлено в их заголовках).
struct FAsdaFishWinSelfTest
{
	// Перехваченная отправка.
	struct FSent
	{
		int32 Op = 0;          // опкод
		TArray<uint8> P;       // тело (без конверта)
		TCHAR Env = 0;         // конверт: 'S' секрет+MD5, 'R' сырой
	};

	int32 Ok = 0, Fail = 0;                     // пройдено / провалено
	TArray<FSent> Sent;                         // перехваченное
	UGameInstance* GI = nullptr;                // экземпляр игры
	UAsdaNetSubsystem* Net = nullptr;           // сеть
	UAsdaFishingSubsystem* F = nullptr;         // автомат рыбалки (В1)
	UAsdaFishWindowSubsystem* Win = nullptr;    // окно 71300 (В2)
	UAsdaWindowSubsystem* WM = nullptr;         // окна-сообщения / реестр
	UAsdaInventorySubsystem* Inv = nullptr;     // инвентарь
	UAsdaLoadManagerSubsystem* LM = nullptr;    // карта
	UAsdaPetSubsystem* Pet = nullptr;           // перехват отправки
	AAsdaPlayerController* PC = nullptr;        // контроллер (фильтр клавиш)
	AAsdaPlayerCharacter* Ch = nullptr;         // свой персонаж
	bool bCapture = false;                      // перехват взведён этим тестом
	TArray<TPair<int32, int32>> UsedCells;      // (контейнер, ячейка), занятые прогоном

	// Снимок для восстановления.
	int32 CharIdWas = 0;            // SelectedCharId
	uint16 UserNumWas = 0;          // SelfUserNum
	bool bKnownWas = false;         // bSelfUserNumKnown
	int32 MapWas = 0;               // CurrentMapId
	FVector2D PosWas;               // позиция персонажа

	static constexpr int32 kCharId = 0x00ABCDEF;   // свой charId (младшее слово ≠ userNum)

	// Одна проверка.
	void Expect(bool b, const FString& What)
	{
		if (b) { ++Ok; UE_LOG(LogAsdaFishWinTest, Log, TEXT("САМОТЕСТ-ОКНО-РЫБЫ ok   %s"), *What); }
		else   { ++Fail; UE_LOG(LogAsdaFishWinTest, Error, TEXT("САМОТЕСТ-ОКНО-РЫБЫ FAIL %s"), *What); }
	}
	// Шаг кадра автомата (порядок Tick В1): очередь → автомат → очередь 6173 → мост клипов.
	void Step(int32 N = 1)
	{
		for (int32 i = 0; i < N; ++i) { F->DrainPackets(); F->UpdateFsm(); F->ApplyPeerQueue(); F->TickClipBridge(); }
	}
	// Подать пакет штатным разбором и разобрать очередь автомата.
	void Feed(int32 Op, const TArray<uint8>& Body) { AsdaTestNet::DebugFeedPacket(Net, Op, Body); F->DrainPackets(); }
	// Сдвинуть часы автомата (их же читает гайд окна).
	void Advance(int32 Ms) { F->TestClockOffsetMs += Ms; }
	// Сколько раз ушёл опкод; OutLast — последний.
	int32 Count(int32 Op, FSent* OutLast = nullptr) const
	{
		int32 N = 0;
		for (const FSent& S : Sent) { if (S.Op == Op) { ++N; if (OutLast) { *OutLast = S; } } }
		return N;
	}
	// Нажатие клавиши ТЕМ ЖЕ путём, что от вьюпорта (`APlayerController::InputKey`). Возврат — «событие съедено».
	bool Key(const FKey& K, EInputEvent Ev)
	{
		return static_cast<APlayerController*>(PC)->InputKey(FInputKeyEventArgs::CreateSimulated(K, Ev, Ev == IE_Released ? 0.f : 1.f));
	}
	// Только фильтр (без передачи в `UPlayerInput`): для клавиш, которые фильтр ПРОПУСКАЕТ (Esc/Enter, всё без рыбалки) —
	//   иначе тест открыл бы ESC-меню / чат или двинул персонажа. Возврат — «событие съедено».
	bool FilterOnly(const FKey& K, EInputEvent Ev)
	{
		return PC->FishFilterKey(FInputKeyEventArgs::CreateSimulated(K, Ev, Ev == IE_Released ? 0.f : 1.f));
	}
	// Был ли нотис «канал:loc-id» в журнале автомата.
	bool HadNotice(const TCHAR* Channel, int32 LocId) const
	{
		const FString Pfx = FString::Printf(TEXT("%s:%d:"), Channel, LocId);
		for (const FString& L : F->GetRecentNotices()) { if (L.StartsWith(Pfx)) { return true; } }
		return false;
	}
	// Видимость узла.
	static bool Vis(const UWidget* W) { return W && W->GetVisibility() != ESlateVisibility::Collapsed && W->GetVisibility() != ESlateVisibility::Hidden; }
	// Левый край слота.
	static float SlotL(const UWidget* W) { const UCanvasPanelSlot* S = W ? AsdaUi::SlotOf(W) : nullptr; return S ? S->GetOffsets().Left : -99999.f; }
	// Ширина слота (POINT-анкер: Right).
	static float SlotW(const UWidget* W) { const UCanvasPanelSlot* S = W ? AsdaUi::SlotOf(W) : nullptr; return S ? S->GetOffsets().Right : -1.f; }
	// Имя ресурса кисти картинки (нижний регистр).
	static FString BrushTex(const UImage* Img)
	{
		const UObject* R = Img ? Img->GetBrush().GetResourceObject() : nullptr;
		return R ? R->GetName().ToLower() : FString();
	}
	// Левый край UV-области кисти в пикселях логического атласа.
	static int32 BrushUvX(const UImage* Img)
	{
		if (!Img) { return -1; }
		const UTexture2D* T = Cast<UTexture2D>(Img->GetBrush().GetResourceObject());
		if (!T) { return -1; }
		const FIntPoint Sz = AsdaUi::LogicalAtlasSize(T);
		const FBox2D UV = Img->GetBrush().GetUVRegion();
		return FMath::RoundToInt((float)UV.Min.X * (float)Sz.X);
	}
	// FKey стрелки по VK (38 ↑ 37 ← 40 ↓ 39 →); bNumpad — нумпадный двойник (8/4/2/6).
	static FKey ArrowKey(int32 Vk, bool bNumpad)
	{
		switch (Vk)
		{
		case 38: return bNumpad ? EKeys::NumPadEight : EKeys::Up;
		case 37: return bNumpad ? EKeys::NumPadFour : EKeys::Left;
		case 40: return bNumpad ? EKeys::NumPadTwo : EKeys::Down;
		case 39: return bNumpad ? EKeys::NumPadSix : EKeys::Right;
		default: return EKeys::Invalid;
		}
	}
	// Ожидаемая рыбка 71326: L = база + (int)(786/500·v − 56) + 8 (`gauge_SetValue`).
	float WantAniL(int32 V) const
	{
		return Win->AniBaseLeft + (float)((int32)(int64)((double)Win->GaugeExtentW / 500.0 * (double)V - (double)Win->AniExtentW) + 8);
	}
	// Тело 6168.
	static TArray<uint8> B6168(uint8 Code, int32 V1, int32 Bonus)
	{
		TArray<uint8> B; B.SetNumZeroed(7);
		B[0] = Code; AsdaBytes::WrI32(B.GetData() + 1, V1); AsdaBytes::WrI16(B.GetData() + 5, Bonus);
		return B;
	}
	// Тело 6170.
	static TArray<uint8> B6170(uint8 Code, int32 Phase, int32 Gauge)
	{
		TArray<uint8> B; B.SetNumZeroed(5);
		B[0] = Code; AsdaBytes::WrI16(B.GetData() + 1, Phase); AsdaBytes::WrI16(B.GetData() + 3, Gauge);
		return B;
	}
	// Тело 6172 (код, навык, вес, размер; наживка @7, рыба @64).
	static TArray<uint8> B6172(uint8 Code, int32 Size, int32 FishCode, int32 FishSlot)
	{
		TArray<uint8> B; B.SetNumZeroed(121);
		B[0] = Code; AsdaBytes::WrI16(B.GetData() + 1, 40); AsdaBytes::WrI16(B.GetData() + 3, 100); AsdaBytes::WrI16(B.GetData() + 5, Size);
		uint8* R = B.GetData() + 7; AsdaBytes::WrI32(R, 69667); R[4] = 3; AsdaBytes::WrI16(R + 5, 10); AsdaBytes::WrI32(R + 9, 4);
		if (FishCode > 0) { uint8* Q = B.GetData() + 64; AsdaBytes::WrI32(Q, FishCode); Q[4] = 1; AsdaBytes::WrI16(Q + 5, FishSlot); AsdaBytes::WrI32(Q + 9, 1); }
		return B;
	}

	// Этап 1 (синхронно): весь цикл. false — дальше идти нельзя.
	bool RunLogic(UWorld* W);
	// Этап 2 (через кадры): живая геометрия.
	void RunGeometry();
	// Подготовка этапа 2: окно, панель на 4 ячейки (O, X, O, X), таймер, шкала 250.
	void PrepareGeometry();
	// После этапа 2: спрятать панель и окно, автомат 0.
	void AfterGeometry();
	// Восстановление.
	void Restore();
};

bool FAsdaFishWinSelfTest::RunLogic(UWorld* W)
{
	GI = W ? W->GetGameInstance() : nullptr;
	Net = GI ? AsdaTestNet::Get(GI) : nullptr;
	F = GI ? GI->GetSubsystem<UAsdaFishingSubsystem>() : nullptr;
	Win = GI ? GI->GetSubsystem<UAsdaFishWindowSubsystem>() : nullptr;
	WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	Pet = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
	PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	Ch = F ? F->SelfChar() : nullptr;
	if (!Net || !F || !Win || !WM || !Inv || !LM || !Pet || !PC || !Ch)
	{
		Expect(false, TEXT("нет подсистем/контроллера/персонажа"));
		return false;
	}
	// ── Снимок ──
	CharIdWas = AsdaTestNet::SelectedCharId(Net); UserNumWas = AsdaTestNet::SelfUserNum(Net); bKnownWas = AsdaTestNet::bSelfUserNumKnown(Net);
	MapWas = LM->CurrentMapId; PosWas = Ch->GetGamePosXZ();
	IConsoleVariable* CvBridge = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Fish.ClipBridge"));
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): исходный ClipBridge (значение + SetBy) запоминает `AsdaCVarSave` (прежде — поле BridgeWas).
	if (CvBridge) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.ClipBridge"), 1); }

	// ════════ A. РЕГИСТРАЦИЯ И РАЗМЕТКА ════════
	Expect(F->GetRegisteredMiniGame() == Win, TEXT("A1 окно В2 зарегистрировано мини-игрой автомата (эмулятор В1 отключён)"));
	Expect(Win->EnsureWidget(), TEXT("A2 WBP_FISH_WIN открыт и разобран ЦЕЛИКОМ (15 ячеек + клоны O/X шага пост-импорта, полосы, цифры, кнопки)"));
	if (!Win->bControlsReady) { return false; }
	Expect(!Vis(Win->TmplO) && !Vis(Win->TmplX), TEXT("A3 шаблоны SMALL_O/SMALL_X скрыты (init эталона)"));
	{
		bool bClonesOk = true;
		for (int32 i = 0; i < UAsdaFishWindowSubsystem::kCells; ++i)
		{
			const FAsdaFishCellUi& C = Win->Cells[i];
			bClonesOk &= C.O && C.X && C.O->GetParent() == C.Icon->GetParent() && FMath::IsNearlyEqual(SlotL(C.O), SlotL(C.Icon) - 15.f) && !Vis(C.O) && !Vis(C.X);
		}
		Expect(bClonesOk, TEXT("A4 клоны O/X у каждой из 15 ячеек: родитель ячейки, Origin −15 (InstantiateChild), скрыты"));
	}

	if (!Pet->BeginSendCapture([this](int32 Op, const TArray<uint8>& Body, bool, TCHAR Env)
	{
		FSent S; S.Op = Op; S.P = Body; S.Env = Env; Sent.Add(MoveTemp(S));
	}, kCharId))
	{
		Expect(false, TEXT("перехват отправки занят другим инструментом"));
		return false;
	}
	bCapture = true;
	F->bTestNoNetDrain = true;
	F->ResetSessionState(EAsdaSessionResetReason::Disconnected);
	// ★ Ревизия В2 МЕЛК-1 (волна В4): прежний A5 здесь был тавтологией (окно ещё не показывалось) — настоящая проверка
	//   сброса посреди раунда (окно открыто, гайд 4) — K1 в конце этапа.
	WM->ClearMsgBoxQueue(TEXT("самотест окна рыбалки: старт"));
	LM->CurrentMapId = 3;
	TArray<int32> Alp; F->GetSpotsOfMap(3, Alp);
	const FAsdaFishSpotRow* S0 = Alp.Num() ? F->GetSpot(Alp[0]) : nullptr;
	if (!S0) { Expect(false, TEXT("нет спотов Альпии")); return false; }
	Ch->StopMove(); Ch->NetSnap(FVector2D((float)S0->X * 100.f + 300.f, (float)S0->Y * 100.f));
	Inv->DebugPutItem(3, UAsdaFishingSubsystem::kEquipSlotRod, 69657, 1);  UsedCells.Add({ 3, UAsdaFishingSubsystem::kEquipSlotRod });
	Inv->DebugPutItem(3, UAsdaFishingSubsystem::kEquipSlotBait, 69667, 5); UsedCells.Add({ 3, UAsdaFishingSubsystem::kEquipSlotBait });

	// ════════ B. ВХОД → 6168 → ОКНО ════════
	F->StartAtNearestSpot(); Step(); WM->CloseConfirm(true);
	Step(3);
	Expect(F->GetFishState() == 107 && Count(6167) == 1, FString::Printf(TEXT("B1 81 OK → 102 → 101 → 100 → 6167 → 107 (автомат %d)"), F->GetFishState()));
	Inv->OpenWindow();
	Feed(6168, B6168(1, 69577, 120));
	Expect(F->GetFishState() == 108 && FMath::IsNearlyEqual(SlotL(Win->FishAni), WantAniL(120)),
		FString::Printf(TEXT("B2 6168 код 1 → шкала 120: рыбка 71326 L=%.0f (ждём %.0f = база + (int)(786/500·120 − 56) + 8)"), SlotL(Win->FishAni), WantAniL(120)));
	Step(); Step(); Advance(3700); Step();   // 108 → 112; 112 — действие 60 (мост клипа взведён); конец клипа → 110
	Expect(F->GetFishState() == 110, TEXT("B3 108 → 112 → конец клипа 60 → 110"));
	Step();
	const FAsdaFishDataRow* Rec = F->FindFishData(69577);
	const TCHAR* WantTex = TEXT("ne_fish_small");
	if (Rec && Rec->FishType == 1) { WantTex = TEXT("ne_fish_nomal"); }
	if (Rec && Rec->FishType == 2) { WantTex = TEXT("ne_fish_big"); }
	if (Rec && Rec->FishType == 3) { WantTex = TEXT("ne_fish_special"); }
	Expect(F->GetFishState() == 114 && Win->IsOpen() && WM->IsWindowVisible(FName(TEXT("FISH_WIN"))) && !Inv->IsWindowOpen() &&
		Vis(Win->ImageGrp) && !Vis(Win->TimerGrp) && !Vis(Win->PopupGrp) && BrushTex(Win->FishTypeImg) == WantTex &&
		!F->IsPaused() && !Win->ChkPause->IsChecked() && Win->ChkAuto->IsChecked(),
		FString::Printf(TEXT("B4 110 FishingWindow_Show: окно видно, инвентарь закрыт, 71307 видна, таймер/всплывашка скрыты, картинка «%s» (ждём %s, тип %d), пауза 0 (галка снята), галка автоповтора = ВКЛ (Р8)"),
			*BrushTex(Win->FishTypeImg), WantTex, Rec ? Rec->FishType : -1));
	Step();
	Expect(F->GetFishState() == 115 && Win->Guide.State == 1 && Win->Guide.TotalPages == 6 && Win->RoundText->GetText().IsEmpty(),
		TEXT("B5 114 → гайд на 6 раундов (Fish_data +8), state 1, текст 71346 пуст"));

	// ════════ C. КОНТРОЛЬНЫЕ ПАРЫ ВВОДА ВНЕ STATE 4 ════════
	Step();   // гайд 1 → 2
	{
		const bool bAteW = Key(EKeys::W, IE_Pressed);
		const bool bAteWr = Key(EKeys::W, IE_Repeat);
		Key(EKeys::W, IE_Released);
		const bool bAte1 = Key(EKeys::One, IE_Pressed);
		Key(EKeys::One, IE_Released);
		const bool bAteRel = FilterOnly(EKeys::D, IE_Released);
		Expect(Win->Guide.State == 2 && Win->Guide.Recs.Num() == 0 && bAteW && bAteWr && bAte1 && !bAteRel,
			TEXT("C1 гайд 2: W (нажатие и автоповтор) и «1» (хотбар) при рыбалке съедены фильтром до UPlayerInput — ни хода, ни хотбара; ответа нет (вне state 4 не засчитывается); отпускание проходит"));
		const bool bAteEsc = FilterOnly(EKeys::Escape, IE_Pressed);
		const bool bAteEnt = FilterOnly(EKeys::Enter, IE_Pressed);
		Expect(!bAteEsc && !bAteEnt, TEXT("C2 Esc и Enter при рыбалке фильтр пропускает (KEYUP эталона 27/13)"));
	}
	// Пауза в гайде 2 — ставится; держит отсчёт.
	F->ClearRecentNotices();
	Win->OnPausePressed();
	Expect(F->IsPaused() && Win->ChkPause->IsChecked() && HadNotice(TEXT("plank"), 111062), TEXT("C3 71329 при (115, гайд 2): пауза 1, галка, плашка 111062"));
	Advance(5000); Step();
	Expect(Win->Guide.State == 2, TEXT("C4 пауза держит гайд в 2 (5 с > 3 с)"));
	F->ClearRecentNotices();
	Win->OnPausePressed();
	Expect(!F->IsPaused() && !Win->ChkPause->IsChecked() && HadNotice(TEXT("plank"), 111063), TEXT("C5 повтор 71329: пауза 0, плашка 111063"));
	Win->ChkPause->SetIsChecked(true);   // как щелчок UMG по галке
	Win->OnPauseCheckChanged(true);
	Expect(!Win->ChkPause->IsChecked(), TEXT("C6 галка сама не переключается (+609 = 1): состояние ставит только код"));

	// ════════ D. РАУНД 1: ВВОД ════════
	Advance(3100); Step(); Step();
	Expect(Win->Guide.State == 4 && Win->Guide.Recs.Num() == 4 && Win->IsPanelShown() && Vis(Win->PanelGrp) && Vis(Win->TimerGrp) &&
		Vis(Win->DigitsGrp) && !Vis(Win->ImageGrp) && Win->RoundText->GetText().ToString() == TEXT("1/6") && Win->Guide.RoundLimitMs == 4000,
		FString::Printf(TEXT("D1 гайд 3 → 4: стрелок %d (Fish_num по умению спота = 4), панель/таймер/цифры видны, 71307 скрыта, «%s», лимит %d"),
			Win->Guide.Recs.Num(), *Win->RoundText->GetText().ToString(), Win->Guide.RoundLimitMs));
	{
		bool bCells = true, bArrows = true;
		for (int32 i = 0; i < UAsdaFishWindowSubsystem::kCells; ++i)
		{
			bCells &= (Vis(Win->Cells[i].Btn.Get()) == (i < 4)) && !Vis(Win->Cells[i].O) && !Vis(Win->Cells[i].X);
		}
		for (int32 j = 0; j < 4; ++j)
		{
			const int32 Vk = Win->Guide.Recs[j].Vk;
			const int32 Want = (Vk == 38) ? 0 : (Vk == 37) ? 1 : (Vk == 40) ? 2 : (Vk == 39) ? 3 : -1;
			bArrows &= Want >= 0 && BrushTex(Win->Cells[j].Icon) == TEXT("fish_gfont") && BrushUvX(Win->Cells[j].Icon) == 32 * Want;
		}
		Expect(bCells && bArrows, TEXT("D2 показаны ровно 4 подложки BLUE_BIG_BTN, O/X скрыты; кадры fish_gfont 32·ячейка (↑0 ←1 ↓2 →3), только стрелки"));
		Expect(FMath::IsNearlyEqual(SlotL(Win->PanelGrp), Win->PanelRootLeft + 9.f) && FMath::IsNearlyEqual(SlotW(Win->PanelPlane), 254.f) &&
			FMath::IsNearlyEqual(SlotW(Win->PanelBbPlane), 246.f),
			FString::Printf(TEXT("D3 центрирование 71310: x = 100 − (int)(3·61·0.5) = 9 (L %.0f), ширины 71310/71308 = %.0f/%.0f (ждём 254/246)"),
				SlotL(Win->PanelGrp) - Win->PanelRootLeft, SlotW(Win->PanelPlane), SlotW(Win->PanelBbPlane)));
	}
	F->ClearRecentNotices();
	Win->OnPausePressed();
	Expect(!F->IsPaused() && HadNotice(TEXT("plank"), 111061), TEXT("D4 контроль: 71329 в гайде 4 — «нельзя» (111061), пауза не ставится"));
	Step();
	// ★ В45fix2 (09.10.2026): часы автомата — НАСТОЯЩИЕ (`timeGetTime`, `NowMs`), а 4000 мс — ровно граница разряда: если между
	//   стартом раунда (D1) и этим шагом прошла хоть 1 мс реального времени (D4 пишет плашку в чат), остаток 3999…3901 мс
	//   законно показывается «0» «3» «9». Проверка привязана к фактически прошедшему времени: до 0 мс — «040», 1…99 мс — «039».
	{
		const bool b040 = BrushUvX(Win->Digit1) == 0 && BrushUvX(Win->Digit2) == 16 * 4 && BrushUvX(Win->Digit3) == 0;
		const bool b039 = BrushUvX(Win->Digit1) == 0 && BrushUvX(Win->Digit2) == 16 * 3 && BrushUvX(Win->Digit3) == 16 * 9;
		Expect(b040 || b039,
			FString::Printf(TEXT("D5 таймер: 4000 мс → «0» «4» «0», либо «0» «3» «9», если с начала раунда прошло 1…99 мс реального времени (fish_gfont2 16·d): %d/%d/%d"),
				BrushUvX(Win->Digit1), BrushUvX(Win->Digit2), BrushUvX(Win->Digit3)));
	}
	Advance(1350); Step();
	Expect(BrushUvX(Win->Digit2) == 16 * 2 && BrushUvX(Win->Digit3) == 16 * 6,
		FString::Printf(TEXT("D6 через 1350 мс: остаток 2650 → «02» «6» (десятые): %d/%d"), BrushUvX(Win->Digit2), BrushUvX(Win->Digit3)));
	{
		const int32 V0 = Win->Guide.Recs[0].Vk, V1 = Win->Guide.Recs[1].Vk, V3 = Win->Guide.Recs[3].Vk;
		Key(EKeys::LeftAlt, IE_Pressed);
		Key(ArrowKey(V0, false), IE_Pressed);
		Key(ArrowKey(V0, false), IE_Released);
		Key(EKeys::LeftAlt, IE_Released);
		Expect(!Win->Guide.Recs[0].bAnswered, TEXT("D7 контроль: Alt и Alt+стрелка (WM_SYSKEYDOWN) ответом не считаются"));
		const bool bAte = Key(ArrowKey(V0, false), IE_Pressed);
		Key(ArrowKey(V0, false), IE_Released);
		Key(ArrowKey(V1, true), IE_Pressed);   Key(ArrowKey(V1, true), IE_Released);
		Key(EKeys::LeftShift, IE_Pressed);     Key(EKeys::LeftShift, IE_Released);   // любая клавиша = ответ (неверный)
		Key(ArrowKey(V3, false), IE_Repeat);   // автоповтор не отсекается
		const FAsdaFishKeyGuide& G = Win->Guide;
		Expect(bAte && G.Recs[0].bAnswered && G.Recs[0].bCorrect && G.Recs[1].bCorrect && G.Recs[2].bAnswered && !G.Recs[2].bCorrect &&
			G.Recs[3].bAnswered && G.Recs[3].bCorrect,
			TEXT("D8 ввод через PlayerController::InputKey: стрелка верно (съедена), нумпад-двойник верно, Shift = неверный ответ, автоповтор засчитан"));
		Expect(Vis(Win->Cells[0].O) && !Vis(Win->Cells[0].X) && Vis(Win->Cells[2].X) && !Vis(Win->Cells[2].O) && Vis(Win->Cells[3].O),
			TEXT("D9 клоны O/X на ячейках по ответам (0 — O, 2 — X, 3 — O)"));
		const bool bAteExtra = Key(EKeys::Q, IE_Pressed); Key(EKeys::Q, IE_Released);
		Expect(bAteExtra && G.Recs.Num() == 4, TEXT("D10 клавиша после заполнения всех ячеек съедена без эффекта"));
	}
	Sent.Reset();
	Step();                 // 4: все отвечены → 6
	Step();                 // 6 → 7
	Advance(2100); Step();  // 7 → 5
	Step();                 // 5: 6169
	{
		FSent S; const int32 N = Count(6169, &S);
		Expect(N == 1 && S.Env == TEXT('S') && S.P.Num() == 8 && AsdaBytes::RdI32(S.P.GetData()) == kCharId &&
			AsdaBytes::RdI16(S.P.GetData() + 4) == 120 && S.P[6] == 4 && S.P[7] == 3 && !Win->IsPanelShown() &&
			(F->GetSelfAction() == 63 || F->GetSelfAction() == 64) && Win->Guide.State == 1 && Win->Guide.CurPage == 1,
			FString::Printf(TEXT("D11 6169 ×%d конверт '%c': %s (ждём EF CD AB 00 78 00 04 03), панель скрыта, действие %d (63|64), гайд → 1, раунд 1"),
				N, S.Env ? S.Env : TEXT('-'), *BytesToHex(S.P.GetData(), S.P.Num()), F->GetSelfAction()));
	}
	Feed(6170, B6170(1, 1, 200));
	Expect(FMath::IsNearlyEqual(SlotL(Win->FishAni), WantAniL(200)), FString::Printf(TEXT("D12 6170 → шкала 200, рыбка L %.0f (ждём %.0f)"), SlotL(Win->FishAni), WantAniL(200)));

	// ════════ E. РАУНД 2: ТАЙМАУТ ════════
	Step(); Advance(3100); Step(); Step();
	Expect(Win->Guide.State == 4 && Win->RoundText->GetText().ToString() == TEXT("2/6"), TEXT("E1 раунд 2: «2/6»"));
	Advance(4100); Step();
	{
		bool bAllX = true;
		for (int32 j = 0; j < 4; ++j) { bAllX &= Win->Guide.Recs[j].bAnswered && !Win->Guide.Recs[j].bCorrect && Vis(Win->Cells[j].X); }
		Expect(Win->Guide.State == 6 && bAllX && BrushUvX(Win->Digit1) == 0 && BrushUvX(Win->Digit2) == 0 && BrushUvX(Win->Digit3) == 0,
			TEXT("E2 таймаут 4000 мс: неотвеченные = неверно (X), таймер «00» «0» (Р9: остаток < 0 → 0)"));
	}
	Sent.Reset(); Step(); Advance(2100); Step(); Step();
	{
		FSent S; const int32 N = Count(6169, &S);
		Expect(N == 1 && S.P.Num() == 8 && AsdaBytes::RdI16(S.P.GetData() + 4) == 200 && S.P[6] == 4 && S.P[7] == 0,
			FString::Printf(TEXT("E3 6169 таймаута: %s (ждём … C8 00 04 00)"), *BytesToHex(S.P.GetData(), S.P.Num())));
	}

	// ════════ F. ШКАЛА ПОЛНА → 116 → 6171 → РЕЗУЛЬТАТ ════════
	Feed(6170, B6170(1, 2, 500));
	Expect(F->GetFishState() == 116 && FMath::IsNearlyEqual(SlotL(Win->FishAni), WantAniL(500)), TEXT("F1 6170 шкала 500 → 116, рыбка в конце полосы"));
	Sent.Reset(); Step(); Step();
	Expect(F->GetFishState() == 118 && Count(6171) == 1, TEXT("F2 116 → 117 → 6171(1) → 118"));
	int32 FishSlot = -1;
	for (int32 c = 0; c < Inv->GetContainerSize(1); ++c) { if (Inv->GetCellItemCode(1, c) == 0) { FishSlot = c; break; } }
	if (FishSlot >= 0) { UsedCells.Add({ 1, FishSlot }); }
	Feed(6172, B6172(1, 55, 69577, FishSlot));
	Step(); Step(); Advance(2100); Step(); Step(); Advance(3400); Step();   // 119 → 113 → (62) → 126 → (65) → 120
	Expect(F->GetFishState() == 120, FString::Printf(TEXT("F3 6172 код 1 → 119 → 113 → 126 → 120 (автомат %d)"), F->GetFishState()));
	Step();
	{
		const UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
		const FString Name = IT ? IT->GetItemName(69577) : FString();
		const FString Raw = Win->PopupText.LastRaw;
		Expect(Win->IsCatchPopupShown() && Raw.Contains(Name) && Raw.Contains(TEXT("55")) && Win->PopupIcon->GetBrush().GetResourceObject() != nullptr,
			FString::Printf(TEXT("F4 120 → всплывашка 71400: «%s» (111070: имя «%s», 55 см), иконка улова есть"), *Raw, *Name));
	}
	Advance(3100); Step();
	Expect(F->GetFishState() == 121 && !Win->IsCatchPopupShown(), TEXT("F5 через 3 с → 121, всплывашка скрыта"));

	// ════════ G. АВТОПОВТОР (Р8) И КОНЕЦ 128 ════════
	Win->OnAutoRepeatPressed();
	Expect(!F->GetAutoRepeat() && !Win->ChkAuto->IsChecked(), TEXT("G1 71349: автоповтор ВЫКЛ, галка снята (Р8: фактическое состояние)"));
	Step();
	Expect(F->GetFishState() == 128, TEXT("G2 121 при автоповторе 0 → 128"));
	Step();
	Expect(F->GetFishState() == 0 && !Win->IsOpen() && !WM->IsWindowVisible(FName(TEXT("FISH_WIN"))) && !Win->IsPanelShown() &&
		Win->RoundText->GetText().IsEmpty() && FMath::IsNearlyEqual(Win->GaugeBar.Value, 0.f),
		TEXT("G3 128: панель скрыта, 71303/71346 очищены, 71305 = 0, окно скрыто → 0"));
	{
		IConsoleVariable* CvAuto = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Fish.LegacyAutoCheck"));
		if (CvAuto) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.LegacyAutoCheck"), 1); }
		Win->OnAutoRepeatPressed();   // AR 0 → 1; эталон: галка = (AR == 0) = снята
		const bool bA = F->GetAutoRepeat() && !Win->ChkAuto->IsChecked();
		Win->OnAutoRepeatPressed();   // AR 1 → 0; эталон: галка отмечена
		const bool bB = !F->GetAutoRepeat() && Win->ChkAuto->IsChecked();
		Expect(bA && bB, TEXT("G4 контроль Р8: asda.Fish.LegacyAutoCheck 1 — инверсия оригинала (ВКЛ — снята, ВЫКЛ — отмечена)"));
		if (CvAuto) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.LegacyAutoCheck"), 0); }
		Win->OnAutoRepeatPressed();
		Expect(F->GetAutoRepeat() && Win->ChkAuto->IsChecked(), TEXT("G5 ключ 0: автоповтор ВКЛ — галка отмечена"));
	}
	{
		const bool bAte = FilterOnly(EKeys::W, IE_Pressed);
		const bool bAteUp = FilterOnly(EKeys::Up, IE_Pressed);
		Expect(!bAte && !bAteUp && !Win->IsGuideInputState(), TEXT("G6 контроль: без рыбалки фильтр клавиш ничего не съедает (W, ↑)"));
	}

	// ════════ H. КНОПКИ X / ESC / ПОДСКАЗКИ ════════
	F->FishState = 115;
	Win->FishWinShow();
	WM->ClearMsgBoxQueue(TEXT("самотест окна рыбалки: H"));
	Win->OnCancelPressed();
	Expect(WM->IsMsgBoxQueued(1, 80), TEXT("H1 X 71328 → диалог (1, 80) «Прекратить рыбалку?»"));
	WM->ClearMsgBoxQueue(TEXT("самотест окна рыбалки: H2"));
	const bool bEsc = WM->CloseTopmostOnEsc();
	Expect(bEsc && WM->IsMsgBoxQueued(1, 80), TEXT("H2 ESC (общий проход CloseTopmostOnEsc, событие 0x15) → диалог 80"));
	WM->ClearMsgBoxQueue(TEXT("самотест окна рыбалки: H3"));
	if (UAsdaItemTooltipSubsystem* Tip = GI->GetSubsystem<UAsdaItemTooltipSubsystem>())
	{
		Win->OnHoverControl(71329);
		const bool bShown = Tip->GetTipOwner() == EAsdaTipOwner::FishWin;
		Win->OnHoverControl(0);
		Expect(bShown && Tip->GetTipOwner() != EAsdaTipOwner::FishWin, TEXT("H3 наведение 71329 → общая подсказка (111096/111065), уход — скрыта"));
	}

	// ════════ K. ИСПРАВЛЕНИЯ ПО РЕВИЗИИ В2 (волна В4, 08.10.2026) ════════
	// Гайд в состоянии ввода стрелок посреди раунда (окно открыто).
	auto EnterState4 = [this]()
	{
		WM->ClearMsgBoxQueue(TEXT("самотест окна рыбалки: K"));
		F->FishState = 115;
		Win->FishWinShow();
		Win->Guide.State = 4;
		Win->Guide.Recs.Reset();
		for (int32 j = 0; j < 4; ++j) { FAsdaFishKeyRec R; R.Vk = 38; Win->Guide.Recs.Add(R); }
		Win->PanelSetVisible(true);
	};
	// K1 (МЕЛК-1): сброс сессии (смена карты) посреди раунда — окно скрыто, панель скрыта, гайд не в состоянии ввода.
	EnterState4();
	F->ResetSessionState(EAsdaSessionResetReason::MapChanged);
	Expect(!Win->IsOpen() && !Win->IsPanelShown() && Win->GetGuideState() == 0 && !Win->IsGuideInputState(),
		TEXT("K1 сброс сессии при открытом окне и гайде 4: окно и панель скрыты, гайд 0 (ревизия В2 МЕЛК-1)"));
	// K2/K3 (КРИТ-1): отмена в состоянии 4 → X → диалог 80 OK → 6171(2) без флага → 128 → 0 → клавиатура жива.
	IConsoleVariable* CvStale = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Fish.LegacyStaleGuide"));
	for (int32 Legacy = 0; Legacy <= 1; ++Legacy)
	{
		if (CvStale) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.LegacyStaleGuide"), Legacy); }
		EnterState4();
		Win->OnCancelPressed();
		const bool bDlg = WM->IsMsgBoxQueued(1, 80);
		WM->CloseConfirm(true);   // «Да» → 6171(2); флага рыбалки нет → сразу 128
		Step(); Step();
		const bool bAteW = FilterOnly(EKeys::W, IE_Pressed);
		FilterOnly(EKeys::W, IE_Released);
		if (Legacy == 0)
		{
			Expect(bDlg && F->GetFishState() == 0 && Win->GetGuideState() == 0 && !bAteW,
				FString::Printf(TEXT("K2 КРИТ-1 [ИСПРАВЛЕНИЕ]: отмена в state 4 → 80 OK → 128 → 0 (автомат %d), гайд %d, W НЕ съедена — клавиатура жива"),
					F->GetFishState(), Win->GetGuideState()));
		}
		else
		{
			Expect(bDlg && F->GetFishState() == 0 && Win->GetGuideState() == 4 && bAteW,
				FString::Printf(TEXT("K3 контроль ключа asda.Fish.LegacyStaleGuide 1 — как оригинал: гайд остался %d, W съедена"), Win->GetGuideState()));
		}
		Win->Guide.State = 0;
	}
	if (CvStale) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.LegacyStaleGuide"), 0); }
	// K4 (ВАЖН-1): Alt «нажат» событием, отпускание потеряно (Alt+Tab: сброс клавиш мимо фильтра) → стрелка в state 4 засчитана.
	EnterState4();
	FilterOnly(EKeys::LeftAlt, IE_Pressed);
	PC->FlushPressedKeys();
	FilterOnly(EKeys::Up, IE_Pressed);
	Expect(Win->Guide.Recs.Num() > 0 && Win->Guide.Recs[0].bAnswered && Win->Guide.Recs[0].bCorrect,
		TEXT("K4 ВАЖН-1: после «Alt нажат + потеря фокуса» стрелка в state 4 засчитана (признак Alt — живой модификатор Slate)"));
	Win->PanelHideAndClear();
	Win->OnFishSessionReset(false);
	F->FishState = 0;
	return true;
}

// Этап 2: путь игрока — окно, панель на 4 ячейки, шкала; геометрия через кадр отрисовки.
void FAsdaFishWinSelfTest::RunGeometry()
{
	UUserWidget* Root = Win->Widget;
	UWidget* RootPlane = Root ? AsdaUi::FindNodeByName(Root, TEXT("FISH_WIN_0_0"), nullptr) : nullptr;
	FVector2D RMn(0, 0), RMx(0, 0);
	const bool bRoot = RootPlane && AsdaUi::WidgetAbsRect(RootPlane, RMn, RMx);
	UE_LOG(LogAsdaFishWinTest, Log, TEXT("\n%s\n%s"), *Win->DumpControls(), *Win->DumpState());
	FVector2D VMn(0, 0), VSz(0, 0);
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetGameViewportWidget().IsValid())
	{
		const FGeometry G = GEngine->GameViewport->GetGameViewportWidget()->GetCachedGeometry();
		VMn = G.GetAbsolutePosition(); VSz = G.GetAbsoluteSize();
	}
	const FVector2D VC = VMn + VSz * 0.5f;
	Expect(bRoot && FVector2D::Distance((RMn + RMx) * 0.5f, VC) < 2.f,
		FString::Printf(TEXT("I1 корень FISH_WIN по центру вьюпорта: центр (%.1f,%.1f), вьюпорт (%.1f,%.1f) %.0f×%.0f"),
			(RMn.X + RMx.X) * 0.5f, (RMn.Y + RMx.Y) * 0.5f, VC.X, VC.Y, VSz.X, VSz.Y));
	FVector2D PMn(0, 0), PMx(0, 0);
	const bool bPanel = AsdaUi::WidgetAbsRect(Win->PanelPlane, PMn, PMx);
	const float Scale = (RMx.X - RMn.X) / 272.f;   // DPI-масштаб: корень 272×272
	Expect(bPanel && FMath::Abs((PMn.X + PMx.X) * 0.5f - (RMn.X + RMx.X) * 0.5f) <= 1.5f * FMath::Max(1.f, Scale),
		FString::Printf(TEXT("I2 панель 71310 (4 ячейки) по центру окна: центр X %.1f, окно %.1f (масштаб %.3f)"), (PMn.X + PMx.X) * 0.5f, (RMn.X + RMx.X) * 0.5f, Scale));
	bool bDisjoint = true, bInside = true, bOx = true;
	TArray<FBox2D> R;
	for (int32 j = 0; j < 4; ++j)
	{
		FVector2D Mn(0, 0), Mx(0, 0), IMn(0, 0), IMx(0, 0), OMn(0, 0), OMx(0, 0);
		AsdaUi::WidgetAbsRect(Win->Cells[j].Btn.Get(), Mn, Mx);
		AsdaUi::WidgetAbsRect(Win->Cells[j].Icon, IMn, IMx);
		R.Add(FBox2D(Mn, Mx));
		bInside &= IMn.X >= Mn.X - 0.5f && IMx.X <= Mx.X + 0.5f && IMn.Y >= Mn.Y - 0.5f && IMx.Y <= Mx.Y + 0.5f && Mx.X > Mn.X;
		bInside &= IMn.X >= PMn.X - 0.5f && Mx.X <= PMx.X + 0.5f;   // ячейка внутри рамки 71310
		const UImage* Shown = Vis(Win->Cells[j].O) ? Win->Cells[j].O.Get() : Win->Cells[j].X.Get();
		if (AsdaUi::WidgetAbsRect(Shown, OMn, OMx))
		{
			bOx &= OMn.X <= IMn.X + 0.5f && OMx.X >= IMx.X - 0.5f && OMn.Y <= IMn.Y + 0.5f && OMx.Y >= IMx.Y - 0.5f;
		}
		else { bOx = false; }
	}
	for (int32 a = 0; a < R.Num(); ++a)
	{
		for (int32 b = a + 1; b < R.Num(); ++b)
		{
			bDisjoint &= R[a].Max.X <= R[b].Min.X + 0.01f || R[b].Max.X <= R[a].Min.X + 0.01f || R[a].Max.Y <= R[b].Min.Y + 0.01f || R[b].Max.Y <= R[a].Min.Y + 0.01f;
		}
	}
	Expect(bDisjoint, TEXT("I3 4 ячейки BLUE_BIG_BTN не пересекаются"));
	Expect(bInside, TEXT("I4 кадры стрелок внутри своих ячеек, ячейки внутри рамки 71310"));
	Expect(bOx, TEXT("I5 видимый клон O/X накрывает кадр своей ячейки (Origin −15,−13, 60×60)"));
	FVector2D BMn(0, 0), BMx(0, 0), AMn(0, 0), AMx(0, 0);
	const bool bBar = AsdaUi::WidgetAbsRect(Win->GaugeBar.Back, BMn, BMx);
	const bool bAni = AsdaUi::WidgetAbsRect(Win->FishAni, AMn, AMx);
	Expect(bBar && bAni && AMn.Y < BMx.Y && AMx.Y > BMn.Y, TEXT("I6 рыбка 71326 на полосе шкалы 71305 (по высоте)"));
	{
		// ★ Ревизия В2 МЕЛК-2 (волна В4): X рыбки — ЖИВОЙ геометрией, а не формулой от той же базы: при шкале 250
		//   L(71326) − L(71304) = (int)(786/500·250 − 56) + 8 = 345 (× масштаб): `SetField74_75(71326, x)` ставит x от начала 71304.
		//   Начало 71304 — через план полосы 71305 (`.JUI`: Origin 7,6 внутри 71304): у группы импортёра `NoImage_grp_1`
		//   собственный прямоугольник шире плана 71304 (замер: группа 40, полоса 52 ⇒ 71304 = 45), по нему мерить нельзя.
		const float L71304 = (float)BMn.X - 7.f * Scale;
		const float Want = (float)((int32)(786.0 / 500.0 * 250.0 - 56.0) + 8) * Scale;
		Expect(bBar && bAni && FMath::Abs((AMn.X - L71304) - Want) <= 1.5f * FMath::Max(1.f, Scale),
			FString::Printf(TEXT("I8 рыбка при шкале 250: L(71326) − L(71304) = %.1f (ждём %.1f = 345 × %.3f; L(71304) = L(71305) − 7)"), AMn.X - L71304, Want, Scale));
	}
	FVector2D XMn(0, 0), XMx(0, 0);
	Expect(AsdaUi::WidgetAbsRect(Win->BtnX.Get(), XMn, XMx) && AsdaUi::WidgetAbsRect(Win->ChkPause.Get(), XMn, XMx) && AsdaUi::WidgetAbsRect(Win->ChkAuto.Get(), XMn, XMx),
		TEXT("I7 кнопки X / пауза / автоповтор имеют живую геометрию"));
}

void FAsdaFishWinSelfTest::PrepareGeometry()
{
	F->FishState = 115;
	Win->FishWinShow();
	Win->Guide.State = 4;
	Win->Guide.Recs.Reset();
	static const int32 kVk[4] = { 38, 37, 40, 39 };   // ↑ ← ↓ →
	for (int32 j = 0; j < 4; ++j)
	{
		FAsdaFishKeyRec R;
		R.Vk = kVk[j];
		R.bAnswered = true;
		R.bCorrect = (j % 2) == 0;
		Win->Guide.Recs.Add(R);
	}
	Win->Guide.RoundLimitMs = 4000;
	Win->PanelSetVisible(true);
	Win->UpdateTimer(1500);
	F->SetGaugeValue(250);
}

void FAsdaFishWinSelfTest::AfterGeometry()
{
	Win->PanelHideAndClear();
	F->FishState = 0;
}

void FAsdaFishWinSelfTest::Restore()
{
	if (!F || !Win) { AsdaCVarSave::RestoreAll(TEXT("asda.Fish.WinSelfTest")); return; }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи — и на этом выходе
	for (const TPair<int32, int32>& C : UsedCells) { if (Inv) { Inv->DebugPutItem(C.Key, C.Value, 0, 0); } }
	F->ResetSessionState(EAsdaSessionResetReason::Disconnected);
	F->TestClockOffsetMs = 0;
	F->bTestNoNetDrain = false;
	if (bCapture && Pet) { Pet->EndSendCapture(); }
	if (Net) { AsdaTestNet::SelectedCharId(Net) = CharIdWas; AsdaTestNet::SelfUserNum(Net) = UserNumWas; AsdaTestNet::bSelfUserNumKnown(Net) = bKnownWas; }
	if (LM) { LM->CurrentMapId = MapWas; }
	if (Ch) { Ch->NetSnap(PosWas); }
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи прогона (Fish.ClipBridge, Fish.LegacyAutoCheck, Fish.LegacyStaleGuide) — к исходному значению
	//   И приоритету SetBy (прежде мост — прежним значением с SetByConsole, остальные — константой 0).
	AsdaCVarSave::RestoreAll(TEXT("asda.Fish.WinSelfTest"));
	if (WM) { WM->ClearMsgBoxQueue(TEXT("самотест окна рыбалки: конец")); }
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaFishWinSelfTest(
	TEXT("asda.Fish.WinSelfTest"),
	TEXT("asda.Fish.WinSelfTest [delay=N] [quit] — ОФЛАЙН-самотест окна рыбалки 71300 и мини-игры (волна В2): полный цикл через ")
	TEXT("HandlePacket, ввод клавиш через PlayerController::InputKey, 6169 байт в байт, шкала/рыбка, всплывашка, отсчёт, кнопки, ")
	TEXT("контрольные пары, затем живая геометрия (GetCachedGeometry)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Fish.WinSelfTest"));   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернёт сторож
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Fish.WinSelfTest"), KitArgs)) { return; }
		if (!W) { return; }
		float Delay = 0.f;   // задержка запуска, с
		bool bQuit = false;  // выйти после прогона
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		}
		Delay = AsdaSelfTestKit::DelayArgOr(A, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
		TWeakObjectPtr<UWorld> WW = W;
		auto Go = [WW, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd) { return; }
			TSharedPtr<FAsdaFishWinSelfTest> T = MakeShared<FAsdaFishWinSelfTest>();
			const bool bLogic = T->RunLogic(Wd);
			auto Finish = [T, WW, bQuit]()
			{
				T->Restore();
				UE_LOG(LogAsdaFishWinTest, Log, TEXT("САМОТЕСТ-ОКНО-РЫБЫ ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
				AsdaSelfTestKit::Quit(WW.Get(), bQuit);
			};
			if (!bLogic) { Finish(); return; }
			// Этап 2: окно с 4 ячейками (две O, две X), шкала 250 — ждём отрисовку и снимаем геометрию.
			T->PrepareGeometry();
			FTimerHandle H;
			Wd->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([T, Finish]()
			{
				T->RunGeometry();
				T->AfterGeometry();
				Finish();
			}), 1.0f, false);
		};
		// Свой персонаж офлайн — штатные крюки (как самотест В1) у первого спота Альпии, затем прогон через 3 с.
		auto SpawnThenGo = [WW, Go, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(Wd, TEXT("asda.Fish.WinSelfTest"), bQuit)) { return; }   // Р-2: офлайн повторно — на отложенном старте
			APlayerController* P = Wd->GetFirstPlayerController();
			if ((!P || !P->GetPawn()) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 139 77"));
				FTimerHandle H2;
				Wd->GetTimerManager().SetTimer(H2, FTimerDelegate::CreateLambda(Go), 3.f, false);
				return;
			}
			Go();
		};
		if (Delay <= 0.f) { SpawnThenGo(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(SpawnThenGo), Delay, false);
	}));

#endif // !UE_BUILD_SHIPPING
