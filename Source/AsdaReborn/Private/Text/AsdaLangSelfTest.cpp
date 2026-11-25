// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ [ПОРТ] 09.10.2026 (мультиязычность, этап 1) САМОТЕСТ ЯЗЫКОВОГО СЛОЯ ДАННЫХ — `asda.Lang.SelfTest [quit]`.
//   Проверяет точки выдачи текста данных, подключённые к `FAsdaLang` (сводка ключей —
//   `Reports/owner_1009/multilang/stage1_keys.md`):
//     • ФОРМАТ КЛЮЧЕЙ: путь оверлея, имя таблицы = ассет без `DT_`, ключ item_list = `_row` (НЕ ItemCode),
//       обратный индекс «указатель строки → RowName» (`AsdaLangRows::RowNameOf`);
//     • ТОЧКИ: каждая возвращает ОЖИДАЕМУЮ строку — в русской сессии ровно прежнюю RU-строку ассета (сравнение
//       с прямым чтением строки DataTable), в иной — строку оверлея языка, затем EN, затем RU (цепочка);
//       выборка id по каждой таблице: GameText, npc_list (FAsdaText::Get), item_list имя, item_tooltip описание,
//       sowell/title/pet/skilltooltip/stateinfotip/specialitem_tooltiplist (геттеры подсистемы), DT_NpcNames,
//       DT_MobNames, Talk (формат ключа), Constellation, EpisodeTable, QuestBook и страница сценария NPC
//       (перевод копии);
//     • ЦЕПОЧКА: у русского оверлеев не бывает; таблицы без EN-оверлея перечисляются (их точки в EN-сессии отдают RU).
//   Только чтение (ни одного пакета), но по правилу самотестов порта — только офлайн (`RefuseDebugWhenOnline`).
//   Строки проверок — «САМОТЕСТ-ЯЗЫК», итог — «САМОТЕСТ-ЯЗЫК ИТОГ: пройдено N, провалов M». Ключ `quit` — выход.
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): самотесты и стенды в Shipping не входят
#include "Text/AsdaLang.h"
#include "Text/AsdaLangRows.h"
// RefuseDebugWhenOnline — общий офлайн-гейт самотестов   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaDataTableCache.h"               // AsdaDt::Cached — те же таблицы, что у точек
#include "Items/AsdaItemTableSubsystem.h"     // геттеры item-БД (точки этапа 1)
#include "Items/AsdaItemDbRows.h"             // строки item-БД для прямого чтения
#include "Login/AsdaLoginData.h"              // FAsdaLoginData::LocRaw + путь DT_GameText
#include "Login/AsdaGameText.h"               // FAsdaGameTextRow / FAsdaTextRow
#include "Login/AsdaText.h"                   // FAsdaText::Get / FindTable (loose-таблицы)
#include "World/AsdaEntityText.h"             // имена NPC/мобов
#include "World/AsdaWorldSubsystem.h"         // FAsdaEntityNameRow
#include "UI/AsdaConstellationSubsystem.h"    // SignName — имена знаков
#include "Quest/AsdaQuestMarkerSubsystem.h"   // EpisodeTitleByEpisodeId — названия эпизодов
#include "Quest/AsdaQuestTypes.h"             // FAsdaQuestBookRow
#include "Npc/AsdaNpcDialogTypes.h"           // FAsdaEpisodeRow / FAsdaNsrPageRow
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaLangTest, Log, All);

namespace AsdaLangSelfTestPriv
{
	// Пути таблиц, которые точки читают сами (литералы продублированы только здесь, в тесте).
	const TCHAR* kTalkPath    = TEXT("/Game/AsdaAssets/Text/Dialogue/DT_Talk.DT_Talk");
	const TCHAR* kSignPath    = TEXT("/Game/AsdaAssets/Text/World/DT_Constellation.DT_Constellation");
	const TCHAR* kEpisodePath = TEXT("/Game/AsdaAssets/Data/NPC/DT_EpisodeTable.DT_EpisodeTable");
	const TCHAR* kBookPath    = TEXT("/Game/AsdaAssets/Data/Quest/DT_QuestBook.DT_QuestBook");
	const TCHAR* kPagePath    = TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcNsrPage.DT_NpcNsrPage");

