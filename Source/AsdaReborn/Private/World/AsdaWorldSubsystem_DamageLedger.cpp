// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_DamageLedger.cpp — ★ R3-c 12.11.2026 (разрез AsdaWorldSubsystem_Damage.cpp, дословный перенос): журнал ударов: сверка HP, удержание, отложенная смерть, окно своего удара.
//   Общее для частей — в AsdaWorldSubsystem_Damage_Private.h; шапка и реверс — в AsdaWorldSubsystem_Damage.cpp.
#include "World/AsdaWorldSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "HAL/IConsoleManager.h"        // ключ `asda.Move.RefusePlank`
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер
#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"   // ★ «это я?» — единая точка своего номера (E2, 14.09.2026)
#include "World/AsdaPlayerCharacter.h"
// ★★★ ЕДИНЫЙ маршрутизатор сообщений (категория → окно по таблице)   ★ R3: комментарий снятого включения "UI/AsdaMessageSubsystem.h" (файл им не пользовался) — сохранён
#include "Framework/Application/SlateApplication.h"
#include "Npc/AsdaMobActor.h"
#include "World/AsdaPlayerController.h"      // снятие таргета при смерти цели-моба (эталон SetActionState(10))
#include "World/AsdaProjectileActor.h"       // ★ снаряды дальнобойного оружия (projectile_re.md)
#include "Resources/AsdaGameUserSettings.h"  // ★ 17.10.2026 (Ф5): режим показа цифр урона `DamageTextMode`
#include "World/AsdaFxLightSettings.h"         // ★ волна 3 п.16 (15.09.2026): AsdaWeaponGlow — имя свечения заточки (одна таблица баз)
// тип оружия атакующего (импакт battk01..04)   ★ R3: комментарий снятого включения "Items/AsdaItemTableSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaSkillDatabase.h"            // ★ Аудит №3: поля записи навыка — только отсюда
#include "UI/AsdaEffectsSubsystem.h"         // ★ состояния мобов: 4017 бафф-стеки и бафф отражения
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "TimerManager.h"
#include "World/AsdaNameplateComponent.h"   // над-головные плашки (порт JUI-окон 1:1)
#include "World/AsdaEntityText.h"           // ★ волна 3 п.14: имена/модели/звук-базы мобов и NPC — одна точка
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "Misc/ConfigCacheIni.h"             // GConfig — опции показа ангела (FxConfig)
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
// UAsdaRichText::ApplyFormatted (единая точка текста)   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя (ASDA_CTRACE) — полный цикл действий клиента в бою
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)
#include "AsdaWorldSubsystem_Damage_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaWorldSubsystem_Damage*.cpp

static ASDA_DEV_CVAR(int32, CVarAsdaLedgerRaiseBareOnly,
	TEXT("asda.Combat.LedgerRaiseBareOnly"), 1,
	TEXT("1 (по умолчанию) = пока у моба есть непоказанные удары ленты, снимок 4015/5257/4102 полосу не поднимает (эталон: 4015 ")
	TEXT("kind 1/2 пишет только серверное HP), реген 4013/4019/4017 поднимает голым значением; 0 = прежний подъём до «снимок + ")
	TEXT("непоказанный урон» (4015 со старым HP поднимал полосу выше сервера)."),
	ECVF_Default);

// ═══ ★★★ ЛЕНТА ПОПАДАНИЙ МОБА — ЕДИНОЕ УДЕРЖАНИЕ HP И СМЕРТИ ДЛЯ ЛЮБОГО АТАКУЮЩЕГО (17.10.2026, синхронизация боя, Ф1) ═══
//   [ПОРТ] Проект `Reports/combat_sync_2026-10-17/03_DESIGN.md` §3 (ред. 2, ревизия `04_REVIEW.md` п.4–6, 10, 11, 14).
//   ПРЕДЫСТОРИЯ. Правка 17.10 (`Reports/combat_hitframe_2026-10-17/REPORT.md`) завела окно «наш замах в полёте»: пока НАШ
//   замах по цели не долетел, серверное HP (4015/4017/4013/4019) и серверная смерть (4015 HP=0, 4017 flags=0, 4013/4019 HP=0)
//   придерживаются до попадания — сервер обнуляет HP сразу, а смерть отпускает по вшитым 1000/2000 мс (`CMonster::OnDamaged
//   @0x4423C0`, `CMonster::ProcessState@0x448350`), и выстрел после полного натяжения приходился позже. Окно держало ОДИН rid
//   и ТОЛЬКО свои замахи; у наблюдателя (сосед бьёт моба) HP и труп ложились по серверу — до выстрела соседа (Р1/Р3 разбора
//   `01_client_roles.md`), а при общей цели последний удар на каждом экране свой (ревизия п.4).
//   ЛЕНТА. На каждого моба — записи ожидающих ВИДИМЫХ попаданий любого атакующего {атакующий, вид, момент на ЭТОМ экране,
//   смертельная}. Правила (без ветвления «я атакующий» — момент попадания только из общих данных: оружие → клип → кадры
//   `combo_time` → полёт и момент прихода пакета):
//   1) взвод: свой замах — при отправке 4026/4028 (`ArmOurImpactWindowFromSend`, исход неизвестен) и по ответу 4029/4027 (момент
//      и урон известны); чужой — по приходу его 4029/4027; навык — при парковке попаданий 5257/4102 до порога фазы
//      (`ArmSkillImpact`). Смертельная — ответ с HP ≤ 0 (4029 @4 ≤ 0, 5257 +11 ≤ 0, 4102 «убит»).
//   2) смерть — на попадании СМЕРТЕЛЬНОЙ записи; исход неизвестен (заявка без ответа, ответ без видимого урона «−2») — ждём это
//      попадание; известны только не смертельные — до ближайшего попадания, но не дольше 0.5 с после серверной смерти; записей
//      нет (слив накопителя, DoT, атакующий не виден) — сразу (`LedgerDeathDueS`).
//   3) полоса: на попадании — цифра и вычет у ЭТОГО атакующего (`NoteImpactShown`); на каждом серверном снимке — сверка
//      «только вниз»: полоса не выше «снимок + ещё не показанный известный урон записей» (`LedgerReconcileHp`); подъём — только
//      прошедший фильтр прогноза (`IsServerHpSnapshotStale`, прогноз не короче удержания); реген/респаун снимает смерть.
//   4) добыча (5005) и свой опыт (4038) по мобу, чья смерть/попадание ещё ждёт ленту, — выпускаются вместе со смертью.
//   5) потолки: взвод записи ≤ kImpactWindowMaxS (4.0 с), удержание ≤ kImpactHoldMaxS (4.5 с) абсолютно; прерывание (отказ, смена
//      цели, смерть атакующего, навык, уход из видимости) — запись сокращается до назначенного ответом попадания или снимается.
//   [ОРИГИНАЛ] эталон этой гонки не лечит (`Recv_CombatHit@0xF310E0` пишет HP @+25 безусловно, `entityListUpdateFade@0xF78660`
//   валит цель с HP ≤ 0, `Recv_CombatHitAux@0xF33B60` flags = 0 — `SetActionState(10)`). Исправление дефекта оригинала (закон
//   fix-original-defects, решение владельца 24–25.08 и 17.10 «HP и смерть — на кадре удара», запрос 17.10 «у всех одинаково»).
//   Неустранимо: сдвиг на пинг между экранами (общих часов нет), вход посреди пачки, «моб виден — атакующий нет» (4029 нет —
//   записей нет — HP и смерть сразу, как оригинал).
//   Ключи отката: `asda.Combat.HitFrameAllChannels 0` — лента выключена целиком (окно как до 17.10); `asda.Combat.ObserverSync 0` —
//   лента только для своих замахов (наблюдатель по-старому).
static constexpr double kDeathNoRecordGraceS = 0.5;   // ★ Ф1: смерть без смертельной записи ждёт ближайшее попадание не дольше (с)

