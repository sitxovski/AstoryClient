// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ «КОРОНА» ВЛАДЕЛЬЦА ДОБЫЧИ РЕЙД-БОССА — приём 6126 и эффект `king_cap01.jeb` над персонажем (27.09.2026).
//   Ревизия `Reports/owner_0927/charid_senders_REVIEW.md` СРЕДН-1, итог — `Reports/owner_0927/charid_senders_fix.md`.
//
// ═════════ ЧТО ЭТО В ОРИГИНАЛЕ (полный поток, сверено с эталоном, IDB и сервером) ═════════════════
//   [ПРОТОКОЛ] СЕРВЕР. Призыв рейд-босса (6124 → NpcServer 8910 → ответ) заканчивается в
//     `CMonsterManager::Recv_RaidMonsterCallResult` (GameServer.c:18024-18045): при успехе мир запоминает
//     «владельца добычи» (`sub_4C2C00(world,1)` — флаг +18676, `sub_4C2C40` «SetItemOwnerInfo» — userNum +18677
//     и charId +18679) и шлёт 6126 {i32 charId, u8 1}. Тот же пакет повторяется: при входе владельца в мир
//     (`CUserManager::CharMake` :124185, `CharPortalMake` :124388), при появлении владельца в чужом обзоре
//     (`Send_MoveSectorInClippingUserInfo` :11795/:11933/:12556/:12693) и в `Search_AddMonsterMapItem` :14206
//     (записанный владелец добычи не найден → 6126 с charId игрока-адресата этой ветки). Снятия метки у сервера НЕТ: флаг мира
//     живёт до перезапуска процесса GameServer; байт во ВСЕХ отправках = 1.
//   [ОРИГИНАЛ] КЛИЕНТ, приём — `netHandleCharIdByteUpdate@0x973E30` (network.c:3609-3640):
//     • гейт `*(BYTE*)(dword_34A0490 + 16) == 1`. `dword_34A0490` = `g_pMobMgr + 0x9A8` = указатель на записи
//       `Data\Mob\CallMobTable.BIN` (`mobLoadCallMobTable@0xF76D10`, `*((DWORD*)this + 618) = new[]`), +16 —
//       поле `SpawnArgByte` ЗАПИСИ 0 (в данных = 1 ⇒ гейт открыт). Иных записей в `dword_34A0490` нет (xref IDB);
//     • `CharManager_FindByCharId(charId)` найден → `char+0x330D5 = 1` («показать при следующем тике»),
//       `char+0x33094 = байт` (видимость); не найден → `g_nCrownOwnerId@0x2909998 = charId`,
//       `byte_290999C = байт` (байт больше НИКЕМ не читается — xref IDB: единственная запись).
//   [ОРИГИНАЛ] КЛИЕНТ, показ — `CharFx_UpdateKingCrown@0xA21110` (effects_particles.c:3528), каждый кадр для своего
//     (`LocalPlayer_UpdateRender@0x9D4170`) и для каждого персонажа (`CharManager_UpdateAll@0x9D56C0`):
//       1) `+0x330D5 && +0x33088 < 0` → `Effect_SpawnByName(g_pEffectMgr, "king_cap01.jeb", 1, pos,
//          +0x2A1F8, char->posVec, 0, /*attach*/6, 0, 1.0f)`, объект → `+0x330E4`, его id → `+0x33088`,
//          `fx+0x2A8 = 1` (зациклить: `CEffectMgr_StopByOwner` ставит 0), видимость `objMapSetFlag296All(fx+8, +0x33094)`,
//          `+0x330D5 = 0`; иначе при живом объекте — только видимость;
//       2) `g_nCrownOwnerId >= 0 && (состояние действия +0x2E6FC & 1) == 0 && g_nCrownOwnerId == char->charId(+0x41C)
//          && +0x33088 < 0` → тот же спавн и та же видимость из `+0x33094`.
//     Флаг эмиттера +296 = показ (0 гасит: `uiPanelHideAll@0x995410` ставит 0, показ панели — 1).
//   [ОРИГИНАЛ] Гашение: смена карты — только свой (`MainLoop_Tick_SoundUpdate`, шаг 11: `StopByOwner(+0x33088)`,
//     `+0x33088 = -1`, `+0x330E4 = 0`; `+0x330D5`/`+0x33094` своего НЕ трогаются); уход персонажа —
//     `CharManager_RemoveNode@0x9D2D20` (то же). `g_nCrownOwnerId` не сбрасывается никогда (живёт весь процесс).
//   Сам эффект (сверено парсером `Tools/asda_jeb_parse.py` по `king_cap01.jeb`, 27.09): 2 эмиттера 0x12 на высоте 155 —
//     M_001 `cap_01.jem` вращается по Y 0→360° за проход, M_002 `cap_02.jem` неподвижен; жизнь 4 с, endMode 0, аддитив (4/1).
//
// ═════════ ЧТО В ПОРТЕ ═════════════════════════════════════════════════════════════════════════════
//   [ПОРТ] Поля `CCharacter` → записи этой подсистемы: свой персонаж — `SelfState` (переживает ТОЛЬКО смену карты:
//     шаг 11 загрузки гасит эффект на ТОМ ЖЕ объекте; вход из выбора персонажа создаёт НОВЫЙ `CCharacter` —
//     `questCreateAndRegister@0x9D1490`: `operator new(0x39BE0)` + ctor, поля 0), чужие — `ByCharId` (умирают с актором, как объект
//     `CCharacter` у `CharManager_RemoveNode`). Глобальный владелец — `CrownOwnerCharId`/`CrownOwnerByte`.
//   [ПОРТ] Состояние действия (+0x2E6FC) у актора порта одним полем не хранится — выводится из фаз анимации
//     (`EtalonActionStateOf`): смерть 6, сидение 7/8/9, удар 5, навык 24, эмоция 33, бег 1, покой 0.
//   ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА (ключ отката `asda.Crown.GlobalUsesStoredByte 0`): ветка 2) берёт видимость из
//     `+0x33094` персонажа, а у персонажа, которого не было в мире в момент 6126, там 0 от конструктора —
//     корона спавнится ПОГАШЕННОЙ; присланный байт оригинал запоминает в `byte_290999C` и не читает нигде.
//     Порт по умолчанию берёт видимость из запомненного байта (если своя видимость 0).
//   Эффект собирается `Tools/asda_build_char_fx.py` (`ASDA_CHARFX_ONLY=king_cap01`) в `BP_Fx_king_cap01`.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"
#include "AsdaTickGate.h"   // ★ R4, доля a: шаблон класса не тикает (общая точка)
#include "AsdaKingCrownSubsystem.generated.h"

