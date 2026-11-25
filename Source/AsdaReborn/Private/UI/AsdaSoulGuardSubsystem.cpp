// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaSoulGuardSubsystem — реализация системы «хранителя» (Soul Guardian / Soul Boost).
//   Полный разбор эталона, раскладки полей и источники КАЖДОГО числа — в шапке
//   Public/UI/AsdaSoulGuardSubsystem.h. Здесь только код, построчно повторяющий эталонные функции.
#include "UI/AsdaSoulGuardSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ U13 (В1 дедупа №2): AsdaNotice::ConsoleEcho — эхо консольных команд
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaUiHelpers.h"   // ★ Аудит №4 (B1): AsdaUi::SlotOf/SlotRect — общая точка слота канваса

#include "UI/AsdaMiniStateSubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"    // ★ ширина окна мини-карты для авто-якоря (эталон GetSize(g_pMiniMapWnd))
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaSkillUseSubsystem.h"   // ★ ОБЩИЙ диспетчер применения: ряды Хранителя ходят через него
#include "UI/AsdaSkillDatabase.h"       // ★ Аудит №3 (A13): имя иконки навыка — только отсюда
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaAnimatedPlane.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h" // ★ Аудит №3 (A3): SkillCoin и skilltooltip — только через менеджер
#include "World/AsdaKeybindConfig.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"   // ★ 3D-компаньон хранителя: наличие → высота плашки, каст → мотион
// ★ цель-МОБ как точка вылета монеты (FindNetMobByRid отдаёт AAsdaMobActor*)   ★ R3: комментарий снятого включения "Npc/AsdaMobActor.h" (файл им не пользовался) — сохранён
#include "AsdaNetSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/SlateApplication.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Text/AsdaLang.h"       // [ПОРТ] 09.10.2026 (мультиязычность): FAsdaLang::ResolveRow — имя хранителя на языке сессии
#include "AsdaSoulGuardSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSoulGuardSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaSoul);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

namespace SoulUi {
	// Индексы импортёра по рядам (i = 0,1,2) — сняты с самого WBP_MINISTATE_WIN.
	static const int32 BoxHit[3]      = { 164, 182, 201 };   // Soul_Boost_BOX_{i+1}_{idx}_HIT
	static const int32 Backg[3]       = { 165, 183, 202 };
	static const int32 BlackCircle[3] = { 166, 184, 203 };   // BLACKCIRCLE_{idx}
	static const int32 BallBack[3]    = { 167, 185, 204 };
	static const int32 DnHit[3]       = { 168, 190, 209 };   // Soul_Boost_D{i+1}_{idx}_HIT
	static const int32 Ball10[3]      = { 169, 191, 210 };   // D{i+1}_10 → далее _9.._1 идут idx+1..+9
	static const int32 Mark[3]        = { 179, 186, 205 };
	static const int32 Main[3]        = { 180, 187, 206 };
	static const int32 GText[3]       = { 181, 188, 207 };
	static const int32 Bolt[2]        = { 189, 208 };        // BOLT_{idx}: только для i = 0 и 1
	static const int32 MarkAni[3]     = { 226, 227, 228 };   // soul_boost_mark_ani_{i+1}_{idx}_0
	static const int32 BallAni[3]     = { 225, 229, 230 };   // soul_boost_ballani_{i+1}_{idx}_0
}

// ─────────────────────────────── маленькие помощники ────────────────────────────────────────────
// ★ Волна 1 (14.09.2026, В3): общая точка `AsdaUi::SetShownHit` — режим прежний (`Visible`, без повторной
//   установки того же режима).
void UAsdaSoulGuardSubsystem::SetVis(UWidget* W, bool bVisible)
{
	AsdaUi::SetShownHit(W, bVisible, /*bOnlyIfChanged*/ true);
}

// Показ/скрытие ГРУППЫ-контейнера: видимая группа обязана остаться SelfHitTestInvisible, иначе
//   прозрачный CanvasPanel начнёт съедать клики по миру (грабля оконной подсистемы).
void UAsdaSoulGuardSubsystem::SetGroupVis(UWidget* W, bool bVisible)
{
	if (!W) { return; }
	const ESlateVisibility V = bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	if (W->GetVisibility() != V) { W->SetVisibility(V); }
}

// Эталонный CControl_SetColor4(ctrl, alpha, plane) меняет АЛЬФУ вершин плана; в UMG это
//   RenderOpacity самого узла (плана) — визуально то же самое.
void UAsdaSoulGuardSubsystem::SetAlpha(UWidget* W, float A)
{
	if (!W) { return; }
	W->SetRenderOpacity(FMath::Clamp(A, 0.f, 1.f));
}

// Эталон CControl_Show@0x12E1350: сбрасывает таймеры плана и перезаряжает счётчик кадров, т.е.
//   ПЕРЕЗАПУСКАЕТ кадровую анимацию, и только потом SetVisible(1). Голый SetVisible анимацию не трогает.
void UAsdaSoulGuardSubsystem::ShowAndRestart(UWidget* W)
{
	if (!W) { return; }
	if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(W)) { P->RestartAnim(); }
	SetVis(W, true);
}

// Эталон CControl_CloseHide@0x12E1420: ОСТАНОВИТЬ анимацию, сбросить её в кадр 0, скрыть.
//   ★ Именно StopAnim, а не RestartAnim: `CloseHide` вызывается в том числе ИЗ обработчика события
//     «анимация завершена» (стрелка 56208, SOULBALL_ANI 56251..56253), и перезапуск там снова взвёл
//     бы тик — план крутился бы вечно на скрытом контроле.
void UAsdaSoulGuardSubsystem::CloseHide(UWidget* W)
{
	if (!W) { return; }
	if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(W)) { P->StopAnim(); }
	SetVis(W, false);
}

// `CKeybind_FormatHotkeyString@…` (input.c:4826-4873) 1:1: «<модификатор+><имя клавиши>»,
//   модификаторы 1/2/3 → "Shift+"/"Ctrl+"/"Alt+", имя клавиши — из карты VK→строка.
//   Привязку берём из живого UAsdaKeybindConfig игрока (дефолты + ini-оверрайды).
FString UAsdaSoulGuardSubsystem::FormatHotkeyLabel(int32 Cmd) const
{
	const UWorld* Wld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const AAsdaPlayerController* PC = Wld ? Cast<AAsdaPlayerController>(Wld->GetFirstPlayerController()) : nullptr;
	if (!PC || !PC->Keys) { return FString(); }
	const FAsdaKeyBind* B = PC->Keys->Binds.Find(Cmd);
	if (!B || B->Vk == 0) { return FString(); }
	FString Mod;
	switch (B->Mod)
	{
	case EAsdaKeyMod::Shift: Mod = TEXT("Shift+"); break;
	case EAsdaKeyMod::Ctrl:  Mod = TEXT("Ctrl+");  break;
	case EAsdaKeyMod::Alt:   Mod = TEXT("Alt+");   break;
	default: break;
	}
	const FKey K = UAsdaKeybindConfig::VkToFKey(B->Vk);
	return Mod + (K.IsValid() ? K.GetDisplayName(false).ToString() : FString::Printf(TEXT("VK%d"), B->Vk));
}

