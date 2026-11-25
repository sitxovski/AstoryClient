// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ПОКАЗА ШЛЕМА И ХРАНИТЕЛЯ (25.10.2026): `asda.SelfTest.HelmetShow [quit]`. [ПОРТ]
//   Жалоба игроков: «Show Helmet не работает — шлем виден и после отключения; Soul Guardian так же не скрывается».
//   Реверс (эталон и сервер) — у объявлений `FAsdaWornParts::bHelmetShown`, `UAsdaNetSubsystem::SelfHelmetShow`,
//   `FAsdaRemotePlayer::HelmetShow`, `OnHelmetShowSync`, `UAsdaSystemOptionSubsystem::ApplyServerOptionFlags`.
//   Офлайн (`-AsdaNoNet`), карта мира. Реальный путь (закон diagnostic-must-use-real-path): пакеты 1027 / 4003 / 4005 / 4021 /
//   4025 подаются в общий разбор входящих (`DebugFeedPacket`); «ОК» окна — `DebugConfirmCharacterDisplay` (тот же хвост, что
//   `optApplySettingsFromConfig`: накат «Интерфейса», свой шлем, пакет 4070 — перехват отправки).
//   Проверяется по МОДЕЛИ (компоненты сборки с тегом части), а не только по флагу:
//     A) 1027: блок опций → буфер окна (заявки, имена, шлем); ключ asda.SysOpt.ServerFlags 0 — блок не принят;
//     B) свой: 4003 [8] = 0 → шлем не рисуется, причёска обычная; «ОК» с флагом 1/0 — шлем появляется/исчезает (причёска по
//        режиму шлема), 4070 [15] = флаг; смена шлема при снятом флаге — новый шлем не рисуется; портал (повторный 4003) с
//        [8] = 0 и = 1; ключ asda.Char.HelmetShowFlag 0 — прежний порт (шлем рисуется всегда);
//     C) сосед: 4005 [6] = 0 → без шлема, обычная причёска; 4025/111 флаг 1 → шлем и «подшлемная» причёска (жен., режим 1),
//        флаг 0 → снова без шлема; смена шлема соседу (4021) при снятом флаге; контроль 4005 [6] = 1; 4025/111 про
//        отсутствующего — ничего;
//     D) хранитель: «Показывать моего / других хранителей» в окне → свой / чужой компаньон уходит и возвращается.
//   Итог: «ШЛЕМ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Characters/AsdaCharAssembly.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaSystemOptionSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaSoulGuardianActor.h"
#include "Test/AsdaSelfTestKit.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaHelmetTest, Log, All);

