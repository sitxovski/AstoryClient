// Copyright (c) 2026 @sitxovski (Discord)
// AsdaTutorialSelfTest.cpp — самотест ОБУЧЕНИЯ (10.11.2026): гид 1950 и книга обучения со значком «?».
//   Разбор системы — `Docs/Port/systems/TUTORIAL.md`.
//
// СЦЕНАРИЙ `asda.SelfTest.Tutorial [out=<папка>] [quit]` (офлайн, -AsdaNoNet, мир со своим персонажем; RU и EN).
//   ГИД (путь игрока там, где он есть в порте офлайн):
//     G1  вход в мир на уровне 1 (`OnWorldReady`) — окно 1950 открыто на шаге 1, текст = loc 103001 языка сессии,
//         «Да»/«Нет» видны; кадр `asda_tutorial_guide_step1.png`;
//     G2  «Да» → шаг 2; флаги шага (сдвиг + ЛКМ) → через 1 с шаг 3 (засчёт с задержкой, не раньше);
//     G3  шаг 9: клавиша карты (`ToggleWorldMap`, байт 78) → шаг 10; кадр `asda_tutorial_guide_step9.png`;
//     G4  шаг 18: открыть рюкзак (`UAsdaInventorySubsystem::OpenWindow`, байт 79) → шаг 19;
//     G5  крестик 1955 → окно закрыто; вход в мир на уровне 2 гида не открывает.
//   КНИГА:
//     B1  6185 с нулевой маской (`DebugFeedPacket`) → через 3 с «?» 69800 на первом доступном разделе; кадр `asda_tutorial_notice.png`;
//     B2  клик по «?» → полоса 69200, раздел «Чат»: заголовок/текст/ответ на языке сессии; кадр `asda_tutorial_book.png`;
//     B3  разговор с NPC при открытой книге не открывается (гейт `regionWindow_OpenAndPathMove`);
//     B4  «Далее» → стр. 1, «КОНЕЦ» → полоса скрыта, бит раздела, 6184 (перехват): charId/userNum/id раздела;
//     B5  «?» больше не горит на пройденном разделе: опрос выбирает СЛЕДУЮЩИЙ раздел;
//     B6  «Закрыть» 69299 — 6184 НЕ уходит (опыт только за «КОНЕЦ», решение владельца 10.11), бит раздела стоит;
//     B7  «перезаход»: 6185 с маской из отправленных битов — «?» не возвращается ни на «КОНЕЦ», ни на «Закрыть».
//   ЛОКАЛЬНЫЙ ПРОГРЕСС (AsdaTutorialStore.cpp; каталог самотеста `Saved/AsdaSelfTest/TutorialStore`, стирается):
//     L1  персонаж A: файл записан, зашифрован (маски открытым текстом в нём нет);
//     L2  «перезапуск» A (полный сброс сессии): гид на уровне 1 не стартует (закрыт крестиком на шаге 19), отметки
//         «Закрыть» вернулись из файла, «?» не горит на разделах A даже при пустой маске сервера;
//     L3  персонаж B той же учётки — всё с нуля: гид с шага 1, разделы A не отмечены; шаг 5 B продолжается после «перезапуска»;
//     L4  подделанный файл A (байт шифротекста) — тихий сброс: гид с шага 1, отметок нет, падения нет;
//     L5  файл B, подложенный под имя A, и файл A под другой учёткой — отвергнуты (привязка учётка/сервер/charId).
//   Итог — `ОБУЧЕНИЕ ИТОГ: ok=N fail=M`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"   // ASDA_DEV_CONSOLE — дев-команда вне Shipping
#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
#include "UI/AsdaTutorialSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "Login/AsdaText.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTutorialTest, Log, All);