bool UAsdaSoulGuardSubsystem::GetSlotRect(const UWidget* W, FVector2D& OutPos, FVector2D& OutSize)
{
	// ★ Аудит №4 (B1): тело переехало в `AsdaUi::SlotRect` — здесь тонкий форвард.
	return AsdaUi::SlotRect(W, OutPos, OutSize);
}

void UAsdaSoulGuardSubsystem::ShiftGroup(UWidget* Group, const FVector2D& Delta)
{
	if (!Group || Delta.IsNearlyZero()) { return; }
	// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U15): SPAN-безопасный сдвиг группы (было `Left/Top += d`; у групп
	//   `Soul_Boost_Win_grp`/`Soul_Boost_Texts_grp` анкоры POINT — перепись якорей WBP_MINISTATE_WIN, результат прежний).
	AsdaUi::ShiftCanvasSlot(Group, Delta);
}

// ─────────────────────────────── жизненный цикл ─────────────────────────────────────────────────
void UAsdaSoulGuardSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaMiniStateSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaEffectsSubsystem::StaticClass());

	Coin.Init(0, 3); PrevCoin.Init(0, 3);
	RowSkillCode.Init(-1, 3); bRowAvailable.Init(false, 3); bMarkAniPending.Init(false, 3);
	BurstCache.Init(0, 3);
	// Эталонный список запрещённых типов карт (hudQuickSlot_TryUseSkill, ui_core.c:92694).
	BannedMapTypes = { 13, 14, 15, 24, 25, 27 };

	if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
	{
		Net->OnSoulSlotState.AddUObject(this, &UAsdaSoulGuardSubsystem::OnSlotState6157);
		Net->OnSoulGuardIdx.AddUObject(this, &UAsdaSoulGuardSubsystem::OnSoulGuardIndex6156);
		// ★ Сетевой тракт применения скилла хранителя (свой И ЧУЖИЕ) — 6159 / 6160 / 6161+5271.
		Net->OnSkillCast.AddUObject(this, &UAsdaSoulGuardSubsystem::OnSkillCast6159);
		Net->OnSkillCastIdle.AddUObject(this, &UAsdaSoulGuardSubsystem::OnSkillCastIdle6160);
		Net->OnSkillCastState.AddUObject(this, &UAsdaSoulGuardSubsystem::OnSkillCastState);
	}
	LoadTables();
	// ★ 10.10.2026 (язык на лету, до входа в мир): имена хранителей — заново на новом языке.
	FAsdaLang::OnLanguageChanged().AddWeakLambda(this, [this]() { bTablesLoaded = false; LoadTables(); });
}

void UAsdaSoulGuardSubsystem::Deinitialize()
{
	FAsdaLang::OnLanguageChanged().RemoveAll(this);   // ★ 10.10.2026: подписка «язык на лету»
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnSoulSlotState.RemoveAll(this);
			Net->OnSoulGuardIdx.RemoveAll(this);
			Net->OnSkillCast.RemoveAll(this);
			Net->OnSkillCastIdle.RemoveAll(this);
			Net->OnSkillCastState.RemoveAll(this);
		}
	}
	ResetControls();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaSoulGuardSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Плашки хранителей висят над АКТОРАМИ мира, а карта 6156 ключуется сессионными номерами.
	ResetControls();
	PeerBanners.Reset();
	SoulGuardIndexByUser.Reset();
	BannerAlpha = 0.f;
	bBannerFadeOut = false;
	ActiveBurstSlot = -1;
	// ★ ИСПРАВЛЕНО 2026-09-02 (падение при загрузке уровня). БЫЛО `BurstCache.Reset()` — это
	//   делает массив ПУСТЫМ, а он обязан всегда быть размера 3: `UpdateBanner` обходит ряды
	//   `for (i = 2; i >= 0; --i)` и читает `BurstCache[i]` без проверки индекса (как эталон).
	//   Итог был `Array index out of bounds: 2 into an array of size 0` на первом же тике
	//   после сброса. Здесь нужно ОБНУЛИТЬ ЗНАЧЕНИЯ, сохранив длину, — как в `Initialize`.
	BurstCache.Init(0, 3);
	LastBannerGuardIndex = -2;
	PendingNormalSkillMotion.Reset();
	CoinTargetWorld = FVector::ZeroVector;
	LastUseTickS = 0.0; LastMapCheckTickS = 0.0;
	NextSimDecayS = 0.0;
	FlashHideAtS = 0.0;
	bPinnedPosStale = true;
	(void)Reason;
}

void UAsdaSoulGuardSubsystem::ResetControls()
{
	MiniWidget = nullptr; bControlsReady = false; WidgetGen = -1;
	RootGroup = nullptr; RootHit = nullptr; BarLeft = nullptr; BarRight = nullptr;
	BarLeftDrag = nullptr; BarRightDrag = nullptr; PinCheck = nullptr;
	RowBox.Reset(); RowMark.Reset(); RowGText.Reset(); RowBolt.Reset(); RowMain.Reset();
	RowBallBack.Reset(); RowBackg.Reset(); RowBlackCircle.Reset(); RowDn.Reset();
	RowMarkAni.Reset(); RowBallAni.Reset(); RowHotText.Reset(); RowBall.Reset();
	FlashRed = nullptr; FlashBlue = nullptr; FlashGreen = nullptr; ArrowUp = nullptr; ArrowDown = nullptr;
	FlashHideAtS = 0.0;
	TextsGroup = nullptr; TextsHit = nullptr;
	BurstPanel.Reset(); BurstLabel.Reset(); BurstDigit.Reset();
	GuardPanel.Reset(); GuardLabel.Reset(); GuardDigit.Reset();
	BurstGroup.Reset(); BurstLabelGroup.Reset(); GuardGroup.Reset(); GuardLabelGroup.Reset();
	BannerRow = -1; bBannerIsGuard = false;
	BannerAlpha = 0.f; bBannerFadeOut = false; ActiveBurstSlot = -1;
	LastBannerGuardIndex = -2;
	// ★ Копии планов живут в дереве СТАРОГО виджета — их надо снять до его смены (иначе висячие ссылки).
	ClearPeerBanners();
	bRelOffsetsCached = false;
	bWindowShown = false;
	bPinnedPosStale = false;
	DesignPos56000 = FVector2D::ZeroVector;
}