namespace AsdaHelmetTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.HelmetShow");   // имя команды
	static const TCHAR* kKeyFlag = TEXT("asda.Char.HelmetShowFlag");     // ключ отката: флаг показа в сборке
	static const TCHAR* kKeySrv  = TEXT("asda.SysOpt.ServerFlags");      // ключ отката: блок опций 1027
	static const TCHAR* kUnderHelmetF = TEXT("ch1_f_00_hair000_00_00_1"); // «подшлемная» причёска (эталон LoadHairModel(char, 1))
	constexpr int32 kPeerA = 50;     // сосед с флагом 0
	constexpr int32 kPeerB = 51;     // сосед-контроль с флагом 1
	constexpr int32 kPeerMissing = 77;   // userIdx, которого нет в зоне
	constexpr int32 kSgIdx = 3;      // индекс хранителя (0..26 — есть в SoulGuarderTable)

	// Счётчики и вывод.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;   // счётчики проверок
		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaHelmetTest, Log,   TEXT("ШЛЕМ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaHelmetTest, Error, TEXT("ШЛЕМ FAIL %s"), *What); }
		}
	};

	// Сколько ЖИВЫХ компонентов-частей сборки несут скелетный меш `MeshName` (шлем рисуется ⇔ > 0).
	static int32 CountPartMesh(const AActor* A, const FString& MeshName)
	{
		if (!A || MeshName.IsEmpty()) { return 0; }
		int32 N = 0;
		TInlineComponentArray<USkeletalMeshComponent*> Comps(A);
		for (const USkeletalMeshComponent* C : Comps)
		{
			if (!IsValid(C) || !C->IsRegistered() || !C->ComponentHasTag(UAsdaCharAssembly::WornPartTag)) { continue; }
			const USkeletalMesh* M = C->GetSkeletalMeshAsset();
			if (M && M->GetName() == MeshName) { ++N; }
		}
		return N;
	}

	// Имя меша причёски (жёсткий меш с тегом части); пусто — причёски нет.
	static FString HairMeshOf(const AActor* A)
	{
		if (!A) { return FString(); }
		TInlineComponentArray<UStaticMeshComponent*> Comps(A);
		for (const UStaticMeshComponent* C : Comps)
		{
			if (!IsValid(C) || !C->IsRegistered() || !C->ComponentHasTag(UAsdaCharAssembly::WornPartTag)) { continue; }
			if (C->GetName().StartsWith(TEXT("HairMesh")) && C->GetStaticMesh()) { return C->GetStaticMesh()->GetName(); }
		}
		return FString();
	}

	// Шлем ItemType 0 с нужным режимом волос по полу и с загружаемым мешем; `Except` — пропустить этот код. 0 — нет.
	static int32 FindHelmet(UAsdaItemTableSubsystem* Items, bool bMale, int32 WantMode, int32 Except)
	{
		for (int32 Id = 20000; Id < 140000; ++Id)
		{
			if (Id == Except) { continue; }
			const FAsdaItemRecord* Rec = Items->GetItem(Id);
			if (!Rec || Rec->ItemType != 0) { continue; }
			const FAsdaImgRecord* Img = Items->GetImgRecord(Rec->ImgIndex);
			if (!Img) { continue; }
			const int32 Mode = bMale ? Img->SubTypeMale : Img->SubTypeFemale;
			if (WantMode >= 0 && Mode != WantMode) { continue; }
			if (WantMode < 0 && Mode != 0 && Mode != 1 && Mode != 2) { continue; }
			const FString Mesh = Items->GetItemMeshName(Id, bMale);
			if (Mesh.IsEmpty() || !UAsdaCharAssembly::LoadEquipPartMesh(Mesh)) { continue; }
			return Id;
		}
		return 0;
	}

	// Тело 4003 (9 б): userNum, тайлы, состояние, байт показа шлема.
	static TArray<uint8> Body4003(int32 Me, uint8 Helmet)
	{
		TArray<uint8> B; B.SetNumZeroed(9);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Me);
		AsdaBytes::WrI16(B.GetData() + 2, 256);
		AsdaBytes::WrI16(B.GetData() + 4, 256);
		AsdaBytes::WrI16(B.GetData() + 6, 0);
		B[8] = Helmet;
		return B;
	}

	// Тело 4005 (151 б): соседка (пол 2), причёска 0, шлем в слоте 0, байт показа шлема [6].
	static TArray<uint8> Body4005(int32 Uidx, int32 HelmetId, uint8 Show, float GameX, float GameZ)
	{
		TArray<uint8> B; B.SetNumZeroed(151);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Uidx);
		B[6] = Show;
		AsdaBytes::WrI32(B.GetData() + 11, 990000 + Uidx);   // charId — заведомо не свой
		const char* Name = "HelmTest";
		FMemory::Memcpy(B.GetData() + 15, Name, FCStringAnsi::Strlen(Name));
		B[37] = 2; B[38] = 1; B[40] = 10;                     // пол, класс, уровень
		B[48] = 0; B[49] = 0; B[50] = 0;                      // причёска, цвет, лицо
		FMemory::Memcpy(B.GetData() + 56, &GameX, 4);
		FMemory::Memcpy(B.GetData() + 60, &GameZ, 4);
		AsdaBytes::WrI32(B.GetData() + 96, HelmetId);         // слот 0 — шлем
		return B;
	}

	// Тело 4025 (13 б): userIdx, действие 111, флаг @4.
	static TArray<uint8> Body4025(int32 Uidx, uint8 Flag)
	{
		TArray<uint8> B; B.SetNumZeroed(13);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Uidx);
		AsdaBytes::WrI16(B.GetData() + 2, 111);
		B[4] = Flag;
		return B;
	}

	// Тело 1027 (651 б): успех, блок опций с +632.
	static TArray<uint8> Body1027(const uint8 (&Blk)[19])
	{
		TArray<uint8> B; B.SetNumZeroed(651);
		B[0] = 1;
		FMemory::Memcpy(B.GetData() + 632, Blk, 19);
		return B;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestHelmetShow(
	TEXT("asda.SelfTest.HelmetShow"),
	TEXT("Самотест показа шлема и хранителя души: 1027 (опции персонажа), 4003 [8], «ОК» окна + 4070 [15], смена шлема при снятом флаге, "
	     "портал, 4005 [6] / 4025 действие 111 / 4021 у соседа, опции хранителей. Офлайн, на карте мира. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaHelmetTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		UAsdaSystemOptionSubsystem* Opt = GI ? GI->GetSubsystem<UAsdaSystemOptionSubsystem>() : nullptr;
		if (!Net || !WS || !Items || !Opt || !IConsoleManager::Get().FindConsoleVariable(kKeyFlag) || !IConsoleManager::Get().FindConsoleVariable(kKeySrv))
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем сети/мира/предметов/настроек или ключей отката"));
			return;
		}
		FRun R;
		FAsdaNetSelfScope SelfScope(Net, kCmd);
		FAsdaCVarScope CVars(W, kCmd);
		const int32 Me = 4321;

		// ═══ 0. Шлем соседки из таблицы предметов (свои — после спавна, по полу своего персонажа) ═══
		const int32 HmF1 = FindHelmet(Items, /*bMale*/false, 1, 0);         // женский, режим 1 («подшлемная» причёска)
		const FString MeshF1 = Items->GetItemMeshName(HmF1, false);
		R.Check(HmF1 > 0, FString::Printf(TEXT("0 шлем соседки: жен. режим 1 = %d (%s)"), HmF1, *MeshF1));
		if (HmF1 <= 0) { AsdaSelfTestKit::Finish(W, kCmd, KA.bQuit, LogAsdaHelmetTest, TEXT("ШЛЕМ"), R.Ok, R.Fail); return; }

		// ═══ A. 1027 — блок опций персонажа → окно ═══
		{
			uint8 Blk[19] = { 1,1,1,1,1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0,0,0 };   // [5]=0 [7]=1 [8]=0 [9]=1 [10]=0 [11]=1 [12]=0 [13]=1 [14]=0 [15]=0
			CVars.Set(kKeySrv, 1);
			Net->DebugFeedPacket(1027, Body1027(Blk));
			R.Check(Net->ServerOptionBlock.Num() == 19 && Net->ServerOptionBlock[15] == 0, TEXT("A1 1027: сеть сохранила блок 19 б, [15] = 0"));
			const int32 G1[7] = { Opt->GetInterfaceFlag(1,0), Opt->GetInterfaceFlag(1,1), Opt->GetInterfaceFlag(1,2), Opt->GetInterfaceFlag(1,3),
			                      Opt->GetInterfaceFlag(1,4), Opt->GetInterfaceFlag(1,5), Opt->GetInterfaceFlag(1,6) };
			R.Check(G1[0] == 1 && G1[1] == 1 && G1[2] == 0 && G1[3] == 1 && G1[4] == 0 && G1[5] == 0 && G1[6] == 0,
				FString::Printf(TEXT("A2 «Интерфейс-1» из блока (байты 7,9,10,11,12,5,8): %d%d%d%d%d%d%d (ждём 1101000)"), G1[0], G1[1], G1[2], G1[3], G1[4], G1[5], G1[6]));
			R.Check(Opt->GetInterfaceFlag(2,0) == 1 && Opt->GetInterfaceFlag(2,1) == 0 && Opt->GetInterfaceFlag(3,1) == 0,
				FString::Printf(TEXT("A3 «Интерфейс-2» №0/№1 = %d/%d (ждём 1/0), «Показывать шлем» = %d (ждём 0)"),
					Opt->GetInterfaceFlag(2,0), Opt->GetInterfaceFlag(2,1), Opt->GetInterfaceFlag(3,1)));
			CVars.Set(kKeySrv, 0);
			Blk[15] = 1;
			Net->DebugFeedPacket(1027, Body1027(Blk));
			R.Check(Opt->GetInterfaceFlag(3,1) == 0, TEXT("A4 ключ asda.SysOpt.ServerFlags 0: блок не принят (шлем в окне остался 0)"));
			CVars.Set(kKeySrv, 1);
		}

		// ═══ B. Свой персонаж ═══
		CVars.Set(kKeyFlag, 1);
		Net->SelfHair = 0; Net->SelfHairColor = 0;   // причёска 0 — чтобы её меш был виден на месте шлема
		GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
		Net->DebugFeedPacket(4003, Body4003(Me, 0));
		AActor* MeA = WS->LocalPlayerActor.Get();
		R.Check(MeA && Net->SelfHelmetShow == 0 && !WS->SelfParts.bHelmetShown,
			FString::Printf(TEXT("B1 4003 [8] = 0: павн %s, сеть %u, снимок %d"), MeA ? TEXT("есть") : TEXT("НЕТ"), (uint32)Net->SelfHelmetShow, WS->SelfParts.bHelmetShown ? 1 : 0));
		if (!MeA) { AsdaSelfTestKit::Finish(W, kCmd, KA.bQuit, LogAsdaHelmetTest, TEXT("ШЛЕМ"), R.Ok, R.Fail); return; }
		const bool bMeMale = (WS->SelfSex != 2);
		const FString HairNormalMe = UAsdaCharAssembly::ResolveHairMeshName(bMeMale, 0, 0, Items);
		const int32 HmM2 = FindHelmet(Items, bMeMale, 2, 0);          // режим 2 (волос под шлемом нет) — видно «причёска вместо шлема»
		const int32 HmMx = FindHelmet(Items, bMeMale, -1, HmM2);      // второй шлем — «смена шлема при снятом флаге»
		const FString MeshM2 = Items->GetItemMeshName(HmM2, bMeMale);
		const FString MeshMx = Items->GetItemMeshName(HmMx, bMeMale);
		R.Check(HmM2 > 0 && HmMx > 0, FString::Printf(TEXT("B1a свои шлемы (пол %d): режим 2 = %d (%s), второй = %d (%s)"),
			(int32)WS->SelfSex, HmM2, *MeshM2, HmMx, *MeshMx));
		if (HmM2 <= 0 || HmMx <= 0) { AsdaSelfTestKit::Finish(W, kCmd, KA.bQuit, LogAsdaHelmetTest, TEXT("ШЛЕМ"), R.Ok, R.Fail); return; }
		WS->ApplySelfEquipPart(0, HmM2);
		MeA = WS->LocalPlayerActor.Get();
		R.Check(WS->SelfParts.Helmet == HmM2 && CountPartMesh(MeA, MeshM2) == 0 && HairMeshOf(MeA) == HairNormalMe,
			FString::Printf(TEXT("B2 надет шлем %d при флаге 0: компонентов шлема %d (ждём 0), причёска «%s» (ждём обычную «%s»)"),
				HmM2, CountPartMesh(MeA, MeshM2), *HairMeshOf(MeA), *HairNormalMe));

		uint8 Sent15 = 0xFF; int32 Sent4070 = 0;
		const uint32 Gen = AsdaSelfTestKit::ArmCapture(Net, [&Sent15, &Sent4070](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (Op == 4070 && Len >= 16) { Sent15 = P[15]; ++Sent4070; }
		}, 4070, kCmd);
		R.Check(Gen != 0, TEXT("B3 перехват отправки 4070 взведён"));

		Opt->DebugConfirmCharacterDisplay(3, 1, true);
		MeA = WS->LocalPlayerActor.Get();
		R.Check(WS->SelfParts.bHelmetShown && CountPartMesh(MeA, MeshM2) == 1,
			FString::Printf(TEXT("B4 «ОК» с «Показывать шлем» = 1: снимок %d, компонентов шлема %d (ждём 1)"),
				WS->SelfParts.bHelmetShown ? 1 : 0, CountPartMesh(MeA, MeshM2)));
		R.Check(HairMeshOf(MeA).IsEmpty(), FString::Printf(TEXT("B5 шлем режима 2: причёски под шлемом нет («%s»)"), *HairMeshOf(MeA)));
		R.Check(Sent4070 == 1 && Sent15 == 1, FString::Printf(TEXT("B6 4070 ушёл (%d раз), байт [15] = %u (ждём 1)"), Sent4070, (uint32)Sent15));

		Opt->DebugConfirmCharacterDisplay(3, 1, false);
		MeA = WS->LocalPlayerActor.Get();
		R.Check(!WS->SelfParts.bHelmetShown && CountPartMesh(MeA, MeshM2) == 0 && HairMeshOf(MeA) == HairNormalMe,
			FString::Printf(TEXT("B7 «ОК» с «Показывать шлем» = 0: компонентов шлема %d (0), причёска «%s» (обычная)"), CountPartMesh(MeA, MeshM2), *HairMeshOf(MeA)));
		R.Check(Sent4070 == 2 && Sent15 == 0, FString::Printf(TEXT("B8 4070 ушёл (%d раз), байт [15] = %u (ждём 0)"), Sent4070, (uint32)Sent15));
		AsdaSelfTestKit::DisarmCapture(Net, Gen);

		WS->ApplySelfEquipPart(0, HmMx);
		MeA = WS->LocalPlayerActor.Get();
		R.Check(WS->SelfParts.Helmet == HmMx && CountPartMesh(MeA, MeshMx) == 0 && CountPartMesh(MeA, MeshM2) == 0 && HairMeshOf(MeA) == HairNormalMe,
			FString::Printf(TEXT("B9 смена шлема при снятом флаге (%d → %d): нового шлема %d, старого %d (оба 0), причёска «%s»"),
				HmM2, HmMx, CountPartMesh(MeA, MeshMx), CountPartMesh(MeA, MeshM2), *HairMeshOf(MeA)));

		CVars.Set(kKeyFlag, 0);
		WS->ApplySelfEquipPart(0, HmM2);
		MeA = WS->LocalPlayerActor.Get();
		R.Check(CountPartMesh(MeA, MeshM2) == 1, FString::Printf(TEXT("B10 ключ asda.Char.HelmetShowFlag 0 (прежний порт): шлем рисуется вопреки флагу (%d)"), CountPartMesh(MeA, MeshM2)));
		CVars.Set(kKeyFlag, 1);

		// Портал: повторный 4003 (новый павн, снимок спавна заново).
		Net->DebugFeedPacket(4003, Body4003(Me, 0));
		WS->ApplySelfEquipPart(0, HmM2);
		MeA = WS->LocalPlayerActor.Get();
		R.Check(MeA && !WS->SelfParts.bHelmetShown && CountPartMesh(MeA, MeshM2) == 0 && HairMeshOf(MeA) == HairNormalMe,
			FString::Printf(TEXT("B11 портал, 4003 [8] = 0: снимок %d, шлема %d (0), причёска «%s»"), WS->SelfParts.bHelmetShown ? 1 : 0, CountPartMesh(MeA, MeshM2), *HairMeshOf(MeA)));
		Net->DebugFeedPacket(4003, Body4003(Me, 1));
		WS->ApplySelfEquipPart(0, HmM2);
		MeA = WS->LocalPlayerActor.Get();
		R.Check(MeA && WS->SelfParts.bHelmetShown && CountPartMesh(MeA, MeshM2) == 1,
			FString::Printf(TEXT("B12 портал, 4003 [8] = 1: снимок %d, шлема %d (1)"), WS->SelfParts.bHelmetShown ? 1 : 0, CountPartMesh(MeA, MeshM2)));

		// ═══ C. Сосед ═══
		const FVector2D MePos(25650.f, 25650.f);   // тайл 256 → 256*100+50
		const FString HairNormalF = UAsdaCharAssembly::ResolveHairMeshName(false, 0, 0, Items);
		Net->DebugFeedPacket(4005, Body4005(kPeerA, HmF1, 0, MePos.X + 200.f, MePos.Y));
		bool bShowA = true;
		AActor* PA = WS->ActorByUserIdx(kPeerA);
		const bool bHasA = WS->GetRemoteHelmetShow(kPeerA, bShowA);
		R.Check(PA && bHasA && !bShowA && CountPartMesh(PA, MeshF1) == 0 && HairMeshOf(PA) == HairNormalF,
			FString::Printf(TEXT("C1 4005 [6] = 0: сосед %s, флаг %d, шлема %d (0), причёска «%s» (обычная «%s»)"),
				PA ? TEXT("есть") : TEXT("НЕТ"), bShowA ? 1 : 0, CountPartMesh(PA, MeshF1), *HairMeshOf(PA), *HairNormalF));
		Net->DebugFeedPacket(4025, Body4025(kPeerA, 1));
		PA = WS->ActorByUserIdx(kPeerA);
		R.Check(WS->GetRemoteHelmetShow(kPeerA, bShowA) && bShowA && CountPartMesh(PA, MeshF1) == 1 && HairMeshOf(PA) == kUnderHelmetF,
			FString::Printf(TEXT("C2 4025/111 флаг 1: шлема %d (1), причёска «%s» (подшлемная «%s»)"), CountPartMesh(PA, MeshF1), *HairMeshOf(PA), kUnderHelmetF));
		Net->DebugFeedPacket(4025, Body4025(kPeerA, 0));
		PA = WS->ActorByUserIdx(kPeerA);
		R.Check(WS->GetRemoteHelmetShow(kPeerA, bShowA) && !bShowA && CountPartMesh(PA, MeshF1) == 0 && HairMeshOf(PA) == HairNormalF,
			FString::Printf(TEXT("C3 4025/111 флаг 0: шлема %d (0), причёска «%s» (обычная)"), CountPartMesh(PA, MeshF1), *HairMeshOf(PA)));
		{
			const int32 HmF1b = FindHelmet(Items, /*bMale*/false, -1, HmF1);
			TArray<uint8> B4021; B4021.SetNumZeroed(9);
			AsdaBytes::WrI16(B4021.GetData() + 0, (int16)kPeerA);
			AsdaBytes::WrI16(B4021.GetData() + 2, 0);
			AsdaBytes::WrI32(B4021.GetData() + 4, HmF1b);
			Net->DebugFeedPacket(4021, B4021);
			PA = WS->ActorByUserIdx(kPeerA);
			R.Check(HmF1b > 0 && CountPartMesh(PA, Items->GetItemMeshName(HmF1b, false)) == 0 && HairMeshOf(PA) == HairNormalF,
				FString::Printf(TEXT("C4 4021 смена шлема соседу при снятом флаге (→ %d): шлема %d (0), причёска «%s»"),
					HmF1b, CountPartMesh(PA, Items->GetItemMeshName(HmF1b, false)), *HairMeshOf(PA)));
		}
		Net->DebugFeedPacket(4005, Body4005(kPeerB, HmF1, 1, MePos.X - 200.f, MePos.Y));
		AActor* PB = WS->ActorByUserIdx(kPeerB);
		bool bShowB = false;
		R.Check(PB && WS->GetRemoteHelmetShow(kPeerB, bShowB) && bShowB && CountPartMesh(PB, MeshF1) == 1 && HairMeshOf(PB) == kUnderHelmetF,
			FString::Printf(TEXT("C5 контроль 4005 [6] = 1: шлема %d (1), причёска «%s» (подшлемная)"), CountPartMesh(PB, MeshF1), *HairMeshOf(PB)));
		Net->DebugFeedPacket(4025, Body4025(kPeerMissing, 0));
		bool bShowM = true;
		R.Check(!WS->GetRemoteHelmetShow(kPeerMissing, bShowM), TEXT("C6 4025/111 про отсутствующего — записи соседа не появилось"));

		// ═══ D. Хранитель души — опции окна ═══
		WS->OnSoulGuardIndex(Me, kSgIdx);
		WS->OnSoulGuardIndex(kPeerB, kSgIdx);
		Opt->DebugConfirmCharacterDisplay(3, 3, true);
		Opt->DebugConfirmCharacterDisplay(3, 4, true);
		WS->TickSoulGuardians();
		auto Alive = [WS](int32 Key) { const AAsdaSoulGuardianActor* G = WS->FindSoulGuardian(Key); return G && !G->bRemoving; };
		R.Check(Alive(Me) && Alive(kPeerB), FString::Printf(TEXT("D1 опции вкл.: свой хранитель %s, чужой %s"),
			Alive(Me) ? TEXT("есть") : TEXT("НЕТ"), Alive(kPeerB) ? TEXT("есть") : TEXT("НЕТ")));
		Opt->DebugConfirmCharacterDisplay(3, 3, false);
		WS->TickSoulGuardians();
		R.Check(!Alive(Me) && Alive(kPeerB) && !WS->GetShowAngel(true),
			FString::Printf(TEXT("D2 «Показывать моего хранителя» = 0: свой %s, чужой %s"), Alive(Me) ? TEXT("ВИДЕН") : TEXT("уходит"), Alive(kPeerB) ? TEXT("есть") : TEXT("НЕТ")));
		Opt->DebugConfirmCharacterDisplay(3, 4, false);
		WS->TickSoulGuardians();
		R.Check(!Alive(kPeerB) && !WS->GetShowAngel(false), FString::Printf(TEXT("D3 «Показывать других хранителей» = 0: чужой %s"), Alive(kPeerB) ? TEXT("ВИДЕН") : TEXT("уходит")));
		Opt->DebugConfirmCharacterDisplay(3, 3, true);
		Opt->DebugConfirmCharacterDisplay(3, 4, true);
		WS->TickSoulGuardians();
		R.Check(Alive(Me) && Alive(kPeerB), FString::Printf(TEXT("D4 опции снова вкл.: свой %s, чужой %s"),
			Alive(Me) ? TEXT("есть") : TEXT("НЕТ"), Alive(kPeerB) ? TEXT("есть") : TEXT("НЕТ")));
		WS->OnSoulGuardIndex(Me, -1);
		WS->OnSoulGuardIndex(kPeerB, -1);
		WS->TickSoulGuardians();

		AsdaSelfTestKit::Finish(W, kCmd, KA.bQuit, LogAsdaHelmetTest, TEXT("ШЛЕМ"), R.Ok, R.Fail);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
