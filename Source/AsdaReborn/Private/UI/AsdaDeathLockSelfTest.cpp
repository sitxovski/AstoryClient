// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ 05.11.2026 САМОТЕСТ «МЁРТВЫЙ: ЗАМОК ВВОДА, Ч/Б НЕ НА ОКНАХ, СВИТОК В МЕНЮ» — `asda.SelfTest.DeathLock [quit]`.
//   Баг владельца 05.11 (три части): (1) мёртвому работают горячие клавиши и кнопки меню; (2) ч/б смерти ложится на окна
//   (гача); (3) премиум-свиток воскрешения — только двойным кликом, предложения в меню смерти нет.
//   ПУТЬ ИГРОКА, офлайн (-AsdaNoNet): офлайн-спавн на Альпене → настоящие пакеты сервера через `DebugFeedPacket`
//   (5272 состояние 189 Resuscitate на себя, 4053 своя смерть, 4056 воскрешение), исходящие — перехватом (4054, 5450),
//   клавиши — `InputKey` контроллера (тот же путь, что физическая клавиатура до `PollKeys`), кнопка системного меню —
//   `ClickButton` (тот же обработчик, что клик мышью), кадры сцены гачи — чтение её render-target + PNG в Saved/Screenshots.
//   Разбор эталона — `UI/AsdaDeathInputLock.h`, `UI/AsdaCaptureGradeIsolation.h`, `AsdaAfterDeathSubsystem.cpp` (опции 3/5).
//   Итог: «СМЕРТЬ-ЗАМОК ИТОГ: ok=N fail=M». EN — `--lang en` пакетного прогона (тексты опций сверяются с таблицей языка).
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "AsdaTestPackets.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaAfterDeathSubsystem.h"
#include "UI/AsdaDeathInputLock.h"
#include "UI/AsdaCaptureGradeIsolation.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaSystemMenuSubsystem.h"
#include "UI/AsdaPremiumGachaSubsystem.h"
#include "UI/AsdaGachaSceneActor.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaNotice.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Text/AsdaLang.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaKeybindConfig.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "EngineUtils.h"
#include "ImageUtils.h"                    // FImageUtils::PNGCompressImageArray — кадр RT в PNG
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "RenderingThread.h"               // FlushRenderingCommands — кадр захвата до чтения RT
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaDeathLockTest, Log, All);

namespace AsdaDeathLockSelfTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.DeathLock");   // имя команды (итог, отказ, ключи)
	static constexpr uint16 kOp4054 = 4054;                       // C→S воскрешение (режим)
	static constexpr uint16 kOp5450 = 5450;                       // C→S применение премиум-предмета (свиток)
	static constexpr int32  kInvCmd = 33;                         // команда раскладки INTERFACE INVENTORY

	// Один перехваченный исходящий пакет.
	struct FSent { uint16 Op = 0; TArray<uint8> Body; };

	// Состояние прогона (шаги идут таймерами — клавиши `PollKeys` видит по кадрам).
	struct FState
	{
		TWeakObjectPtr<UWorld> W;
		FAsdaSelfTestArgs Args;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		TUniquePtr<FAsdaCVarScope> Cv;
		TArray<FSent> Sent;
		uint32 CapGen = 0;
		int32 Ok = 0, Fail = 0;
		int32 Uidx = 7;
		int32 ScrollCode = 0, ScrollCell = -1;
		FKey InvKey;
		int32 Idx3 = -1, Idx5 = -1;
		float ChromaOn = -1.f, ChromaOff = -1.f;
		TArray<TPair<float, TFunction<void(FState&)>>> Steps;
		int32 Next = 0;

		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
		AAsdaPlayerController* PC() const { UGameInstance* G = GI(); return G ? Cast<AAsdaPlayerController>(G->GetFirstLocalPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Me() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaDeathLockTest, Log, TEXT("СМЕРТЬ-ЗАМОК ok   %s"), *What); }
			else { ++Fail; UE_LOG(LogAsdaDeathLockTest, Error, TEXT("СМЕРТЬ-ЗАМОК FAIL %s"), *What); }
		}
		int32 Count(uint16 Op) const { int32 N = 0; for (const FSent& S : Sent) { if (S.Op == Op) { ++N; } } return N; }
		void Then(float Delay, TFunction<void(FState&)> F) { Steps.Emplace(Delay, MoveTemp(F)); }
	};

	// 4053 (4 б): userIdx i16, убийца i16 (`SendCharDead@0x4CB480`).
	using AsdaTestPackets::Body4053;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.
	// 4056 (22 б) — `CharRegenSelfPosition`: uidx, тайл X/Z, HP i32, MP i16, exp i64, @20 состояние 0.
	static TArray<uint8> Body4056(int32 Uidx, int32 X, int32 Z, int32 Hp, int32 Mp)
	{
		TArray<uint8> B; B.SetNumZeroed(22);
		AsdaBytes::WrI16(&B[0], (int16)Uidx); AsdaBytes::WrI16(&B[2], (int16)X); AsdaBytes::WrI16(&B[4], (int16)Z);
		AsdaBytes::WrI32(&B[6], Hp); AsdaBytes::WrI16(&B[10], (int16)Mp);
		return B;
	}
	// 5272 (27 б) — наложение скиллового состояния (раскладка — `AsdaBuffStatsSelfTest`): @0 userIdx, @2/@4 состояние.
	static TArray<uint8> Body5272(int32 Uidx, int32 State, int32 Sec)
	{
		TArray<uint8> B; B.SetNumZeroed(27);
		AsdaBytes::WrI16(&B[0], (int16)Uidx); AsdaBytes::WrI16(&B[2], (int16)State); AsdaBytes::WrI16(&B[4], (int16)State);
		AsdaBytes::WrI16(&B[6], 598); B[8] = 1; B[10] = 2; AsdaBytes::WrI16(&B[11], (int16)Sec); B[13] = 1; AsdaBytes::WrI16(&B[14], 0);
		return B;
	}
	// 5273 (9 б) — снятие состояния: @0 userIdx, @2 состояние.
	static TArray<uint8> Body5273(int32 Uidx, int32 State)
	{
		TArray<uint8> B; B.SetNumZeroed(9); AsdaBytes::WrI16(&B[0], (int16)Uidx); AsdaBytes::WrI16(&B[2], (int16)State); return B;
	}

	// Средняя «цветность» кадра (max−min канала) по непустым пикселям render-target. −1 — кадра нет.
	//   Независимая от боевого кода метрика: серый кадр даёт ≈0, цветные автоматы гачи — десятки.
	static float FrameChroma(UTextureRenderTarget2D* Rt, int32& OutLit, const TCHAR* SaveName = nullptr)
	{
		OutLit = 0;
		FlushRenderingCommands();   // захват кадра должен дойти до RT до чтения
		FTextureRenderTargetResource* Res = Rt ? Rt->GameThread_GetRenderTargetResource() : nullptr;
		TArray<FColor> Px;
		const bool bRead = Res && Res->ReadPixels(Px);
		UE_LOG(LogAsdaDeathLockTest, Log, TEXT("СМЕРТЬ-ЗАМОК кадр RT: rt=%s res=%s формат=%d прочитано=%d пикселей=%d"),
			Rt ? *Rt->GetName() : TEXT("нет"), Res ? TEXT("есть") : TEXT("нет"), Rt ? (int32)Rt->RenderTargetFormat : -1, bRead ? 1 : 0, Px.Num());
		if (!bRead || Px.Num() == 0) { return -1.f; }
		if (SaveName)
		{
			// Кадр — в постоянную папку проекта (пакетный прогон чистит `Saved_<суффикс>`): AstoryClient/Saved/Screenshots/deathlock/.
			TArray<FColor> Opaque = Px; for (FColor& C : Opaque) { C.A = 255; }
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Rt->SizeX, Rt->SizeY, TArrayView64<const FColor>(Opaque.GetData(), Opaque.Num()), Png);
			const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()) / TEXT("Saved/Screenshots/deathlock");
			IFileManager::Get().MakeDirectory(*Dir, true);
			const FString File = Dir / FString::Printf(TEXT("%s_%s.png"), SaveName, FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en"));
			const bool bSaved = FFileHelper::SaveArrayToFile(Png, *File);
			UE_LOG(LogAsdaDeathLockTest, Log, TEXT("СМЕРТЬ-ЗАМОК кадр сохранён: %s (%s)"), *File, bSaved ? TEXT("да") : TEXT("НЕТ"));
		}
		double Sum = 0.0;
		for (const FColor& C : Px)
		{
			const int32 Mx = FMath::Max3<int32>(C.R, C.G, C.B), Mn = FMath::Min3<int32>(C.R, C.G, C.B);
			if (Mx < 24) { continue; }   // тёмный/пустой фон
			Sum += (double)(Mx - Mn); ++OutLit;
		}
		return OutLit > 0 ? (float)(Sum / OutLit) : 0.f;
	}

	static void Step(TSharedRef<FState> S);

	static void Finish(FState& S)
	{
		UGameInstance* GI = S.GI();
		if (UAsdaPremiumGachaSubsystem* G = S.Sub<UAsdaPremiumGachaSubsystem>()) { if (G->IsSceneOpen()) { G->CloseScene(); } }
		if (UAsdaInventorySubsystem* Inv = S.Sub<UAsdaInventorySubsystem>())
		{
			if (S.ScrollCell >= 0) { Inv->DebugPutItem(1, S.ScrollCell, 0, 0); }
		}
		if (UAsdaNetSubsystem* Net = S.Sub<UAsdaNetSubsystem>())
		{
			Net->DebugFeedPacket(5273, Body5273(S.Uidx, 189));
			if (S.CapGen) { AsdaSelfTestKit::DisarmCapture(Net, S.CapGen); S.CapGen = 0; }
		}
		if (UAsdaWindowSubsystem* WM = S.Sub<UAsdaWindowSubsystem>()) { WM->ClearMsgBoxQueue(TEXT("самотест замка смерти: конец")); }
		if (S.SelfScope) { S.SelfScope->Restore(); }
		if (S.Cv) { S.Cv->Restore(); }
		(void)GI;
		AsdaSelfTestKit::Finish(S.W.Get(), kCmd, S.Args.bQuit, LogAsdaDeathLockTest, TEXT("СМЕРТЬ-ЗАМОК"), S.Ok, S.Fail);
	}

	static void Step(TSharedRef<FState> S)
	{
		UWorld* W = S->W.Get();
		if (!W) { return; }
		if (S->Next >= S->Steps.Num()) { Finish(*S); return; }
		const float Delay = S->Steps[S->Next].Key;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([S]()
		{
			if (!S->W.IsValid()) { return; }
			TFunction<void(FState&)> F = S->Steps[S->Next].Value;
			++S->Next;
			F(*S);
			Step(S);
		}), FMath::Max(0.01f, Delay), false);
	}

	static void Build(TSharedRef<FState> S)
	{
		// ════════ 0. Подготовка: свой номер, перехват, свиток в сумке, Resuscitate, тексты ════════
		S->Then(0.05f, [](FState& T)
		{
			UAsdaNetSubsystem* Net = T.Sub<UAsdaNetSubsystem>();
			UAsdaInventorySubsystem* Inv = T.Sub<UAsdaInventorySubsystem>();
			UAsdaItemTableSubsystem* IT = T.Sub<UAsdaItemTableSubsystem>();
			T.SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
			T.Cv = MakeUnique<FAsdaCVarScope>(T.W.Get(), kCmd);
			T.Cv->Set(TEXT("asda.Death.InputLock"), 1);
			T.Cv->Set(TEXT("asda.Death.ScrollOption"), 1);
			T.Cv->Set(TEXT("asda.Death.ResuscitateOption"), 1);
			T.Cv->Set(TEXT("asda.Death.DesatWorldOnly"), 1);
			if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = 7; Net->bSelfUserNumKnown = true; }
			T.Uidx = (int32)Net->SelfUserNum;
			Net->SelfLevel = 50;          // > 20: опции 6 нет — набор эталона «3, 5, 4»
			if (Net->EnterMapId < 0 || Net->EnterMapId == 19 || Net->EnterMapId == 23) { Net->EnterMapId = 3; }
			T.CapGen = AsdaSelfTestKit::ArmCapture(Net, [&T](uint16 Op, const uint8* B, int32 N, bool, TCHAR)
			{
				FSent X; X.Op = Op; if (B && N > 0) { X.Body.Append(B, N); } T.Sent.Add(MoveTemp(X));
			}, 0, kCmd);
			T.Expect(T.CapGen != 0, TEXT("перехват исходящих взведён"));

			// Свитки воскрешения в таблице: special_itemlist.SubType == 48 (эталон `Table2Record->subType == 48`).
			TArray<int32> Scrolls;
			for (int32 Code = 1; Code < 2000; ++Code)
			{
				FAsdaSpecialItemlistRow Sp;
				if (!IT->GetItem(Code) && IT->GetSpecialItem(Code, Sp) && Sp.SubType == 48) { Scrolls.Add(Code); }
			}
			FString L; for (int32 C : Scrolls) { L += FString::Printf(TEXT("%d "), C); }
			T.Expect(Scrolls.Num() > 0, FString::Printf(TEXT("0: в special_itemlist есть свитки подтипа 48: %s(%d шт.)"), *L, Scrolls.Num()));
			T.ScrollCode = Scrolls.Num() ? Scrolls[0] : 0;
			// Свободная ячейка сумки (контейнер 1).
			for (int32 I = Inv->GetContainerSize(1) - 1; I >= 0; --I)
			{
				const FAsdaItemInstance* C = Inv->GetCell(1, I);
				if (!C || C->IsEmpty()) { T.ScrollCell = I; break; }
			}
			const bool bPut = T.ScrollCode > 0 && T.ScrollCell >= 0 && Inv->DebugPutItem(1, T.ScrollCell, T.ScrollCode, 1);
			T.Expect(bPut, FString::Printf(TEXT("0: свиток %d положен в сумку, ячейка %d"), T.ScrollCode, T.ScrollCell));

			// Resuscitate (состояние 189) на себе — настоящим пакетом наложения 5272.
			Net->DebugFeedPacket(5272, Body5272(T.Uidx, 189, 600));
			bool bHas189 = false;
			if (const UAsdaEffectsSubsystem* Eff = T.Sub<UAsdaEffectsSubsystem>())
			{ for (const FAsdaEffectEntry& E : Eff->GetSelfList()) { bHas189 |= (E.Id == 189 && E.Type == 0); } }
			T.Expect(bHas189, TEXT("0: на себе состояние 189 вида 0 (Resuscitate) после 5272"));

			// Тексты опций на языке сессии (таблица игры; новых строк нет).
			const bool bEn = !FAsdaLang::IsRu();
			for (const int32 Id : { 2381, 2376, 2370 })
			{
				const FString Tx = AsdaNotice::Loc(T.GI(), Id);
				T.Expect(!Tx.IsEmpty() && !Tx.StartsWith(TEXT("[#")), FString::Printf(TEXT("0: [%s] loc %d «%s»"), bEn ? TEXT("en") : TEXT("ru"), Id, *Tx));
			}

			// Клавиша команды 33 (инвентарь) по действующей раскладке.
			AAsdaPlayerController* P = T.PC();
			FAsdaKeyBind KB;
			if (P && P->Keys && P->Keys->GetBind(kInvCmd, KB) && KB.Vk != 0)
			{
				T.InvKey = UAsdaKeybindConfig::VkToFKey(KB.Vk);   // штатное правило раскладки VK → FKey
			}
			T.Expect(T.InvKey.IsValid(), FString::Printf(TEXT("0: клавиша команды 33 (инвентарь) = %s (VK %d)"), *T.InvKey.ToString(), KB.Vk));
			if (UAsdaInventorySubsystem* I2 = T.Sub<UAsdaInventorySubsystem>()) { if (I2->IsWindowOpen()) { P->RunCommandForCheck(kInvCmd); } }

			// ── Гача открыта ДО смерти (так видит владелец: окно открыто, персонаж умер) ──
			UAsdaPremiumGachaSubsystem* G = T.Sub<UAsdaPremiumGachaSubsystem>();
			T.Expect(G && G->OpenScene(), TEXT("0: сцена премиум-гачи открыта (тот же путь, что кнопка 73410)"));

			// ── СВОЯ СМЕРТЬ — настоящий 4053 ──
			Net->DebugFeedPacket(4053, Body4053(T.Uidx, -1));
		});

		// ════════ 1. Меню смерти: набор опций 3, 5, 4 и подписи ════════
		S->Then(1.2f, [](FState& T)
		{
			UAsdaAfterDeathSubsystem* Ad = T.Sub<UAsdaAfterDeathSubsystem>();
			T.Expect(Ad && Ad->IsDeathMenuOpen(), TEXT("1: меню смерти открыто по 4053"));
			FString Set;
			for (int32 I = 0; Ad && I < Ad->DebugOptionCount(); ++I)
			{
				const int32 Ty = Ad->DebugOptionType(I);
				Set += FString::Printf(TEXT("%d "), Ty);
				if (Ty == 3) { T.Idx3 = I; }
				if (Ty == 5) { T.Idx5 = I; }
			}
			T.Expect(Ad && Ad->DebugOptionCount() == 3 && Ad->DebugOptionType(0) == 3 && Ad->DebugOptionType(1) == 5 && Ad->DebugOptionType(2) == 4,
				FString::Printf(TEXT("1: набор опций по эталону «3 5 4» (Resuscitate, свиток, город): %s"), *Set));
			if (Ad && T.Idx5 >= 0)
			{
				const FString Lb = Ad->DebugSlotLabel(T.Idx5), Ex = AsdaNotice::Loc(T.GI(), 2376);
				T.Expect(Lb.Contains(Ex.Left(8)), FString::Printf(TEXT("1: подпись кнопки свитка «%s» = loc 2376"), *Lb));
			}
			if (Ad && T.Idx3 >= 0)
			{
				const FString Lb = Ad->DebugSlotLabel(T.Idx3), Ex = AsdaNotice::Loc(T.GI(), 2381);
				T.Expect(Lb.Contains(Ex.Left(8)), FString::Printf(TEXT("1: подпись кнопки Resuscitate «%s» = loc 2381"), *Lb));
			}
			T.Expect(AsdaDeathLock::IsInputLocked(T.GI()), TEXT("1: замок ввода мёртвого взведён (эталон CUIManager+0x44)"));
			const UAsdaPremiumGachaSubsystem* G = T.Sub<UAsdaPremiumGachaSubsystem>();
			T.Expect(G && !G->IsSceneOpen(), TEXT("1: сцена гачи закрыта смертью (эталон `objStateAllowsRender`: состояние 6 → default → 1 → закрыть, нотис 6813)"));
			FString Why;
			const UAsdaHotbarSubsystem* Hb = T.Sub<UAsdaHotbarSubsystem>();
			const bool bHbBlocked = Hb && Hb->IsActivationBlockedForCheck(Why);
			T.Expect(bHbBlocked && Why.Contains(TEXT("смерти")), FString::Printf(TEXT("1: панель быстрого доступа заперта: «%s»"), *Why));
		});

		// ════════ 2. Ч/б мира на смерти включён ════════
		S->Then(0.3f, [](FState& T)
		{
			AAsdaPlayerCharacter* Ch = T.Me();
			APostProcessVolume* V = Ch ? Ch->DeathPPV.Get() : nullptr;
			T.Expect(Ch && Ch->bDeathDesatOn && Ch->DeathDesatCur > 0.99f && V && V->bEnabled && V->BlendWeight >= 0.99f,
				FString::Printf(TEXT("2: мир обесцвечен (доля %.2f, объём %s, вес %.2f)"), Ch ? Ch->DeathDesatCur : -1.f,
					V ? TEXT("есть") : TEXT("НЕТ"), V ? V->BlendWeight : -1.f));
		});

		// ════════ 3. Клавиша инвентаря (команда 33) мёртвому — не работает ════════
		S->Then(0.2f, [](FState& T) { if (AAsdaPlayerController* P = T.PC()) { P->InputKey(FInputKeyEventArgs::CreateSimulated(T.InvKey, IE_Pressed, 1.f)); } });
		S->Then(0.15f, [](FState& T) { if (AAsdaPlayerController* P = T.PC()) { P->InputKey(FInputKeyEventArgs::CreateSimulated(T.InvKey, IE_Released, 0.f)); } });
		S->Then(0.3f, [](FState& T)
		{
			UAsdaInventorySubsystem* Inv = T.Sub<UAsdaInventorySubsystem>();
			T.Expect(Inv && !Inv->IsWindowOpen(), TEXT("3: клавиша инвентаря мёртвому не исполняется (эталон: только команда 44)"));
			// Кнопка системного меню 73402 (инвентарь) — открытие тоже заперто.
			if (UAsdaSystemMenuSubsystem* Sm = T.Sub<UAsdaSystemMenuSubsystem>()) { Sm->ClickButton(73402); }
		});
		S->Then(0.4f, [](FState& T)
		{
			UAsdaInventorySubsystem* Inv = T.Sub<UAsdaInventorySubsystem>();
			T.Expect(Inv && !Inv->IsWindowOpen(), TEXT("3: кнопка системного меню 73402 мёртвому окно не открывает (эталон: только 73406)"));
			// Контроль метода: ключ 0 — та же клавиша открывает инвентарь (это и был баг).
			T.Cv->Set(TEXT("asda.Death.InputLock"), 0);
		});
		S->Then(0.1f, [](FState& T) { if (AAsdaPlayerController* P = T.PC()) { P->InputKey(FInputKeyEventArgs::CreateSimulated(T.InvKey, IE_Pressed, 1.f)); } });
		S->Then(0.15f, [](FState& T) { if (AAsdaPlayerController* P = T.PC()) { P->InputKey(FInputKeyEventArgs::CreateSimulated(T.InvKey, IE_Released, 0.f)); } });
		S->Then(0.4f, [](FState& T)
		{
			UAsdaInventorySubsystem* Inv = T.Sub<UAsdaInventorySubsystem>();
			const bool bOpened = Inv && Inv->IsWindowOpen();
			T.Expect(bOpened, TEXT("3: контроль — ключ 0 (прежний порт): та же клавиша мёртвому открывает инвентарь"));
			if (bOpened) { if (AAsdaPlayerController* P = T.PC()) { P->RunCommandForCheck(kInvCmd); } }
			T.Cv->Set(TEXT("asda.Death.InputLock"), 1);
		});

		// ════════ 4. Опция 5 → свиток применяется как двойной клик (5450), 4054 нет ════════
		S->Then(0.3f, [](FState& T)
		{
			UAsdaAfterDeathSubsystem* Ad = T.Sub<UAsdaAfterDeathSubsystem>();
			const int32 N4054 = T.Count(kOp4054), N5450 = T.Count(kOp5450);
			if (Ad && T.Idx5 >= 0) { Ad->SelectOptionAt(T.Idx5); }
			T.Expect(T.Count(kOp5450) == N5450 + 1 && T.Count(kOp4054) == N4054,
				FString::Printf(TEXT("4: опция 5 → ровно один 5450 (%d → %d), 4054 не уходит (%d)"), N5450, T.Count(kOp5450), T.Count(kOp4054)));
			T.Expect(Ad && Ad->IsDeathMenuOpen(), TEXT("4: меню остаётся до ответа сервера (закрывается по 4055/4056)"));
		});

		// ════════ 5. Опция 3 → 4054 режим 1 ════════
		S->Then(0.3f, [](FState& T)
		{
			UAsdaAfterDeathSubsystem* Ad = T.Sub<UAsdaAfterDeathSubsystem>();
			const int32 N4054 = T.Count(kOp4054);
			if (Ad && T.Idx3 >= 0) { Ad->SelectOptionAt(T.Idx3); }
			const FSent* Last = nullptr;
			for (const FSent& X : T.Sent) { if (X.Op == kOp4054) { Last = &X; } }
			const int32 Mode = (Last && Last->Body.Num() > 0) ? (int32)Last->Body.Last() : -1;
			T.Expect(T.Count(kOp4054) == N4054 + 1 && Mode == 1,
				FString::Printf(TEXT("5: опция 3 (Resuscitate) → 4054 режим %d (ждём 1)"), Mode));
		});

		// ════════ 6. Воскрешение 4056 → замок снят, панель работает, мир цветной ════════
		S->Then(0.2f, [](FState& T)
		{
			UAsdaNetSubsystem* Net = T.Sub<UAsdaNetSubsystem>();
			Net->DebugFeedPacket(4056, Body4056(T.Uidx, FMath::Max(1, Net->SelfTileX), FMath::Max(1, Net->SelfTileZ), 500, 100));
		});
		S->Then(1.0f, [](FState& T)
		{
			UAsdaAfterDeathSubsystem* Ad = T.Sub<UAsdaAfterDeathSubsystem>();
			T.Expect(Ad && !Ad->IsDeathMenuOpen() && !AsdaDeathLock::IsInputLocked(T.GI()), TEXT("6: 4056 → меню закрыто, замок снят"));
			FString Why;
			const UAsdaHotbarSubsystem* Hb = T.Sub<UAsdaHotbarSubsystem>();
			const bool bBlocked = Hb && Hb->IsActivationBlockedForCheck(Why);
			T.Expect(!Why.Contains(TEXT("смерти")), FString::Printf(TEXT("6: панель быстрого доступа больше не заперта смертью (%s)"),
				bBlocked ? *Why : TEXT("свободна")));
			AAsdaPlayerCharacter* Ch = T.Me();
			T.Expect(Ch && !Ch->bDeathDesatOn && Ch->DeathDesatCur < 0.01f, FString::Printf(TEXT("6: мир снова цветной (доля %.2f)"), Ch ? Ch->DeathDesatCur : -1.f));
		});
		// После воскрешения клавиша инвентаря снова работает (регресс замка).
		S->Then(0.1f, [](FState& T) { if (AAsdaPlayerController* P = T.PC()) { P->InputKey(FInputKeyEventArgs::CreateSimulated(T.InvKey, IE_Pressed, 1.f)); } });
		S->Then(0.15f, [](FState& T) { if (AAsdaPlayerController* P = T.PC()) { P->InputKey(FInputKeyEventArgs::CreateSimulated(T.InvKey, IE_Released, 0.f)); } });
		S->Then(0.4f, [](FState& T)
		{
			UAsdaInventorySubsystem* Inv = T.Sub<UAsdaInventorySubsystem>();
			const bool bOpened = Inv && Inv->IsWindowOpen();
			T.Expect(bOpened, TEXT("6: живому та же клавиша открывает инвентарь (замок не мешает)"));
			if (bOpened) { if (AAsdaPlayerController* P = T.PC()) { P->RunCommandForCheck(kInvCmd); } }
		});

		// ════════ 7. Кадры: тот же объём ч/б смерти над миром, сцена гачи открыта ════════
		//   Мёртвому сцена гачи в эталоне не держится (см. шаг 1), поэтому механизм дефекта — «объём мира сереет UI-захват» —
		//   проверяем у живого с ВКЛЮЧЁННЫМ тем же обесцвечиванием смерти (`SetDeathDesaturation(true)`, тот же объём).
		S->Then(0.2f, [](FState& T)
		{
			if (AAsdaPlayerCharacter* Ch = T.Me()) { Ch->SetDeathDesaturation(true); }
			UAsdaPremiumGachaSubsystem* G = T.Sub<UAsdaPremiumGachaSubsystem>();
			T.Expect(G && G->OpenScene(), TEXT("7: сцена гачи открыта при включённом ч/б объёме смерти"));
		});
		S->Then(1.5f, [](FState& T)
		{
			AAsdaPlayerCharacter* Ch = T.Me();
			APostProcessVolume* V = Ch ? Ch->DeathPPV.Get() : nullptr;
			T.Expect(Ch && Ch->DeathDesatCur > 0.99f && V && V->bEnabled, FString::Printf(TEXT("7: объём ч/б активен (доля %.2f)"), Ch ? Ch->DeathDesatCur : -1.f));
			AAsdaGachaSceneActor* A = nullptr;
			for (TActorIterator<AAsdaGachaSceneActor> It(T.W.Get()); It; ++It) { A = *It; break; }
			UE_LOG(LogAsdaDeathLockTest, Log, TEXT("СМЕРТЬ-ЗАМОК актор сцены гачи: %s, охват: %s"), A ? *A->GetName() : TEXT("НЕТ"),
				A ? *A->DescribeFrameCoverage() : TEXT("-"));
			int32 Lit = 0;
			T.ChromaOn = A ? FrameChroma(A->GetRenderTarget(), Lit, TEXT("gacha_under_death_desat_key1")) : -1.f;
			if (A && A->GetRenderTarget())
			{
				UKismetRenderingLibrary::ExportRenderTarget(T.W.Get(), A->GetRenderTarget(), FPaths::ProjectSavedDir() / TEXT("Screenshots"),
					FString::Printf(TEXT("deathlock_gacha_key1_%s.png"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en")));
			}
			T.Expect(T.ChromaOn > 8.f, FString::Printf(TEXT("7: ключ 1 — кадр гачи ЦВЕТНОЙ под ч/б объёмом: цветность %.1f (пикселей %d), PNG deathlock_gacha_key1"),
				T.ChromaOn, Lit));
			T.Cv->Set(TEXT("asda.Death.DesatWorldOnly"), 0);
			if (A) { A->SetCaptureEnabled(true); }
		});
		S->Then(0.8f, [](FState& T)
		{
			AAsdaGachaSceneActor* A = nullptr;
			for (TActorIterator<AAsdaGachaSceneActor> It(T.W.Get()); It; ++It) { A = *It; break; }
			int32 Lit = 0;
			T.ChromaOff = A ? FrameChroma(A->GetRenderTarget(), Lit, TEXT("gacha_under_death_desat_key0")) : -1.f;
			if (A && A->GetRenderTarget())
			{
				UKismetRenderingLibrary::ExportRenderTarget(T.W.Get(), A->GetRenderTarget(), FPaths::ProjectSavedDir() / TEXT("Screenshots"),
					FString::Printf(TEXT("deathlock_gacha_key0_%s.png"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en")));
			}
			T.Expect(T.ChromaOff >= 0.f && T.ChromaOff < 2.f && T.ChromaOn > T.ChromaOff + 6.f,
				FString::Printf(TEXT("7: контроль — ключ 0 (прежний порт): кадр гачи СЕРЫЙ, цветность %.1f против %.1f (PNG deathlock_gacha_key0)"),
					T.ChromaOff, T.ChromaOn));
			T.Cv->Set(TEXT("asda.Death.DesatWorldOnly"), 1);
			if (A) { A->SetCaptureEnabled(true); }
			if (UAsdaPremiumGachaSubsystem* G = T.Sub<UAsdaPremiumGachaSubsystem>()) { G->CloseScene(); }
			if (AAsdaPlayerCharacter* Ch = T.Me()) { Ch->SetDeathDesaturation(false); }
		});
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		const uint16 Extra[] = { kOp4054, kOp5450 };
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args, 0.f, Extra)) { return; }
		if (!W) { return; }
		const bool bSpawn = AsdaSelfTestKit::NeedSelfSpawn(W) && GEngine;
		if (bSpawn)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 115 396"));
		}
		TSharedRef<FState> S = MakeShared<FState>();
		S->W = W;
		S->Args = Args;
		TWeakObjectPtr<UWorld> WW(W);
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([S, WW]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd) { return; }
			if (!S->Sub<UAsdaNetSubsystem>() || !S->Sub<UAsdaAfterDeathSubsystem>() || !S->Sub<UAsdaInventorySubsystem>() || !S->Me())
			{
				AsdaSelfTestKit::Refused(Wd, kCmd, S->Args.bQuit, TEXT("нет подсистем или своего персонажа — нужна карта мира"));
				return;
			}
			Build(S);
			Step(S);
		}), bSpawn ? 2.f : 0.05f, false);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestDeathLock(
	TEXT("asda.SelfTest.DeathLock"),
	TEXT("Самотест мёртвого: опции 3/5 меню смерти (Resuscitate, свиток подтипа 48 → 5450), замок ввода (клавиша инвентаря, кнопка "
	     "73402, панель быстрого доступа), ч/б мира не ложится на сцену гачи (кадры RT, контроль ключом 0), снятие по 4056. "
	     "Только офлайн, карта мира. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaDeathLockSelfTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
