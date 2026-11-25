// Copyright (c) 2026 @sitxovski (Discord)
// AsdaQuestMarkerSubsystem_Debug.cpp — ★ R3-c 12.11.2026 (разрез AsdaQuestMarkerSubsystem.cpp, дословный перенос): дев-команды и самотесты меток (QuestMarkPage / QuestMarkArshes / QuestMarkMelve).
//   Общее для частей — в AsdaQuestMarkerSubsystem_Private.h; шапка и реверс — в AsdaQuestMarkerSubsystem.cpp.
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "Npc/AsdaNpcGmOnly.h"   // ★ 09.11.2026: NPC «только для GM_»
#include "Quest/AsdaNsrScriptPage.h"     // ★ 04.11.2026 (questmark-chmella): одно правило «запись без страницы» для меток и разговора
#include "Npc/AsdaNpcDialogTypes.h"      // FAsdaNsrPageRow — проверка наличия страницы сценария
#include "Quest/AsdaQuestGuideTarget.h"   // ★ 31.10.2026 (баг 6): место цели строки гида без координат — общая точка с «Следовать»
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaDataTableCache.h"   // ★ Аудит №4 (B2): AsdaDt::Cached — одна ленивая загрузка DataTable на порт

#include "AsdaNetSubsystem.h"
#include "Npc/AsdaNpcActor.h"
#include "UI/AsdaMinimapSubsystem.h"
// FAsdaMarkup::StripTags — единая точка разбора тегов <F..>   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Npc/AsdaNpcDialogSubsystem.h"     // ShowQuestInfo/HideQuestInfo — окно QUEST_INFO_WIN (599)
#include "World/AsdaLoadManagerSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"        // ★ Волна В4: роспись «закрыть всё» (трекер 1800)

#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Containers/Ticker.h"              // ★ 08.11.2026: самотест asda.SelfTest.QuestMarkArshes (шаги по времени)
#include "UnrealClient.h"                   // FScreenshotRequest — кадры самотеста в файлы
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Text/AsdaLangRows.h"               // [ПОРТ] 09.10.2026 (мультиязычность): названия эпизодов и деревень на языке сессии
#include "AsdaQuestMarkerSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaQuestMarkerSubsystem*.cpp

// ── Консоль ─────────────────────────────────────────────────────────────────────────────────────
// ★ ОФЛАЙН-ЭМУЛЯЦИЯ ПРИХОДА 5047 (сервер не нужен). Собирает то же 688-байтовое тело, что шлёт
//   сервер, и пропускает его штатным путём `ApplyQuestStateBlob`. Нужна для доказательной
//   проверки условий и облачка выбора квеста на PIE: без состояния квестов КАЖДОЕ условие
//   категории 2 честно ложно, и проверить вычислитель нечем.
//   Формат: `asda.Quest.FakeState <номер квеста> <стадия> [список завершённых через запятую]`
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaQuestFakeState(
	TEXT("asda.Quest.FakeState"),
	TEXT("asda.Quest.FakeState <questNo> <stage> [завершённые,через,запятую] — офлайн-эмуляция пакета 5047"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельное состояние квестов — клики NPC отправили бы 5043/5041 по нему.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Quest.FakeState"))) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
		if (!QM || Args.Num() < 1) { return; }
		const int32 QuestNo = FCString::Atoi(*Args[0]);
		const int32 Stage = (Args.Num() > 1) ? FCString::Atoi(*Args[1]) : 0;

		TArray<uint8> Blob; Blob.SetNumZeroed(688);
		auto W32 = [&Blob](int32 O, int32 V) { Blob[O] = V & 0xFF; Blob[O+1] = (V>>8)&0xFF; Blob[O+2] = (V>>16)&0xFF; Blob[O+3] = (V>>24)&0xFF; };
		auto W16 = [&Blob](int32 O, int32 V) { Blob[O] = V & 0xFF; Blob[O+1] = (V>>8)&0xFF; };
		// Слот 0 — «квест взят»: `QuestId >= 2001` (эталонный признак занятости), номер и стадия.
		W32(0, 2001); W16(8, QuestNo); W16(10, Stage);
		// Цели слота оставляем нулевыми ⇒ «все набраны» ⇒ квест готов к сдаче.
		for (int32 j = 0; j < 5; ++j) { W32(14 + 6*j, 0); W16(18 + 6*j, 0); }
		// Битовая карта «квест завершён» — 160 байт с +528.
		if (Args.Num() > 2)
		{
			TArray<FString> Done; Args[2].ParseIntoArray(Done, TEXT(","), true);
			for (const FString& D : Done)
			{
				const int32 Id = FCString::Atoi(*D);
				if (Id >= 0 && (Id >> 3) < 160) { Blob[528 + (Id >> 3)] |= (uint8)(1u << (Id & 7)); }
			}
		}
		QM->ApplyQuestStateBlob(Blob);
		UE_LOG(LogAsdaQuest, Log,
			TEXT("[квест-метки] ОФЛАЙН-ЭМУЛЯЦИЯ 5047: квест %d взят (стадия %d), завершённые=«%s»"),
			QuestNo, Stage, Args.Num() > 2 ? *Args[2] : TEXT(""));
	}));
#endif // ASDA_DEV_CONSOLE

