// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ПЛАВНОСТЬ АНИМАЦИЙ: смешивание клипов, доворот модели, «хвост» сетевых поправок (22.10.2026, слово владельца
//   «проверить плавность анимаций при ходьбе, поворотах… смягчить переходы»). Разбор — в этом заголовке; проверка —
//   самотест `asda.Anim.SelfTestSmooth` (`Private/AsdaAnimSmoothSelfTest.cpp`).
//
// ═════════════════════════════ ЧТО ДЕЛАЕТ ОРИГИНАЛ (эталон + байты бинаря) ═════════════════════════════
//   1) СМЕШИВАНИЕ КЛИПОВ ЕСТЬ, 60 мс, линейно, из ЗАМЁРЗШЕЙ позы прежнего слота:
//      `CChar_SetState@0xA13D50` (entity_char.c:19239–19246) запоминает позу старого слота
//      (`effectSlots[112].field_4 = pModelObj + (animSlotIdx<<6)`, она же `self[47550]`) и взводит `animPlayFlag`
//      (`self[52132]`) для покоя (case 0) и движения (case 1/2 — только если слот сменился). Тики
//      `objUpdateAnimBlend@0xA16800`, `CGameUI_UpdateFadeAnim@0xA17B00` (game_main.c:7746–7836),
//      `charAnim_UpdateActionBlend@0xAB3060` (замах): `w = время_нового_клипа / 60.0; если w < 1 —
//      Anim_BlendKeyframes(новая, старая, 1 − w)` (`Anim_BlendKeyframes@0x112AEF0`: lerp позиции + slerp кватерниона
//      каждой кости). Время клипов — мс. Байты: `fdiv [0x1396D9C]` = 60.0f в 0xA16800 и 0xAB3060.
//      Мобы/соседи (`charAction_tickWarp23@0xF79CE0`, entity_char.c:54795–54810, флаг `[1764]`,
//      `animBlendBetweenSlots@0x114A2C0`) — те же 60 мс, но НОВЫЙ клип всё это время стоит на кадре 0 и пускается
//      с нуля только по концу бленда.
//      ★ [ПОРТ] сознательное отступление: у нас новый клип идёт с кадра 0 СРАЗУ (и у мобов) — иначе время клипа сдвигалось бы
//      на длину бленда и с ним кадры удара/шага/звуков, которые порт отсчитывает от позиции клипа.
//   2) ПОВОРОТ МОДЕЛИ ПЛАВНЫЙ, постоянной угловой скоростью:
//      игроки (свой и соседи; `objAI_FaceTargetByUserIdx@0xA23350`, зовётся из `LocalPlayer_UpdateRender` и
//      `CharManager_UpdateAll`): видимый угол `self[47641]` идёт к курсу шагом `0.0104666 рад/мс` = 600 °/с
//      (байты: `fld [0x13AE5C0]` = 0.0104665998f, 4 места в 0xA23910…0xA23A57); 180° — 0.3 с.
//      мобы (`charUpdateRotateAndFx@0xF86CB0` из тика моба `charUpdateActionState23@0xF844F0`): `0.0062831854 рад/мс`
//      = 360 °/с (байты: `fld [0x1451498]`, 4 места в 0xF86CFB…0xF86E72).
//   3) Позиция при сетевых поправках (4009 / 4007 result 5 / рассинхрон > 150) — СНАП в один кадр.
//
// ═════════════════════════════ ЧТО БЫЛО В ПОРТЕ ДО 22.10 ═════════════════════════════
//   `PlayAnimation` в режиме AnimationSingleNode — смена клипа В ОДИН КАДР (смешивания нет вовсе: отступление от
//   эталона); поворот `MeshRoot->SetWorldRotation` — мгновенный (тоже отступление); снапы — как у оригинала.
//
// ═════════════════════════════ ЧТО СДЕЛАНО (ключи, 0 = прежний порт дословно) ═════════════════════════════
//   • `UAsdaBlendAnimInstance` — тот же `UAnimSingleNodeInstance` (весь код `GetSingleNodeInstance()`, `GetPosition`,
//     `SetPlayRate`, `SetPosition` работает как прежде), но при смене клипа поза СМЕШИВАЕТСЯ: новый клип идёт с кадра 0
//     и в своём темпе (тайминги удара, шага, звуков и эффектов, привязанные к времени клипа, НЕ сдвигаются), меняется
//     только картинка первых `BlendSec`. Источник — живой прежний клип (он доигрывает своё время в течение бленда)
//     или, при цепочке переключений, снимок показанной позы (= метод оригинала). Ход бленда — по МИРОВОМУ времени:
//     у моба вне экрана (поза не тикает) бленд не «замерзает», а смена клипа, которую никто не видел, идёт без бленда.
//       asda.Anim.BlendSec        0.15  — покой/бег/стоп/фиджет/сидение/эмоции/смерть [ПОРТ: по слову владельца; у оригинала 0.06]
//       asda.Anim.BlendAttackSec  0.06  — замах и клип навыка [ОРИГИНАЛ 60 мс]
//       asda.Anim.BlendLive       1     — 1 прежний клип доигрывает во время бленда [ИСПРАВЛЕНИЕ]; 0 — замёрзшая поза [ОРИГИНАЛ]
//       asda.Anim.BlendCurve      1     — 1 плавная кривая (smoothstep) [ИСПРАВЛЕНИЕ]; 0 — линейно [ОРИГИНАЛ]
//   • Доворот: логический курс (пакеты, мини-карта, 4024, выстрелы) ставится СРАЗУ, как прежде; крутится только
//     видимая модель.
//       asda.Anim.TurnDegPerSec    600  — игроки [ОРИГИНАЛ 0.0104666 рад/мс]; 0 — мгновенно (прежний порт)
//       asda.Anim.MobTurnDegPerSec 360  — мобы [ОРИГИНАЛ 0.0062831854 рад/мс]; 0 — мгновенно
//       asda.Anim.TurnEaseSec      0.05 — мягкое торможение у цели (экспонента) [ИСПРАВЛЕНИЕ]; 0 — постоянная скорость [ОРИГИНАЛ]
//   • «Хвост» сетевой поправки игрока: актор (логика, камера, плашка, хитбокс) встаёт в точку сервера сразу, а тело
//     догоняет экспонентой. Питомец и хранитель привязаны к ТЕЛУ (актор + хвост).
//       asda.Anim.SnapSmoothSec      0.12 — постоянная времени [ИСПРАВЛЕНИЕ: у оригинала снап]; 0 — снап (прежний порт)
//       asda.Anim.SnapSmoothMaxUnits 400  — поправка длиннее — настоящий телепорт, без хвоста
//   Журнал: asda.Anim.Trace 1 — строка «[аним-плавность]» на каждую смену клипа (что, откуда, сколько бленд).
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSingleNodeInstanceProxy.h"
#include "Engine/TimerHandle.h"   // ★ 29.10: снятие покраснения по таймеру
#include "AsdaBlendAnimInstance.generated.h"