// Разбор дерева. Все 95 контролов Soul_Boost* + 56200-поддерево резолвятся ЗДЕСЬ и нигде больше.
bool UAsdaSoulGuardSubsystem::EnsureControls()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr;
	if (!Mini) { return false; }
	UUserWidget* W = Mini->GetMiniWidget();
	if (!W) { if (bControlsReady) { ResetControls(); } return false; }
	if (bControlsReady && MiniWidget == W && WidgetGen == Mini->GetWidgetGeneration()) { return true; }

	ResetControls();
	MiniWidget = W;
	WidgetGen = Mini->GetWidgetGeneration();

	TArray<UWidget*> All;
	if (W->WidgetTree) { W->WidgetTree->GetAllWidgets(All); }

	auto Find    = [W](const TCHAR* N) -> UWidget*    { return W->GetWidgetFromName(FName(N)); };
	auto FindImg = [W](const FString& N) -> UImage*   { return Cast<UImage>(W->GetWidgetFromName(FName(*N))); };
	auto Plane   = [W](const TCHAR* Base, int32 Idx) -> UImage*
	{
		return Cast<UImage>(W->GetWidgetFromName(FName(*FString::Printf(TEXT("%s_%d_0"), Base, Idx))));
	};

	RootGroup    = Find(SoulUi::RootGroup);
	RootHit      = Find(SoulUi::RootHit);
	BarLeft      = FindImg(SoulUi::BarLeft);
	BarRight     = FindImg(SoulUi::BarRight);
	BarLeftDrag  = Find(SoulUi::BarLeftDrag);
	BarRightDrag = Find(SoulUi::BarRightDrag);
	PinCheck     = Cast<UCheckBox>(Find(SoulUi::PinCheck));
	FlashRed     = FindImg(SoulUi::FlashRed);
	FlashBlue    = FindImg(SoulUi::FlashBlue);
	FlashGreen   = FindImg(SoulUi::FlashGreen);
	ArrowUp      = FindImg(SoulUi::ArrowUp);
	ArrowDown    = FindImg(SoulUi::ArrowDown);
	TextsGroup   = Find(SoulUi::TextsGroup);
	TextsHit     = Find(SoulUi::TextsHit);

	RowBox.SetNum(3); RowMark.SetNum(3); RowGText.SetNum(3); RowBolt.SetNum(3); RowMain.SetNum(3);
	RowBallBack.SetNum(3); RowBackg.SetNum(3); RowBlackCircle.SetNum(3); RowDn.SetNum(3);
	RowMarkAni.SetNum(3); RowBallAni.SetNum(3); RowHotText.SetNum(3);
	RowBall.SetNum(30);
	for (int32 i = 0; i < 3; ++i)
	{
		RowBox[i]         = Find(*FString::Printf(TEXT("Soul_Boost_BOX_%d_grp"), i + 1));
		if (!RowBox[i]) { RowBox[i] = Find(*FString::Printf(TEXT("Soul_Boost_BOX_%d_%d_HIT"), i + 1, SoulUi::BoxHit[i])); }
		RowBackg[i]       = Plane(*FString::Printf(TEXT("Soul_Boost_backg_%d"), i + 1), SoulUi::Backg[i]);
		RowBlackCircle[i] = Plane(TEXT("BLACKCIRCLE"), SoulUi::BlackCircle[i]);
		RowBallBack[i]    = Plane(*FString::Printf(TEXT("Soul_Boost_bb_back_%d"), i + 1), SoulUi::BallBack[i]);
		RowMark[i]        = Plane(*FString::Printf(TEXT("Soul_Boost_MARK_%d"), i + 1), SoulUi::Mark[i]);
		RowMain[i]        = Plane(*FString::Printf(TEXT("Soul_Boost_main_%d"), i + 1), SoulUi::Main[i]);
		// GTEXT ряда 1 в .JUI назван без подчёркивания («Soul_Boost_GTEXT1»), рядов 2/3 — с ним.
		RowGText[i]       = Plane(i == 0 ? TEXT("Soul_Boost_GTEXT1")
		                                 : *FString::Printf(TEXT("Soul_Boost_GTEXT_%d"), i + 1), SoulUi::GText[i]);
		RowBolt[i]        = (i <= 1) ? Plane(TEXT("BOLT"), SoulUi::Bolt[i]) : nullptr;
		RowDn[i]          = Find(*FString::Printf(TEXT("Soul_Boost_D%d_grp"), i + 1));
		if (!RowDn[i]) { RowDn[i] = Find(*FString::Printf(TEXT("Soul_Boost_D%d_%d_HIT"), i + 1, SoulUi::DnHit[i])); }
		RowMarkAni[i]     = Plane(*FString::Printf(TEXT("soul_boost_mark_ani_%d"), i + 1), SoulUi::MarkAni[i]);
		RowBallAni[i]     = Plane(*FString::Printf(TEXT("soul_boost_ballani_%d"), i + 1), SoulUi::BallAni[i]);
		RowHotText[i]     = Cast<UTextBlock>(Find(*FString::Printf(TEXT("soul_boost_hot%d"), i + 1)));
		// Шары: j = 0 → D{i+1}_10 (левый, X=0) … j = 9 → D{i+1}_1 (правый, X=36).
		//   Эталонный цикл `for (j = 9; j >= 0; --j) obj[23i + (9-j) + 8] = ID(base + j + 56021)`.
		for (int32 j = 0; j < 10; ++j)
		{
			RowBall[i * 10 + j] = Plane(
				*FString::Printf(TEXT("Soul_Boost_D%d_%d"), i + 1, 10 - j), SoulUi::Ball10[i] + j);
		}
	}
	BurstPanel.SetNum(3); BurstLabel.SetNum(3); BurstDigit.SetNum(3);
	GuardPanel.SetNum(3); GuardLabel.SetNum(3); GuardDigit.SetNum(3);
	BurstGroup.SetNum(3); BurstLabelGroup.SetNum(3); GuardGroup.SetNum(3); GuardLabelGroup.SetNum(3);
	static const TCHAR* kBurstLabelSkin[3] = { TEXT("Text_burst_g"), TEXT("Text_burst_b"), TEXT("Text_burst_r") };
	static const TCHAR* kGuardLabelSkin[3] = { TEXT("Text_gaurder_g"), TEXT("Text_gaurder_b"), TEXT("Text_gaurder_r") };
	for (int32 i = 0; i < 3; ++i)
	{
		BurstPanel[i] = Plane(*FString::Printf(TEXT("Soul_Boost_Texts_%d"), i + 1), SoulUi::BurstPanel[i]);
		BurstLabel[i] = Plane(kBurstLabelSkin[i], SoulUi::BurstLabel[i]);
		BurstDigit[i] = Plane(*FString::Printf(TEXT("SOUL_%d"), i + 1), SoulUi::BurstDigit[i]);
		GuardPanel[i] = Plane(*FString::Printf(TEXT("Soul_Boost_Texts_%d"), i + 4), SoulUi::GuardPanel[i]);
		GuardLabel[i] = Plane(kGuardLabelSkin[i], SoulUi::GuardLabel[i]);
		GuardDigit[i] = Plane(*FString::Printf(TEXT("SOUL_%d"), i + 1), SoulUi::GuardDigit[i]);
		// ★ Показывать/прятать надо ГРУППЫ (в UMG-импорте каждый узел с детьми — вложенный
		//   CanvasPanel; спрятанная группа скрывает всё поддерево независимо от детей).
		BurstGroup[i]      = Find(*FString::Printf(TEXT("Soul_Boost_Texts_%d_grp"), i + 1));
		BurstLabelGroup[i] = Find(*FString::Printf(TEXT("%s_grp"), kBurstLabelSkin[i]));
		GuardGroup[i]      = Find(*FString::Printf(TEXT("Soul_Boost_Texts_%d_grp"), i + 4));
		GuardLabelGroup[i] = Find(*FString::Printf(TEXT("%s_grp"), kGuardLabelSkin[i]));
	}
	// ★ Снять взаимную раскладку троек с самих JUI-узлов (нужна копиям над чужими игроками).
	CacheBannerLayout();

	// ★★★ ВОССТАНОВЛЕНИЕ ВИДИМОСТИ ПОДДЕРЕВА. Генератор JUI→UMG ставит `Collapsed` не только узлу
	//   с `Visible="0"`, но и ВСЕМ его потомкам (`eff_hidden` протаскивается вниз,
	//   asda_jui_generic_umg.py:607/634). У корня 56000 и у 56200 в .JUI `Visible="0"` ⇒ в ассете
	//   collapsed ВЕСЬ блок Soul_Boost — включая шары, иконки и полосы, у которых в .JUI Visible="1".
	//   Оригинал так не работает: `CControl_SetVisible` меняет флаг ТОЛЬКО себе, а рендер просто не
	//   заходит в детей скрытого родителя. Поэтому здесь возвращаем каждому узлу его СОБСТВЕННУЮ
	//   видимость из .JUI, а прятать/показывать будем только сами группы-«окна».
	{
		static const TCHAR* kPrefixes[] = { TEXT("Soul_Boost"), TEXT("soul_boost"), TEXT("SOULBar"),
			TEXT("SOUL_1"), TEXT("SOUL_2"), TEXT("SOUL_3"), TEXT("Text_burst"), TEXT("Text_gaurder"),
			TEXT("BLACKCIRCLE"), TEXT("BOLT") };
		int32 Restored = 0;
		for (UWidget* Wd : All)
		{
			if (!Wd) { continue; }
			const FString N = Wd->GetName();
			bool bMine = false;
			for (const TCHAR* P : kPrefixes) { if (N.StartsWith(P)) { bMine = true; break; } }
			if (!bMine) { continue; }
			// ⚠ Группы-контейнеры и НЕВИДИМЫЕ ЗОНЫ (`*_HIT` / `*_DRAG`) обязаны остаться
			//   SelfHitTestInvisible — ровно как их ставит генератор. Если сделать их Visible,
			//   прозрачные UImage начнут поглощать клики по миру (регрессия «блокировка кликов»).
			//   Оконная подсистема и так их находит: её хит-тест и перетаскивание работают по
			//   ГЕОМЕТРИИ виджета, а не по Slate-хит-тесту.
			const bool bZone = N.EndsWith(TEXT("_HIT")) || N.EndsWith(TEXT("_DRAG"));
			Wd->SetVisibility((Cast<UCanvasPanel>(Wd) || bZone) ? ESlateVisibility::SelfHitTestInvisible
			                                                    : ESlateVisibility::Visible);
			++Restored;
		}
		UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] восстановлена собственная видимость %d узлов поддерева"), Restored);
	}

	// ── Стартовое состояние = ровно `soulCraftWnd_init@0xEF2F40` ────────────────────────────────
	//   alpha 0x40 у корня, боковых полос, BOX, main, bb_back, backg, MARK, BLACKCIRCLE, BOLT;
	//   alpha 0x80 у GTEXT; шары alpha 0xFF; стрелки и «вспышки» скрыты; 56200 видим, дети скрыты;
	//   чекбокс взведён (CControl_SetVisibleState(cbox, 1)).
	SetAlpha(BarLeft, DimAlpha); SetAlpha(BarRight, DimAlpha);
	for (int32 i = 0; i < 3; ++i)
	{
		SetAlpha(RowMain[i], DimAlpha);  SetAlpha(RowBallBack[i], DimAlpha);
		SetAlpha(RowBackg[i], DimAlpha); SetAlpha(RowMark[i], DimAlpha);
		SetAlpha(RowBlackCircle[i], DimAlpha);
		SetAlpha(RowGText[i], GTextAlpha);
		if (RowBolt[i]) { SetAlpha(RowBolt[i], DimAlpha); }
		for (int32 j = 0; j < 10; ++j) { SetAlpha(RowBall[i * 10 + j], 1.f); }
		SetVis(RowHotText[i], true);
		if (RowHotText[i]) { RowHotText[i]->SetAutoWrapText(false); }
		CloseHide(RowMarkAni[i]);
		CloseHide(RowBallAni[i]);
	}
	SetVis(FlashRed, false); SetVis(FlashBlue, false); SetVis(FlashGreen, false);
	CloseHide(ArrowUp); CloseHide(ArrowDown);
	SetGroupVis(TextsGroup, true);                  // эталон: SetVisible(56200, 1) в init
	for (int32 i = 0; i < 3; ++i)
	{
		ShowBannerTriple(i, /*bGuard=*/false, false);   // Texts_1..3 + Text_burst_* + SOUL_N
		ShowBannerTriple(i, /*bGuard=*/true,  false);   // Texts_4..6 + Text_gaurder_* + SOUL_N
	}
	if (PinCheck) { PinCheck->SetIsChecked(true); }
	bPinned = true; bPrevPinChecked = true;
	bAutoAnchor = true;                 // эталонный `*((_BYTE*)this + 524) = 1` в конце soulCraftWnd_init

	// ★ ОДНОРАЗОВЫЕ анимации (эталон: событие 24 «анимация закончилась» прячет контрол —
	//   uiDialogMsgProc, ui_core.c:92560-92588). Планы 56207/56208/56251..56253 показываются
	//   через CControl_Show (перезапуск с кадра 0), проигрываются РАЗ и гасят себя сами.
	//   SOULBAR_ANI (56255..56257) в этот список НЕ входит — он крутится, пока монет >= 8.
	auto MakeOneShot = [](UImage* Img) { if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(Img)) { P->bLoop = false; P->MaxLoops = 1; } };
	MakeOneShot(ArrowUp); MakeOneShot(ArrowDown);
	if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(ArrowUp.Get()))
	{
		P->OnAnimFinished.RemoveAll(this);
		P->OnAnimFinished.AddDynamic(this, &UAsdaSoulGuardSubsystem::OnArrowUpFinished);
	}
	if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(ArrowDown.Get()))
	{
		P->OnAnimFinished.RemoveAll(this);
		P->OnAnimFinished.AddDynamic(this, &UAsdaSoulGuardSubsystem::OnArrowDownFinished);
	}
	for (int32 i = 0; i < 3; ++i)
	{
		MakeOneShot(RowMarkAni[i].Get());
		if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(RowMarkAni[i].Get()))
		{
			P->OnAnimFinished.RemoveAll(this);
			switch (i)
			{
			case 0: P->OnAnimFinished.AddDynamic(this, &UAsdaSoulGuardSubsystem::OnMarkAni1Finished); break;
			case 1: P->OnAnimFinished.AddDynamic(this, &UAsdaSoulGuardSubsystem::OnMarkAni2Finished); break;
			default: P->OnAnimFinished.AddDynamic(this, &UAsdaSoulGuardSubsystem::OnMarkAni3Finished); break;
			}
		}
	}
	// Окно 56000 закрыто, пока нет ни одной монеты (hudSkillPopup_RefreshOrHide).
	bWindowShown = false;
	SetGroupVis(RootGroup, false);

	bControlsReady = true;

	// ★★★ ПОЗИЦИЯ, ВЫБРАННАЯ ПОЛЬЗОВАТЕЛЕМ, ГЛАВНЕЕ АВТО-ЯКОРЯ.
	//   `Soul_Boost_Win` (56000) — НЕ отдельное окно, а контрол внутри `MINISTATE_WIN`
	//   (`gamemain_jui.ini:11743`), поэтому общий механизм сохранения раскладки
	//   (`UAsdaWindowSubsystem`, ini-секция `[AsdaWindowLayout]`) хранит его как сдвиг
	//   `Pos.MINISTATE_WIN.Soul_Boost_Win` и восстанавливает сам — двигая ту же группу
	//   `Soul_Boost_Win_grp`, которую двигаем и мы.
	//   Если позиция восстановлена, авто-якорь эталона перебивать её НЕ ДОЛЖЕН: это ровно то
	//   состояние, в которое приходит и сам эталон после ПЕРВОГО клика по чекбоксу 56264
	//   (`this[524] = 0`, ui_core.c:92531-92556) — дальше чекбокс работает как обычно.
	if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
	{
		// ★ ДИЗАЙН-позиция 56000 = текущая − то, что общий механизм УЖЕ применил из ini.
		//   Нужна как база SyncSavedWindowOffset при переключениях чекбокса (см. TickPointer).
		FVector2D AppliedOff = FVector2D::ZeroVector;
		WM->GetAppliedWindowOffset(FName(TEXT("MINISTATE_WIN")), FName(TEXT("Soul_Boost_Win")), AppliedOff);
		DesignPos56000 = GroupSpacePos(RootGroup, RootHit) - AppliedOff;

		FVector2D SavedOff;
		if (WM->GetSavedWindowOffset(FName(TEXT("MINISTATE_WIN")), FName(TEXT("Soul_Boost_Win")), SavedOff)
			&& !SavedOff.IsNearlyZero())
		{
			bAutoAnchor = false;                       // как после первого клика по 56264
			// ★ Саму позицию захватываем ЛЕНИВО первым тиком показа: сдвиг из ini общий механизм
			//   мог ещё не применить (порядок регистрации окна и разбора дерева не гарантирован).
			bPinnedPosStale = true;
			PinnedPos = DesignPos56000 + SavedOff;     // предварительная оценка (для лога/дампа)
			UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] позиция восстановлена из настроек (сдвиг %.0f,%.0f) — ")
				TEXT("авто-якорь выключен, 56000 ≈ (%.0f,%.0f)"), SavedOff.X, SavedOff.Y, PinnedPos.X, PinnedPos.Y);
		}
	}

	int32 Found = 0;
	for (int32 i = 0; i < 3; ++i)
	{
		Found += (RowBox[i] ? 1 : 0) + (RowMark[i] ? 1 : 0) + (RowGText[i] ? 1 : 0) + (RowMain[i] ? 1 : 0)
		       + (RowBallBack[i] ? 1 : 0) + (RowBackg[i] ? 1 : 0) + (RowBlackCircle[i] ? 1 : 0)
		       + (RowDn[i] ? 1 : 0) + (RowMarkAni[i] ? 1 : 0) + (RowBallAni[i] ? 1 : 0)
		       + (RowHotText[i] ? 1 : 0) + (RowBolt[i] ? 1 : 0)
		       + (BurstPanel[i] ? 1 : 0) + (BurstLabel[i] ? 1 : 0) + (BurstDigit[i] ? 1 : 0)
		       + (GuardPanel[i] ? 1 : 0) + (GuardLabel[i] ? 1 : 0) + (GuardDigit[i] ? 1 : 0);
		for (int32 j = 0; j < 10; ++j) { Found += RowBall[i * 10 + j] ? 1 : 0; }
	}
	Found += (RootGroup ? 1 : 0) + (RootHit ? 1 : 0) + (BarLeft ? 1 : 0) + (BarRight ? 1 : 0)
	       + (PinCheck ? 1 : 0) + (FlashRed ? 1 : 0) + (FlashBlue ? 1 : 0) + (FlashGreen ? 1 : 0)
	       + (ArrowUp ? 1 : 0) + (ArrowDown ? 1 : 0) + (TextsGroup ? 1 : 0) + (TextsHit ? 1 : 0);
	UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] окно разобрано: узлов найдено %d/95 (поколение %d)"),
		Found, WidgetGen);
	RefreshIcons();
	return true;
}