// Доступ к закрытым частям подсистемы (друг класса).
struct FAsdaTutorialSelfTest
{
	static void WorldReady(UAsdaTutorialSubsystem* T) { T->OnWorldReady(); }
	static void Ok(UAsdaTutorialSubsystem* T) { T->GuideOnOk(); }
	static void CloseX(UAsdaTutorialSubsystem* T) { T->GuideOnClose(); }
	static void SetStep(UAsdaTutorialSubsystem* T, int32 S) { T->GuideStep = S; }
	static void Flag(UAsdaTutorialSubsystem* T, int32 I) { T->GuideFlags[I] = 1; }
	static FString ShownText(UAsdaTutorialSubsystem* T) { return T->GuideTxtFmt.LastRaw; }
	static FString FullText(UAsdaTutorialSubsystem* T) { return T->GuideText; }
	static bool OkVisible(UAsdaTutorialSubsystem* T) { return T->GuideOkBtn.IsValid() && T->GuideOkBtn->GetVisibility() == ESlateVisibility::Visible; }
	static bool NoVisible(UAsdaTutorialSubsystem* T) { return T->GuideNoBtn.IsValid() && T->GuideNoBtn->GetVisibility() == ESlateVisibility::Visible; }
	static bool HasCloseBtn(UAsdaTutorialSubsystem* T) { return T->GuideCloseBtn.IsValid(); }
	static bool GuideShown(UAsdaTutorialSubsystem* T) { return T->GuideWin && T->GuideWin->GetVisibility() == ESlateVisibility::SelfHitTestInvisible; }
	static void ResetCycle(UAsdaTutorialSubsystem* T) { T->BookTimerMs = 0.0; }
	static void OpenBook(UAsdaTutorialSubsystem* T) { T->OpenBook(); }
	static bool PickAnswer(UAsdaTutorialSubsystem* T, int32 J)
	{
		const FAsdaTutorialPage* P = (T->Sections.IsValidIndex(T->BookCategory) && T->Sections[T->BookCategory].Pages.IsValidIndex(T->BookPage))
			? &T->Sections[T->BookCategory].Pages[T->BookPage] : nullptr;
		if (!P || !P->NextPage.IsValidIndex(J) || P->NextPage[J] == -1) { return false; }
		T->BookPage = P->NextPage[J];   // то же, что ветка события 0xB `BookMouseTick`
		T->RefreshPage();
		return true;
	}
	static void CloseBook(UAsdaTutorialSubsystem* T) { T->OnBookCloseClicked(); }
	static FString Raw(const FAsdaFmtText& F) { return F.LastRaw; }
	static FString Title(UAsdaTutorialSubsystem* T) { return Raw(T->BookTitle); }
	static FString Desc(UAsdaTutorialSubsystem* T) { return Raw(T->BookDesc); }
	static FString Answer(UAsdaTutorialSubsystem* T, int32 J) { return Raw(T->BookAnswer[J]); }
	static bool BookShown(UAsdaTutorialSubsystem* T) { return T->BookWin && T->BookWin->GetVisibility() == ESlateVisibility::SelfHitTestInvisible; }
	static bool NoticeShown(UAsdaTutorialSubsystem* T) { return T->NoticeWin && T->NoticeWin->GetVisibility() == ESlateVisibility::SelfHitTestInvisible; }
	static int32 SectionCount(UAsdaTutorialSubsystem* T) { return T->Sections.Num(); }
	static int32 SectionId(UAsdaTutorialSubsystem* T, int32 I) { return T->Sections.IsValidIndex(I) ? T->Sections[I].Id : -1; }
	static void GetMask(UAsdaTutorialSubsystem* T, TArray<uint8>& Out) { Out.SetNumUninitialized(32); FMemory::Memcpy(Out.GetData(), T->Mask, 32); }
	// Локальные отметки разделов (то, что уходит в файл персонажа).
	static void GetLocalMask(UAsdaTutorialSubsystem* T, TArray<uint8>& Out) { Out.SetNumUninitialized(32); FMemory::Memcpy(Out.GetData(), T->LocalMask, 32); }
	// Бит раздела в маске СЕРВЕРА/памяти (без локальных отметок).
	static bool ServerBit(UAsdaTutorialSubsystem* T, int32 I) { return I >= 0 && I < 256 && (T->Mask[I >> 3] & (1u << (I & 7))) != 0; }
	// Каталог хранилища самотеста (пусто — штатный Saved/AsdaTutorial).
	static void SetStoreDir(UAsdaTutorialSubsystem* T, const FString& D) { T->StoreDirOverride = D; }
	// Путь файла текущего персонажа (по привязке сессии).
	static FString StorePath(UAsdaTutorialSubsystem* T) { return T->StorePathFor(T->MakeStoreBinding()); }
	// Индекс записи раздела по его id (бит маски — по индексу записи, 6184 несёт id).
	static int32 IndexOfId(UAsdaTutorialSubsystem* T, int32 Id) { for (int32 I = 0; I < T->Sections.Num(); ++I) { if (T->Sections[I].Id == Id) { return I; } } return -1; }
	// «Перезапуск»: полный сброс сессии (как выход в выбор персонажа / новый вход) — память обучения с нуля.
	static void FullReset(UAsdaTutorialSubsystem* T) { T->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
};

namespace AsdaTutorialTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.Tutorial");
	static const TCHAR* kPfx = TEXT("ОБУЧЕНИЕ");

