// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «СВИТОК СБРОСА НАВЫКОВ → ПАНЕЛЬ БЫСТРОГО ДОСТУПА» (26.10.2026): `asda.SelfTest.SkillResetHotbar [quit]`. [ИСПРАВЛЕНИЕ]
//   Жалоба игроков: «All Skill Reset Scroll resets skill points but leaves hotbar skill icons populated/functional».
//   Путь игрока, без сети (-AsdaNoNet), карта мира (нет павна — офлайн-спавн штатными крюками). Ответы сервера подаются штатным
//   разбором `UAsdaNetSubsystem::DebugFeedPacket` в той же последовательности, что шлёт сервер (GameServer.c: полный сброс
//   `sub_4803A0` — 5462 → 6057 → 5251; ветка `sub_480F20` — 5464 → 6057 → 5251; изучение — 5254). Навыки кладутся на панель
//   `AssignSkillToSlot` (точка броска из книги), применяются `ActivateSlot` (точка клика/клавиши), иконка сверяется и в модели
//   слота, и в КИСТИ картинки слота окна QUICK_NEW_WIN (то, что видит игрок).
//     A. Три выученных навыка (A, B, C) на слотах 1–3 текущей страницы — иконки обычные; применение A не отбито гейтом уровня.
//     B. Частичный сброс ветки (A и C → уровень 0, B не тронут): A и C серые «-g», B обычный; A отбит гейтом уровня (нотис 1018),
//        B — нет; раскладка (5067) не ушла.
//     C. Изучение A (5254, уровень 1): A снова обычный, C серый.
//     D. Полный сброс (все → 0): все три серые; раскладка не ушла.
//     E. Ключ asda.Quick.GreyUnlearnedSkill 0 — прежнее поведение: после того же сброса все три обычные (дефект воспроизведён).
//     F. Список ещё не пришёл (6057 без 5251, модель пуста) — слоты НЕ гасятся (нет ложной серости до прихода списка).
//   Итог — «СБРОС-ХОТБАР ИТОГ: ok=… fail=…». Слоты 1–3 возвращаются как были; модель навыков в конце очищается (6057).
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaSkillDatabase.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Resources/AsdaAssetPaths.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaResetHotbarTest, Log, All);

namespace AsdaResetHotbarTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.SkillResetHotbar");
	static constexpr int32 kN = 3;   // навыков в прогоне (A, B, C)

	// Перехватчик строк гейта уровня `UAsdaSkillUseSubsystem::CheckGates` («[навыки] <код> «…»: навык не выучен (уровень 0)»).
	class FSink : public FOutputDevice
	{
	public:
		TArray<int32> LevelRefused;   // коды навыков, отбитых гейтом уровня
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName& Category) override
		{
			if (!IsInGameThread()) { return; }   // ★ 05.11.2026: устройство синхронное (CanBeUsedOnMultipleThreads) — ловим только строки игрового потока: их пишет проверяемый код, читает самотест, замка у полей нет
			static const FName kUse(TEXT("LogAsdaSkillUse"));
			if (!V || Category != kUse || !FCString::Strstr(V, TEXT("навык не выучен (уровень 0)"))) { return; }
			const TCHAR* P = FCString::Strstr(V, TEXT("[навыки] "));
			if (P) { LevelRefused.Add(FCString::Atoi(P + 9)); }
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }   // ★ 05.11.2026: НЕбуферизованный список UE 5.8 — строка доходит в момент записи (иначе гонка с потоком логов: строка после RemoveOutputDevice/проверки)
	};

	// Запись списка 5251 (34 б) — раскладка живого пакета (журнал Player52 22.10 19:50:49: `BD 02 00 01 FF FF FF FF 00 00 FF FF …`).
	static void AddRec(TArray<uint8>& B, int32 Code, int32 Level)
	{
		const int32 O = B.Num();
		B.AddUninitialized(34);
		FMemory::Memset(B.GetData() + O, 0xFF, 34);
		B[O + 0] = (uint8)(Code & 0xFF); B[O + 1] = (uint8)((Code >> 8) & 0xFF);
		B[O + 2] = (uint8)Level;
		B[O + 3] = 1;                    // «можно вкачивать»
		B[O + 8] = 0; B[O + 9] = 0;
		B[O + 12] = 0; B[O + 13] = 0; B[O + 14] = 0; B[O + 15] = 0;
	}
}

