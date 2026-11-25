// Copyright (c) 2026 @sitxovski (Discord)
// Окна боевой фазы «Битвы армий»: 49600 очки армий, 49900 карта боя (+ слой рисунка и 10 таймеров точек), 49910 крупные надписи,
//   49940 «Осталось времени» + 49500 «Рейд», 49510 полоса захвата, 49800 итог, 49399/49400 «Рейд» (список сквада и попап).
//   Видимость узлов, которую эталон выставляет `CControl_SetVisible`, переносится КОДОМ (логика, не правка разметки); модель
//   видимости/текстов (`NodeVis`/`NodeText`) переживает пересоздание окон после травела. Узлы — `campwar_wbp_tree.txt`.

#include "UI/AsdaCampWarBattleSubsystem.h"
#include "UI/AsdaUiHelpers.h"   // ★ R1: прямое включение (раньше приходило транзитивно)
#include "AsdaCampWarBattleInternal.h"
#include "UI/AsdaCampWarSubsystem.h"
#include "UI/AsdaJuiWindowHandle.h"         // каркас окна, FAsdaGaugeFill, FAsdaPopupMenu
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"        // развёрнута ли миникарта (сдвиг 49600)
#include "UI/AsdaPartySubsystem.h"          // 49410 «Пригласить в отряд»
#include "UI/AsdaInventorySubsystem.h"      // 6186: в сумке есть предмет
#include "UI/AsdaItemTooltipSubsystem.h"    // подсказка полос итога (`tutorialPopupLayoutSmall`)
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaChatSubsystem.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaSelf.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Login/AsdaButtonRelay.h"
#include "Npc/AsdaNpcActor.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "World/AsdaClanFlag.h"
#include "World/AsdaKingCrownSubsystem.h"
#include "World/AsdaWorldSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SLeafWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCampWarWin, Log, All);

using namespace AsdaCwBattle;

// ═════════════════════════════ ОПИСАНИЕ ОКОН ═════════════════════════════════════════════════════
namespace
{
	// Окно: корень IDNum, id реестра (ID корня в .JUI), WBP, z-порядок.
	struct FCwWinDesc { int32 Root; const TCHAR* Id; const TCHAR* Wbp; int32 Z; };
	const FCwWinDesc kWins[] =
	{
		{ kWinPoint, TEXT("CAMPWAR_Point"),           TEXT("/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_CAMPWAR_Point.WBP_CAMPWAR_Point_C"), 130 },
		{ kWinMap,   TEXT("CAMPWAR_L_MAP"),           TEXT("/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_CAMPWAR_L_MAP.WBP_CAMPWAR_L_MAP_C"), 130 },
		{ kWinFont,  TEXT("CAMPWAR_FONT_Win"),        TEXT("/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_CAMPWAR_FONT_Win.WBP_CAMPWAR_FONT_Win_C"), 140 },
		{ kWinTime,  TEXT("CAMPWAR_Time_Win"),        TEXT("/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_CAMPWAR_Time_Win.WBP_CAMPWAR_Time_Win_C"), 130 },
		{ kWinHold,  TEXT("CAPWAR_Hold_ING"),         TEXT("/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_CAPWAR_Hold_ING.WBP_CAPWAR_Hold_ING_C"), 135 },
		{ kWinEnd,   TEXT("CAMPWAR_End"),             TEXT("/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_CAMPWAR_End.WBP_CAMPWAR_End_C"), 700 },
		{ kWinAtk,   TEXT("CAMPWAR_ATTACK"),          TEXT("/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_CAMPWAR_ATTACK.WBP_CAMPWAR_ATTACK_C"), 700 },
		{ kWinPop,   TEXT("CAMPWAR_ATTACK_Position"), TEXT("/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_CAMPWAR_ATTACK_Position.WBP_CAMPWAR_ATTACK_Position_C"), 720 },
	};
	const FCwWinDesc* Desc(int32 Root) { for (const FCwWinDesc& D : kWins) { if (D.Root == Root) { return &D; } } return nullptr; }

	// Суффикс набора цифр армии F в ассете 49600: армия 0 — исходный набор под mark1, 1/2 — копии под mark2/mark3 (решение
	//   координатора 30.10: точечно в ассет, без рантайм-построения).
	const TCHAR* PackSuffix(int32 F) { return F == 1 ? TEXT("_m2") : F == 2 ? TEXT("_m3") : TEXT(""); }
	// Узлы 49800 по строке i (B3 у ассета назван «Slide2» — так в дампе).
	const TCHAR* kEndMark[3]  = { TEXT("CAMPWAR_End_1_B1_MARK1_14_0"), TEXT("CAMPWAR_End_1_B2_MARK2_22_0"), TEXT("CAMPWAR_End_1_B3_MARK3_30_0") };
	const TCHAR* kEndFront[3] = { TEXT("CAMPWAR_End_1_B1_Slide1_18_0"), TEXT("CAMPWAR_End_1_B2_Slide2_26_0"), TEXT("CAMPWAR_End_1_B3_Slide2_34_0") };
	const TCHAR* kEndBack[3]  = { TEXT("CAMPWAR_End_1_B1_Slide1_back_17_0"), TEXT("CAMPWAR_End_1_B2_Slide2_back_25_0"), TEXT("CAMPWAR_End_1_B3_Slide2_back_33_0") };
	const TCHAR* kEndIcon[3]  = { TEXT("CAMPWAR_End_2_Item1_M_41_0"), TEXT("CAMPWAR_End_2_Item2_M_45_0"), TEXT("CAMPWAR_End_2_Item3_M_49_0") };
	const float kEndBarW = 245.f;   // ширина полос 49811/49814 (Slide 245×21)
	const float kHoldBarW = 140.f;  // ширина полосы 49521 (Red_Bar 140×20)

	// `String_FormatThousands`: «1,234,567».
	FString Thousands(int64 V)
	{
		FString S = FString::Printf(TEXT("%lld"), (long long)FMath::Abs(V));
		for (int32 I = S.Len() - 3; I > 0; I -= 3) { S.InsertAt(I, TEXT(",")); }
		return (V < 0) ? TEXT("-") + S : S;
	}
	// Позиция слота узла в канвасе родителя.
	FVector2D SlotPosOf(const UWidget* W) { return AsdaUi::SlotPos(W); }   // ★ R5-fix-b 13.11.2026: была копия `AsdaUi::SlotPos`
	// Поставить узел в позицию канваса родителя.
	void SetSlotPosOf(UWidget* W, const FVector2D& P) { if (UCanvasPanelSlot* S = W ? Cast<UCanvasPanelSlot>(W->Slot) : nullptr) { S->SetPosition(P); } }
	// Ребёнок панели, чьё имя начинается с Prefix (и, если задан, оканчивается Suffix).
	UWidget* ChildByPrefix(UPanelWidget* P, const FString& Prefix, const TCHAR* Suffix)
	{
		if (!P) { return nullptr; }
		for (UWidget* C : P->GetAllChildren())
		{
			const FString N = C ? C->GetName() : FString();
			if (N.StartsWith(Prefix) && (!Suffix || !*Suffix ? !N.EndsWith(TEXT("_m2")) && !N.EndsWith(TEXT("_m3")) : N.EndsWith(Suffix))) { return C; }
		}
		return nullptr;
	}
}

// ═════════════════════════════ СЛОЙ РИСУНКА КАРТЫ 49900 ══════════════════════════════════════════
// Порт ветки кампвара `uiMiniMapRender@0xB4EFE0` (`byte_2C07244 == 1` и 49904 видим): `./Map/CampWar_Map1.TEX` квадом 200×200
//   в позиции 49900 (UV 0…0.78125), 10 значков 32×32 из `CampWar_Object.TEX` (`hudIconStripRender@0xB545E0`), накладка
//   `CampWar_Number1.TEX` тем же квадом. Окно 49900 у эталона само прозрачно — картинку даёт этот слой (под окном: таймеры выше).
class SAsdaCampWarMapLayer : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAsdaCampWarMapLayer) {}
	SLATE_END_ARGS()

	// Владелец (модель точек, гейт рисунка, текстуры).
	void Construct(const FArguments&, UAsdaCampWarBattleSubsystem* InOwner) { Owner = InOwner; SetCanTick(false); }
	// Размер не важен — слой растянут на вьюпорт.
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1.f, 1.f); }
	// Рисунок: карта, значки, номера.
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		UAsdaCampWarBattleSubsystem* O = Owner.Get();
		if (!O || !O->IsMapLayerDrawn() || O->MapTex.Num() < 3) { return LayerId; }
		const UWidget* Anchor = O->Node(kWinMap, kWinMap);
		if (!Anchor) { return LayerId; }
		const FGeometry& G = Anchor->GetCachedGeometry();
		const FVector2D TL = AllottedGeometry.AbsoluteToLocal(G.GetAbsolutePosition());
		const FVector2D BR = AllottedGeometry.AbsoluteToLocal(G.GetAbsolutePosition() + G.GetAbsoluteSize());
		const float K = (BR.X - TL.X) / 200.f;
		if (K <= 0.f) { return LayerId; }
		auto Quad = [&](UTexture2D* Tex, const FVector2D& Pos, const FVector2D& Size, const FBox2f& UV, FSlateBrush& B)
		{
			if (!Tex) { return; }
			B.SetResourceObject(Tex);
			B.DrawAs = ESlateBrushDrawType::Image;
			B.ImageSize = FVector2f(Size);
			B.SetUVRegion(UV);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), &B,
				ESlateDrawEffect::None, FLinearColor::White);
		};
		const FBox2f MapUV(FVector2f(0.f, 0.f), FVector2f(0.78125f, 0.78125f));   // 200/256
		Quad(O->MapTex[0], TL, FVector2D(200.f * K, 200.f * K), MapUV, BMap);
		Icons.SetNum(UAsdaCampWarBattleSubsystem::kSlots);
		for (int32 S = 0; S < UAsdaCampWarBattleSubsystem::kSlots; ++S)
		{
			FVector2D UV0;
			if (O->GetMapIconCell(S, UV0) < 0) { continue; }
			Quad(O->MapTex[1], TL + UAsdaCampWarBattleSubsystem::PointMapPos(S) * K, FVector2D(32.f * K, 32.f * K),
				FBox2f(FVector2f(UV0), FVector2f(UV0) + FVector2f(0.25f, 0.25f)), Icons[S]);
		}
		Quad(O->MapTex[2], TL, FVector2D(200.f * K, 200.f * K), MapUV, BNum);
		return LayerId;
	}

	TWeakObjectPtr<UAsdaCampWarBattleSubsystem> Owner;   // владелец
	mutable FSlateBrush BMap, BNum;                      // кисти карты и накладки номеров
	mutable TArray<FSlateBrush> Icons;                   // кисти 10 значков
};

void UAsdaCampWarBattleSubsystem::EnsureMapLayer()
{
	// Слой живёт во вьюпорте под окном 49900 (z 129 < 130); виден только по гейту `IsMapLayerDrawn`.
	UGameInstance* GI = GetGameInstance();
	UGameViewportClient* VP = GI ? GI->GetGameViewportClient() : nullptr;
	if (!VP) { return; }
	if (MapTex.Num() < 3)
	{
		UAsdaTextureSubsystem* TS = GEngine ? GEngine->GetEngineSubsystem<UAsdaTextureSubsystem>() : nullptr;
		if (!TS) { return; }
		MapTex.Reset();
		for (const TCHAR* N : { TEXT("/Game/AsdaAssets/Textures/Maps/CampWar_Map1"), TEXT("/Game/AsdaAssets/Textures/Maps/CampWar_Object"),
			TEXT("/Game/AsdaAssets/Textures/Maps/CampWar_Number1") })
		{
			MapTex.Add(TS->LoadTextureCached(N));
		}
	}
	if (!MapLayer.IsValid())
	{
		SAssignNew(MapLayer, SAsdaCampWarMapLayer, this);
		VP->AddViewportWidgetContent(MapLayer.ToSharedRef(), 129);
	}
	const EVisibility Want = IsMapLayerDrawn() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	if (MapLayer->GetVisibility() != Want) { MapLayer->SetVisibility(Want); }
}

