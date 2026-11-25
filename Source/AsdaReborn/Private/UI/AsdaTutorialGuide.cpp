// Copyright (c) 2026 @sitxovski (Discord)
// AsdaTutorialGuide.cpp — ОБУЧЕНИЕ, ЧАСТЬ А: пошаговый гид с роботом (окно `Tuto_Msg_Box_4` 1950). [ОРИГИНАЛ]
//   Тик — порт `uiTutorialWindowUpdate@0xEBBD40` (ui_core.c:85531), кнопки — `boosterShopOnMessage_D020@0xEBD020`
//   (МИСНЕЙМ), эффект шага — `Player_SpawnRareBoostEffect@0xEBD130` (МИСНЕЙМ `tagBoosterShop_PlayRareEffect`).
//   Таблица шагов и флагов — `Docs/Port/systems/TUTORIAL.md` §2.3; тексты loc 103000+шаг (Приложение А).
#include "UI/AsdaTutorialSubsystem.h"
#include "UI/AsdaUiHelpers.h"   // ★ R1: прямое включение (раньше приходило транзитивно)

#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaZoneSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTutorialGuide, Log, All);

namespace AsdaTutorialGuide
{
	static const FName kWindowId(TEXT("Tuto_Msg_Box_4"));   // 1950 — ключ окна в реестре окон
	static const TCHAR* kWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Tutorial/WBP_Tuto_Msg_Box_4.WBP_Tuto_Msg_Box_4_C");   // ассет окна
	constexpr int32 kZ = 720;                // слой окна гида (уровень обычных окон)
	constexpr int32 kLastStep = 22;          // последний шаг («Пока»)
	constexpr int32 kLocStepBase = 103000;   // текст шага = 103000 + шаг
	constexpr int32 kLocNext = 102900;       // «Вперёд» — подпись 1952 при сборке окна и после шага 9
	constexpr int32 kLocNo = 102901;         // «Нет» — подпись 1953
	constexpr int32 kLocYes = 102902;        // «Да» — шаги 1 и 8
	constexpr int32 kLocBye = 102903;        // «Пока» — шаг 22
	constexpr int32 kLocGoNorth = 102904;    // «Отправляйся на север Альпии» — шаг 14
	constexpr double kTypeMs = 30.0;         // `timeGetTime() - g_dwAutoActionLastTick > 0x1E`
	constexpr int32 kTypeChars = 2;          // `+1200 += 2`
	constexpr double kFireMs = 1000.0;       // `timeGetTime() - g_dwAutoActionTime > 0x3E8`
	constexpr float kMoveDistSq = 300.f;     // сдвиг игрока: `|pos − start|² > 300`
	constexpr float kDragDistSq = 100.f;     // протяжка ПКМ: `|cursor − anchor|² > 100`
	constexpr int32 kGoNorthTileX = 112;     // `mapClick_BeginPathMove(…, 112, 235)` — шаг 14
	constexpr int32 kGoNorthTileY = 235;
}

// Часы гида в миллисекундах (`timeGetTime` эталона).
static double AsdaGuideNowMs() { return FPlatformTime::Seconds() * 1000.0; }

// ══════════════════════════════ Старт / закрытие ══════════════════════════════
void UAsdaTutorialSubsystem::OnWorldReady()
{
	using namespace AsdaTutorialGuide;
	// game_main.c:3178-3184 (этап загрузки 11): `if (уровень своего == 1) { [0] = 1; uiWindowShowRegister(); }`.
	//   Этап 11 есть только у ЗАГРУЗКИ КАРТЫ: 4056 (вход/переход) и 4055 на другую карту. 4055 без смены карты
	//   (воскрешение на месте) загрузку не запускает — гид, закрытый крестиком, не возвращается.
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bMapLoad = !Net || Net->LastWorldReadyOpcode != 4055 || Net->bLastWorldReadyMapChange;
	if (!bMapLoad) { return; }
	// [ИСПРАВЛЕНИЕ] После «Пока» шаг = 23 и у эталона живёт до новой сборки интерфейса: на уровне 1 следующая карта
	//   открывала пустое облачко (строки loc 103023 нет). Пройденный гид не поднимаем.
	if (GuideStep > kLastStep) { return; }
	// [ИСПРАВЛЕНИЕ] Локальный прогресс персонажа (AsdaTutorialStore.cpp): шаг продолжается с сохранённого, а гид,
	//   пройденный («Пока») или закрытый («Нет»/крестик), на входе больше не поднимается. Оригинал — каждый вход с шага 1.
	EnsureProgressLoaded();
	if (bGuideDismissed || GuideStep > kLastStep) { return; }
	if (AsdaSelf::GetLevel(GI) == 1) { StartGuide(TEXT("загрузка карты на уровне 1")); }
}