struct FAsdaSkillResetHotbarSelfTest
{
	struct FRun
	{
		TWeakObjectPtr<UWorld> W;
		bool bQuit = false;
		int32 Ok = 0, Fail = 0;
		int32 Waits = 0;
		int32 Codes[AsdaResetHotbarTestLocal::kN] = { 0, 0, 0 };    // A, B, C
		FString Icons[AsdaResetHotbarTestLocal::kN];                // обычные имена иконок
		int32 Abs[AsdaResetHotbarTestLocal::kN] = { -1, -1, -1 };  // слоты 1–3 текущей страницы
		FAsdaHotbarSlot Saved[AsdaResetHotbarTestLocal::kN];        // что было в слотах до прогона
		uint32 CapGen = 0;
		int32 Sent5067 = 0, Sent5256 = 0;
		AsdaResetHotbarTestLocal::FSink Sink;
		TUniquePtr<FAsdaCVarScope> Keys;
		~FRun() { if (GLog) { GLog->RemoveOutputDevice(&Sink); } }
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaResetHotbarTest, Log, TEXT("СБРОС-ХОТБАР ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaResetHotbarTest, Error, TEXT("СБРОС-ХОТБАР FAIL %s"), *What); }
		}
	};
	using FRef = TSharedRef<FRun>;

	template<class T> static T* Sub(const FRef& R)
	{
		UWorld* W = R->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<T>() : nullptr;
	}
	static void After(const FRef& R, float Sec, TFunction<void(const FRef&)> Fn)
	{
		UWorld* W = R->W.Get();
		if (!W) { return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([R, Fn = MoveTemp(Fn)]() { Fn(R); }), Sec, false);
	}
	static void Feed(const FRef& R, int32 Op, const TArray<uint8>& B)
	{
		if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R)) { Net->DebugFeedPacket(Op, B); }
	}
	// Ответ сервера на сброс: заголовок (5462 очки / 5464 очки+ветка), 6057, новый список 5251 с уровнями `Lv`.
	static void FeedReset(const FRef& R, int32 HeadOp, const int32 (&Lv)[AsdaResetHotbarTestLocal::kN])
	{
		TArray<uint8> H;
		if (HeadOp == 5462) { H = { 6, 0 }; }
		else if (HeadOp == 5464) { H = { 5, 0, 1 }; }
		if (HeadOp) { Feed(R, HeadOp, H); }
		Feed(R, 6057, TArray<uint8>());
		TArray<uint8> L;
		for (int32 i = 0; i < AsdaResetHotbarTestLocal::kN; ++i) { AsdaResetHotbarTestLocal::AddRec(L, R->Codes[i], Lv[i]); }
		Feed(R, 5251, L);
		if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(R)) { WM->ClearMsgBoxQueue(TEXT("самотест SkillResetHotbar")); }
	}
	// Картинка слота столбца `Col` основного ряда (узел JUI `QUICK_NEW_SLOT<Col+1>`, последний из совпавших — как разбор панели).
	static UImage* SlotImage(const FRef& R, int32 Col)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(R);
		UUserWidget* Win = WM ? WM->FindWindow(FName(TEXT("QUICK_NEW_WIN"))) : nullptr;
		if (!Win || !Win->WidgetTree) { return nullptr; }
		TArray<UWidget*> All;
		Win->WidgetTree->GetAllWidgets(All);
		const FString Want = FString::Printf(TEXT("QUICK_NEW_SLOT%d"), Col + 1);
		UImage* Found = nullptr;
		for (UWidget* Wd : All)
		{
			FString Idc; int32 J = 0, P = 0;
			if (Wd && AsdaUi::ParsePlaneName(Wd->GetName(), Idc, J, P) && Idc == Want)
			{
				if (UImage* Img = Cast<UImage>(Wd)) { Found = Img; }
			}
		}
		return Found;
	}
	static FString BrushName(const FRef& R, int32 Col)
	{
		const UImage* Img = SlotImage(R, Col);
		const UObject* Res = Img ? Img->GetBrush().GetResourceObject() : nullptr;
		return Res ? Res->GetName() : FString(TEXT("<нет>"));
	}
	// Иконка слота i: в модели и в кисти — обычная (`bGrey == false`) или серая «-g».
	static void ExpectIcon(const FRef& R, int32 i, bool bGrey, const TCHAR* Step)
	{
		const UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R);
		const FString Want = bGrey ? (R->Icons[i] + TEXT("-g")) : R->Icons[i];
		const FString Model = Hb ? Hb->GetSlot(R->Abs[i]).IconAtlas : FString();
		const FString Brush = BrushName(R, i);
		R->Expect(Model.Equals(Want, ESearchCase::IgnoreCase) && Brush.Equals(Want, ESearchCase::IgnoreCase),
			FString::Printf(TEXT("%s слот %d (навык %d): %s — модель «%s», кисть «%s» (ожидание «%s»)"), Step, i + 1, R->Codes[i],
				bGrey ? TEXT("серый") : TEXT("обычный"), *Model, *Brush, *Want));
	}
	// Применить слот i путём игрока и сказать, отбил ли его гейт уровня.
	static bool UseRefusedByLevel(const FRef& R, int32 i)
	{
		UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R);
		if (!Hb) { return false; }
		GLog->Flush();
		R->Sink.LevelRefused.Reset();
		Hb->ActivateSlot(R->Abs[i]);
		GLog->Flush();
		return R->Sink.LevelRefused.Contains(R->Codes[i]);
	}

	static void Start(UWorld* W, bool bQuit);
	static void WaitHotbar(const FRef& R);
	static void Run(const FRef& R);
	static void Finish(const FRef& R);
};

