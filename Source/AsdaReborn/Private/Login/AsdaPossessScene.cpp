// Copyright (c) 2026 @sitxovski (Discord)
#include "Login/AsdaPossessScene.h"
#include "World/AsdaPlayerCharacter.h"   // ★ общая точка kAsdaCharPreviewBaseM/F (база пути превью-скелета)
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Text/AsdaLang.h"   // [ПОРТ] 09.10.2026 (мультиязычность): текст панели души на языке сессии
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"   // EKeys::LeftMouseButton
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPossess, Log, All);

// ── База путей превью-скелета (ОБЩАЯ ТОЧКА `kAsdaCharPreviewBaseM/F` из заголовка) ─────────────
//   Клип парного набора живёт на скелете ПАРТНЁРА: мужской набор (`…mman`) — 50 костей с корнем
//   `M`, женский (`…woman`) — 54 кости с корнем `G`. Экспортёр `export_char_possess.py` кладёт
//   их на общий скелет `cremmy` с ремапом `G→M` / `M_s→M`, поэтому оба ложатся на свои базы.
static const TCHAR* GPreviewBaseM = kAsdaCharPreviewBaseM;
static const TCHAR* GPreviewBaseF = kAsdaCharPreviewBaseF;

FString FAsdaPossessData::PairClipSourceName(bool bMyMale, bool bHisMale, int32 ClipIdx)
{
	if (ClipIdx < 0 || ClipIdx > 4) { return FString(); }
	// 1:1 с эталоном: буква моего пола, буква его пола, слово по ЕГО полу.
	const TCHAR* Set = bMyMale ? (bHisMale ? TEXT("mmman") : TEXT("mwwoman"))
	                           : (bHisMale ? TEXT("wmman") : TEXT("wwwoman"));
	return FString::Printf(TEXT("cre_cre%s-a_00_%d.jbb"), Set, ClipIdx);
}

int32 FAsdaPossessData::PairClipAnimIndex(bool bMyMale, bool bHisMale, int32 ClipIdx)
{
	if (ClipIdx < 0 || ClipIdx > 4) { return -1; }
	// Блоки по 5: MM 205, MЖ 210, ЖM 215, ЖЖ 220 (см. коммент в заголовке — порт-соглашение).
	const int32 Base = bMyMale ? (bHisMale ? 205 : 210) : (bHisMale ? 215 : 220);
	return Base + ClipIdx;
}

FString FAsdaPossessData::PairClipAssetPath(bool bMyMale, bool bHisMale, int32 ClipIdx)
{
	const int32 Idx = PairClipAnimIndex(bMyMale, bHisMale, ClipIdx);
	if (Idx < 0) { return FString(); }
	// Скелет партнёра: его пол выбирает базу, мой — только номер клипа.
	return FString::Printf(TEXT("%sanim_%02d"), bHisMale ? GPreviewBaseM : GPreviewBaseF, Idx);
}

// ── База путей ассетов «души» (силуэта). Ассет самостоятельный: свой скелет + 5 клипов
//    `anim_00..04`, собранных `Tools/export_char_possess.py` ровно из эталонного набора
//    (`CEntity_LoadCreatureMorphModels_Cremmman@0xF05930`). ⚠ НЕ путать со СТАТИЧЕСКИМ импортом
//    `Models/Char/cre_cresoul*-a_00` из общего батча моделей: тот не анимируется.
static const TCHAR* GSoulBaseM = TEXT("/Game/AsdaAssets/Models/Skeletal/Characters/cre_cresoulman_preview/SkeletalMeshes/cre_cresoulman_preview");
static const TCHAR* GSoulBaseF = TEXT("/Game/AsdaAssets/Models/Skeletal/Characters/cre_cresoulwoman_preview/SkeletalMeshes/cre_cresoulwoman_preview");
// ★★★ СМЕШАННЫЕ ПАРЫ — ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА ПО РЕШЕНИЮ ВЛАДЕЛЬЦА 02.09.2026 (силуэт-заглушка
//   показывается ПРОТИВОПОЛОЖНЫМ полом). Ассеты собраны из АВТОРСКИХ клипов того же архива
//   (`cre_cremwwoman`+`cre_cremwsoul` и `cre_crewmman`+`cre_crewmsoul`), которые эталонный
//   загрузчик не открывает. Полное обоснование и ключ отката — у `SpawnSoulInSlot`.
//   Имя набора читается как «мой пол» + «пол силуэта» + слово по полу силуэта, как у парных клипов.
static const TCHAR* GSoulBaseMW = TEXT("/Game/AsdaAssets/Models/Skeletal/Characters/cre_cresoulmwwoman_preview/SkeletalMeshes/cre_cresoulmwwoman_preview");
static const TCHAR* GSoulBaseWM = TEXT("/Game/AsdaAssets/Models/Skeletal/Characters/cre_cresoulwmman_preview/SkeletalMeshes/cre_cresoulwmman_preview");

