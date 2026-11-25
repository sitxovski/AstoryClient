// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ЦЕЛЬ-МОБ ГИДА ЗАДАНИЯ» (25.10.2026): `asda.SelfTest.QuestGuideMobNames [quit]`.
//   Баг игроков: «квест засчитывает не тех мобов» — журнал 700 / трекер 1800 называли моба соседней строки таблицы
//   (`QDT.BIN +8` — индекс строки `Mob list.BIN`, порт вычитал единицу), а гид переноса Флэммио указывал мобов в
//   нумерации Global. Разбор: `Reports/quest_mobs/QUEST_MOB_AUDIT.md`; сверка данных с сервером и спауном —
//   `Tools/asda_quest_mob_audit.py`.
//   Проверки (реальный путь — `UAsdaQuestLogSubsystem::BuildRoutesFor`, тот, что пишет строки «=> Цель - Карта»):
//     1) ВСЕ строки `DT_QuestGuide` с целью-мобом: имя = строка `TargetId` таблицы `DT_MobNames` на языке сессии
//        (оракул — строка таблицы и оверлей `MobNames` напрямую, без `AsdaEntityText`);
//     2) якоря из жалоб игроков и из ведомости исправлений гида — точные имена RU/EN и координаты;
//     3) ключ `asda.Quest.GuideMobRow 0` возвращает прежнее поведение (строка TargetId − 1);
//     4) (25.10.2026, баг «2/0 вместо 2/2») экранная строка прогресса: тело 5052 квеста 424 через `OnQuestProgress`
//        (вход сети) — промежуточный моб «1/2 », последний (стадия выросла) «2/2» без пробела, ключ
//        `asda.Quest.ProgressNeedPrevStage 0` — прежнее «2/0 »; ремесленное 20040 — «3/10»;
//     5) (25.10.2026, решение владельца) тексты стадий и описания Флэммио 566–626 называют моба цели (= моба сервера) на
//        языке сессии, имён мобов Global в них нет; ни у одного названия задания (книги, эпизоды) нет пробела у края;
//        название 424 без ведущего пробела.
//   Офлайн (-AsdaNoNet), карта любая (нужен GameInstance). Итог: «ГИД-МОБ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaDataTableCache.h"            // AsdaDt::Cached — таблицы гида и имён мобов
#include "AsdaNetSubsystem.h"              // OnQuestProgress — тот же вход 5052, что у сети (реальный путь строки прогресса)
#include "Items/AsdaItemTableSubsystem.h"  // GetItemName — имя предмета-цели в ожидаемой строке
#include "Quest/AsdaQuestMarkerSubsystem.h" // ApplyQuestProgressBlob / GetSlots — исходное состояние слота
#include "Misc/OutputDevice.h"             // перехват строки «прогресс на экран» из журнала
#include "Misc/ScopeLock.h"                // FScopeLock — замок массива пойманных строк (Serialize из любого потока)
#include "Npc/AsdaNpcDialogTypes.h"        // FAsdaEpisodeRow — названия эпизодов (часть 5)
#include "Text/AsdaLangRows.h"             // AsdaLangRows::Text — поле строки на языке сессии (как журнал)
#include "Internationalization/Regex.h"    // FRegexPattern — имена мобов Global в текстах (часть 5)
#include "Quest/AsdaNsrScriptPage.h"       // StartClosed — закрытая выдача заданий
#include "Quest/AsdaQuestLogSubsystem.h"   // BuildRoutesFor — реальный путь строк маршрута журнала
#include "Quest/AsdaQuestTypes.h"          // FAsdaQuestGuideRow
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"                 // язык сессии и оверлей MobNames (оракул)
#include "World/AsdaEntityText.h"          // QuestTargetName — общая точка имени цели (журнал и трекер)
#include "World/AsdaWorldSubsystem.h"      // FAsdaEntityNameRow — строка DT_MobNames
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaGuideMobTest, Log, All);

namespace AsdaQuestGuideMobSelfTest
{
	// Имя команды (строка итога, реестр ключей).
	static const TCHAR* kCmd = TEXT("asda.SelfTest.QuestGuideMobNames");
	static const TCHAR* kGuideDT = TEXT("/Game/AsdaAssets/Data/Quest/DT_QuestGuide.DT_QuestGuide");