	struct FSent { int32 CharId = 0; int32 UserNum = 0; int32 Id = -1; };

	struct FRun : public TSharedFromThis<FRun>
	{
		TWeakObjectPtr<UGameInstance> GI;
		TWeakObjectPtr<UWorld> World;
		bool bQuit = false;
		FString OutDir;
		int32 Ok = 0, Fail = 0;
		int32 Phase = 0;
		double PhaseT0 = 0.0;
		uint32 Gen = 0;
		TSharedRef<TArray<FSent>> Sent = MakeShared<TArray<FSent>>();
		TUniquePtr<FAsdaNetSelfScope> Self;
		TUniquePtr<FAsdaCVarScope> Keys;
		TArray<FString> Shots;
		int32 FirstSection = -1, SecondSection = -1;
		FString StoreDir;              // каталог хранилища самотеста
		FString SavedAccount;          // учётка сессии до прогона (вернуть)
		FString PathA, PathB;          // файлы персонажей A и B
		TArray<uint8> FileA, FileB;    // их содержимое (для подмены)

		template <class T> T* Sub() const { return GI.IsValid() ? GI->GetSubsystem<T>() : nullptr; }
		UAsdaTutorialSubsystem* Tut() const { return Sub<UAsdaTutorialSubsystem>(); }
		void Check(bool b, const FString& What)
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaTutorialTest, Display, TEXT("%s %s %s"), kPfx, b ? TEXT("ok  ") : TEXT("FAIL"), *What);
		}
		void Shot(const TCHAR* Name)
		{
			FScreenshotRequest::RequestScreenshot(Name, /*bShowUI*/ true, /*bAddUniqueSuffix*/ false);
			Shots.Add(Name);
		}
		double Now() const { return FPlatformTime::Seconds(); }
		void Next() { ++Phase; PhaseT0 = Now(); }
		bool Waited(double S) const { return Now() - PhaseT0 >= S; }
		FString Loc(int32 Id) const { return AsdaUi::LocRaw(GI.Get(), Id, false); }
		void FeedMask(const TArray<uint8>& M)
		{
			if (UAsdaNetSubsystem* N = Sub<UAsdaNetSubsystem>()) { N->DebugFeedPacket(6185, M); }
		}
	};

	// Один шаг машины состояний; false — прогон окончен.
	static bool Step(const TSharedRef<FRun>& R)
	{
		UAsdaTutorialSubsystem* T = R->Tut();
		UAsdaNetSubsystem* Net = R->Sub<UAsdaNetSubsystem>();
		if (!T || !Net) { R->Check(false, TEXT("подсистемы пропали")); return false; }
		using X = FAsdaTutorialSelfTest;
		switch (R->Phase)
		{
		case 0:   // G1: вход в мир на уровне 1
			Net->SelfLevel = 1;
			X::WorldReady(T);
			R->Check(T->IsGuideActive() && T->GetGuideStep() == 1, FString::Printf(TEXT("G1 вход на уровне 1 — гид открыт, шаг %d"), T->GetGuideStep()));
			R->Check(X::HasCloseBtn(T), TEXT("G1 крестик 1955 есть в окне (Tuto_Msg_Box_4_BTN00)"));
			R->Next();
			return true;
		case 1:   // ждём печать текста шага 1
			if (!R->Waited(3.0)) { return true; }
			{
				const FString Want = R->Loc(103001);
				R->Check(!Want.IsEmpty() && X::ShownText(T) == Want, FString::Printf(TEXT("G1 текст шага 1 = loc 103001 («%s»), показано «%s»"), *Want.Left(40), *X::ShownText(T).Left(40)));
				R->Check(X::OkVisible(T) && X::NoVisible(T), TEXT("G1 «Да»/«Нет» видны"));
				R->Check(X::GuideShown(T), TEXT("G1 окно 1950 видно"));
				R->Shot(TEXT("asda_tutorial_guide_step1"));
			}
			R->Next();
			return true;
		case 2:   // G2: «Да» → шаг 2; флаги шага 2
			if (!R->Waited(0.5)) { return true; }
			X::Ok(T);
			R->Check(T->GetGuideStep() == 2, FString::Printf(TEXT("G2 «Да» → шаг %d"), T->GetGuideStep()));
			R->Next();
			return true;
		case 3:
			if (!R->Waited(0.3)) { return true; }
			X::Flag(T, 82); X::Flag(T, 44);
			R->Next();
			return true;
		case 4:   // засчёт не раньше 1 с
			if (!R->Waited(0.5)) { return true; }
			R->Check(T->GetGuideStep() == 2, TEXT("G2 через 0,5 с шаг ещё 2 (задержка засчёта 1 с)"));
			R->Next();
			return true;
		case 5:
			if (!R->Waited(1.0)) { return true; }
			R->Check(T->GetGuideStep() == 3, FString::Printf(TEXT("G2 сдвиг+ЛКМ → шаг %d"), T->GetGuideStep()));
			X::SetStep(T, 9);
			R->Next();
			return true;
		case 6:   // G3: шаг 9 — клавиша карты
			if (!R->Waited(0.5)) { return true; }
			R->Shot(TEXT("asda_tutorial_guide_step9"));
			if (UAsdaMinimapSubsystem* Mm = R->Sub<UAsdaMinimapSubsystem>()) { Mm->ToggleWorldMap(); }
			R->Next();
			return true;
		case 7:
			if (!R->Waited(1.6)) { return true; }
			R->Check(T->GetGuideStep() == 10, FString::Printf(TEXT("G3 клавиша карты (байт 78) → шаг %d"), T->GetGuideStep()));
			if (UAsdaMinimapSubsystem* Mm = R->Sub<UAsdaMinimapSubsystem>()) { if (Mm->IsBigMapOpen()) { Mm->SetBigMapVisible(false); } }
			X::SetStep(T, 18);
			R->Next();
			return true;
		case 8:   // G4: шаг 18 — рюкзак
			if (!R->Waited(0.3)) { return true; }
			if (UAsdaInventorySubsystem* Inv = R->Sub<UAsdaInventorySubsystem>()) { Inv->CloseWindow(); Inv->OpenWindow(); }
			R->Next();
			return true;
		case 9:
			if (!R->Waited(1.6)) { return true; }
			R->Check(T->GetGuideStep() == 19, FString::Printf(TEXT("G4 рюкзак (байт 79) → шаг %d"), T->GetGuideStep()));
			if (UAsdaInventorySubsystem* Inv = R->Sub<UAsdaInventorySubsystem>()) { Inv->CloseWindow(); }
			X::CloseX(T);
			R->Check(!T->IsGuideActive(), TEXT("G5 крестик 1955 закрыл гид"));
			Net->SelfLevel = 2;
			X::WorldReady(T);
			R->Check(!T->IsGuideActive(), TEXT("G5 после крестика вход (уровень 2) гида не открывает"));
			R->Next();
			return true;
		case 10:  // B1: 6185 нулевая маска, уровень 20
		{
			Net->SelfLevel = 20;
			TArray<uint8> Zero; Zero.SetNumZeroed(32);
			R->FeedMask(Zero);
			X::ResetCycle(T);
			R->Next();
			return true;
		}
		case 11:
			if (!R->Waited(0.5)) { return true; }
			R->FirstSection = T->GetBookCategory();
			R->Check(T->IsNoticeShown() && X::NoticeShown(T), FString::Printf(TEXT("B1 «?» 69800 показан, раздел %d"), R->FirstSection));
			R->Check(X::SectionCount(T) == 27, FString::Printf(TEXT("B1 таблица книги: разделов %d (ждём 27)"), X::SectionCount(T)));
			R->Check(R->FirstSection == 2, TEXT("B1 первый доступный раздел — 2 «Чат» (уровень ≥ 2)"));
			R->Shot(TEXT("asda_tutorial_notice"));
			R->Next();
			return true;
		case 12:  // B2: клик по «?»
			if (!R->Waited(0.5)) { return true; }
			X::OpenBook(T);
			R->Check(T->IsBookOpen() && X::BookShown(T) && !X::NoticeShown(T), TEXT("B2 клик по «?» — полоса 69200 открыта, «?» погас"));
			{
				const FString WantTitle = FAsdaLang::Resolve(TEXT("Tutorial"), TEXT("2.p0"), FAsdaLang::IsRu() ? TEXT("Чат") : TEXT("Chat"));
				R->Check(X::Title(T) == WantTitle, FString::Printf(TEXT("B2 заголовок «%s» (ждём «%s»)"), *X::Title(T), *WantTitle));
				const FString WantDesc = FAsdaText::Get(TEXT("Tutorial_text"), 20000, TEXT("text"));
				R->Check(!WantDesc.IsEmpty() && X::Desc(T) == WantDesc, FString::Printf(TEXT("B2 текст страницы = Tutorial_text 20000 («%s»)"), *X::Desc(T).Left(40)));
				const FString WantAns = FAsdaText::Get(TEXT("Tutorial_text_answer"), 2, TEXT("text"));
				R->Check(X::Answer(T, 0) == WantAns, FString::Printf(TEXT("B2 ответ 1 = «%s»"), *FAsdaMarkup::StripTags(X::Answer(T, 0))));
			}
			R->Check(!AsdaTutorial::OnNpcTalkOpening(R->GI.Get()), TEXT("B3 разговор с NPC при открытой книге не открывается"));
			R->Shot(TEXT("asda_tutorial_book"));
			R->Next();
			return true;
		case 13:  // B4: «Далее» → стр. 1, «КОНЕЦ»
			if (!R->Waited(0.5)) { return true; }
			R->Check(X::PickAnswer(T, 0) && T->GetBookPage() == 1, FString::Printf(TEXT("B4 «Далее» → страница %d"), T->GetBookPage()));
			R->Check(FAsdaMarkup::StripTags(X::Answer(T, 0)) == FAsdaMarkup::StripTags(FAsdaText::Get(TEXT("Tutorial_text_answer"), 4, TEXT("text"))),
				FString::Printf(TEXT("B4 на стр. 1 ответ «%s» (КОНЕЦ)"), *FAsdaMarkup::StripTags(X::Answer(T, 0))));
			R->Check(!AsdaTutorial::OnNpcTalkOpening(R->GI.Get()) && T->GetBookPage() == 1 && !X::NoticeShown(T),
				TEXT("B3б [ИСПРАВЛЕНИЕ] разговор с NPC при открытой книге не сбрасывает страницу 1 и не поднимает «?»"));
			{
				const int32 N0 = R->Sent->Num();
				X::PickAnswer(T, 0);
				R->Check(!T->IsBookOpen() && !X::BookShown(T), TEXT("B4 «КОНЕЦ» — полоса скрыта"));
				R->Check(T->IsSectionDone(2), TEXT("B4 бит раздела 2 поставлен"));
				const bool bOne = R->Sent->Num() == N0 + 1;
				R->Check(bOne, FString::Printf(TEXT("B4 ушёл ровно один 6184 (%d)"), R->Sent->Num() - N0));
				if (bOne)
				{
					const FSent& S = R->Sent->Last();
					R->Check(S.CharId == 12345 && S.UserNum == 321 && S.Id == X::SectionId(T, 2),
						FString::Printf(TEXT("B4 тело 6184: charId=%d userNum=%d id=%d"), S.CharId, S.UserNum, S.Id));
				}
			}
			X::ResetCycle(T);
			R->Next();
			return true;
		case 14:  // B5: «?» на следующем разделе
			if (!R->Waited(0.5)) { return true; }
			R->SecondSection = T->GetBookCategory();
			R->Check(T->IsNoticeShown() && R->SecondSection != 2 && R->SecondSection >= 0,
				FString::Printf(TEXT("B5 «?» снова горит, но на разделе %d (пройденный 2 пропущен)"), R->SecondSection));
			X::OpenBook(T);
			R->Next();
			return true;
		case 15:  // B6: «Закрыть» — 6184 нет, бит есть
		{
			if (!R->Waited(0.3)) { return true; }
			const int32 N0 = R->Sent->Num();
			X::CloseBook(T);
			R->Check(R->Sent->Num() == N0 && T->IsSectionDone(R->SecondSection) && !T->IsBookOpen(),
				FString::Printf(TEXT("B6 «Закрыть» раздела %d — 6184 нет (опыта нет), бит стоит"), R->SecondSection));
			// B7: «перезаход» — сервер присылает маску только из отправленных битов (раздел 2 по «КОНЕЦ»).
			TArray<uint8> M; M.SetNumZeroed(32);
			for (const FSent& S : *R->Sent)
			{
				const int32 I = X::IndexOfId(T, S.Id);
				if (I >= 0 && I < 256) { M[I >> 3] |= (uint8)(1u << (I & 7)); }
			}
			R->FeedMask(M);
			X::ResetCycle(T);
			R->Next();
			return true;
		}
		case 16:
			if (!R->Waited(0.5)) { return true; }
			R->Check(X::ServerBit(T, 2) && !X::ServerBit(T, R->SecondSection) && T->IsSectionDone(R->SecondSection),
				FString::Printf(TEXT("B7 маска сервера: раздел 2 есть, %d («Закрыть») нет — пройден по локальному биту"), R->SecondSection));
			R->Check(T->IsNoticeShown() && T->GetBookCategory() >= 0 && T->GetBookCategory() != 2 && T->GetBookCategory() != R->SecondSection,
				FString::Printf(TEXT("B7 «?» не возвращается на разделы 2/%d (горит на %d)"), R->SecondSection, T->GetBookCategory()));
			R->Next();
			return true;
		case 17:  // L1: файл A
		{
			R->PathA = X::StorePath(T);
			const bool bLoaded = FFileHelper::LoadFileToArray(R->FileA, *R->PathA);
			R->Check(bLoaded && R->FileA.Num() == 88, FString::Printf(TEXT("L1 файл персонажа A записан (%d б): %s"), R->FileA.Num(), *FPaths::GetCleanFilename(R->PathA)));
			TArray<uint8> LM; X::GetLocalMask(T, LM);
			bool bPlain = false;
			for (int32 I = 0; I + 32 <= R->FileA.Num(); ++I) { if (FMemory::Memcmp(R->FileA.GetData() + I, LM.GetData(), 32) == 0) { bPlain = true; } }
			const int32 CidA = 12345;
			bool bCid = false;
			for (int32 I = 0; I + 4 <= R->FileA.Num(); ++I) { if (FMemory::Memcmp(R->FileA.GetData() + I, &CidA, 4) == 0) { bCid = true; } }
			R->Check(bLoaded && !bPlain && !bCid, TEXT("L1 файл зашифрован: ни маски, ни 4 байт charId открытым текстом в нём нет"));
			// L2: «перезапуск» A
			X::FullReset(T);
			R->Check(!T->IsSectionDone(R->SecondSection) && T->GetGuideStep() == 1, TEXT("L2 полный сброс: память обучения пуста"));
			Net->SelfLevel = 1;
			X::WorldReady(T);
			R->Check(!T->IsGuideActive() && T->IsGuideDismissed() && T->GetGuideStep() == 19,
				FString::Printf(TEXT("L2 вход A на уровне 1 — гид не стартует (закрыт на шаге %d)"), T->GetGuideStep()));
			R->Check(T->IsSectionDone(R->SecondSection) && T->IsSectionDone(2), FString::Printf(TEXT("L2 отметки A из файла: разделы 2 и %d"), R->SecondSection));
			Net->SelfLevel = 20;
			TArray<uint8> Zero; Zero.SetNumZeroed(32);
			R->FeedMask(Zero);   // худший случай: сервер не знает ни одного раздела
			X::ResetCycle(T);
			R->Next();
			return true;
		}
		case 18:
			if (!R->Waited(0.5)) { return true; }
			R->Check(T->IsNoticeShown() && T->GetBookCategory() >= 0 && T->GetBookCategory() != 2 && T->GetBookCategory() != R->SecondSection,
				FString::Printf(TEXT("L2 после перезапуска при пустой маске сервера «?» горит на %d, не на разделах A 2/%d"), T->GetBookCategory(), R->SecondSection));
			// L3: персонаж B той же учётки
			X::FullReset(T);
			Net->SelectedCharId = 54321;
			Net->SelfLevel = 2;
			X::WorldReady(T);
			R->Check(!T->IsGuideActive() && !T->IsGuideDismissed(), TEXT("G5 чистый персонаж B на уровне 2 — гид не открывается"));
			Net->SelfLevel = 1;
			X::WorldReady(T);
			R->Check(T->IsGuideActive() && T->GetGuideStep() == 1 && !T->IsGuideDismissed(), FString::Printf(TEXT("L3 персонаж B: гид с нуля, шаг %d"), T->GetGuideStep()));
			R->Check(!T->IsSectionDone(2) && !T->IsSectionDone(R->SecondSection), TEXT("L3 персонаж B: разделы A не отмечены"));
			X::SetStep(T, 5);
			R->Next();
			return true;
		case 19:
			if (!R->Waited(0.5)) { return true; }   // тик гида: смена шага → запись
			R->PathB = X::StorePath(T);
			R->Check(R->PathB != R->PathA && FFileHelper::LoadFileToArray(R->FileB, *R->PathB) && R->FileB.Num() == 88, TEXT("L3 у B свой файл"));
			X::FullReset(T);
			Net->SelfLevel = 1;
			X::WorldReady(T);
			R->Check(T->IsGuideActive() && T->GetGuideStep() == 5, FString::Printf(TEXT("L3 B после перезапуска продолжает с шага %d (ждём 5)"), T->GetGuideStep()));
			T->CloseGuide(TEXT("самотест"));
			// L4: подделка файла A (байт шифротекста)
			{
				TArray<uint8> Bad = R->FileA;
				Bad[30] ^= 0x01;
				FFileHelper::SaveArrayToFile(Bad, *R->PathA);
			}
			X::FullReset(T);
			Net->SelectedCharId = 12345;
			X::WorldReady(T);
			R->Check(T->IsGuideActive() && T->GetGuideStep() == 1 && !T->IsGuideDismissed() && !T->IsSectionDone(R->SecondSection),
				FString::Printf(TEXT("L4 подделанный файл A — тихий сброс: гид с шага %d, отметок нет"), T->GetGuideStep()));
			T->CloseGuide(TEXT("самотест"));
			// L5а: файл B под именем A
			FFileHelper::SaveArrayToFile(R->FileB, *R->PathA);
			X::FullReset(T);
			X::WorldReady(T);
			R->Check(T->IsGuideActive() && T->GetGuideStep() == 1, FString::Printf(TEXT("L5 файл B под именем A отвергнут: шаг %d"), T->GetGuideStep()));
			T->CloseGuide(TEXT("самотест"));
			// L5б: подлинный файл A под другой учёткой (тот же charId)
			FFileHelper::SaveArrayToFile(R->FileA, *R->PathA);
			X::FullReset(T);
			Net->PendingSessionAccount = TEXT("selftest_other");
			FFileHelper::SaveArrayToFile(R->FileA, *X::StorePath(T));
			X::WorldReady(T);
			R->Check(T->IsGuideActive() && T->GetGuideStep() == 1 && !T->IsSectionDone(R->SecondSection),
				FString::Printf(TEXT("L5 файл A под другой учёткой отвергнут: шаг %d"), T->GetGuideStep()));
			T->CloseGuide(TEXT("самотест"));
			// Подлинный A на своей учётке читается снова (подмены не испортили хранилище).
			X::FullReset(T);
			Net->PendingSessionAccount = TEXT("selftest_a");
			X::WorldReady(T);
			R->Check(!T->IsGuideActive() && T->IsGuideDismissed() && T->IsSectionDone(R->SecondSection), TEXT("L5 подлинный файл A на своей учётке читается"));
			R->Next();
			return true;
		default:
			return false;
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestTutorial(
	TEXT("asda.SelfTest.Tutorial"),
	TEXT("Самотест обучения: гид 1950 (шаги, флаги, крестик), книга с «?» (6185/6184, «КОНЕЦ», «Закрыть») и локальный прогресс на персонажа (A/B, перезапуск, подделка). [out=<папка>] [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaTutorialTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 90.f)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!GI || !Net || !GI->GetSubsystem<UAsdaTutorialSubsystem>()) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем")); return; }
		TSharedRef<FRun> R = MakeShared<FRun>();
		R->GI = GI; R->World = W; R->bQuit = KA.bQuit;
		for (const FString& A : KA.Pos) { if (A.StartsWith(TEXT("out="))) { R->OutDir = A.Mid(4); } }
		R->Keys = MakeUnique<FAsdaCVarScope>(W, kCmd);
		R->Keys->Set(TEXT("asda.Tutorial.LocalProgress"), 1);
		R->Self = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		Net->SelfUserNum = 321; Net->bSelfUserNumKnown = true; Net->SelectedCharId = 12345;
		// Хранилище самотеста — свой каталог (стирается), учётка «selftest_a»; память обучения с нуля.
		R->SavedAccount = Net->PendingSessionAccount;
		Net->PendingSessionAccount = TEXT("selftest_a");
		R->StoreDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AsdaSelfTest") / TEXT("TutorialStore"));
		IFileManager::Get().DeleteDirectory(*R->StoreDir, false, true);
		if (UAsdaTutorialSubsystem* T0 = GI->GetSubsystem<UAsdaTutorialSubsystem>())
		{
			FAsdaTutorialSelfTest::SetStoreDir(T0, R->StoreDir);
			FAsdaTutorialSelfTest::FullReset(T0);
		}
		TSharedRef<TArray<FSent>> Sent = R->Sent;
		R->Gen = AsdaSelfTestKit::ArmCapture(Net, [Sent](uint16, const uint8* D, int32 Len, bool, TCHAR)
		{
			// Перехват — полезная нагрузка 8 б (`SendMenuPacket` отдаёт стенду тело ДО конверта).
			if (D && Len >= 8) { FSent S; S.CharId = AsdaBytes::RdI32(D); S.UserNum = (uint16)AsdaBytes::RdI16(D + 4); S.Id = (uint16)AsdaBytes::RdI16(D + 6); Sent->Add(S); }
		}, 6184, kCmd);
		R->Check(R->Gen != 0, TEXT("перехват 6184 взведён"));
		R->PhaseT0 = R->Now();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([R, W](float)
		{
			if (Step(R)) { return true; }
			const TSharedRef<FRun>& X = R;
			if (!X->OutDir.IsEmpty())
			{
				IFileManager::Get().MakeDirectory(*X->OutDir, true);
				for (const FString& S : X->Shots)
				{
					const FString Src = FPaths::ScreenShotDir() / (S + TEXT(".png"));
					const FString Dst = X->OutDir / (S + TEXT("_") + FAsdaLang::Code(FAsdaLang::Current()) + TEXT(".png"));
					X->Check(IFileManager::Get().Copy(*Dst, *Src) == COPY_OK, FString::Printf(TEXT("кадр %s"), *Dst));
				}
			}
			if (UAsdaTutorialSubsystem* T = X->Tut())
			{
				T->CloseGuide(TEXT("самотест окончен"));
				FAsdaTutorialSelfTest::FullReset(T);          // память самотеста не протекает в игру
				FAsdaTutorialSelfTest::SetStoreDir(T, FString());
			}
			if (UAsdaNetSubsystem* N = X->Sub<UAsdaNetSubsystem>()) { N->PendingSessionAccount = X->SavedAccount; }
			IFileManager::Get().DeleteDirectory(*X->StoreDir, false, true);
			if (UAsdaNetSubsystem* N = X->Sub<UAsdaNetSubsystem>()) { AsdaSelfTestKit::DisarmCapture(N, X->Gen); }
			X->Self.Reset();
			X->Keys.Reset();
			AsdaSelfTestKit::Finish(W, kCmd, X->bQuit, LogAsdaTutorialTest, kPfx, X->Ok, X->Fail);
			return false;
		}), 0.1f);
	}));
#endif