class USkeletalMeshComponent;
class UAnimSequenceBase;

// Источник последней смены клипа (диагностика самотеста и журнала).
enum class EAsdaBlendSource : uint8
{
	None,          // без бленда: ключ 0, первый клип, пустой клип
	Live,          // живой прежний клип
	Snapshot,      // снимок показанной позы (ключ BlendLive 0 или прежний клип не последовательность)
	ChainSnapshot, // смена посреди идущего бленда — снимок уже смешанной позы
	SameFrame,     // вторая смена в том же кадре — источник прежний, бленд перезапущен
	Unseen,        // поза с прошлой смены не показывалась (моб вне экрана) — без бленда
};

// Прокси: поверх штатного одиночного узла накладывает смешивание с позой-источником.
//   Все поля пишутся на игровом потоке через `GetProxyOnGameThread` (он дожидается параллельной оценки) и читаются в
//   Update/Evaluate рабочего потока — та же схема, что у полей самого `FAnimSingleNodeInstanceProxy`.
struct FAsdaBlendNodeProxy : public FAnimSingleNodeInstanceProxy
{
	FAsdaBlendNodeProxy() = default;
	explicit FAsdaBlendNodeProxy(UAnimInstance* InAnimInstance) : FAnimSingleNodeInstanceProxy(InAnimInstance) {}

