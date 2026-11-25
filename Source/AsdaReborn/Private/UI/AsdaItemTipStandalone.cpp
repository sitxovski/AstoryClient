// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemTipStandalone.cpp — «отдельный комплект» построителей подсказки предмета для редакторских инструментов.
//   Шапка и обоснование — UI/AsdaItemTipStandalone.h. ★ [ПОРТ] 29.10.2026.
#include "UI/AsdaItemTipStandalone.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"      // FAsdaItemInstance — синтетическая запись слота
#include "Items/AsdaItemTableSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "Subsystems/SubsystemCollection.h"  // пустая коллекция для `Initialize` подсистемы таблиц
#include "Engine/GameInstance.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaItemTipStandalone, Log, All);

namespace AsdaItemTipStandalonePriv
{
	// Объекты комплекта. Все корневые (AddToRoot): живут, пока комплект не выброшен `Release`.
	static UGameInstance*             KitGI   = nullptr;   // служебный владелец (Within=GameInstance у подсистем); НЕ Init()
	static UAsdaItemTableSubsystem*   KitItems = nullptr;  // таблицы item-БД (те же DataTable-ассеты, что в игре)
	static UAsdaTextSubsystem*        KitText  = nullptr;  // текст GameText + подстановка маркеров
	static UAsdaItemTooltipSubsystem* KitTip   = nullptr;  // построители подсказки (Initialize НЕ зовётся — без консоли и окон)
	// «Профиль зрителя» (см. `SetViewer`).
	static int32 ViewerLv = 0, ViewerCls = 0, ViewerSub = 0;

	// Снять объект с корня и пометить мусором (сам объект соберёт GC).
	static void Drop(UObject*& Obj)
	{
		if (Obj) { Obj->RemoveFromRoot(); Obj->MarkAsGarbage(); Obj = nullptr; }
	}
}

UAsdaItemTooltipSubsystem* AsdaItemTipStandalone::Acquire()
{
	using namespace AsdaItemTipStandalonePriv;
	check(IsInGameThread());
	if (KitTip) { return KitTip; }

	const double T0 = FPlatformTime::Seconds();
	KitGI = NewObject<UGameInstance>(GetTransientPackage(), UGameInstance::StaticClass(),
		MakeUniqueObjectName(GetTransientPackage(), UGameInstance::StaticClass(), TEXT("AsdaItemTipKitGI")), RF_Transient);
	KitGI->AddToRoot();

	KitItems = NewObject<UAsdaItemTableSubsystem>(KitGI, NAME_None, RF_Transient);
	KitItems->AddToRoot();
	{
		// `Initialize` подсистемы таблиц коллекцию не читает (зависимостей нет) — пустая коллекция достаточна.
		FSubsystemCollection<UGameInstanceSubsystem> Dummy;
		KitItems->Initialize(Dummy);
	}
	if (!KitItems->IsLoaded())
	{
		UE_LOG(LogAsdaItemTipStandalone, Error, TEXT("[комплект подсказки] таблицы item-БД не загрузились — карточка недоступна"));
		UObject* O = KitItems; Drop(O); KitItems = nullptr;
		O = KitGI; Drop(O); KitGI = nullptr;
		return nullptr;
	}

	KitText = NewObject<UAsdaTextSubsystem>(KitGI, NAME_None, RF_Transient);
	KitText->AddToRoot();
	KitTip = NewObject<UAsdaItemTooltipSubsystem>(KitGI, NAME_None, RF_Transient);
	KitTip->AddToRoot();

	UE_LOG(LogAsdaItemTipStandalone, Log, TEXT("[комплект подсказки] создан за %.0f мс"), (FPlatformTime::Seconds() - T0) * 1000.0);
	return KitTip;
}

void AsdaItemTipStandalone::Release()
{
	using namespace AsdaItemTipStandalonePriv;
	check(IsInGameThread());
	if (KitItems) { KitItems->Deinitialize(); }
	UObject* O = KitTip;   Drop(O); KitTip = nullptr;
	O = KitText;           Drop(O); KitText = nullptr;
	O = KitItems;          Drop(O); KitItems = nullptr;
	O = KitGI;             Drop(O); KitGI = nullptr;
}

bool AsdaItemTipStandalone::IsAcquired()             { return AsdaItemTipStandalonePriv::KitTip != nullptr; }
bool AsdaItemTipStandalone::IsStandaloneTip(const UObject* Obj)
{
	return Obj != nullptr && Obj == AsdaItemTipStandalonePriv::KitTip;
}
UAsdaItemTableSubsystem* AsdaItemTipStandalone::Items() { return AsdaItemTipStandalonePriv::KitItems; }
UAsdaTextSubsystem* AsdaItemTipStandalone::Text()       { return AsdaItemTipStandalonePriv::KitText; }

void AsdaItemTipStandalone::SetViewer(int32 Level, int32 ClassId, int32 SubJob)
{
	AsdaItemTipStandalonePriv::ViewerLv  = FMath::Max(0, Level);
	AsdaItemTipStandalonePriv::ViewerCls = FMath::Max(0, ClassId);
	AsdaItemTipStandalonePriv::ViewerSub = FMath::Max(0, SubJob);
}
int32 AsdaItemTipStandalone::ViewerLevel()  { return AsdaItemTipStandalonePriv::ViewerLv; }
int32 AsdaItemTipStandalone::ViewerClass()  { return AsdaItemTipStandalonePriv::ViewerCls; }
int32 AsdaItemTipStandalone::ViewerSubJob() { return AsdaItemTipStandalonePriv::ViewerSub; }

