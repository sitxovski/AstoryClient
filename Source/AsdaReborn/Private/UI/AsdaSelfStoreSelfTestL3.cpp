// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ВОЛНЫ Л3 ТОРГОВЛИ — ВЫВЕСКИ И ПРИЛАВКИ ЛИЧНЫХ ЛАВОК (30.09.2026, `Reports/trade/wave_L3.md`). [ПОРТ]
//
// ЧТО ПРОВЕРЯЕТ. Путь через НАСТОЯЩИЕ функции: приём 6304 через ШТАТНЫЙ `UAsdaNetSubsystem::DebugFeedPacket` → `HandlePacket`
//   (длина, делегат, очередь колбэка) → очередь вывесок (`charMgrAddNameStatusEntry`) → разбор раз в 1 с
//   (`charManager_UpdateNameStatusList`) → прилавки (`CShopMgr_OpenNpcShop` / `npcShopMgr_CloseShopByUserIdx`) → вывески
//   (`Chat_RenderHeadBubble`): клон 23450/23470, обрезка названия, позиция (сдвиг за титул/клан), GM-скрытие (4025 300/301), уход
//   персонажа (4011), свой прилавок, смерть → 6305 (раз в 4 с), реклама премиум-лавки 5086 (раз в 300 с), сброс сессии. Отправки
//   6307/6305/5086 — БОЕВЫЕ отправители под перехватом (`ArmSendCapture`, весь трафик: офлайн сокета нет).
//   Фаза «путём игрока»: настоящий клик `SimulateClickAt` по живой геометрии рамки вывески → 6307; мировой клик погашен зоной
//   интерфейса; сидящий продавец (4025 108) — точка вывески ниже (центр AABB); геометрия рамки против `.JUI`.
//   ★ Контрольные пары (закон pixel-metric-needs-control-pairs): каждое действие проверяется и в противоположном условии.
//
// Консоль: `asda.Trade.SelfTestStoreL3 [delay=N] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки — «САМОТЕСТ-ЛАВКА-Л3», итог — «САМОТЕСТ-ЛАВКА-Л3 ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "UI/AsdaStallSignSubsystem.h"
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "UI/AsdaSelfStoreSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaKingCrownSubsystem.h"
#include "World/AsdaCoord.h"
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaSelf.h"
#include "AsdaBytes.h"
#include "Login/AsdaLoginData.h"
#include "Blueprint/UserWidget.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Camera/PlayerCameraManager.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaStoreTestL3, Log, All);

namespace AsdaStoreL3Local
{
	constexpr int32 kSelfCharId = 424242;   // свой charId
	constexpr int32 kSelfUserNum = 17;      // свой userNum
	constexpr int32 kSeller = 55;           // продавец (обычная лавка), charId 900055
	constexpr int32 kPlain = 56;            // сосед без лавки (контроль), charId 900056
	constexpr int32 kPremium = 57;          // продавец премиум-лавки, charId 900057
	constexpr int32 kLate = 58;             // появляется ПОСЛЕ 6304 (очередь ждёт персонажа), charId 900058
	inline int32 CharOf(int32 UserNum) { return 900000 + UserNum; }   // `DebugSpawnFakePeer`: charId = 900000 + userNum
	const TCHAR* kShortTitle = TEXT("Лавка Л3");
	const TCHAR* kLongTitle = TEXT("Очень длинное название премиальной лавки номер семь");

	// Тело 6304 (56 б): [0] открыта, [1] премиум, @2 charId, @6 название[50].
	TArray<uint8> Make6304(uint8 Open, uint8 Premium, int32 CharId, const FString& Title)
	{
		TArray<uint8> B; B.SetNumZeroed(56);
		B[0] = Open; B[1] = Premium;
		AsdaBytes::WrI32(B.GetData() + 2, CharId);
		FAsdaLoginData::EncodeCp1251(Title, B.GetData() + 6, 49);
		return B;
	}
	// ★ Волна Л4/Л5 (К-9): тело 4005 (151 б) соседа — ШТАТНЫЙ спавн через приём сети: userIdx u16@0, charId i32@11, имя@15 (20),
	//   пол@37, класс@38, уровень@40, позиция float@56/@60 (раскладка `FAsdaRemotePlayer`, AsdaNetSubsystem.cpp case 4005).
	TArray<uint8> Make4005(int32 UserNum, const FString& Name)
	{
		TArray<uint8> B; B.SetNumZeroed(151);
		AsdaBytes::WrI16(B.GetData(), (int16)UserNum);
		AsdaBytes::WrI32(B.GetData() + 11, CharOf(UserNum));
		FAsdaLoginData::EncodeCp1251(Name, B.GetData() + 15, 19);
		B[37] = 1; B[38] = 1; B[40] = 10;
		const float X = 258.f, Z = 256.f;
		FMemory::Memcpy(B.GetData() + 56, &X, 4);
		FMemory::Memcpy(B.GetData() + 60, &Z, 4);
		return B;
	}
	// Тело 4025 (13 б): userIdx u16@0, действие u16@2 — GM-скрытие 300/301, «сидит» 108.
	TArray<uint8> Make4025(int32 UserNum, int32 Act)
	{
		TArray<uint8> B; B.SetNumZeroed(13);
		AsdaBytes::WrI16(B.GetData(), (int16)UserNum);
		AsdaBytes::WrI16(B.GetData() + 2, (int16)Act);
		return B;
	}
}