double UAsdaWorldSubsystem::LedgerDeathDueS(const FMobImpactLedger& L, double Now) const
{
	constexpr double kInf = 1.0e30;   // «нет такой записи»
	double Lethal = kInf, Unknown = kInf, Nearest = kInf;   // ближайшие: смертельная, с неизвестным исходом, любая
	for (const FAsdaImpactEntry& E : L.Pending)
	{
		if (E.bLethal) { Lethal = FMath::Min(Lethal, E.AtS); }
		else if (E.bOutcomeUnknown) { Unknown = FMath::Min(Unknown, E.AtS); }
		Nearest = FMath::Min(Nearest, E.AtS);
	}
	double Due = Now;   // записей нет — смерть сразу
	if (Lethal < kInf) { Due = Lethal; }                                                        // смерть — на смертельном попадании
	else if (Unknown < kInf) { Due = Unknown; }                                                 // исход ещё не известен — ждём попадание
	else if (Nearest < kInf) { Due = FMath::Min(Nearest, L.DeathArrivedS + kDeathNoRecordGraceS); }   // смерть без записи — допуск
	if (L.HoldSinceS > 0.0) { Due = FMath::Min(Due, L.HoldSinceS + kImpactHoldMaxS + L.HoldExtraS); }        // абсолютный потолок удержания
	return Due;
}

void UAsdaWorldSubsystem::LedgerReconcileHp(int32 Rid, const TCHAR* Why)
{
	FMobImpactLedger* L = ImpactLedgers.Find(Rid);
	AAsdaMobActor* M = FindNetMobByRid(Rid);
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!L || !M || !W || M->IsDead()) { return; }
	const double Now = W->GetTimeSeconds();
	if (L->ServerHp >= 0 && !L->bDeathPending)
	{
		bool bUnknown = false;   // есть запись с неизвестным уроном — она может покрыть любую разницу
		int64 Cover = 0;         // известный урон записей, ещё не показанный попаданием
		for (const FAsdaImpactEntry& E : L->Pending) { bUnknown |= E.bOutcomeUnknown; Cover += E.PendingDamage; }
		const bool bFinal = L->Pending.Num() == 0;   // записей больше нет — снимок применяется целиком
		if (!bUnknown)
		{
			const int64 Floor = (int64)L->ServerHp + Cover;   // ниже этого полоса сейчас быть не может
			if (Floor < (int64)M->CurHp)
			{
				const int32 Was = M->CurHp;   // для журнала
				if (bFinal)
				{
					UE_LOG(LogAsdaWorld, Log, TEXT("[бой] rid=%d: придержанное HP применено %d → %d (%s; лента пуста)"), Rid, Was, L->ServerHp, Why ? Why : TEXT("?"));
					ASDA_CTRACE_MOB(Rid, EAsdaCT::HpPlate, TEXT("лента %s: ПРИДЕРЖАННОЕ HP ПРИМЕНЕНО %d → %d (%s; записей больше нет — снимок целиком)"),
						*AsdaCtWorld::Mob(Rid, M), Was, L->ServerHp, Why ? Why : TEXT("?"));
					ApplyMobHpUnfiltered(M, L->ServerHp, L->bServerHpNumber);
				}
				else
				{
					UE_LOG(LogAsdaWorld, Log, TEXT("[лента] rid=%d: сверка «только вниз» %d → %lld (снимок %d + не показано %lld; %s)"), Rid, Was, Floor,
						L->ServerHp, Cover, Why ? Why : TEXT("?"));
					ASDA_CTRACE_MOB(Rid, EAsdaCT::HpPlate, TEXT("лента %s: сверка «только вниз» %d → %lld (снимок %d + ещё не показано %lld; %s)"),
						*AsdaCtWorld::Mob(Rid, M), Was, Floor, L->ServerHp, Cover, Why ? Why : TEXT("?"));
					ApplyMobHpUnfiltered(M, (int32)Floor, /*bAllowNumber=*/false);
				}
			}
			else
			{
				ASDA_CTRACE_V(EAsdaCT::HpPlate, TEXT("лента %s: сверка — полоса %d не выше «снимок %d + не показано %lld», без изменений (%s)"),
					*AsdaCtWorld::Mob(Rid, M), M->CurHp, L->ServerHp, Cover, Why ? Why : TEXT("?"));
			}
		}
		else
		{
			ASDA_CTRACE_V(EAsdaCT::HpPlate, TEXT("лента %s: сверка со снимком %d отложена — у записи исход неизвестен (полоса %d; %s)"),
				*AsdaCtWorld::Mob(Rid, M), L->ServerHp, M->CurHp, Why ? Why : TEXT("?"));
		}
		L = ImpactLedgers.Find(Rid);   // показ цифры мог тронуть карту лент — указатель заново
		if (!L) { return; }
		if (bFinal) { L->ServerHp = -1; }   // снимок исчерпан: следующий бой не должен опираться на старое значение
	}
	LedgerUpdateHold(Rid);
	if (FMobImpactLedger* L2 = ImpactLedgers.Find(Rid)) { L2->ShownHp = M->CurHp; }
}

// ★★★ 17.10.2026 (Ф2, ревизия Ф1 п.1 — КРИТИЧНО): «держим» ≠ «полоса выше снимка».
//   Было: удержание (и его абсолютный потолок 4.5 с) стартовало, как только полоса выше голого серверного снимка. Но полоса выше
//   снимка, пока удары ответа ещё не показаны, — НОРМА: снимок 4029 @4 уже включает урон, чьи цифры встанут на кадрах удара.
//   Отсчёт потолка поэтому стартовал на каждом 4029 и сбрасывался лишь при догоне; запись сливается по «атакующий + вид» и
//   продлевается каждым 4028 — при непрерывной стрельбе/общей цели потолок срабатывал ~раз в 4.5 с и (вместе с `FlushLedger`)
//   вычитал урон дважды. Теперь держим, только если есть что-то НЕОБЪЯСНИМОЕ ожидающими записями: полоса выше «снимок + ещё не
//   показанный известный урон записей», отложенная смерть или удержанный показ (добыча/опыт). Пересчёт — на каждом снимке
//   (`LedgerReconcileHp`) и на каждом показанном попадании (`NoteImpactShown`).
void UAsdaWorldSubsystem::LedgerUpdateHold(int32 Rid)
{
	FMobImpactLedger* L = ImpactLedgers.Find(Rid);
	const AAsdaMobActor* M = FindNetMobByRid(Rid);
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!L || !M || !W) { return; }
	int64 Cover = 0;   // известный урон записей, ещё не показанный попаданием
	for (const FAsdaImpactEntry& E : L->Pending) { Cover += E.PendingDamage; }
	const bool bHpHeld = L->ServerHp >= 0 && (int64)M->CurHp > (int64)L->ServerHp + Cover;   // полоса выше объяснимого записями
	const bool bHeld = L->bDeathPending || bHpHeld || L->HeldLoot.Num() > 0;                    // что-то держим
	if (bHeld && L->HoldSinceS <= 0.0) { L->HoldSinceS = W->GetTimeSeconds(); }
	else if (!bHeld) { L->HoldSinceS = 0.0; }
}