// ─────────────────────────────── таблицы ────────────────────────────────────────────────────────
// ★★★ 2026-08-26: .bin-ФОЛБЭК УБРАН ЦЕЛИКОМ. Раньше здесь при пустых DataTable читались сырые
//   `Content/AsdaAssets/Data/Character/SoulGuarderTable.BIN` и `SkillCoinTable.bin`. Сырым файлам
//   в `Content` не место: они не ассеты, их не видит редактор и не забирает упаковщик, а сама
//   ветка была тихой ловушкой — при незалитой таблице игра «работала», а расхождение данных
//   всплывало только в бою. Исходники перенесены в `RawAssets/Data/`, обе таблицы строит
//   `Tools/asda_build_soulguard_dt.py`. Нет таблицы — это ОШИБКА, и она теперь видна в логе.
void UAsdaSoulGuardSubsystem::LoadTables()
{
	if (bTablesLoaded) { return; }
	GuarderTable.Reset();

	if (const UDataTable* DT = LoadObject<UDataTable>(nullptr, *GuarderTablePath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		for (const TPair<FName, uint8*>& It : DT->GetRowMap())
		{
			if (const FAsdaSoulGuarderRow* R = reinterpret_cast<const FAsdaSoulGuarderRow*>(It.Value))
			{
				// ★ [ПОРТ] 09.10.2026 (мультиязычность): имя хранителя — на языке сессии (оверлей `SoulGuarder`, ключ `<строка>.Name`).
				FAsdaSoulGuarderRow& C = GuarderTable.Add_GetRef(*R);
				C.Name = FAsdaLang::ResolveRow(DT, It.Key, TEXT("Name"), R->Name);
			}
		}
		GuarderTable.Sort([](const FAsdaSoulGuarderRow& A, const FAsdaSoulGuarderRow& B) { return A.SoulGuardIndex < B.SoulGuardIndex; });
	}
	else
	{
		UE_LOG(LogAsdaSoul, Error, TEXT("[хранитель] нет таблицы %s — хранители не заработают"), *GuarderTablePath);
	}
	// ★★★ Аудит №3 (A3, дубль данных Дд4): СВОЙ `LoadObject` таблицы монет и своя карта её строк
	//   УБРАНЫ. `DT_SkillCoin` держит `UAsdaItemTableSubsystem` (запись `GExtraTables`), резолв —
	//   `GetSkillCoin(SkillId)` по ключевому индексу. До правки те же 1255 строк лежали в памяти
	//   ВТОРОЙ копией, и правка данных разъезжалась между окном навыков, хранителем и менеджером.
	MgrSkillCoinRows = 0;
	if (UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr)
	{
		if (const UDataTable* Dt = IT->GetDataTable(FName(TEXT("SkillCoin")))) { MgrSkillCoinRows = Dt->GetRowMap().Num(); }
	}
	if (MgrSkillCoinRows == 0)
	{
		UE_LOG(LogAsdaSoul, Error, TEXT("[хранитель] у менеджера таблиц пуста DT_SkillCoin — монеты навыков не начислятся"));
	}
	bTablesLoaded = (GuarderTable.Num() > 0) || (SkillCoinCount() > 0);
	UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] таблицы: SoulGuarderTable %d записей, SkillCoinTable %d записей (менеджер)"),
		GuarderTable.Num(), SkillCoinCount());
}