// ★ Разбор условия ЛЮБОЙ записи сценария по имени NSR и номеру записи (проверка вычислителя).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaQuestFakeSelf(
	TEXT("asda.Quest.FakeSelf"),
	TEXT("asda.Quest.FakeSelf <уровень> [класс] [субкласс] — офлайн-подмена уровня/класса для проверки условий (-1 = снять)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: подмена уровня/класса меняет доступные шаги диалога NPC — клики отправили бы
		//   5043/5041 по неверным условиям. Снятие подмены разрешено всегда, но снятием считается ТОЛЬКО случай, когда
		//   аргументов нет или ВСЕ они равны −1. ★ Редакция 1 (СРЕДН-1): прежняя проверка смотрела лишь уровень, и
		//   `asda.Quest.FakeSelf -1 5 3` онлайн подменял класс/подкласс мимо гейта (потребитель применяет их независимо).
		const bool bSetting = Args.ContainsByPredicate([](const FString& A) { return FCString::Atoi(*A) != -1; });
		if (bSetting && FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Quest.FakeSelf"))) { return; }
		GAsdaFakeSelfLevel = (Args.Num() > 0) ? FCString::Atoi(*Args[0]) : -1;
		GAsdaFakeSelfClass = (Args.Num() > 1) ? FCString::Atoi(*Args[1]) : -1;
		GAsdaFakeSelfSub   = (Args.Num() > 2) ? FCString::Atoi(*Args[2]) : -1;
		UE_LOG(LogAsdaQuest, Log, TEXT("[квест-метки] ОФЛАЙН-ПОДМЕНА персонажа: уровень=%d класс=%d субкласс=%d"),
			GAsdaFakeSelfLevel, GAsdaFakeSelfClass, GAsdaFakeSelfSub);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaQuestCondWhy(
	TEXT("asda.Quest.CondWhy"),
	TEXT("asda.Quest.CondWhy <ключ SNQICT> — разбор условия записи SNQICT.Bin (категория/итог)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
		if (!QM || Args.Num() < 1) { return; }
		const int32 Key = FCString::Atoi(*Args[0]);
		const FAsdaSnqIctRow* R = AsdaDt::RowByInt<FAsdaSnqIctRow>(
			TEXT("/Game/AsdaAssets/Data/NPC/DT_SnqIct.DT_SnqIct"), Key, TEXT("QuestMarker"));
		if (!R) { UE_LOG(LogAsdaQuest, Warning, TEXT("[условие] ключа %d нет в DT_SnqIct"), Key); return; }
		FString Why;
		const bool bOk = QM->EvalSnqIctCond(*R, &Why);
		UE_LOG(LogAsdaQuest, Log,
			TEXT("[условие] SNQICT ключ %d: talkId=%d уровень %d..%d, ")
			TEXT("квесты (завершён/не взят/в работе) %d/%d/%d -> %s"),
			Key, R->TalkId, R->LevelMin, R->LevelMax, R->QuestDone, R->QuestFreeA, R->QuestActive,
			bOk ? TEXT("ВЫПОЛНЕНО") : TEXT("НЕ выполнено"));
		UE_LOG(LogAsdaQuest, Log, TEXT("%s"), *Why);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaQuestMarks(
	TEXT("Asda.Quest.Marks"),
	TEXT("Квестовые метки NPC: состояние квестов, посчитанные метки, размер таблиц. "
	     "С аргументом <npcId> — метка конкретного NPC."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			UAsdaQuestMarkerSubsystem* Q = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
			if (!Q) { UE_LOG(LogAsdaQuest, Warning, TEXT("[квест-метки] подсистема недоступна")); return; }
			if (Args.Num() >= 1)
			{
				const int32 Id = FCString::Atoi(*Args[0]);
				UE_LOG(LogAsdaQuest, Log, TEXT("[квест-метки] NPC %d → метка %d"), Id, Q->GetMarkForNpcId(Id));
				return;
			}
			UE_LOG(LogAsdaQuest, Log, TEXT("[квест-метки] %s"), *Q->DescribeMarks());
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaQuestAwaitMaps(
	TEXT("Asda.Quest.AwaitMaps"),
	TEXT("Подсказка робота «Минопио» (окно 599): карты, где ждёт новый квест, и итоговая строка. "
	     "С аргументом «show» — показать баннер немедленно, минуя проверку «состав не менялся»."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			UAsdaQuestMarkerSubsystem* Q = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
			if (!Q) { UE_LOG(LogAsdaQuest, Warning, TEXT("[Минопио] подсистема недоступна")); return; }
			Q->RebuildMarks();
			if (Args.Num() >= 1 && Args[0].Equals(TEXT("show"), ESearchCase::IgnoreCase))
			{
				UAsdaNpcDialogSubsystem* Dlg = GI->GetSubsystem<UAsdaNpcDialogSubsystem>();
				const FString M = Q->GetAwaitHintMarkup();
				if (Dlg && !M.IsEmpty()) { Dlg->ShowQuestInfoEx(M, /*bMarkBusy*/ false, /*bMayDefer*/ false); }
				else { UE_LOG(LogAsdaQuest, Log, TEXT("[Минопио] показывать нечего")); }
			}
			UE_LOG(LogAsdaQuest, Log, TEXT("[Минопио] %s"), *Q->DescribeAwaitMaps());
		}));
#endif // ASDA_DEV_CONSOLE

// ★ ДИАГНОСТИКА высоты метки: поставить одну и ту же метку ВСЕМ NPC карты (без сервера настоящих
//   меток нет). `off` / `-1` — снять принуждение и вернуться к посчитанным меткам.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaQuestMarkAll(
	TEXT("Asda.Quest.MarkAll"),
	TEXT("Asda.Quest.MarkAll <0..3|off> — принудительная метка на всех NPC карты (диагностика высоты)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			UAsdaQuestMarkerSubsystem* Q = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
			if (!Q) { UE_LOG(LogAsdaQuest, Warning, TEXT("[квест-метки] подсистема недоступна")); return; }
			const int32 M = (Args.Num() >= 1 && !Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
				? FCString::Atoi(*Args[0]) : -1;
			Q->SetForcedMarkForAll(M);
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaQuestRebuild(
	TEXT("Asda.Quest.Rebuild"),
	TEXT("Пересчитать квестовые метки немедленно."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>&, UWorld* World)
		{
			UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			if (UAsdaQuestMarkerSubsystem* Q = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr)
			{
				Q->RebuildMarks();
				UE_LOG(LogAsdaQuest, Log, TEXT("[квест-метки] %s"), *Q->DescribeMarks());
			}
		}));
#endif // ASDA_DEV_CONSOLE

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  САМОТЕСТ `asda.SelfTest.QuestMarkPage` (04.11.2026, questmark-chmella)
// ════════════════════════════════════════════════════════════════════════════════════════════════
// Проверяет одно правило «запись сценария без страницы пропускается» (`AsdaNsrScript::ShouldSkipRecordWithoutPage`) на путях
//   игрока: (А) перепись записей без страницы, (Б) Хмелла (npc 20) по шагам «до приёма / приём 303 / цели собраны / сдано» и
//   мёртвая сдача 135, при ключе 1 и 0, (В) по ВСЕМ сценариям и набору уровней — метка = запись, которую выбрал бы разговор
//   (`RunScriptRecordScan`: условия → страница), (Г) метка над NPC текущей карты через `RebuildMarks` → `GetMarkForNpcId`
//   (Фхашу 14 на Альпене: сдача 192 видна записью #0 опкода 123, хотя его записи #31/#32/#36 без страниц).
//   Только офлайн (`-AsdaNoNet`): подменяет состояние квестов и уровень/класс, в конце снимает подмену.
#if ASDA_DEV_CONSOLE
struct FAsdaQuestMarkPageSelfTest
{
	static int32 Ok;     // пройдено проверок
	static int32 Fail;   // провалено проверок

	static void Check(bool bOk, const FString& What)
	{
		bOk ? ++Ok : ++Fail;
		UE_LOG(LogAsdaQuest, Log, TEXT("QMARK-PAGE %s %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
	}

	// Пакет 5047 (688 б): слоты по 44 б (`+0` id ≥ 2001, `+8` номер, `+10` стадия, цели нулевые), с `+528` карта завершения.
	static TArray<uint8> Blob(const TArray<FIntPoint>& Active, const TArray<int32>& Done)
	{
		TArray<uint8> B; B.SetNumZeroed(688);
		auto W16 = [&B](int32 O, int32 V) { B[O] = V & 0xFF; B[O + 1] = (V >> 8) & 0xFF; };
		for (int32 s = 0; s < 12; ++s) { W16(44 * s, 0xFFFF); W16(44 * s + 2, 0xFFFF); }   // пустой слот: id = −1
		for (int32 i = 0; i < Active.Num() && i < 12; ++i)
		{
			const int32 O = 44 * i;
			B[O] = 2001 & 0xFF; B[O + 1] = (2001 >> 8) & 0xFF; B[O + 2] = 0; B[O + 3] = 0;
			W16(O + 8, Active[i].X); W16(O + 10, Active[i].Y);
		}
		for (int32 Id : Done) { if (Id >= 0 && (Id >> 3) < 160) { B[528 + (Id >> 3)] |= (uint8)(1u << (Id & 7)); } }
		return B;
	}

	static void SetState(UAsdaQuestMarkerSubsystem* QM, int32 Lv, int32 Cls, const TArray<FIntPoint>& Active, const TArray<int32>& Done)
	{
		GAsdaFakeSelfLevel = Lv; GAsdaFakeSelfClass = Cls; GAsdaFakeSelfSub = 0;
		QM->ApplyQuestStateBlob(Blob(Active, Done));
	}

	// Какую запись выбрал бы РАЗГОВОР (тело `RunScriptRecordScan` без отрисовки): метка этой записи или 0. Опкод 122 метки не
	//   даёт, разговор на нём останавливается — у всех сценариев он на `RecIndex 63`, после всех записей с меткой.
	static int32 DialogPickMark(const UAsdaQuestMarkerSubsystem* QM, const FString& Nsr, int32* OutRec)
	{
		*OutRec = -1;
		const TArray<const FAsdaNpcQuestScriptRow*>* Recs = QM->ScriptByNsr.Find(Nsr.ToLower());
		if (!Recs) { return 0; }
		for (const FAsdaNpcQuestScriptRow* R : *Recs)
		{
			if (R->RecIndex >= 64) { break; }
			if (R->Opcode == 122) { continue; }
			if (R->Opcode == 123)
			{
				const int32 N = QM->CollectDeliverableForNsr(Nsr);
				if (N >= 2 && !AsdaNsrScript::ShouldSkipRecordWithoutPage(Nsr, R->PageId)) { *OutRec = R->RecIndex; return N == 99 ? 2 : 1; }
				continue;
			}
			if (R->Opcode == 121) { continue; }
			if (QM->CheckRecord(*R) && !AsdaNsrScript::ShouldSkipRecordWithoutPage(Nsr, R->PageId)) { *OutRec = R->RecIndex; return R->Mark; }
		}
		return 0;
	}

	struct FCase { const TCHAR* Name; int32 Lv; int32 Cls; TArray<FIntPoint> Act; TArray<int32> Done; int32 Want1; int32 Want0; };

	static void Run(UWorld* W)
	{
		Ok = Fail = 0;
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
		IConsoleVariable* Key = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Npc.SkipMissingPage"));
		if (!QM || !Key) { Check(false, TEXT("нет подсистемы меток или ключа asda.Npc.SkipMissingPage")); }
		else
		{
			QM->EnsureTables();
			const int32 KeyWas = Key->GetInt();
			Key->Set(1, ECVF_SetByConsole);

			// (А) перепись: записи, способные дать метку (опкод 123 или метка 1…3, не 121/122), без страницы.
			int32 Missing = 0, MissChm = 0;
			FString ChmList;
			for (const TPair<FString, TArray<const FAsdaNpcQuestScriptRow*>>& It : QM->ScriptByNsr)
			{
				for (const FAsdaNpcQuestScriptRow* R : It.Value)
				{
					if (R->Opcode == 121 || R->Opcode == 122) { continue; }
					if (R->Opcode != 123 && (R->Mark < 1 || R->Mark > 3)) { continue; }
					if (!AsdaNsrScript::ShouldSkipRecordWithoutPage(It.Key, R->PageId)) { continue; }
					++Missing;
					UE_LOG(LogAsdaQuest, Log, TEXT("QMARK-PAGE запись без страницы: %s #%d опкод %d страница %d метка %d"),
						*It.Key, R->RecIndex, R->Opcode, R->PageId, R->Mark);
					if (It.Key == TEXT("chmella")) { ++MissChm; ChmList += FString::Printf(TEXT(" #%d"), R->RecIndex); }
				}
			}
			Check(Missing == 139, FString::Printf(TEXT("(А) записей с меткой без страницы: %d (ожидалось 139)"), Missing));
			Check(MissChm == 13, FString::Printf(TEXT("(А) у Хмеллы: %d —%s (ожидалось #3…#8, #13…#19)"), MissChm, *ChmList));

			// (Б) Хмелла по шагам игрока. Ожидание при ключе 1 / при ключе 0 (прежнее поведение).
			const TArray<int32> LuffyDone = { 23, 57, 296, 297, 298, 299, 300, 312 };
			TArray<int32> LuffyDone2 = LuffyDone; LuffyDone2.Add(303);
			TArray<FCase> Cases;
			Cases.Add({ TEXT("новичок ур.41: задание 312 доступно (страница 13 есть)"), 41, 0, {}, {}, 1, 1 });
			Cases.Add({ TEXT("LUFFY до приёма 303 (класс 24, 296–300 сданы): была фантомная #16 «135 не взято»"), 100, 24, {}, LuffyDone, 0, 1 });
			Cases.Add({ TEXT("LUFFY принял 303 у статуи (стадия 0): сдача у Хмеллы — красный, запись #2"), 100, 24, { FIntPoint(303, 0) }, LuffyDone, 2, 2 });
			Cases.Add({ TEXT("LUFFY сдал 303 (класс 25, 370 в работе): была фантомная #16"), 100, 25, { FIntPoint(370, 0) }, LuffyDone2, 0, 1 });
			Cases.Add({ TEXT("задание 57 в работе, стадия 2 (цели собраны): сдача — красный, запись #10"), 30, 23, { FIntPoint(57, 2) }, {}, 2, 2 });
			Cases.Add({ TEXT("задание 57 в работе, стадия 0 (цели не собраны): красного нет, жёлтый за 312"), 30, 23, { FIntPoint(57, 0) }, {}, 1, 1 });
			Cases.Add({ TEXT("задание 135 в работе, стадия 1 (сдачи у сервера нет): фантомный красный #5 → жёлтый за 312"), 50, 0, { FIntPoint(135, 1) }, {}, 1, 2 });
			for (const FCase& C : Cases)
			{
				SetState(QM, C.Lv, C.Cls, C.Act, C.Done);
				Key->Set(0, ECVF_SetByConsole);
				const int32 M0 = QM->EvalMarkForNsr(TEXT("chmella"));
				Key->Set(1, ECVF_SetByConsole);
				const int32 M1 = QM->EvalMarkForNsr(TEXT("chmella"));
				int32 Rec = -1;
				const int32 D1 = DialogPickMark(QM, TEXT("chmella"), &Rec);
				Check(M1 == C.Want1 && M0 == C.Want0 && D1 == M1,
					FString::Printf(TEXT("(Б) %s: метка %d (ждали %d), ключ 0 — %d (ждали %d), запись разговора #%d → %d"),
						C.Name, M1, C.Want1, M0, C.Want0, Rec, D1));
			}

			// (В) все сценарии × уровни: метка = выбор разговора; что изменил ключ — в журнал.
			int32 Pairs = 0, Agree = 0, Changed = 0;
			const int32 Levels[] = { 1, 10, 30, 41, 60, 100, 150 };
			for (int32 Lv : Levels)
			{
				SetState(QM, Lv, 0, {}, {});
				for (const TPair<FString, TArray<const FAsdaNpcQuestScriptRow*>>& It : QM->ScriptByNsr)
				{
					++Pairs;
					int32 Rec = -1;
					const int32 M1 = QM->EvalMarkForNsr(It.Key);
					const int32 D1 = DialogPickMark(QM, It.Key, &Rec);
					if (M1 == D1) { ++Agree; }
					else { UE_LOG(LogAsdaQuest, Log, TEXT("QMARK-PAGE расхождение: %s ур.%d метка %d, разговор #%d → %d"), *It.Key, Lv, M1, Rec, D1); }
					Key->Set(0, ECVF_SetByConsole);
					const int32 M0 = QM->EvalMarkForNsr(It.Key);
					Key->Set(1, ECVF_SetByConsole);
					if (M0 != M1)
					{
						++Changed;
						UE_LOG(LogAsdaQuest, Log, TEXT("QMARK-PAGE снята фантомная метка: %s ур.%d %d → %d"), *It.Key, Lv, M0, M1);
					}
				}
			}
			Check(Agree == Pairs, FString::Printf(TEXT("(В) метка = выбор разговора: %d из %d пар «сценарий×уровень»; ключом изменено %d"),
				Agree, Pairs, Changed));

			// (Г) метка над NPC текущей карты: Фхашу (14).
			const FAsdaNpcQuestNpcRow* Fh = nullptr;
			for (const FAsdaNpcQuestNpcRow* N : QM->NpcRows) { if (N && N->NpcId == 14) { Fh = N; } }
			UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
			if (Fh && LM && LM->CurrentMapId == Fh->MapId)
			{
				// Записи #31/#32/#35/#36 Фхашу без страниц. Состояние «192 в работе, класс 0»: при ключе 0 перебор встаёт на #35
				//   (метка 0, «192 в работе», страницы 36 нет) — ни метки, ни страницы; при ключе 1 обе ветки пропускают #35 и
				//   доходят до #61 «задание 23 не взято, ур. 5+, класс 0» (страница 13 есть, ячейка «выдаёт» 23 у Фхашу есть) —
				//   жёлтый «!» и страница предложения 23. Проверяем: метка над головой = перебор = выбор разговора = #61.
				SetState(QM, 60, 0, { FIntPoint(192, 1) }, {});
				QM->RebuildMarks();
				const int32 A = QM->GetMarkForNpcId(14);
				int32 RecA = -1;
				const int32 DA = DialogPickMark(QM, TEXT("fhashu"), &RecA);
				Key->Set(0, ECVF_SetByConsole);
				const int32 A0 = QM->EvalMarkForNsr(TEXT("fhashu"));
				Key->Set(1, ECVF_SetByConsole);
				Check(A == 1 && A == QM->EvalMarkForNsr(TEXT("fhashu")) && A == DA && RecA == 61 && A0 == 0,
					FString::Printf(TEXT("(Г) Фхашу над головой, 192 в работе (стадия 1): метка %d = разговор #%d → %d (ждали 1, #61); ключ 0 — %d (ждали 0, #35)"),
						A, RecA, DA, A0));
				SetState(QM, 100, 25, { FIntPoint(370, 0) }, LuffyDone2);
				QM->RebuildMarks();
				const int32 B = QM->GetMarkForNpcId(14);
				Check(B == 0, FString::Printf(TEXT("(Г) Фхашу при состоянии LUFFY после сдачи 303: метка %d (ждали 0)"), B));
			}
			// (Ж) настоящий актор Хмеллы (npc 20) на её карте (Силарис, карта 0): после `RebuildMarks` у актора та метка, что
			//   выбирает разговор, по шагам LUFFY. Запуск: `--map "/Game/Map/silalis/silalis?game=…" --pre "Asda.Load.CurrentMap 0"`.
			AAsdaNpcActor* Chm = nullptr;
			for (TActorIterator<AAsdaNpcActor> AIt(W); AIt; ++AIt) { if (AIt->NpcId == 20) { Chm = *AIt; break; } }
			const FAsdaNpcQuestNpcRow* ChmRow = nullptr;
			for (const FAsdaNpcQuestNpcRow* N : QM->NpcRows) { if (N && N->NpcId == 20) { ChmRow = N; } }
			const bool bOnChmMap = Chm && ChmRow && LM && LM->CurrentMapId == ChmRow->MapId;
			if (bOnChmMap)
			{
				const TArray<int32> LDone = { 23, 57, 296, 297, 298, 299, 300, 312 };
				TArray<int32> LDone2 = LDone; LDone2.Add(303);
				struct FStep { const TCHAR* Name; int32 Cls; TArray<FIntPoint> Act; TArray<int32> Done; int32 Want; };
				TArray<FStep> Steps;
				Steps.Add({ TEXT("до приёма 303"), 24, {}, LDone, 0 });
				Steps.Add({ TEXT("303 принят (стадия 0)"), 24, { FIntPoint(303, 0) }, LDone, 2 });
				Steps.Add({ TEXT("303 сдан, 370 в работе"), 25, { FIntPoint(370, 0) }, LDone2, 0 });
				for (const FStep& St : Steps)
				{
					SetState(QM, 100, St.Cls, St.Act, St.Done);
					QM->RebuildMarks();
					int32 Rec = -1;
					const int32 D = DialogPickMark(QM, TEXT("chmella"), &Rec);
					Check(Chm->QuestMarkKind == St.Want && D == St.Want,
						FString::Printf(TEXT("(Ж) актор Хмеллы, LUFFY «%s»: метка над головой %d, разговор #%d → %d (ждали %d)"),
							St.Name, Chm->QuestMarkKind, Rec, D, St.Want));
				}
			}
			// (И) баг Васы (06.11): задание со сдачей у Васы подсвечивает только Васу. Рейн, ур. 145, 417 на стадии 1 / 0. Запуск на
			//   своей карте: `--map "/Game/Map/rein/rein?game=…" --pre "Asda.Load.CurrentMap 1"` — тогда сверяются и настоящие акторы.
			const bool bOnRein = LM && LM->CurrentMapId == 1;
			{
				IConsoleVariable* StageKey = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Quest.TakeCellByStage"));
				SetState(QM, 145, 0, { FIntPoint(417, 1) }, { 33 });
				const int32 V1 = QM->EvalMarkForNsr(TEXT("soldier13")), A1 = QM->EvalMarkForNsr(TEXT("37npc"));
				const int32 C1 = QM->EvalMarkForNsr(TEXT("49npc")), T1 = QM->EvalMarkForNsr(TEXT("touran"));
				Check(V1 == 2 && A1 == 0 && C1 == 0 && T1 != 2, FString::Printf(
					TEXT("(И) 417 стадия 1 (сдача у Васы): Васа %d (2), Аршес %d (0), кристалл 49 %d (0), Туран %d (не 2)"), V1, A1, C1, T1));
				if (StageKey)
				{
					StageKey->Set(0, ECVF_SetByConsole);
					const int32 T0 = QM->EvalMarkForNsr(TEXT("touran"));
					StageKey->Set(1, ECVF_SetByConsole);
					Check(T0 == 2, FString::Printf(TEXT("(И) ключ TakeCellByStage 0 — поведение оригинала: Туран %d (2)"), T0));
				}
				Key->Set(0, ECVF_SetByConsole);
				const int32 A0 = QM->EvalMarkForNsr(TEXT("37npc"));
				Key->Set(1, ECVF_SetByConsole);
				Check(A0 == 1, FString::Printf(TEXT("(И) ключ SkipMissingPage 0 — как до 04.11: Аршес %d (1, фантом #10)"), A0));
				SetState(QM, 145, 0, { FIntPoint(417, 0) }, { 33 });
				const int32 T2 = QM->EvalMarkForNsr(TEXT("touran")), V2 = QM->EvalMarkForNsr(TEXT("soldier13"));
				Check(T2 == 2 && V2 != 2, FString::Printf(TEXT("(И) 417 стадия 0 (шаг у Турана): Туран %d (2), Васа %d (не 2)"), T2, V2));
				// Все 7 ячеек без привязки к заданию: своя стадия — счёт сдачи 99, чужая — нет.
				struct FC { int32 Npc; int32 Q; int32 St; };
				const FC Cs[] = { {71,80,0}, {72,76,0}, {73,78,0}, {36,417,1}, {36,418,1}, {103,48,1}, {103,417,0} };
				for (const FC& C : Cs)
				{
					const FAsdaNpcQuestNpcRow* Row = nullptr;
					for (const FAsdaNpcQuestNpcRow* N : QM->NpcRows) { if (N && N->NpcId == C.Npc) { Row = N; } }
					if (!Row) { Check(false, FString::Printf(TEXT("(И) нет NPC %d"), C.Npc)); continue; }
					SetState(QM, 145, 0, { FIntPoint(C.Q, C.St) }, {});
					const int32 Own = QM->CollectDeliverableForNsr(Row->NsrName);
					SetState(QM, 145, 0, { FIntPoint(C.Q, C.St == 0 ? 1 : 0) }, {});
					const int32 Other = QM->CollectDeliverableForNsr(Row->NsrName);
					Check(Own == 99 && Other != 99, FString::Printf(TEXT("(И) ячейка NPC %d задание %d шаг %d: своя стадия %d (99), чужая %d (не 99)"),
						C.Npc, C.Q, C.St, Own, Other));
				}
				if (bOnRein)
				{
					SetState(QM, 145, 0, { FIntPoint(417, 1) }, { 33 });
					QM->RebuildMarks();
					TMap<int32, int32> Seen;
					for (TActorIterator<AAsdaNpcActor> AIt(W); AIt; ++AIt) { Seen.Add(AIt->NpcId, AIt->QuestMarkKind); }
					const int32* SV = Seen.Find(36); const int32* SA = Seen.Find(37); const int32* ST = Seen.Find(103);
					Check(SV && SA && ST && *SV == 2 && *SA == 0 && *ST != 2, FString::Printf(
						TEXT("(И) акторы Рейна: Васа %d, Аршес %d, Туран %d (2, 0, не 2)"), SV ? *SV : -1, SA ? *SA : -1, ST ? *ST : -1));
				}
			}
			if (!(Fh && LM && LM->CurrentMapId == Fh->MapId) && !bOnChmMap && !bOnRein)
			{
				Check(false, FString::Printf(TEXT("(Г/Ж) ни Фхашу, ни Хмеллы нет на текущей карте %d — проверка над головой не выполнена"),
					LM ? LM->CurrentMapId : -1));
			}

			// (Д) каждая из записей без страницы — в состоянии, где её условия сходятся: оба перебора её пропускают и совпадают;
			//   затем метка над головой (`RebuildMarks` → `GetMarkForNpcId`, карта NPC подставляется) у ВСЕХ NPC этих сценариев.
			{
				int32 Tried = 0, Hit = 0, Good = 0;
				TSet<FString> Affected;
				for (const TPair<FString, TArray<const FAsdaNpcQuestScriptRow*>>& It : QM->ScriptByNsr)
				{
					for (const FAsdaNpcQuestScriptRow* R : It.Value)
					{
						if (R->Opcode == 121 || R->Opcode == 122) { continue; }
						if (!AsdaNsrScript::ShouldSkipRecordWithoutPage(It.Key, R->PageId)) { continue; }
						Affected.Add(It.Key);
						if (R->Opcode == 123) { continue; }   // спец-запись: условие — ячейки, её покрывает (Е)
						++Tried;
						TArray<FIntPoint> Act; TArray<int32> Done;
						if (R->QuestActive >= 0) { Act.Add(FIntPoint(R->QuestActive, FMath::Max(R->QuestStage, 0))); }
						if (R->QuestDone >= 0) { Done.Add(R->QuestDone); }
						const int32 Lv = FMath::Max<int32>(R->LevelMin, 1);
						SetState(QM, Lv, R->ClassId >= 0 ? R->ClassId : 0, Act, Done);
						if (!QM->CheckRecord(*R)) { continue; }   // условие не выразилось подменой (режим класса и т. п.)
						++Hit;
						int32 Rec = -1;
						const int32 D = DialogPickMark(QM, It.Key, &Rec);
						const int32 M = QM->EvalMarkForNsr(It.Key);
						if (M == D && Rec != R->RecIndex) { ++Good; }
						else
						{
							UE_LOG(LogAsdaQuest, Log, TEXT("QMARK-PAGE (Д) расхождение: %s #%d — метка %d, разговор #%d → %d"),
								*It.Key, R->RecIndex, M, Rec, D);
						}
					}
				}
				Check(Hit > 0 && Good == Hit, FString::Printf(TEXT("(Д) записи без страницы в «своём» состоянии: подменой выражено %d из %d, ")
					TEXT("пропущены обоими переборами и метка = разговор у %d"), Hit, Tried, Good));

				const int32 MapWas = LM ? LM->CurrentMapId : -1;
				int32 Npcs = 0, NpcGood = 0;
				const int32 Lvs[] = { 1, 41, 100 };
				for (const FAsdaNpcQuestNpcRow* N : QM->NpcRows)
				{
					if (!N || !Affected.Contains(N->NsrName.ToLower()) || !LM) { continue; }
					if (N->NpcType != 0 && N->NpcType != 4) { continue; }   // в метки идут только типы 0/4 (тип 5 — доска)
					++Npcs;
					bool bAll = true;
					for (int32 Lv : Lvs)
					{
						SetState(QM, Lv, 0, {}, {});
						LM->CurrentMapId = N->MapId;
						QM->RebuildMarks();
						int32 Rec = -1;
						const int32 D = DialogPickMark(QM, N->NsrName, &Rec);
						const int32 Over = QM->GetMarkForNpcId(N->NpcId);
						if (Over != D)
						{
							bAll = false;
							UE_LOG(LogAsdaQuest, Log, TEXT("QMARK-PAGE (Д) NPC %d «%s» ур.%d: над головой %d, разговор #%d → %d"),
								N->NpcId, *N->NsrName, Lv, Over, Rec, D);
						}
					}
					if (bAll) { ++NpcGood; }
				}
				if (LM) { LM->CurrentMapId = MapWas; }
				Check(Npcs > 0 && NpcGood == Npcs, FString::Printf(TEXT("(Д) NPC сценариев без страниц (%d сценариев): метка над головой = выбор разговора у %d из %d NPC"),
					Affected.Num(), NpcGood, Npcs));
			}

			// (Е) строки облачка: страница своя / перенаправлена на страницу шага с тем же ключом / не показывается.
			{
				const UDataTable* SnqDt = AsdaDt::Cached(kSnqDT);
				TMap<int32, FString> NsrByNpc;
				for (const FAsdaNpcQuestNpcRow* N : QM->NpcRows) { if (N) { NsrByNpc.Add(N->NpcId, N->NsrName.ToLower()); } }
				int32 Cells = 0, Own = 0, Redirect = 0, Hidden = 0;
				FString RedirList, HiddenList;
				if (SnqDt)
				{
					for (const TPair<FName, uint8*>& It : SnqDt->GetRowMap())
					{
						const FAsdaNpcSnqRow* S = reinterpret_cast<const FAsdaNpcSnqRow*>(It.Value);
						const FString* Nsr = S ? NsrByNpc.Find(S->NpcId) : nullptr;
						if (!Nsr) { continue; }
						for (int32 Kind = 0; Kind < 2; ++Kind)
						{
							for (const FAsdaSnqEntry& E : (Kind == 0 ? S->Give : S->Take))
							{
								if (E.QuestId <= 0) { continue; }
								for (int32 K : E.Keys)
								{
									const FAsdaSnqIctRow* R = K > 0 ? AsdaDt::RowByInt<FAsdaSnqIctRow>(kSnqIctDT, K, TEXT("QMarkPage")) : nullptr;
									if (!R || R->Key < 0) { continue; }
									++Cells;
									const int32 T = AsdaNsrScript::ResolveCellTalkId(*Nsr, R->TalkId, R->Key);
									if (T == R->TalkId) { ++Own; }
									else if (T >= 0) { ++Redirect; RedirList += FString::Printf(TEXT(" %s:%d(%s)%d→%d"), **Nsr, E.QuestId, Kind ? TEXT("T") : TEXT("G"), R->TalkId, T); }
									else { ++Hidden; HiddenList += FString::Printf(TEXT(" %s:%d(%s)"), **Nsr, E.QuestId, Kind ? TEXT("T") : TEXT("G")); }
								}
							}
						}
					}
				}
				UE_LOG(LogAsdaQuest, Log, TEXT("QMARK-PAGE (Е) перенаправлены:%s"), *RedirList);
				UE_LOG(LogAsdaQuest, Log, TEXT("QMARK-PAGE (Е) скрыты:%s"), *HiddenList);
				// ★ tomb1 (NPC 64) возвращён из донора (`Tools/asda_nsr_donor_restore.py`): его 3 ячейки ведут на свои страницы 10/11/12.
				Check(Cells == 887 && Own == 875 && Redirect == 12 && Hidden == 0,
					FString::Printf(TEXT("(Е) строки облачка: всего %d (ждали 887), своя страница %d (875), перенаправлено %d (12), скрыто %d (0)"),
						Cells, Own, Redirect, Hidden));
			}

			// (З) Могила лишённого гордости (tomb1, NPC 64) — сценарий из донора: разговор выбирает квестовые записи по шагам
			//   заданий 127/128/129 (уровни приведены к серверу: 33+), страницы этих записей есть.
			{
				struct FT { const TCHAR* Name; TArray<FIntPoint> Act; TArray<int32> Done; int32 WantRec; int32 WantPage; };
				TArray<FT> Ts;
				Ts.Add({ TEXT("127 в работе (стадия 0) → #10, стр. 11 (сдача 127)"), { FIntPoint(127, 0) }, {}, 10, 11 });
				Ts.Add({ TEXT("128 в работе (стадия 1) → #9, стр. 10 (сдача 128)"), { FIntPoint(128, 1) }, { 127 }, 9, 10 });
				Ts.Add({ TEXT("128 сдан, 129 не взят → #11, стр. 12 (выдача 129)"), {}, { 127, 128 }, 11, 12 });
				for (const FT& T : Ts)
				{
					SetState(QM, 40, 0, T.Act, T.Done);
					int32 Rec = -1;
					DialogPickMark(QM, TEXT("tomb1"), &Rec);
					const TArray<const FAsdaNpcQuestScriptRow*>* Recs = QM->ScriptByNsr.Find(TEXT("tomb1"));
					int32 Page = -1;
					if (Recs) { for (const FAsdaNpcQuestScriptRow* R : *Recs) { if (R->RecIndex == Rec) { Page = R->PageId; } } }
					Check(Rec == T.WantRec && Page == T.WantPage && !AsdaNsrScript::ShouldSkipRecordWithoutPage(TEXT("tomb1"), Page),
						FString::Printf(TEXT("(З) tomb1 ур.40, %s: запись #%d, страница %d"), T.Name, Rec, Page));
				}
			}

			GAsdaFakeSelfLevel = GAsdaFakeSelfClass = GAsdaFakeSelfSub = -1;
			Key->Set(KeyWas, ECVF_SetByConsole);
		}
		UE_LOG(LogAsdaQuest, Log, TEXT("QMARK-PAGE ИТОГ: ok=%d fail=%d"), Ok, Fail);
	}
};
int32 FAsdaQuestMarkPageSelfTest::Ok = 0;
int32 FAsdaQuestMarkPageSelfTest::Fail = 0;

static FAutoConsoleCommandWithWorldAndArgs GAsdaQuestMarkPageSelfTest(
	TEXT("asda.SelfTest.QuestMarkPage"),
	TEXT("Самотест: квестовая метка и разговор пропускают одни и те же записи .NSR без страницы (Хмелла, все сценарии, Фхашу). Только офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.SelfTest.QuestMarkPage"))) { return; }
		// ★ 11.11.2026 (D4b): самотест сверяет метки над головой на текущей карте, а первым в игре шёл при карте −1 (офлайн её
		//   никто не назвал) — провал «ни Фхашу, ни Хмеллы нет на текущей карте -1». Карта называется так же, как у соседних
		//   самотестов (`Asda.Load.CurrentMap 3` — Альпия, на ней стоит Фхашу); названную прежде карту не трогаем.
		if (GEngine && W && W->GetGameInstance())
		{
			const UAsdaLoadManagerSubsystem* PageLm = W->GetGameInstance()->GetSubsystem<UAsdaLoadManagerSubsystem>();
			if (PageLm && PageLm->CurrentMapId < 0) { GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3")); }
		}
		FAsdaQuestMarkPageSelfTest::Run(W);
		// Пакет самотестов (`asda.SelfTest.Batch`) добавляет `quit` и узнаёт по нему конец самотеста.
		for (const FString& A : Args)
		{
			if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase) && GEngine) { GEngine->Exec(W, TEXT("quit")); break; }
		}
	}));
#endif // ASDA_DEV_CONSOLE

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  САМОТЕСТ `asda.SelfTest.QuestMarkArshes [quit]` (08.11.2026, пачка 15 баг 7: «у Аршеса на карте горит иконка миссии»)
// ════════════════════════════════════════════════════════════════════════════════════════════════
// Путь игрока: карта Рейн (1) с настоящими акторами NPC, открытая карта мира, состояние персонажа — штатный `ApplyQuestStateBlob`
//   и офлайн-подмена уровня, метки — штатный `RebuildMarks` (он же раздаёт мини-карте, карте мира и акторам). По каждому
//   состоянию: все отметки карты (метка задания 1/2, цель задания, зона рыбалки) в радиусе 8 клеток от Аршеса (NPC 37,
//   клетка 74,372), метка над актором Аршеса и кадр экрана в `Reports/questmark_2026-11-08/frames/<язык>_<состояние>.png`.
//   Ожидание: при ключе `asda.Npc.SkipMissingPage 1` у Аршеса нет ни метки задания, ни цели задания ни в одном состоянии;
//   контроль — ур. 145 при ключе 0 (поведение до 04.11) даёт жёлтую метку Аршеса, значит тест её видит. Запуск:
//   `--map "/Game/Map/rein/rein?game=/Script/AsdaReborn.AsdaWorldGameMode" --pre "Asda.Load.CurrentMap 1"`.
#if ASDA_DEV_CONSOLE
struct FAsdaQuestMarkArshesSelfTest
{
	struct FState { FString Name; int32 Lv; TArray<FIntPoint> Act; TArray<int32> Done; int32 Key; bool bExpectArshesMark; };
	static TArray<FState> States;    // состояния по порядку
	static int32 Idx;                // текущее состояние
	static int32 Phase;              // 0 — выставить состояние, 1 — снять отметки и кадр
	static int32 Ok, Fail;
	static bool bQuit;
	static TWeakObjectPtr<UWorld> World;
	static FTSTicker::FDelegateHandle Tick;
	static double NextAt;

	static void Check(bool bOk, const FString& What)
	{
		bOk ? ++Ok : ++Fail;
		UE_LOG(LogAsdaQuest, Log, TEXT("QMARK-ARSHES %s %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
	}

	static void Start(UWorld* W, bool bInQuit)
	{
		World = W; bQuit = bInQuit; Ok = Fail = 0; Idx = 0; Phase = 0; NextAt = FPlatformTime::Seconds() + 2.0;
		const TArray<int32> Vasa1 = { 33, 413, 414, 415, 416 };                 // задания Васы ур. 17
		const TArray<int32> Vasa2 = { 33, 413, 414, 415, 416, 419, 421, 422 };  // и ур. 19 (422 — последнее)
		States.Reset();
		States.Add({ TEXT("lv26_none"), 26, {}, {}, 1, false });
		States.Add({ TEXT("lv26_vasa_413_st2"), 26, { FIntPoint(413, 2) }, { 33 }, 1, false });
		States.Add({ TEXT("lv26_vasa_done"), 26, {}, Vasa1, 1, false });
		States.Add({ TEXT("lv40_vasa_421_st1"), 40, { FIntPoint(421, 1) }, { 33, 413, 414, 415, 416, 419 }, 1, false });
		States.Add({ TEXT("lv40_vasa_done"), 40, {}, Vasa2, 1, false });
		States.Add({ TEXT("lv145_vasa_done"), 145, {}, Vasa2, 1, false });
		States.Add({ TEXT("lv145_vasa_done_key0"), 145, {}, Vasa2, 0, true });   // контроль: поведение до 04.11
		// Свой персонаж рядом с Аршесом (клетка 76,374): в кадр попадают Аршес с меткой над головой и мини-карта.
		if (GEngine && W) { GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 76 374 QMarkArshes")); }
		NextAt = FPlatformTime::Seconds() + 5.0;
		Tick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&FAsdaQuestMarkArshesSelfTest::Step), 0.1f);
	}

	static bool Step(float)
	{
		UWorld* W = World.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
		UAsdaMinimapSubsystem* MM = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
		IConsoleVariable* Key = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Npc.SkipMissingPage"));
		if (!QM || !MM || !Key) { Check(false, TEXT("нет подсистемы меток/карты или ключа")); return Finish(); }
		if (FPlatformTime::Seconds() < NextAt) { return true; }
		if (Idx >= States.Num()) { return Finish(); }
		const FState& S = States[Idx];
		if (Phase == 0)
		{
			Key->Set(S.Key, ECVF_SetByConsole);
			GAsdaFakeSelfLevel = S.Lv; GAsdaFakeSelfClass = 0; GAsdaFakeSelfSub = 0;
			QM->ApplyQuestStateBlob(FAsdaQuestMarkPageSelfTest::Blob(S.Act, S.Done));
			if (MM->IsBigMapOpen()) { MM->ToggleBigMap(); }
			QM->RebuildMarks();
			Phase = 1; NextAt = FPlatformTime::Seconds() + 1.5;
			return true;
		}
		if (Phase == 1)
		{
			Shot(S.Name + TEXT("_scene"));   // кадр сцены: Аршес и мини-карта
			if (!MM->IsBigMapOpen()) { MM->ToggleBigMap(); }
			QM->RebuildMarks();   // уже с открытой картой мира (значки целей строятся для показанной карты)
			Phase = 2; NextAt = FPlatformTime::Seconds() + 2.0;
			return true;
		}
		// Отметки всех видов в радиусе 8 клеток от Аршеса.
		const FIntPoint A(74, 372);
		FString Near;
		int32 QuestNear = 0;
		const EAsdaMapMark Kinds[] = { EAsdaMapMark::Quest, EAsdaMapMark::Quest2, EAsdaMapMark::QuestTarget, EAsdaMapMark::Fishing };
		for (EAsdaMapMark K : Kinds)
		{
			if (const TArray<FAsdaMapMark>* L = MM->FindMarks(K))
			{
				for (const FAsdaMapMark& M : *L)
				{
					if (M.MapId != 1 || FMath::Abs(M.TileX - A.X) > 8 || FMath::Abs(M.TileY - A.Y) > 8) { continue; }
					Near += FString::Printf(TEXT(" %s(%d,%d)"), *UEnum::GetValueAsString(K), M.TileX, M.TileY);
					if (K != EAsdaMapMark::Fishing) { ++QuestNear; }
				}
			}
		}
		int32 Over = -1;
		for (TActorIterator<AAsdaNpcActor> It(W); It; ++It) { if (It->NpcId == 37) { Over = It->QuestMarkKind; } }
		const int32 Mk = QM->GetMarkForNpcId(37);
		if (S.bExpectArshesMark)
		{
			Check(Mk == 1 && Over == 1 && QuestNear > 0, FString::Printf(TEXT("[%s] контроль ключа 0: Аршес метка %d, над головой %d, отметок у Аршеса %d —%s"),
				*S.Name, Mk, Over, QuestNear, *Near));
		}
		else
		{
			Check(Mk == 0 && Over == 0 && QuestNear == 0, FString::Printf(TEXT("[%s] Аршес метка %d, над головой %d, отметок задания у Аршеса %d (ждали 0) —%s"),
				*S.Name, Mk, Over, QuestNear, Near.IsEmpty() ? TEXT(" нет") : *Near));
		}
		Shot(S.Name + TEXT("_map"));      // кадр карты мира
		++Idx; Phase = 0; NextAt = FPlatformTime::Seconds() + 1.5;
		return true;
	}

	static void Shot(const FString& Name)
	{
		FString Lang = TEXT("ru");
		FParse::Value(FCommandLine::Get(), TEXT("AsdaLang="), Lang);
		const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/questmark_2026-11-08/frames")));
		IFileManager::Get().MakeDirectory(*Dir, true);
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString::Printf(TEXT("%s_%s.png"), *Lang, *Name)), /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
	}

	static bool Finish()
	{
		if (IConsoleVariable* Key = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Npc.SkipMissingPage"))) { Key->Set(1, ECVF_SetByConsole); }
		GAsdaFakeSelfLevel = GAsdaFakeSelfClass = GAsdaFakeSelfSub = -1;
		UE_LOG(LogAsdaQuest, Log, TEXT("QMARK-ARSHES ИТОГ: ok=%d fail=%d"), Ok, Fail);
		FTSTicker::GetCoreTicker().RemoveTicker(Tick);
		if (bQuit && GEngine && World.IsValid()) { GEngine->Exec(World.Get(), TEXT("quit")); }
		return false;
	}
};
TArray<FAsdaQuestMarkArshesSelfTest::FState> FAsdaQuestMarkArshesSelfTest::States;
int32 FAsdaQuestMarkArshesSelfTest::Idx = 0;
int32 FAsdaQuestMarkArshesSelfTest::Phase = 0;
int32 FAsdaQuestMarkArshesSelfTest::Ok = 0;
int32 FAsdaQuestMarkArshesSelfTest::Fail = 0;
bool FAsdaQuestMarkArshesSelfTest::bQuit = false;
TWeakObjectPtr<UWorld> FAsdaQuestMarkArshesSelfTest::World;
FTSTicker::FDelegateHandle FAsdaQuestMarkArshesSelfTest::Tick;
double FAsdaQuestMarkArshesSelfTest::NextAt = 0.0;

static FAutoConsoleCommandWithWorldAndArgs GAsdaQuestMarkArshesSelfTest(
	TEXT("asda.SelfTest.QuestMarkArshes"),
	TEXT("Самотест (пачка 15, баг 7): у Аршеса на Рейне нет метки задания ни над головой, ни на карте мира, при ур. 26/40/145 и заданиях Васы; кадры в Reports. Только офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.SelfTest.QuestMarkArshes"))) { return; }
		bool bQ = false;
		for (const FString& A : Args) { if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQ = true; } }
		FAsdaQuestMarkArshesSelfTest::Start(W, bQ);
	}));