struct FAsdaSelfStoreL3SelfTest
{
	// ★ 11.10.2026 (В3 S1, N1(г)): «свой» персонаж (charId, userNum, уровень…) подменяется в Setup — возврат RAII каркаса.
	FAsdaNetSelfScope SelfScope;
	int32 Ok = 0;     // пройдено проверок
	int32 Fail = 0;   // провалено проверок
	struct FCap { uint16 Op = 0; TArray<uint8> P; };           // перехваченный исходящий пакет
	TSharedRef<TArray<FCap>> Caps = MakeShared<TArray<FCap>>();
	uint32 Gen = 0;                                              // поколение перехватчика
	TArray<TFunction<void(UWorld*)>> Steps;                      // шаги фазы «путём игрока»
	FVector2D StandAnchor = FVector2D::ZeroVector;               // точка вывески стоящего продавца (для пары «сидит»)
	int32 SitPolls = 0;                                          // сколько раз ждали фазу «сидит»

	void Expect(bool bCond, const FString& Name, const FString& Detail)
	{
		if (bCond) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaStoreTestL3, Log, TEXT("САМОТЕСТ-ЛАВКА-Л3 %s: %s — %s"), bCond ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name, *Detail);
	}
	int32 Count(uint16 Op, TArray<uint8>* Out = nullptr) const
	{
		int32 N = 0;
		for (const FCap& C : *Caps) { if (C.Op == Op) { ++N; if (Out) { *Out = C.P; } } }
		return N;
	}
	// Подать пакет штатным приёмом и разобрать очередь колбэка вывесок (её разбирает Tick; здесь — сразу).
	static void Feed(UAsdaNetSubsystem* Net, UAsdaStallSignSubsystem* G, int32 Op, const TArray<uint8>& Body)
	{
		AsdaTestNet::DebugFeedPacket(Net, Op, Body);
		G->PumpPending();
	}
	// Полный проход вывесок без ожидания таймеров (очередь, прилавки, вывески).
	static void Pass(UAsdaStallSignSubsystem* G)
	{
		G->UpdateNameStatusList(true);
		G->UpdateStalls(true);
		G->RenderSigns();
	}
	// Поставить соседа перед камерой: вперёд Fwd, вбок Side (юниты UE), на высоте своего персонажа.
	static void PlaceInFront(UWorld* W, AActor* A, float Fwd, float Side)
	{
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		APawn* Self = PC ? PC->GetPawn() : nullptr;
		if (!A || !Self || !PC->PlayerCameraManager) { return; }
		const float Yaw = PC->PlayerCameraManager->GetCameraRotation().Yaw;
		const FVector F = FRotator(0.f, Yaw, 0.f).Vector();
		const FVector R = FRotator(0.f, Yaw + 90.f, 0.f).Vector();
		A->SetActorLocation(Self->GetActorLocation() + F * Fwd + R * Side, false, nullptr, ETeleportType::TeleportPhysics);
	}

	void Run(UWorld* W)
	{
		using namespace AsdaStoreL3Local;
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaStallSignSubsystem* G = GI ? GI->GetSubsystem<UAsdaStallSignSubsystem>() : nullptr;
		UAsdaSelfStoreSubsystem* S = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!G || !S || !Net || !WM || !WS)
		{
			UE_LOG(LogAsdaStoreTestL3, Error, TEXT("САМОТЕСТ-ЛАВКА-Л3 ИТОГ: ok=0 fail=1 (нет подсистем)"));
			return;
		}
		// ── Подготовка: свой персонаж, соседи 55/56/57 перед камерой, перехват всего трафика ──
		SelfScope.Capture(Net, TEXT("asda.Trade.SelfTestStoreL3"));   // ★ В3 S1: снимок «своего» до подмены
		AsdaTestNet::SelectedCharId(Net) = kSelfCharId;
		AsdaTestNet::SetSelfUserNumForDebug(Net, kSelfUserNum);
		AsdaTestNet::SelfLevel(Net) = 30;
		S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		G->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		WS->DebugSpawnFakePeer(kSeller, TEXT("Продавец"), FVector2D(258.f, 256.f));
		WS->DebugSpawnFakePeer(kPlain, TEXT("Сосед"), FVector2D(254.f, 256.f));
		WS->DebugSpawnFakePeer(kPremium, TEXT("Богач"), FVector2D(256.f, 258.f));
		PlaceInFront(W, WS->ActorByUserIdx(kSeller), 350.f, -170.f);
		PlaceInFront(W, WS->ActorByUserIdx(kPlain), 450.f, 0.f);
		PlaceInFront(W, WS->ActorByUserIdx(kPremium), 350.f, 170.f);
		Caps->Reset();
		TSharedRef<TArray<FCap>> C = Caps;
		Gen = AsdaSelfTestKit::ArmCapture(Net, [C](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			FCap X; X.Op = Op; if (P && Len > 0) { X.P.Append(P, Len); } C->Add(MoveTemp(X));
		}, 0, TEXT("asda.Trade.SelfTestStoreL3"));
		WM->ClearMsgBoxQueue(TEXT("самотест лавки Л3"));
		Expect(WS->ActorByUserIdx(kSeller) && WS->ActorByUserIdx(kPlain) && WS->ActorByUserIdx(kPremium) && WS->ActorByUserIdx(kSelfUserNum),
			TEXT("подготовка: свой персонаж и соседи 55/56/57 в мире"), G->DumpState());

		// ═══ 1. 6304 → очередь; разбор раз в > 1 с: рано — ждёт ↔ по времени — признаки персонажа ═══
		{
			Feed(Net, G, 6304, Make6304(1, 0, CharOf(kSeller), kShortTitle));
			const int32 Q0 = G->GetQueueNum();
			G->LastQueueAt = FPlatformTime::Seconds();
			G->UpdateNameStatusList(false);
			bool bOpen = false, bPrem = false; FString T;
			const bool bEarly = G->GetQueueNum() == 1 && !G->GetCharFlags(CharOf(kSeller), bOpen, bPrem, T);
			G->UpdateNameStatusList(true);
			const bool bHas = G->GetCharFlags(CharOf(kSeller), bOpen, bPrem, T);
			Expect(Q0 == 1 && bEarly && G->GetQueueNum() == 0 && bHas && bOpen && !bPrem && T == kShortTitle,
				TEXT("6304: штатный приём → очередь; < 1 с — запись ждёт ↔ > 1 с — признаки charId 900055: в лавке, не премиум, название"),
				FString::Printf(TEXT("очередь %d, рано=%d, признаки=%d открыта=%d премиум=%d «%s»"), Q0, bEarly ? 1 : 0, bHas ? 1 : 0, bOpen ? 1 : 0, bPrem ? 1 : 0, *T));
		}

		// ═══ 2. Прилавок `shop.jbm`: у продавца есть (на `MeshRoot`, в нуле), плашка сдвинута ↔ у соседа без лавки — нет ═══
		{
			G->UpdateStalls(true);
			UStaticMeshComponent* M = G->DebugStallMesh(kSeller);
			const AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kSeller));
			const bool bMesh = M && M->GetStaticMesh() && M->GetStaticMesh()->GetName() == TEXT("shop") && Ch && M->GetAttachParent() == Ch->MeshRoot
				&& M->GetRelativeLocation().IsNearlyZero() && M->GetRelativeRotation().IsNearlyZero();
			const UAsdaNameplateComponent* NP = Ch ? Ch->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
			const AActor* Plain = WS->ActorByUserIdx(kPlain);
			const UAsdaNameplateComponent* NP2 = Plain ? Plain->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
			Expect(bMesh && NP && NP->bShopOpen && !G->HasStall(kPlain) && NP2 && !NP2->bShopOpen,
				TEXT("прилавок: у продавца 55 модель shop на MeshRoot (матрица тела), плашка — признак лавки ↔ сосед 56 без 6304 — ни прилавка, ни признака"),
				FString::Printf(TEXT("модель=%d плашка55=%d прилавок56=%d плашка56=%d; %s"), bMesh ? 1 : 0, (NP && NP->bShopOpen) ? 1 : 0, G->HasStall(kPlain) ? 1 : 0,
					(NP2 && NP2->bShopOpen) ? 1 : 0, *G->DumpState()));
		}

		// ═══ 3. Очередь ждёт персонажа: 6304 соседа 58 (его ещё нет) — запись остаётся ↔ появился — признак и прилавок ═══
		{
			Feed(Net, G, 6304, Make6304(1, 1, CharOf(kPremium), kLongTitle));
			Feed(Net, G, 6304, Make6304(1, 0, CharOf(kLate), TEXT("Поздний")));
			G->UpdateNameStatusList(true);
			const int32 QWait = G->GetQueueNum();
			bool bOpen = false, bPrem = false; FString T;
			const bool bLateNone = !G->GetCharFlags(CharOf(kLate), bOpen, bPrem, T);
			const bool bPremOk = G->GetCharFlags(CharOf(kPremium), bOpen, bPrem, T) && bOpen && bPrem;
			WS->DebugSpawnFakePeer(kLate, TEXT("Поздний"), FVector2D(256.f, 254.f));
			PlaceInFront(W, WS->ActorByUserIdx(kLate), 600.f, 0.f);
			G->UpdateNameStatusList(true);
			G->UpdateStalls(true);
			Expect(QWait == 1 && bLateNone && bPremOk && G->GetQueueNum() == 0 && G->HasStall(kLate) && G->HasStall(kPremium),
				TEXT("очередь 6304: персонажа 58 нет — запись ждёт (57 применён: премиум) ↔ 58 появился — запись применена, прилавки 57/58"),
				FString::Printf(TEXT("ждёт=%d 58-нет=%d 57-премиум=%d очередь после=%d; %s"), QWait, bLateNone ? 1 : 0, bPremOk ? 1 : 0, G->GetQueueNum(), *G->DumpState()));
		}

		// ═══ 4. Вывески: клон 23450 / 23470 по премиум-признаку; название ≤ 145 px целиком ↔ длинное — обрезка «...» ═══
		{
			G->DebugExpireTimers();
			G->RenderSigns();
			const FString T55 = G->DebugSignText(kSeller);
			const FString T57 = G->DebugSignText(kPremium);
			Expect(G->DebugSignWidget(kSeller) && !G->DebugSignPremium(kSeller) && G->DebugSignWidget(kPremium) && G->DebugSignPremium(kPremium),
				TEXT("вывеска: обычная лавка — клон 23450 ↔ премиум (6304 [1] = 1) — клон 23470"),
				FString::Printf(TEXT("55=%s 57=%s"), G->DebugSignWidget(kSeller) ? (G->DebugSignPremium(kSeller) ? TEXT("23470") : TEXT("23450")) : TEXT("нет"),
					G->DebugSignWidget(kPremium) ? (G->DebugSignPremium(kPremium) ? TEXT("23470") : TEXT("23450")) : TEXT("нет")));
			Expect(T55 == kShortTitle && T57.EndsWith(TEXT("...")) && T57.Len() < FString(kLongTitle).Len() && FString(kLongTitle).StartsWith(T57.LeftChop(3)),
				TEXT("название: короткое — целиком ↔ шире 145 px — обрезка по 125 px с «...» (`uiSetTruncatedMarkupText`)"),
				FString::Printf(TEXT("55 «%s»; 57 «%s»"), *T55, *T57));
		}

		// ═══ 5. Позиция: X − ширина/2, Y − (сдвиг + 62): без титула и клана +10 ↔ с титулом +16 ═══
		{
			FVector2D TL55, A55, TL57, A57;
			const bool b55 = G->DebugSignPlacement(kSeller, TL55, A55);
			AActor* P57 = WS->ActorByUserIdx(kPremium);
			UAsdaNameplateComponent* NP57 = P57 ? P57->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
			if (NP57) { NP57->TitleId0 = 3; }
			G->RenderSigns();
			const bool b57 = G->DebugSignPlacement(kPremium, TL57, A57);
			if (NP57) { NP57->TitleId0 = -1; }
			const bool bY55 = b55 && FMath::IsNearlyEqual(TL55.Y, FMath::FloorToFloat(A55.Y - 72.f), 0.01f) && FMath::IsNearlyEqual(TL55.X, FMath::FloorToFloat(A55.X - 95.f), 0.01f);
			const bool bY57 = b57 && FMath::IsNearlyEqual(TL57.Y, FMath::FloorToFloat(A57.Y - 78.f), 0.01f) && FMath::IsNearlyEqual(TL57.X, FMath::FloorToFloat(A57.X - 96.f), 0.01f);
			Expect(bY55 && bY57, TEXT("позиция вывески: без титула/клана — Y − 72, X − 95 (190/2) ↔ с титулом — Y − 78, премиум X − 96 (192/2)"),
				FString::Printf(TEXT("55 (%.0f,%.0f)←(%.1f,%.1f); 57 (%.0f,%.0f)←(%.1f,%.1f)"), TL55.X, TL55.Y, A55.X, A55.Y, TL57.X, TL57.Y, A57.X, A57.Y));
		}

		// ═══ 6. GM-скрытие (4025 300) — прилавок снят ↔ показ (4025 301) — снова поставлен ═══
		{
			AsdaTestNet::DebugFeedPacket(Net, 4025, Make4025(kSeller, 300));
			G->UpdateStalls(true);
			const bool bHidden = !G->HasStall(kSeller);
			AsdaTestNet::DebugFeedPacket(Net, 4025, Make4025(kSeller, 301));
			G->UpdateStalls(true);
			Expect(bHidden && G->HasStall(kSeller), TEXT("GM-скрытие продавца (4025/300) — прилавок и вывеска сняты ↔ показ (4025/301) — поставлены"),
				FString::Printf(TEXT("скрыт → прилавок %d; показан → %d"), bHidden ? 0 : 1, G->HasStall(kSeller) ? 1 : 0));
		}

		// ═══ 7. 6304 «закрыта» — прилавок снят, плашка без признака ↔ другой продавец цел ═══
		{
			Feed(Net, G, 6304, Make6304(0, 0, CharOf(kLate), FString()));
			Pass(G);
			const AActor* A58 = WS->ActorByUserIdx(kLate);
			const UAsdaNameplateComponent* NP = A58 ? A58->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
			Expect(!G->HasStall(kLate) && NP && !NP->bShopOpen && G->HasStall(kSeller),
				TEXT("6304 «закрыта» (58) — прилавок и вывеска сняты, признак плашки снят ↔ прилавок 55 цел"), G->DumpState());
		}

		// ═══ 8. Уход продавца из обзора (4011) — прилавок снят СРАЗУ (актор ещё тает) ↔ остальные целы ═══
		{
			TArray<uint8> D; D.SetNumZeroed(2); AsdaBytes::WrI16(D.GetData(), (int16)kPremium);
			AsdaTestNet::DebugFeedPacket(Net, 4011, D);
			G->UpdateStalls(true);
			bool bOpen = false, bPrem = false; FString T;
			Expect(!G->HasStall(kPremium) && !G->GetCharFlags(CharOf(kPremium), bOpen, bPrem, T) && G->HasStall(kSeller),
				TEXT("уход продавца 57 (4011) — прилавок/вывеска сняты, признаки его персонажа забыты ↔ прилавок 55 цел"), G->DumpState());
		}

		// ═══ 9. Свой прилавок; смерть → 6305 раз в 4 с (реестр Л1-2) ↔ жив — нет; без своей вывески мёртвый — нет ═══
		{
			S->DebugSetHost(kSelfCharId, kSelfUserNum);
			Feed(Net, G, 6304, Make6304(1, 0, kSelfCharId, kShortTitle));
			Pass(G);
			const bool bOwn = G->HasStall(kSelfUserNum) && G->DebugSignWidget(kSelfUserNum);
			Caps->Reset();
			G->DebugExpireTimers(); G->RenderSigns();
			const int32 NAlive = Count(6305);
			const bool bDeadWas = AsdaTestNet::bSelfDead(Net);
			AsdaTestNet::bSelfDead(Net) = true;
			G->DebugExpireTimers(); G->RenderSigns();
			const int32 NDead = Count(6305);
			G->RenderSigns();
			const int32 NDead2 = Count(6305);
			TArray<uint8> P; Count(6305, &P);
			Expect(bOwn && NAlive == 0 && NDead == 1 && NDead2 == 1 && P.Num() == 6 && AsdaBytes::RdI32(P.GetData()) == kSelfCharId,
				TEXT("своя вывеска: смерть → 6305 (свой charId), повтор не раньше 4 с ↔ жив — нет"),
				FString::Printf(TEXT("своя=%d жив=%d мёртв=%d повтор=%d"), bOwn ? 1 : 0, NAlive, NDead, NDead2));
			Feed(Net, G, 6304, Make6304(0, 0, kSelfCharId, FString()));
			Pass(G);
			Caps->Reset();
			G->DebugExpireTimers(); G->RenderSigns();
			AsdaTestNet::bSelfDead(Net) = bDeadWas;
			Expect(!G->HasStall(kSelfUserNum) && Count(6305) == 0, TEXT("своя лавка закрыта (6304/0) — своей вывески нет, мёртвый — 6305 нет"),
				FString::Printf(TEXT("своя=%d 6305=%d"), G->HasStall(kSelfUserNum) ? 1 : 0, Count(6305)));
		}

		// ═══ 10. Реклама премиум-лавки 5086 раз в 300 с ↔ не премиум — нет ═══
		{
			S->bPremium = true;
			S->StoreTitle = kShortTitle;
			Feed(Net, G, 6304, Make6304(1, 1, kSelfCharId, kShortTitle));
			Pass(G);
			Caps->Reset();
			G->DebugExpireTimers(); G->RenderSigns();
			TArray<uint8> P; const int32 N1 = Count(5086, &P);
			G->RenderSigns();
			const int32 N2 = Count(5086);
			const AActor* Self = WS->ActorByUserIdx(kSelfUserNum);
			const FVector2D Gm = Self ? AsdaCoord::UeToGame2D(Self->GetActorLocation()) : FVector2D::ZeroVector;
			const FString Want = FString::Printf(TEXT("%s [X:%d.Y:%d]"), kShortTitle, (int32)(uint32)(uint64)(int64)(Gm.X / 100.0), (int32)(uint32)(uint64)(int64)(Gm.Y / 100.0));
			uint8 WantBytes[200] = {}; const int32 WN = FAsdaLoginData::EncodeCp1251(Want, WantBytes, 199);
			const bool bBody = P.Num() == 27 + WN && P[2] == 0 && FMemory::Memcmp(P.GetData() + 27, WantBytes, WN) == 0;
			const FString Name = P.Num() >= 27 ? FAsdaLoginData::DecodeCp1251(P.GetData() + 3, 24) : FString();
			S->bPremium = false;
			G->DebugExpireTimers(); G->RenderSigns();
			const int32 N3 = Count(5086);
			Expect(N1 == 1 && bBody && Name == WS->GetSelfCharName() && N2 == 1 && N3 == 1,
				TEXT("реклама: премиум → 5086 «название [X:x/100.Y:z/100]», +2 = 0, имя с +3; повтор не раньше 300 с ↔ не премиум — нет"),
				FString::Printf(TEXT("5086: %d → %d → %d; тело %d б «%s» имя «%s»"), N1, N2, N3, P.Num(), *Want, *Name));
		}

		// ═══ 12. ★ Волна Л4/Л5: [ИСПРАВЛЕНИЕ] К-9 (`asda.Trade.StallQueueFixK9`) — пары рецепта ревизии Л3 §3 ═══
		//   «открыта» для не появившегося X → 4005 X без 6304 (снапшот сервера: лавка закрыта) → прилавка нет ↔ ключ 0 — фантомный
		//   прилавок (как оригинал); 4005 + 6304 «открыта» → прилавок есть; схлопывание по charId ↔ ключ 0 — записи копятся.
		{
			IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Trade.StallQueueFixK9"));
			constexpr int32 kG1 = 59, kG2 = 60, kG3 = 61, kG4 = 62;
			// а) ключ 1: «открыта» 59 в очереди (персонажа нет) → 4005 59 (штатный спавн, маркер в потоке) → запись снята, прилавка нет.
			Feed(Net, G, 6304, Make6304(1, 0, CharOf(kG1), TEXT("Фантом")));
			const int32 QA0 = G->GetQueueNum();
			Feed(Net, G, 4005, Make4005(kG1, TEXT("Призрак")));
			const bool bSpawnA = WS->ActorByUserIdx(kG1) != nullptr;
			const int32 QA1 = G->GetQueueNum();
			Pass(G);
			const bool bNoStallA = !G->HasStall(kG1);
			// б) ключ 0: то же для 60 → запись живёт, персонаж появился — фантомный прилавок (оригинал).
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Trade.StallQueueFixK9"), 0); }
			Feed(Net, G, 6304, Make6304(1, 0, CharOf(kG2), TEXT("Фантом")));
			Feed(Net, G, 4005, Make4005(kG2, TEXT("Призрак2")));
			const int32 QB = G->GetQueueNum();
			Pass(G);
			const bool bPhantomB = G->HasStall(kG2);
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Trade.StallQueueFixK9"), 1); }
			Expect(Cv && QA0 == 1 && bSpawnA && QA1 == 0 && bNoStallA && QB == 1 && bPhantomB,
				TEXT("[ИСПРАВЛЕНИЕ] К-9: «открыта» 59 без персонажа → 4005 59 без 6304 — запись снята, прилавка нет ↔ ключ 0 — запись жива, фантомный прилавок 60"),
				FString::Printf(TEXT("ключ1: очередь %d→%d, спавн=%d, прилавок59=%d; ключ0: очередь %d, прилавок60=%d"), QA0, QA1, bSpawnA ? 1 : 0,
					bNoStallA ? 0 : 1, QB, bPhantomB ? 1 : 0));
			// в) 4005 61 + 6304 «открыта» 61 (снапшот сервера при открытой лавке) → прилавок есть.
			Feed(Net, G, 4005, Make4005(kG3, TEXT("Продавец2")));
			Feed(Net, G, 6304, Make6304(1, 0, CharOf(kG3), TEXT("Лавка 61")));
			Pass(G);
			Expect(G->HasStall(kG3) && G->GetQueueNum() == 0, TEXT("К-9: 4005 + 6304 «открыта» (снапшот при открытой лавке) → прилавок 61 есть, очередь пуста"),
				G->DumpState());
			// г) схлопывание по charId: «открыта» + «закрыта» 62 (персонажа нет) → одна запись («закрыта») ↔ ключ 0 — две.
			Feed(Net, G, 6304, Make6304(1, 0, CharOf(kG4), TEXT("Раз")));
			Feed(Net, G, 6304, Make6304(0, 0, CharOf(kG4), FString()));
			const int32 QD1 = G->GetQueueNum();
			const bool bLastClosed = QD1 == 1 && G->Queue.Last().CharId == CharOf(kG4) && G->Queue.Last().Open == 0;
			G->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Trade.StallQueueFixK9"), 0); }
			Feed(Net, G, 6304, Make6304(1, 0, CharOf(kG4), TEXT("Раз")));
			Feed(Net, G, 6304, Make6304(0, 0, CharOf(kG4), FString()));
			const int32 QD0 = G->GetQueueNum();
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Trade.StallQueueFixK9"), 1); }
			Expect(QD1 == 1 && bLastClosed && QD0 == 2, TEXT("К-9: схлопывание по charId — «открыта»+«закрыта» 62 → одна запись («закрыта») ↔ ключ 0 — две"),
				FString::Printf(TEXT("ключ1: %d (последняя «закрыта»=%d); ключ0: %d"), QD1, bLastClosed ? 1 : 0, QD0));
		}

		// ═══ 11. Сброс сессии (смена карты): всё снято, очередь и признаки пусты, пакетов нет ═══
		{
			Feed(Net, G, 6304, Make6304(1, 0, 999999, TEXT("Никто")));   // запись для персонажа, которого нет
			Caps->Reset();
			G->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			bool bOpen = false, bPrem = false; FString T;
			Expect(G->GetStallCount() == 0 && G->GetQueueNum() == 0 && !G->GetCharFlags(CharOf(kSeller), bOpen, bPrem, T) && Caps->Num() == 0,
				TEXT("сброс сессии (смена карты): прилавки/вывески сняты, очередь 6304 и признаки пусты, пакетов нет"),
				FString::Printf(TEXT("%s пакетов=%d"), *G->DumpState(), Caps->Num()));
		}
	}

	// ═══ ФАЗА «ПУТЁМ ИГРОКА» — шаги раз в 0,4 с, настоящие события Slate по живой геометрии ═══
	void BuildInputPhase()
	{
		using namespace AsdaStoreL3Local;
		auto Ctx = [](UWorld* W, UGameInstance*& GI, UAsdaStallSignSubsystem*& G, UAsdaSelfStoreSubsystem*& S, UAsdaNetSubsystem*& Net,
			UAsdaWindowSubsystem*& WM, UAsdaWorldSubsystem*& WS)
		{
			GI = W ? W->GetGameInstance() : nullptr;
			G = GI ? GI->GetSubsystem<UAsdaStallSignSubsystem>() : nullptr;
			S = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
			Net = GI ? AsdaTestNet::Get(GI) : nullptr;
			WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			return G && S && Net && WM && WS;
		};
		struct FClick { FVector2D At = FVector2D::ZeroVector; bool bOk = false; };
		TSharedRef<FClick> Clk = MakeShared<FClick>();
		// 0. Продавец 55 снова в лавке штатным 6304; Tick игры сам ставит прилавок и вывеску.
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaStallSignSubsystem* G; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM; UAsdaWorldSubsystem* WS;
			if (!Ctx(W, GI, G, S, Net, WM, WS)) { return; }
			AsdaTestNet::SelectedCharId(Net) = kSelfCharId; AsdaTestNet::SetSelfUserNumForDebug(Net, kSelfUserNum);
			S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			PlaceInFront(W, WS->ActorByUserIdx(kSeller), 350.f, -170.f);
			AsdaTestNet::DebugFeedPacket(Net, 6304, Make6304(1, 0, CharOf(kSeller), kShortTitle));
			G->DebugExpireTimers();   // следующий Tick разберёт очередь сразу (иначе — через 1 с, как у эталона)
		});
		Steps.Add([](UWorld*) {});   // кадр на отрисовку клона (живая геометрия)
		// 1. Геометрия рамки против `.JUI` и клик ЛКМ по центру вывески → 6307 (хозяин 900055/55); зона гасит мировой клик.
		Steps.Add([this, Ctx, Clk](UWorld* W)
		{
			UGameInstance* GI; UAsdaStallSignSubsystem* G; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM; UAsdaWorldSubsystem* WS;
			if (!Ctx(W, GI, G, S, Net, WM, WS)) { return; }
			FVector2D Mn, Mx;
			const bool bRect = G->SignAbsRect(kSeller, Mn, Mx);
			UUserWidget* Wd = G->DebugSignWidget(kSeller);
			const float Scale = Wd ? Wd->GetCachedGeometry().Scale : 0.f;
			const FVector2D Sz = bRect && Scale > 0.f ? (Mx - Mn) / Scale : FVector2D::ZeroVector;
			Expect(bRect && FMath::Abs(Sz.X - 190.f) <= 1.5f && FMath::Abs(Sz.Y - 25.f) <= 1.5f,
				TEXT("путь игрока: живая геометрия рамки вывески 23450 = 190×25 (`.JUI`) при масштабе вьюпорта"),
				FString::Printf(TEXT("есть=%d (%.1f,%.1f)–(%.1f,%.1f) размер %.1f×%.1f масштаб %.3f; %s"), bRect ? 1 : 0, Mn.X, Mn.Y, Mx.X, Mx.Y, Sz.X, Sz.Y, Scale, *G->DumpState()));
			if (!bRect) { return; }
			Clk->At = (Mn + Mx) * 0.5f;
			Clk->bOk = true;
			S->LastVisitReqAt = -10.0;
			Caps->Reset();
			WM->SimulateClickAt(Clk->At, /*HoldFrames*/ 3);
		});
		Steps.Add([](UWorld*) {});   // [ПОРТ-самотест] запас: удержание 3 кадра + кадр опроса при низкой частоте кадров -game
		Steps.Add([this, Ctx, Clk](UWorld* W)
		{
			UGameInstance* GI; UAsdaStallSignSubsystem* G; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM; UAsdaWorldSubsystem* WS;
			if (!Ctx(W, GI, G, S, Net, WM, WS)) { return; }
			TArray<uint8> P; const int32 N = Count(6307, &P);
			FName Zone; const bool bOver = WM->IsCursorOverAnyUiWindow(Zone);
			// ★ Волна Л4/Л5 (ревизия Л3, МЕЛК-3): «4006 нет» ничего не доказывало (офлайн мировой клик 4006 не даёт и мимо вывески).
			//   Доказательство погашенного мирового клика — зона интерфейса под курсором: этой же функцией `IsCursorOverAnyUiWindow`
			//   контроллер гасит мировой клик (`AsdaPlayerController.cpp`, `IsCursorOverUi`). Положительная половина — ровно
			//   `StallSign_55`; контроль (ниже) — клик мимо вывески: зоны вывески под курсором нет, мировой клик прошёл бы.
			Expect(Clk->bOk && N == 1 && P.Num() == 10 && AsdaBytes::RdI32(P.GetData()) == kSelfCharId && AsdaBytes::RdI32(P.GetData() + 4) == CharOf(kSeller)
				&& AsdaBytes::RdI16(P.GetData() + 8) == kSeller && bOver && Zone == FName(*FString::Printf(TEXT("StallSign_%d"), kSeller)),
				TEXT("путь игрока: ЛКМ по вывеске → 6307 (свой charId, хозяин 900055/55); под курсором зона StallSign_55 — мировой клик погашен"),
				FString::Printf(TEXT("6307=%d зона «%s»"), N, *Zone.ToString()));
			// Контроль: клик мимо вывески (ниже рамки на 60 px) — 6307 нет.
			S->LastVisitReqAt = -10.0;
			Caps->Reset();
			FVector2D Mn, Mx;
			if (G->SignAbsRect(kSeller, Mn, Mx)) { WM->SimulateClickAt(FVector2D((Mn.X + Mx.X) * 0.5f, Mx.Y + 60.f), 3); }
		});
		Steps.Add([](UWorld*) {});   // запас на клик (см. выше)
		Steps.Add([this, Ctx, Clk](UWorld* W)
		{
			UGameInstance* GI; UAsdaStallSignSubsystem* G; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM; UAsdaWorldSubsystem* WS;
			if (!Ctx(W, GI, G, S, Net, WM, WS)) { return; }
			FName ZoneMiss; const bool bOverMiss = WM->IsCursorOverAnyUiWindow(ZoneMiss);
			const bool bNoSignZone = !bOverMiss || !ZoneMiss.ToString().StartsWith(TEXT("StallSign_"));
			Expect(Count(6307) == 0 && bNoSignZone, TEXT("путь игрока: ЛКМ мимо вывески (контроль) → 6307 нет, зоны вывески под курсором нет (мировой клик не погашен)"),
				FString::Printf(TEXT("6307=%d над UI=%d зона «%s»"), Count(6307), bOverMiss ? 1 : 0, *ZoneMiss.ToString()));
			// Контроль гейта: я хозяин открытой витрины — клик по вывеске молча.
			S->bSpecialShop = true;
			S->LastVisitReqAt = -10.0;
			Caps->Reset();
			if (Clk->bOk) { WM->SimulateClickAt(Clk->At, 3); }
		});
		Steps.Add([](UWorld*) {});   // запас на клик (см. выше)
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaStallSignSubsystem* G; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM; UAsdaWorldSubsystem* WS;
			if (!Ctx(W, GI, G, S, Net, WM, WS)) { return; }
			S->bSpecialShop = false;
			Expect(Count(6307) == 0, TEXT("путь игрока: ЛКМ по вывеске, когда я хозяин открытой витрины (`byte_2C2FC0C == 1`) → молча"),
				FString::Printf(TEXT("6307=%d"), Count(6307)));
			// Точка вывески стоящего продавца — для пары «сидит».
			FVector2D TL;
			G->DebugSignPlacement(kSeller, TL, StandAnchor);
			AsdaTestNet::DebugFeedPacket(Net, 4025, Make4025(kSeller, 108));   // «сидит» (продавец в лавке сидит — поза 107/108)
		});
		// 2. Сидящий продавец (состояние 8): точка вывески — центр AABB (`soundPos`), ниже стоящей ↔ стоя — верх AABB + 10.
		for (int32 k = 0; k < 8; ++k)
		{
			Steps.Add([this, Ctx, k](UWorld* W)
			{
				UGameInstance* GI; UAsdaStallSignSubsystem* G; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM; UAsdaWorldSubsystem* WS;
				if (!Ctx(W, GI, G, S, Net, WM, WS)) { return; }
				if (SitPolls < 0) { return; }   // проверка уже сделана
				const int32 State = UAsdaKingCrownSubsystem::EtalonActionStateOf(WS->ActorByUserIdx(kSeller));
				if (State != 8 && k < 7) { ++SitPolls; return; }
				FVector2D TL, A;
				const bool bP = G->DebugSignPlacement(kSeller, TL, A);
				Expect(State == 8 && bP && A.Y > StandAnchor.Y + 5.f,
					TEXT("сидящий продавец (4025/108, состояние 8): точка вывески — центр AABB, на экране ниже ↔ стоя — верх AABB + 10"),
					FString::Printf(TEXT("состояние %d, точка стоя Y=%.1f, сидя Y=%.1f (ожиданий %d)"), State, StandAnchor.Y, A.Y, SitPolls));
				SitPolls = -1;
			});
		}
		// 3. Кадр со слоем UI (закон `full-1to1-reproduction` — смотреть глазами): прилавок shop.jbm под сидящим продавцом и вывеска
		//   над ним. Файл — `<Saved>/Screenshots/WindowsEditor/trade_l3_stall.png` (общая точка `asda.UI.Shot`).
		Steps.Add([](UWorld* W) { if (GEngine) { GEngine->Exec(W, TEXT("asda.UI.Shot trade_l3_stall")); } });
		Steps.Add([](UWorld*) {});   // кадр на запись снимка
	}

	void Disarm(UAsdaNetSubsystem* Net) { if (Net) { AsdaSelfTestKit::DisarmCapture(Net, Gen); } }
};