void UAsdaCampWarBattleSubsystem::DropMapLayer()
{
	if (!MapLayer.IsValid()) { return; }
	UGameInstance* GI = GetGameInstance();
	if (UGameViewportClient* VP = GI ? GI->GetGameViewportClient() : nullptr) { VP->RemoveViewportWidgetContent(MapLayer.ToSharedRef()); }
	MapLayer.Reset();
}

bool UAsdaCampWarBattleSubsystem::IsMapLayerDrawn() const
{
	// Гейт `uiMiniMapRender`: +0x10C миникарты (= HUD боя) и 49904 видим; окно 49900 живо; интерфейс не спрятан Alt+Z.
	const FAsdaCwWindow* W = Win(kWinMap);
	const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return bHudOn && VisModel(49904, false) && W && W->bShown && WM && WM->IsWindowLive(W->Widget) && !AsdaUi::GUiInputHidden;
}

int32 UAsdaCampWarBattleSubsystem::GetMapIconCell(int32 S, FVector2D& OutUV) const
{
	// `hudIconStripRender@0xB545E0`: state 0 → (0, 0.75); 1 → (0, 32·f/128); 2 → (0.25, 32·f/128); f — владелец.
	if (S < 0 || S >= kSlots) { return -1; }
	const FAsdaCwPointSlot& P = Slots[S];
	const float V = (P.Owner >= 0 && P.Owner < kArmies) ? 32.f * P.Owner / 128.f : 0.75f;
	if (P.State == 1) { OutUV = FVector2D(0.f, V); return 1; }
	if (P.State == 2) { OutUV = FVector2D(0.25f, V); return 2; }
	OutUV = FVector2D(0.f, 0.75f);
	return 0;
}

// ═════════════════════════════ КАРКАС ОКОН ═══════════════════════════════════════════════════════

FAsdaCwWindow* UAsdaCampWarBattleSubsystem::Win(int32 RootId) { return Windows.Find(RootId); }
const FAsdaCwWindow* UAsdaCampWarBattleSubsystem::Win(int32 RootId) const { return Windows.Find(RootId); }

UWidget* UAsdaCampWarBattleSubsystem::Node(int32 RootId, int32 Key) const
{
	const FAsdaCwWindow* W = Win(RootId);
	return W ? W->Ctl.FindRef(Key).Get() : nullptr;
}

bool UAsdaCampWarBattleSubsystem::VisModel(int32 Key, bool bDefault) const
{
	const bool* B = NodeVis.Find(Key);
	return B ? *B : bDefault;
}

bool UAsdaCampWarBattleSubsystem::EnsureWin(int32 RootId)
{
	FAsdaCwWindow* W = Win(RootId);
	const FCwWinDesc* D = Desc(RootId);
	if (!W || !D) { return false; }
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(FName(D->Id), D->Wbp, D->Z).Ensure(GetGameInstance(), W->Widget, W->bReady,
		[W](UAsdaWindowSubsystem&) { W->Widget = nullptr; W->bReady = false; W->Ctl.Reset(); W->Base.Reset(); },
		[this, RootId, W](UAsdaWindowSubsystem&) { SetupWin(RootId, *W); },
		[D]() { UE_LOG(LogAsdaCampWarWin, Error, TEXT("[битва армий/бой] окно %s не загрузилось (%s)"), D->Id, D->Wbp); });
	if (R == EAsdaJuiEnsure::Created)
	{
		RegisterZones();
		RedrawWin(RootId);
	}
	return FAsdaJuiWindowHandle::Succeeded(R) && W->bReady;
}

void UAsdaCampWarBattleSubsystem::ShowWin(int32 RootId, bool bShow, bool bTop)
{
	FAsdaCwWindow* W = Win(RootId);
	const FCwWinDesc* D = Desc(RootId);
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!W || !D || !WM) { return; }
	W->bShown = bShow;
	if (bShow)
	{
		if (!EnsureWin(RootId)) { return; }
		WM->SetWindowVisible(FName(D->Id), true);
		if (bTop) { WM->BringWindowToFront(FName(D->Id)); }
	}
	else if (W->Widget && WM->IsWindowLive(W->Widget))
	{
		WM->SetWindowVisible(FName(D->Id), false);
	}
}

void UAsdaCampWarBattleSubsystem::SyncWindows()
{
	// Окно видно моделью, а виджет умер (травел на карту 19 после 5055) — пересоздать, показать, перерисовать из модели.
	UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
	if (!WM) { return; }
	for (const FCwWinDesc& D : kWins)
	{
		FAsdaCwWindow* W = Win(D.Root);
		if (!W || !W->bShown) { continue; }
		if (W->Widget && W->bReady && WM->IsWindowLive(W->Widget)) { continue; }
		if (EnsureWin(D.Root)) { WM->SetWindowVisible(FName(D.Id), true); }
	}
}

void UAsdaCampWarBattleSubsystem::ResetWidgets()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	for (const FCwWinDesc& D : kWins)
	{
		if (WM) { WM->UnregisterUiZone(FName(D.Id)); WM->CloseWindow(FName(D.Id)); }
	}
	if (WM) { for (const FName& Z : ZoneNames) { WM->UnregisterUiZone(Z); } }   // ★ 31.10: зоны узлов табло и карты
	ZoneNames.Reset();
	for (const FCwWinDesc& D : kWins)
	{
		if (FAsdaCwWindow* W = Win(D.Root)) { W->Widget = nullptr; W->bReady = false; W->bShown = false; W->Ctl.Reset(); W->Base.Reset(); }
	}
	Relays.Reset();
	OrigBrush.Reset();
}

void UAsdaCampWarBattleSubsystem::RegisterZones()
{
	// Зоны окон — клик по окну не уходит в мир.
	UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
	if (!WM) { return; }
	// ★★★ 31.10.2026 [ИСПРАВЛЕНИЕ ПОРТА] (отзыв игроков «can't use top half of screen above my char»): ПРИЧИНА — накопитель
	//   `FAsdaPopupMenu::AccumRect` расширяет переданные Mn/Mx (min/max), а шлюз «курсор над окном» (`IsScreenPointOverAnyUiWindow`)
	//   отдаёт зоне НЕИНИЦИАЛИЗИРОВАННЫЕ Mn/Mx (локальные в цикле). Прежние лямбды их не сбрасывали — в прямоугольник зоны попадал
	//   мусор стека (на деле — прямоугольник предыдущей зоны и точка ≈ (0, −1)): табло 49600 давало (0,−1)–(885,168), карта 49900 —
	//   (0,−1)–(885,522), то есть почти вся верхняя половина экрана «окно» и клик в мир глотался (самотест-сетка 31.10: 448 из 768
	//   ячеек 1024×768 только из-за этих двух зон). Теперь накопитель сбрасывается в каждом вызове, и у каждого узла — своя зона
	//   (собственный прямоугольник `GetCachedGeometry`). Ключ отката `asda.CampWar.ZoneUnion 1` — прежнее объединение узлов (уже
	//   со сбросом накопителя).
	auto One = [this](int32 Root, int32 Key, FVector2D& Mn, FVector2D& Mx)
	{
		const FAsdaCwWindow* W = Win(Root);
		if (!W || !W->bShown) { return false; }
		Mn = FVector2D(FLT_MAX, FLT_MAX); Mx = FVector2D(-FLT_MAX, -FLT_MAX);   // накопитель — с нуля (см. выше)
		bool bAny = false;
		if (UWidget* N = Node(Root, Key)) { FAsdaPopupMenu::AccumRect(N, Mn, Mx, bAny); }
		return bAny;
	};
	auto Union = [this](int32 Root, std::initializer_list<int32> Keys, FVector2D& Mn, FVector2D& Mx)
	{
		const FAsdaCwWindow* W = Win(Root);
		if (!W || !W->bShown) { return false; }
		Mn = FVector2D(FLT_MAX, FLT_MAX); Mx = FVector2D(-FLT_MAX, -FLT_MAX);
		bool bAny = false;
		for (int32 K : Keys) { if (UWidget* N = Node(Root, K)) { FAsdaPopupMenu::AccumRect(N, Mn, Mx, bAny); } }
		return bAny;
	};
	for (const FName& Z : ZoneNames) { WM->UnregisterUiZone(Z); }
	ZoneNames.Reset();
	if (AsdaCwBattle::ZoneUnionLegacy())
	{
		ZoneNames.Add(FName(TEXT("CAMPWAR_Point")));
		ZoneNames.Add(FName(TEXT("CAMPWAR_L_MAP")));
		WM->RegisterUiZone(ZoneNames[0], FAsdaUiZoneRect::CreateWeakLambda(this, [Union](FVector2D& Mn, FVector2D& Mx)
			{ return Union(kWinPoint, { 49732, 49730, 49731 }, Mn, Mx); }));
		WM->RegisterUiZone(ZoneNames[1], FAsdaUiZoneRect::CreateWeakLambda(this, [this, Union](FVector2D& Mn, FVector2D& Mx)
			{ return VisModel(49904, false) ? Union(kWinMap, { kWinMap, 49902, 49903 }, Mn, Mx) : Union(kWinMap, { 49902, 49903 }, Mn, Mx); }));
	}
	else
	{
		// Табло очков: группа цифр 49732 и кнопки свёртки 49730/49731 — каждая своей зоной.
		for (int32 K : { 49732, 49730, 49731 })
		{
			const FName Z(*FString::Printf(TEXT("CAMPWAR_Point.%d"), K));
			ZoneNames.Add(Z);
			WM->RegisterUiZone(Z, FAsdaUiZoneRect::CreateWeakLambda(this, [One, K](FVector2D& Mn, FVector2D& Mx) { return One(kWinPoint, K, Mn, Mx); }));
		}
		// Карта: квадрат 200×200 — только пока развёрнута (49904 виден), кнопки 49902/49903 — всегда (каждая своей зоной).
		for (int32 K : { (int32)kWinMap, 49902, 49903 })
		{
			const FName Z(*FString::Printf(TEXT("CAMPWAR_L_MAP.%d"), K));
			ZoneNames.Add(Z);
			WM->RegisterUiZone(Z, FAsdaUiZoneRect::CreateWeakLambda(this, [this, One, K](FVector2D& Mn, FVector2D& Mx)
				{ return (K == kWinMap && !VisModel(49904, false)) ? false : One(kWinMap, K, Mn, Mx); }));
		}
	}
	for (const FCwWinDesc& D : kWins)
	{
		const TCHAR* Fmt = (D.Root == kWinTime) ? TEXT("CAMPWAR_Time_Win_0_%d") : (D.Root == kWinEnd) ? TEXT("CAMPWAR_End_0_%d")
			: (D.Root == kWinAtk) ? TEXT("CAMPWAR_ATTACK_0_%d") : (D.Root == kWinPop) ? TEXT("CAMPWAR_ATTACK_Position_0_%d") : nullptr;
		FAsdaCwWindow* W = Win(D.Root);
		if (Fmt && W) { FAsdaJuiWindowHandle::RegisterFrameZone(*WM, FName(D.Id), this, W->Widget, W->bShown, Fmt, 9); }
	}
	// ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: щелчок мимо гасит попап 49400; разбор — `AsdaWindowSubsystem_TopPress.cpp`.
	WM->RegisterClickAwayMenu(FName(TEXT("CAMPWAR_ATTACK_Position")), FSimpleDelegate::CreateWeakLambda(this, [this]() { ShowWin(kWinPop, false); }));
}