	// Таблицы оверлеев этапа 1 — для отчёта о цепочке (есть ли EN-оверлей).
	const TCHAR* const kOverlayTables[] = {
		TEXT("GameText"), TEXT("npc_list"), TEXT("item_list"), TEXT("item_tooltip"), TEXT("sowell_list"),
		TEXT("sowell_tooltip"), TEXT("special_itemlist"), TEXT("specialitem_tooltiplist"), TEXT("title_list"),
		TEXT("title_option"), TEXT("pettable"), TEXT("petexptable"), TEXT("petoptiontable"), TEXT("skilltooltip"),
		TEXT("skill_info"), TEXT("jobtype"), TEXT("stateinfotip"), TEXT("soulstateinfotip"), TEXT("emotiontip"),
		TEXT("itemoptiontable"), TEXT("making_recipe"), TEXT("hairtable"), TEXT("soulmateskilltable"),
		TEXT("soulmateskill"), TEXT("soulsoulmatetext"), TEXT("loginhelpertext"), TEXT("NpcNames"), TEXT("MobNames"),
		TEXT("Talk"), TEXT("Constellation"), TEXT("EpisodeTable"), TEXT("QuestBook"), TEXT("RaidQuestData"),
		TEXT("TimList"), TEXT("NpcNsrHeader"), TEXT("NpcNsrButton"), TEXT("NpcNsrPage"), TEXT("ExtraMenu"),
		TEXT("FishBook"), TEXT("WarRank"), TEXT("SoulGuarder"), TEXT("MapTable"),
	};

	// Ожидаемый текст точки на языке сессии. Русская сессия — RU-строка ассета; иначе оверлей языка, затем EN,
	//   затем RU (решение владельца 09.10.2026). `bBakeable` — точка закрыта гейтом `ActiveForBakeable`
	//   (имена NPC/мобов): там, где гейт закрыт (мир редактора вне PIE), ожидается RU.
	FString Expect(const TCHAR* Table, const FString& Key, const FString& Ru, bool bBakeable = false)
	{
		if (FAsdaLang::IsRu()) { return Ru; }
		if (bBakeable && !AsdaLangRows::ActiveForBakeable()) { return Ru; }
		FString S;
		if (FAsdaLang::FindOverlay(FAsdaLang::Current(), Table, Key, S)) { return S; }
		if (FAsdaLang::Current() != EAsdaLang::En && FAsdaLang::FindOverlay(EAsdaLang::En, Table, Key, S)) { return S; }
		// ★ 05.11.2026 (gold-boast): русская заглушка «Нет» без перевода — пустая строка (`asda.Lang.DropRuPlaceholder`).
		if (AsdaLangRows::DropsRuPlaceholder(Ru)) { return FString(); }
		return Ru;
	}

	// Имена строк таблицы в порядке возрастания номера (имена ItemDB/loose — десятичные числа); нечисловые — в конце.
	TArray<FName> SortedRows(const UDataTable* Dt)
	{
		TArray<FName> N = Dt ? Dt->GetRowNames() : TArray<FName>();
		N.Sort([](const FName& A, const FName& B)
		{
			const FString SA = A.ToString(), SB = B.ToString();
			const bool bA = SA.IsNumeric(), bB = SB.IsNumeric();
			if (bA != bB) { return bA; }
			return bA ? FCString::Atoi(*SA) < FCString::Atoi(*SB) : SA < SB;
		});
		return N;
	}