class AAsdaEffectActor;

UCLASS()
class ASDAREBORN_API UAsdaKingCrownSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// ── IAsdaSessionResettable ──
	// Смена карты: гасим эффекты (мир умер), чужие записи уходят, свои поля «ждёт показа»/«видимость» живут
	//   (у оригинала тот же объект локального игрока). Выход в выбор персонажа / вход в мир: свои поля к нулю —
	//   оригинал создаёт новый CCharacter (`questCreateAndRegister@0x9D1490`); глобальный владелец живёт.
	//   Разрыв связи = конец процесса оригинала → полный сброс, включая глобального владельца.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// ── FTickableGameObject ──
	virtual void Tick(float DeltaTime) override;   // эталон: CharFx_UpdateKingCrown для каждого персонажа мира
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaKingCrownSubsystem, STATGROUP_Tickables); }
	// ★ R4, доля a (14.11.2026): шаблон класса (CDO) не тикает — `AsdaTickGate.h`, ключ `asda.Perf.NoCdoTick`; рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }   // только игра
	virtual bool IsTickableWhenPaused() const override { return false; } // пауза — без обновления, как главный цикл

	// Приём 6126 — порт `netHandleCharIdByteUpdate@0x973E30` (разбор тела — в `UAsdaNetSubsystem`).
	void OnCharIdByteUpdate(int32 CharId, uint8 Byte);

	// Один проход показа по всем персонажам мира (тело `Tick`; публично — для самотеста).
	void UpdateAllCrowns();

	// Гейт приёма: поле `SpawnArgByte` записи 0 таблицы `DT_CallMob` == 1 (эталон `*(dword_34A0490+16) == 1`).
	bool IsGateOpen() const;

	// Эталонное состояние действия персонажа порта (+0x2E6FC оригинала), выведенное из фаз анимации. [ПОРТ]
	static int32 EtalonActionStateOf(const AActor* Actor);

	// ── Чтение для самотеста и диагностики ──
	int32 GetCrownOwnerCharId() const { return CrownOwnerCharId; }   // g_nCrownOwnerId (−1 — нет)
	uint8 GetCrownOwnerByte() const { return CrownOwnerByte; }       // byte_290999C
	// Состояние персонажа по charId (свой — по `SelectedCharId`). false — записи нет.
	bool GetCrownState(int32 CharId, bool& bOutPending, int32& OutVisible, AAsdaEffectActor*& OutFx) const;

	// ── Только самотест: подмена гейта (−1 — из таблицы, 0/1 — принудительно) и сброс состояния ──
	int32 GateOverrideForTest = -1;
	void DebugResetAll();

private:
	// Поля «короны» одного персонажа (эталон: `CCharacter` +0x330D5 / +0x33094 / +0x33088 / +0x330E4).
	struct FCrownState
	{
		bool bPending = false;                    // +0x330D5 «спавнить при следующем тике»
		int32 Visible = 0;                        // +0x33094 видимость (байт пакета)
		TWeakObjectPtr<AAsdaEffectActor> Fx;      // +0x330E4 / +0x33088 объект эффекта (пусто = id −1)
		TWeakObjectPtr<AActor> Host;              // актор-носитель (у чужих — смена актора = новый объект CCharacter)
	};

	// Тело `CharFx_UpdateKingCrown` для одного персонажа.
	void UpdateOne(int32 CharId, AActor* Actor, FCrownState& St);
	// Спавн `king_cap01.jeb` attach-6 на персонаже; nullptr — BP не собран.
	AAsdaEffectActor* SpawnCrown(AActor* Host, int32 CharId);
	// `objMapSetFlag296All(fx+8, Visible)`: 1 — показать, 0 — погасить.
	static void ApplyVisible(const FCrownState& St);
	// Погасить и забыть эффект (эталон `CEffectMgr_StopByOwner` + `+0x33088 = -1`, `+0x330E4 = 0`).
	static void StopFx(FCrownState& St);

	FCrownState SelfState;                 // свой персонаж (объект локального игрока; новый при входе из выбора персонажа)
	TMap<int32, FCrownState> ByCharId;     // чужие персонажи мира по charId
	int32 CrownOwnerCharId = -1;           // g_nCrownOwnerId@0x2909998 (−1 из .data)
	uint8 CrownOwnerByte = 0;              // byte_290999C (оригинал не читает; порт — см. исправление в шапке)
	TSet<int32> SpawnFailLogged;           // charId, по которым уже сказали «BP не собран» (без спама каждый кадр)
};