bool UAsdaWorldSubsystem::LedgerHoldServerHp(AAsdaMobActor* M, int32 Hp, const TCHAR* Chan, bool bStaleChecked, bool bAllowNumber, bool bRegenChan)
{
	if (!M || CVarAsdaHitFrameAllChannels.GetValueOnGameThread() == 0) { return false; }
	const int32 Rid = M->NetRenderId;   // моб снимка
	FMobImpactLedger* L = ImpactLedgers.Find(Rid);
	if (!L || L->Pending.Num() == 0) { return false; }   // записей нет — применять сразу, как раньше
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return false; }
	const double Now = W->GetTimeSeconds();   // мир-время приёма пакета
	if (L->HoldSinceS > 0.0 && Now - L->HoldSinceS > kImpactHoldMaxS + L->HoldExtraS)
	{
		ASDA_CTRACE_MOB(Rid, EAsdaCT::HpPlate, TEXT("лента %s: HP=%d из %s — удержание дольше потолка %.1f с, придержанное применяется сразу"),
			*AsdaCtWorld::Mob(Rid, M), Hp, Chan ? Chan : TEXT("?"), kImpactHoldMaxS);
		FlushLedger(Rid, TEXT("потолок удержания"));
		return false;
	}
	// ★ Ревизия правки 17.10, п.8: фильтр устаревшего снимка — ОДИН раз, на приёме (он пишет `LastServerHpSnapshot`).
	if (!bStaleChecked && IsServerHpSnapshotStale(Rid, Hp))
	{
		const FAsdaMobHpForecast* Fc = MobHpForecast.Find(Rid);   // прогноз, по которому снимок отброшен
		UE_LOG(LogAsdaWorld, Log,
			TEXT("[net] моб rid=%d '%s': снимок HP=%d из %s ОТБРОШЕН — сервер сам прогнозировал HP=%d ")
			TEXT("(накопитель урона NpcServer ещё не слит; откат asda.Skill.LegacyDeath 1)"),
			Rid, *M->MobName, Hp, Chan ? Chan : TEXT("?"), Fc ? Fc->Hp : -1);
		ASDA_CTRACE_MOB(Rid, EAsdaCT::HpPlate, TEXT("%s: снимок HP=%d из %s ОТБРОШЕН как устаревший (прогноз сервера %d), полоса остаётся %d/%d"),
			*AsdaCtWorld::Mob(Rid, M), Hp, Chan ? Chan : TEXT("?"), Fc ? Fc->Hp : -1, M->CurHp, M->MaxHp);
		return true;
	}
	if (L->bDeathPending)
	{
		// Серверная смерть уже отложена: живое HP другого канала её не отменяет (отменяют только реген/респаун).
		UE_LOG(LogAsdaWorld, Verbose, TEXT("[лента] rid=%d: HP=%d из %s при отложенной смерти — без изменений"), Rid, Hp, Chan ? Chan : TEXT("?"));
		ASDA_CTRACE_MOB(Rid, EAsdaCT::HpPlate, TEXT("%s: HP=%d из %s при ОТЛОЖЕННОЙ смерти — без изменений (полоса %d)"), *AsdaCtWorld::Mob(Rid, M), Hp,
			Chan ? Chan : TEXT("?"), M->CurHp);
		return true;
	}
	// ★ Ф2 (ревизия Ф1 п.2): «подъём» сравнивается с «снимок + ещё не показанный известный урон записей», а не с голым снимком:
	//   цифры ожидающих попаданий вычтут свой урон сами — подъём до голого снимка вычел бы его второй раз (полоса ниже сервера,
	//   потом рывок вверх).
	int64 RaiseCover = 0;   // известный урон записей, ещё не показанный попаданием
	for (const FAsdaImpactEntry& E : L->Pending) { RaiseCover += E.PendingDamage; }
	// ★★★ 18.10.2026 (ключ `asda.Combat.LedgerRaiseBareOnly`, разбор у объявления): «снимок + непоказанное» верно только для снимка
	//   ПОСЛЕ этих ударов. 4015 несёт живое поле HP ДО них (без вычета накопителя) — подъём давал 35 + 19 = 54 при серверных ≤ 35
	//   (живой журнал 18.10). [ОРИГИНАЛ] 4015 kind 1/2 пишет только `ent[284]`, полосу не трогает (network.c:37551); показанное HP
	//   поднимают только спаун/реген. Поэтому при записях ленты: 4015, 5257/4102 — подъёма нет (только запомнить снимок, сверка
	//   «только вниз» ниже); 4013/4019/4017 — подъём голым значением (без покрытия) и только если оно выше полосы.
	const bool bBareOnly = CVarAsdaLedgerRaiseBareOnly.GetValueOnGameThread() != 0;   // новое правило подъёма
	if (bBareOnly) { RaiseCover = 0; }
	const int64 RaiseTo = FMath::Min<int64>((int64)Hp + RaiseCover, (int64)INT32_MAX);   // полоса после подъёма
	if (bBareOnly && !bRegenChan && RaiseTo > (int64)M->CurHp)
	{
		ASDA_CTRACE_MOB(Rid, EAsdaCT::HpPlate, TEXT("лента %s: HP=%d из %s выше полосы %d — полосу НЕ поднимает (не канал регена; у моба %d запис. с непоказанными ударами; эталон: 4015 пишет только серверное HP)"),
			*AsdaCtWorld::Mob(Rid, M), Hp, Chan ? Chan : TEXT("?"), M->CurHp, L->Pending.Num());
	}
	else if (RaiseTo > (int64)M->CurHp)
	{
		// Подъём, прошедший фильтр прогноза (прогноз держится не меньше удержания), — законный реген: сразу, без цифры.
		UE_LOG(LogAsdaWorld, Log, TEXT("[лента] rid=%d '%s': подъём HP %d → %lld из %s (снимок %d + не показано %lld; прошёл фильтр прогноза)"),
			Rid, *M->MobName, M->CurHp, RaiseTo, Chan ? Chan : TEXT("?"), Hp, RaiseCover);
		ASDA_CTRACE_MOB(Rid, EAsdaCT::HpPlate, TEXT("лента %s: ПОДЪЁМ HP полосы %d → %lld из %s (снимок %d + не показано %lld; законный реген, прошёл фильтр прогноза)"),
			*AsdaCtWorld::Mob(Rid, M), M->CurHp, RaiseTo, Chan ? Chan : TEXT("?"), Hp, RaiseCover);
		L->ServerHp = Hp; L->bServerHpNumber = false;
		ApplyMobHpUnfiltered(M, (int32)RaiseTo, /*bAllowNumber=*/false);
		if (FMobImpactLedger* L2 = ImpactLedgers.Find(Rid)) { L2->ShownHp = M->CurHp; }
		LedgerUpdateHold(Rid);
		return true;
	}
	L->ServerHp = Hp;
	L->bServerHpNumber = bAllowNumber;
	UE_LOG(LogAsdaWorld, Log, TEXT("[бой] rid=%d '%s': HP=%d из %s — лента попаданий (%d запис.; откат asda.Combat.HitFrameAllChannels 0)"),
		Rid, *M->MobName, Hp, Chan ? Chan : TEXT("?"), L->Pending.Num());
#if ASDA_COMBAT_TRACE
	{
		double CtNext = 1.0e30;   // ближайшее попадание ленты (мир-время) — когда придержанное доедет до полосы
		for (const FAsdaImpactEntry& E : L->Pending) { CtNext = FMath::Min(CtNext, E.AtS); }
		if (Hp != M->CurHp)
		{
			ASDA_CTRACE_MOB(Rid, EAsdaCT::HpPlate, TEXT("%s: HP=%d из %s ПРИДЕРЖАНО лентой (%d запис., ближайшее попадание через %.2f с), полоса пока %d/%d"),
				*AsdaCtWorld::Mob(Rid, M), Hp, Chan ? Chan : TEXT("?"), L->Pending.Num(), FMath::Max(0.0, CtNext - Now), M->CurHp, M->MaxHp);
		}
	}
#endif
	LedgerReconcileHp(Rid, Chan);
	RearmLedgerTimer(Rid);
	return true;
}