const FAsdaSoulGuarderRow* UAsdaSoulGuardSubsystem::FindGuarderByIndex(int32 Index) const
{
	for (const FAsdaSoulGuarderRow& R : GuarderTable) { if (R.SoulGuardIndex == Index) { return &R; } }
	return nullptr;
}

const FAsdaSoulGuarderRow* UAsdaSoulGuardSubsystem::FindGuarderByTypeRow(int32 InSkillType, int32 RowIndex) const
{
	for (const FAsdaSoulGuarderRow& R : GuarderTable)
	{
		if (R.SkillType == InSkillType && R.RowIndex == RowIndex) { return &R; }
	}
	return nullptr;
}

// ★ Аудит №3 (A3): единственная точка доступа к строке таблицы монет — менеджер таблиц.
//   .bin-фолбэка здесь больше нет (см. `LoadTables`): держатель строки ровно один.
bool UAsdaSoulGuardSubsystem::FindSkillCoin(int32 SkillId, FAsdaSkillCoinRow& OutRow) const
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr)
	{
		if (IT->GetSkillCoin(SkillId, OutRow)) { return true; }
	}
	return false;
}

// Число строк таблицы монет у менеджера (снято один раз в `LoadTables`).
int32 UAsdaSoulGuardSubsystem::SkillCoinCount() const
{
	return MgrSkillCoinRows;
}