void UAsdaCampWarBattleSubsystem::SetVisNode(int32 RootId, int32 Key, bool bVis)
{
	NodeVis.Add(Key, bVis);
	UWidget* N = Node(RootId, Key);
	if (!N) { return; }
	const ESlateVisibility V = !bVis ? ESlateVisibility::Collapsed : N->IsA<UButton>() ? ESlateVisibility::Visible : ESlateVisibility::SelfHitTestInvisible;
	if (N->GetVisibility() != V) { N->SetVisibility(V); }
}

void UAsdaCampWarBattleSubsystem::SetTextNode(int32 RootId, int32 Key, const FString& S)
{
	NodeText.Add(Key, S);
	if (UTextBlock* T = Cast<UTextBlock>(Node(RootId, Key))) { T->SetText(FText::FromString(S)); }
}

void UAsdaCampWarBattleSubsystem::PlaceNode(int32 RootId, int32 Key, int32 X, int32 Y)
{
	// `CControl_SetField74_75`: −1 — координата не меняется. Координаты — в канвасе родителя узла.
	UWidget* N = Node(RootId, Key);
	if (!N) { return; }
	FVector2D P = SlotPosOf(N);
	if (X != -1) { P.X = (float)X; }
	if (Y != -1) { P.Y = (float)Y; }
	if (!P.Equals(SlotPosOf(N))) { SetSlotPosOf(N, P); }
}

void UAsdaCampWarBattleSubsystem::DrawTexRect(UImage* Img, const TCHAR* Tex, int32 X, int32 Y, int32 W, int32 H)
{
	// `CControl_DrawTextureRect(ctrl, tex, x, y, w, h)` — u = x / ширина атласа (логическая), v = y / высота.
	if (!Img) { return; }
	UAsdaTextureSubsystem* TS = GEngine ? GEngine->GetEngineSubsystem<UAsdaTextureSubsystem>() : nullptr;
	UTexture2D* T = TS ? TS->LoadUITexture(FName(Tex)) : nullptr;
	if (!T) { return; }
	const FIntPoint Sz = AsdaUi::LogicalAtlasSize(T);
	FSlateBrush B = Img->GetBrush();
	B.SetResourceObject(T);
	B.DrawAs = ESlateBrushDrawType::Image;
	B.Tiling = ESlateBrushTileType::NoTile;
	B.SetUVRegion(FBox2f(FVector2f((float)X / Sz.X, (float)Y / Sz.Y), FVector2f((float)(X + W) / Sz.X, (float)(Y + H) / Sz.Y)));
	Img->SetBrush(B);
}

// ═════════════════════════════ РАЗБОР КОНТРОЛОВ (init эталона) ═══════════════════════════════════