bool UAsdaWorldSubsystem::LedgerDeferDeath(AAsdaMobActor* M, const TCHAR* Chan)
{
	if (!M || M->IsDead() || CVarAsdaHitFrameAllChannels.GetValueOnGameThread() == 0
	    || CVarAsdaDeathOnHitFrame.GetValueOnGameThread() == 0) { return false; }
	const int32 Rid = M->NetRenderId;   // моб смерти
	FMobImpactLedger* L = ImpactLedgers.Find(Rid);
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!L || L->Pending.Num() == 0)
	{
		ASDA_CTRACE_MOB(Rid, EAsdaCT::Death, TEXT("лента %s: смерть из %s НЕ откладывается — ожидающих попаданий нет, смерть сразу%s"),
			*AsdaCtWorld::Mob(Rid, M), Chan ? Chan : TEXT("?"), (L && L->HeldLoot.Num() > 0) ? TEXT(" (удержанные добыча/опыт выйдут следующим тиком)") : TEXT(""));
		// Записей нет — смерть сразу (вызывающий). ★ Ф2 (ревизия Ф1 п.5): удержанный показ (добыча/опыт ждали смерть при пустой
		//   ленте) выходит вместе с ней — следующим тиком, когда вызывающий уже положил труп.
		if (L && W && L->HeldLoot.Num() > 0)
		{
			W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Rid]()
			{
				ReleaseLedgerHeld(Rid, TEXT("смерть при пустой ленте"));
				if (FMobImpactLedger* L2 = ImpactLedgers.Find(Rid)) { L2->HoldSinceS = 0.0; if (L2->Pending.Num() == 0) { L2->HoldExtraS = 0.0; } }   // ★ 10.11: + надбавка записи
			}));
		}
		return false;
	}
	if (!W) { return false; }
	const double Now = W->GetTimeSeconds();
	if (L->HoldSinceS > 0.0 && Now - L->HoldSinceS > kImpactHoldMaxS + L->HoldExtraS)
	{
		ASDA_CTRACE_MOB(Rid, EAsdaCT::Death, TEXT("лента %s: смерть из %s — удержание дольше потолка %.1f с, придержанное применяется, смерть сразу"),
			*AsdaCtWorld::Mob(Rid, M), Chan ? Chan : TEXT("?"), kImpactHoldMaxS);
		FlushLedger(Rid, TEXT("потолок удержания"));
		return false;
	}
	const bool bFirst = !L->bDeathPending;   // первый канал смерти
	if (bFirst) { L->DeathArrivedS = Now; L->DeathChan = Chan ? Chan : TEXT("?"); }
	L->bDeathPending = true;
	if (L->HoldSinceS <= 0.0) { L->HoldSinceS = Now; }
	const double Due = LedgerDeathDueS(*L, Now);   // когда смерть должна лечь на этом экране
	if (Due <= Now + 0.001)
	{
		// Смертельной записи нет и ближайшее попадание дальше допуска — смерть сразу (вызывающий); удержанное — вместе с ней.
		UE_LOG(LogAsdaWorld, Log, TEXT("[лента] rid=%d '%s': смерть из %s без смертельной записи — сразу (ближайшее попадание дальше %.1f с)"),
			Rid, *M->MobName, Chan ? Chan : TEXT("?"), kDeathNoRecordGraceS);
		ASDA_CTRACE_MOB(Rid, EAsdaCT::Death, TEXT("лента %s: смерть из %s без смертельной записи — СРАЗУ (ближайшее попадание дальше допуска %.1f с; %d запис. сняты)"),
			*AsdaCtWorld::Mob(Rid, M), Chan ? Chan : TEXT("?"), kDeathNoRecordGraceS, L->Pending.Num());
		L->Pending.Reset(); L->bDeathPending = false; L->DeathChan.Reset(); L->HoldSinceS = 0.0; L->ServerHp = -1; L->HoldExtraS = 0.0;
		W->GetTimerManager().ClearTimer(L->Timer);
		W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Rid]() { ReleaseLedgerHeld(Rid, TEXT("смерть без записи")); }));
		return false;
	}
	L->ServerHp = 0; L->bServerHpNumber = false;
	// Метка «обречён» (бит 0x20 эталона) — только боевая логика: новых замахов по цели не начинаем; визуал гаснет на попадании.
	M->bServerDead = true;
	UE_LOG(LogAsdaWorld, Log, TEXT("[net] смерть моба rid=%d '%s' из %s ОТЛОЖЕНА лентой попаданий до +%.2f с (%d запис.; откат asda.Combat.HitFrameAllChannels 0)"),
		Rid, *M->MobName, Chan ? Chan : TEXT("?"), Due - Now, L->Pending.Num());
	ASDA_CTRACE_MOB(Rid, EAsdaCT::Death, TEXT("лента %s: серверная смерть из %s ОТЛОЖЕНА до попадания +%.2f с (%d запис.%s); метка «обречён», HP ленты 0, полоса пока %d/%d"),
		*AsdaCtWorld::Mob(Rid, M), Chan ? Chan : TEXT("?"), Due - Now, L->Pending.Num(), bFirst ? TEXT("") : TEXT(", повторный канал"), M->CurHp, M->MaxHp);
	RearmLedgerTimer(Rid);
	return true;
}

bool UAsdaWorldSubsystem::CancelDeferredDeathOnRevive(AAsdaMobActor* M, int32 Hp, const TCHAR* Chan, bool bSureRevive)
{
	if (!M || Hp <= 0 || M->IsDead()) { return false; }
	const int32 Rid = M->NetRenderId;   // моб регена
	FMobImpactLedger* L = ImpactLedgers.Find(Rid);
	// ★ Ревизия 18.10: реген/респаун (4017 flags&4 или отложенная серверная смерть) — смертельный удар прошлой жизни моба не убьёт.
	if (bSureRevive || (L && L->bDeathPending)) { ForgetLethalMark(Rid); }
	// ★ Ф2 (ревизия Ф1 п.7): реген/возрождение — серверная смерть прошлой жизни недействительна: признак «смертельная» с записей
	//   снимается (иначе её попадание положило бы возрождённого моба), заявки без ответа снимаются (их исход относился к прошлой
	//   жизни); записи с назначенным ответом доживают — их цифры уже стоят в таймерах. Реген наверняка — 4017 flags&4
	//   (`bSureRevive`) или отложенная смерть; повторная 4013 живого моба (AoI) записи боя не трогает.
	if (L && L->Pending.Num() > 0 && (bSureRevive || L->bDeathPending))
	{
		ASDA_CTRACE_MOB(Rid, EAsdaCT::HitFrame, TEXT("лента %s: %s HP=%d — реген: признак «смертельная» снят с %d запис., заявки без ответа сняты"),
			*AsdaCtWorld::Mob(Rid, M), Chan ? Chan : TEXT("?"), Hp, L->Pending.Num());
		UWorld* Wr = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		const double NowR = Wr ? Wr->GetTimeSeconds() : 0.0;   // мир-время регена
		for (int32 i = L->Pending.Num() - 1; i >= 0; --i)
		{
			FAsdaImpactEntry& E = L->Pending[i];
			E.bLethal = false;
			if (E.CommittedAtS <= NowR + 0.001) { L->Pending.RemoveAt(i); }
		}
	}
	if (!L || !L->bDeathPending)
	{
		if (L) { LedgerUpdateHold(Rid); RearmLedgerTimer(Rid); }
		return false;
	}
	// Реген/респаун по смыслу пакета (эталон: 4013 `Recv_MobSpawn@0xF305E0` — общий хвост пишет HP и зовёт
	//   `CCharacter_SetActionState`, труп оживает; 4017 flags&4 `Recv_CombatHitAux@0xF33B60` — HP @92 + SetActionState(1)).
	//   Серверная смерть, отложенная лентой, уже отменена сервером — на попадании `NetDeath` убил бы возрождённого моба.
	L->bDeathPending = false; L->DeathChan.Reset();
	L->ServerHp = Hp; L->bServerHpNumber = false; L->HoldSinceS = 0.0;
	if (L->Pending.Num() == 0) { L->HoldExtraS = 0.0; }   // ★ 10.11.2026 (ревизия 5): надбавка отложенной записи — вместе с удержанием
	M->bServerDead = false;                     // метка «обречён» снимается (эталон: новое состояние 1)
	MobHpForecast.Remove(Rid);                  // прогноз HP прошлой жизни недействителен (как в ветке flags&4)
	ASDA_CTRACE_MOB(Rid, EAsdaCT::Death, TEXT("лента %s: %s HP=%d — реген/респаун, отложенная смерть ОТМЕНЕНА, метка «обречён» снята; HP полосы %d → %d / %d"),
		*AsdaCtWorld::Mob(Rid, M), Chan ? Chan : TEXT("?"), Hp, M->CurHp, Hp, ((AAsdaMobActor::IsMaxHpFixed() && M->MobTypeMaxHp > 0) ? M->MaxHp : FMath::Max(M->MaxHp, Hp)));
	M->CurHp = Hp;
	M->ApplyPacketHpToMax(Hp, Chan, /*bSpawnChan=*/true);   // ★ 18.10.2026: реген/респаун — канал спауна (ключ asda.Combat.MobMaxHpFixed)
	UpdateMobHpBar(M);
	UE_LOG(LogAsdaWorld, Log, TEXT("[net] rid=%d '%s': %s HP=%d — реген/респаун, отложенная смерть ОТМЕНЕНА (лента попаданий)"),
		Rid, *M->MobName, Chan ? Chan : TEXT("?"), Hp);
	ReleaseLedgerHeld(Rid, TEXT("реген — смерть отменена"));
	RearmLedgerTimer(Rid);
	return true;
}