	// Счёт проверок прогона.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;
	};

	// Записать результат проверки.
	static void Expect(FRun& R, bool b, const FString& What)
	{
		if (b) { ++R.Ok; UE_LOG(LogAsdaGuideMobTest, Log, TEXT("ГИД-МОБ ok   %s"), *What); }
		else { ++R.Fail; UE_LOG(LogAsdaGuideMobTest, Error, TEXT("ГИД-МОБ FAIL %s"), *What); }
	}

	// Оракул: имя строки `Row` таблицы мобов на языке сессии — сама строка (RU) или оверлей `MobNames` `<Row>.Name`.
	static FString OracleMobName(const UDataTable* Names, int32 Row)
	{
		const FAsdaEntityNameRow* R = Names ? Names->FindRow<FAsdaEntityNameRow>(FName(*FString::FromInt(Row)), TEXT("GuideMobTest"), false) : nullptr;
		if (!R) { return FString(); }
		FString En;
		if (!FAsdaLang::IsRu() && FAsdaLang::FindOverlay(FAsdaLang::Current(), TEXT("MobNames"), FString::Printf(TEXT("%d.Name"), Row), En))
		{
			return En;
		}
		return R->Name;
	}

	// Якорь: строка маршрута задания (QuestNo, Stage) с номером Line содержит имя Ru/En и (если X ≥ 0) координаты.
	static void Anchor(FRun& R, const UAsdaQuestLogSubsystem* QL, int32 QuestNo, int32 Stage, int32 Line,
		const TCHAR* Ru, const TCHAR* En, int32 MapId, int32 X, int32 Y, const TCHAR* Why)
	{
		const TArray<FAsdaQuestRoute> Routes = QL->BuildRoutesFor(QuestNo, Stage);
		const FString Want = FAsdaLang::IsRu() ? FString(Ru) : FString(En);
		const bool bHas = Routes.IsValidIndex(Line);
		const FString Text = bHas ? Routes[Line].Text : FString();
		const FString Head = FString::Printf(TEXT("=> %s - "), *Want);
		bool bOk = bHas && Text.StartsWith(Head) && Routes[Line].MapId == MapId;
		if (bOk && X >= 0) { bOk = Routes[Line].X == X && Routes[Line].Y == Y; }
		Expect(R, bOk, FString::Printf(TEXT("квест %d ст.%d строка %d: «%s» (ждали «%s…», карта %d, x %d, y %d) — %s"),
			QuestNo, Stage, Line, *Text, *Head, MapId, X, Y, Why));
	}

	// ── Часть 4 (25.10.2026, баг «2/0 вместо 2/2» у последнего моба) ─────────────────────────────────────────────
	// Перехват строки экранного прогресса: журнал пишет её в лог «[квест] прогресс на экран: «…»» тем же вызовом,
	//   что отдаёт в окно 221.
	// ★ 05.11.2026 (плавающий FAIL части 4: «» при строке в логе): в UE 5.8 FOutputDeviceRedirector кладёт устройство
	//   в НЕбуферизованный (синхронный) список только при CanBeUsedOnMultipleThreads() == true; с одним
	//   CanBeUsedOnAnyThread оно попадало в буферизованный — если поток логов в этот момент держал главную блокировку,
	//   строка уходила в очередь и доставлялась уже после RemoveOutputDevice. Теперь устройство синхронное всегда;
	//   строки других потоков приходят тоже — массив под замком.
	struct FProgressLineCap : public FOutputDevice
	{
		TArray<FString> Lines;   // текст между « и » каждой пойманной строки
		FCriticalSection Guard;  // Serialize зовётся из любого потока, пишущего в лог
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }  // ← НЕбуферизованный список
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName&) override
		{
			const FString S(V);
			const int32 P = S.Find(TEXT("прогресс на экран: «"));
			if (P == INDEX_NONE) { return; }
			const int32 B = P + FCString::Strlen(TEXT("прогресс на экран: «"));
			const int32 E = S.Find(TEXT("»"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (E > B) { FScopeLock Lock(&Guard); Lines.Add(S.Mid(B, E - B)); }
		}
	};

	// Тело 5052 (508 б, раскладка эталона `questItemResult_apply`): ветка ОБНОВЛЕНИЯ слота `Slot`, задание `QuestNo`
	//   (эпизод `EpisodeId`), стадия `Stage`, одна цель `Item` × `Count`.
	static TArray<uint8> Body5052(int32 Slot, int32 EpisodeId, int32 QuestNo, int32 Stage, int32 Item, int32 Count)
	{
		TArray<uint8> B; B.SetNumZeroed(508);
		auto W32 = [&B](int32 O, int32 V) { B[O] = V & 0xFF; B[O + 1] = (V >> 8) & 0xFF; B[O + 2] = (V >> 16) & 0xFF; B[O + 3] = (V >> 24) & 0xFF; };
		auto W16 = [&B](int32 O, int32 V) { B[O] = V & 0xFF; B[O + 1] = (V >> 8) & 0xFF; };
		W16(0, EpisodeId); B[4] = 2; W32(13, 0);
		W32(22 + 0, EpisodeId); B[22 + 4] = 4; W16(22 + 5, Slot); B[22 + 7] = 2;
		W16(22 + 8, QuestNo); W16(22 + 10, Stage); W32(22 + 14, Item); W16(22 + 18, Count);
		return B;
	}

	// Разослать тело через `OnQuestProgress` (как сеть) и вернуть пойманную строку экрана (пусто — не было).
	static FString BroadcastAndCatch(UAsdaNetSubsystem* Net, const TArray<uint8>& Body)
	{
		FProgressLineCap Cap;
		GLog->AddOutputDevice(&Cap);
		Net->OnQuestProgress.Broadcast(Body);
		GLog->RemoveOutputDevice(&Cap);  // под write-замком редиректора: после него Serialize в Cap не зайдёт
		FScopeLock Lock(&Cap.Guard);
		return Cap.Lines.Num() ? Cap.Lines.Last() : FString();
	}

	static void RunProgressCounter(FRun& R, UGameInstance* GI, FAsdaCVarScope& Keys)
	{
		UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
		UAsdaQuestMarkerSubsystem* QM = GI->GetSubsystem<UAsdaQuestMarkerSubsystem>();
		UAsdaItemTableSubsystem* Items = GI->GetSubsystem<UAsdaItemTableSubsystem>();
		if (!Net || !QM || !Items || !Net->OnQuestProgress.IsBound())
		{
			Expect(R, false, TEXT("4: нет сети/квестов/таблицы предметов или журнал не подписан на 5052"));
			return;
		}
		// Слот 11 — сохранить и вернуть после проверки (офлайн он пуст, но восстанавливаем честно).
		const int32 Slot = 11;
		const FAsdaQuestSlot Saved = QM->GetSlots().IsValidIndex(Slot) ? QM->GetSlots()[Slot] : FAsdaQuestSlot();
		// Квест 424 «Самый верный друг / A Man's Best Friend» (эпизод 2423): шаг 0 = SMInfo 238 — предмет 31161 × 2,
		//   шаг 1 — под-записи нет (−1). Скрин владельца: «< A Man's Best Friend > Lost Young Wolf : 2/0».
		const int32 Ep = 2423, Q = 424, Item = 31161;
		const FString ItemName = Items->GetItemName(Item);
		const FString TitleWant = FAsdaLang::IsRu() ? TEXT("Самый верный друг") : TEXT("Best Friend");
		QM->ApplyQuestProgressBlob(Body5052(Slot, Ep, Q, 0, Item, 0));                      // исходное: 0 из 2, стадия 0
		const FString L1 = BroadcastAndCatch(Net, Body5052(Slot, Ep, Q, 0, Item, 1));        // 1 из 2, стадия та же
		Expect(R, L1.Contains(TitleWant) && L1.EndsWith(FString::Printf(TEXT("%s : 1/2 "), *ItemName)),
			FString::Printf(TEXT("4: промежуточный моб — «%s» (стадия не выросла: «надо» текущей стадии, хвостовой пробел)"), *L1));
		const FString L2 = BroadcastAndCatch(Net, Body5052(Slot, Ep, Q, 1, Item, 2));        // последний: стадия 0 → 1
		Expect(R, L2.Contains(TitleWant) && L2.EndsWith(FString::Printf(TEXT("%s : 2/2"), *ItemName)) && !L2.Contains(TEXT("/0")),
			FString::Printf(TEXT("4: последний моб — «%s» (стадия выросла: «надо» прежней стадии, без хвостового пробела, не «2/0»)"), *L2));
		// Ключ 0 — прежнее поведение порта («2/0 »).
		Keys.Set(TEXT("asda.Quest.ProgressNeedPrevStage"), 0);
		QM->ApplyQuestProgressBlob(Body5052(Slot, Ep, Q, 0, Item, 1));
		const FString L3 = BroadcastAndCatch(Net, Body5052(Slot, Ep, Q, 1, Item, 2));
		Expect(R, L3.EndsWith(TEXT(" : 2/0 ")), FString::Printf(TEXT("4: ключ 0 — прежнее «%s»"), *L3));
		Keys.Set(TEXT("asda.Quest.ProgressNeedPrevStage"), 1);
		// Ремесленное 20040 (Raid_quest_data: 31233 × 10, Mode 0): «надо» из таблицы ремесла, без хвостового пробела.
		QM->ApplyQuestProgressBlob(Body5052(Slot, 2001, 20040, 0, 31233, 0));
		const FString L4 = BroadcastAndCatch(Net, Body5052(Slot, 2001, 20040, 0, 31233, 3));
		Expect(R, L4.EndsWith(FString::Printf(TEXT("%s : 3/10"), *Items->GetItemName(31233))),
			FString::Printf(TEXT("4: ремесленное 20040 — «%s» (без хвостового пробела, как эталон)"), *L4));
		// Возврат слота: исходная запись (или пустая).
		QM->ApplyQuestProgressBlob(Body5052(Slot, Saved.QuestId, Saved.QuestNo, Saved.Stage,
			Saved.ObjIds.IsValidIndex(0) ? Saved.ObjIds[0] : 0, Saved.ObjCounts.IsValidIndex(0) ? Saved.ObjCounts[0] : 0));
	}

	// ── Часть 5 (25.10.2026, решение владельца): тексты стадий Флэммио называют моба цели; названия заданий без пробелов ─
	// Нижний регистр (ICU — кириллица тоже), ё→е, без пробелов, дефисов и кавычек.
	static FString NormText(const FString& S)
	{
		FString L = FText::FromString(S).ToLower().ToString().Replace(TEXT("ё"), TEXT("е"));
		FString Out;
		for (TCHAR C : L)
		{
			if (!FChar::IsWhitespace(C) && C != TEXT('-') && C != TEXT('\'') && C != TEXT('`') && C != TEXT('"')
				&& C != 0x2019 && C != 0x00AB && C != 0x00BB) { Out.AppendChar(C); }
		}
		return Out;
	}

	// Имя цели упомянуто в тексте: каждое слово имени длиннее 4 букв найдено основой (без 2 последних букв, не короче 4)
	//   — то же правило, что у `Tools/asda_quest_mob_audit.py` (`name_in_text`).
	static bool NameInText(const FString& Name, const FString& Text)
	{
		const FString T = NormText(Text);
		FString N = FText::FromString(Name).ToLower().ToString().Replace(TEXT("ё"), TEXT("е")).Replace(TEXT("-"), TEXT(" "));
		TArray<FString> Words;
		N.ParseIntoArrayWS(Words);
		int32 Checked = 0;
		for (const FString& Wd : Words)
		{
			if (Wd.Len() <= 4) { continue; }
			++Checked;
			if (!T.Contains(Wd.Left(FMath::Max(4, Wd.Len() - 2)))) { return false; }
		}
		return Checked > 0 && !T.IsEmpty();
	}

	// Пробел у края названия (в том числе внутри кавычек RU: «"Патруль "»).
	static bool EdgeSpace(const FString& V)
	{
		const FString S = V.TrimStartAndEnd();
		const FString Inner = (S.Len() >= 2 && S[0] == TEXT('"') && S[S.Len() - 1] == TEXT('"')) ? S.Mid(1, S.Len() - 2) : S;
		return V != S || Inner != Inner.TrimStartAndEnd();
	}

	static void RunFlammioTexts(FRun& R, const UDataTable* Guide)
	{
		static const TCHAR* kBookDT = TEXT("/Game/AsdaAssets/Data/Quest/DT_QuestBook.DT_QuestBook");
		static const TCHAR* kEpDT = TEXT("/Game/AsdaAssets/Data/NPC/DT_EpisodeTable.DT_EpisodeTable");
		const UDataTable* Book = AsdaDt::Cached(kBookDT);
		const UDataTable* Eps = AsdaDt::Cached(kEpDT);
		if (!Book || !Eps) { Expect(R, false, TEXT("5: нет DT_QuestBook / DT_EpisodeTable")); return; }
		static const TSet<int32> Strict = { 566, 568, 579, 582, 584, 589, 590, 591, 592, 598, 604, 606, 607, 623, 624, 626 };
		int32 N = 0, Bad = 0;
		FString First;
		for (const TPair<FName, uint8*>& It : Guide->GetRowMap())
		{
			const FAsdaQuestGuideRow* G = reinterpret_cast<const FAsdaQuestGuideRow*>(It.Value);
			if (!G || G->TargetKind != 1 || !Strict.Contains(G->QuestNo)) { continue; }
			const FName Bn(*FString::Printf(TEXT("%dQST"), G->QuestNo));
			const FAsdaQuestBookRow* B = Book->FindRow<FAsdaQuestBookRow>(Bn, TEXT("GuideMobTest"), false);
			const FString Step = (B && B->Steps.IsValidIndex(G->Stage))
				? AsdaLangRows::Text(Book, B, *FString::Printf(TEXT("Steps.%d"), G->Stage), B->Steps[G->Stage]) : FString();
			const FString Target = AsdaEntityText::QuestTargetName(1, G->TargetId);
			++N;
			if (!NameInText(Target, Step))
			{
				if (Bad++ == 0) { First = FString::Printf(TEXT("квест %d: цель «%s», текст «%s»"), G->QuestNo, *Target, *Step); }
			}
		}
		Expect(R, N == 18 && Bad == 0, FString::Printf(TEXT("5: тексты стадий Флэммио называют моба цели: строк %d (ждали 18), расхождений %d%s%s"),
			N, Bad, Bad ? TEXT(" — первое: ") : TEXT(""), *First));
		// ★ 25.10.2026 (решение владельца): описания тех же заданий. Где моб назван — это моб цели; в описаниях без моба
		//   (только предметы/NPC) проверяется лишь отсутствие мобов Global. Правило — как у `asda_quest_mob_audit.py`.
		static const TSet<int32> DescNoMob = { 584, 589, 591, 604, 606, 624, 626 };
		const FRegexPattern GlobalMob(TEXT("слиз(ен|н)|царапун|огненн\\w{0,4}голем|лавов\\w{0,4}голем|железяк\\w{0,2}(воин|часов)|декронов|")
			TEXT("steelslime|firegolem|flamegolems|lavagolem|junkmanwarrior|junkmansentr|violonger|deckron(slime|junk)"));
		int32 ND = 0, BadD = 0, BadG = 0;
		FString FirstD, FirstG;
		for (const TPair<FName, uint8*>& It : Guide->GetRowMap())
		{
			const FAsdaQuestGuideRow* G = reinterpret_cast<const FAsdaQuestGuideRow*>(It.Value);
			if (!G || G->TargetKind != 1 || !Strict.Contains(G->QuestNo)) { continue; }
			const FAsdaQuestBookRow* B = Book->FindRow<FAsdaQuestBookRow>(FName(*FString::Printf(TEXT("%dQST"), G->QuestNo)), TEXT("GuideMobTest"), false);
			if (!B) { ++BadD; continue; }
			const FString Desc = AsdaLangRows::Text(Book, B, TEXT("Desc"), B->Desc);
			const FString Step = B->Steps.IsValidIndex(G->Stage) ? AsdaLangRows::Text(Book, B, *FString::Printf(TEXT("Steps.%d"), G->Stage), B->Steps[G->Stage]) : FString();
			const FString Target = AsdaEntityText::QuestTargetName(1, G->TargetId);
			if (!DescNoMob.Contains(G->QuestNo))
			{
				++ND;
				if (!NameInText(Target, Desc) && BadD++ == 0) { FirstD = FString::Printf(TEXT("квест %d: цель «%s»"), G->QuestNo, *Target); }
			}
			for (const FString* Txt : { &Desc, &Step })
			{
				FRegexMatcher M(GlobalMob, NormText(*Txt));
				if (M.FindNext() && BadG++ == 0) { FirstG = FString::Printf(TEXT("квест %d: «%s»"), G->QuestNo, *M.GetCaptureGroup(0)); }
			}
		}
		Expect(R, ND == 9 && BadD == 0, FString::Printf(TEXT("5: описания Флэммио с мобом называют моба цели: строк %d (ждали 9), расхождений %d%s%s"),
			ND, BadD, BadD ? TEXT(" — первое: ") : TEXT(""), *FirstD));
		Expect(R, BadG == 0, FString::Printf(TEXT("5: имён мобов Global в стадиях и описаниях Флэммио: %d%s%s"),
			BadG, BadG ? TEXT(" — первое: ") : TEXT(""), *FirstG));
		// Якорь: 579 «Просьба Ирис» — моб цели и в тексте, и в строке гида.
		const FAsdaQuestBookRow* B579 = Book->FindRow<FAsdaQuestBookRow>(FName(TEXT("579QST")), TEXT("GuideMobTest"), false);
		const FString S579 = B579 && B579->Steps.Num() ? AsdaLangRows::Text(Book, B579, TEXT("Steps.0"), B579->Steps[0]) : FString();
		const TCHAR* Want579 = FAsdaLang::IsRu() ? TEXT("Поймай 35 железношлемов") : TEXT("Capture 35 Patrolling Metal Helmet Slimes");
		Expect(R, S579.StartsWith(Want579), FString::Printf(TEXT("5: 579 стадия 0 — «%s»"), *S579));
		// ★ 06.11.2026: текст шага называет настоящую карту цели (ведомости RU и EN).
		auto StepOf = [Book](const TCHAR* Key) -> FString
		{
			const FAsdaQuestBookRow* B = Book->FindRow<FAsdaQuestBookRow>(FName(Key), TEXT("GuideMobTest"), false);
			return B && B->Steps.Num() ? AsdaLangRows::Text(Book, B, TEXT("Steps.0"), B->Steps[0]) : FString();
		};
		const FString S397 = StepOf(TEXT("397_Qst_180"));
		Expect(R, S397.Contains(FAsdaLang::IsRu() ? TEXT("в Логове Дракона") : TEXT("in Dragon's Lair")), FString::Printf(TEXT("5: 397 — «%s»"), *S397));
		const FString S392 = StepOf(TEXT("392_Qst_181")), S395 = StepOf(TEXT("395_Qst_179")), S181 = StepOf(TEXT("181_Qst_13"));
		Expect(R, S392.Contains(FAsdaLang::IsRu() ? TEXT("в Пламении") : TEXT("of Flamio")), FString::Printf(TEXT("5: 392 — «%s»"), *S392));
		Expect(R, S395.Contains(FAsdaLang::IsRu() ? TEXT("в Долине Ночи") : TEXT("in Night Valley")), FString::Printf(TEXT("5: 395 — «%s»"), *S395));
		Expect(R, S181.Contains(FAsdaLang::IsRu() ? TEXT("йети в Альпии") : TEXT("Abominable Snowmen in Alpen")), FString::Printf(TEXT("5: 181 — «%s»"), *S181));
		// Названия заданий (книги и эпизоды) на языке сессии — без пробелов у края.
		int32 NT = 0, BadT = 0;
		FString FirstT;
		for (const TPair<FName, uint8*>& It : Book->GetRowMap())
		{
			const FAsdaQuestBookRow* B = reinterpret_cast<const FAsdaQuestBookRow*>(It.Value);
			if (!B || B->Title.IsEmpty()) { continue; }
			const FString T = AsdaLangRows::Text(Book, B, TEXT("Title"), B->Title);
			++NT;
			if (EdgeSpace(T) && BadT++ == 0) { FirstT = FString::Printf(TEXT("книга %s «%s»"), *It.Key.ToString(), *T); }
		}
		for (const TPair<FName, uint8*>& It : Eps->GetRowMap())
		{
			const FAsdaEpisodeRow* E = reinterpret_cast<const FAsdaEpisodeRow*>(It.Value);
			if (!E || E->Title.IsEmpty()) { continue; }
			const FString T = AsdaLangRows::Text(Eps, E, TEXT("Title"), E->Title);
			++NT;
			if (EdgeSpace(T) && BadT++ == 0) { FirstT = FString::Printf(TEXT("эпизод %s «%s»"), *It.Key.ToString(), *T); }
		}
		Expect(R, NT > 900 && BadT == 0, FString::Printf(TEXT("5: названий заданий %d, с пробелом у края %d%s%s"),
			NT, BadT, BadT ? TEXT(" — первое: ") : TEXT(""), *FirstT));
		const FAsdaQuestBookRow* B424 = Book->FindRow<FAsdaQuestBookRow>(FName(TEXT("424_Qst_34")), TEXT("GuideMobTest"), false);
		const FString T424 = B424 ? AsdaLangRows::Text(Book, B424, TEXT("Title"), B424->Title) : FString();
		Expect(R, T424 == (FAsdaLang::IsRu() ? FString(TEXT("\"Самый верный друг\"")) : FString(TEXT("A Man’s Best Friend"))),
			FString::Printf(TEXT("5: название 424 — «%s»"), *T424));
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaQuestLogSubsystem* QL = GI ? GI->GetSubsystem<UAsdaQuestLogSubsystem>() : nullptr;
		const UDataTable* Guide = AsdaDt::Cached(kGuideDT);
		const UDataTable* Names = AsdaDt::Cached(AsdaEntityText::kMobNamesPath);
		if (!QL || !Guide || !Names) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет журнала заданий или таблиц гида/имён мобов")); return; }
		FRun R;
		FAsdaCVarScope Keys(W, kCmd);
		Keys.Set(TEXT("asda.Quest.GuideMobRow"), 1);
		UE_LOG(LogAsdaGuideMobTest, Log, TEXT("ГИД-МОБ язык сессии: %s"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en"));

		// 1) Все цели-мобы гида: общая точка имени = оракул по строке TargetId.
		int32 NMob = 0, NBad = 0, NEmpty = 0;
		FString FirstBad;
		for (const TPair<FName, uint8*>& It : Guide->GetRowMap())
		{
			const FAsdaQuestGuideRow* G = reinterpret_cast<const FAsdaQuestGuideRow*>(It.Value);
			if (!G || G->TargetKind != 1) { continue; }
			++NMob;
			const FString Want = OracleMobName(Names, G->TargetId);
			const FString Got = AsdaEntityText::QuestTargetName(1, G->TargetId);
			if (Want.IsEmpty()) { ++NEmpty; }
			if (Want.IsEmpty() || Got != Want)
			{
				if (NBad++ == 0) { FirstBad = FString::Printf(TEXT("квест %d ст.%d цель %d: «%s» ≠ «%s»"), G->QuestNo, G->Stage, G->TargetId, *Got, *Want); }
			}
		}
		Expect(R, NMob == 358 && NBad == 0, FString::Printf(TEXT("1: целей-мобов в гиде %d (ждали 358), расхождений с оракулом %d (пустых имён %d)%s%s"),
			NMob, NBad, NEmpty, NBad ? TEXT(" — первое: ") : TEXT(""), *FirstBad));

		// 2) Якоря: жалобы игроков (Discord 18.10) и ведомость исправлений гида.
		Anchor(R, QL, 407, 0, 0, TEXT("Малютка-прыгун"), TEXT("Baby Stamp"), 3, 120, 163, TEXT("«Воин-новичок»: было «Мерзкий жуткозонтик / Outrageous Panic Parasol»"));
		Anchor(R, QL, 91, 0, 0, TEXT("Зонтик-малютка"), TEXT("BabyParasol"), 3, 70, 213, TEXT("первое задание: было «Степной волк / FieldWolf»"));
		Anchor(R, QL, 207, 0, 0, TEXT("Рыжеухий волк"), TEXT("Red-earedWolf"), 3, 60, 163, TEXT("третье задание: было «Зонтик-малютка / BabyParasol»"));
		Anchor(R, QL, 2, 0, 0, TEXT("Злобный зонтик"), TEXT("ManiacParasol"), 3, 115, 128, TEXT("«Доспехи и оружие» (Armory): было «Вожак степных волков / FieldWolfGeneral»"));
		Anchor(R, QL, 90, 0, 0, TEXT("Степной волк"), TEXT("FieldWolf"), 3, 110, 213, TEXT("цель 0: было «#0»"));
		Anchor(R, QL, 221, 0, 0, TEXT("Тяжелонос"), TEXT("Loaderwalk"), 8, -1, -1, TEXT("ведомость: дефект оригинала, сервер type 289"));
		Anchor(R, QL, 20042, 0, 1, TEXT("Птица-краснокрыл"), TEXT("RedWingParrot"), 6, 151, 75, TEXT("ведомость: дефект оригинала, сервер type 595"));
		Anchor(R, QL, 310, 0, 0, TEXT("Эллоса"), TEXT("Ellosa"), 0, 424, 378, TEXT("ведомость: стадия 0 — только Эллоса"));
		Anchor(R, QL, 310, 1, 0, TEXT("Яростный жуткозонтик"), TEXT("Violent PanicParasol"), 2, 265, 312, TEXT("ведомость: стадия 1 — моб сервера, координаты текста"));
		Anchor(R, QL, 310, 2, 0, TEXT("Солдат Мелв"), TEXT("Soldier Melve"), 0, 341, 408, TEXT("ведомость: стадия 2 — сдача Мелву"));
		Anchor(R, QL, 579, 0, 0, TEXT("Железношлем"), TEXT("Patrolling Metal Helmet Slime"), 7, 280, 157, TEXT("ведомость: Флэммио, сервер type 264"));
		Anchor(R, QL, 606, 0, 2, TEXT("Железяка-дозорный"), TEXT("Junkman Patrol"), 7, 152, 359, TEXT("ведомость: Флэммио, сервер type 271"));
		// ★ 05.11.2026 (баг «Find Hat Material», Player15 20.10): сервер засчитывает 220 только за type 304/308 «Сварливый слон»
		//   (Monster.ai), «Обычный слон» type 303/307 — нет; до 25.10 гид называл соседнюю строку «Обычный слон».
		//   EN-имя «III-Tempered» — как в данных оригинала (Interlude) и над мобом в мире.
		Anchor(R, QL, 220, 0, 0, TEXT("Сварливый слон"), TEXT("III-Tempered Bishop"), 8, -1, -1, TEXT("«Find Hat Materials»: сервер type 304/308"));
		// ★ 05.11.2026: ведомость, запись 764 — вторая цель 20056 «Охота на попугаев» была «Голем-паук» (type 813, сервер не засчитывает);
		//   стала строка 817 «Попугай Диа» (type 818 순찰자_다이아패럿). Координаты не сверяем: точку может уточнять гид «Следовать».
		Anchor(R, QL, 20056, 0, 1, TEXT("Попугай Диа"), TEXT("DiaParrot"), 16, -1, -1, TEXT("ведомость: дефект оригинала, сервер type 817/818"));
		// ★ 06.11.2026 (решение владельца: зачёт, текст и гид говорят одно): цели, перенесённые с горы Пламень на настоящие карты.
		Anchor(R, QL, 392, 0, 0, TEXT("Огненный голем"), TEXT("FireGolem"), 7, -1, -1, TEXT("ведомость: цель в Пламении, не на горе Пламень"));
		Anchor(R, QL, 395, 0, 0, TEXT("Темнящий"), TEXT("Snider"), 4, -1, -1, TEXT("ведомость: цель в Долине Ночи"));
		Anchor(R, QL, 300, 0, 1, TEXT("Стальной мышоид"), TEXT("Steel Mouse Royd"), 11, -1, -1, TEXT("ведомость: босс из текста, Шахматный замок (турнир), а не Солнечный Берег"));
		// ★ 06.11.2026: стадии, у которых в данных оригинала не было цели в гиде (`adds` ведомости).
		Anchor(R, QL, 48, 0, 0, TEXT("Грязный человек-гриб"), TEXT("DirtyMushroomMan"), 1, -1, -1, TEXT("новая строка гида"));
		Anchor(R, QL, 181, 0, 0, TEXT("Йети"), TEXT("Abominable Snowman"), 3, -1, -1, TEXT("новая строка гида: сервер засчитывает type 240/241"));
		Anchor(R, QL, 397, 0, 0, TEXT("Злой дракон Двуглавый"), TEXT("EvilDragon Enkidu"), 15, -1, -1, TEXT("решение владельца: зачёт дракону 761, Логово Дракона"));
		Anchor(R, QL, 95, 0, 0, TEXT("Палач"), TEXT("HangMan"), 2, -1, -1, TEXT("решение владельца: зачёт палачам 75/76"));
		Anchor(R, QL, 126, 0, 1, TEXT("Крыса-боксер"), TEXT("PunchRat"), 2, -1, -1, TEXT("решение владельца: зачёт крысам 59/60"));
		// ★ 06.11.2026: выдача заданий без цели закрыта ключом; соседнее задание цепочки — нет.
		Expect(R, AsdaNsrScript::StartClosed(156) && AsdaNsrScript::StartClosed(157) && !AsdaNsrScript::StartClosed(155),
			TEXT("выдача 156 и 157 закрыта (asda.Quest.ClosedStart), 155 открыта"));
		Keys.Set(TEXT("asda.Quest.ClosedStart"), 0);
		Expect(R, !AsdaNsrScript::StartClosed(156), TEXT("ключ asda.Quest.ClosedStart 0: как в оригинале"));
		Keys.Set(TEXT("asda.Quest.ClosedStart"), 1);
		Expect(R, QL->BuildRoutesFor(339, 0).Num() == 3 && QL->BuildRoutesFor(192, 0).Num() == 2,
			TEXT("новые строки гида: у 339 три цели, у 192 две (цепочка ContinueFlag)"));
		Expect(R, QL->BuildRoutesFor(310, 0).Num() == 1 && QL->BuildRoutesFor(310, 1).Num() == 1,
			TEXT("ведомость: у квеста 310 на стадиях 0 и 1 ровно по одной строке маршрута"));

		// 3) Ключ отката: 0 = прежняя строка TargetId − 1.
		Keys.Set(TEXT("asda.Quest.GuideMobRow"), 0);
		const FString Old = AsdaEntityText::QuestTargetName(1, 619);
		Expect(R, !AsdaEntityText::QuestGuideMobRowOn() && Old == OracleMobName(Names, 618),
			FString::Printf(TEXT("3: ключ 0 — прежнее поведение: цель 619 → «%s» (строка 618)"), *Old));
		Keys.Set(TEXT("asda.Quest.GuideMobRow"), 1);
		Expect(R, AsdaEntityText::QuestGuideMobRowOn() && AsdaEntityText::QuestTargetName(1, 619) == OracleMobName(Names, 619),
			TEXT("3: ключ 1 — цель 619 снова строка 619"));

		// 4) Счётчик экранной строки прогресса у последнего моба («2/0» → «2/2»).
		Keys.Set(TEXT("asda.Quest.ProgressNeedPrevStage"), 1);
		RunProgressCounter(R, GI, Keys);
		// 5) Тексты стадий Флэммио и названия заданий (данные, без ключей).
		RunFlammioTexts(R, Guide);
		Keys.Restore();

		AsdaSelfTestKit::Finish(W, kCmd, Args.bQuit, LogAsdaGuideMobTest, TEXT("ГИД-МОБ"), R.Ok, R.Fail);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestQuestGuideMobNames(
	TEXT("asda.SelfTest.QuestGuideMobNames"),
	TEXT("Самотест: имя цели-моба гида задания (журнал 700 / трекер 1800) = строка TargetId таблицы мобов, как у эталона; "
	     "якоря жалоб игроков и ведомости гида RU/EN; ключ отката. Офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaQuestGuideMobSelfTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