void UAsdaCampWarBattleSubsystem::SetupWin(int32 RootId, FAsdaCwWindow& W)
{
	W.bReady = false;
	W.Ctl.Reset();
	W.Base.Reset();
	if (!W.Widget) { return; }
	UUserWidget* U = W.Widget;
	int32 Found = 0, Total = 0;
	TArray<FString> Missing;
	auto Add = [&](int32 Key, const FString& Name) -> UWidget*
	{
		++Total;
		UWidget* N = U->GetWidgetFromName(FName(*Name));
		if (!N) { Missing.Add(Name); }
		if (N)
		{
			++Found;
			W.Ctl.Add(Key, N);
			W.Base.Add(Key, SlotPosOf(N));
			if (UImage* I = Cast<UImage>(N)) { if (!OrigBrush.Contains(Key)) { OrigBrush.Add(Key, I->GetBrush()); } }
		}
		return N;
	};
	auto Bind = [&](int32 Key, TFunction<void()>&& Fn)
	{
		if (UButton* Btn = Cast<UButton>(W.Ctl.FindRef(Key).Get()))
		{
			AsdaUi::BindButton(this, Btn, Key, [Fn2 = MoveTemp(Fn)](int32) { Fn2(); }, Relays);
		}
	};
	switch (RootId)
	{
	case kWinPoint:
	{
		// `CCampWar_InitControls`, часть 49600 (G:306690-306737).
		Add(kWinPoint, TEXT("CAMPWAR_Point_0_HIT"));
		Add(49732, TEXT("CAMPWAR_Point_CON_grp"));
		Add(49730, TEXT("CAMPWAR_Point_BB"));
		Add(49731, TEXT("CAMPWAR_Point_BB2"));
		Add(49729, TEXT("CAMPWAR_Point_X"));
		const TCHAR* kMark[3] = { TEXT("CAMPWAR_Point_mark1_3_0"), TEXT("CAMPWAR_Point_mark2_76_0"), TEXT("CAMPWAR_Point_mark3_81_0") };
		for (int32 F = 0; F < kArmies; ++F)
		{
			const TCHAR* Suf = PackSuffix(F);
			if (UImage* M = Cast<UImage>(Add(49601 + F, kMark[F]))) { UAsdaClanFlag::ApplyCampMark(M, F, 32.f); }   // `uiMakeAxisVec2(f)`, 64×64
			Add(PackKey(F), FString(TEXT("CAMPWAR_Point_countpack_grp")) + Suf);
			Add(CommaKey(F), FString(TEXT("CAMPWAR_Point_3_com_38_0")) + Suf);
			Add(49721 + 2 * F, FString::Printf(TEXT("CAMPWAR_3Point_m%d_grp"), F + 1));
			Add(49722 + 2 * F, FString::Printf(TEXT("CAMPWAR_3Point_%d"), F + 1));
			for (int32 J = 0; J < 6; ++J)
			{
				UPanelWidget* Pos = Cast<UPanelWidget>(Add(DigitPosKey(F, J), FString::Printf(TEXT("CAMPWAR_Point_%d_grp"), J + 1) + Suf));
				for (int32 K = 0; K < 10; ++K)
				{
					++Total;
					if (UWidget* G = ChildByPrefix(Pos, FString::Printf(TEXT("CAMPWAR_Point_%d_%d_"), J + 1, K), Suf))
					{
						++Found;
						W.Ctl.Add(DigitKey(F, J, K), G);
					}
				}
			}
			// Клоны `countpack` видимы, клоны глифов скрыты, клон запятой видим; 49721+2f «3Point_m» — код делает ВИДИМЫМ.
			SetVisNode(kWinPoint, PackKey(F), true);
			SetVisNode(kWinPoint, CommaKey(F), true);
			SetVisNode(kWinPoint, 49721 + 2 * F, true);
		}
		// 49730 init: видим; 49731: скрыт (WBP — наоборот; видимость — логика эталона).
		if (!NodeVis.Contains(49730)) { SetVisNode(kWinPoint, 49730, true); SetVisNode(kWinPoint, 49731, false); SetVisNode(kWinPoint, 49732, true); }
		Bind(49730, [this]() { SetVisNode(kWinPoint, 49730, false); SetVisNode(kWinPoint, 49731, true); SetVisNode(kWinPoint, 49732, false); });
		Bind(49731, [this]() { SetVisNode(kWinPoint, 49730, true); SetVisNode(kWinPoint, 49731, false); SetVisNode(kWinPoint, 49732, true); });
		// Исходные позиции детей корня — для сдвига миникартой.
		if (UCanvasPanel* Root = Cast<UCanvasPanel>(U->GetRootWidget()))
		{
			for (int32 I = 0; I < Root->GetChildrenCount(); ++I) { W.Base.Add(3000000 + I, SlotPosOf(Root->GetChildAt(I))); }
		}
		PointShift = 0.f;
		W.bReady = W.Ctl.Contains(49732) && W.Ctl.Contains(DigitKey(0, 0, 0));
		break;
	}
	case kWinMap:
	{
		// `CCampWar_InitControls`, часть 49900: 49902/49903/49904, 10 таймеров (главный + 5 NoImage).
		Add(kWinMap, TEXT("CAMPWAR_L_MAP_0_0"));
		Add(49901, TEXT("CAMPWAR_L_MAP_X"));
		Add(49902, TEXT("CAMPWAR_L_MAP_BB"));
		Add(49903, TEXT("CAMPWAR_L_MAP_BB2"));
		Add(49904, TEXT("CAMPWAR_MOVE_2_0"));
		for (int32 I = 0; I < kSlots; ++I)
		{
			UPanelWidget* Grp = Cast<UPanelWidget>(Add(TimerMainKey(I), (I < 9) ? FString::Printf(TEXT("CAMPWAR_TIME_%d_grp"), I + 1) : FString(TEXT("CAMPWAR_TIME10_grp"))));
			// Картинки группы по порядку: [0] подложка 24×24 главного, затем `CAMPWAR_BACK_n` (NoImage[0]) и 4 глифа 9×9 (NoImage[1..4]).
			TArray<UImage*> Imgs;
			if (Grp) { for (UWidget* C : Grp->GetAllChildren()) { if (UImage* Im = Cast<UImage>(C)) { Imgs.Add(Im); } } }
			for (int32 J = 0; J < 5; ++J)
			{
				++Total;
				if (Imgs.IsValidIndex(J + 1)) { ++Found; W.Ctl.Add(TimerGlyphKey(I, J), Imgs[J + 1]); W.Base.Add(TimerGlyphKey(I, J), SlotPosOf(Imgs[J + 1])); }
			}
		}
		// Клик 49902 (66561): 49902 скрыть, 49903 показать, 49904 скрыть — свернуть карту; 49903 — обратное.
		Bind(49902, [this]() { SetVisNode(kWinMap, 49902, false); SetVisNode(kWinMap, 49903, true); SetVisNode(kWinMap, 49904, false); });
		Bind(49903, [this]() { SetVisNode(kWinMap, 49902, true); SetVisNode(kWinMap, 49903, false); SetVisNode(kWinMap, 49904, true); });
		TimerSig.Reset();
		W.bReady = W.Ctl.Contains(kWinMap) && W.Ctl.Contains(49902) && W.Ctl.Contains(TimerMainKey(0));
		break;
	}
	case kWinFont:
	{
		Add(kWinFont, TEXT("CAMPWAR_FONT_Win_0_0"));
		Add(49911, TEXT("CAMPWAR_FONT1_grp"));
		Add(49912, TEXT("CAMPWAT_FONT1_1_2_0"));
		Add(49913, TEXT("CAMPWAR_FONT2_3_0"));
		Add(49914, TEXT("CAMPWAR_FONT_CBOX_grp"));
		for (int32 N = 0; N < 5; ++N) { Add(49915 + N, FString::Printf(TEXT("CAMPWAR_FONT_C%d_%d_0"), N + 1, N + 5)); }
		Add(49920, TEXT("CAMPWAR_FONT2_1_10_0"));
		Add(49921, TEXT("CAMPWAR_FONT2_2_11_0"));
		W.bReady = W.Ctl.Contains(49911) && W.Ctl.Contains(49913) && W.Ctl.Contains(49921);
		break;
	}
	case kWinTime:
	{
		// 49940: 49941 ← 12510, 49942 ← "", 49500 «Рейд» (подпись коллекции "", 49501 ← 10052).
		Add(kWinTime, TEXT("CAMPWAR_Time_Win_0_0"));
		Add(49941, TEXT("CAMPWAR_Time_Text1"));
		Add(49942, TEXT("CAMPWAR_Time_Text2"));
		Add(49500, TEXT("CAMPWAR_Button_grp"));
		Add(49500 + kAlt, TEXT("CAMPWAR_Button"));
		Add(49501, TEXT("CAMPWAR_Button_Text_1"));
		Add(49501 + kAlt, TEXT("CAMPWAR_Button_Text"));
		if (!NodeText.Contains(49942)) { NodeText.Add(49942, FString()); }
		NodeText.Add(49941, Loc(12510));
		NodeText.Add(49501, Loc(10052));
		NodeText.Add(49501 + kAlt, FString());
		if (UButton* Btn = Cast<UButton>(W.Ctl.FindRef(49500 + kAlt).Get()))
		{
			AsdaUi::BindButton(this, Btn, 49500, [this](int32) { OnRaidButton(); }, Relays);
		}
		W.bReady = W.Ctl.Contains(49942) && W.Ctl.Contains(49500 + kAlt);
		break;
	}
	case kWinHold:
	{
		// 49510: полоса 49521 (подложка + заливка), подпись 49522 ← 10007.
		Add(kWinHold, TEXT("CAPWAR_Hold_ING_0_0"));
		Add(49521, TEXT("CAPWAR_Hold_ING_SLIDE_1_1"));
		Add(49521 + kAlt, TEXT("CAPWAR_Hold_ING_SLIDE_1_0"));
		Add(49522, TEXT("CAPWAR_Hold_ING_SLIDE_Text"));
		NodeText.Add(49522, Loc(10007));
		W.bReady = W.Ctl.Contains(49521);
		break;
	}
	case kWinEnd:
	{
		// `CCampWarEndWnd_InitControls@0xC9A1E0`.
		Add(kWinEnd, TEXT("CAMPWAR_End_0_0"));
		Add(49801, TEXT("CAMPWAR_End_High_Text"));
		Add(49802, TEXT("CAMPWAR_End_Mark_R1_5_0"));
		Add(49805, TEXT("CAMPWAR_End_M_Text_1"));
		Add(49806, TEXT("CAMPWAR_End_M_Text_2"));
		for (int32 I = 0; I < 3; ++I)
		{
			Add(49810 + 10 * I, kEndMark[I]);
			Add(49811 + 10 * I, kEndFront[I]);
			Add(49814 + 10 * I, kEndBack[I]);
			Add(49812 + 10 * I, FString::Printf(TEXT("CAMPWAR_End_1_B%d_Text1"), I + 1));
			Add(49813 + 10 * I, FString::Printf(TEXT("CAMPWAR_End_1_B%d_Text2"), I + 1));
			Add(49850 + 10 * I, kEndIcon[I]);
			Add(49850 + 10 * I + kAlt, FString::Printf(TEXT("CAMPWAR_End_2_Item%d_M"), I + 1));
			Add(49851 + 10 * I, FString::Printf(TEXT("CAMPWAR_End_2_Item%d_Text"), I + 1));
		}
		for (int32 K = 0; K < 5; ++K) { Add(49840 + K, FString::Printf(TEXT("CAMPWAR_End_2_W_Text%d"), K + 1)); }
		Add(49845, TEXT("CAMPWAR_End_2_P_Text1"));
		Add(49880, TEXT("CAMPWAR_End_NO_B"));
		Add(49880 + kAlt, TEXT("CAMPWAR_End_NO_B_Text"));
		Add(49881, TEXT("CAMPWAR_End_X"));
		Add(49882, TEXT("CAMPWAR_End_NO_grp"));
		NodeText.Add(49801, Loc(10121));
		NodeText.Add(49840, Loc(10125));
		NodeText.Add(49841, Loc(10127));
		NodeText.Add(49843, Loc(10128));
		NodeText.Add(49845, Loc(10126));
		NodeText.Add(49880 + kAlt, Loc(10046));   // `Collection_AddString(49880, 10046)`
		for (int32 I = 0; I < 3; ++I) { NodeVis.Add(49814 + 10 * I, true); }   // `CControl_SetVisible(49814+10i, 1)`
		Bind(49880, [this]() { OnEndExit(); });
		Bind(49881, [this]() { OnEndExit(); });
		W.bReady = W.Ctl.Contains(49801) && W.Ctl.Contains(49880) && W.Ctl.Contains(49811);
		break;
	}
	case kWinAtk:
	{
		// `CCampWar_InitAttackListControls` (0xC99095…): 49000 ← 10051, 49010+i ← 10052+i, 15 строк по 10 контролов, 49330 рамка.
		Add(kWinAtk, TEXT("CAMPWAR_ATTACK_0_0"));
		Add(49000, TEXT("CAMPWAR_ATTACK_H_TEXT"));
		Add(49001, TEXT("CAMPWAR_ATTACK_X"));
		Add(49320, TEXT("CAMPWAR_ATTACK_M0_2_grp"));   // у 49320 (колесо) своего узла нет — область списка
		NodeText.Add(49000, Loc(10051));
		for (int32 I = 0; I < 9; ++I)
		{
			Add(49010 + I, FString::Printf(TEXT("CAMPWAR_ATTACK_L_HIGH_T%d"), I + 1));
			NodeText.Add(49010 + I, Loc(10052 + I));
		}
		for (int32 R = 0; R < kRows; ++R)
		{
			const FString P = FString::Printf(TEXT("CAMPWAR_ATTACK_L_%02d"), R + 1);
			UPanelWidget* Row = Cast<UPanelWidget>(Add(RowKey(R, 0), P + TEXT("_grp")));
			// Узлы строки ищутся ПО ПОДДЕРЕВУ строки: имена ассета сбиты (у строки 11 тексты названы `L_12_T2…`, у строки 12 —
			//   `L_12_T2_1…`; подсветка TB_R у строк 1/8…15 — `L_02_TB_R` / `_TB_B_`; группы GONG у строк 9/13 — `_GON_grp`). Тексты:
			//   `…_T<n>[_k]`; подсветка — `_TB_R_`/`_TB_B_`.
			TArray<UWidget*> Desc;
			if (Row) { UWidgetTree::ForWidgetAndChildren(Row, [&Desc](UWidget* D) { Desc.Add(D); }); }
			auto FindText = [&](int32 N) -> UWidget*
			{
				for (UWidget* D : Desc)
				{
					if (!Cast<UTextBlock>(D)) { continue; }
					const FString Nm = D->GetName();
					// Имя `…_T<n>` или `…_T<n>_<k>`.
					const FString Tag = FString::Printf(TEXT("_T%d"), N);
					if (Nm.EndsWith(Tag) || Nm.Contains(Tag + TEXT("_"))) { if (!Nm.Contains(Tag + TEXT("_grp"))) { return D; } }
				}
				return nullptr;
			};
			auto FindImg = [&](const TCHAR* A, const TCHAR* Bk) -> UWidget*
			{
				for (UWidget* D : Desc) { const FString Nm = D->GetName(); if (Cast<UImage>(D) && (Nm.Contains(A) || (Bk && Nm.Contains(Bk)))) { return D; } }
				return nullptr;
			};
			auto Put = [&](int32 K, UWidget* N)
			{
				++Total;
				if (!N) { Missing.Add(FString::Printf(TEXT("%s[%d]"), *P, K)); return; }
				++Found;
				W.Ctl.Add(RowKey(R, K), N);
				if (UImage* Im = Cast<UImage>(N)) { if (!OrigBrush.Contains(RowKey(R, K))) { OrigBrush.Add(RowKey(R, K), Im->GetBrush()); } }
			};
			Put(2, FindText(1)); Put(4, FindText(2)); Put(5, FindText(3)); Put(7, FindText(4)); Put(8, FindText(5)); Put(9, FindText(6));
			// Иконки — ПО СТРУКТУРЕ (имена групп/плит сбиты: у строки 13 группа GONG названа `_MYO_grp`, а MYO — `_MYO_grp_1`; у 9/13
			//   `_GON_`). JUI: 49020 GONG ⊃ 49022 MYO ⊃ 49025 MARK ⇒ группа GONG — прямой ребёнок строки (не `_TB_grp` и не `_T1_grp`):
			//   [1] — её первая картинка; её вложенная группа — MYO: [3] первая картинка, [6] вторая.
			UPanelWidget* GongG = nullptr;
			UPanelWidget* MyoG = nullptr;
			if (Row)
			{
				for (UWidget* C2 : Row->GetAllChildren())
				{
					UPanelWidget* G = Cast<UPanelWidget>(C2);
					const FString Nm = C2 ? C2->GetName() : FString();
					if (G && !Nm.Contains(TEXT("_TB_grp")) && !Nm.Contains(TEXT("_T1_grp"))) { GongG = G; break; }
				}
			}
			if (GongG) { for (UWidget* C2 : GongG->GetAllChildren()) { if (UPanelWidget* G = Cast<UPanelWidget>(C2)) { MyoG = G; break; } } }
			auto NthImg = [](UPanelWidget* G, int32 Nth) -> UWidget*
			{
				int32 I = 0;
				if (G) { for (UWidget* C2 : G->GetAllChildren()) { if (Cast<UImage>(C2) && I++ == Nth) { return C2; } } }
				return nullptr;
			};
			Put(1, NthImg(GongG, 0));
			Put(3, NthImg(MyoG, 0));
			Put(6, NthImg(MyoG, 1));
			Put(10, FindImg(TEXT("_TB_R_"), TEXT("_TB_B_")));
		}
		for (int32 K = 0; K < 8; ++K) { Add(FrameKey(K), FString::Printf(TEXT("CAMPWAR_ATTACK_L_CLICK_290_%d"), K)); }
		Bind(49001, [this]() { CloseAttack(); });
		W.bReady = W.Ctl.Contains(RowKey(0, 0)) && W.Ctl.Contains(RowKey(0, 2)) && W.Ctl.Contains(49001);
		break;
	}
	case kWinPop:
	{
		// 49400: 49410 ← 10061, 49420 ← 10062, 49430 ← 10063 → перезаписано 111930 и ВИДИМО (в WBP свёрнуто), 49440 X.
		Add(kWinPop, TEXT("CAMPWAR_ATTACK_Position_0_0"));
		Add(49410, TEXT("CAMPWAR_ATTACK_Position_1"));
		Add(49410 + kAlt, TEXT("CAMPWAR_ATTACK_Position_1_Text"));
		Add(49420, TEXT("CAMPWAR_ATTACK_Position_2"));
		Add(49420 + kAlt, TEXT("CAMPWAR_ATTACK_Position_2_Text"));
		Add(49430, TEXT("CAMPWAR_ATTACK_Position_3"));
		Add(49430 + kAlt, TEXT("CAMPWAR_ATTACK_Position_3_Text"));
		Add(49440, TEXT("CAMPWAR_ATTACK_Position_X"));
		NodeText.Add(49410 + kAlt, Loc(10061));
		NodeText.Add(49420 + kAlt, Loc(10062));
		NodeText.Add(49430 + kAlt, Loc(111930));
		NodeVis.Add(49430, true);
		NodeVis.Add(49430 + kAlt, true);
		Bind(49410, [this]() { OnPopupInvite(); });
		Bind(49420, [this]() { OnPopupCaptain(); });
		Bind(49430, [this]() { OnPopupKick(); });
		Bind(49440, [this]() { ShowWin(kWinPop, false); });
		W.bReady = W.Ctl.Contains(49410) && W.Ctl.Contains(49420) && W.Ctl.Contains(49430);
		break;
	}
	default: break;
	}
	FoundControls += Found; TotalControls += Total;
	// Окно годно, раз виджет есть: недостающие узлы просто не рисуются (иначе каркас пересоздавал бы окно каждый кадр).
	const bool bKey = W.bReady;
	W.bReady = true;
	UE_LOG(LogAsdaCampWarWin, Log, TEXT("[битва армий/бой] окно %d: разобрано %d из %d контролов%s%s%s"), RootId, Found, Total,
		bKey ? TEXT("") : TEXT(" — КЛЮЧЕВЫЕ НЕ РАЗОБРАНЫ"), Missing.Num() ? TEXT("; нет: ") : TEXT(""),
		*FString::Join(TArrayView<const FString>(Missing.GetData(), FMath::Min(Missing.Num(), 12)), TEXT(", ")));
}