void FAsdaSkillResetHotbarSelfTest::Start(UWorld* W, bool bQuit)
{
	using namespace AsdaResetHotbarTestLocal;
	FRef R = MakeShared<FRun>();
	R->W = W; R->bQuit = bQuit;
	UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("нет сетевой подсистемы")); return; }
	if (Net->IsConnected()) { AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("живое соединение")); return; }
	if (AsdaSelfTestKit::NeedSelfSpawn(W) && GEngine)
	{
		GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
		GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
	}
	WaitHotbar(R);
}

// Панель создаёт её тик, когда есть номер карты и свой павн; мост к книге навыков ставится тем же тиком — ждём до 20 с.
void FAsdaSkillResetHotbarSelfTest::WaitHotbar(const FRef& R)
{
	using namespace AsdaResetHotbarTestLocal;
	UWorld* W = R->W.Get();
	if (!W) { return; }
	UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R);
	if (!Hb || !Hb->bSkillBridgeBound || !SlotImage(R, 0))
	{
		if (++R->Waits > 40) { AsdaSelfTestKit::Refused(W, kCmd, R->bQuit, TEXT("нет панели QUICK_NEW_WIN / моста навыков за 20 с")); return; }
		After(R, 0.5f, [](const FRef& R) { WaitHotbar(R); });
		return;
	}
	Run(R);
}

