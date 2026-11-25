// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaCloseGearSubsystem — общая точка «шестерёнка у крестика». Разбор эталона — в шапке AsdaCloseGear.h.
#include "UI/AsdaCloseGear.h"

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaAnimatedPlane.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCloseGear, Log, All);   // журнал общей точки

// Ключ: 1 — шестерёнки всех окон таблицы ведёт общая точка (как оригинал); 0 — прежние копии подсистем.
static TAutoConsoleVariable<int32> CVarCloseGearHover(
	TEXT("asda.UI.CloseGearHover"), 1,
	TEXT("1 = шестерёнка у крестика: наведение на крестик прячет *_Gear и крутит *_AniGear во всех окнах оригинала (общая точка UAsdaCloseGearSubsystem); ")
	TEXT("0 = прежнее поведение порта (копии в шести окнах, в семи окнах шестерёнка не крутится)."),
	ECVF_Default);

bool AsdaCloseGear::IsCommonOn() { return CVarCloseGearHover.GetValueOnGameThread() != 0; }

const TArray<FAsdaCloseGearRow>& UAsdaCloseGearSubsystem::Rows()
{
	// Окно, крестик, статичная, кадровая, IDNum крестика. Пары id статичная/кадровая — из FindControlByID обработчиков (шапка).
	static const TArray<FAsdaCloseGearRow> R = {
		{ FName(TEXT("NPCSHOP_WIN")),   TEXT("NPCSHOP_CLOSE"),         TEXT("NpcShop_Gear"),    TEXT("NpcShop_AniGaer"),   1302  },  // 1371/1372
		{ FName(TEXT("TRAN_WIN")),      TEXT("TRAN_TOPCLOSE"),         TEXT("Tran_Gaer"),       TEXT("Tran_AniGaer"),      2050  },  // 2058/2059
		{ FName(TEXT("PARTY_WIN")),     TEXT("PARTY_MINIBUT"),         TEXT("Party_Gear"),      TEXT("Party_AniGear"),     2279  },  // 2280/2281
		{ FName(TEXT("COMMUNITY_WIN")), TEXT("COMMUNITY_CLOSE"),       TEXT("Community_Gear"),  TEXT("Community_AniGear"), 6403  },  // 6405/6406
		{ FName(TEXT("QUEST_WIN")),     TEXT("QUEST_TOPCLOSE"),        TEXT("Quest_Gear"),      TEXT("Quest_AniGear"),     702   },  // 740/741
		{ FName(TEXT("ITEM_SWAP_WIN")), TEXT("ITEM_SWAP_CLOSE"),       TEXT("Item_Swap_Gear"),  TEXT("Item_Swap_AniGear"), 15265 },  // 15267/15266
		{ FName(TEXT("INVEN_WIN")),     TEXT("INVEN_WINCLOSEBUT"),     TEXT("Inven_Gear"),      TEXT("Inven_AniGear"),     403   },  // 450/451
		{ FName(TEXT("EXCHANGE_WIN")),  TEXT("EXCHANGE_TITLE_DEL"),    TEXT("Exchange_Gaer"),   TEXT("Exchange_AniGaer"),  13004 },  // 13001/13002
		{ FName(TEXT("SKILL_WIN")),     TEXT("SKILL_WIN_CLOSEBTN"),    TEXT("Skill_Gear"),      TEXT("Skill_AniGear"),     12009 },  // 12002/12003
		{ FName(TEXT("CHARAINFO_WIN")), TEXT("CHARAINFO_CLOSE"),       TEXT("CharaInfo_Gear"),  TEXT("CharaInfo_AniGear"), 980   },  // 985/986
		{ FName(TEXT("WARE_WIN")),      TEXT("WARE_CLOBUT"),           TEXT("Ware_Gear"),       TEXT("Ware_AniGear"),      4050  },  // 4191/4192
		{ FName(TEXT("SYSTEM_WIN")),    TEXT("SYSTEM_TOPCLOSE"),       TEXT("System_Gear"),     TEXT("System_AniGear"),    801   },  // 868/869
		{ FName(TEXT("Title_Win")),     TEXT("Title_Win_CloseBtn"),    TEXT("Title_Win_Gear"),  TEXT("Title_Win_AniGear"), 18703 },  // 18701/18702
	};
	return R;
}

void UAsdaCloseGearSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Live.SetNum(Rows().Num());
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UAsdaCloseGearSubsystem::OnTick), 0.f);
}

void UAsdaCloseGearSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	Live.Reset();
	Super::Deinitialize();
}

bool UAsdaCloseGearSubsystem::OnTick(float /*Dt*/)
{
	TickAll();
	return true;
}

// Узел — потомок панели (крестик внутри группы шестерёнки: прятать такую группу нельзя).
static bool IsInside(const UWidget* Node, const UWidget* Panel)
{
	for (const UWidget* P = Node ? Node->GetParent() : nullptr; P; P = P->GetParent()) { if (P == Panel) { return true; } }
	return false;
}