void UAsdaCampWarBattleSubsystem::RedrawWin(int32 RootId)
{
	// Модель → окно (после создания/пересоздания): видимость и тексты узлов, затем содержимое окна.
	FAsdaCwWindow* W = Win(RootId);
	if (!W) { return; }
	for (const TPair<int32, TObjectPtr<UWidget>>& P : W->Ctl)
	{
		if (const bool* V = NodeVis.Find(P.Key)) { SetVisNode(RootId, P.Key, *V); }
		if (const FString* T = NodeText.Find(P.Key)) { SetTextNode(RootId, P.Key, *T); }
	}
	switch (RootId)
	{
	case kWinPoint: DrawDigits(); DrawHeadcount(); break;
	case kWinMap: TimerSig.Reset(); UpdateObjectTimers(); break;
	case kWinFont:
		for (int32 K : { 49911, 49912, 49913, 49920, 49921, 49915, 49916, 49917, 49918, 49919 })
		{
			if (!NodeVis.Contains(K)) { SetVisNode(kWinFont, K, false); }
		}
		break;
	case kWinEnd:
		if (bEndDraw && LastEnd.Num() >= 65) { ShowEndWindow(LastEnd); } else { ResetEndWindow(); }
		break;
	case kWinAtk: if (W->bShown) { DrawMemberList(); } break;
	default: break;
	}
}

// ═════════════════════════════ 49600 ОЧКИ АРМИЙ ══════════════════════════════════════════════════

void UAsdaCampWarBattleSubsystem::ShowPointPanel()
{
	// `uiWidgetShowPanel@0xC9DE00`: 49600 видим + `UI_SetNumericDigitDisplay`.
	ShowWin(kWinPoint, true);
	DrawDigits();
}

void UAsdaCampWarBattleSubsystem::DrawDigits()
{
	// `UI_SetNumericDigitDisplay@0xC9DE90`: 6 разрядов, ведущие нули показываются; d = v / 10^(5−j), прочие 9 глифов скрыты.
	for (int32 F = 0; F < kArmies; ++F)
	{
		int64 V = Score[F];
		for (int32 J = 0; J < 6; ++J)
		{
			const int64 Pow = (int64)FMath::Pow(10.0, (double)(5 - J));
			const int64 D = V / Pow;
			for (int32 K = 0; K < 10; ++K) { SetVisNode(kWinPoint, DigitKey(F, J, K), D == K); }
			V -= D * Pow;
		}
	}
}

void UAsdaCampWarBattleSubsystem::DrawHeadcount()
{
	// 6742: «%d/%d» (численность, `CampWar_BaseTable.+0x10` строки `sub_CA1440`: карта 3 → 0, 0 → 1, 7 → 2, иначе 0).
	UAsdaCampWarSubsystem* CW = GetGameInstance()->GetSubsystem<UAsdaCampWarSubsystem>();
	const FAsdaCampWarBaseRow* Row = CW ? CW->GetBaseRow(CW->CurrentMapSlot()) : nullptr;
	if (!Row) { return; }
	for (int32 F = 0; F < kArmies; ++F) { SetTextNode(kWinPoint, 49722 + 2 * F, FString::Printf(TEXT("%d/%d"), Headcount[F], Row->Field10)); }
}

void UAsdaCampWarBattleSubsystem::LayoutPointPanel()
{
	// Миникарта развёрнута (шире 136) → 49600 на X−128 (`CControl_AnimateSlide74_75(…, 0)`, G:373846), свёрнута → (0, 0).
	const FAsdaCwWindow* W = Win(kWinPoint);
	if (!W || !W->bReady || !W->Widget) { return; }
	const UAsdaMinimapSubsystem* MM = GetGameInstance()->GetSubsystem<UAsdaMinimapSubsystem>();
	const float Want = (MM && MM->IsExpanded()) ? -128.f : 0.f;
	if (FMath::IsNearlyEqual(Want, PointShift)) { return; }
	if (UCanvasPanel* Root = Cast<UCanvasPanel>(W->Widget->GetRootWidget()))
	{
		for (int32 I = 0; I < Root->GetChildrenCount(); ++I)
		{
			if (const FVector2D* B = W->Base.Find(3000000 + I)) { SetSlotPosOf(Root->GetChildAt(I), *B + FVector2D(Want, 0.f)); }
		}
	}
	PointShift = Want;
}

// ═════════════════════════════ 49900 ТАЙМЕРЫ ТОЧЕК ═══════════════════════════════════════════════

void UAsdaCampWarBattleSubsystem::UpdateObjectTimers()
{
	// `CCampWar_UpdateObjectTimers@0xCA7AA0` (BATTLE_SPEC §3.3).
	const FAsdaCwWindow* W = Win(kWinMap);
	if (!W || !W->bReady) { return; }
	const FVector2D MapBase = W->Base.FindRef(kWinMap);   // позиция 49900 в корне окна (родитель таймеров)
	auto PlaceMain = [&](int32 I)
	{
		const FVector2D P = PointMapPos(I);
		PlaceNode(kWinMap, TimerMainKey(I), (int32)(MapBase.X + P.X + 5.f), (int32)(MapBase.Y + P.Y + 3.f));
	};
	if (!VisModel(49902, false))
	{
		for (int32 I = 0; I < kSlots; ++I) { PlaceMain(I); SetVisNode(kWinMap, TimerMainKey(I), false); }
		return;
	}
	for (int32 I = 0; I < kSlots; ++I)
	{
		const FAsdaCwPointSlot& P = Slots[I];
		bool bShow = (P.State == 1);
		if (P.Owner == 0 && I == 0) { bShow = true; }
		if (P.Owner == 1 && I == 6) { bShow = true; }
		if (P.Owner == 2 && I == 8) { bShow = true; }
		PlaceMain(I);
		SetVisNode(kWinMap, TimerMainKey(I), bShow);
		if (!bShow) { continue; }
		int32 Total = (int32)((double)P.TimerMs / 1000.0);
		if (Total <= 0) { Total = 1; }
		int32 Parts[4] = { Total / 86400, Total % 86400 / 3600, Total % 86400 % 3600 / 60, Total % 60 };
		const int32 Days = Parts[0], Hours = Parts[1], Mins = Parts[2], Secs = Parts[3];
		int32 First = 0, Unit = -1;
		for (int32 J = 0; J < 4; ++J)
		{
			if (Parts[J] > 99) { Parts[J] = 99; }
			if (Parts[J]) { First = Parts[J]; Unit = J; break; }
		}
		const TCHAR* Tex = (P.State == 2) ? TEXT("GreenBufftime") : TEXT("bufftime");
		// Подпись кадра: перерисовываем глифы только при её смене.
		const FString Sig = FString::Printf(TEXT("%s|%d|%d|%d|%d|%d"), Tex, Days, Hours, Mins, Secs, First);
		if (TimerSig.FindRef(TimerMainKey(I)) == Sig) { continue; }
		TimerSig.Add(TimerMainKey(I), Sig);
		auto G = [&](int32 J) { return Cast<UImage>(Node(kWinMap, TimerGlyphKey(I, J))); };
		auto Glyph = [&](int32 J, int32 U) { DrawTexRect(G(J), Tex, U, 0, 9, 9); };
		if (Days || Hours || Mins > 9 || !Mins)
		{
			const FString Str = FString::FromInt(First);
			PlaceNode(kWinMap, TimerGlyphKey(I, 1), 1, 24); PlaceNode(kWinMap, TimerGlyphKey(I, 2), 7, 24);
			PlaceNode(kWinMap, TimerGlyphKey(I, 3), 14, 24); PlaceNode(kWinMap, TimerGlyphKey(I, 4), 4, 24);
			SetVisNode(kWinMap, TimerGlyphKey(I, 4), false);
			const int32 UnitU = 9 * (Unit + 10);   // d 90, h 99, m 108, s 117
			if (Str.Len() == 1)
			{
				Glyph(1, 0);
				Glyph(2, 9 * (Str[0] - '0'));
				Glyph(3, UnitU);
			}
			if (Str.Len() == 2)
			{
				Glyph(1, 9 * (Str[0] - '0'));
				Glyph(2, 9 * (Str[1] - '0'));
				Glyph(3, UnitU);
			}
			if (Str.Len() == 3)
			{
				// Недостижимо (часть ≤ 99), перенесено как есть. [1] — `SetField74_75(−1, 24)`: X не меняется.
				PlaceNode(kWinMap, TimerGlyphKey(I, 1), -1, 24); PlaceNode(kWinMap, TimerGlyphKey(I, 2), 4, 24);
				PlaceNode(kWinMap, TimerGlyphKey(I, 3), 10, 24); PlaceNode(kWinMap, TimerGlyphKey(I, 4), 16, 24);
				SetVisNode(kWinMap, TimerGlyphKey(I, 4), true);
				Glyph(1, 9 * (Str[0] - '0'));
				Glyph(2, 9 * (Str[1] - '0'));
				Glyph(TimerThirdDigitFix() ? 3 : 2, 9 * (Str[2] - '0'));   // Д-Б3
				Glyph(4, UnitU);
			}
		}
		else
		{
			// 1…9 мин: M:SS — [1] минуты, [2][3] секунды, [4] двоеточие (126, 0).
			const FString Str = FString::FromInt(Secs);
			PlaceNode(kWinMap, TimerGlyphKey(I, 1), 0, 24); PlaceNode(kWinMap, TimerGlyphKey(I, 2), 8, 24);
			PlaceNode(kWinMap, TimerGlyphKey(I, 3), 14, 24); PlaceNode(kWinMap, TimerGlyphKey(I, 4), 4, 24);
			SetVisNode(kWinMap, TimerGlyphKey(I, 4), true);
			Glyph(1, 9 * Mins);
			if (Str.Len() == 1) { Glyph(2, 0); Glyph(3, 9 * (Str[0] - '0')); }
			if (Str.Len() == 2) { Glyph(2, 9 * (Str[0] - '0')); Glyph(3, 9 * (Str[1] - '0')); }
			Glyph(4, 126);
		}
	}
}

// ═════════════════════════════ 49910 КРУПНЫЕ НАДПИСИ ═════════════════════════════════════════════