	// Выборка `Count` имён строк, равномерно по таблице (первая, последняя и между ними).
	TArray<FName> Sample(const UDataTable* Dt, int32 Count)
	{
		const TArray<FName> All = SortedRows(Dt);
		TArray<FName> Out;
		if (All.Num() == 0 || Count <= 0) { return Out; }
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 Idx = (Count == 1) ? 0 : (int32)((int64)(All.Num() - 1) * i / (Count - 1));
			Out.AddUnique(All[Idx]);
		}
		return Out;
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdAsdaLangSelfTest(
	TEXT("asda.Lang.SelfTest"),
	TEXT("[ПОРТ] Самотест языкового слоя данных (этап 1): формат ключей, точки выдачи текста (RU-сессия — прежние "
	     "RU-строки), цепочка «язык → en → ru». `quit` — выйти после прохода."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaLangSelfTestPriv;
		bool bQuit = false;
		for (const FString& A : Args) { if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; } }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { UE_LOG(LogAsdaLangTest, Warning, TEXT("САМОТЕСТ-ЯЗЫК: нет GameInstance (нужен -game или PIE)")); return; }
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Lang.SelfTest"), KitArgs)) { return; }

		int32 Ok = 0, Fail = 0;
		auto Check = [&Ok, &Fail](bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaLangTest, Warning, TEXT("САМОТЕСТ-ЯЗЫК %s %s"), bOk ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
		};
		// Сравнение точки с ожиданием (текст в журнал — первые 60 знаков).
		auto Same = [&Check](const FString& Got, const FString& Want, const FString& What)
		{
			Check(Got == Want, FString::Printf(TEXT("%s: «%s»%s"), *What, *Got.Left(60).Replace(TEXT("\n"), TEXT(" ")),
				Got == Want ? TEXT("") : *FString::Printf(TEXT(" ≠ ожидалось «%s»"), *Want.Left(60).Replace(TEXT("\n"), TEXT(" ")))));
		};

		UE_LOG(LogAsdaLangTest, Warning, TEXT("САМОТЕСТ-ЯЗЫК старт: %s"), *FAsdaLang::Describe());
		UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();

		// ══ 1. ФОРМАТ КЛЮЧЕЙ ══
		Check(FAsdaLang::OverlayPath(EAsdaLang::En, TEXT("item_list"))
			== TEXT("/Game/AsdaAssets/Text/L10N/en/DT_Loc_item_list_en.DT_Loc_item_list_en"),
			TEXT("путь оверлея: /Game/AsdaAssets/Text/L10N/en/DT_Loc_item_list_en"));
		{
			FString S;
			Check(!FAsdaLang::FindOverlay(EAsdaLang::Ru, TEXT("GameText"), TEXT("1"), S), TEXT("у русского оверлеев нет (FindOverlay(Ru) = false)"));
		}
		const UDataTable* ItemDt = IT ? IT->GetDataTable(TEXT("item_list")) : nullptr;
		Check(ItemDt && ItemDt->GetName() == TEXT("DT_item_list"), TEXT("таблица оверлея item_list = имя ассета DT_item_list без «DT_»"));
		if (ItemDt && IT)
		{
			int32 RowIsNotCode = 0, IdxOk = 0, PtrOk = 0, N = 0;
			for (const FName& Rn : Sample(ItemDt, 12))
			{
				const FAsdaItemRow* R = ItemDt->FindRow<FAsdaItemRow>(Rn, TEXT("LangTest"), false);
				const FAsdaItemRecord* Rec = R ? IT->GetItem(R->ItemCode) : nullptr;
				if (!R || !Rec) { continue; }
				++N;
				if (Rn.ToString() != FString::FromInt(R->ItemCode)) { ++RowIsNotCode; }
				if (FString::FromInt(Rec->ImgIndex) == Rn.ToString()) { ++IdxOk; }
				if (AsdaLangRows::RowNameOf(ItemDt, R) == Rn) { ++PtrOk; }
			}
			Check(N > 0 && RowIsNotCode == N, FString::Printf(TEXT("ключ item_list = `_row`, не ItemCode (строк, где _row ≠ код: %d из %d)"), RowIsNotCode, N));
			Check(N > 0 && IdxOk == N, FString::Printf(TEXT("ImgIndex записи кэша = имя строки DT_item_list (%d из %d)"), IdxOk, N));
			Check(N > 0 && PtrOk == N, FString::Printf(TEXT("обратный индекс RowNameOf(указатель строки) = RowName (%d из %d)"), PtrOk, N));
			if (FAsdaLang::IsRu())
			{
				Check(FAsdaLang::ResolveRow(ItemDt, FName(TEXT("0")), TEXT("ItemName"), TEXT("<RU>")) == TEXT("<RU>"),
					TEXT("русская сессия: ResolveRow возвращает RU без поиска"));
			}
		}

		// ══ 2. GameText (FAsdaLoginData::LocRaw) ══
		if (const UDataTable* Dt = AsdaDt::Cached(FAsdaLoginData::kGameTextPath))
		{
			for (const FName& Rn : Sample(Dt, 6))
			{
				const FAsdaGameTextRow* R = Dt->FindRow<FAsdaGameTextRow>(Rn, TEXT("LangTest"), false);
				if (!R || !Rn.ToString().IsNumeric()) { continue; }
				const int32 Id = FCString::Atoi(*Rn.ToString());
				Same(FAsdaLoginData::LocRaw(Id), Expect(TEXT("GameText"), Rn.ToString(), R->SourceString),
					FString::Printf(TEXT("GameText %d (LocRaw)"), Id));
			}
		}
		else { Check(false, TEXT("DT_GameText не загрузилась")); }

		// ══ 3. loose npc_list (FAsdaText::Get) ══
		if (const UDataTable* Dt = FAsdaText::FindTable(TEXT("npc_list")))
		{
			for (const FName& Rn : Sample(Dt, 4))
			{
				const FAsdaTextRow* R = Dt->FindRow<FAsdaTextRow>(Rn, TEXT("LangTest"), false);
				if (!R || !Rn.ToString().IsNumeric()) { continue; }
				const int32 Id = FCString::Atoi(*Rn.ToString());
				Same(FAsdaText::Get(TEXT("npc_list"), Id, TEXT("name")), Expect(TEXT("npc_list"), Rn.ToString() + TEXT(".name"), R->name),
					FString::Printf(TEXT("npc_list %d.name (FAsdaText::Get)"), Id));
			}
		}
		else { Check(false, TEXT("DT_npc_list не найдена")); }

		// ══ 4. item_list: GetItemName (ключ `<_row>.ItemName`) ══
		if (ItemDt && IT)
		{
			for (const FName& Rn : Sample(ItemDt, 8))
			{
				const FAsdaItemRow* R = ItemDt->FindRow<FAsdaItemRow>(Rn, TEXT("LangTest"), false);
				const FAsdaItemRecord* Rec = R ? IT->GetItem(R->ItemCode) : nullptr;
				// Дубль кода: GetItem вернёт другую строку — сравнивать нечего (ключ строится по ImgIndex той записи).
				if (!Rec || FString::FromInt(Rec->ImgIndex) != Rn.ToString()) { continue; }
				Same(IT->GetItemName(R->ItemCode), Expect(TEXT("item_list"), Rn.ToString() + TEXT(".ItemName"), R->ItemName),
					FString::Printf(TEXT("item_list %d (строка %s).ItemName (GetItemName)"), R->ItemCode, *Rn.ToString()));
			}
		}

		// ══ 5. Геттеры item-БД: копии с текстом на языке сессии (ключ `<_row>.<поле>`) ══
		if (IT)
		{
			// Общий проход: строки таблицы `Table` с ключевым полем → геттер по id → сравнение полей.
			auto ByKey = [&](const TCHAR* Table, int32 Count, TFunctionRef<void(const UDataTable*, const FName&, const uint8*)> Fn)
			{
				const UDataTable* Dt = IT->GetDataTable(FName(Table));
				if (!Dt) { Check(false, FString::Printf(TEXT("%s не загружена"), Table)); return; }
				for (const FName& Rn : Sample(Dt, Count))
				{
					if (const uint8* const* P = Dt->GetRowMap().Find(Rn)) { Fn(Dt, Rn, *P); }
				}
			};
			ByKey(TEXT("item_tooltip"), 4, [&](const UDataTable* Dt, const FName& Rn, const uint8* P)
			{
				const FAsdaItemTooltipRow* R = reinterpret_cast<const FAsdaItemTooltipRow*>(P);
				FName Idx; FAsdaItemTooltipRow Got;
				// При дублях ItemCode ключевой индекс держит последнюю строку — сравниваем только «свою».
				if (!IT->FindRowName(TEXT("item_tooltip"), R->ItemCode, Idx) || Idx != Rn || !IT->GetItemTooltip(R->ItemCode, Got)) { return; }
				Same(Got.Description, Expect(TEXT("item_tooltip"), Rn.ToString() + TEXT(".Description"), R->Description),
					FString::Printf(TEXT("item_tooltip %d.Description (GetItemTooltip)"), R->ItemCode));
			});
			ByKey(TEXT("sowell_list"), 3, [&](const UDataTable* Dt, const FName& Rn, const uint8* P)
			{
				const FAsdaSowellRow* R = reinterpret_cast<const FAsdaSowellRow*>(P);
				FName Idx; FAsdaSowellRow Got;
				if (!IT->FindRowName(TEXT("sowell_list"), R->Id, Idx) || Idx != Rn || !IT->GetSowell(R->Id, Got)) { return; }
				Same(Got.Name, Expect(TEXT("sowell_list"), Rn.ToString() + TEXT(".Name"), R->Name), FString::Printf(TEXT("sowell_list %d.Name (GetSowell)"), R->Id));
			});
			ByKey(TEXT("title_list"), 3, [&](const UDataTable* Dt, const FName& Rn, const uint8* P)
			{
				const FAsdaTitleRow* R = reinterpret_cast<const FAsdaTitleRow*>(P);
				FName Idx; FAsdaTitleRow Got;
				if (!IT->FindRowName(TEXT("title_list"), R->Id, Idx) || Idx != Rn || !IT->GetTitle(R->Id, Got)) { return; }
				Same(Got.Name, Expect(TEXT("title_list"), Rn.ToString() + TEXT(".Name"), R->Name), FString::Printf(TEXT("title_list %d.Name (GetTitle)"), R->Id));
				Same(Got.Desc, Expect(TEXT("title_list"), Rn.ToString() + TEXT(".Desc"), R->Desc), FString::Printf(TEXT("title_list %d.Desc (GetTitle)"), R->Id));
			});
			ByKey(TEXT("pettable"), 3, [&](const UDataTable* Dt, const FName& Rn, const uint8* P)
			{
				const FAsdaPetRow* R = reinterpret_cast<const FAsdaPetRow*>(P);
				FName Idx; FAsdaPetRow Got;
				if (!IT->FindRowName(TEXT("pettable"), R->Id, Idx) || Idx != Rn || !IT->GetPet(R->Id, Got)) { return; }
				Same(Got.Name, Expect(TEXT("pettable"), Rn.ToString() + TEXT(".Name"), R->Name), FString::Printf(TEXT("pettable %d.Name (GetPet)"), R->Id));
			});
			ByKey(TEXT("skilltooltip"), 3, [&](const UDataTable* Dt, const FName& Rn, const uint8* P)
			{
				const FAsdaSkilltooltipRow* R = reinterpret_cast<const FAsdaSkilltooltipRow*>(P);
				FName Idx; FAsdaSkilltooltipRow Got;
				if (!IT->FindRowName(TEXT("skilltooltip"), R->Id, Idx) || Idx != Rn || !IT->GetSkillTooltip(R->Id, Got)) { return; }
				Same(Got.Name, Expect(TEXT("skilltooltip"), Rn.ToString() + TEXT(".Name"), R->Name), FString::Printf(TEXT("skilltooltip %d.Name (GetSkillTooltip)"), R->Id));
				Same(Got.Text, Expect(TEXT("skilltooltip"), Rn.ToString() + TEXT(".Text"), R->Text), FString::Printf(TEXT("skilltooltip %d.Text (GetSkillTooltip)"), R->Id));
			});
			ByKey(TEXT("stateinfotip"), 3, [&](const UDataTable* Dt, const FName& Rn, const uint8* P)
			{
				const FAsdaStateinfotipRow* R = reinterpret_cast<const FAsdaStateinfotipRow*>(P);
				FName Idx; FAsdaStateinfotipRow Got;
				if (!IT->FindRowName(TEXT("stateinfotip"), R->Id, Idx) || Idx != Rn || !IT->GetStateInfoTip(R->Id, Got)) { return; }
				Same(Got.Text, Expect(TEXT("stateinfotip"), Rn.ToString() + TEXT(".Text"), R->Text), FString::Printf(TEXT("stateinfotip %d.Text (GetStateInfoTip)"), R->Id));
			});
			// Подсказки спец-предметов — ПОЗИЦИОННЫЙ резолв: строка = код − первый код (имя строки = индекс).
			ByKey(TEXT("specialitem_tooltiplist"), 3, [&](const UDataTable* Dt, const FName& Rn, const uint8* P)
			{
				const FAsdaSpecialitemTooltiplistRow* R = reinterpret_cast<const FAsdaSpecialitemTooltiplistRow*>(P);
				if (!Rn.ToString().IsNumeric()) { return; }
				const int32 Idx = FCString::Atoi(*Rn.ToString());
				FAsdaSpecialitemTooltiplistRow Got;
				const FAsdaSpecialitemTooltiplistRow* R0 = Dt->FindRow<FAsdaSpecialitemTooltiplistRow>(FName(TEXT("0")), TEXT("LangTest"), false);
				if (!R0) { return; }
				// Позиционный резолв подсистемы: код = id первой записи + индекс строки.
				const int32 Code = R0->Id + Idx;
				if (!IT->GetSpecialItemTooltip(Code, Got)) { return; }
				Same(Got.Name, Expect(TEXT("specialitem_tooltiplist"), Rn.ToString() + TEXT(".Name"), R->Name),
					FString::Printf(TEXT("specialitem_tooltiplist код %d (строка %d).Name (GetSpecialItemTooltip)"), Code, Idx));
				Same(Got.Tooltip, Expect(TEXT("specialitem_tooltiplist"), Rn.ToString() + TEXT(".Tooltip"), R->Tooltip),
					FString::Printf(TEXT("specialitem_tooltiplist код %d.Tooltip"), Code));
			});
		}

		// ══ 6. Имена NPC и мобов (AsdaEntityText; гейт мира редактора) ══
		if (const UDataTable* Dt = AsdaDt::Cached(AsdaEntityText::kNpcNamesPath))
		{
			for (const FName& Rn : Sample(Dt, 4))
			{
				const FAsdaEntityNameRow* R = Dt->FindRow<FAsdaEntityNameRow>(Rn, TEXT("LangTest"), false);
				if (!R || !Rn.ToString().IsNumeric()) { continue; }
				const int32 Id = FCString::Atoi(*Rn.ToString());
				if (Id <= 0) { continue; }
				Same(AsdaEntityText::NpcName(Id), Expect(TEXT("NpcNames"), Rn.ToString() + TEXT(".Name"), R->Name, true),
					FString::Printf(TEXT("NpcNames %d.Name (AsdaEntityText::NpcName)"), Id));
			}
		}
		else { Check(false, TEXT("DT_NpcNames не загрузилась")); }
		if (const UDataTable* Dt = AsdaDt::Cached(AsdaEntityText::kMobNamesPath))
		{
			for (const FName& Rn : Sample(Dt, 4))
			{
				const FAsdaEntityNameRow* R = Dt->FindRow<FAsdaEntityNameRow>(Rn, TEXT("LangTest"), false);
				if (!R || !Rn.ToString().IsNumeric()) { continue; }
				const int32 MobTypeId = FCString::Atoi(*Rn.ToString()) + 1;   // строка = MobTypeId − 1
				Same(AsdaEntityText::MobName(MobTypeId, FString()), Expect(TEXT("MobNames"), Rn.ToString() + TEXT(".Name"), R->Name, true),
					FString::Printf(TEXT("MobNames %s.Name (AsdaEntityText::MobName(%d))"), *Rn.ToString(), MobTypeId));
			}
		}
		else { Check(false, TEXT("DT_MobNames не загрузилась")); }

		// ══ 7. Talk — формат ключа `<npc>:<line>` (сама точка — закрытый член диалога, выбор реплики случайный) ══
		if (const UDataTable* Dt = AsdaDt::Cached(kTalkPath))
		{
			for (const FName& Rn : Sample(Dt, 3))
			{
				const FAsdaTextRow* R = Dt->FindRow<FAsdaTextRow>(Rn, TEXT("LangTest"), false);
				if (!R) { continue; }
				const FString Key = FString::Printf(TEXT("%s:%d"), *R->npc, R->line);
				Same(FAsdaLang::Resolve(TEXT("Talk"), Key, R->text), Expect(TEXT("Talk"), Key, R->text),
					FString::Printf(TEXT("Talk «%s» (ключ реплики сценария)"), *Key));
			}
		}
		else { Check(false, TEXT("DT_Talk не загрузилась")); }

		// ══ 8. Знаки зодиака (UAsdaConstellationSubsystem::SignName) ══
		if (const UAsdaConstellationSubsystem* Cs = GI->GetSubsystem<UAsdaConstellationSubsystem>())
		{
			if (const UDataTable* Dt = AsdaDt::Cached(kSignPath))
			{
				for (int32 Id : { 1, 6, 12 })
				{
					const FAsdaTextRow* R = Dt->FindRow<FAsdaTextRow>(FName(*FString::FromInt(Id)), TEXT("LangTest"), false);
					if (!R) { continue; }
					Same(Cs->SignName((uint8)Id), Expect(TEXT("Constellation"), FString::Printf(TEXT("%d.name"), Id), R->name),
						FString::Printf(TEXT("Constellation %d.name (SignName)"), Id));
				}
			}
			else { Check(false, TEXT("DT_Constellation не загрузилась")); }
		}

		// ══ 9. Задания: название эпизода (точка маркеров) и книга задания (перевод копии) ══
		if (const UAsdaQuestMarkerSubsystem* QM = GI->GetSubsystem<UAsdaQuestMarkerSubsystem>())
		{
			if (const UDataTable* Dt = AsdaDt::Cached(kEpisodePath))
			{
				for (const FName& Rn : Sample(Dt, 3))
				{
					const FAsdaEpisodeRow* R = Dt->FindRow<FAsdaEpisodeRow>(Rn, TEXT("LangTest"), false);
					if (!R || Rn.ToString() != FString::FromInt(R->EpisodeId)) { continue; }
					Same(QM->EpisodeTitleByEpisodeId(R->EpisodeId), Expect(TEXT("EpisodeTable"), Rn.ToString() + TEXT(".Title"), R->Title),
						FString::Printf(TEXT("EpisodeTable %d.Title (EpisodeTitleByEpisodeId)"), R->EpisodeId));
				}
			}
			else { Check(false, TEXT("DT_EpisodeTable не загрузилась")); }
		}
		if (const UDataTable* Dt = AsdaDt::Cached(kBookPath))
		{
			for (const FName& Rn : Sample(Dt, 3))
			{
				const FAsdaQuestBookRow* R = Dt->FindRow<FAsdaQuestBookRow>(Rn, TEXT("LangTest"), false);
				if (!R) { continue; }
				const FAsdaQuestBookRow C = AsdaLangRows::CopyLocalized(Dt, *R);
				Same(C.Title, Expect(TEXT("QuestBook"), Rn.ToString() + TEXT(".Title"), R->Title), FString::Printf(TEXT("QuestBook %s.Title"), *Rn.ToString()));
				Same(C.Desc, Expect(TEXT("QuestBook"), Rn.ToString() + TEXT(".Desc"), R->Desc), FString::Printf(TEXT("QuestBook %s.Desc"), *Rn.ToString()));
				for (int32 i = 0; i < R->Steps.Num(); ++i)
				{
					if (R->Steps[i].IsEmpty()) { continue; }
					Same(C.Steps[i], Expect(TEXT("QuestBook"), FString::Printf(TEXT("%s.Steps.%d"), *Rn.ToString(), i), R->Steps[i]),
						FString::Printf(TEXT("QuestBook %s.Steps.%d"), *Rn.ToString(), i));
					break;   // одна стадия на книгу — формат ключа тот же
				}
				Check(Rn.ToString() == R->FileName, FString::Printf(TEXT("QuestBook: имя строки = FileName (%s)"), *R->FileName));
			}
		}
		else { Check(false, TEXT("DT_QuestBook не загрузилась")); }

		// ══ 10. Страница сценария NPC (перевод копии: `<nsr>_<talkId>.Text` / `.Steps.<i>.Caption`) ══
		if (const UDataTable* Dt = AsdaDt::Cached(kPagePath))
		{
			for (const FName& Rn : Sample(Dt, 3))
			{
				const FAsdaNsrPageRow* R = Dt->FindRow<FAsdaNsrPageRow>(Rn, TEXT("LangTest"), false);
				if (!R) { continue; }
				Check(Rn.ToString() == FString::Printf(TEXT("%s_%d"), *R->NsrName, R->TalkId),
					FString::Printf(TEXT("NpcNsrPage: имя строки = <nsr>_<talkId> (%s)"), *Rn.ToString()));
				FAsdaNsrPageRow C = *R;
				AsdaLangRows::Localize(Dt, Rn, C);
				Same(C.Text, Expect(TEXT("NpcNsrPage"), Rn.ToString() + TEXT(".Text"), R->Text), FString::Printf(TEXT("NpcNsrPage %s.Text"), *Rn.ToString()));
				if (R->Steps.Num() > 0)
				{
					Same(C.Steps[0].Caption, Expect(TEXT("NpcNsrPage"), Rn.ToString() + TEXT(".Steps.0.Caption"), R->Steps[0].Caption),
						FString::Printf(TEXT("NpcNsrPage %s.Steps.0.Caption"), *Rn.ToString()));
				}
			}
		}
		else { Check(false, TEXT("DT_NpcNsrPage не загрузилась")); }

		// ══ 11. Цепочка: таблицы без EN-оверлея (их точки в EN-сессии отдают RU — проверено ожиданием выше) ══
		{
			int32 Have = 0;
			FString Missing;
			for (const TCHAR* T : kOverlayTables)
			{
				const FString Pkg = FPackageName::ObjectPathToPackageName(FAsdaLang::OverlayPath(EAsdaLang::En, T));
				if (FPackageName::DoesPackageExist(Pkg)) { ++Have; }
				else { Missing += (Missing.IsEmpty() ? TEXT("") : TEXT(", ")) + FString(T); }
			}
			UE_LOG(LogAsdaLangTest, Warning, TEXT("САМОТЕСТ-ЯЗЫК цепочка: EN-оверлеев %d из %d; без EN (в EN-сессии — RU): %s"),
				Have, (int32)UE_ARRAY_COUNT(kOverlayTables), Missing.IsEmpty() ? TEXT("—") : *Missing);
		}

		AsdaSelfTestKit::Summary(LogAsdaLangTest, TEXT("САМОТЕСТ-ЯЗЫК"), Ok, Fail, TEXT(""), true);   // ★ 11.10.2026 (В3 S1): единый итог
		if (bQuit && GEngine)
		{
			// Выход — следующим кадром, не изнутри обработчика консольной команды.
			TWeakObjectPtr<UWorld> WW = W;
			W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WW]()
			{
				if (UWorld* World = WW.Get()) { GEngine->Exec(World, TEXT("quit")); }
			}));
		}
	}));
#endif // ASDA_DEV_CONSOLE

#endif // !UE_BUILD_SHIPPING