#endif // ASDA_DEV_CONSOLE

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  САМОТЕСТ `asda.SelfTest.QuestMarkMelve [quit]` (08.11.2026, пачка 19 баг 5: «у Мелве по Defeated Soldiers красная метка»)
// ════════════════════════════════════════════════════════════════════════════════════════════════
// Оригинал (`questFindMatchingActionMode1@0xD8AB90`): метка NPC = метка ПЕРВОЙ подошедшей записи .NSR по порядку. Задание 105
//   «Defeated Soldiers» выдаёт Джоэл (NPC 18, joel #16, жёлтая), по пути надо поговорить с Вуди (woody #8) и Мелве (melve #10):
//   у обоих запись «105 на стадии 0, своей цели (21294 / 21295) в слоте журнала НЕТ» с меткой 2 (красная), сдача — снова
//   Джоэлу (joel #9, обе цели в слоте, красная). Красная у Мелве при взятом 105 — данные оригинала. Тест проверяет на Силарисе
//   (настоящие акторы, штатные `ApplyQuestStateBlob` / `RebuildMarks`), что порт ставит ровно эти метки, и снимает кадры в
//   `Reports/batch19/quest_mark/frames/<язык>_<состояние>_{scene,map}.png`. Запуск:
//   `--map "/Game/Map/silalis/silalis?game=/Script/AsdaReborn.AsdaWorldGameMode" --pre "Asda.Load.CurrentMap 0"`.
#if ASDA_DEV_CONSOLE
struct FAsdaQuestMarkMelveSelfTest
{
	struct FObj { int32 Id; int32 Cnt; };
	// Состояние: уровень, задание 105 взято или нет, цели в слоте 105, сданные задания, ожидаемые метки Мелве (26) и Джоэла (18).
	struct FState { FString Name; int32 Lv; bool b105; TArray<FObj> Objs; TArray<int32> Done; int32 WantMelve; int32 WantJoel; };
	static TArray<FState> States;    // состояния по порядку
	static int32 Idx;                // текущее состояние
	static int32 Phase;              // 0 — выставить состояние, 1 — кадр сцены и карта мира, 2 — проверка и кадр карты
	static int32 Ok, Fail;
	static bool bQuit;
	static TWeakObjectPtr<UWorld> World;
	static FTSTicker::FDelegateHandle Tick;
	static double NextAt;