	// Сброс бленда при (пере)инициализации инстанса.
	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	// Игровой поток перед обновлением: мировое время для хода бленда.
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	// Штатная поза нового клипа → смешивание с источником (если бленд идёт).
	virtual bool Evaluate(FPoseContext& Output) override;
	// Штатное продвижение клипа → ход бленда и времени живого источника.
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;

	// Время текущего клипа (для источника-«живого клипа» при смене).
	float GetCurTime() const { return GetCurrentTime(); }
	// ★ 29.10.2026: поворот кости «4» поверх готовой позы, пока идёт вздрагивание (см. UAsdaBlendAnimInstance).
	void ApplyHitFlinch(FPoseContext& Output);

	float BlendTotal = 0.f;                     // длительность идущего бленда, с (0 — бленда нет)
	float BlendElapsed = 0.f;                   // прошло с начала бленда (мировое время), с
	double BlendStartWorldS = 0.0;              // мировое время начала бленда
	double WorldNowS = 0.0;                     // мировое время текущего обновления (PreUpdate)
	bool bSmoothCurve = true;                   // кривая веса: smoothstep (true) / линейная (false, оригинал)
	bool bFromLive = false;                     // источник — живой прежний клип (true) или снимок позы (false)
	const UAnimSequenceBase* FromSeq = nullptr; // живой источник (держится UPROPERTY инстанса)
	float FromTime = 0.f;                       // время живого источника, с
	float FromRate = 1.f;                       // темп живого источника
	bool bFromLoop = false;                     // источник в петле
	TArray<FTransform> FromSnap;                // снимок позы-источника: локальные трансформы по индексу кости МЕША
	uint32 EvalSerial = 0;                      // счётчик оценок (поза показывалась ли с прошлой смены)
	float LastAlpha = 1.f;                      // вес нового клипа на последней оценке (диагностика самотеста)
	double FlinchStartS = -1.0;                 // ★ 29.10 вздрагивание: мировое время старта (<0 — нет), копия с инстанса в PreUpdate
	float FlinchRad = 0.f;                      // ★ 29.10 вздрагивание: угол поворота кости «4» вокруг её оси (рад)
	bool bFlinchApplied = false;                // ★ 29.10 вздрагивание: на последней оценке поворот лёг (диагностика самотеста)
};

// Одиночный узел со смешиванием. Ставится классом аним-инстанса (режим AnimationBlueprint с нативным классом) —
//   `GetSingleNodeInstance()` его видит, т.к. это наследник `UAnimSingleNodeInstance`.
//   ⚠ `USkeletalMeshComponent::PlayAnimation` переключает компонент в AnimationSingleNode и пересоздаёт ШТАТНЫЙ инстанс —
//   смешивание тогда молча пропадает. Клипы на таких компонентах запускать только через `AsdaAnimSmooth::PlayClip`.
UCLASS(Transient, NotBlueprintable)
class ASDAREBORN_API UAsdaBlendAnimInstance : public UAnimSingleNodeInstance
{
	GENERATED_BODY()
public:
	// Смена клипа: подготовить источник и длительность бленда, затем штатная смена.
	virtual void SetAnimationAsset(UAnimationAsset* NewAsset, bool bIsLooping = true, float InPlayRate = 1.f) override;