void FAsdaSkillResetHotbarSelfTest::Run(const FRef& R)
{
	using namespace AsdaResetHotbarTestLocal;
	UWorld* W = R->W.Get();
	UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R);
	UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
	const UAsdaSkillWindowSubsystem* SW = Sub<UAsdaSkillWindowSubsystem>(R);
	const UAsdaSkillDatabase* Db = SW ? SW->GetDatabase() : nullptr;
	UAsdaTextureSubsystem* Tex = AsdaTexMgr();
	if (!Hb || !Net || !Db || !Tex) { AsdaSelfTestKit::Refused(W, kCmd, R->bQuit, TEXT("нет панели/сети/базы навыков/текстур")); return; }

	// Три применимых навыка (вид 1/2, подтип не 3/4, код книги ≤ 1201) с обычной и серой иконкой — в порядке кодов.
	TArray<int32> Cand;
	for (const TPair<int32, FAsdaSkillDef>& It : Db->GetAll())
	{
		const FAsdaSkillDef& D = It.Value;
		if (It.Key < 501 || It.Key > 1201 || D.IconName.IsEmpty() || (D.Kind != 1 && D.Kind != 2) || D.SubType == 3 || D.SubType == 4) { continue; }
		Cand.Add(It.Key);
	}
	Cand.Sort();
	int32 Got = 0;
	for (int32 Code : Cand)
	{
		if (Got >= kN) { break; }
		const FString Ic = Db->Get(Code)->IconName;
		if (!Tex->LoadTextureCached(AsdaAssetPaths::JuiTextureObject(Ic)) || !Tex->LoadTextureCached(AsdaAssetPaths::JuiTextureObject(Ic + TEXT("-g")))) { continue; }
		R->Codes[Got] = Code; R->Icons[Got] = Ic; ++Got;
	}
	if (Got < kN) { AsdaSelfTestKit::Refused(W, kCmd, R->bQuit, TEXT("в базе нет трёх применимых навыков с серой иконкой")); return; }

	GLog->AddOutputDevice(&R->Sink);
	R->Keys = MakeUnique<FAsdaCVarScope>(W, kCmd);
	R->Keys->Set(TEXT("asda.Quick.GreyUnlearnedSkill"), 1);
	TWeakPtr<FRun> WR = R;
	R->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WR](uint16 Op, const uint8*, int32, bool, TCHAR)
	{
		TSharedPtr<FRun> Run = WR.Pin();
		if (!Run) { return; }
		if (Op == 5067) { ++Run->Sent5067; }
		if (Op == 5256) { ++Run->Sent5256; }
	}, 0, kCmd);
	if (R->CapGen == 0) { AsdaSelfTestKit::Refused(W, kCmd, R->bQuit, TEXT("перехват отправок занят")); return; }

	for (int32 i = 0; i < kN; ++i)
	{
		R->Abs[i] = Hb->AbsSlot(i, false);
		R->Saved[i] = Hb->GetSlot(R->Abs[i]);
	}
	UE_LOG(LogAsdaResetHotbarTest, Log, TEXT("СБРОС-ХОТБАР навыки A=%d «%s», B=%d «%s», C=%d «%s»; слоты %d/%d/%d"),
		R->Codes[0], *R->Icons[0], R->Codes[1], *R->Icons[1], R->Codes[2], *R->Icons[2], R->Abs[0], R->Abs[1], R->Abs[2]);

	// A. Выученные навыки на панели.
	{ const int32 Lv[kN] = { 3, 2, 1 }; FeedReset(R, 0, Lv); }
	After(R, 0.3f, [](const FRef& R)
	{
		UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R);
		for (int32 i = 0; i < kN; ++i)
		{
			R->Expect(Hb && Hb->AssignSkillToSlot(R->Abs[i], R->Codes[i]), FString::Printf(TEXT("A0 навык %d → слот %d"), R->Codes[i], R->Abs[i]));
		}
		After(R, 0.3f, [](const FRef& R)
		{
			for (int32 i = 0; i < kN; ++i) { ExpectIcon(R, i, false, TEXT("A1 выучено:")); }
			FString Why;
			const UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R);
			R->Expect(Hb && !Hb->IsActivationBlockedForCheck(Why), FString::Printf(TEXT("A2 панель принимает нажатия (%s)"), *Why));
			R->Expect(!UseRefusedByLevel(R, 0), TEXT("A3 нажатие A (уровень 3) гейтом уровня не отбито"));
			const int32 Base5067 = R->Sent5067;

			// B. Частичный сброс ветки: A и C → 0, B не тронут.
			{ const int32 Lv[kN] = { 0, 2, 0 }; FeedReset(R, 5464, Lv); }
			After(R, 0.4f, [Base5067](const FRef& R)
			{
				ExpectIcon(R, 0, true, TEXT("B1 сброс ветки:"));
				ExpectIcon(R, 1, false, TEXT("B2 сброс ветки, другая ветка:"));
				ExpectIcon(R, 2, true, TEXT("B3 сброс ветки:"));
				R->Expect(UseRefusedByLevel(R, 0), TEXT("B4 нажатие серого A отбито гейтом уровня (нотис 1018), заявки нет"));
				After(R, 0.3f, [Base5067](const FRef& R)
				{
					R->Expect(!UseRefusedByLevel(R, 1), TEXT("B5 нажатие B (ветка не сброшена) гейтом уровня не отбито"));
					R->Expect(R->Sent5067 == Base5067, FString::Printf(TEXT("B6 раскладка панели на сервер не ушла (5067: %d)"), R->Sent5067 - Base5067));

					// C. Изучение A заново (ответ 5254: успех, очки 4, навык A, уровень 1).
					TArray<uint8> U; U.SetNumZeroed(111);
					U[0] = 1; U[1] = 4; U[3] = (uint8)(R->Codes[0] & 0xFF); U[4] = (uint8)((R->Codes[0] >> 8) & 0xFF); U[5] = 1;
					Feed(R, 5254, U);
					After(R, 0.3f, [Base5067](const FRef& R)
					{
						ExpectIcon(R, 0, false, TEXT("C1 A изучен снова:"));
						ExpectIcon(R, 2, true, TEXT("C2 C по-прежнему не выучен:"));

						// D. Полный сброс.
						{ const int32 Lv[kN] = { 0, 0, 0 }; FeedReset(R, 5462, Lv); }
						After(R, 0.4f, [Base5067](const FRef& R)
						{
							for (int32 i = 0; i < kN; ++i) { ExpectIcon(R, i, true, TEXT("D1 полный сброс:")); }
							R->Expect(R->Sent5067 == Base5067, FString::Printf(TEXT("D2 раскладка панели на сервер не ушла (5067: %d)"), R->Sent5067 - Base5067));

							// E. Ключ 0 — прежнее поведение (слоты обычные после того же сброса).
							R->Keys->Set(TEXT("asda.Quick.GreyUnlearnedSkill"), 0);
							{ const int32 Lv[kN] = { 0, 0, 0 }; FeedReset(R, 5462, Lv); }
							After(R, 0.4f, [](const FRef& R)
							{
								for (int32 i = 0; i < kN; ++i) { ExpectIcon(R, i, false, TEXT("E1 ключ 0 (как прежде):")); }
								R->Keys->Set(TEXT("asda.Quick.GreyUnlearnedSkill"), 1);

								// F. Список не пришёл (6057 без 5251): модель пуста — ложной серости нет.
								Feed(R, 6057, TArray<uint8>());
								After(R, 0.4f, [](const FRef& R)
								{
									for (int32 i = 0; i < kN; ++i) { ExpectIcon(R, i, false, TEXT("F1 списка нет:")); }
									Finish(R);
								});
							});
						});
					});
				});
			});
		});
	});
}