void UAsdaCampWarBattleSubsystem::ShowModeControls(int32 Code, int32 N)
{
	// `uiPanel_ShowControlsByMode@0xCA1D10`: спрятать детей, показать 49910; 7 «START N min», 8 «CampWar START» (+ таймер),
	//   9 «End N min» (+ таймер), 10 — только корень. Показанным — `objStartTimedAction(ctrl, 255, 0, 3000.0)`.
	ShowWin(kWinFont, true);
	for (int32 K : { 49911, 49912, 49913, 49920, 49921, 49915, 49916, 49917, 49918, 49919 })
	{
		SetVisNode(kWinFont, K, false);
		FadeMs.Remove(K);
		if (UWidget* Nd = Node(kWinFont, K)) { Nd->SetRenderOpacity(1.f); }
	}
	auto ShowFade = [this](int32 K) { SetVisNode(kWinFont, K, true); FadeMs.Add(K, 0.f); };
	UAsdaCampWarSubsystem* CW = GetGameInstance()->GetSubsystem<UAsdaCampWarSubsystem>();
	switch (Code)
	{
	case 7:
		ShowFade(49913); ShowFade(49920);
		if (N >= 1 && N <= 5) { ShowFade(49915 + N - 1); }
		break;
	case 8:
		ShowFade(49911); ShowFade(49912);
		if (CW) { TimerMin = (CW->GetEndH() - CW->GetStartH()) * 60 + CW->GetEndM() - CW->GetStartM() - 5; }
		TimerSec = 0;
		break;
	case 9:
		ShowFade(49921); ShowFade(49920);
		if (N >= 1 && N <= 5) { ShowFade(49915 + N - 1); }
		TimerMin = N;
		TimerSec = 0;
		break;
	default:
		break;
	}
}

void UAsdaCampWarBattleSubsystem::TickFades(float DtMs)
{
	// Альфа 255→0 за 3000 мс; событие 0x18 (конец) в `CClan_OnClanUIInput`: по 49911 — спрятать 49910, 49911, 49912 и альфа 255;
	//   по 49913 — 49913, 49920, 49915…49919; по 49921 — 49921, 49920, 49915…49919.
	if (FadeMs.Num() == 0) { return; }
	TArray<int32> Ended;
	for (TPair<int32, float>& P : FadeMs)
	{
		P.Value += DtMs;
		const float A = FMath::Clamp(1.f - P.Value / 3000.f, 0.f, 1.f);
		if (UWidget* Nd = Node(kWinFont, P.Key)) { Nd->SetRenderOpacity(A); }
		if (P.Value >= 3000.f) { Ended.Add(P.Key); }
	}
	auto HideRestore = [this](std::initializer_list<int32> Keys)
	{
		for (int32 K : Keys)
		{
			SetVisNode(kWinFont, K, false);
			FadeMs.Remove(K);
			if (UWidget* Nd = Node(kWinFont, K)) { Nd->SetRenderOpacity(1.f); }
		}
	};
	for (int32 K : Ended)
	{
		if (!FadeMs.Contains(K)) { continue; }
		FadeMs.Remove(K);
		if (K == 49911) { ShowWin(kWinFont, false); HideRestore({ 49911, 49912 }); }
		else if (K == 49913) { HideRestore({ 49913, 49920, 49915, 49916, 49917, 49918, 49919 }); }
		else if (K == 49921) { HideRestore({ 49921, 49920, 49915, 49916, 49917, 49918, 49919 }); }
	}
}

// ═════════════════════════════ 49510 ПОЛОСА ЗАХВАТА ══════════════════════════════════════════════

void UAsdaCampWarBattleSubsystem::PlaceHoldBar(int32 S)
{
	// Тик 0xCA6528…0xCA65C5: экранная точка объекта (x, верх − 750, z); X = экран.x − ширина/2, Y = экран.y; значение = мс.
	//   [ПОРТ] «верх» объекта (`helper_getSumF(obj+0x1A8)` = позиция.y + высота сущности) — верх габаритов модели NPC.
	FAsdaCwWindow* W = Win(kWinHold);
	AAsdaNpcActor* Obj = (S >= 0 && S < kSlots) ? FindPointActor(Slots[S].Id) : nullptr;
	if (!W || !W->Widget || !Obj) { return; }
	UWorld* World = GetGameInstance()->GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	FVector Origin, Extent;
	Obj->GetActorBounds(true, Origin, Extent);
	const FVector At(Obj->GetActorLocation().X, Obj->GetActorLocation().Y, Origin.Z + Extent.Z - 750.f);
	FVector2D Scr;
	if (PC && AsdaUi::WorldToViewportFollow(PC, World, At, 0.f, Scr))
	{
		const float Scale = FMath::Max(0.01f, UWidgetLayoutLibrary::GetViewportScale(World));
		FAsdaPopupMenu::PlaceAt(W->Widget, Scr / Scale - FVector2D(144.f * 0.5f, 0.f));
	}
	if (UImage* Fill = Cast<UImage>(Node(kWinHold, 49521)))
	{
		FAsdaGaugeFill::Clip(Fill, W->Base.FindRef(49521).X, kHoldBarW, FMath::Clamp(HoldBarMs / kCaptureMs, 0.f, 1.f));
	}
}

// ═════════════════════════════ 49800 ИТОГ ════════════════════════════════════════════════════════

void UAsdaCampWarBattleSubsystem::ShowEndWindow(const TArray<uint8>& B)
{
	// `CItemSynthResultWin_Refresh@0xC9CF00` [МИСНЕЙМ]: наверх; погасить +0x23E0; знак победителя (`uiMakeAxisVec2`), имя 10191+w;
	//   w < 0 → `Reset` (ничья). Иначе 10122/10123 по «победитель == моя армия»; три строки (знак, полосы, % и сумма); слава/монеты;
	//   три награды; 49882 скрыт.
	if (B.Num() < 65) { return; }
	if (B.GetData() != LastEnd.GetData()) { LastEnd = B; }
	if (AActor* Fx = StartFx.Get()) { Fx->Destroy(); }
	StartFx.Reset();
	const int32 Winner = AsdaBytes::RdI16(&B[0]);
	ShowWin(kWinEnd, true, /*bTop*/ true);
	UGameInstance* GI = GetGameInstance();
	UAsdaClanFlag::ApplyCampMark(Cast<UImage>(Node(kWinEnd, 49802)), Winner, 64.f);
	SetTextNode(kWinEnd, 49805, Loc(10191 + Winner));
	if (Winner < 0) { bEndDraw = false; ResetEndWindow(); return; }
	bEndDraw = true;
	SetTextNode(kWinEnd, 49806, Loc(10122 + (Winner != GetSelfArmy() ? 1 : 0)));
	int64 Total = 0;
	for (int32 I = 0; I < 3; ++I) { Total += (int64)AsdaBytes::RdI32(&B[20 + 4 * I]) + (int64)AsdaBytes::RdI32(&B[8 + 4 * I]); }
	static const uint32 kFront[3] = { 0xFF00AEFFu, 0xFFFF3200u, 0xFF75E61Au };   // v47 — полоса A (удержание), 49811+10i
	static const uint32 kBack[3]  = { 0xFF7ED6FFu, 0xFFFE9880u, 0xFFBBFF84u };   // v46 — полоса A+B, 49814+10i
	for (int32 I = 0; I < 3; ++I)
	{
		const int32 Army = AsdaBytes::RdI16(&B[2 + 2 * I]);
		UAsdaClanFlag::ApplyCampMark(Cast<UImage>(Node(kWinEnd, 49810 + 10 * I)), Army, 32.f);
		const float A = (float)AsdaBytes::RdI32(&B[8 + 4 * I]);
		const float Sum = A + (float)AsdaBytes::RdI32(&B[20 + 4 * I]);
		const float PctSum = (Total > 0) ? (float)(Sum / (double)Total * 100.0) : 0.f;
		const float PctA = (Total > 0) ? (float)(A / (double)Total * 100.0) : 0.f;
		const bool bArmy = Army >= 0 && Army < 3;
		if (UImage* Back = Cast<UImage>(Node(kWinEnd, 49814 + 10 * I)))
		{
			FAsdaGaugeFill::Clip(Back, Win(kWinEnd)->Base.FindRef(49814 + 10 * I).X, kEndBarW, FMath::Clamp(PctSum / 100.f, 0.f, 1.f));
			if (bArmy) { Back->SetColorAndOpacity(FLinearColor(Argb(kBack[Army]))); }
		}
		if (UImage* Front = Cast<UImage>(Node(kWinEnd, 49811 + 10 * I)))
		{
			FAsdaGaugeFill::Clip(Front, Win(kWinEnd)->Base.FindRef(49811 + 10 * I).X, kEndBarW, FMath::Clamp(PctA / 100.f, 0.f, 1.f));
			if (bArmy) { Front->SetColorAndOpacity(FLinearColor(Argb(kFront[Army]))); }
		}
		SetTextNode(kWinEnd, 49812 + 10 * I, FString::Printf(TEXT("%02.2f%%"), PctSum));
		SetTextNode(kWinEnd, 49813 + 10 * I, Thousands((int64)Sum));
	}
	SetTextNode(kWinEnd, 49842, FString::Printf(TEXT("%d"), AsdaBytes::RdI32(&B[32])));
	SetTextNode(kWinEnd, 49844, FString::Printf(TEXT("%d"), AsdaBytes::RdI16(&B[36])));
	UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
	for (int32 J = 0; J < 3; ++J)
	{
		const int32 Type = B[38 + J];
		const int32 Code = AsdaBytes::RdI32(&B[41 + 4 * J]);
		const int32 Count = AsdaBytes::RdI32(&B[53 + 4 * J]);
		UImage* Icon = Cast<UImage>(Node(kWinEnd, 49850 + 10 * J));
		FString Name, Cnt;
		bool bIcon = false;
		if (Type == 0)
		{
			Name = FString::Printf(TEXT("%d %s"), Code, *Loc(6010));   // «N Опыт», иконка `exp2`
			bIcon = Icon && AsdaUi::ApplyFlatJuiIcon(Icon, TEXT("exp2"), 32.f);
		}
		else if (Type == 1)
		{
			// Предмет 20551, «N имя».
			if (IT && IT->GetItem(20551))
			{
				Name = FString::Printf(TEXT("%d %s"), Code, *IT->GetItemName(20551));
				bIcon = Icon && AsdaUi::ApplyItemIcon(GI, Icon, 20551, FVector2D(32.f, 32.f));
			}
		}
		else if (Type == 4 && IT)
		{
			// Предмет / самоцвет / вторая таблица по коду; количество — в иконку.
			FAsdaSowellRow Sow;
			FAsdaSpecialitemTooltiplistRow Sp;
			if (IT->GetItem(Code)) { Name = IT->GetItemName(Code); }
			else if (IT->GetSowell(Code, Sow)) { Name = Sow.Name; }
			else if (IT->GetSpecialItemTooltip(Code, Sp)) { Name = Sp.Name; }
			if (!Name.IsEmpty())
			{
				Cnt = FString::Printf(TEXT("%d"), Count);
				bIcon = Icon && AsdaUi::ApplyItemIcon(GI, Icon, Code, FVector2D(32.f, 32.f));
			}
		}
		if (!bIcon && Icon) { if (const FSlateBrush* O = OrigBrush.Find(49850 + 10 * J)) { Icon->SetBrush(*O); } }
		SetTextNode(kWinEnd, 49850 + 10 * J + kAlt, Cnt);
		SetTextNode(kWinEnd, 49851 + 10 * J, Name);
	}
	SetVisNode(kWinEnd, 49882, false);
	UE_LOG(LogAsdaCampWarWin, Log, TEXT("[битва армий/бой] ←6732 итог: победитель %d, всего очков %lld"), Winner, (long long)Total);
}