void UAsdaWorldSubsystem::RearmLedgerTimer(int32 Rid)
{
	FMobImpactLedger* L = ImpactLedgers.Find(Rid);
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!L || !W) { return; }
	const double Now = W->GetTimeSeconds();
	constexpr double kInf = 1.0e30;   // «события нет»
	double Next = kInf;               // ближайшее событие ленты
	for (const FAsdaImpactEntry& E : L->Pending) { Next = FMath::Min(Next, LedgerEntryCloseS(E)); }   // ★ ревизия Ф2 (ВАЖНО 4)
	if (L->bDeathPending) { Next = FMath::Min(Next, LedgerDeathDueS(*L, Now)); }
	if (L->HoldSinceS > 0.0 && (L->Pending.Num() > 0 || L->bDeathPending || L->HeldLoot.Num() > 0)) { Next = FMath::Min(Next, L->HoldSinceS + kImpactHoldMaxS + L->HoldExtraS); }
	if (Next >= kInf) { W->GetTimerManager().ClearTimer(L->Timer); return; }
	W->GetTimerManager().SetTimer(L->Timer, FTimerDelegate::CreateUObject(this, &UAsdaWorldSubsystem::OnLedgerTimer, Rid),
		FMath::Max(0.001f, (float)(Next - Now)), false);
}

void UAsdaWorldSubsystem::OnLedgerTimer(int32 Rid)
{
	ProcessLedger(Rid, TEXT("попадание"));
}

void UAsdaWorldSubsystem::ProcessLedger(int32 Rid, const TCHAR* Why)
{
	FMobImpactLedger* L = ImpactLedgers.Find(Rid);
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!L || !W) { return; }
	const double Now = W->GetTimeSeconds();
	AAsdaMobActor* M = FindNetMobByRid(Rid);
	if (!M)
	{
		ASDA_CTRACE_MOB(Rid, EAsdaCT::Note, TEXT("лента rid=%d: моба больше нет (фейд/AoI) — лента снята (%s)"), Rid, Why ? Why : TEXT("?"));
		DropLedger(Rid);
		return;
	}   // моба больше нет (фейд/AoI) — лента не нужна
	if (M->IsDead())
	{
		ASDA_CTRACE_MOB(Rid, EAsdaCT::HitFrame, TEXT("лента %s: моб уже мёртв (смерть показана другим путём) — %d запис. и удержание сняты (%s)"),
			*AsdaCtWorld::Mob(Rid, M), L->Pending.Num(), Why ? Why : TEXT("?"));
		// Смерть показана другим путём (смертельный ответ `KillTargetAfter`, запись навыка «убит»): записи и удержание сняты.
		L->Pending.Reset(); L->bDeathPending = false; L->DeathChan.Reset(); L->HoldSinceS = 0.0; L->ServerHp = -1; L->HoldExtraS = 0.0;
		W->GetTimerManager().ClearTimer(L->Timer);
		ReleaseLedgerHeld(Rid, TEXT("моб уже мёртв"));
		return;
	}
	if (L->HoldSinceS > 0.0 && Now + 0.001 >= L->HoldSinceS + kImpactHoldMaxS + L->HoldExtraS)
	{
		FlushLedger(Rid, TEXT("потолок удержания"));
		return;
	}
	// Наступившие попадания закрываются (цифры и вычет уже показаны их таймерами). ★ Ревизия Ф2 (ВАЖНО 4): снаряд — по факту показа.
	for (int32 i = L->Pending.Num() - 1; i >= 0; --i)
	{
		if (LedgerEntryCloseS(L->Pending[i]) <= Now + 0.001)
		{
			L->LastAttackS.Add(L->Pending[i].Attacker, Now);
			UE_LOG(LogAsdaWorld, Verbose, TEXT("[лента] rid=%d: попадание атакующего %d закрыто (%s%s)"), Rid, L->Pending[i].Attacker,
				Why ? Why : TEXT("?"), L->Pending[i].bLethal ? TEXT(", смертельное") : TEXT(""));
			ASDA_CTRACE_IF(AsdaCtWorld::Ours(Rid, L->Pending[i].Attacker), EAsdaCT::HitFrame, TEXT("лента %s: попадание атакующего uidx=%d наступило — запись закрыта (%s%s, непоказанный урон %d)"),
				*AsdaCtWorld::Mob(Rid, M), L->Pending[i].Attacker, Why ? Why : TEXT("?"), L->Pending[i].bLethal ? TEXT(", СМЕРТЕЛЬНОЕ") : TEXT(""),
				L->Pending[i].PendingDamage);
			L->Pending.RemoveAt(i);
		}
	}
	if (L->bDeathPending && LedgerDeathDueS(*L, Now) <= Now + 0.001)
	{
		ASDA_CTRACE_MOB(Rid, EAsdaCT::Death, TEXT("лента %s: срок отложенной смерти наступил (%s) — применяется"), *AsdaCtWorld::Mob(Rid, M), Why ? Why : TEXT("?"));
		ApplyLedgerDeath(M, Why);
		return;
	}
	LedgerReconcileHp(Rid, Why);
	L = ImpactLedgers.Find(Rid);
	if (!L) { return; }
	// ★ Ф2 (ревизия Ф1 п.5): записей нет, а смерть ещё не пришла — удержанный показ (добыча 5005, свой опыт 4038) НЕ выпускается:
	//   добыча по мобу означает его смерть, она догонит (4017 flags=0 / 4015 HP=0 — `LedgerDeferDeath` выпустит вместе с ней), а
	//   выпуск сейчас клал вещь под ЖИВОГО моба. Держит общий потолок удержания (`RearmLedgerTimer` → `HoldSinceS` + 4.5 с).
	RearmLedgerTimer(Rid);
}

void UAsdaWorldSubsystem::FlushLedger(int32 Rid, const TCHAR* Why)
{
	FMobImpactLedger* L = ImpactLedgers.Find(Rid);
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!L) { return; }
	const double Now = W ? W->GetTimeSeconds() : 0.0;
	const bool bDeath = L->bDeathPending;       // отложенная смерть
	const int32 Held = L->ServerHp;             // придержанный снимок
	const bool bNum = L->bServerHpNumber;       // канал снимка показал бы цифру
	// ★★★ Ф2 (ревизия Ф1 п.1б — КРИТИЧНО): записи с НАЗНАЧЕННЫМ ответом попаданием (`CommittedAtS` впереди) НЕ снимаются — их
	//   цифры уже стоят в таймерах и вычтут свой урон сами. Прежде лента ставила полосу на голый снимок и удаляла их — таймеры
	//   цифр вычитали урон второй раз (полоса ниже сервера, затем рывок вверх). Снимаются только заявки без ответа; полоса —
	//   на «снимок + ещё не показанный урон оставшихся записей».
	int64 KeptCover = 0;   // известный непоказанный урон оставшихся (назначенных ответом) записей
	for (int32 i = L->Pending.Num() - 1; i >= 0; --i)
	{
		FAsdaImpactEntry& E = L->Pending[i];
		// ★ Ревизия Ф2 (ВАЖНО 4): снаряд в полёте (урон ещё не показан, запас не вышел) — тоже не снимается: его цифра вычтет сама.
		const bool bProjectileInFlight = E.Kind == EAsdaImpactKind::Projectile && E.PendingDamage > 0 && LedgerEntryCloseS(E) > Now + 0.001;
		if (E.CommittedAtS > Now + 0.001 || bProjectileInFlight)
		{
			if (E.CommittedAtS > Now + 0.001) { E.AtS = FMath::Min(E.AtS, E.CommittedAtS); }   // заявки поверх ответа больше не ждём
			E.bOutcomeUnknown = E.bCommittedOutcomeUnknown;   // исход — по последнему ответу
			KeptCover += E.PendingDamage;
			continue;
		}
		L->LastAttackS.Add(E.Attacker, Now);   // авторство дельты — по снятым записям
		L->Pending.RemoveAt(i);
	}
	L->bDeathPending = false; L->HoldSinceS = 0.0;
	if (L->Pending.Num() == 0) { L->HoldExtraS = 0.0; }   // ★ 10.11.2026 (ревизия 5)
	L->ServerHp = L->Pending.Num() > 0 ? Held : -1;   // снимок нужен оставшимся записям для сверки на их попадании
	if (W) { W->GetTimerManager().ClearTimer(L->Timer); }
	AAsdaMobActor* M = FindNetMobByRid(Rid);
#if !UE_BUILD_SHIPPING
	++LedgerFlushCount;   // ★ Ф2: наблюдение самотеста (бой дольше потолка не должен сюда приходить)