void UAsdaCloseGearSubsystem::Resolve(const FAsdaCloseGearRow& R, UUserWidget* Win, FAsdaCloseGearLive& L) const
{
	L = FAsdaCloseGearLive();
	L.Win = Win;
	L.Close = AsdaUi::FindNodeExact(Win, R.Close);
	L.Gear  = AsdaUi::FindNodeByPrefix(Win, *FString::Printf(TEXT("%s_"), R.Gear));
	L.Ani   = AsdaUi::FindNodeByPrefix(Win, *FString::Printf(TEXT("%s_"), R.Ani));
	if (L.Close.IsValid() && (IsInside(L.Close.Get(), L.Gear.Get()) || IsInside(L.Close.Get(), L.Ani.Get())))
	{
		UE_LOG(LogAsdaCloseGear, Warning, TEXT("[шестерёнка] %s: крестик %s внутри группы шестерёнки — строка пропущена"), *R.Window.ToString(), R.Close);
		L.Gear = nullptr; L.Ani = nullptr;
	}
	UE_LOG(LogAsdaCloseGear, Log, TEXT("[шестерёнка] %s: крестик %d %s=%s, %s=%s, %s=%s"), *R.Window.ToString(), R.CloseId,
		R.Close, L.Close.IsValid() ? *L.Close->GetName() : TEXT("НЕТ"), R.Gear, L.Gear.IsValid() ? *L.Gear->GetName() : TEXT("НЕТ"),
		R.Ani, L.Ani.IsValid() ? *L.Ani->GetName() : TEXT("НЕТ"));
}

// Кадровые планы узла (сам узел или его дети).
static void ForEachAnimPlane(UWidget* W, TFunctionRef<void(UAsdaAnimatedPlane*)> Fn)
{
	if (UAsdaAnimatedPlane* A = Cast<UAsdaAnimatedPlane>(W)) { Fn(A); return; }
	if (UPanelWidget* P = Cast<UPanelWidget>(W))
	{
		TArray<UWidget*> Kids;
		UWidgetTree::GetChildWidgets(P, Kids);
		for (UWidget* K : Kids) { if (UAsdaAnimatedPlane* A = Cast<UAsdaAnimatedPlane>(K)) { Fn(A); } }
	}
}

// Показать/спрятать узел шестерёнки вместе с поддеревом: импортёр гасит `Visible="0"` ВСЁ поддерево (закон
//   jui-importer-node-names), и показ одной группы оставил бы её планы свёрнутыми.
static void SetGearTree(UWidget* W, bool bShown)
{
	if (!W) { return; }
	AsdaUi::SetShownNoHit(W, bShown);
	if (UPanelWidget* P = Cast<UPanelWidget>(W))
	{
		TArray<UWidget*> Kids;
		UWidgetTree::GetChildWidgets(P, Kids);
		for (UWidget* K : Kids) { AsdaUi::SetShownNoHit(K, bShown); }
	}
}

void UAsdaCloseGearSubsystem::Apply(FAsdaCloseGearLive& L, bool bOver)
{
	const bool bEdge = !L.bApplied || L.bOver != bOver;
	L.bOver = bOver;
	L.bApplied = true;
	// Видимость сверяем КАЖДЫЙ кадр: показ окна «всем поддеревом» (или его сборка) не должен будить кадровую шестерёнку.
	//   Мышь шестерёнки не ловят (JUI NoSelect у кадровой; статичная лежит под крестиком) — HitTestInvisible.
	if (UWidget* G = L.Gear.Get()) { if (bEdge || AsdaUi::IsShown(G) == bOver) { SetGearTree(G, !bOver); } }
	if (UWidget* A = L.Ani.Get())
	{
		if (bEdge || AsdaUi::IsShown(A) != bOver) { SetGearTree(A, bOver); }
		// `CControl_Show` — прогон с кадра 0; `CControl_CloseHide` — стоп. Только на смене состояния.
		if (bEdge) { ForEachAnimPlane(A, [bOver](UAsdaAnimatedPlane* P) { bOver ? P->RestartAnim() : P->StopAnim(); }); }
	}
}

void UAsdaCloseGearSubsystem::TickAll()
{
	if (!AsdaCloseGear::IsCommonOn()) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	const TArray<FAsdaCloseGearRow>& R = Rows();
	if (Live.Num() != R.Num()) { Live.SetNum(R.Num()); }
	for (int32 i = 0; i < R.Num(); ++i)
	{
		UUserWidget* Win = WM->FindWindow(R[i].Window);
		FAsdaCloseGearLive& L = Live[i];
		if (!Win) { if (L.Win.IsValid() || L.bApplied) { L = FAsdaCloseGearLive(); } continue; }
		// ★ Окно пересоздано (травел, повторная сборка) — узлы ищем заново (память window-pointers-stale-after-travel).
		if (L.Win.Get() != Win || (L.Close.IsStale() || L.Gear.IsStale() || L.Ani.IsStale())) { Resolve(R[i], Win, L); }
		if (!L.Close.IsValid() || (!L.Gear.IsValid() && !L.Ani.IsValid())) { continue; }
		// Закрытое окно: курсор над ним не стоит — шестерёнки в покое (при следующем показе окно откроется со статичной).
		const bool bOver = WM->IsWindowVisible(R[i].Window) && AsdaUi::IsCursorOverWidget(L.Close.Get());
		Apply(L, bOver);
	}
}

bool UAsdaCloseGearSubsystem::DebugNodes(FName Window, UWidget*& OutClose, UWidget*& OutGear, UWidget*& OutAni)
{
	OutClose = OutGear = OutAni = nullptr;
	const TArray<FAsdaCloseGearRow>& R = Rows();
	for (int32 i = 0; i < R.Num() && i < Live.Num(); ++i)
	{
		if (R[i].Window != Window) { continue; }
		OutClose = Live[i].Close.Get(); OutGear = Live[i].Gear.Get(); OutAni = Live[i].Ani.Get();
		return OutClose != nullptr;
	}
	return false;
}