bool UAsdaSoulGuardSubsystem::GetSkillCoins(int32 SkillId, int32& OutC0, int32& OutC1, int32& OutC2) const
{
	FAsdaSkillCoinRow R;
	if (FindSkillCoin(SkillId, R))
	{
		OutC0 = R.Coin0; OutC1 = R.Coin1; OutC2 = R.Coin2;
		return true;
	}
	OutC0 = OutC1 = OutC2 = 0;
	return false;
}

// ─────────────────────────────── эталонные функции окна ─────────────────────────────────────────
int32 UAsdaSoulGuardSubsystem::GetCoin(int32 Row) const { return Coin.IsValidIndex(Row) ? Coin[Row] : 0; }
bool  UAsdaSoulGuardSubsystem::IsRowAvailable(int32 Row) const { return bRowAvailable.IsValidIndex(Row) && bRowAvailable[Row]; }
int32 UAsdaSoulGuardSubsystem::GetRowSkillCode(int32 Row) const { return RowSkillCode.IsValidIndex(Row) ? RowSkillCode[Row] : -1; }

// Формула сервера sub_48B210 (GameServer.c:55712): подряд с ряда 0, пока монет >= порога.
int32 UAsdaSoulGuardSubsystem::GetGuardLevel() const
{
	int32 Lvl = -1;
	for (int32 i = 0; i < 3 && Coin.IsValidIndex(i) && Coin[i] >= GuardLevelThreshold; ++i) { Lvl = i; }
	return Lvl;
}

int32 UAsdaSoulGuardSubsystem::SelfUserIdx() const
{
	// ★ Волна 1 (14.09.2026, В9 / исправление E2): общая точка `AsdaSelf::GetUserIdxOr`. До пакета 4003
	//   своего номера нет — INDEX_NONE, а не 0 (0 — законный номер ЧУЖОГО игрока, закон `charid-vs-usernum`).
	//   Ключ отката ядра `asda.Self.StrictUnknownNum` на выборку номера не действует (`AsdaSelf.h`).
	return AsdaSelf::GetUserIdxOr(GetGameInstance(), INDEX_NONE);
}

// «Мы мертвы» из ЖИВОГО источника порта (павн + сетевое зеркало CUser+79) — эталонное
//   состояние действия 6 (`CChar_SetState case 6`, entity_char.c:19622 = смерть).
bool UAsdaSoulGuardSubsystem::IsSelfDeadNow() const
{
	const UWorld* Wld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const AAsdaPlayerController* PC = Wld ? Cast<AAsdaPlayerController>(Wld->GetFirstPlayerController()) : nullptr;
	return PC && PC->IsSelfDead();
}

// Реально ли узел дойдёт до экрана: он сам и ВСЯ цепочка родителей не Collapsed/Hidden.
bool UAsdaSoulGuardSubsystem::IsEffectivelyVisible(const UWidget* W)
{
	for (const UWidget* P = W; P; P = P->GetParent())
	{
		const ESlateVisibility V = P->GetVisibility();
		if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return false; }
	}
	return true;
}

AAsdaPlayerCharacter* UAsdaSoulGuardSubsystem::GetLocalChar() const
{
	const UWorld* Wld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* PC = Wld ? Wld->GetFirstPlayerController() : nullptr;
	return PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
}

// `hudQuickSlotLoadSkills@0xEF44E0` (ui_core.c:92650) 1:1:
//   a2 == 0 → все три slot = −1; иначе линейный проход по SoulGuarderTable, сравнение
//   `record.SkillType == a2 && record.RowIndex == следующийРяд` → в слот кладётся GuardianSkillId (+152).
void UAsdaSoulGuardSubsystem::LoadRowSkills(int32 InSkillType)
{
	SkillType = InSkillType;
	if (InSkillType != 0)
	{
		int32 Next = 0;
		RowSkillCode.Init(-1, 3);
		for (const FAsdaSoulGuarderRow& R : GuarderTable)
		{
			if (Next >= 3) { break; }
			if (R.SkillType == InSkillType && R.RowIndex == Next)
			{
				RowSkillCode[Next] = R.GuardianSkillId;
				++Next;
			}
		}
		UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] тип %d → скиллы рядов: %d / %d / %d"),
			InSkillType, RowSkillCode[0], RowSkillCode[1], RowSkillCode[2]);
	}
	else
	{
		RowSkillCode.Init(-1, 3);
		UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] тип 0 → скиллы рядов сброшены (−1)"));
	}
	RefreshIcons();
}