#endif
	UE_LOG(LogAsdaWorld, Log, TEXT("[лента] rid=%d: удержанное применено сразу (%s; оставлено назначенных ответом записей %d, их урон %lld)"),
		Rid, Why ? Why : TEXT("?"), L->Pending.Num(), KeptCover);
	ASDA_CTRACE_MOB(Rid, EAsdaCT::HpPlate, TEXT("лента %s: удержанное применяется СРАЗУ (%s): смерть %s, снимок %d, оставлено назначенных ответом записей %d (их урон %lld)"),
		*AsdaCtWorld::Mob(Rid, M), Why ? Why : TEXT("?"), bDeath ? TEXT("отложена — применяется") : TEXT("нет"), Held, L->Pending.Num(), KeptCover);
	if (M && !M->IsDead())
	{
		if (bDeath) { ApplyLedgerDeath(M, Why); return; }
		const int64 Want = (int64)Held + KeptCover;   // полоса после потолка
		if (Held >= 0 && Want < (int64)M->CurHp)
		{
			ASDA_CTRACE_MOB(Rid, EAsdaCT::HpPlate, TEXT("лента %s: HP полосы %d → %lld (снимок %d + назначенный урон %lld; %s)"), *AsdaCtWorld::Mob(Rid, M),
				M->CurHp, Want, Held, KeptCover, Why ? Why : TEXT("?"));
			ApplyMobHpUnfiltered(M, (int32)Want, bNum && KeptCover == 0);
		}
	}
	ReleaseLedgerHeld(Rid, Why);
	RearmLedgerTimer(Rid);
}

void UAsdaWorldSubsystem::ReleaseLedgerHeld(int32 Rid, const TCHAR* Why)
{
	FMobImpactLedger* L = ImpactLedgers.Find(Rid);
	if (!L || L->HeldLoot.Num() == 0) { return; }
	TArray<FAsdaHeldPresentation> Out = MoveTemp(L->HeldLoot);   // забираем до выпуска: выпуск может тронуть карту лент
	L->HeldLoot.Reset();
	for (FAsdaHeldPresentation& H : Out)
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[лента] rid=%d: выпущено удержанное «%s» (%s)"), Rid, *H.Why, Why ? Why : TEXT("?"));
		ASDA_CTRACE_MOB(Rid, EAsdaCT::Reward, TEXT("rid=%d: ВЫПУЩЕНО удержанное до смерти «%s» (%s)"), Rid, *H.Why, Why ? Why : TEXT("?"));
		if (H.Release) { H.Release(); }
	}
}

void UAsdaWorldSubsystem::ApplyLedgerDeath(AAsdaMobActor* M, const TCHAR* Why)
{
	if (!M) { return; }
	const int32 Rid = M->NetRenderId;   // моб смерти
	FString Chan = TEXT("ответ");       // канал отложенной смерти (журнал)
	if (FMobImpactLedger* L = ImpactLedgers.Find(Rid))
	{
		if (!L->DeathChan.IsEmpty()) { Chan = L->DeathChan; }
		// Состояние ленты снимается ДО снятия цели: снятие цели контроллером прерывает свои записи (`CutImpactEntries`).
		L->Pending.Reset(); L->bDeathPending = false; L->DeathChan.Reset(); L->HoldSinceS = 0.0; L->ServerHp = -1; L->HoldExtraS = 0.0;
		if (UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr) { W->GetTimerManager().ClearTimer(L->Timer); }
	}
	if (!M->IsDead())
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[net] смерть моба rid=%d '%s' (отложенная лентой из %s — %s)"), Rid, *M->MobName, *Chan, Why ? Why : TEXT("?"));
		ASDA_CTRACE_MOB(Rid, EAsdaCT::Death, TEXT("%s: СМЕРТЬ применена (отложенная лентой из %s — %s), HP полосы было %d/%d → NetDeath%s"),
			*AsdaCtWorld::Mob(Rid, M), *Chan, Why ? Why : TEXT("?"), M->CurHp, M->MaxHp,
			(Rid == AsdaCombatTrace::CurrentCycleTarget()) ? TEXT(", цель цикла") : TEXT(""));
		if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
		{
			if (PC->TargetActor.Get() == M) { PC->ClearTarget(); }
		}
		M->NetDeath();
		// Журнал боя: метка «цель умерла» — ДО выпуска удержанного (смерть → награда); в ветке «уже мёртв» не повторяется.
		ASDA_CTRACE_END_IF_TARGET(Rid, TEXT("цель умерла (смерть ленты попаданий)"));
	}
	else
	{
		ASDA_CTRACE_MOB(Rid, EAsdaCT::Death, TEXT("%s: смерть ленты (%s) — моб уже мёртв, только снятие ленты"), *AsdaCtWorld::Mob(Rid, M), Why ? Why : TEXT("?"));
	}
	ReleaseLedgerHeld(Rid, Why);
}

void UAsdaWorldSubsystem::CutImpactEntries(int32 Attacker, const TCHAR* Why, int32 OnlyRid, int32 ExceptRid)
{
	if (CVarAsdaHitFrameAllChannels.GetValueOnGameThread() == 0) { return; }   // ключ 0 — ленты нет
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return; }
	const double Now = W->GetTimeSeconds();   // мир-время прерывания
	TArray<int32> Touched;                    // ленты, где записи изменились
	for (TPair<int32, FMobImpactLedger>& KV : ImpactLedgers)
	{
		if (OnlyRid >= 0 && KV.Key != OnlyRid) { continue; }
		if (ExceptRid >= 0 && KV.Key == ExceptRid) { continue; }
		TArray<FAsdaImpactEntry>& P = KV.Value.Pending;
		for (int32 i = P.Num() - 1; i >= 0; --i)
		{
			FAsdaImpactEntry& E = P[i];
			if (E.Attacker != Attacker || (E.Kind != EAsdaImpactKind::Attack && E.Kind != EAsdaImpactKind::Projectile)) { continue; }
			if (E.CommittedAtS > Now + 0.001)
			{
				// Ответ сервера уже назначил попадание: его цифры, полоса и смерть придут по своим таймерам — запись доживает до него.
				E.AtS = FMath::Min(E.AtS, E.CommittedAtS);
				E.bOutcomeUnknown = E.bCommittedOutcomeUnknown;   // прерванная заявка исхода уже не принесёт
				UE_LOG(LogAsdaWorld, Log, TEXT("[бой] rid=%d: замах атакующего %d прерван (%s) — запись сокращена до назначенного ответом попадания (+%.2f с)"),
					KV.Key, Attacker, Why ? Why : TEXT("?"), E.AtS - Now);
				ASDA_CTRACE_IF(AsdaCtWorld::Ours(KV.Key, Attacker), EAsdaCT::Swing, TEXT("лента rid=%d: замах атакующего uidx=%d ПРЕРВАН (%s) — запись сокращена до назначенного ответом попадания (+%.2f с)"),
					KV.Key, Attacker, Why ? Why : TEXT("?"), E.AtS - Now);
			}
			else
			{
				UE_LOG(LogAsdaWorld, Log, TEXT("[бой] rid=%d: замах атакующего %d прерван (%s) — назначенного ответом попадания нет, запись снята"),
					KV.Key, Attacker, Why ? Why : TEXT("?"));
				ASDA_CTRACE_IF(AsdaCtWorld::Ours(KV.Key, Attacker), EAsdaCT::Swing, TEXT("лента rid=%d: замах атакующего uidx=%d ПРЕРВАН (%s) — ответа не было, запись снята (придержанное HP/смерть применятся без неё)"),
					KV.Key, Attacker, Why ? Why : TEXT("?"));
				KV.Value.LastAttackS.Add(Attacker, Now);
				P.RemoveAt(i);
			}
			Touched.AddUnique(KV.Key);
		}
	}
	for (const int32 Rid : Touched) { ProcessLedger(Rid, Why); }
}