	// Длительность бленда для СЛЕДУЮЩЕЙ смены клипа, с (<0 — по ключу `asda.Anim.BlendSec`).
	void SetNextBlendSec(float Sec) { NextBlendSec = Sec; }
	// Идёт ли бленд сейчас и вес нового клипа (1 — бленда нет). Для самотеста/журнала.
	bool IsBlendingGT();
	float GetBlendAlphaGT();
	// Длительность последнего начатого бленда, с (0 — последняя смена шла без бленда) и его источник.
	float GetLastBlendSec() const { return LastBlendSec; }
	EAsdaBlendSource GetLastBlendSource() const { return LastSource; }

	// ★★★ 29.10.2026 ВЗДРАГИВАНИЕ ОТ УДАРА (баг игроков «персонажи стоят как вкопанные, когда их бьют мобы») [ОРИГИНАЛ].
	//   Клипа «получил удар» у персонажа-игрока в оригинале НЕТ (загрузчики `CCharacter_LoadMotionAnims@0x9B84D0`,
	//   `Char_LoadMotionAnim@0x9B9890`; в строках exe ни одного `_damage/_dmg/_hit`). Реакция процедурная:
	//   старт `gfxBias_LazyInitHalfTexel@0xA2C7B0` (МИСНЕЙМ, render_core.c:3729) — если флаг `char+47552 & 0x200` не стоит:
	//   угол `char[52287] = −0.5` рад, таймер `char[47556] = 0`, флаг 0x200 (повторный удар НЕ перезапускает);
	//   тик `charEntityUpdateTick@0xA1ED30` (entity_char.c:22713-22800, dt в мс): пока таймер < 125 мс — кость
	//   `char[52088]` (= кость «4», голова, entity_char.c:21238) домножается слева на `D3DXMatrixRotationY(угол)`
	//   (поворот в СВОЕЙ системе кости); по таймеру > 125 — флаг снят, кость в позе клипа.
	//   ★ Угол НЕ затухает: дизасм 0xA1FDC1 `fcomp [+0x330FC]; test ah,0x41; jne` — «если a > −0.5, то a = −0.5», а
	//   a = (125 − t)·(−0.5)/125 всегда ≥ −0.5 ⇒ все 125 мс держится −0.5 рад и возвращается рывком.
	//   Перевод осей: RotY эталона в оси кости UE = поворот вокруг локальной Z (Yaw) на тот же угол
	//   (Q(game) = (x, −z, y), проверено на `UAsdaCharAssembly::WeaponStowAttachRaw`: RotY(−90°) ⇒ Yaw −90°).
	//   Ключ `asda.Char.HitFlinch` (1 — как оригинал, 0 — прежний порт), длительность и угол — `asda.Char.HitFlinchMs`/`HitFlinchRad`.
	//   ★ Покраснение на те же 125 мс (вершинный свет: амбиент +360 = 0.3, точечный +640 = (1,0,0) без спада ⇒ R насыщен, G/B ×0.3):
	//   всем мешам персонажа пишется Custom Primitive Data[1] = 1 − asda.Char.HitFlinchTintGB, материал (`M_AsdaCharLit`/`M_AsdaSkinLit`,
	//   ветка `Tools/asda_add_hit_flinch_tint.py`) умножает BaseColor/Emissive на (1, 1−F, 1−F); по окончании — 0 (картинка прежняя).
	// Начать вздрагивание (игровой поток). false — уже идёт (эталон не перезапускает) или ключ выключен.
	bool StartHitFlinchGT();
	// Идёт ли вздрагивание сейчас (мировое время, игровой поток).
	bool IsHitFlinchActiveGT() const;
	// Лёг ли поворот кости на последней оценке позы (самотест).
	bool WasHitFlinchAppliedGT();

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FAsdaBlendNodeProxy(this); }

