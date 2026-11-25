// Copyright (c) 2026 @sitxovski (Discord)
// Реализация дуэли `CDuelMgr` и оверлея `PVP_Gfont_Win` 1200 (шапка и поток — в .h).
//   Волна 2-В (06.10.2026), отчёт `Reports/owner_1006/wave2C_duel_mark_banner.md` §1.
#include "UI/AsdaDuelSubsystem.h"
#include "AsdaPerf.h"   // R4, доля a: области замера времени игрового потока
#include "UI/AsdaUiVisibilitySubsystem.h"   // ★ 21.10.2026: Alt+Z — флаг «интерфейс скрыт» (владелец)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"                       // RdI16/RdI32 — поля тел 4302..4305
#include "Login/AsdaLoginData.h"             // DecodeCp1251 — имена в теле
#include "UI/AsdaHotbarSubsystem.h"          // значок уведомления 14 (`statusBarAddIcon` / `buffSlotCompact`)
#include "UI/AsdaPartySubsystem.h"           // окно 2245 — общее с отрядом
#include "UI/AsdaMiniStateSubsystem.h"       // PvP-иконка портрета (`sub_922DFD(g_pHUD, 0)`)
#include "UI/AsdaEffectsSubsystem.h"         // `pvp_win.jeb` / `pvp_lose.jeb` (привязка 6)
#include "UI/AsdaNotice.h"                   // нотисы: журнал/буфер/окно-сообщение
#include "UI/AsdaUiHelpers.h"                // FormatPrintfArgs — loc-строка как формат `sprintf`
#include "World/AsdaWorldSubsystem.h"        // поиск персонажей по userNum/charId, свой павн
#include "World/AsdaCoord.h"                 // тайл → игровые координаты, UE → игра
#include "World/AsdaGroundTrace.h"           // высота земли для `Camp_start.jeb`
#include "AsdaEffectActor.h"                 // SpawnByJebName — эффект по позиции (`CEffectMgr_CreateEffect_F561F0`)
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Crc.h"
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`
#include "World/AsdaPlayerCharacter.h"      // ★ 30.10.2026: жест поражения 51 (`EnterCampPose`)
#include "Animation/SkeletalMeshActor.h"    // ★ 31.10.2026: флаг типа 6 в центре арены
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaDuel, Log, All);

// ★ [ПОРТ] ключи исправлений дефектов оригинала/сервера (отчёт §1.5).
static ASDA_DEV_CVAR(int32, CVarDuelDedupResult,
	TEXT("asda.Duel.DedupResult"), 1,
	TEXT("1 (по умолчанию) — повтор того же 4304 в течение 3 с отбрасывается: сервер шлёт итог проигравшему ДВАЖДЫ ")
	TEXT("(рассылка по сектору победителя + личная отправка, GameServer.c:78016-78017), и оригинал дважды выводит нотисы, ")
	TEXT("эффект и надпись LOSE. 0 — как в оригинале."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarDuelFixWarnSpam,
	TEXT("asda.Duel.FixWarnSpam"), 1,
	TEXT("1 (по умолчанию) — предупреждение «выходишь за границы поля боя» (108125) не чаще раза в 500 мс и для вызвавшего. ")
	TEXT("В оригинале ветка вызвавшего не обнуляет накопитель (`uiZoneProximityWarnTick`, ветка charObjA) — строка шла КАЖДЫЙ кадр. ")
	TEXT("0 — как в оригинале."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarDuelFixCode9,
	TEXT("asda.Duel.FixCode9"), 1,
	TEXT("1 (по умолчанию) — отказ сервера с кодом 9 («цель уже в ожидающем вызове», Recv_UserPvpReq) показывается как ")
	TEXT("loc 108104 «Игрок в бою.»; в оригинале кода 9 в `PvpDuel_OnNoticePacket` нет — вызов молча пропадал. 0 — как в оригинале."),
	ECVF_Default);

// ★ 31.10.2026 [ОРИГИНАЛ] флаги дуэли (типы 5/6 объектов-флагов эталона, разбор — у объявлений в .h). 0 — прежний порт (флагов нет).
static ASDA_DEV_CVAR(int32, CVarDuelFlags,
	TEXT("asda.Duel.Flags"), 1,
	TEXT("[ОРИГИНАЛ] Флаги дуэли: pvp_flag_land в центре арены (4305) и pvp_flag_back за спиной обоих участников (4305/4206) до итога 4304; "
	     "0 — без флагов (прежний порт)."),
	ECVF_Default);

namespace AsdaDuelFlag
{
	// Модели и клипы (Tools/duel_flag_export_models.py → duel_flag_import_models.py; таблица `unk_3519D3C` эталона).
	static const TCHAR* kBackMesh = TEXT("/Game/AsdaAssets/Models/Skeletal/DuelFlag/pvp_flag_back/SkeletalMeshes/pvp_flag_back");
	static const TCHAR* kBackAnim = TEXT("/Game/AsdaAssets/Models/Skeletal/DuelFlag/pvp_flag_back/SkeletalMeshes/pvp_flag_back_Anim");
	static const TCHAR* kLandMesh = TEXT("/Game/AsdaAssets/Models/Skeletal/DuelFlag/pvp_flag_land/SkeletalMeshes/pvp_flag_land");
	static const TCHAR* kLandAnim = TEXT("/Game/AsdaAssets/Models/Skeletal/DuelFlag/pvp_flag_land/SkeletalMeshes/pvp_flag_land_Anim");
	static const FName kBone(TEXT("2"));   // `List_FindIndexByName(модель персонажа, "2")` (CharAnim_UpdateFrame)
}

namespace AsdaDuel
{
	static const TCHAR* kWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/HUD/WBP_PVP_Gfont_Win.WBP_PVP_Gfont_Win_C");
	static constexpr int32 kZOrder = 905;              // над HUD и баннерами 18500/18550
	static constexpr float kPromptMs = 30000.f;        // `CProgressBar_SetRange(m_pTimerBar, 0, 30000.0f)` / 0x7530
	static constexpr float kTimer31Ms = 30000.f;       // `SetTimer(hWnd, 0x1F, 0x7530)` в `statusBarAddIcon(14)`
	static constexpr uint32 kCountdownEndMs = 11000u;  // 0x2AF8
	static constexpr float kSlotFadeMs = 3000.f;       // `objStartTimedAction(…, 255, 0, 3000.0)`
	static constexpr float kWarnDelayMs = 500.f;       // `warnDelayMs = 500`
	static constexpr int32 kNoticeType = 14;           // тип значка хотбара «вызов на дуэль» (иконка mes_dual_64)
	// Цвета эталона (ARGB → FColor(R,G,B,A)).
	static const FColor kPink(0xFE, 0xD9, 0xF1, 0xFF);     // −75279: 108101/108115/108116
	static const FColor kWin(0x44, 0x44, 0xFE, 0xFF);      // −12303106: 108112/108113
	static const FColor kLose(0xF9, 0x87, 0x96, 0xFF);     // −424042: 108122/108123
	static const FColor kWarnFar(0xF9, 0x3C, 0x3C, 0xFF);  // −443332: 108124
	static const FColor kWarnOut(0xF9, 0x00, 0x00, 0xFF);  // −458752: 108125
	// Вид выдачи нотиса.
	enum EKind : int32 { System = 0, Duel = 1, War = 2, MsgBox = 3 };

	// U3 (дедуп №2, В2-A, 11.10.2026): правило «показать без хит-теста» — общая `AsdaUi::SetShownNoHit` (вынос, поведение то же).
	static void SetShown(UWidget* W, bool bShown) { AsdaUi::SetShownNoHit(W, bShown); }
	static bool IsShown(const UWidget* W)
	{
		return W && W->GetVisibility() != ESlateVisibility::Collapsed && W->GetVisibility() != ESlateVisibility::Hidden;
	}
	// Имя char[20] из тела.
	static FString Name20(const TArray<uint8>& B, int32 At)
	{
		return (B.Num() >= At + 20) ? FAsdaLoginData::DecodeCp1251(B.GetData() + At, 20) : FString();
	}
}

void UAsdaDuelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaHotbarSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaPartySubsystem::StaticClass());
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		DuelPacketHandle = Net->OnDuelPacket.AddUObject(this, &UAsdaDuelSubsystem::OnDuelPacket);
		PvpModeHandle = Net->OnPvpMode.AddUObject(this, &UAsdaDuelSubsystem::OnPvpMode4206);   // ★ 31.10: флаг типа 5 по 4206
	}
	if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		NoticeClickHandle = Hb->OnNoticeIconClicked.AddUObject(this, &UAsdaDuelSubsystem::OnNoticeIconClicked);
	}
	if (UAsdaPartySubsystem* Party = GI ? GI->GetSubsystem<UAsdaPartySubsystem>() : nullptr)
	{
		JoinBoxHandle = Party->OnJoinBoxButton.AddUObject(this, &UAsdaDuelSubsystem::OnJoinBoxButton);
	}
}

void UAsdaDuelSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr) { Net->OnDuelPacket.Remove(DuelPacketHandle); Net->OnPvpMode.Remove(PvpModeHandle); }
	if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr) { Hb->OnNoticeIconClicked.Remove(NoticeClickHandle); }
	if (UAsdaPartySubsystem* Party = GI ? GI->GetSubsystem<UAsdaPartySubsystem>() : nullptr) { Party->OnJoinBoxButton.Remove(JoinBoxHandle); }
	if (Overlay) { Overlay->RemoveFromParent(); Overlay = nullptr; }
	Super::Deinitialize();
}

void UAsdaDuelSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	if (AsdaIsFullSessionReset(Reason)) { ResetAll(TEXT("полный сброс сессии")); return; }
	// Смена карты = ветка входа на карту эталона (game_main.c:3896-3903): `partyEffects_clearAll(g_pDuelMgr)`,
	//   `uiQuickSlot_HideAll`, флаг над собой, `[88]=0`, `[80]=−1`, `[92]=0`, `sub_922DFD(g_pHUD, 0)`.
	//   Вызов, пришедший мне, и отсчёт `[136]` эталон здесь НЕ трогает (персонаж тот же) — мы тоже.
	ResetArenas(TEXT("смена карты"));
}

// ════════════════════════════════ СЕТЬ ════════════════════════════════
void UAsdaDuelSubsystem::OnDuelPacket(int32 Opcode, const TArray<uint8>& Body)
{
	switch (Opcode)
	{
	case 4302: OnChallenge4302(Body); break;
	case 4303: OnNotice4303(Body); break;
	case 4304: OnResult4304(Body); break;
	case 4305: OnArena4305(Body); break;
	default: break;
	}
}

// 4302 → `Recv_PartyMember(g_pQuickSlotBar, 14, pkt)` (ui_core: case 14 = `duelCopyTargetInfoFromPkt` + `statusBarAddIcon`).
void UAsdaDuelSubsystem::OnChallenge4302(const TArray<uint8>& B)
{
	using namespace AsdaDuel;
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || B.Num() < 34) { return; }
	const int32 Callee = AsdaBytes::RdI16(&B[26]);
	// `duelCopyTargetInfoFromPkt@0xDE4A00`: `CharManager_FindByUserIdx(+26)` и `field_40` (это я) — иначе поля не трогаются.
	if (AsdaSelf::IsSelfUserIdx(Net, Callee))
	{
		Target.CharId  = AsdaBytes::RdI32(&B[2]);
		Target.UserIdx = AsdaBytes::RdI16(&B[0]);
		Target.Name    = Name20(B, 6);
		Target.PvpIdx  = AsdaBytes::RdI16(&B[32]);
	}
	UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] 4302: вызов от «%s» (userNum %d, charId %d), цель userNum %d%s, группа %d"),
		*Name20(B, 6), AsdaBytes::RdI16(&B[0]), AsdaBytes::RdI32(&B[2]), Callee,
		AsdaSelf::IsSelfUserIdx(Net, Callee) ? TEXT(" = я") : TEXT(" — НЕ я, поля вызова не тронуты"), AsdaBytes::RdI16(&B[32]));
	// `statusBarAddIcon(this, 14)`: значок ставится БЕЗ гейта «это я» (так в эталоне); флаг и WM_TIMER 31 — только если
	//   значку нашёлся свободный слот (они внутри цикла поиска слота).
	if (UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>())
	{
		// ★ Ревизия 2-В М-4: флаг и таймер — только если значку нашёлся СВОБОДНЫЙ слот (как в эталоне).
		if (Hb->AddNoticeIcon(kNoticeType))
		{
			bRequestActive = true;        // `byte_2C304C4 = 1`
			Timer31LeftMs = kTimer31Ms;   // `SetTimer(hWnd, 0x1F, 0x7530)` — повторный вызов перевзводит таймер
		}
	}
}

// 4303 → `PvpDuel_OnNoticePacket@0xDE5170` (combat_pvp.c:6372-6453).
void UAsdaDuelSubsystem::OnNotice4303(const TArray<uint8>& B)
{
	using namespace AsdaDuel;
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!Net || B.Num() < 33) { return; }
	const int32 Code = (int32)B[0];
	const FString Name = Name20(B, 7);
	UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] 4303: код %d, userNum %d, имя «%s», группа %d"), Code, AsdaBytes::RdI16(&B[1]), *Name, AsdaBytes::RdI16(&B[27]));
	switch (Code)
	{
	case 0:   // отказ/30 с без ответа: `sprintf("%s%s", имя, GetLocalizedString(108102))` → окно-сообщение
		Notice(108102, LocName(108102, Name), MsgBox, FColor::White, 10000.f);
		break;
	case 1:   // принято: [156] = группа, [136] = 0 (старт отсчёта), «имя»+108101 — пара журнал+221 → по решению владельца журнал
	{
		const int32 Uidx = AsdaBytes::RdI16(&B[1]);
		if (!WS || !WS->GetPlayerActorByUserNum(Uidx))
		{
			UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] 4303 код 1: персонажа %d нет в мире — эталон молчит"), Uidx);
			break;
		}
		DuelIndexByUser.Add(Uidx, AsdaBytes::RdI16(&B[27]));
		if (AsdaSelf::IsSelfUserIdx(Net, Uidx))
		{
			CountdownMs = 0u;   // `*(_DWORD *)&pTargetChar[1].buffFxSlots[136] = 0`
		}
		else
		{
			// Сервер кладёт в +1 ВСЕГДА userNum получателя (Recv_UserPvpRes, стр. 132269-132289) — чужой здесь не ожидается.
			UE_LOG(LogAsdaDuel, Warning, TEXT("[дуэль] 4303 код 1 про чужого %d: отсчёт идёт только у своего персонажа"), Uidx);
		}
		Notice(108101, LocName(108101, Name), System, kPink, 10000.f);
		break;
	}
	case 2: Notice(108103, AsdaNotice::LocRaw(GI, 108103), MsgBox, FColor::White, 10000.f); break;   // «Идет бой.»
	case 3: Notice(108104, AsdaNotice::LocRaw(GI, 108104), MsgBox, FColor::White, 10000.f); break;   // «Игрок в бою.»
	case 4: Notice(108105, AsdaNotice::LocRaw(GI, 108105), MsgBox, FColor::White, 10000.f); break;   // «Тебе больше нельзя сражаться.»
	case 5: Notice(108108, AsdaNotice::LocRaw(GI, 108108), MsgBox, FColor::White, 10000.f); break;   // «Сражаться не с кем.»
	case 6: Notice(108109, AsdaNotice::LocRaw(GI, 108109), MsgBox, FColor::White, 10000.f); break;   // «Ошибка данных боя.»
	case 7: Notice(108110, AsdaNotice::LocRaw(GI, 108110), MsgBox, FColor::White, 10000.f); break;   // «Сражения в городе запрещены.»
	case 8: Notice(108111, AsdaNotice::LocRaw(GI, 108111), MsgBox, FColor::White, 10000.f); break;   // «Ты принимаешь вызов.»
	case 9:
		// [ПОРТ] сервер шлёт 9 («цель уже в ожидающем вызове», Recv_UserPvpReq), а клиент его не знает (`default: return`).
		if (CVarDuelFixCode9.GetValueOnGameThread() != 0)
		{
			Notice(108104, AsdaNotice::LocRaw(GI, 108104), MsgBox, FColor::White, 10000.f);
		}
		else
		{
			UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] 4303 код 9: в оригинале без нотиса (asda.Duel.FixCode9 0)"));
		}
		break;
	default:
		break;   // эталон: `default: return;`
	}
}

// 4305 → `CDuelMgr_CreateDuelArena@0xDE61E0` (combat_pvp.c:6657-6737).
void UAsdaDuelSubsystem::OnArena4305(const TArray<uint8>& B)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (B.Num() < 14) { return; }
	FArena A;
	const int32 Pvp = AsdaBytes::RdI16(&B[0]);
	A.ObjId  = Pvp + 10000;                                                    // `*pArena = *a2 + 10000`
	A.Center = AsdaCoord::TileCenterGame(AsdaBytes::RdI16(&B[10]), AsdaBytes::RdI16(&B[12]));   // `CMap_GetTileAt(a2[5], a2[6])`
	A.CharA  = AsdaBytes::RdI32(&B[2]);                                        // `pArenaEntry[5] = *(a2 + 1)`
	A.CharB  = AsdaBytes::RdI32(&B[6]);                                        // `pArenaEntry[6] = *(a2 + 3)`
	// `clanFlagSpawnAt(1, x, y, z, id)` — флаг-объект типа 6 (`pvp_flag_land`) на тайле-центре арены, клип по кругу.
	//   ★ 31.10.2026 [ОРИГИНАЛ]: прежде [ОТЛОЖЕНО] («системы флагов-объектов в порту нет»).
	A.CenterFlag = SpawnCenterFlag(A.Center);
	AActor* Self = WS ? (IsValid(WS->LocalPlayerActor) ? WS->LocalPlayerActor.Get() : nullptr) : nullptr;
	for (const int32 C : { A.CharA, A.CharB })
	{
		if (C < 0) { continue; }
		AActor* P = WS ? WS->FindPlayerActorByCharId(C) : nullptr;
		if (!P) { continue; }
		InArenaChars.Add(C);   // `*(_DWORD *)&pAttacker[1].buffFxSlots[92] = 1`
		// `uiClanFlagGetOrCreate(g_pGameObjectMgr, −1, charId + 20000, 5)` — флаг `pvp_flag_back` за спиной участника.
		//   ★ 31.10.2026 [ОРИГИНАЛ]: прежде [ОТЛОЖЕНО].
		SetBackFlag(C, true, TEXT("4305"));
		if (P == Self && GI && GI->GetWorld())
		{
			// `CEffectMgr_CreateEffect_F561F0("Camp_start.jeb", 1, центр…)` — только своему участнику.
			const FVector2D Ue2 = AsdaCoord::GameToUe2D(A.Center);
			const float Z = AsdaGround::GroundZ(GI->GetWorld(), Ue2.X, Ue2.Y, Self->GetActorLocation().Z, Self);
			AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnByJebName(GI->GetWorld(), TEXT("Camp_start"), FVector(Ue2.X, Ue2.Y, Z), FRotator::ZeroRotator, nullptr);
			A.StartFx = Fx;
			UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] 4305: эффект Camp_start %s"),
				Fx ? TEXT("запущен") : TEXT("НЕ запущен — BP_Fx_Camp_start нет в ассетах (эффект не импортирован)"));
		}
	}
	Arenas.Add(A);
	UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] 4305: арена %d (группа %d), центр (%.0f, %.0f), участники charId %d / %d; арен %d"),
		A.ObjId, Pvp, A.Center.X, A.Center.Y, A.CharA, A.CharB, Arenas.Num());
}

// 4304 → `PvpDuel_ApplyMatchResult@0xDE5560` (combat_pvp.c:6456-6654).
void UAsdaDuelSubsystem::OnResult4304(const TArray<uint8>& B)
{
	using namespace AsdaDuel;
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!Net || B.Num() < 55) { return; }
	// [ПОРТ] сервер шлёт итог проигравшему ДВАЖДЫ (сектор победителя + лично): второй экземпляр отбрасываем.
	if (CVarDuelDedupResult.GetValueOnGameThread() != 0)
	{
		const uint32 Sig = FCrc::MemCrc32(B.GetData(), 55);
		const double Now = FPlatformTime::Seconds();
		ResultDedup.RemoveAll([Now](const TPair<uint32, double>& E) { return Now - E.Value > 3.0; });
		if (ResultDedup.ContainsByPredicate([Sig](const TPair<uint32, double>& E) { return E.Key == Sig; }))
		{
			++DedupDropped;
			UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] 4304: повтор того же итога отброшен (asda.Duel.DedupResult 1)"));
			return;
		}
		ResultDedup.Add(TPair<uint32, double>(Sig, Now));
	}
	const int32 Pvp = AsdaBytes::RdI16(&B[0]);
	if (Pvp > -1)
	{
		// `listEraseByObjId(pvp + 10000)` (флаг-объект — [ОТЛОЖЕНО]); запись арены: [5]/[6] = −1, эффект гасится, запись удаляется.
		const int32 Obj = Pvp + 10000;
		for (int32 I = Arenas.Num() - 1; I >= 0; --I)
		{
			if (Arenas[I].ObjId != Obj) { continue; }
			if (AActor* Fx = Arenas[I].StartFx.Get()) { Fx->Destroy(); }   // `CEffectMgr_StopByOwner(…, ArenaById[1])`
			if (AActor* Fl = Arenas[I].CenterFlag.Get()) { Fl->Destroy(); }   // ★ 31.10: `listEraseByObjId(pvp + 10000)` — флаг типа 6
			Arenas.RemoveAt(I);
			break;   // `duelMgrFindArenaById` возвращает первую найденную
		}
		// `listEraseByObjId(charId + 20000)` ×2 — флаги над участниками ([ОТЛОЖЕНО]). Ключ флага = charId + 20000:
		//   поле `CCharacter::userIdx` (+0x41C) эталона — МИСНЕЙМ, это charId (structs_ru.h:114), поэтому 4305 создаёт,
		//   4304 и вход на карту стирают флаг по одному и тому же ключу (ревизия 2-В С-1).
	}
	const int32 Type = (int32)B[14];
	if (Type > 4)
	{
		UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] 4304: тип итога %d — эталон ничего не делает"), Type);
		return;
	}
	const bool bSurrender = (Type == 4);
	const int32 WinU = AsdaBytes::RdI16(&B[2]);
	const int32 LoseU = AsdaBytes::RdI16(&B[8]);
	const FString WinName = Name20(B, 15);
	const FString LoseName = Name20(B, 35);
	UAsdaEffectsSubsystem* Fx = GI->GetSubsystem<UAsdaEffectsSubsystem>();
	UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] 4304: группа %d, тип %d (%s), победитель «%s» (%d), проигравший «%s» (%d)"),
		Pvp, Type, bSurrender ? TEXT("сдача") : TEXT("0 время/1 граница/2 смерть/3 выход"), *WinName, WinU, *LoseName, LoseU);
	// Модель `[92]` чужих участников эталон снимает пакетом 4206 подтип 0 (сервер шлёт его ДО 4304, GameServer.c:78007);
	//   у нас 4206 этого поля не ведёт — снимаем по 4304 (тот же итог).
	InArenaChars.Remove(AsdaBytes::RdI32(&B[4]));
	InArenaChars.Remove(AsdaBytes::RdI32(&B[10]));
	// ★ 31.10.2026 [ОРИГИНАЛ]: `listEraseByObjId(g_pGameObjectMgr, charId + 20000)` ×2 — флаги типа 5 обоих участников (при pvp > −1,
	//   combat_pvp.c:6503-6504; у сдачи/выхода та же ветка).
	if (Pvp > -1)
	{
		SetBackFlag(AsdaBytes::RdI32(&B[4]), false, TEXT("4304"));
		SetBackFlag(AsdaBytes::RdI32(&B[10]), false, TEXT("4304"));
	}

	// ── победитель ──
	if (WS && WS->GetPlayerActorByUserNum(WinU))
	{
		DuelIndexByUser.Remove(WinU);                                  // `[156] = −1`
		if (AsdaSelf::IsSelfUserIdx(Net, WinU)) { CountdownMs = 0xFFFFFFFFu; }   // `[136] = −1`
		const AActor* E = Fx ? Fx->SpawnTitleFx(WinU, TEXT("pvp_win")) : nullptr;   // привязка 6, как у эффекта звания
		if (!E) { UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] эффект pvp_win не запущен — BP_Fx_pvp_win нет в ассетах")); }
		if (AsdaSelf::IsSelfUserIdx(Net, WinU))
		{
			ClearSelfPvpFields(TEXT("победа"));
			const FString T = AsdaUi::FormatPrintfArgs(AsdaNotice::LocRaw(GI, bSurrender ? 108113 : 108112), { WinName, LoseName });
			ShowSlot(1);   // VICTORY
			Notice(bSurrender ? 108113 : 108112, T, Duel, kWin, 10000.f);
		}
	}
	// ── проигравший ──
	if (WS && WS->GetPlayerActorByUserNum(LoseU))
	{
		DuelIndexByUser.Remove(LoseU);
		if (AsdaSelf::IsSelfUserIdx(Net, LoseU)) { CountdownMs = 0xFFFFFFFFu; }
		// `CharFx_AutoSabzil_State(loser, 51)` — жест поражения (трек 26, `ch4_{m|f}_00_loser_00_0.jbb`, один раз → 0).
		//   ★ 30.10.2026 [ОРИГИНАЛ]: клип импортирован (anim_99, Tools/campwar_export_clips.py); прежде жест не проигрывался.
		if (AAsdaPlayerCharacter* LoseCh = Cast<AAsdaPlayerCharacter>(WS->GetPlayerActorByUserNum(LoseU)))
		{
			LoseCh->EnterCampPose(51, TEXT("4304 проигравший дуэли (PvpDuel_ApplyMatchResult)"));
		}
		const AActor* E = Fx ? Fx->SpawnTitleFx(LoseU, TEXT("pvp_lose")) : nullptr;
		if (!E) { UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] эффект pvp_lose не запущен — BP_Fx_pvp_lose нет в ассетах")); }
		if (AsdaSelf::IsSelfUserIdx(Net, LoseU))
		{
			ClearSelfPvpFields(TEXT("поражение"));
			const FString T = AsdaUi::FormatPrintfArgs(AsdaNotice::LocRaw(GI, bSurrender ? 108123 : 108122), { LoseName, WinName });
			ShowSlot(2);   // LOSE
			Notice(bSurrender ? 108123 : 108122, T, Duel, kLose, 10000.f);
		}
	}
	// ── зритель: ни победитель, ни проигравший не я (`Target_GetSelectedUserIdx(свой)` = свой userNum) ──
	if (!AsdaSelf::IsSelfUserIdx(Net, WinU) && !AsdaSelf::IsSelfUserIdx(Net, LoseU))
	{
		const FString T = AsdaUi::FormatPrintfArgs(AsdaNotice::LocRaw(GI, bSurrender ? 108113 : 108112), { WinName, LoseName });
		Notice(bSurrender ? 108113 : 108112, T, Duel, kWin, 10000.f);
	}
}

// ════════════════════════════════ ЗНАЧОК 14 И ОКНО 2245 ════════════════════════════════
void UAsdaDuelSubsystem::OnNoticeIconClicked(int32 Type)
{
	if (Type != AsdaDuel::kNoticeType) { return; }
	PanelShow();   // `uiNoticeButtonDispatch` case 14 → `duelUiPanelShow(g_pDuelMgr)`
}

// `duelUiPanelShow@0xDE4190` = `duelUiUpdateLabelText` + `m_nTimerAccum = 0` + показ окна.
void UAsdaDuelSubsystem::PanelShow()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaPartySubsystem* Party = GI ? GI->GetSubsystem<UAsdaPartySubsystem>() : nullptr;
	if (!Party) { return; }
	const FString Text = LocName(108100, Target.Name);   // `sprintf("%s%s", m_szTargetName, GetLocalizedString(108100))`
	TimerAccumMs = 0.f;
	bPromptShown = Party->ShowJoinBoxText(Text);
	UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] окно 2245: «%s» (вызов %s)"), *Text, Target.CharId >= 0 ? TEXT("есть") : TEXT("пуст"));
}

// `duelUiPanelReset@0xDE41E0`.
void UAsdaDuelSubsystem::PanelReset(const TCHAR* Why)
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaPartySubsystem* Party = GI ? GI->GetSubsystem<UAsdaPartySubsystem>() : nullptr)
	{
		if (bPromptShown || Party->IsJoinBoxOpen()) { Party->HideJoinBoxShared(); }   // `CControl_SetVisible(m_pDuelWnd, 0)`
	}
	bPromptShown = false;
	Target = FTarget();      // −1 / −1 / −1 / имя пусто
	TimerAccumMs = 0.f;
	bRequestActive = false;
	UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] вызов сброшен: %s"), Why);
}

// `CParty_OnControl` 2248/2249: `if (m_nTargetSessionId >= 0) { duelSendReqPkt4303(1|0); duelUiPanelReset(); }`.
void UAsdaDuelSubsystem::OnJoinBoxButton(bool bOk)
{
	if (Target.CharId < 0) { return; }
	SendAnswer(bOk ? 1 : 0, bOk ? TEXT("кнопка 2248") : TEXT("кнопка 2249"));
	PanelReset(bOk ? TEXT("ответ «принять»") : TEXT("ответ «отказ»"));
}

void UAsdaDuelSubsystem::SendAnswer(uint8 Answer, const TCHAR* Why)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bSent = Net && Net->SendDuelAnswer4303(Answer, Target.UserIdx, Target.CharId, Target.PvpIdx);
	UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] 4303 → ответ %u вызвавшему userNum %d charId %d, группа %d (%s): %s"),
		(uint32)Answer, Target.UserIdx, Target.CharId, Target.PvpIdx, Why, bSent ? TEXT("отправлен") : TEXT("НЕ отправлен"));
}

// ════════════════════════════════ ОКНО 1200 ════════════════════════════════
bool UAsdaDuelSubsystem::EnsureOverlay()
{
	using namespace AsdaDuel;
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	// ★ Ревизия 2-В К-1: смена карты в порту — `OpenLevel` (новый UWorld), движок снимает виджеты прежнего мира
	//   (`UGameViewportSubsystem::HandleRemoveWorld`). Окно, которого нет на экране или которое принадлежит
	//   прежнему миру, создаётся заново (тот же приём, что у баннера уровня и окон отряда).
	if (Overlay && Overlay->IsInViewport() && Overlay->GetWorld() == W) { return true; }
	if (Overlay)
	{
		UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] окно 1200 осталось от прежнего мира (на экране %d) — создаю заново"), Overlay->IsInViewport() ? 1 : 0);
		Overlay->RemoveFromParent();
		Overlay = nullptr;
		Icons.Reset(); TensDigits.Reset(); OnesDigits.Reset(); TensBase = nullptr; OnesBase = nullptr;
	}
	if (!W) { return false; }
	UClass* Cls = LoadClass<UUserWidget>(nullptr, kWbp);
	if (!Cls) { UE_LOG(LogAsdaDuel, Warning, TEXT("[дуэль] окно 1200 не загрузилось: %s"), kWbp); return false; }
	Overlay = CreateWidget<UUserWidget>(W, Cls);
	if (!Overlay) { return false; }
	Overlay->AddToViewport(kZOrder);
	// ★ 21.10.2026 (Alt+Z): прячется вместе с интерфейсом — у оригинала это окно JUI блока бита 128 (render_core.c:1482).
	UAsdaUiVisibilitySubsystem::TrackFor(this, Overlay);
	SetShown(Overlay, false);   // корень 1200 в `.JUI` объявлен `Visible="0"`
	// Узлы — имена дерева WBP (дамп `_wave2C_predump_tree.txt`): картинки «<JUI-имя>_<индекс>_0», группы «…_grp».
	Icons.SetNum(3);
	Icons[0] = AsdaUi::FindNodeExact(Overlay, TEXT("PVP_Gfont_1_1_0"));   // 1201 START
	Icons[1] = AsdaUi::FindNodeExact(Overlay, TEXT("PVP_Gfont_2_2_0"));   // 1202 VICTORY
	Icons[2] = AsdaUi::FindNodeExact(Overlay, TEXT("PVP_Gfont_3_3_0"));   // 1203 LOSE
	TensBase = AsdaUi::FindNodeExact(Overlay, TEXT("PVP_Gfont_Count_grp"));    // 1210
	OnesBase = AsdaUi::FindNodeExact(Overlay, TEXT("PVP_Gfont_Count2_grp"));   // 1221
	TensDigits.SetNum(10); OnesDigits.SetNum(10);
	int32 Found = 0;
	for (int32 D = 0; D < 10; ++D)
	{
		TensDigits[D] = AsdaUi::FindNodeExact(Overlay, *FString::Printf(TEXT("Gfont_Big_Count_%d_%d_0"), D, 5 + D));    // 1211+D
		OnesDigits[D] = AsdaUi::FindNodeExact(Overlay, *FString::Printf(TEXT("Gfont_Big_Count_%d_%d_0"), D, 16 + D));   // 1222+D
		Found += (TensDigits[D] ? 1 : 0) + (OnesDigits[D] ? 1 : 0);
	}
	UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] окно 1200 поднято: надписи %d/%d/%d, группы %s/%s, цифр %d/20"),
		Icons[0] ? 1 : 0, Icons[1] ? 1 : 0, Icons[2] ? 1 : 0, TensBase ? TEXT("есть") : TEXT("НЕТ"),
		OnesBase ? TEXT("есть") : TEXT("НЕТ"), Found);
	return true;
}

// `uiHideControlGroup@0xDE3E50`: база, обе подложки счёта (элементы [3]/[4] массива эталона) и все 20 цифр.
void UAsdaDuelSubsystem::HideControlGroup()
{
	using namespace AsdaDuel;
	if (!EnsureOverlay()) { return; }
	SetShown(Overlay, false);
	SetShown(TensBase, false); SetShown(OnesBase, false);
	for (int32 D = 0; D < 10; ++D) { SetShown(TensDigits[D], false); SetShown(OnesDigits[D], false); }
}

// `uiQuickSlot_HideAll@0xDE4000`.
void UAsdaDuelSubsystem::HideAll()
{
	using namespace AsdaDuel;
	HideControlGroup();
	for (UWidget* I : Icons) { SetShown(I, false); }
	SlotFadeElapsedMs = -1.f;
}

// `uiQuickSlot_ShowTwoDigitNumber@0xDE3F00`.
void UAsdaDuelSubsystem::ShowTwoDigitNumber(int32 N)
{
	using namespace AsdaDuel;
	HideControlGroup();
	if ((uint32)N >= 100u) { return; }   // −1 = просто скрыть
	if (!Overlay || TensDigits.Num() < 10 || OnesDigits.Num() < 10) { return; }   // окна нет (нет мира)
	SetShown(Overlay, true);
	SetShown(OnesBase, true);            // `m_pCount1Base` = 1221
	if (N / 10 > 0)
	{
		SetShown(TensBase, true);        // `m_pCount2Base` = 1210
		SetShown(TensDigits[N / 10], true);
	}
	SetShown(OnesDigits[N % 10], true);
	LastCountdown = N;
}

// `uiQuickSlot_ShowSlot@0xDE4070`: надпись 0 START / 1 VICTORY / 2 LOSE и угасание 255→0 за 3000 мс.
void UAsdaDuelSubsystem::ShowSlot(int32 Slot)
{
	using namespace AsdaDuel;
	HideAll();
	if ((uint32)Slot > 2u || !Icons.IsValidIndex(Slot)) { return; }
	SetShown(Overlay, true);
	SetShown(Icons[Slot], true);
	// [ПОРТ] гасим ТОЛЬКО надпись: база 1200 — `NoImage_icon` (невидима), а в UMG прозрачность корня наследуется детьми
	//   (в JUI цвет у каждого контрола свой) — угасание корня погасило бы и цифры следующего отсчёта.
	if (Icons[Slot]) { Icons[Slot]->SetRenderOpacity(1.f); }
	SlotFadeElapsedMs = 0.f;
	++SlotShowSeq;
	LastSlot = Slot;
	UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] окно 1200: надпись %s"), Slot == 0 ? TEXT("START") : Slot == 1 ? TEXT("VICTORY") : TEXT("LOSE"));
}

// ════════════════════════════════ ВРЕМЯ ════════════════════════════════
void UAsdaDuelSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.Duel");   // R4, доля a: область замера (AsdaPerf.h; в Shipping пусто)
	TickFlags();   // ★ 31.10.2026: видимость флагов дуэли (тип 5) — каждый кадр, как `UpdateList_TypedSub`
	if (bManualClock) { return; }
	Advance(DeltaTime);
}

void UAsdaDuelSubsystem::Advance(float Seconds)
{
	using namespace AsdaDuel;
	if (Seconds <= 0.f) { return; }
	const float Ms = Seconds * 1000.f;
	UGameInstance* GI = GetGameInstance();
	// `duelUiTickTimer@0xDE4270` (зовётся из `uiZoneProximityWarnTick` каждый кадр).
	if (bRequestActive)
	{
		TimerAccumMs += Ms;
		UAsdaPartySubsystem* Party = GI ? GI->GetSubsystem<UAsdaPartySubsystem>() : nullptr;
		if (Party && Party->IsJoinBoxOpen())   // `CControl_IsVisible(m_pDuelWnd)`
		{
			if (TimerAccumMs <= kPromptMs)
			{
				if (bPromptShown) { Party->SetJoinBoxTimerFracShared(TimerAccumMs / kPromptMs); }
			}
			else
			{
				SendAnswer(0, TEXT("30 с без ответа"));
				PanelReset(TEXT("30 с без ответа"));
			}
		}
	}
	// WM_TIMER 31: `duelUiPanelReset` + `buffSlotCompact(g_pQuickSlotBar, 14)` (без ответа серверу).
	//   Порядок с порогом 30 с окна (выше) в одном кадре в эталоне не определён (сообщение окна против кадра);
	//   у нас сперва порог окна (ответ 0), затем таймер — ревизия 2-В М-5, не дефект.
	if (Timer31LeftMs >= 0.f)
	{
		Timer31LeftMs -= Ms;
		if (Timer31LeftMs <= 0.f)
		{
			Timer31LeftMs = -1.f;
			PanelReset(TEXT("WM_TIMER 31 (30 с от значка)"));
			if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr) { Hb->RemoveNoticeIcon(kNoticeType); }
		}
	}
	const int32 SeqBefore = SlotShowSeq;   // надпись, показанная в ЭТОМ шаге, начинает гаснуть со следующего (`objStartTimedAction` от «сейчас»)
	TickProximity(Ms);   // render_core.c:3443 — до отсчёта
	TickCountdown(Ms);
	// Угасание надписи окна 1200.
	if (SlotFadeElapsedMs >= 0.f && SlotShowSeq == SeqBefore)
	{
		SlotFadeElapsedMs += Ms;
		UWidget* I = Icons.IsValidIndex(LastSlot) ? Icons[LastSlot].Get() : nullptr;
		const float A = FMath::Clamp(1.f - SlotFadeElapsedMs / kSlotFadeMs, 0.f, 1.f);
		if (I) { I->SetRenderOpacity(A); }
		if (SlotFadeElapsedMs >= kSlotFadeMs)
		{
			SlotFadeElapsedMs = -1.f;
			// Эталон оставляет контролы видимыми с альфой 0; у нас окно сворачиваем — на экране то же самое.
			HideAll();
		}
	}
}

// render_core.c:3444-3475 — отсчёт на своём персонаже.
void UAsdaDuelSubsystem::TickCountdown(float Ms)
{
	using namespace AsdaDuel;
	if (CountdownMs >= kCountdownEndMs) { return; }
	UGameInstance* GI = GetGameInstance();
	CountdownTickAccum += Ms;
	CountdownMs = (uint32)((double)CountdownMs + Ms);   // `self[52259] = (unsigned __int64)(field + deltaTime)`
	const int32 Sec = (int32)(((int64)kCountdownEndMs - (int64)CountdownMs) / 1000);
	if (Sec > 0)
	{
		if (CountdownTickAccum > 1000.f)
		{
			ShowTwoDigitNumber(Sec);
			const FString T = AsdaUi::FormatPrintfArgs(AsdaNotice::LocRaw(GI, 108116), { FString::FromInt(Sec) });
			Notice(108116, T, System, kPink, 10000.f);
			CountdownTickAccum = 0.f;
		}
	}
	else
	{
		CountdownMs = 0xFFFFFFFFu;   // `self[52259] = −1`
		Notice(108115, AsdaNotice::LocRaw(GI, 108115), System, kPink, 10000.f);
		ShowTwoDigitNumber(-1);
		ShowSlot(0);   // START
	}
}

// `uiZoneProximityWarnTick@0xDE3710` — только у своего участника.
void UAsdaDuelSubsystem::TickProximity(float Ms)
{
	using namespace AsdaDuel;
	if (Arenas.Num() == 0) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	AActor* Self = WS ? (IsValid(WS->LocalPlayerActor) ? WS->LocalPlayerActor.Get() : nullptr) : nullptr;
	const bool bFixSpam = CVarDuelFixWarnSpam.GetValueOnGameThread() != 0;
	for (int32 I = 0; I < Arenas.Num(); )
	{
		const FArena& A = Arenas[I];
		AActor* PA = (WS && A.CharA >= 0) ? WS->FindPlayerActorByCharId(A.CharA) : nullptr;
		AActor* PB = (WS && A.CharB >= 0) ? WS->FindPlayerActorByCharId(A.CharB) : nullptr;
		auto Check = [&](AActor* P, bool bIsA)
		{
			if (!P || P != Self) { return; }   // `charObj && charObj->effectSlots[112].field_40`
			const FVector2D My = AsdaCoord::UeToGame2D(P->GetActorLocation());
			const float D2 = (float)FVector2D::DistSquared(My, A.Center);   // `dist2DSquared` по X и Z
			if (A.OuterR * A.OuterR >= D2)
			{
				if (A.InnerR * A.InnerR >= D2) { WarnAccumMs = 0.f; return; }
				WarnAccumMs += Ms;
				if (WarnAccumMs > kWarnDelayMs)
				{
					Notice(108124, AsdaNotice::LocRaw(GI, 108124), War, kWarnFar, 2000.f);   // пара журнал+221, 2000 мс
					WarnAccumMs = 0.f;
				}
			}
			else
			{
				WarnAccumMs += Ms;
				if (WarnAccumMs > kWarnDelayMs)
				{
					Notice(108125, AsdaNotice::LocRaw(GI, 108125), War, kWarnOut, 2000.f);
					// ⚠ эталон в ветке вызвавшего (charObjA) накопитель НЕ обнуляет — нотис каждый кадр. [ПОРТ] ключ.
					if (!bIsA || bFixSpam) { WarnAccumMs = 0.f; }
				}
			}
		};
		Check(PA, true);
		Check(PB, false);
		if (!PA && !PB)
		{
			// Обоих участников нет в мире — запись удаляется (`sub_936A0B` + `sub_936326`).
			if (AActor* Fx = Arenas[I].StartFx.Get()) { Fx->Destroy(); }
			UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] арена %d: участников нет в мире — запись удалена"), Arenas[I].ObjId);
			Arenas.RemoveAt(I);
			continue;
		}
		++I;
	}
}

// ════════════════════════════════ ПРОЧЕЕ ════════════════════════════════
void UAsdaDuelSubsystem::Notice(int32 LocId, const FString& Text, int32 Kind, const FColor& Color, float TtlMs)
{
	using namespace AsdaDuel;
	UGameInstance* GI = GetGameInstance();
	NoticeTrace.Add(LocId);
	LastTextByLoc.Add(LocId, Text);
	switch (Kind)
	{
	case System: AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, Text, Color, TtlMs); break;   // журнал + буфер A
	case Duel:   AsdaNotice::ChatColored(GI, EAsdaMsgCategory::Duel, Text, Color, TtlMs); break;     // журнал + буфер B
	case War:    AsdaNotice::ChatColored(GI, EAsdaMsgCategory::War, Text, Color, TtlMs); break;      // NOTICE_WIN 228
	case MsgBox: AsdaNotice::MsgBox(GI, EAsdaMsgCategory::Duel, Text); break;                         // ONEBUT_MSGBOX 1500
	default: break;
	}
	UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] нотис %d (%s): «%s»"), LocId,
		Kind == System ? TEXT("журнал+A") : Kind == Duel ? TEXT("журнал+B") : Kind == War ? TEXT("NOTICE_WIN") : TEXT("окно"), *Text);
}

// `sprintf("%s%s", имя, GetLocalizedString(id))`. [ПОРТ] в нашей таблице эти строки несут «%s'» (у оригинала имя
//   приклеивалось к началу) — подставляем имя в «%s», иначе на экран вышел бы буквальный «%s».
FString UAsdaDuelSubsystem::LocName(int32 LocId, const FString& Name) const
{
	const FString L = AsdaNotice::LocRaw(GetGameInstance(), LocId);
	if (L.Contains(TEXT("%s"))) { return AsdaUi::FormatPrintfArgs(L, { Name }); }
	return Name + L;
}

// Свой участник по 4304: `[160] = 0; 108117; [88] = 0; [80] = −1; [92] = 0; sub_922DFD(g_pHUD, 0)`.
void UAsdaDuelSubsystem::ClearSelfPvpFields(const TCHAR* Why)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	Notice(108117, AsdaNotice::LocRaw(GI, 108117), AsdaDuel::System, FColor::White, 10000.f);   // «Режим боя отключен.» −1, 10 с
	if (Net) { InArenaChars.Remove(Net->SelectedCharId); }
	if (UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr) { Mini->SetPvpModeIcon(false); }
	// `[160] = 0` (флаг PvP для площадных баффов) у нас ведёт `UAsdaSkillUseSubsystem` по 4206 подтип 0, который сервер
	//   шлёт участнику ДО 4304 (PvpResult, GameServer.c:78007) — второй писатель здесь не нужен.
	UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] свои поля режима боя сброшены (%s)"), Why);
}

// `partyEffects_clearAll(g_pDuelMgr)` + `uiQuickSlot_HideAll` + свои `[88]/[80]/[92]` + `sub_922DFD(g_pHUD, 0)`.
void UAsdaDuelSubsystem::ResetArenas(const TCHAR* Why)
{
	UGameInstance* GI = GetGameInstance();
	for (FArena& A : Arenas)
	{
		if (AActor* Fx = A.StartFx.Get()) { Fx->Destroy(); }
		if (AActor* Fl = A.CenterFlag.Get()) { Fl->Destroy(); }   // ★ 31.10: вход на карту — `gameEntityMapClearDestroyAll(g_pGameObjectMgr)`
	}
	{
		TArray<int32> Keys; BackFlags.GetKeys(Keys);
		for (const int32 K : Keys) { SetBackFlag(K, false, Why); }
		SelfPvp92 = 0;
	}
	const int32 N = Arenas.Num();
	Arenas.Reset();
	WarnAccumMs = 0.f;
	if (Overlay) { HideAll(); }
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr) { InArenaChars.Remove(Net->SelectedCharId); }
	if (UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr) { Mini->SetPvpModeIcon(false); }
	UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] арены сняты (%d), окно 1200 скрыто: %s"), N, Why);
}

// ════════════════════════════════ ★ 31.10.2026 ФЛАГИ ДУЭЛИ ════════════════════════════════
// Флаг типа 6 (`clanFlagSpawnAt` → `uiClanFlagGetOrCreate(…, pvp + 10000, 6)`, `posSetWithChildOffset(x, y, z)` тайла-центра,
//   `[142] = 0` — клип 0 сразу; тик `partAnimAdvanceBlend`: клип 0 по кругу, объект на месте).
AActor* UAsdaDuelSubsystem::SpawnCenterFlag(const FVector2D& CenterGame)
{
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	if (!W || CVarDuelFlags.GetValueOnGameThread() == 0) { return nullptr; }
	USkeletalMesh* M = LoadObject<USkeletalMesh>(nullptr, AsdaDuelFlag::kLandMesh);
	UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, AsdaDuelFlag::kLandAnim);
	if (!M)
	{
		UE_LOG(LogAsdaDuel, Warning, TEXT("[дуэль] флаг центра арены: модели pvp_flag_land нет (%s)"), AsdaDuelFlag::kLandMesh);
		return nullptr;
	}
	const FVector2D Ue2 = AsdaCoord::GameToUe2D(CenterGame);
	const UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	AActor* Self = (WS && IsValid(WS->LocalPlayerActor)) ? WS->LocalPlayerActor.Get() : nullptr;
	const float Z = AsdaGround::GroundZ(W, Ue2.X, Ue2.Y, Self ? Self->GetActorLocation().Z : 0.f, Self);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASkeletalMeshActor* A = W->SpawnActor<ASkeletalMeshActor>(FVector(Ue2.X, Ue2.Y, Z), FRotator::ZeroRotator, P);
	if (!A) { return nullptr; }
	USkeletalMeshComponent* C = A->GetSkeletalMeshComponent();
	C->SetSkeletalMesh(M);
	// Скелеталки glTF порта — в игровых единицах ×100 и с зеркалом Y: тот же масштаб, что у корня модели персонажа/питомца/лута
	//   (`AAsdaPlayerCharacter` MeshRoot (0.01, −0.01, 0.01)). Без него флаг выходил в 100 раз больше (габарит ~10 000).
	C->SetRelativeScale3D(FVector(0.01f, -0.01f, 0.01f));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	if (Clip) { C->PlayAnimation(Clip, /*bLooping*/ true); }
	UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] флаг центра арены pvp_flag_land поставлен (%.0f, %.0f, %.0f), клип %s"), Ue2.X, Ue2.Y, Z, Clip ? TEXT("по кругу") : TEXT("НЕТ"));
	return A;
}

// Флаг типа 5: `uiClanFlagGetOrCreate(…, charId + 20000, 5)` / `listEraseByObjId(…, charId + 20000)`. Крепление — кость "2" модели персонажа,
//   поворот `D3DXMatrixRotationYawPitchRoll(π, 0, π/2)` — тот же, что у флага клана типа 1 (`AAsdaPlayerCharacter::ClanFlagAttachRotation`,
//   пересчёт осей D3D → UE). Клип 0 (`Battle_Flag_00`) по кругу (по концу клипа — снова 0, `CharAnim_UpdateFrame`).
void UAsdaDuelSubsystem::SetBackFlag(int32 CharId, bool bOn, const TCHAR* Why)
{
	if (CharId < 0) { return; }
	if (!bOn || CVarDuelFlags.GetValueOnGameThread() == 0)
	{
		if (USkeletalMeshComponent* Old = BackFlags.FindRef(CharId).Get()) { Old->DestroyComponent(); }
		if (BackFlags.Remove(CharId) > 0) { UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] флаг за спиной charId %d снят (%s)"), CharId, Why); }
		return;
	}
	if (BackFlags.FindRef(CharId).IsValid()) { return; }   // `uiClanFlagGetOrCreate`: тот же тип — существующий объект
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	AAsdaPlayerCharacter* Ch = WS ? Cast<AAsdaPlayerCharacter>(WS->FindPlayerActorByCharId(CharId)) : nullptr;
	USkeletalMeshComponent* Body = Ch ? Ch->BaseSkel.Get() : nullptr;
	if (!Body) { return; }   // эталон: персонажа нет — объект живёт, но не рисуется (LABEL_27); у порта — не создаём
	if (Body->GetBoneIndex(AsdaDuelFlag::kBone) == INDEX_NONE)
	{
		UE_LOG(LogAsdaDuel, Warning, TEXT("[дуэль] у модели charId %d нет кости '2' — флаг за спиной не крепится"), CharId);
		return;
	}
	USkeletalMesh* M = LoadObject<USkeletalMesh>(nullptr, AsdaDuelFlag::kBackMesh);
	UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, AsdaDuelFlag::kBackAnim);
	if (!M)
	{
		UE_LOG(LogAsdaDuel, Warning, TEXT("[дуэль] флаг за спиной: модели pvp_flag_back нет (%s)"), AsdaDuelFlag::kBackMesh);
		return;
	}
	USkeletalMeshComponent* F = NewObject<USkeletalMeshComponent>(Ch, *FString::Printf(TEXT("DuelBackFlag_%d"), CharId));
	F->SetupAttachment(Body, AsdaDuelFlag::kBone);
	F->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	F->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	F->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	F->SetCastShadow(true);
	F->RegisterComponent();
	F->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, AsdaDuelFlag::kBone);
	F->SetRelativeLocation(FVector::ZeroVector);
	F->SetRelativeRotation(Ch->ClanFlagAttachRotation);
	F->SetRelativeScale3D(FVector::OneVector);
	F->SetSkeletalMesh(M);
	if (Clip) { F->PlayAnimation(Clip, /*bLooping*/ true); }
	BackFlags.Add(CharId, F);
	UE_LOG(LogAsdaDuel, Log, TEXT("[дуэль] флаг за спиной charId %d (кость '2', клип %s) — %s"), CharId, Clip ? TEXT("по кругу") : TEXT("НЕТ"), Why);
}

// Видимость флагов типа 5 (`CharAnim_UpdateFrame`): персонаж найден и виден, `[92] == 1` (в арене) — иначе объект не рисуется.
//   Свой `[92]` пишет и 4206 команда 1 (`SelfPvp92`). Актор скрыт (GM-скрытие, смена вида) — флаг скрыт вместе с ним (вложение).
void UAsdaDuelSubsystem::TickFlags()
{
	if (BackFlags.Num() == 0) { return; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	for (auto It = BackFlags.CreateIterator(); It; ++It)
	{
		USkeletalMeshComponent* F = It.Value().Get();
		if (!F) { It.RemoveCurrent(); continue; }   // персонаж ушёл из обзора — компонент умер с ним
		const bool bSelf = Net && Net->SelectedCharId == It.Key();
		const bool bShow = InArenaChars.Contains(It.Key()) || (bSelf && SelfPvp92 > 0);
		if (F->IsVisible() != bShow) { F->SetVisibility(bShow); }
	}
}

// 4206 команда 1 (`netTradeRequestHandler@0xF4A190`, network.c:44099-44150): [7] == 1 → флаг типа 5 (`charId + 20000`); [7] == 0 →
//   стереть (при `[80] ≥ 0`). Своему ещё и `[92] = [7]`.
void UAsdaDuelSubsystem::OnPvpMode4206(int32 UserIdx, bool bOn, int32 SubType)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	AActor* A = WS ? WS->GetPlayerActorByUserNum(UserIdx) : nullptr;
	const bool bSelf = Net && AsdaSelf::IsSelfUserIdx(Net, UserIdx);
	if (bSelf) { SelfPvp92 = SubType; }
	if (!A) { return; }
	// charId актора: свой — выбранный; чужой — среди участников арен (`[92] == 1`; без него флаг чужого и так не рисуется).
	int32 CharId = bSelf ? Net->SelectedCharId : -1;
	if (CharId < 0 && WS) { for (const int32 C : InArenaChars) { if (WS->FindPlayerActorByCharId(C) == A) { CharId = C; break; } } }
	if (SubType == 1)
	{
		if (CharId >= 0) { SetBackFlag(CharId, true, TEXT("4206 [7] = 1")); }
	}
	else if (SubType == 0)
	{
		// стереть флаг этого персонажа (ключ — его актор: charId чужого вне арены порт не знает)
		TArray<int32> Kill;
		for (const TPair<int32, TWeakObjectPtr<USkeletalMeshComponent>>& Kv : BackFlags) { if (Kv.Value.IsValid() && Kv.Value->GetOwner() == A) { Kill.Add(Kv.Key); } }
		for (const int32 K : Kill) { SetBackFlag(K, false, TEXT("4206 [7] = 0")); }
	}
	(void)bOn;
}

AActor* UAsdaDuelSubsystem::DebugCenterFlag(int32 Pvp) const
{
	for (const FArena& A : Arenas) { if (A.ObjId == Pvp + 10000) { return A.CenterFlag.Get(); } }
	return nullptr;
}

USkeletalMeshComponent* UAsdaDuelSubsystem::DebugBackFlag(int32 CharId) const
{
	return BackFlags.FindRef(CharId).Get();
}

void UAsdaDuelSubsystem::ResetAll(const TCHAR* Why)
{
	UGameInstance* GI = GetGameInstance();
	if (bRequestActive || bPromptShown || Target.CharId >= 0) { PanelReset(Why); }
	Timer31LeftMs = -1.f;
	if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr) { Hb->RemoveNoticeIcon(AsdaDuel::kNoticeType); }
	CountdownMs = 0xFFFFFFFFu;
	CountdownTickAccum = 0.f;
	ResetArenas(Why);
	InArenaChars.Reset();
	DuelIndexByUser.Reset();
	LastCountdown = -1;
	LastSlot = -1;
	ResultDedup.Reset();
}

// ── отладочные чтения окна 1200 ──
bool UAsdaDuelSubsystem::DebugIconShown(int32 Slot) const
{
	return DebugOverlayShown() && Icons.IsValidIndex(Slot) && AsdaDuel::IsShown(Icons[Slot]);
}
float UAsdaDuelSubsystem::DebugIconOpacity(int32 Slot) const
{
	return (Icons.IsValidIndex(Slot) && Icons[Slot]) ? Icons[Slot]->GetRenderOpacity() : 0.f;
}
int32 UAsdaDuelSubsystem::DebugShownTens() const
{
	if (!DebugOverlayShown() || !AsdaDuel::IsShown(TensBase)) { return -1; }
	for (int32 D = 0; D < TensDigits.Num(); ++D) { if (AsdaDuel::IsShown(TensDigits[D])) { return D; } }
	return -1;
}
int32 UAsdaDuelSubsystem::DebugShownOnes() const
{
	if (!DebugOverlayShown() || !AsdaDuel::IsShown(OnesBase)) { return -1; }
	for (int32 D = 0; D < OnesDigits.Num(); ++D) { if (AsdaDuel::IsShown(OnesDigits[D])) { return D; } }
	return -1;
}
bool UAsdaDuelSubsystem::DebugOverlayShown() const
{
	return Overlay && AsdaDuel::IsShown(Overlay);
}