// База набора «души» по моему полу и признаку «силуэт противоположного пола».
static const TCHAR* SoulBase(bool bMyMale, bool bOppositeSex)
{
	if (bOppositeSex) { return bMyMale ? GSoulBaseMW : GSoulBaseWM; }
	return bMyMale ? GSoulBaseM : GSoulBaseF;
}

FString FAsdaPossessData::SoulMeshAssetPath(bool bMyMale, bool bOppositeSex)
{
	return FString(SoulBase(bMyMale, bOppositeSex));
}

FString FAsdaPossessData::SoulClipAssetPath(bool bMyMale, bool bOppositeSex, int32 ClipIdx)
{
	if (ClipIdx < 0 || ClipIdx > 4) { return FString(); }
	return FString::Printf(TEXT("%sanim_%02d"), SoulBase(bMyMale, bOppositeSex), ClipIdx);
}

FString FAsdaPossessData::SoulInfoText(const UGameInstance* GI, int32 Type)
{
	if (!GI) { return FString(); }
	UAsdaItemTableSubsystem* Items = const_cast<UGameInstance*>(GI)->GetSubsystem<UAsdaItemTableSubsystem>();
	const UDataTable* DT = Items ? Items->GetDataTable(FName(TEXT("soulsoulmatetext"))) : nullptr;
	if (!DT)
	{
		UE_LOG(LogAsdaPossess, Warning, TEXT("DT_soulsoulmatetext не загружена — панель 114 останется пустой"));
		return FString();
	}
	// ★ Порядок склейки — ПОРЯДОК ЗАПИСЕЙ В ФАЙЛЕ (эталон читает bin последовательно и делает
	//   `+=`). Имена строк таблицы = `_row` исходного bin, то есть десятичный номер записи;
	//   поэтому идём по 0..N-1, а не по `GetRowNames()` (её порядок не гарантирован).
	FString Out;
	int32 Used = 0;
	const int32 Total = DT->GetRowMap().Num();
	for (int32 i = 0; i < Total; ++i)
	{
		const FAsdaSoulsoulmatetextRow* R =
			DT->FindRow<FAsdaSoulsoulmatetextRow>(FName(*FString::FromInt(i)), TEXT("SoulInfoText"), false);
		if (!R || R->Type != Type) { continue; }
		// ★ [ПОРТ] 09.10.2026 (мультиязычность): оверлей `soulsoulmatetext`, ключ `<_row>.Text` (имя строки = i).
		Out += FAsdaLang::ResolveRow(DT, FName(*FString::FromInt(i)), TEXT("Text"), R->Text);
		++Used;
	}
	if (Used == 0)
	{
		// Тип без записей — это НЕ ошибка: в нашей сборке `soulsoulmatetext.bin` записей типа 1
		//   действительно нет (проверено распаковкой `Bin.bbz`), и эталон в этом случае показывает
		//   пустой контрол 116. Пишем в лог, чтобы отличать «нет данных» от «таблица не пришла».
		UE_LOG(LogAsdaPossess, Log, TEXT("DT_soulsoulmatetext: записей типа %d нет — контрол останется пустым"), Type);
	}
	return Out;
}

void UAsdaScenePickCatcher::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// Дерево создаётся движком при Initialize(); корня у виджета без блюпринта нет — ставим сами.
	//   Прозрачная рамка (альфа 0) заполняет весь слот вьюпорта и попадает в хит-тест-сетку Slate:
	//   хит-тест смотрит на геометрию и видимость, а не на альфу, поэтому клик доходит и через неё.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		if (UBorder* Plane = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ScenePickPlane")))
		{
			Plane->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
			Plane->SetVisibility(ESlateVisibility::Visible);   // ⚠ не SelfHitTestInvisible — иначе мышь не ловится
			WidgetTree->RootWidget = Plane;
		}
	}
}

FReply UAsdaScenePickCatcher::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnSceneClicked.ExecuteIfBound();
		// Handled: клик по сцене — наш; дальше по стеку виджетов он не нужен. Кнопки панели лежат
		//   ВЫШЕ ловца, до него их клики не доходят вовсе.
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}