void UAsdaCampWarBattleSubsystem::ResetEndWindow()
{
	// `CItemSynthResultWin_Reset@0xC9DB40` (ничья): знак «noimage», имя "", 10129; строки — пусто, 0.00%, 0; слава/монеты 0;
	//   награды пусты; 49882 видим.
	if (const FSlateBrush* O = OrigBrush.Find(49802)) { if (UImage* M = Cast<UImage>(Node(kWinEnd, 49802))) { M->SetBrush(*O); } }
	SetTextNode(kWinEnd, 49805, FString());
	SetTextNode(kWinEnd, 49806, Loc(10129));
	for (int32 I = 0; I < 3; ++I)
	{
		if (const FSlateBrush* O = OrigBrush.Find(49810 + 10 * I)) { if (UImage* M = Cast<UImage>(Node(kWinEnd, 49810 + 10 * I))) { M->SetBrush(*O); } }
		if (UImage* Front = Cast<UImage>(Node(kWinEnd, 49811 + 10 * I))) { FAsdaGaugeFill::Clip(Front, Win(kWinEnd)->Base.FindRef(49811 + 10 * I).X, kEndBarW, 0.f); }
		SetTextNode(kWinEnd, 49812 + 10 * I, FString::Printf(TEXT("%02.2f%%"), 0.0));
		SetTextNode(kWinEnd, 49813 + 10 * I, TEXT("0"));
		if (const FSlateBrush* O = OrigBrush.Find(49850 + 10 * I)) { if (UImage* M = Cast<UImage>(Node(kWinEnd, 49850 + 10 * I))) { M->SetBrush(*O); } }
		SetTextNode(kWinEnd, 49851 + 10 * I, FString());
	}
	SetTextNode(kWinEnd, 49842, TEXT("0"));
	SetTextNode(kWinEnd, 49844, TEXT("0"));
	SetVisNode(kWinEnd, 49882, true);
}

void UAsdaCampWarBattleSubsystem::OnEndExit()
{
	// 49880 / 49881 (`CClan_OnMemberListUI` 0xC2D8…): состояние действия ≠ 6 (мёртв) и ≠ 29 → спрятать 49800, погасить +0x23E0,
	//   на кампварной карте (+69 == 1) → 6731. Мёртвым — ничего.
	const UAsdaWorldSubsystem* WS = GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>();
	const int32 St = UAsdaKingCrownSubsystem::EtalonActionStateOf(WS ? WS->LocalPlayerActor.Get() : nullptr);
	if (St == 6 || St == 29) { return; }
	ShowWin(kWinEnd, false);
	if (AActor* Fx = StartFx.Get()) { Fx->Destroy(); }
	StartFx.Reset();
	if (UAsdaClanFlag::IsCampWarMap(GetGameInstance())) { SendLeave6731(); }
}

void UAsdaCampWarBattleSubsystem::ResetChildPositions()
{
	// `uiDlg_ResetChildPositions@0xCA2110`: позиции 49399 и 49800 к умолчанию .JUI (47000/48099/46000 — их окна у регистрации/выбора).
	if (UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>())
	{
		WM->ResetWindowLayout(FName(TEXT("CAMPWAR_ATTACK")));
		WM->ResetWindowLayout(FName(TEXT("CAMPWAR_End")));
	}
}

// ═════════════════════════════ 49399 / 49400 «РЕЙД» ══════════════════════════════════════════════

void UAsdaCampWarBattleSubsystem::OnRaidButton()
{
	// 49500 «Рейд»: +0x235A = 1 + 6717 (окно откроется по 6716).
	bWaitList = true;
	SendMemberList6717();
}

void UAsdaCampWarBattleSubsystem::CloseAttack()
{
	// `uiSynthResultWinClose` [МИСНЕЙМ]: спрятать 49400 и 49399.
	ShowWin(kWinPop, false);
	ShowWin(kWinAtk, false);
	HoverRow = -1;
}

void UAsdaCampWarBattleSubsystem::ClearAttackRows()
{
	// `uiRefreshControls@0xC9C9B0` по 15 строкам: иконки [1],[3],[6] — «пустая» картинка, тексты [2],[4],[5],[7],[8],[9] — "".
	for (int32 R = 0; R < kRows; ++R)
	{
		for (int32 K : { 1, 3, 6 })
		{
			UImage* Im = Cast<UImage>(Node(kWinAtk, RowKey(R, K)));
			if (const FSlateBrush* O = OrigBrush.Find(RowKey(R, K)); Im && O) { Im->SetBrush(*O); }
		}
		for (int32 K : { 2, 4, 5, 7, 8, 9 }) { SetTextNode(kWinAtk, RowKey(R, K), FString()); }
		RowMember[R] = -1;
		RowCharId[R] = 0;
	}
}

void UAsdaCampWarBattleSubsystem::DrawMemberList()
{
	// `CClan_DrawMemberList@0xC9C2E0`: только при HUD (+0x2388); 49399 видим + наверх; строки с +0x2314 — по занятым записям.
	if (!bHudOn) { return; }
	ShowWin(kWinAtk, true, /*bTop*/ true);
	ClearAttackRows();
	UGameInstance* GI = GetGameInstance();
	int32 Row = 0;
	for (int32 M = FMath::Max(0, ListTop); M < kMembers; ++M)
	{
		const FAsdaCwMember& Mb = Members[M];
		if (!Mb.Used()) { continue; }
		if (Row >= kRows) { break; }
		if (Mb.Captain() == 0) { DrawTexRect(Cast<UImage>(Node(kWinAtk, RowKey(Row, 1))), TEXT("Faction_Grade"), 0, 72, 24, 24); }
		RowMember[Row] = M;
		RowCharId[Row] = Mb.CharId();
		SetTextNode(kWinAtk, RowKey(Row, 2), Mb.Name());
		SetTextNode(kWinAtk, RowKey(Row, 4), FString::Printf(TEXT("%d"), Mb.Level()));
		SetTextNode(kWinAtk, RowKey(Row, 5), Loc(12556 + ((int8)Mb.Job2() <= 0 ? (int8)Mb.Job1() : (int8)Mb.Job2())));
		if (Mb.Grade() >= 1)
		{
			// `uiGrid_CellPosFromIndex(звание − 1)` = (24·(n%8), 24·(n/8)), 24×24.
			const int32 N = Mb.Grade() - 1;
			DrawTexRect(Cast<UImage>(Node(kWinAtk, RowKey(Row, 3))), TEXT("Faction_Grade"), 24 * (N % 8), 24 * (N / 8), 24, 24);
		}
		if (Mb.HasFlag())
		{
			// Флаг клана — испечённая плашка (+15…+54 — блок 40 б как в 6542), вырез (23,12,64,42).
			if (UImage* Im = Cast<UImage>(Node(kWinAtk, RowKey(Row, 6))))
			{
				FSlateBrush Br;
				if (UAsdaClanFlag::MakePlateBrush(GI, FAsdaClanFlagData::FromWire(Mb.Raw + 15), 21.f, Br)) { Im->SetBrush(Br); }
			}
		}
		SetTextNode(kWinAtk, RowKey(Row, 7), Mb.Clan());
		SetTextNode(kWinAtk, RowKey(Row, 8), Loc(Mb.Squad() < 0 ? 10065 : 10064));
		SetTextNode(kWinAtk, RowKey(Row, 9), Mb.Leader());
		++Row;
	}
}

void UAsdaCampWarBattleSubsystem::ClearRowSel()
{
	// `sub_C9CDF0`: +0x2318 = −1, 49330 скрыт.
	ListSel = -1;
	for (int32 K = 0; K < 8; ++K) { SetVisNode(kWinAtk, FrameKey(K), false); }
}

void UAsdaCampWarBattleSubsystem::SelectRow(int32 Row, bool bRight)
{
	// `questTrackerSelectSlot@0xC9CC70` [МИСНЕЙМ]: при a3 или скрытом 49400; строка > 14 или имя непустое; своя строка или та же без a3 —
	//   снять выбор; иначе рамка 49330 на строку (+15, +3 от строки — у порта сдвиг планов рамки на шаг строк), +0x2318 = строка.
	const FAsdaCwWindow* Pop = Win(kWinPop);
	if (!bRight && Pop && Pop->bShown) { return; }
	if (Row < 0) { return; }
	if (Row <= 14)
	{
		const UTextBlock* T = Cast<UTextBlock>(Node(kWinAtk, RowKey(Row, 2)));
		if (!T || T->GetText().IsEmpty()) { return; }
	}
	const UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	const int32 RowChar = (Row < kRows) ? RowCharId[Row] : 0;
	if ((Net && RowChar == Net->SelectedCharId) || (ListSel == Row && !bRight)) { ClearRowSel(); return; }
	const FAsdaCwWindow* W = Win(kWinAtk);
	const FVector2D D = (W ? W->Base.FindRef(RowKey(FMath::Min(Row, kRows - 1), 0)) - W->Base.FindRef(RowKey(0, 0)) : FVector2D::ZeroVector);
	for (int32 K = 0; K < 8; ++K)
	{
		if (UWidget* P = Node(kWinAtk, FrameKey(K))) { SetSlotPosOf(P, W->Base.FindRef(FrameKey(K)) + D); }
		SetVisNode(kWinAtk, FrameKey(K), true);
	}
	ListSel = Row;
}

int32 UAsdaCampWarBattleSubsystem::MemberIndexOfSel() const
{
	// Член под выбранной строкой: от +0x2314 считать занятые записи до +0x2318 + 1 (эталон: не нашёл — 50).
	int32 Cnt = 0, M = FMath::Max(0, ListTop);
	for (; M < kMembers; ++M)
	{
		if (Members[M].Used()) { ++Cnt; }
		if (Cnt == ListSel + 1) { break; }
	}
	return M;
}

void UAsdaCampWarBattleSubsystem::ShowRowPopup()
{
	// `uiSynthResultWinShow@0xC9CE30` [МИСНЕЙМ]: 49400 видим, поверх (`sub_929A2C(…, 1)`), позиция = курсор.
	if (ListSel < 0) { return; }
	ShowWin(kWinPop, true, /*bTop*/ true);
	const FAsdaCwWindow* W = Win(kWinPop);
	if (W && W->Widget)
	{
		FAsdaPopupMenu::PlaceAt(W->Widget, AsdaUi::ScreenToCanvasLocal(W->Widget, Mouse.Cursor, GetGameInstance()->GetWorld()));
	}
}

void UAsdaCampWarBattleSubsystem::OnPopupInvite()
{
	// 49410 «Пригласить в отряд» (G:308383-308425; цвет −11514 = 0xFFFFD306, 10000 мс).
	UGameInstance* GI = GetGameInstance();
	UAsdaPartySubsystem* Party = GI->GetSubsystem<UAsdaPartySubsystem>();
	const UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
	const int32 M = MemberIndexOfSel();
	ShowWin(kWinPop, false);
	if (!Party || M >= kMembers) { return; }
	const FAsdaCwMember& Mb = Members[M];
	const int32 TargetNum = Mb.UserNum();   // `g_PartyInviteTargetClanId` [МИСНЕЙМ] — номер персонажа цели
	auto Line = [this](int32 LocId) { ChatLine(LocId, LocId > 0 ? Loc(LocId) : FString(), 0xFFFFD306u, 10000.f, false); };
	if (Party->IsMemberCharId(Mb.CharId()))
	{
		// [ОРИГИНАЛ] пустая строка (`nameBuf` не заполнен) — Д-Б4(а); ключ 1 — 1047 «уже в отряде».
		if (InviteFix()) { Line(1047); } else { Line(0); }
		return;
	}
	if (Party->IsInParty())
	{
		if (Party->IsOwnRequestActive()) { Line(1056); return; }
		if (Party->IsIncomingPending()) { Line(1045); return; }
		if (!Party->IsPartyLeader()) { Line(1089); return; }
		// [ОРИГИНАЛ] имя записи [118·строка] — индекс СТРОКИ, а не найденного члена (Д-Б4(б)).
		const FString Name = InviteFix() ? Mb.Name() : Members[FMath::Clamp(ListSel, 0, kMembers - 1)].Name();
		Party->ShowInviteRequestFor(TargetNum, Name);
		return;
	}
	if (Party->IsOwnRequestActive()) { Line(1056); return; }
	if (Party->IsIncomingPending()) { Line(1045); return; }
	if (Net && AsdaSelf::IsSelfUserIdx(Net, TargetNum)) { return; }   // своя запись — молча
	Party->ShowInviteRequestFor(TargetNum, Mb.Name());
}