// Имя базы иконки скилла: запись skilltooltip.bin (504 б) по skillCode, поле +36 (char[64]).
//   В порту это DT_skilltooltip: колонки Id/Name/Text + DataHex = ВЕСЬ сырой рекорд в hex,
//   поэтому +36 = символы 72.. строки DataHex. Эталон: `uiGetRecordByIndex(g_pSkillWnd, code)`
//   → `record->filler[36]` (ui_core.c:92775).
// ★ Аудит №3 (A13): к полю +36 вело ТРИ пути — свой `LoadObject` + свой hex-цикл здесь, свой `FindRow`
//   в панели быстрого доступа и разбор при подъёме базы навыков. Остался один: `UAsdaSkillDatabase`
//   (подсистема, A12) разбирает ту же запись один раз и держит имя в `FAsdaSkillDef::IconName`.
//   Заодно ушла вторичная загрузка `DT_skilltooltip` (A3) и дефект `FParse::HexDigit` (A6).
FString UAsdaSoulGuardSubsystem::GetSkillIconName(int32 SkillCode) const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaSkillDatabase* Db = GI ? GI->GetSubsystem<UAsdaSkillDatabase>() : nullptr;
	const FAsdaSkillDef* D = Db ? Db->Get(SkillCode) : nullptr;
	return D ? D->IconName : FString();
}

// Поставить иконку 32×32 в MARK-контрол ряда (эталон CControl_DrawTextureRect(mark, tex, 0,0,32,32)).
bool UAsdaSoulGuardSubsystem::SetRowIcon(int32 Row, const FString& TexName)
{
	if (!RowMark.IsValidIndex(Row) || !RowMark[Row] || TexName.IsEmpty()) { return false; }
	UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();   // ★ Аудит №3 (B8): короткий доступ к менеджеру текстур
	UTexture2D* Tex = TexMgr ? TexMgr->LoadUITexture(FName(*TexName)) : nullptr;
	if (!Tex)
	{
		UE_LOG(LogAsdaSoul, Warning, TEXT("[хранитель] иконка скилла «%s» не найдена в %s"), *TexName, *IconPackagePath);
		return false;
	}
	// ★ Дедуп №2, В2-B (U4d): крой 32×32 по эталону — общая точка `AsdaUi::ApplyJuiIconRegionTex` (та же формула UV = 32 / логический
	//   размер). Отличие одно: у текстуры МЕНЬШЕ 32 px здесь был зажим UV до 1 (вся текстура), а эталон даёт UV > 1 (заворот,
	//   D3DTADDRESS_WRAP); таких иконок в данных нет (все `skl_*` хранителя — 32×32, перепись `asda.UI.WholeIconCensus`).
	AsdaUi::ApplyJuiIconRegionTex(RowMark[Row], Tex, 0, 0, 32, 32, 32.f, 32.f);
	SetVis(RowMark[Row], true);
	return true;
}

// `hudQuickSlot_RefreshIcons@0xEF4800` (ui_core.c:92713) 1:1.
void UAsdaSoulGuardSubsystem::RefreshIcons()
{
	if (!bControlsReady) { return; }
	const int32 RowCount[3] = { GetCoin(0), GetCoin(1), GetCoin(2) };

	// Чекбокс 56264: взведён → полосы полупрозрачны и НЕ ловят мышь (ручки перетаскивания выключены);
	//   снят → полосы яркие и ловят мышь (окно можно таскать).
	if (PinCheck)
	{
		bPinned = PinCheck->IsChecked();
		SetAlpha(BarLeft,  bPinned ? DimAlpha : 1.f);
		SetAlpha(BarRight, bPinned ? DimAlpha : 1.f);
		// `helper_setByte295(bar, 1)` = «контрол не участвует в хит-тесте». У нас хит-тест ручки
		//   ведёт UAsdaWindowSubsystem по ГЕОМЕТРИИ виджета `*_DRAG`: свёрнутый = нулевая
		//   геометрия = ручка выключена; SelfHitTestInvisible = геометрия есть, ручка работает,
		//   и при этом сам прозрачный UImage не поглощает клики.
		SetGroupVis(BarLeftDrag,  !bPinned);
		SetGroupVis(BarRightDrag, !bPinned);
	}

	for (int32 i = 0; i < 3; ++i)
	{
		// Шары: видимы те, чей индекс <= count−1 (слева направо).
		for (int32 j = 0; j < 10; ++j)
		{
			SetVis(RowBall[i * 10 + j], j <= RowCount[i] - 1);
		}
		// «Пустая» иконка ряда: эталон сначала рисует дефолтную текстуру из g_pGameClient+3081080
		//   (пустой 32×32), затем — если у ряда есть скилл — настоящую. У нас «пусто» = скрыть
		//   контрол: скин MARK — NoImage_icon (пустой пиксель), рисовать нечего.
		SetVis(RowMark[i], false);

		// ★ ОТСТУПЛЕНИЕ (по умолчанию ВКЛ): эталон считает флаги доступности ВНУТРИ блока
		//   `if (RecordByIndex)` — то есть пока у ряда нет skillCode (сервер не прислал SkillType)
		//   или нет записи тултипа, все три флага остаются нулями и хоткей молча не работает.
		//   Считаем их всегда; false = ровно как в эталоне.
		if (bAlwaysRecomputeAvailability)
		{
			for (int32 k = 0; k < 3; ++k) { bRowAvailable[k] = (RowCount[k] >= AvailableThreshold); }
		}

		if (RowSkillCode[i] == -1) { continue; }
		FString Icon = GetSkillIconName(RowSkillCode[i]);
		if (Icon.IsEmpty() && !bAlwaysRecomputeAvailability) { continue; }

		// Подпись хоткея: эталон `CKeybind_FormatHotkeyString(g_pKeybindMgr, 45 + i)` (input.c:4826)
		//   = `"<Shift+|Ctrl+|Alt+>" + имя клавиши` для кода действия 45 + ряд. Дефолты
		//   `keybindLoadDefaults@0xE3A9C0` (game_main.c:11285): 45 = VK 45 Insert, 46 = VK 36 Home,
		//   47 = VK 33 Page Up, модификатор 0 — ровно то, что уже лежит в UAsdaKeybindConfig.
		if (RowHotText[i]) { RowHotText[i]->SetText(FText::FromString(FormatHotkeyLabel(45 + i))); }

		// Флаги доступности всех трёх рядов (эталон пересчитывает их ЦЕЛИКОМ на каждой итерации).
		for (int32 k = 0; k < 3; ++k) { bRowAvailable[k] = (RowCount[k] >= AvailableThreshold); }

		if (bRowAvailable[i])
		{
			// Одноразовый показ SOULBALL_ANI при переходе «стал доступен».
			if (bMarkAniPending[i])
			{
				bMarkAniPending[i] = false;
				ShowAndRestart(RowMarkAni[i]);
			}
		}
		else
		{
			Icon += DisabledIconSuffix;                  // «серая» текстура `*_g`
			if (RowHotText[i]) { RowHotText[i]->SetText(FText::GetEmpty()); }   // эталон: пустая строка
			bMarkAniPending[i] = true;
			CloseHide(RowMarkAni[i]);
		}

		// SOULBAR_ANI — только при >= 8 монет.
		if (RowCount[i] < GuardLevelThreshold) { CloseHide(RowBallAni[i]); }
		else                                   { ShowAndRestart(RowBallAni[i]); }

		if (!Icon.IsEmpty()) { SetRowIcon(i, Icon); }
		SetAlpha(RowMark[i], 1.f);                       // эталон: SetColor4(mark, 0xFF, 0) в конце
	}
}

