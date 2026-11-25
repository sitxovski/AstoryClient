// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ЗНАЧКА «ОТКАТ НАВЫКА ИСТЁК» (21.10.2026): `asda.SelfTest.CooldownReadyIcon [quit]`.
//   Офлайн (`-AsdaNoNet`), на карте мира. Реальный путь (закон diagnostic-must-use-real-path): кадр 5274 подаётся в общий
//   разбор входящих (`DebugFeedPacket`) → делегат `OnSkillCooldownReady` → `UAsdaCooldownReadyIconSubsystem`. Проверяются:
//     A) опция 30479: вкл — запись есть, выкл — нет; тело 4 б (мусор в [2..3]) читается как i16 id;
//     B) временная модель `0xB4FA20` по кадрам 60 FPS: 256 → 32 за 0,373 с, альфа не выше 100, пауза > 500 мс, исчезание
//        32 → 0 за 0,178 с, всего ≈ 1,05 с; запись удаляется;
//     C) навык без иконки (id < 100, как у соулмейта) — запись живёт ≈ 1 с, картинки нет; навык с иконкой — картинка есть;
//     D) несколько записей подряд — в порядке прихода, без лимита и без слияния дублей;
//     E) гейт рисования — мини-карта: свёрнута → картинка скрыта, но запись ТИКАЕТ (ИСПРАВЛЕНИЕ залпа эталона);
//     F) полный сброс сессии очищает список, смена карты — нет.
//   Итог: «ОТКАТ-ЗНАЧОК ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "AsdaBytes.h"
#include "UI/AsdaCooldownReadyIconSubsystem.h"
#include "UI/AsdaSkillDatabase.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "Resources/AsdaGameUserSettings.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Resources/AsdaAssetPaths.h"
#include "Test/AsdaSelfTestKit.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCdReadyTest, Log, All);

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCooldownReadyIcon(
	TEXT("asda.SelfTest.CooldownReadyIcon"),
	TEXT("Самотест значка «откат навыка истёк» (5274, опция 30479): приём, модель 256→32→0 за ≈1,05 с, иконки, гейт мини-карты, "
	     "сброс. Офлайн, на карте мира. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		static const TCHAR* kCmd = TEXT("asda.SelfTest.CooldownReadyIcon");
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		UAsdaCooldownReadyIconSubsystem* Cd = GI ? GI->GetSubsystem<UAsdaCooldownReadyIconSubsystem>() : nullptr;
		const UAsdaSkillDatabase* Db = GI ? GI->GetSubsystem<UAsdaSkillDatabase>() : nullptr;
		UAsdaMinimapSubsystem* Mm = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
		UAsdaGameUserSettings* GUS = UAsdaGameUserSettings::Get();
		if (!Net || !Cd || !Db || !GUS || !GUS->InterfaceOther.IsValidIndex(8))
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем / профиля опций"));
			return;
		}
		int32 Ok = 0, Fail = 0;
		auto Check = [&Ok, &Fail](bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaCdReadyTest, Log,   TEXT("ОТКАТ-ЗНАЧОК ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaCdReadyTest, Error, TEXT("ОТКАТ-ЗНАЧОК FAIL %s"), *What); }
		};
		const uint8 OptWas = GUS->InterfaceOther[8];
		Cd->ResetSessionState(EAsdaSessionResetReason::Disconnected);   // чистый старт (записей прошлых прогонов нет)

		// Навык с иконкой, у которой есть текстура, и навык без иконки (id < 100 — как у соулмейта).
		int32 IconSkill = -1;
		for (int32 Id = 100; Id < 5000 && IconSkill < 0; ++Id)
		{
			const FAsdaSkillDef* D = Db->Get(Id);
			UAsdaTextureSubsystem* Tm = AsdaTexMgr();
			if (D && !D->IconName.IsEmpty() && Tm && Tm->LoadTextureCached(AsdaAssetPaths::JuiTextureObject(D->IconName))) { IconSkill = Id; }
		}
		int32 NoIconSkill = -1;
		for (int32 Id = 0; Id < 100 && NoIconSkill < 0; ++Id)
		{
			const FAsdaSkillDef* D = Db->Get(Id);
			if (!D || D->IconName.IsEmpty()) { NoIconSkill = Id; }
		}
		Check(IconSkill >= 0 && NoIconSkill >= 0, FString::Printf(TEXT("0 подготовка: навык с иконкой %d, без иконки %d"), IconSkill, NoIconSkill));

		auto Feed = [Net](int32 Id, bool bFour)
		{
			TArray<uint8> B; B.SetNumZeroed(bFour ? 4 : 2);
			AsdaBytes::WrI16(B.GetData(), Id);
			if (bFour) { B[2] = 0xCD; B[3] = 0xAB; }   // мусор стека сервера (`sub_47BEC0`)
			AsdaTestNet::DebugFeedPacket(Net, 5274, B);
		};
		float S = 0.f, A = 0.f; int32 Ph = 0, Sid = 0;

		// ═══ A. Опция и приём ═══
		GUS->InterfaceOther[8] = 0;
		Feed(IconSkill, false);
		Check(Cd->DebugCount() == 0, TEXT("A1 опция 30479 выключена — значка нет"));
		GUS->InterfaceOther[8] = 1;
		Feed(IconSkill, true);
		Check(Cd->DebugCount() == 1 && Cd->DebugEntry(0, S, A, Ph, Sid) && Sid == IconSkill && FMath::IsNearlyEqual(S, 256.f) && Ph == 0 && A == 0.f,
			FString::Printf(TEXT("A2 опция включена, тело 4 б — запись навыка %d, s = 256, фаза 0, альфа 0"), IconSkill));
		Check(Cd->DebugHasImage(0) && Cd->DebugHasMaterial(0), TEXT("A3 у навыка с иконкой — картинка есть, кисть — материал с линейным сэмплером"));
		{
			// Геометрия квада `0xB4F180`: центр (W/2, H/2 + 100), сторона s.
			Cd->DebugStep(0.1f);
			FVector2D Pos, Sz, Vp(0, 0);
			if (GI->GetGameViewportClient()) { GI->GetGameViewportClient()->GetViewportSize(Vp); }
			Cd->DebugEntry(0, S, A, Ph, Sid);
			const bool bRect = Cd->DebugSlotRect(0, Pos, Sz);
			const FVector2D C = Pos + Sz * 0.5f;
			Check(bRect && FMath::IsNearlyEqual(Sz.X, S, 0.5f) && FMath::IsNearlyEqual(Sz.Y, S, 0.5f)
				&& FMath::IsNearlyEqual(C.X, (float)(int32)Vp.X * 0.5f, 0.5f) && FMath::IsNearlyEqual(C.Y, (float)(int32)Vp.Y * 0.5f + 100.f, 0.5f),
				FString::Printf(TEXT("A4 квад: сторона %.1f (s %.1f), центр %s (кадр %s → W/2, H/2+100)"), Sz.X, S, *C.ToString(), *Vp.ToString()));
			Cd->ResetSessionState(EAsdaSessionResetReason::Disconnected);
			Feed(IconSkill, true);   // запись снова с s = 256 — модель ниже считает от старта
		}

		// ═══ B. Модель по кадрам 60 FPS ═══
		{
			const float Dt = 1.f / 60.f;
			float T = 0.f, MaxA = 0.f, THold = -1.f, TFade = -1.f, TGone = -1.f;
			for (int32 k = 0; k < 200 && TGone < 0.f; ++k)
			{
				Cd->DebugStep(Dt); T += Dt;
				if (!Cd->DebugEntry(0, S, A, Ph, Sid)) { TGone = T; break; }
				MaxA = FMath::Max(MaxA, A);
				if (Ph >= 1 && THold < 0.f) { THold = T; Check(FMath::IsNearlyEqual(S, 32.f), TEXT("B1 вход в паузу — s зафиксирован на 32")); }
				if (Ph == 2 && TFade < 0.f) { TFade = T; }
			}
			Check(THold > 0.36f && THold < 0.40f, FString::Printf(TEXT("B2 сжатие 256 → 32 за %.3f с (эталон 0,373)"), THold));
			Check(FMath::IsNearlyEqual(MaxA, 100.f), FString::Printf(TEXT("B3 альфа не выше 100 из 255 (макс %.1f)"), MaxA));
			Check(TFade > 0.f && TFade - THold > 0.5f && TFade - THold < 0.54f, FString::Printf(TEXT("B4 пауза %.3f с (строго > 0,5)"), TFade - THold));
			Check(TGone > 0.f && TGone - TFade > 0.15f && TGone - TFade < 0.21f, FString::Printf(TEXT("B5 исчезание 32 → 0 за %.3f с (эталон 0,178)"), TGone - TFade));
			Check(TGone > 1.0f && TGone < 1.12f && Cd->DebugCount() == 0, FString::Printf(TEXT("B6 запись удалена через %.3f с (эталон ≈1,05)"), TGone));
		}

		// ═══ C. Навык без иконки ═══
		Feed(NoIconSkill, false);
		Check(Cd->DebugCount() == 1 && !Cd->DebugHasImage(0), FString::Printf(TEXT("C1 навык %d без иконки — запись есть, картинки нет"), NoIconSkill));
		for (int32 k = 0; k < 80; ++k) { Cd->DebugStep(1.f / 60.f); }
		Check(Cd->DebugCount() == 0, TEXT("C2 запись без иконки тоже уходит за ≈1,05 с"));

		// ═══ D. Несколько подряд ═══
		Feed(IconSkill, false); Feed(IconSkill, false); Feed(NoIconSkill, true);
		int32 Id0 = 0, Id1 = 0, Id2 = 0;
		Cd->DebugEntry(0, S, A, Ph, Id0); Cd->DebugEntry(1, S, A, Ph, Id1); Cd->DebugEntry(2, S, A, Ph, Id2);
		Check(Cd->DebugCount() == 3 && Id0 == IconSkill && Id1 == IconSkill && Id2 == NoIconSkill, TEXT("D1 три 5274 подряд — три записи в порядке прихода, дубли не сливаются"));

		// ═══ E. Гейт мини-карты ═══
		{
			const bool bAllowed = Cd->DebugDrawAllowed();
			const bool bMmUp = Mm && Mm->IsMinimapVisible();
			Check(bAllowed == bMmUp && Cd->DebugImageVisible(0) == bAllowed,
				FString::Printf(TEXT("E1 гейт рисования = мини-карта развёрнута (%s), картинка видна ровно тогда"), bMmUp ? TEXT("да") : TEXT("нет")));
			if (Mm && Mm->IsMinimapVisible())
			{
				Mm->OnCloseClicked();   // кнопка 1704 MINIMAP_CLOSE — тот же обработчик
				Cd->DebugStep(1.f / 60.f);
				float S0 = 0.f; Cd->DebugEntry(0, S0, A, Ph, Sid);
				Cd->DebugStep(0.1f);
				float S1 = 0.f; Cd->DebugEntry(0, S1, A, Ph, Sid);
				Check(!Mm->IsMinimapVisible() && !Cd->DebugImageVisible(0) && S1 < S0,
					FString::Printf(TEXT("E2 мини-карта свёрнута: картинка скрыта, запись тикает (%.0f → %.0f) [ИСПРАВЛЕНИЕ залпа эталона]"), S0, S1));
				Mm->OnRestoreClicked();   // 1701 MINIMAPMIN_ON
				Cd->DebugStep(1.f / 60.f);
				Check(Mm->IsMinimapVisible() && Cd->DebugDrawAllowed() && Cd->DebugImageVisible(0), TEXT("E3 мини-карта развёрнута снова — картинка рисуется"));
			}
			else { UE_LOG(LogAsdaCdReadyTest, Warning, TEXT("ОТКАТ-ЗНАЧОК E2/E3 пропущены: мини-карта не развёрнута на этой карте")); }
		}

		// ═══ F. Сброс ═══
		Cd->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		Check(Cd->DebugCount() == 3, TEXT("F1 смена карты список не трогает"));
		Cd->ResetSessionState(EAsdaSessionResetReason::Disconnected);
		Check(Cd->DebugCount() == 0, TEXT("F2 полный сброс сессии очищает список [ИСПРАВЛЕНИЕ]"));

		GUS->InterfaceOther[8] = OptWas;
		AsdaSelfTestKit::Finish(W, kCmd, KA.bQuit, LogAsdaCdReadyTest, TEXT("ОТКАТ-ЗНАЧОК"), Ok, Fail);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