void UAsdaCampWarBattleSubsystem::OnPopupCaptain()
{
	// 49420 «Назначить капитана» → `Clan_Send_KickMember@0xCA39B0` [МИСНЕЙМ] (6734, 14 б): [0..1] армия, [2..5] charId, [6..7] мой индекс
	//   +0xB7E, [8..11] charId цели (+0xBC7), [12..13] слот цели; спрятать 49400.
	const int32 M = MemberIndexOfSel();
	ShowWin(kWinPop, false);
	const UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	TArray<uint8> B; B.SetNumZeroed(14);
	AsdaBytes::WrI16(B.GetData(), GetSelfArmy());
	AsdaBytes::WrI32(B.GetData() + 2, Net ? Net->SelectedCharId : 0);
	AsdaBytes::WrI16(B.GetData() + 6, MyIndex);
	// [ПОРТ] эталон при «не нашёл» (50) читает запись за концом массива — у порта charId 0.
	AsdaBytes::WrI32(B.GetData() + 8, (M < kMembers) ? Members[M].CharId() : 0);
	AsdaBytes::WrI16(B.GetData() + 12, M);
	SendBody(6734, B, TEXT("«Назначить капитана»"));
}

void UAsdaCampWarBattleSubsystem::OnPopupKick()
{
	// 49430 «Исключить»: окно `CItem_BuildTooltipText(g_pItemTooltipUI, 1, 87, sprintf(111937, имя))` → «Да» (msgbox 87) → 6186.
	const int32 M = MemberIndexOfSel();
	ShowWin(kWinPop, false);
	const FString Name = (M < kMembers) ? Members[M].Name() : FString();
	TWeakObjectPtr<UAsdaCampWarBattleSubsystem> Self(this);
	Confirm(111937, AsdaUi::FormatPrintfArgs(Loc(111937), { Name }), 87, [Self]()
	{
		UAsdaCampWarBattleSubsystem* S = Self.Get();
		if (!S) { return; }
		// `netSendListSelectReq@0xCA88A0` [МИСНЕЙМ] (6186, 10 б): не шлётся, если в сумке (`g_InventoryArray2`, контейнер 2, 60 записей)
		//   нет ни одного предмета; [0..3] charId, [4..5] слот цели (пересчёт по выбору на момент «Да»), [6..9] charId цели.
		UGameInstance* GI = S->GetGameInstance();
		const UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>();
		const UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
		bool bAny = false;
		const int32 Cells = Inv ? FMath::Min(Inv->GetContainerSize(2), 60) : 0;
		for (int32 J = 0; J < Cells && !bAny; ++J)
		{
			const FAsdaItemInstance* It = Inv->GetCell(2, J);
			bAny = It && !It->IsEmpty() && IT && IT->GetItem(It->ItemCode);
		}
		if (!bAny) { UE_LOG(LogAsdaCampWarWin, Log, TEXT("[битва армий/бой] 6186 не отправлен: в сумке нет ни одного предмета (как эталон)")); return; }
		const int32 Slot = S->MemberIndexOfSel();
		const UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
		TArray<uint8> B; B.SetNumZeroed(10);
		AsdaBytes::WrI32(B.GetData(), Net ? Net->SelectedCharId : 0);
		AsdaBytes::WrI16(B.GetData() + 4, Slot);
		AsdaBytes::WrI32(B.GetData() + 6, (Slot < kMembers) ? S->Members[Slot].CharId() : 0);
		S->SendBody(6186, B, TEXT("«Исключить» из армии"));
	});
}

// ═════════════════════════════ ОПРОС МЫШИ ════════════════════════════════════════════════════════

void UAsdaCampWarBattleSubsystem::TickMouse()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
	const FAsdaCwWindow* Atk = Win(kWinAtk);
	const FAsdaCwWindow* End = Win(kWinEnd);
	const bool bAtk = Atk && Atk->bShown && Atk->bReady;
	const bool bEnd = End && End->bShown && End->bReady;
	if (!WM || (!bAtk && !bEnd)) { Mouse.Reset(); return; }
	Mouse.Update();
	if (AsdaUi::IsModalDispatchBlocked(GI)) { Mouse.Reset(); return; }
	// 49399: события 8/0xA (наведение — подсветка TB_R), 0xB (ЛКМ) / 0xE (ПКМ) — выбор, 0xF (ПКМ отпущена) — попап, 0x17 — колесо.
	if (bAtk && !WM->IsPointerBlockedByTopmost(FName(TEXT("CAMPWAR_ATTACK"))) && !WM->IsCursorOverHigherWindow(FName(TEXT("CAMPWAR_ATTACK"))))
	{
		int32 Over = -1;
		for (int32 R = 0; R < kRows; ++R) { if (UWidget* RowW = Node(kWinAtk, RowKey(R, 0)); RowW && AsdaUi::IsCursorOverWidget(RowW)) { Over = R; break; } }
		if (Over != HoverRow)
		{
			for (int32 R = 0; R < kRows; ++R) { SetVisNode(kWinAtk, RowKey(R, 10), R == Over); }
			HoverRow = Over;
		}
		if (Over >= 0 && Mouse.bLmbDown) { SelectRow(Over, false); }
		if (Over >= 0 && Mouse.bRmbDown) { SelectRow(Over, true); }
		if (Over >= 0 && Mouse.bRmbUp && ListSel >= 0) { ShowRowPopup(); }
		UWidget* List = Node(kWinAtk, 49320);
		APlayerController* PC = GI->GetWorld() ? GI->GetWorld()->GetFirstPlayerController() : nullptr;
		const float Wheel = PC ? PC->GetInputAnalogKeyState(EKeys::MouseWheelAxis) : 0.f;
		if (List && Wheel != 0.f && AsdaUi::IsCursorOverWidget(List))
		{
			// `uiControlSetScrollRange(49320, 15, 30)`, страница 1: первая строка 0…15.
			const int32 NewTop = FMath::Clamp(ListTop + (Wheel < 0.f ? 1 : -1), 0, 15);
			if (NewTop != ListTop) { ListTop = NewTop; DrawMemberList(); ClearRowSel(); }
		}
	}
	else if (HoverRow != -1)
	{
		for (int32 R = 0; R < kRows; ++R) { SetVisNode(kWinAtk, RowKey(R, 10), false); }
		HoverRow = -1;
	}
	// 49800: полосы 49811+10i / 49814+10i — событие 9 → подсказка 111093 + 111094/111095 (`tutorialPopupLayoutSmall`), 0xA — скрыть.
	if (bEnd)
	{
		int32 Bar = -1;
		if (!WM->IsCursorOverHigherWindow(FName(TEXT("CAMPWAR_End"))))
		{
			for (int32 I = 0; I < 3 && Bar < 0; ++I)
			{
				// Передняя полоса лежит над задней — сначала она.
				if (UWidget* F = Node(kWinEnd, 49811 + 10 * I); F && AsdaUi::IsCursorOverWidget(F)) { Bar = 49811 + 10 * I; }
				else if (UWidget* Bk = Node(kWinEnd, 49814 + 10 * I); Bk && AsdaUi::IsCursorOverWidget(Bk)) { Bar = 49814 + 10 * I; }
			}
		}
		if (Bar != HoverBar)
		{
			UAsdaItemTooltipSubsystem* Tip = GI->GetSubsystem<UAsdaItemTooltipSubsystem>();
			if (Tip)
			{
				if (Bar < 0) { Tip->HideTooltipFor(EAsdaTipOwner::CampWar); }
				else
				{
					FAsdaTipMetrics Mt;
					Mt.WidthAdd = 27.f;   // «маленькая» всплывашка эталона
					Mt.ExtraLines = 0;
					const bool bFront = ((Bar - 49811) % 10) == 0;
					Tip->ShowTextTip(EAsdaTipOwner::CampWar, AsdaNotice::LocRaw(GI, 111093), AsdaNotice::LocRaw(GI, bFront ? 111094 : 111095), Mt);
				}
			}
			HoverBar = Bar;
		}
	}
}

// ═════════════════════════════ ДИАГНОСТИКА ═══════════════════════════════════════════════════════

bool UAsdaCampWarBattleSubsystem::IsWinShown(int32 IdNum) const
{
	const FAsdaCwWindow* W = Win(IdNum);
	const UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
	return W && W->bShown && W->Widget && WM && WM->IsWindowLive(W->Widget) && W->Widget->GetVisibility() != ESlateVisibility::Collapsed
		&& W->Widget->GetVisibility() != ESlateVisibility::Hidden;
}

UWidget* UAsdaCampWarBattleSubsystem::GetNode(int32 IdNum) const
{
	for (const TPair<int32, FAsdaCwWindow>& P : Windows) { if (UWidget* N = P.Value.Ctl.FindRef(IdNum).Get()) { return N; } }
	return nullptr;
}

bool UAsdaCampWarBattleSubsystem::IsShown(int32 IdNum) const
{
	const UWidget* N = GetNode(IdNum);
	return N && AsdaUi::IsShown(N);
}

FString UAsdaCampWarBattleSubsystem::GetText(int32 IdNum) const
{
	const UTextBlock* T = Cast<UTextBlock>(GetNode(IdNum));
	return T ? T->GetText().ToString() : FString();
}

float UAsdaCampWarBattleSubsystem::GetBarValue(int32 IdNum) const
{
	if (IdNum == 49521) { return HoldBarMs; }
	const UImage* I = Cast<UImage>(GetNode(IdNum));
	const UCanvasPanelSlot* S = I ? Cast<UCanvasPanelSlot>(I->Slot) : nullptr;
	return S ? S->GetSize().X / kEndBarW * 100.f : -1.f;
}

FVector2D UAsdaCampWarBattleSubsystem::GetNodePos(int32 IdNum) const { return SlotPosOf(GetNode(IdNum)); }

float UAsdaCampWarBattleSubsystem::GetNodeAlpha(int32 IdNum) const
{
	const UWidget* N = GetNode(IdNum);
	return N ? N->GetRenderOpacity() : -1.f;
}

int32 UAsdaCampWarBattleSubsystem::GetVisibleDigit(int32 F, int32 J) const
{
	int32 Shown = -1;
	for (int32 K = 0; K < 10; ++K)
	{
		const UWidget* N = GetNode(DigitKey(F, J, K));
		if (N && N->GetVisibility() != ESlateVisibility::Collapsed) { if (Shown >= 0) { return -2; } Shown = K; }
	}
	return Shown;
}

FString UAsdaCampWarBattleSubsystem::DumpControls() const
{
	FString S = FString::Printf(TEXT("[битва армий/бой] контролов разобрано %d из %d:"), FoundControls, TotalControls);
	for (const FCwWinDesc& D : kWins)
	{
		const FAsdaCwWindow* W = Win(D.Root);
		S += FString::Printf(TEXT(" %d %s/%s;"), D.Root, (W && W->bReady) ? TEXT("готово") : TEXT("нет"), (W && W->bShown) ? TEXT("видно") : TEXT("скрыто"));
	}
	return S;
}