// `uiSlotState_UpdateArrowIndicators@0xEF56D0` (ui_core.c:92839) 1:1.
void UAsdaSoulGuardSubsystem::UpdateArrowIndicators(int32 C0, int32 C1, int32 C2)
{
	if (!Coin.IsValidIndex(2)) { return; }
	PrevCoin[0] = Coin[0]; PrevCoin[1] = Coin[1]; PrevCoin[2] = Coin[2];
	Coin[0] = C0; Coin[1] = C1; Coin[2] = C2;
	OnCoinsChanged.Broadcast(C0, C1, C2);
	if (!bControlsReady) { return; }

	for (int32 i = 0; i < 3; ++i)
	{
		if (PrevCoin[i] == Coin[i] || Coin[i] > 10) { continue; }
		const int32 SlotIdx = FMath::Max(0, Coin[i] - 1);
		// Эталон: `sumX/sumY = позиция шара + позиция контейнера D{i+1} + позиция бокса BOX_{i+1}`
		//   — там координаты контролов ЛОКАЛЬНЫЕ и складываются по цепочке предков.
		// ★ В UMG-импорте ровно та же математика: генератор строит ВЛОЖЕННЫЕ CanvasPanel-группы,
		//   и оффсет каждого узла локален относительно СВОЕЙ группы (asda_jui_generic_umg.py:601-635,
		//   «дети кладутся ВНУТРЬ группы в ЛОКАЛЬНЫХ координатах»). Цепочка здесь такая же:
		//   Soul_Boost_Win_grp → Soul_Boost_BOX_{i+1}_grp → Soul_Boost_D{i+1}_grp → шар,
		//   поэтому сумма трёх оффсетов = позиция шара в системе координат Win_grp — ровно там же
		//   живут стрелки и «вспышки» (они листья глубины 2, то есть прямые дети Win_grp).
		FVector2D PBase(0, 0), PMid(0, 0), PSlot(0, 0), Sz;
		GetSlotRect(RowBall[i * 10 + SlotIdx], PBase, Sz);
		GetSlotRect(RowDn[i],  PMid,  Sz);
		GetSlotRect(RowBox[i], PSlot, Sz);
		FVector2D BallSz(20, 19), UpSz(48, 48), DnSz(64, 64);
		FVector2D Tmp;
		GetSlotRect(RowBall[i * 10 + 0], Tmp, BallSz);
		GetSlotRect(ArrowUp,   Tmp, UpSz);
		GetSlotRect(ArrowDown, Tmp, DnSz);
		const FVector2D BallPos = PBase + PMid + PSlot;

		auto MoveTo = [](UWidget* W, const FVector2D& P)
		{
			if (!W) { return; }
			if (UCanvasPanelSlot* S = AsdaUi::SlotOf(W))
			{
				FMargin M = S->GetOffsets();
				M.Left = P.X; M.Top = P.Y;
				S->SetOffsets(M);
			}
		};

		if (PrevCoin[i] <= Coin[i])
		{
			SetVis(FlashRed, false); SetVis(FlashBlue, false); SetVis(FlashGreen, false);
			MoveTo(ArrowUp, FVector2D(BallSz.X * 0.5f + BallPos.X - UpSz.X * 0.5f,
			                          BallSz.Y * 0.5f + BallPos.Y - UpSz.Y * 0.5f));
			ShowAndRestart(ArrowUp);
			if (Coin[i] >= 1 && Coin[i] <= 5)  { SetVis(FlashGreen, true); MoveTo(FlashGreen, BallPos); }
			if (Coin[i] >= 6 && Coin[i] <= 8)  { SetVis(FlashBlue,  true); MoveTo(FlashBlue,  BallPos); }
			if (Coin[i] >= 9 && Coin[i] <= 10) { SetVis(FlashRed,   true); MoveTo(FlashRed,   BallPos); }
			// Крайний срок жизни кружка — на случай, если событие 24 стрелки не придёт (см. Tick).
			FlashHideAtS = FPlatformTime::Seconds() + (double)FlashFallbackHideMs / 1000.0;
		}
		else
		{
			MoveTo(ArrowDown, FVector2D(BallSz.X * 0.5f + BallPos.X - DnSz.X * 0.5f,
			                            BallSz.Y * 0.5f + BallPos.Y - DnSz.Y * 0.5f + 1.f));
			ShowAndRestart(ArrowDown);
		}
	}
}

// `hudSkillPopup_RefreshOrHide@0xEF6A30` (ui_core.c:92920) 1:1: окно видно, пока есть хоть одна монета.
void UAsdaSoulGuardSubsystem::RefreshOrHide()
{
	if (!bControlsReady) { return; }
	const bool bWant = (Coin[0] >= 1 || Coin[1] >= 1 || Coin[2] >= 1);
	if (bWant != bWindowShown)
	{
		bWindowShown = bWant;
		SetGroupVis(RootGroup, bWant);
		UE_LOG(LogAsdaSoul, Verbose, TEXT("[хранитель] окно 56000 %s"), bWant ? TEXT("показано") : TEXT("скрыто"));
	}
	// ★★★ Эталонная ветка `LocalPlayer->effectSlots[112].field_8 == 6`: счётчики обнуляются и окно
	//   прячется. РАЗОБРАНО: `effectSlots[112].field_8` — это ЧИСЛОВОЕ СОСТОЯНИЕ ДЕЙСТВИЯ персонажа
	//   (`CChar_SetState`, entity_char.c:16519/19258 — там же поле field_174 = субрежим), а
	//   **состояние 6 = СМЕРТЬ**: ветка `case 6` (entity_char.c:19622) сбрасывает цель и
	//   bActionBusy, играет предсмертный звук 63/64 по полу, ставит аним-слот 7 (клип смерти),
	//   ссаживает с транспорта, чистит все скилловые эффекты и прячет панели, если это была цель.
	//   Сходится с сервером: `sub_48DD60` на смерти шлёт «[DEAD] Coin Reset..!!».
	//   ⇒ Источник в порту ЕСТЬ: `AAsdaPlayerController::IsSelfDead()` (павн + сетевое зеркало
	//   CUser+79). Ручной `SelfActionState` оставлен как ПЕРЕОПРЕДЕЛЕНИЕ для проверки: −1 = «нет
	//   данных, берём живой источник».
	const bool bDead = (SelfActionState >= 0) ? (SelfActionState == 6) : IsSelfDeadNow();
	if (bDead && (Coin[0] || Coin[1] || Coin[2]))
	{
		Coin.Init(0, 3);
		bWindowShown = false;
		SetGroupVis(RootGroup, false);
		UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] персонаж мёртв (состояние 6) — счётчики обнулены, окно скрыто (эталон)"));
	}
}