void FAsdaSkillResetHotbarSelfTest::Finish(const FRef& R)
{
	using namespace AsdaResetHotbarTestLocal;
	if (UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R))
	{
		for (int32 i = 0; i < kN; ++i)
		{
			const FAsdaHotbarSlot& S = R->Saved[i];
			if (R->Abs[i] < 0) { continue; }
			if (S.IsEmpty()) { Hb->ClearSlot(R->Abs[i]); }
			else { Hb->SetSlotContent(R->Abs[i], S.Type, S.SubType, S.Index, S.Count, S.Id); }
		}
	}
	if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R)) { AsdaSelfTestKit::DisarmCapture(Net, R->CapGen); }
	if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(R)) { WM->ClearMsgBoxQueue(TEXT("самотест SkillResetHotbar")); }
	if (R->Keys) { R->Keys->Restore(); }
	GLog->Flush();
	GLog->RemoveOutputDevice(&R->Sink);
	AsdaSelfTestKit::Finish(R->W.Get(), kCmd, R->bQuit, LogAsdaResetHotbarTest, TEXT("СБРОС-ХОТБАР"), R->Ok, R->Fail);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestSkillResetHotbar(
	TEXT("asda.SelfTest.SkillResetHotbar"),
	TEXT("Самотест: свиток сброса навыков (полный 5462 и ветки 5464) → панель быстрого доступа: сброшенные навыки серые «-g», "
	     "нажатие отбито гейтом уровня, изучение зажигает слот, раскладка не уходит; ключ asda.Quick.GreyUnlearnedSkill 0. "
	     "Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, AsdaResetHotbarTestLocal::kCmd, A)) { return; }
		if (!W) { return; }
		if (A.Delay <= 0.f) { FAsdaSkillResetHotbarSelfTest::Start(W, A.bQuit); return; }
		TWeakObjectPtr<UWorld> WW = W;
		const bool bQuit = A.bQuit;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, bQuit]()
		{
			if (WW.Get() && AsdaSelfTestKit::RefuseDeferred(WW.Get(), AsdaResetHotbarTestLocal::kCmd, bQuit)) { return; }
			if (UWorld* Wd = WW.Get()) { FAsdaSkillResetHotbarSelfTest::Start(Wd, bQuit); }
		}), A.Delay, false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