FString AsdaItemTipStandalone::Loc(int32 Id, EAsdaLang Lang)
{
	UAsdaTextSubsystem* T = Text();
	if (!T) { return FString(); }
	FAsdaLangScope Scope(Lang);
	return T->FormatNotice(Id);
}

bool AsdaItemTipStandalone::BuildCard(int32 ItemCode, EAsdaLang Lang, EAsdaItemCardKind Kind, FAsdaItemCard& Out)
{
	Out = FAsdaItemCard();
	UAsdaItemTooltipSubsystem* Tip = Acquire();
	UAsdaItemTableSubsystem* IT = Items();
	if (!Tip || !IT || ItemCode <= 0) { return false; }

	FAsdaLangScope Scope(Lang);

	// Синтетическая запись слота — эталонная запись предпросмотра (`uiInventoryShowSlotTooltip@0xB87200`).
	FAsdaItemInstance Slot;
	const TArray<uint8> Rec = Tip->BuildPreviewSlotRecord(ItemCode);
	Slot.SetRaw(Rec.GetData(), Rec.Num());

	const FAsdaItemRecord* R = IT->GetItem(ItemCode);
	// Аргумент `a4` простого построителя (`invSlotBuildTooltip@0xD06670`) — так же, как в `ShowItemTooltip`.
	const int32 BindByte = (Rec.Num() > 50) ? (int32)Rec[50] : 0;
	int32 BindArg = -1;
	if (R && BindByte == 3 && R->ItemSubType == 32)     { BindArg = 0; }
	else if (R && R->ItemType >= 5 && R->ItemType <= 7) { BindArg = BindByte; }

	if (!R && IT->IsSowelCode(ItemCode))
	{
		// Самоцвет — всегда свой построитель (`CItem_BuildSowelTooltipText`, первая половина).
		Out.Tip = Tip->BuildTooltipSowel(ItemCode, Out.M);
		Out.Pipeline = TEXT("CItem_BuildSowelTooltipText@0xEA2EC0 (самоцвет)");
	}
	else if (!R && IT->IsSpecialItemCode(ItemCode))
	{
		if (Kind == EAsdaItemCardKind::Simple)
		{
			Out.Tip = Tip->BuildTooltipSimple(ItemCode, /*Quantity*/0, BindArg, Out.M);
			Out.Pipeline = TEXT("CItem_BuildSowelTooltipText@0xEA2EC0 (ветка второй таблицы)");
		}
		else
		{
			Out.Tip = Tip->BuildTooltipTable2(Slot, Out.M);
			Out.Pipeline = TEXT("ItemTooltip_Build@0xEA5260 (ветка второй таблицы special_itemlist)");
		}
	}
	else if (R)
	{
		bool bFull = true;
		switch (Kind)
		{
		case EAsdaItemCardKind::Full:   bFull = true;  break;
		case EAsdaItemCardKind::Simple: bFull = false; break;
		default:
			// Гейт ПОЛНОГО построителя витрины (`cItemBar_BuildSlotTooltip@0xB6F3C0`, = `ShowGachaSlotTooltip`).
			bFull = (R->ItemType <= 4) || (R->ItemType >= 8 && R->ItemType <= 9) || (R->CategoryFlag == 3);
			break;
		}
		if (bFull && Tip->UsesBagNonGearTip(R))
		{
			// ★ [ИСПРАВЛЕНИЕ] баг 11 (01.11.2026): та же функция, что у игры (`ShowItemTooltip`, полный путь) — просмотрщик не расходится.
			Out.Tip = Tip->BuildTooltipBagNonGear(Slot, Out.M);
			Out.Pipeline = TEXT("CItem_BuildSowelTooltipText@0xEA2EC0 + профессия/привязка (исправление бага 11, asda.ItemTip.BagNonGearSimple)");
		}
		else if (bFull)
		{
			Out.Tip = Tip->BuildTooltip(Slot);
			// Метрики — как у `ShowItemTooltip` для полного построителя.
			if (R->CategoryFlag == 3) { Out.M.FixedWidth = 0.f; Out.M.WidthAdd = 38.f; }
			else                      { Out.M.FixedWidth = 250.f; }
			Out.M.ExtraLines = 2;
			Out.M.bPremiumFooter = (R->CategoryFlag == 3);
			// Подвал «элитного» предмета — нотис 109832 (`kLocEliteFooter` подсказки).
			Out.M.FooterText = (Out.M.bPremiumFooter && Text()) ? Text()->FormatNotice(109832) : FString();
			Out.Pipeline = TEXT("ItemTooltip_Build@0xEA5260 → Item_BuildTooltipText@0xEC2890 (полный)");
		}
		else
		{
			Out.Tip = Tip->BuildTooltipSimple(ItemCode, /*Quantity*/0, BindArg, Out.M);
			Out.Pipeline = TEXT("CItem_BuildSowelTooltipText@0xEA2EC0 (простой, по itemType)");
		}
	}
	Out.bValid = Out.Tip.bValid;
	return Out.bValid;
}