void UAsdaWorldSubsystem::DropLedger(int32 Rid)
{
	FMobImpactLedger* L = ImpactLedgers.Find(Rid);
	if (!L) { return; }
	if (UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr) { W->GetTimerManager().ClearTimer(L->Timer); }
	TArray<FAsdaHeldPresentation> Out = MoveTemp(L->HeldLoot);   // удержанный показ не теряется: добыча и опыт настоящие
	ImpactLedgers.Remove(Rid);
	ForgetLethalState(Rid);   // ★ ревизия 18.10 (п.6): моба больше нет — его поставленные удары и метка смертельного не нужны
	for (FAsdaHeldPresentation& H : Out) { if (H.Release) { H.Release(); } }
}

void UAsdaWorldSubsystem::ArmSkillImpact(int32 CasterIdx, int32 Rid, bool bFrom4102, int32 Damage, int32 HpAfter, float RecordDelayS)
{
	if (!IsImpactLedgerOn(AsdaLedgerIsSelf(GetGameInstance(), CasterIdx))) { return; }
	AAsdaMobActor* M = FindNetMobByRid(Rid);
	if (!M || M->IsDead()) { return; }
	// −1 в `HpAfter` = «сервер HP не сообщил» (у 4102 и у первых частей многоударного навыка) — смертельной такая запись не бывает.
	// ★ 05.11.2026 (bug-mob-hp-zero, ключ `asda.Combat.HealEpoch`): запись, посчитанная сервером до лечения 8013, — не снимок и не смерть.
	const bool bPreHeal = IsPreHealRecord(M, HpAfter, Damage);
	const bool bLethal = HpAfter <= 0 && HpAfter != -1 && !bPreHeal;
	ASDA_CTRACE_IF(AsdaCtWorld::Ours(Rid, CasterIdx), EAsdaCT::Skill, TEXT("%s: запись навыка (%s) кастера uidx=%d в ленту — урон %d, HP после %d%s; HP и смерть ждут момента удара навыка"),
		*AsdaCtWorld::Mob(Rid, M), bFrom4102 ? TEXT("4102 припаркован") : TEXT("5257 на полке"), CasterIdx, Damage, HpAfter, bLethal ? TEXT(", СМЕРТЕЛЬНАЯ") : TEXT(""));
	const EAsdaImpactKind K = bFrom4102 ? EAsdaImpactKind::Proc4102 : EAsdaImpactKind::Skill;   // вид записи
	// Момент удара навыка (порог фазы / прилёт носителя) знает только слой показа: запись живёт до выпуска, не дольше потолка взвода.
	// ★ 10.11.2026 (job4-vfx): отложенная запись (907/911) выпускается через RecordDelayS после порога фазы — надбавка к потолкам ленты.
	if (RecordDelayS > 0.f) { FMobImpactLedger& LX = ImpactLedgers.FindOrAdd(Rid); LX.HoldExtraS = FMath::Max(LX.HoldExtraS, (double)RecordDelayS); }
	ArmImpact(Rid, CasterIdx, K, kImpactWindowMaxS + FMath::Max(0.f, RecordDelayS), /*bCommitted=*/true, bLethal, FMath::Max(0, Damage), /*bOutcomeKnown=*/true,
		bFrom4102 ? TEXT("4102 припаркован") : TEXT("5257 на полке"));
	if (FMobImpactLedger* L = ImpactLedgers.Find(Rid))
	{
		if (FAsdaImpactEntry* E = L->Pending.FindByPredicate([CasterIdx, K](const FAsdaImpactEntry& X) { return X.Attacker == CasterIdx && X.Kind == K; }))
		{
			++E->PendingRecs;
		}
	}
	if (HpAfter >= 0 && !bPreHeal) { NoteLedgerSnapshot(Rid, HpAfter, /*bAllowNumber=*/false); }   // «5257 +11» / «4102 HP после» — серверный снимок
}

bool UAsdaWorldSubsystem::ConsumeSkillImpact(int32 CasterIdx, int32 Rid, bool bFrom4102, int32 Damage)
{
	FMobImpactLedger* L = ImpactLedgers.Find(Rid);
	if (!L) { return false; }
	const EAsdaImpactKind K = bFrom4102 ? EAsdaImpactKind::Proc4102 : EAsdaImpactKind::Skill;   // вид записи
	FAsdaImpactEntry* E = L->Pending.FindByPredicate([CasterIdx, K](const FAsdaImpactEntry& X) { return X.Attacker == CasterIdx && X.Kind == K; });
	if (!E) { return false; }
	E->PendingRecs = FMath::Max(0, E->PendingRecs - 1);
	E->PendingDamage = FMath::Max(0, E->PendingDamage - FMath::Max(0, Damage));
	if (E->PendingRecs == 0)
	{
		UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		E->AtS = W ? W->GetTimeSeconds() : 0.0;   // все записи навыка выпущены — попадание наступило сейчас
		// ★ 10.11.2026: надбавка снимается, только когда ни у одного кастера не осталось невыпущенных записей навыка (ревизия 5).
		if (!L->Pending.ContainsByPredicate([](const FAsdaImpactEntry& X) { return X.Kind == EAsdaImpactKind::Skill && X.PendingRecs > 0; })) { L->HoldExtraS = 0.0; }
	}
	return true;
}

bool UAsdaWorldSubsystem::HoldUntilMobDeathShown(int32 Rid, TFunction<void()> Release, const TCHAR* Why, bool bReleaseOnReset)
{
	if (CVarAsdaHitFrameAllChannels.GetValueOnGameThread() == 0 || Rid < 0) { return false; }
	FMobImpactLedger* L = ImpactLedgers.Find(Rid);
	const AAsdaMobActor* M = FindNetMobByRid(Rid);
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!L || !M || !W || M->IsDead()) { return false; }
	if (!L->bDeathPending && L->Pending.Num() == 0) { return false; }   // лента смерть не держит — показ сразу
	FAsdaHeldPresentation& H = L->HeldLoot.AddDefaulted_GetRef();
	H.Why = Why ? Why : TEXT("?");
	H.Release = MoveTemp(Release);
	H.bReleaseOnReset = bReleaseOnReset;
	if (L->HoldSinceS <= 0.0) { L->HoldSinceS = W->GetTimeSeconds(); }   // потолок общий с удержанием HP/смерти
	UE_LOG(LogAsdaWorld, Log, TEXT("[лента] rid=%d '%s': «%s» удержан до показа смерти (%d запис., смерть %s)"), Rid, *M->MobName, *H.Why,
		L->Pending.Num(), L->bDeathPending ? TEXT("отложена") : TEXT("не пришла"));
	ASDA_CTRACE_MOB(Rid, EAsdaCT::Reward, TEXT("%s: «%s» УДЕРЖАН до показа смерти (%d запис., смерть %s)"), *AsdaCtWorld::Mob(Rid, M), *H.Why,
		L->Pending.Num(), L->bDeathPending ? TEXT("отложена") : TEXT("ещё не пришла"));
	RearmLedgerTimer(Rid);
	return true;
}

// ═══ ОБЁРТКИ ПРЕЖНЕГО ОКНА «НАШ ЗАМАХ В ПОЛЁТЕ» (их зовут контроллер, ритм атаки и самотест кадра удара) ═══
void UAsdaWorldSubsystem::CutOurImpactWindow(const TCHAR* Why, int32 OnlyRid)
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !Net->IsSelfUserNumKnown()) { return; }
	CutImpactEntries((int32)Net->SelfUserNum, Why, OnlyRid);
}