	static void Check(bool bOk, const FString& What)
	{
		bOk ? ++Ok : ++Fail;
		UE_LOG(LogAsdaQuest, Log, TEXT("QMARK-MELVE %s %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
	}

	// Пакет 5047 (688 б), как `FAsdaQuestMarkPageSelfTest::Blob`, плюс цели слота 105 (`+14 + 6j` i32 id, `+18 + 6j` i16 счётчик).
	static TArray<uint8> Blob(const FState& S)
	{
		TArray<uint8> B = FAsdaQuestMarkPageSelfTest::Blob(S.b105 ? TArray<FIntPoint>{ FIntPoint(105, 0) } : TArray<FIntPoint>{}, S.Done);
		for (int32 j = 0; S.b105 && j < S.Objs.Num() && j < 5; ++j)
		{
			const int32 O = 14 + 6 * j;
			for (int32 k = 0; k < 4; ++k) { B[O + k] = (S.Objs[j].Id >> (8 * k)) & 0xFF; }
			B[O + 4] = S.Objs[j].Cnt & 0xFF; B[O + 5] = (S.Objs[j].Cnt >> 8) & 0xFF;
		}
		return B;
	}

	static void Shot(const FString& Name)
	{
		FString Lang = TEXT("ru");
		FParse::Value(FCommandLine::Get(), TEXT("AsdaLang="), Lang);
		const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch19/quest_mark/frames")));
		IFileManager::Get().MakeDirectory(*Dir, true);
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString::Printf(TEXT("%s_%s.png"), *Lang, *Name)), /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
	}

	static int32 OverheadOf(UWorld* W, int32 NpcId)
	{
		for (TActorIterator<AAsdaNpcActor> It(W); It; ++It) { if (It->NpcId == NpcId) { return It->QuestMarkKind; } }
		return -1;
	}

	static void Start(UWorld* W, bool bInQuit)
	{
		World = W; bQuit = bInQuit; Ok = Fail = 0; Idx = 0; Phase = 0; NextAt = FPlatformTime::Seconds() + 5.0;
		const TArray<int32> Others = { 267, 304, 109, 110, 113, 144, 222, 231, 232, 233, 309, 310 };   // прочие задания Мелве — сданы
		States.Reset();
		States.Add({ TEXT("lv30_none"),            30, false, {}, {}, 1, 1 });                                   // Мелве: 267 (#17), Джоэл: 105 (#16)
		States.Add({ TEXT("lv30_105_taken"),       30, true,  {}, {}, 2, -1 });                                  // Мелве #10 раньше #17 → красная
		States.Add({ TEXT("lv30_105_melve_done"),  30, true,  { { 21295, 1 } }, {}, 1, -1 });                     // поговорил с Мелве → его #10 не подходит, снова #17
		States.Add({ TEXT("lv30_others_done_105"), 30, true,  {}, Others, 2, -1 });                               // кроме 105 у Мелве ничего
		States.Add({ TEXT("lv30_others_done_105_both"), 30, true, { { 21294, 1 }, { 21295, 1 } }, Others, 0, 2 }); // обе цели → сдача Джоэлу
		States.Add({ TEXT("lv30_others_done_105_done"), 30, false, {}, [&Others]() { TArray<int32> D = Others; D.Add(105); return D; }(), 0, -1 });
		GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 345 410 QMarkMelve"));
		Tick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&FAsdaQuestMarkMelveSelfTest::Step), 0.1f);
	}

	static bool Step(float)
	{
		UWorld* W = World.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
		UAsdaMinimapSubsystem* MM = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
		if (!QM || !MM) { Check(false, TEXT("нет подсистемы меток или карты")); return Finish(); }
		if (FPlatformTime::Seconds() < NextAt) { return true; }
		if (Idx >= States.Num()) { return Finish(); }
		const FState& S = States[Idx];
		if (Phase == 0)
		{
			GAsdaFakeSelfLevel = S.Lv; GAsdaFakeSelfClass = 0; GAsdaFakeSelfSub = 0;
			if (MM->IsBigMapOpen()) { MM->ToggleBigMap(); }
			QM->ApplyQuestStateBlob(Blob(S));
			QM->RebuildMarks();
			Phase = 1; NextAt = FPlatformTime::Seconds() + 1.5;
			return true;
		}
		if (Phase == 1)
		{
			Shot(S.Name + TEXT("_scene"));
			if (!MM->IsBigMapOpen()) { MM->ToggleBigMap(); }
			QM->RebuildMarks();
			Phase = 2; NextAt = FPlatformTime::Seconds() + 1.5;
			return true;
		}
		const int32 Melve = QM->GetMarkForNpcId(26), MelveOver = OverheadOf(W, 26);
		const int32 Joel = QM->GetMarkForNpcId(18);
		Check(Melve == S.WantMelve && MelveOver == S.WantMelve, FString::Printf(TEXT("[%s] Мелве (26): метка %d, над головой %d, ждали %d"),
			*S.Name, Melve, MelveOver, S.WantMelve));
		if (S.WantJoel >= 0)
		{
			Check(Joel == S.WantJoel, FString::Printf(TEXT("[%s] Джоэл (18): метка %d, ждали %d"), *S.Name, Joel, S.WantJoel));
		}
		else
		{
			UE_LOG(LogAsdaQuest, Log, TEXT("QMARK-MELVE info [%s] Джоэл (18): метка %d (не утверждается в этом состоянии)"), *S.Name, Joel);
		}
		Shot(S.Name + TEXT("_map"));
		++Idx; Phase = 0; NextAt = FPlatformTime::Seconds() + 1.5;
		return true;
	}

	static bool Finish()
	{
		GAsdaFakeSelfLevel = GAsdaFakeSelfClass = GAsdaFakeSelfSub = -1;
		UE_LOG(LogAsdaQuest, Log, TEXT("QMARK-MELVE ИТОГ: ok=%d fail=%d"), Ok, Fail);
		FTSTicker::GetCoreTicker().RemoveTicker(Tick);
		if (bQuit && GEngine && World.IsValid()) { GEngine->Exec(World.Get(), TEXT("quit")); }
		return false;
	}
};
TArray<FAsdaQuestMarkMelveSelfTest::FState> FAsdaQuestMarkMelveSelfTest::States;
int32 FAsdaQuestMarkMelveSelfTest::Idx = 0;
int32 FAsdaQuestMarkMelveSelfTest::Phase = 0;
int32 FAsdaQuestMarkMelveSelfTest::Ok = 0;
int32 FAsdaQuestMarkMelveSelfTest::Fail = 0;
bool FAsdaQuestMarkMelveSelfTest::bQuit = false;
TWeakObjectPtr<UWorld> FAsdaQuestMarkMelveSelfTest::World;
FTSTicker::FDelegateHandle FAsdaQuestMarkMelveSelfTest::Tick;
double FAsdaQuestMarkMelveSelfTest::NextAt = 0.0;

static FAutoConsoleCommandWithWorldAndArgs GAsdaQuestMarkMelveSelfTest(
	TEXT("asda.SelfTest.QuestMarkMelve"),
	TEXT("Самотест (пачка 19, баг 5): метки Мелве (26) и Джоэла (18) по заданию 105 «Defeated Soldiers» на Силарисе — как в данных оригинала; кадры в Reports. Только офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.SelfTest.QuestMarkMelve"))) { return; }
		bool bQ = false;
		for (const FString& A : Args) { if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQ = true; } }
		FAsdaQuestMarkMelveSelfTest::Start(W, bQ);
	}));
#endif // ASDA_DEV_CONSOLE