void UAsdaTutorialSubsystem::StartGuide(const TCHAR* Why)
{
	if (!EnsureGuideWidget()) { return; }
	bGuideOn = true;
	GuideWin->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	GuideMouse.Reset();
	UE_LOG(LogAsdaTutorialGuide, Log, TEXT("[гид] открыт на шаге %d (%s)"), GuideStep, Why);
}

void UAsdaTutorialSubsystem::CloseGuide(const TCHAR* Why)
{
	// `*self = 0` → в следующем тике `sub_EBBD00`: спрятать окно, если показано.
	if (!bGuideOn) { return; }
	bGuideOn = false;
	if (GuideWin) { GuideWin->SetVisibility(ESlateVisibility::Collapsed); }
	bGuidePending = bGuideFire = false;
	UE_LOG(LogAsdaTutorialGuide, Log, TEXT("[гид] закрыт на шаге %d (%s)"), GuideStep, Why);
}

bool UAsdaTutorialSubsystem::EnsureGuideWidget()
{
	using namespace AsdaTutorialGuide;
	UUserWidget* W = EnsureWindow(kWindowId, kWbp, kZ, GuideWin);
	if (!W) { return false; }
	if (GuideBound.Get() == W) { return true; }
	GuideBound = W;
	// Контролы 1951…1955 (init `0xEBB960`): 1952 «Вперёд» (102900), 1953 «Нет» (102901).
	UAsdaRichText::CaptureFmt(GuideTxtFmt, Cast<UTextBlock>(AsdaUi::FindNodeExact(W, TEXT("Tuto4_MsgTxt"))));
	GuideOkBtn = Cast<UButton>(AsdaUi::FindNodeExact(W, TEXT("Tuto4_OKBtn")));
	GuideNoBtn = Cast<UButton>(AsdaUi::FindNodeExact(W, TEXT("Tuto4_CanBtn")));
	GuideCloseBtn = Cast<UButton>(AsdaUi::FindNodeExact(W, TEXT("Tuto_Msg_Box_4_BTN00")));   // 1955 (шаг S2 импорта)
	GuideOkTxt = Cast<UTextBlock>(AsdaUi::FindNodeExact(W, TEXT("Tuto4_OKBtn_Text")));
	GuideNoTxt = Cast<UTextBlock>(AsdaUi::FindNodeExact(W, TEXT("Tuto4_CanBtn_Text")));
	GuideImg = Cast<UImage>(AsdaUi::FindNodeExact(W, TEXT("Tuto4_Img_5_0")));
	if (GuideOkBtn.IsValid()) { GuideOkBtn->OnClicked.AddUniqueDynamic(this, &UAsdaTutorialSubsystem::OnGuideOkClicked); }
	if (GuideNoBtn.IsValid()) { GuideNoBtn->OnClicked.AddUniqueDynamic(this, &UAsdaTutorialSubsystem::OnGuideNoClicked); }
	if (GuideCloseBtn.IsValid()) { GuideCloseBtn->OnClicked.AddUniqueDynamic(this, &UAsdaTutorialSubsystem::OnGuideCloseClicked); }
	if (GuideNoTxt.IsValid()) { GuideNoTxt->SetText(FText::FromString(FAsdaMarkup::StripTags(LocText(kLocNo)))); }
	if (!GuideOkBtn.IsValid() || !GuideCloseBtn.IsValid())
	{
		UE_LOG(LogAsdaTutorialGuide, Warning, TEXT("[гид] в WBP_Tuto_Msg_Box_4 нет кнопок 1952/1955 — проверьте шаг импорта S2"));
	}
	W->SetVisibility(bGuideOn ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	// Окно новое (травел снёс прежнее): у эталона окно живёт, шаг и флаги событий не сбрасываются — возвращаем только
	//   картинку шага (текст допечатается сам: `LastRaw` нового узла пуст) и скрываем кнопки (шаги 1/8/14/22 покажут их сами).
	GuideSetButtons(false, false, 0);
	if (GuideShownStep == GuideStep) { SetImageTexture(GuideImg.Get(), FString::Printf(TEXT("tutorial_%d"), GuideStep), false); }
	return true;
}

void UAsdaTutorialSubsystem::GuideSetButtons(bool bOk, bool bNo, int32 OkLoc)
{
	if (GuideOkBtn.IsValid())
	{
		GuideOkBtn->SetVisibility(bOk ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (GuideOkTxt.IsValid())
		{
			GuideOkTxt->SetVisibility(bOk ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			if (OkLoc > 0) { GuideOkTxt->SetText(FText::FromString(FAsdaMarkup::StripTags(LocText(OkLoc)))); }
		}
	}
	if (GuideNoBtn.IsValid())
	{
		GuideNoBtn->SetVisibility(bNo ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (GuideNoTxt.IsValid()) { GuideNoTxt->SetVisibility(bNo ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	}
}

// ══════════════════════════════ Кнопки (`boosterShopOnMessage_D020@0xEBD020`) ══════════════════════════════
void UAsdaTutorialSubsystem::OnGuideOkClicked() { GuideOnOk(); }
void UAsdaTutorialSubsystem::OnGuideNoClicked() { GuideOnNo(); }
void UAsdaTutorialSubsystem::OnGuideCloseClicked() { GuideOnClose(); }

void UAsdaTutorialSubsystem::GuideOnOk()
{
	using namespace AsdaTutorialGuide;
	if (!bGuideOn) { return; }
	if (GuideStep == 14)
	{
		// «Отправляйся на север Альпии»: автопуть к клетке (112, 235) своей карты, шаг не меняется.
		UGameInstance* GI = GetGameInstance();
		if (UAsdaMinimapSubsystem* Mm = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr)
		{
			Mm->SetDestinationTile(kGoNorthTileX, kGoNorthTileY, true);
		}
		if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(UGameplayStatics::GetPlayerController(GI ? GI->GetWorld() : nullptr, 0)))
		{
			PC->MoveToGamePoint(FVector2D(100.f * kGoNorthTileX + 50.f, 100.f * kGoNorthTileY + 50.f));
		}
		UE_LOG(LogAsdaTutorialGuide, Log, TEXT("[гид] шаг 14: автопуть к клетке (%d, %d)"), kGoNorthTileX, kGoNorthTileY);
		return;
	}
	if (++GuideStep >= kLastStep)
	{
		GuideSetButtons(true, false, 0);   // `CControl_SetVisible(+5, 1)`
		GuideDismiss(TEXT("кнопка «Пока»"));
		GuideAdvanceFx();
	}
}

void UAsdaTutorialSubsystem::GuideOnNo()
{
	GuideFlags[73] = 1;   // 1953: `*((_BYTE *)this + 73) = 1` — гид закроется в тике шагов 1/8
}

void UAsdaTutorialSubsystem::GuideOnClose()
{
	GuideDismiss(TEXT("крестик 1955"));
}

void UAsdaTutorialSubsystem::GuideAdvanceFx()
{
	// `Player_SpawnRareBoostEffect@0xEBD130`: при `+1206` — `eff_lucky_a_01.jeb` на кости M_DUMMY/G_DUMMY и
	//   `s_item_boostrare.wav`; флаг гасится (до следующей смены шага). Тот же эффект, что у бустера.
	if (!bGuideFx) { return; }
	bGuideFx = false;
	UGameInstance* GI = GetGameInstance();
	const int32 SelfIdx = AsdaSelf::GetUserIdxOr(GI, INDEX_NONE);
	if (UAsdaEffectsSubsystem* Eff = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr)
	{
		Eff->SpawnBoostFx(SelfIdx, /*bRare=*/true);
	}
}

void UAsdaTutorialSubsystem::GuideAdvance()
{
	// Общая ветка засчёта: `g_bAutoActionFire = 0`, скрыть 1952, эффект, шаг+1.
	bGuideFire = false;
	GuideSetButtons(false, false, 0);
	GuideAdvanceFx();
	++GuideStep;
}

// ══════════════════════════════ Смена шага ══════════════════════════════
void UAsdaTutorialSubsystem::GuideEnterStep()
{
	using namespace AsdaTutorialGuide;
	GuideShownStep = GuideStep;
	FMemory::Memzero(GuideFlags + 40, 60);   // `memset(+40, 0, 0x3C)`
	if (const AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(UGameplayStatics::GetPlayerPawn(GetGameInstance()->GetWorld(), 0)))
	{
		GuideStartPos = Ch->GetGamePosXZ();
	}
	GuideText = LocText(kLocStepBase + GuideStep);
	GuideTextLen = GuideText.Len();
	GuideTextShown = 0;
	GuideSetButtons(false, false, 0);
	// Картинка 1954: сначала пустая, затем `Tutorial_<шаг>.TEX` 128×128 (у шагов 1, 8, 19, 22 файла нет — пусто).
	SetImageTexture(GuideImg.Get(), FString::Printf(TEXT("tutorial_%d"), GuideStep), false);
	bGuidePending = false;
	bGuideFx = true;
	bGuideFire = false;
	bGuideTextRevealed = false;
	UE_LOG(LogAsdaTutorialGuide, Log, TEXT("[гид] шаг %d"), GuideStep);
	SaveProgress(TEXT("шаг гида"));   // [ИСПРАВЛЕНИЕ] следующий вход продолжит с этого шага
}

// ══════════════════════════════ Тик (`uiTutorialWindowUpdate@0xEBBD40`) ══════════════════════════════
void UAsdaTutorialSubsystem::GuideTick(float Dt)
{
	using namespace AsdaTutorialGuide;
	if (!bGuideOn)
	{
		if (GuideWin && GuideWin->GetVisibility() != ESlateVisibility::Collapsed) { GuideWin->SetVisibility(ESlateVisibility::Collapsed); }
		return;
	}
	if (!IsInGameWorld() || !EnsureGuideWidget()) { return; }
	UGameInstance* GI = GetGameInstance();
	// Пока идёт разговор с NPC (`g_bInventoryLocked` = `g_pNpcWnd`+0x400C), окно скрыто.
	const UAsdaNpcDialogSubsystem* Dlg = GI->GetSubsystem<UAsdaNpcDialogSubsystem>();
	const bool bTalk = Dlg && Dlg->IsDialogOpen();
	bool bShow = !bTalk;

	// Клетка города (бит 0x80): 95 — в городе, 94 — вне его.
	const UAsdaZoneSubsystem* Zone = GI->GetSubsystem<UAsdaZoneSubsystem>();
	const bool bTown = Zone && Zone->IsLocalPlayerInPeaceZoneNow();
	GuideFlags[95] = bTown ? 1 : 0;
	GuideFlags[94] = bTown ? 0 : 1;

	// Кнопки мыши — `GetAsyncKeyState` (поверх интерфейса тоже).
	GuideMouse.bIgnoreUiHidden = true;
	GuideMouse.Update();
	if (GuideMouse.bLmb) { GuideFlags[44] = 1; }
	if (!bRmbAnchorSet) { bRmbAnchorSet = true; RmbAnchor = GuideMouse.Cursor; }
	if (GuideMouse.bRmb)
	{
		GuideFlags[45] = 1;
		if (FVector2D::DistSquared(GuideMouse.Cursor, RmbAnchor) > kDragDistSq) { GuideFlags[46] = 1; }
	}
	else
	{
		RmbAnchor = GuideMouse.Cursor;
	}

	if (GuideShownStep != GuideStep) { GuideEnterStep(); }

	// Сдвиг игрока от точки смены шага.
	if (const AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(UGameplayStatics::GetPlayerPawn(GI->GetWorld(), 0)))
	{
		if (FVector2D::DistSquared(Ch->GetGamePosXZ(), GuideStartPos) > kMoveDistSq) { GuideFlags[82] = 1; }
	}

	// Печать текста: +2 символа каждые 30 мс; дошли до конца — «текст показан».
	const double Now = AsdaGuideNowMs();
	if (Now - GuideTypeLastMs > kTypeMs)
	{
		GuideTypeLastMs = Now;
		GuideTextShown += kTypeChars;
		if (GuideTextShown > GuideTextLen)
		{
			GuideTextShown = GuideTextLen;
			if (!bGuideTextRevealed) { bGuideTextRevealed = true; bGuideFire = false; }
		}
	}
	if (bGuidePending && Now - GuidePendingMs > kFireMs) { bGuideFire = true; }
	{
		// [ПОРТ] Эталон режет строку по байтам; последовательность «\n» и теги `<…>` здесь не режутся пополам
		//   (иначе на кадр печати мелькал бы «\» или сырой тег) — показ символов тот же.
		int32 Cut = FMath::Clamp(GuideTextShown, 0, GuideTextLen);
		if (Cut > 0 && Cut < GuideTextLen && GuideText[Cut - 1] == TEXT('\\')) { ++Cut; }
		const int32 Lt = GuideText.Left(Cut).Find(TEXT("<"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (Lt != INDEX_NONE && GuideText.Left(Cut).Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromEnd) < Lt)
		{
			const int32 Gt = GuideText.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Lt);
			Cut = (Gt != INDEX_NONE) ? Gt + 1 : Cut;
		}
		const FString Shown = GuideText.Left(Cut);
		if (GuideTxtFmt.LastRaw != Shown) { UAsdaRichText::SetFmt(GuideTxtFmt, Shown); }
	}

	auto Arm = [this, Now](bool bCond)
	{
		if (bCond && !bGuidePending) { bGuidePending = true; GuidePendingMs = Now; }
	};
	auto CloseBigMap = [GI]()
	{
		// `miniMapWndClose(g_pMiniMapWnd)` + `Cursor_SetByIndex(0)` — курсор карты сбрасывает сама карта при закрытии.
		if (UAsdaMinimapSubsystem* Mm = GI->GetSubsystem<UAsdaMinimapSubsystem>())
		{
			if (Mm->IsBigMapOpen()) { Mm->SetBigMapVisible(false); }
		}
	};
	const uint8* F = GuideFlags;
	switch (GuideStep)
	{
	case 1:
	case 8:
		// «Хочешь потренироваться?» / «Нужна помощь ещё?»: «Да» (102902) → шаг+1, «Нет» (флаг 73) → закрыть.
		GuideSetButtons(true, true, kLocYes);
		if (F[73]) { GuideDismiss(TEXT("«Нет»")); return; }
		break;
	case 2: Arm(F[82] && F[44]); if (bGuideFire) { GuideAdvance(); } break;
	case 3: Arm(F[82] && F[40]); if (bGuideFire) { GuideAdvance(); } break;
	case 4: Arm(F[82] && F[40] && F[42] && F[43]); if (bGuideFire) { GuideAdvance(); } break;
	case 5: Arm(F[46] != 0); if (bGuideFire) { GuideAdvance(); } break;
	case 6:
		Arm(F[82] && F[46]);
		if (bGuideFire)
		{
			GuideAdvance();
			// Шаг 7 (колесо) пропускается, если у мыши меньше трёх кнопок (`GetSystemMetrics(SM_CMOUSEBUTTONS)`).
			int32 Buttons = 3;
#if PLATFORM_WINDOWS
			Buttons = ::GetSystemMetrics(SM_CMOUSEBUTTONS);
#endif
			if (Buttons < 3) { ++GuideStep; GuideSetButtons(false, false, 0); }
		}
		break;
	case 7: Arm(F[47] != 0); if (bGuideFire) { GuideAdvance(); } break;
	case 9:
		Arm(F[78] != 0);
		if (bGuideFire) { GuideSetButtons(false, false, kLocNext); GuideAdvance(); }
		break;
	case 10:
		Arm(F[82] && F[74]);
		if (bGuideFire) { GuideAdvance(); CloseBigMap(); }
		break;
	case 11:
		if (F[83]) { bShow = false; }
		Arm(F[77] && F[84]);
		if (bGuideFire) { GuideAdvance(); bShow = !bTalk; }
		break;
	case 12: Arm(F[92] != 0); if (bGuideFire) { GuideAdvance(); } break;
	case 13: Arm(F[93] != 0); if (bGuideFire) { GuideAdvance(); } break;
	case 14:
		GuideSetButtons(true, false, kLocGoNorth);
		Arm(F[94] != 0);
		if (bGuideFire) { GuideAdvance(); }
		break;
	case 15: Arm(F[85] != 0); if (bGuideFire) { GuideAdvance(); } break;
	case 16: Arm(F[75] != 0); if (bGuideFire) { GuideAdvance(); } break;
	case 17: Arm(F[80] != 0); if (bGuideFire) { GuideAdvance(); } break;
	case 18: Arm(F[79] != 0); if (bGuideFire) { GuideAdvance(); } break;
	case 19: Arm(F[96] != 0); if (bGuideFire) { GuideAdvance(); } break;
	case 20:
		if (F[74]) { CloseBigMap(); }
		Arm(F[95] != 0);
		if (bGuideFire) { GuideAdvance(); }
		break;
	case 21:
		if (F[83]) { bShow = false; }
		Arm(F[81] && F[84]);
		if (bGuideFire) { GuideAdvance(); bShow = !bTalk; }
		break;
	case kLastStep:
		GuideSetButtons(true, false, kLocBye);
		break;
	default:
		break;
	}
	if (GuideWin && bGuideOn)
	{
		const ESlateVisibility Want = bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
		if (GuideWin->GetVisibility() != Want) { GuideWin->SetVisibility(Want); }
	}
}