namespace
{
	// ★ R5-fix-c (13.11.2026): своя `ParseStoreL3Delay` (дословная копия у двенадцати самотестов) убрана — разбор `delay=N` в общей точке
	//   `AsdaSelfTestKit::DelayArg` (`Test/AsdaSelfTestKit.h`), вызовы ниже идут в неё.

	FAutoConsoleCommandWithWorldAndArgs GAsdaTradeSelfTestStoreL3(
		TEXT("asda.Trade.SelfTestStoreL3"),
		TEXT("asda.Trade.SelfTestStoreL3 [delay=N] [quit] — самотест вывесок личных лавок (волна Л3): 6304 → очередь → признаки, прилавки shop.jbm, "
		     "вывески 23450/23470 (обрезка, позиция), GM-скрытие, уход 4011, своя вывеска (смерть → 6305), реклама 5086, сброс; путь игрока — "
		     "клик по вывеске → 6307, сидящий продавец. Только офлайн."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Trade.SelfTestStoreL3"), KitArgs)) { return; }
			const float Delay = AsdaSelfTestKit::DelayArg(Args);
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit](float) -> bool
			{
				UWorld* Wl = WW.Get();
				if (!Wl) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(Wl, TEXT("asda.Trade.SelfTestStoreL3"), bQuit)) { return false; }   // Р-2: офлайн повторно — на отложенном старте
				const AAsdaPlayerController* P = Cast<AAsdaPlayerController>(Wl->GetFirstPlayerController());
				const bool bNeedSpawn = !P || !P->GetPawn();
				if (bNeedSpawn && GEngine)
				{
					GEngine->Exec(Wl, TEXT("Asda.Load.CurrentMap 3"));
					GEngine->Exec(Wl, TEXT("Asda.World.FakeSelfSpawn 256 256"));
				}
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit](float) -> bool
				{
					UWorld* W2 = WW.Get();
					if (!W2) { return false; }
					TSharedRef<FAsdaSelfStoreL3SelfTest> T = MakeShared<FAsdaSelfStoreL3SelfTest>();
					T->Run(W2);
					T->BuildInputPhase();
					TSharedRef<int32> Idx = MakeShared<int32>(0);
					FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Idx, bQuit](float) -> bool
					{
						UWorld* W3 = WW.Get();
						if (!W3) { AsdaCVarSave::RestoreAll(TEXT("asda.Trade.SelfTestStoreL3")); return false; }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи — и при уходе мира
						if (*Idx < T->Steps.Num()) { T->Steps[(*Idx)++](W3); return true; }
						UGameInstance* GI3 = W3->GetGameInstance();
						if (UAsdaNetSubsystem* Net3 = GI3 ? AsdaTestNet::Get(GI3) : nullptr) { T->Disarm(Net3); }
						if (UAsdaStallSignSubsystem* G3 = GI3 ? GI3->GetSubsystem<UAsdaStallSignSubsystem>() : nullptr) { G3->ResetSessionState(EAsdaSessionResetReason::MapChanged); }
						if (UAsdaSelfStoreSubsystem* S3 = GI3 ? GI3->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr) { S3->ResetSessionState(EAsdaSessionResetReason::MapChanged); }
						if (UAsdaWindowSubsystem* WM3 = GI3 ? GI3->GetSubsystem<UAsdaWindowSubsystem>() : nullptr) { WM3->ClearMsgBoxQueue(TEXT("самотест Л3: итог")); }
						// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключ Trade.StallQueueFixK9 — к исходному значению И приоритету SetBy (прежде шаги
						//   возвращали константу 1 с SetByConsole).
						AsdaCVarSave::RestoreAll(TEXT("asda.Trade.SelfTestStoreL3"));
						UE_LOG(LogAsdaStoreTestL3, Log, TEXT("САМОТЕСТ-ЛАВКА-Л3 ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
						T->SelfScope.Restore();   // ★ В3 S1 (N1(г)): «свой» — как до прогона
						AsdaSelfTestKit::Quit(W3, bQuit);
						return false;
					}), 0.4f);
					return false;
				}), bNeedSpawn ? 2.f : 0.f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaStoreTestL3, Log, TEXT("САМОТЕСТ-ЛАВКА-Л3: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