void UAsdaWorldSubsystem::ArmOurImpactWindowFromSend(AAsdaPlayerCharacter* Ch, AAsdaMobActor* M, int32 WeaponSub, const TCHAR* Why)
{
	if (CVarAsdaHitFrameAllChannels.GetValueOnGameThread() == 0 || !Ch || !M || M->IsDead())
	{
		ASDA_CTRACE(EAsdaCT::HitFrame, TEXT("запись ленты по отправке (%s) НЕ ставится: %s"), Why ? Why : TEXT("?"),
			(CVarAsdaHitFrameAllChannels.GetValueOnGameThread() == 0) ? TEXT("ключ HitFrameAllChannels 0") : (!Ch ? TEXT("своего персонажа нет") : (!M ? TEXT("цели нет") : TEXT("цель уже труп"))));
		return;
	}
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !Net->IsSelfUserNumKnown()) { return; }
	// Момент визуального удара — та же арифметика, что у раскладки ударов в `OnCombatAnim`: дальнобойное — кадр удара
	//   минус пройденное (в цикле выстрел стоит в точке перемотки, т.е. сразу), ближний под серверными часами — ближайший
	//   кадр удара плана замаха (`UpcomingHitDelaySec`).
	const bool bRanged = IsRangedWeapon(WeaponSub);   // дальнобойное: к кадру удара прибавляется полёт снаряда
	const AAsdaPlayerCharacter::FAsdaComboTiming CT = Ch->GetComboTiming(WeaponSub);   // кадры удара текущего темпа (реальные с)
	// Пройденная часть замаха (с): замах ещё не начат (взвод по 4026 до старта клипа) — 0, иначе позиция клипа прошлого
	//   замаха давала «кадр удара +0.00» (видно в журнале самотеста 17.10).
	const float Elapsed = Ch->IsAttackAnimActive() ? Ch->GetAttackSwingElapsedSec() : 0.f;
	float At = (CT.bValid && CT.HitTimesSec.Num() > 0)   // до ближайшего кадра удара от текущей позиции замаха (с)
		? FMath::Max(0.f, CT.HitTimesSec[0] - Elapsed)
		: 0.5f;
	// ★ Ф2: тот же план кадров, что у раскладки ударов (`OwnPlanDelay` в `OnCombatAnim`): гейт часов и «только ближний» снят
	//   ключом `asda.Combat.SwingPlanFromPackets` (0 — прежний гейт).
	const bool bPlanOk = AAsdaPlayerCharacter::IsSwingPlanFromPackets() || (!bRanged && Ch->IsServerRhythmOwner());
	if (bPlanOk)
	{
		const float Up = Ch->UpcomingHitDelaySec(0);   // ближайший кадр удара плана (−1 — нет)
		if (Up >= 0.f) { At = Up; }
	}
	float Flight = 0.f;   // оценка полёта снаряда до цели (с); ближний — 0
	if (bRanged)
	{
		bool bBoneOk = false;                                     // кость дула найдена (иначе — запасная точка)
		const FVector MuzzleNow = ProjectileMuzzle(Ch, bBoneOk);  // точка вылета снаряда сейчас
		FVector TO, TE; M->GetActorBounds(false, TO, TE);         // центр (TO) и полуразмер (TE) цели
		Flight = AAsdaProjectileActor::FlightTimeSeconds((float)(TO - MuzzleNow).Size(), 2.5f);
	}
	ArmImpact(M->NetRenderId, (int32)Net->SelfUserNum, bRanged ? EAsdaImpactKind::Projectile : EAsdaImpactKind::Attack,
		At + Flight + 0.05f, /*bCommitted=*/false, /*bLethal=*/false, 0, /*bOutcomeKnown=*/false, Why);
	UE_LOG(LogAsdaWorld, Log, TEXT("[бой] rid=%d: запись ленты по отправке (%s): кадр удара +%.2f с, снаряд %.2f с"),
		M->NetRenderId, Why ? Why : TEXT("?"), At, Flight);
	ASDA_CTRACE(EAsdaCT::HitFrame, TEXT("%s: свой замах по отправке (%s) — ожидаемый кадр удара через %.2f с%s, снаряд %.2f с (позиция замаха %.3f) — HP/смерть цели держатся до попадания"),
		*AsdaCtWorld::Mob(M->NetRenderId, M), Why ? Why : TEXT("?"), At, bPlanOk ? TEXT(" (по плану замаха)") : TEXT(" (по combo_time)"), Flight, Elapsed);
}

// ★ Ф1, ревизия Ф0 п.6 [ПОРТ]: свой яд/DoT по мобу (ступени 4017 без замаха и без записей ленты). Источник дельты — состояние,
//   наложенное НАМИ (`FAsdaEffectEntry::CasterUserIdx`, его пишет 4102/4013 при наложении): на мобе есть тикающее состояние
//   своего кастера и нет ни одного состояния другого кастера. Иначе источник не различить — авторства нет.
bool UAsdaWorldSubsystem::IsOwnDotOnMob(int32 Rid) const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Fx || !Net || !Net->IsSelfUserNumKnown()) { return false; }
	// ★ Ф2 (ревизия Ф1 п.8): «своё» — только состояние УРОНА ПО ВРЕМЕНИ, наложенное нами (прежде — любое своё временное: свой
	//   дебафф брони превращал чужую дельту в «нашу» цифру). Номера урона по времени на мобе — таблица обработчиков NpcServer
	//   (`Reports/mob_skills_npcserver.md` §1a): 68…74 DotBleed/Dot_Piercing_Accum/DotPoison/DotFire/DotDisease/DotCurse/
	//   DotFire_Excel, 106 DotPhysicalPercent. Чужое — ЛЮБОЕ состояние урона по времени другого кастера (источник не различить).
	//   ★ 17.10.2026 (ревизия Ф2, мелочь 6) — сверено с id состояний КЛИЕНТА (`Reports/skill_buff_fx.tsv`, BuffId = id записи
	//   списка состояний, в т.ч. из 4017 @8/4013): 68 Кровотечение, 69 Сильное кровотечение, 70 Яд, 71 Пламя, 72 Болезнь,
	//   73 Проклятие, 74 Воспламенение — те же номера и смысл, что у обработчиков NpcServer (индекс таблицы = id состояния:
	//   0 Ice/1 Stone/3 Stun/8 Sleep/14 ABSOLUTE_STUN совпадают с клиентскими 0/1/3/8/14). 106 у клиента — «Огненная атака»
	//   (`Effect_PlayFlameCloakStatus`), у NpcServer — DotPhysicalPercent: урон по времени — НЕ ДОКАЗАНО (номер оставлен по
	//   таблице сервера).
	auto IsDot = [](int32 Id) { return (Id >= 68 && Id <= 74) || Id == 106; };   // состояние урона по времени на мобе
	bool bOwn = false, bOther = false;   // есть своё тикающее состояние урона по времени / есть чужое
	for (const FAsdaEffectEntry& E : Fx->GetMobList(Rid))
	{
		if (E.Type != 0 || E.CasterUserIdx < 0 || !IsDot(E.Id)) { continue; }
		if (Net->IsSelfUserIdx(E.CasterUserIdx)) { bOwn |= !E.IsPermanent(); } else { bOther = true; }
	}
	return bOwn && !bOther;
}

bool UAsdaWorldSubsystem::IsServerHpSnapshotStale(int32 Rid, int32 SnapHp) const
{
	if (GAsdaLegacyMobDeath) { return false; }
	const FAsdaMobHpForecast* F = MobHpForecast.Find(Rid);
	if (!F || F->Hp < 0) { return false; }
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W || W->GetTimeSeconds() > F->UntilS) { return false; }
	// ★★★ СНИЖЕНИЕ ПРОПУСКАЕМ ВСЕГДА (правка 2026-08-25). Правило «снимок выше прогноза — значит
	//   устарел» ловит накопитель боевого сервера, но заодно глушило ВТОРОЙ поток урона: яд и
	//   прочий урон по времени идут ступеньками 391 → 383 → 375, и это ПАДЕНИЕ здоровья, а не
	//   оживление монстра. В живом логе так потерялось 23 снимка подряд.
	//   Поэтому: снимок ниже предыдущего снимка того же монстра — принимаем, даже если он выше
	//   нашего прогноза. Отбрасываем только настоящий подъём.
	const int32* Prev = LastServerHpSnapshot.Find(Rid);
	const bool bFalling = (Prev != nullptr) && (SnapHp < *Prev);
	LastServerHpSnapshot.Add(Rid, SnapHp);   // помним, что видели, независимо от решения
	// ★ 17.10.2026 (Ф2, ревизия Ф1 п.2): при ленте попаданий «падающий» снимок ВЫШЕ живого прогноза тоже устарел. Прогноз —
	//   минимум того, что сервер уже подтвердил (4029 @4 = HP − накопитель); поле 4015/4017 без вычета накопителя «падает»
	//   ступенями, оставаясь выше прогноза, — его применение поднимало полосу в момент своего замаха. Ступени яда ниже прогноза
	//   по-прежнему проходят (правка 2026-08-25). Ключ отката `asda.Combat.HitFrameAllChannels 0` — прежнее правило.
	if (bFalling && !(CVarAsdaHitFrameAllChannels.GetValueOnGameThread() != 0 && SnapHp > F->Hp)) { return false; }
	return SnapHp > F->Hp;
}