private:
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> FromSeqRef;   // живой источник бленда (держим от сборщика мусора)
	float NextBlendSec = -1.f;                  // заявка длительности на следующую смену (<0 — нет заявки)
	uint32 EvalSerialAtLastSet = 0;             // счётчик оценок на момент прошлой смены
	uint64 FrameAtLastSet = MAX_uint64;         // кадр движка прошлой смены (вторая смена в том же кадре)
	float LastBlendSec = 0.f;                   // длительность последнего начатого бленда
	EAsdaBlendSource LastSource = EAsdaBlendSource::None;   // источник последней смены
	double FlinchStartS = -1.0;                 // ★ 29.10 вздрагивание: мировое время старта (<0 — не идёт)
	float FlinchRad = 0.f;                      // ★ 29.10 вздрагивание: угол на этот раз (ключ asda.Char.HitFlinchRad на старте)
	FTimerHandle FlinchTintTimer;               // ★ 29.10 покраснение: снятие CPD[1] по окончании вздрагивания
	// ★ 29.10 покраснение: записать CPD[1] = F всем мешам персонажа (тело, части, оружие, щит).
	void ApplyFlinchTint(float F);
	friend struct FAsdaBlendNodeProxy;          // прокси читает поля вздрагивания в PreUpdate (игровой поток)
};

// Общая точка плавности: ключи консоли и помощники (для игрока, соседей, мобов, транспорта).
namespace AsdaAnimSmooth
{
	// Ключи (значения — в описании заголовка).
	ASDAREBORN_API float BlendSec();          // asda.Anim.BlendSec
	ASDAREBORN_API float BlendAttackSec();    // asda.Anim.BlendAttackSec
	ASDAREBORN_API bool  BlendLive();         // asda.Anim.BlendLive
	ASDAREBORN_API bool  BlendCurve();        // asda.Anim.BlendCurve
	ASDAREBORN_API float TurnDegPerSec();     // asda.Anim.TurnDegPerSec
	ASDAREBORN_API float MobTurnDegPerSec();  // asda.Anim.MobTurnDegPerSec
	ASDAREBORN_API float TurnEaseSec();       // asda.Anim.TurnEaseSec
	ASDAREBORN_API float SnapSmoothSec();     // asda.Anim.SnapSmoothSec
	ASDAREBORN_API float SnapSmoothMaxUnits();// asda.Anim.SnapSmoothMaxUnits
	ASDAREBORN_API bool  Trace();             // asda.Anim.Trace
	ASDAREBORN_API bool  HitFlinchOn();       // asda.Char.HitFlinch (вздрагивание от удара, см. UAsdaBlendAnimInstance)
	ASDAREBORN_API float HitFlinchSec();      // asda.Char.HitFlinchMs / 1000

	// ★ 29.10.2026: начать вздрагивание у компонента-тела персонажа (нет нашего инстанса — false).
	ASDAREBORN_API bool StartHitFlinch(USkeletalMeshComponent* Comp);

	// Поставить компоненту класс инстанса со смешиванием (вызывать в конструкторе актора или сразу после создания
	//   компонента, ДО назначения меша). Режим компонента — AnimationBlueprint с нативным классом.
	ASDAREBORN_API void Install(USkeletalMeshComponent* Comp);
	// Стоит ли на компоненте класс инстанса со смешиванием (даже если сам инстанс ещё не создан — меша нет).
	ASDAREBORN_API bool IsInstalled(const USkeletalMeshComponent* Comp);

	// Запустить клип с бленд-длительностью `BlendSecOverride` (<0 — ключ `asda.Anim.BlendSec`). Повторяет
	//   `PlayAnimation` (SetAnimation + Play) без смены режима. Инстанса ещё нет (клип пришёл до меша) — отложенный запуск:
	//   клип кладётся в `AnimationData` компонента, его подхватит инициализация инстанса (режим не меняется).
	//   Компонент без нашего класса — штатный `PlayAnimation` и предупреждение в журнал.
	ASDAREBORN_API void PlayClip(USkeletalMeshComponent* Comp, UAnimationAsset* Clip, bool bLoop, float BlendSecOverride = -1.f);

	// Шаг видимого курса к логическому (градусы UE). `DegPerSec` ≤ 0 — сразу в цель. Возвращает новый видимый курс.
	ASDAREBORN_API float StepYaw(float CurYaw, float TargetYaw, float DegPerSec, float DeltaSeconds);
}
