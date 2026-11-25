// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCastBarSubsystem.cpp — полоса применения навыка `SKILL_BAR_Win` (66000).
//   Разбор: `Reports/skill_use_system.md` §1. Шапка .h объясняет модель; здесь — реализация.

#include "UI/AsdaCastBarSubsystem.h"
#include "AsdaPerf.h"   // R4, доля a: области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaSkillDatabase.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCombatTrace.h"   // ★ 18.10.2026: журнал боя — старт/прерывание/конец полосы каста

#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCastBar, Log, All);

namespace
{
	// Окно 66000 и его WBP. Имя окна = имя окна в .JUI — по нему `UAsdaWindowSubsystem` находит
	//   запись в `DA_JuiWindowMeta` и подтягивает ручки перетаскивания (EVS 6/0x20003).
	static const TCHAR* kCastBarWindowId = TEXT("SKILL_BAR_Win");
	static const TCHAR* kCastBarWbp =
		TEXT("/Game/AsdaAssets/UI/Player_UI/HUD/WBP_SKILL_BAR_Win.WBP_SKILL_BAR_Win_C");

	// Состояния действия персонажа, в которых эталон показывает полосу (`ui_core.c:89767`).
	FORCEINLINE bool StateShowsBar(int32 S) { return S == 13 || S == 14 || S == 24 || S == 27; }
}

void UAsdaCastBarSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogAsdaCastBar, Log, TEXT("[полоса каста] подсистема поднята (окно SKILL_BAR_Win 66000)"));
}

void UAsdaCastBarSubsystem::Deinitialize()
{
	ResetControls();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaCastBarSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Полоса каста показывает навык, который применял персонаж прошлого мира.
	ResetControls();
	ActiveSkill = -1;
	ValueMs = 0.f; MaxMs = 0.f;
	CharState = 0;
	LastAppliedMax = -1.f;
	bHalfPassed = false;
	bWindowShown = false;
	LastLoggedSkill = -1;
	(void)Reason;
}

void UAsdaCastBarSubsystem::ResetControls()
{
	Win = nullptr;
	Bar = FAsdaMiniBarFill();
	NameText = nullptr;
	ReserveText = nullptr;
	bControlsReady = false;
	bWindowShown = false;
	LastAppliedMax = -1.f;
}

bool UAsdaCastBarSubsystem::EnsureControls()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }

	UUserWidget* W = WM->FindWindow(FName(kCastBarWindowId));
	if (!W)
	{
		// Открываем скрытым: в .JUI у окна 66000 `Visible = 0` — оно живёт постоянно и только
		//   показывается/прячется (эталон дергает SetVisible, а не создаёт окно на каждый каст).
		W = WM->OpenWindowByPath(FName(kCastBarWindowId), kCastBarWbp, CastBarZ,
			ESlateVisibility::SelfHitTestInvisible);
		if (!W) { return false; }
		WM->SetWindowVisible(FName(kCastBarWindowId), false);
		bWindowShown = false;
	}
	if (bControlsReady && Win == W) { return true; }

	ResetControls();
	Win = W;

	// ★ ES0 = подложка, ES1 = ЗАЛИВКА (JuiSkin `Orange_Bar`, ProgressType 5, Diffuse FFFF6C00).
	Bar.Back = Cast<UImage>(AsdaUi::FindNodeByName(W, TEXT("SKILL_BAR_1_0"), TEXT("SKILL_BAR_1_0")));
	Bar.Fill = Cast<UImage>(AsdaUi::FindNodeByName(W, TEXT("SKILL_BAR_1_1"), TEXT("SKILL_BAR_1_1")));
	NameText = Cast<UTextBlock>(AsdaUi::FindNodeByName(W, TEXT("SKILL_TEST_TEXT"), TEXT("SKILL_TEST_TEXT")));
	ReserveText = Cast<UTextBlock>(AsdaUi::FindNodeByName(W, TEXT("SKILL_TEST_TEXT2"), TEXT("SKILL_TEST_TEXT2")));

	if (!Bar.Fill)
	{
		UE_LOG(LogAsdaCastBar, Warning, TEXT("[полоса каста] в WBP_SKILL_BAR_Win не найден план заливки ")
			TEXT("`SKILL_BAR_1_1` — полоса не будет анимироваться. Ассет перегенерировать НЕЛЬЗЯ ")
			TEXT("(законы no-invented-ui / ui-editable-in-ue), проверять импорт."));
		return false;
	}

	// ★ Полную ширину заливки снимаем ОДИН РАЗ (грабля EXP-бара: после первого клипа в 0 её уже
	//   не восстановить). Импортёр кладёт вложенные узлы POINT-анкером ⇒ Offsets = (L, T, W, H).
	// ★★★ ИСПРАВЛЕНО 2026-08-10 (живой тест: «полоса останавливается на 80-90% и исчезает»).
	//   Эталонный скин `Orange_Bar` (ProgressType 5) — Rect ПЛАНОВ: ES0 «0 0 30 5», ES1 «0 0 20 5».
	//   Это ТЕКСТУРНЫЕ ПАТЧИ, а рисуются оба плана в ширину КОНТРОЛА (SKILL_BAR Size «300 4»):
	//   ProgressBar эталона растягивает ES1 на `value/max · ширина контрола`. Импортёр же перенёс
	//   Rect-пропорцию в слот: подложка ES0 = 300, заливка ES1 = 300·(20/30) = 200. Клип по ширине
	//   СЛОТА ЗАЛИВКИ (200) на клампе 100 % закрывал лишь 2/3 подложки — визуально полоса «застревала
	//   на 80-90 %» и потом пряталась. Ширина и левый край берутся с ПОДЛОЖКИ (геометрия контрола),
	//   заливка на 100 % обязана совпасть с ней.
	FVector2D FillPos, FillSize, BackPos, BackSize;
	const bool bFillRect = UAsdaMiniStateSubsystem::GetSlotRect(Bar.Fill, FillPos, FillSize);
	const bool bBackRect = UAsdaMiniStateSubsystem::GetSlotRect(Bar.Back, BackPos, BackSize);
	if (bBackRect && BackSize.X > 0.f)
	{
		Bar.BaseLeft = BackPos.X;
		Bar.FullW = BackSize.X;
	}
	else if (bFillRect && FillSize.X > 0.f)
	{
		Bar.BaseLeft = FillPos.X;
		Bar.FullW = FillSize.X;
	}
	if (Bar.FullW <= 0.f)
	{
		// В .JUI контрол SKILL_BAR объявлен 300×4 — если импортёр отдал 0, берём паспорт.
		Bar.FullW = 300.f;
		UE_LOG(LogAsdaCastBar, Warning, TEXT("[полоса каста] ширина подложки/заливки из ассета = 0, взята паспортная 300 ")
			TEXT("(контрол SKILL_BAR Size «300 4»)"));
	}
	// Высоту заливки выравнивать не нужно (клип только по X), а вертикаль оставляем ассетную.

	bControlsReady = true;
	UE_LOG(LogAsdaCastBar, Log, TEXT("[полоса каста] контролы связаны: заливка=%s (слот %.0f×%.0f) подложка=%s (слот %.0f×%.0f) ")
		TEXT("имя=%s резерв=%s; рабочая ширина=%.0f левый край=%.0f (по ПОДЛОЖКЕ — ширина контрола, не текстурного патча ES1)"),
		Bar.Fill ? TEXT("да") : TEXT("НЕТ"), FillSize.X, FillSize.Y,
		Bar.Back ? TEXT("да") : TEXT("НЕТ"), BackSize.X, BackSize.Y,
		NameText ? TEXT("да") : TEXT("НЕТ"), ReserveText ? TEXT("да") : TEXT("НЕТ"), Bar.FullW, Bar.BaseLeft);
	return true;
}

void UAsdaCastBarSubsystem::Begin(int32 SkillCode, float InMaxMs, float InFullMs, int32 State)
{
	ActiveSkill = SkillCode;
	ValueMs = 0.f;
	// Эталон `uiUpdateActiveSkillSlot` (`ui_core.c:89747-89762`): `if (max <= 0.0) max = 1.0`.
	//   ★ ЕДИНИЦА — МИЛЛИСЕКУНДА, а не кадр: и значение (`Object_GetFloat13` анимобъекта, шаг
	//   `dt·animSpeedScale`), и максимум (слот +0x58) живут в мс — `combo time.bin` переводится из
	//   кадров в мс ЕЩЁ ПРИ ЗАГРУЗКЕ (`CComboManager_LoadComboTime@0x9BC5F0`, `item_inventory.c:428`,
	//   ×33.333328), а COMBO0 из `SkillInfo.inl` изначально в мс. Значит `max = 1.0` = 1 мс, и
	//   наблюдаемый итог — полоса заполняется мгновенно.
	//   ⚠ У ОРИГИНАЛА нулевой максимум ТОЖЕ бывает штатно (174 строки таблицы клипов из 1664 имеют
	//   COMBO0 == 0), поэтому подмена — это не «наша заглушка», а эталонное поведение.
	MaxMs = InMaxMs;
	FullMs = InFullMs;
	CharState = State;
	// ★★★ 16.09.2026: множитель темпа анимаций навыков ПОДКЛЮЧЁН К ИСТОЧНИКУ. Поле `AnimSpeedScale`
	//   стояло здесь с самого порта полосы, но всегда равнялось 1.0 — считать его было нечем.
	//   Теперь берём эталонное `pChar->animSpeedScale` (`CCharacter + 0x500`) своего персонажа:
	//   и значение полосы (`ValueMs`, шаг `dt · animSpeedScale`), и её максимум (COMBO0) живут в
	//   КЛИП-ВРЕМЕНИ, поэтому при ускоренных анимациях полоса обязана наполняться быстрее ровно во
	//   столько же раз — иначе она отстаёт от клипа, который игрок видит. Разбор — у
	//   `UAsdaWorldSubsystem::GetAnimSpeedScale`; откат — `asda.Combat.AnimSpeedScale 0`.
	//   ★ Снимок берётся на старте каста и в течение показа не пересчитывается — как у эталона,
	//   где поле меняется только при пересборке внешности (смене экипировки).
	AnimSpeedScale = 1.f;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (const UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>())
		{
			if (const AAsdaPlayerCharacter* Self = Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor))
			{
				AnimSpeedScale = WS->GetAnimSpeedScale(Self);
			}
		}
	}
	bHalfPassed = false;
	LastAppliedMax = -1.f;
	ASDA_CTRACE(EAsdaCT::Skill, TEXT("полоса каста СТАРТ: навык %d, максимум COMBO0 %.0f мс%s, полный клип %.0f мс, состояние действия %d (полоса видна: %s), темп анимации ×%.2f"),
		SkillCode, InMaxMs, InMaxMs <= 0.f ? TEXT(" (ноль → эталонный 1 мс, мгновенно)") : TEXT(""), InFullMs, State,
		StateShowsBar(State) ? TEXT("да") : TEXT("нет — состояние не 13/14/24/27"), AnimSpeedScale);

	if (LastLoggedSkill != SkillCode)
	{
		LastLoggedSkill = SkillCode;
		if (InMaxMs > 0.f)
		{
			UE_LOG(LogAsdaCastBar, Log, TEXT("[полоса каста] навык %d: максимум = COMBO0 %.0f мс, полная длительность клипа %.0f мс"),
				SkillCode, InMaxMs, InFullMs);
		}
		else
		{
			UE_LOG(LogAsdaCastBar, Log, TEXT("[полоса каста] навык %d: максимум нулевой. Источник максимума — ")
				TEXT("`DT_SkillClips` (колонка Combo0Ms, ветка выбирается полом и подтипом оружия; таблица ")
				TEXT("снята с хардкод-загрузчиков `CharSkillEffect_InitDispatchTable@0xA89510`, 1664 строки). ")
				TEXT("Ноль означает ОДНО ИЗ ДВУХ: (а) строки навыка в таблице нет, (б) COMBO0 == 0 в данных ")
				TEXT("оригинала (174 строки из 1664: 69 клипов без записи в `combo time.bin` + 105 с hit[0] == 0). ")
				TEXT("Эталон в обоих случаях ставит max = 1 мс — полоса заполняется мгновенно. ")
				TEXT("Различить: UAsdaSkillFxSubsystem::TryGetCastBarMaxMs / консоль asda.Skill.ClipInfo."), SkillCode);
		}
	}
	ApplyVisibility();
	ApplyName();
	ApplyValue();
}

void UAsdaCastBarSubsystem::SetState(int32 State)
{
	if (CharState == State) { return; }
	ASDA_CTRACE(EAsdaCT::Skill, TEXT("полоса каста: состояние действия %d → %d (навык %d, полоса видна при 13/14/24/27: %s)"),
		CharState, State, ActiveSkill, StateShowsBar(State) ? TEXT("да") : TEXT("нет"));
	CharState = State;
	ApplyVisibility();
}

void UAsdaCastBarSubsystem::End(const TCHAR* Reason)
{
	if (ActiveSkill < 0)
	{
		// Гасить нечего — и окно ради этого создавать не надо.
		return;
	}
	ASDA_CTRACE(EAsdaCT::Skill, TEXT("полоса каста КОНЕЦ/ПРЕРЫВАНИЕ: навык %d — %s (прошло %.0f из %.0f мс = %.0f%%, полный клип %.0f мс, за половиной: %s)"),
		ActiveSkill, Reason ? Reason : TEXT("—"), ValueMs, MaxMs, MaxMs > 0.f ? 100.f * FMath::Min(ValueMs / MaxMs, 1.f) : 100.f,
		FullMs, bHalfPassed ? TEXT("да") : TEXT("нет"));
	UE_LOG(LogAsdaCastBar, Log, TEXT("[полоса каста] навык %d погашен: %s (значение %.0f / %.0f мс)"),
		ActiveSkill, Reason ? Reason : TEXT("—"), ValueMs, MaxMs);
	ActiveSkill = -1;
	ValueMs = 0.f;
	CharState = 0;
	ApplyVisibility();
}

void UAsdaCastBarSubsystem::ForceShowForTest(int32 SkillCode, float TestMaxMs)
{
	// Состояние 24 = «исполнение навыка» — ровно то, что ставит `Recv_SkillCast` при успехе.
	Begin(SkillCode, TestMaxMs > 0.f ? TestMaxMs : 1000.f, 0.f, 24);
}

void UAsdaCastBarSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.CastBar");   // R4, доля a: область замера (AsdaPerf.h; в Shipping пусто)
	// ★ Окно создаём ЛЕНИВО — на первом же `Begin`. В эталоне окно 66000 существует всегда
	//   (`Visible = 0` в .JUI), но там оно живёт внутри игрового HUD, а у нас подсистема жива и на
	//   экране логина: создавать там окно незачем, а пытаться делать это каждый кадр — тем более.
	if (ActiveSkill < 0) { return; }
	if (!EnsureControls()) { return; }

	// ★ Продвижение значения. Эталон: `CObject_SetFloatDispatch(animObj, dt · animSpeedScale)`,
	//   где `dt` движка — МИЛЛИСЕКУНДЫ (закон из weapon-projectiles). UE даёт секунды ⇒ ×1000.
	//   ★ Нижний зажим — 0.1, как во всех прочих точках обеих шкал темпа (было 0.f). Источник
	//     (`GetAnimSpeedScale`) и так не отдаёт меньше 0.1, но порог обязан быть один и тот же везде.
	ValueMs += DeltaTime * 1000.f * FMath::Max(0.1f, AnimSpeedScale);

	// Признак «анимация доиграла» — `Game_IsPausedOrMenu@0x99ED60`: `obj[13] >= obj[11]`
	//   (текущее время >= полной длительности клипа). У эталона это один из ~30 путей гашения.
	if (bAutoEndOnClipFinished && FullMs > 0.f && ValueMs >= FullMs)
	{
		End(TEXT("анимация навыка доиграла (эталон: obj[13] >= obj[11])"));
		return;
	}
	ApplyValue();
}

void UAsdaCastBarSubsystem::ApplyVisibility()
{
	if (!EnsureControls()) { return; }
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }

	// Эталон `uiUpdateActiveSkillSlot`, дословно: сперва «есть активный слот?», ПОТОМ гейт состояния.
	const bool bWant = (ActiveSkill >= 0) && StateShowsBar(CharState);
	if (bWant == bWindowShown) { return; }
	ASDA_CTRACE(EAsdaCT::Skill, TEXT("полоса каста %s (окно SKILL_BAR_Win 66000): навык %d, состояние %d"),
		bWant ? TEXT("ПОКАЗАНА") : TEXT("скрыта"), ActiveSkill, CharState);
	bWindowShown = bWant;
	WM->SetWindowVisible(FName(kCastBarWindowId), bWant);
}

void UAsdaCastBarSubsystem::ApplyName()
{
	if (!NameText) { return; }
	// Эталон берёт имя из ИЗУЧЕННОЙ записи навыка окна навыков (`uiGetRecordByIndex(g_pSkillWnd, id)`
	//   + 4), а НЕ из notice-таблицы и не из `.sif`. В порту это `UAsdaSkillDatabase::Get()->Name`
	//   (та же запись `Skilltooltip.BIN +4`, которой питается окно навыков).
	// ★ Аудит №3 (A12): база — своя подсистема; раньше сюда ходили ЧЕРЕЗ окно навыков, и не открывал
	//   игрок книгу — полоса оставалась без имени.
	FString Name;
	if (const UAsdaSkillDatabase* Db = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillDatabase>() : nullptr)
	{
		if (const FAsdaSkillDef* D = Db->Get(ActiveSkill)) { Name = D->Name; }
	}
	if (Name.IsEmpty()) { Name = FString::Printf(TEXT("%d"), ActiveSkill); }
	NameText->SetText(FText::FromString(Name));
	// Контрол 66003 (`ReserveText`) НЕ ТРОГАЕМ — ссылок на обновление его текста в срезе нет.
}

void UAsdaCastBarSubsystem::ApplyValue()
{
	if (!Bar.IsValid()) { return; }
	// Эталон `uiUpdateActiveSkillSlot` (`ui_core.c:89747-89762`), построчно:
	//   if (value < 0) value = 0;  if (max <= 0) max = 1.0;  if (value > max) value = max;
	//   if (value + 0.5*max > max && !byte_2FFC888) byte_2FFC888 = 1;
	//   if (lastMax != max) { lastMax = max; SetRange(0, (int)max); }   SetValue(value);
	// ★ 1.0 здесь = 1 МИЛЛИСЕКУНДА (обоснование единиц — в Begin выше).
	float V = FMath::Max(0.f, ValueMs);
	float M = (MaxMs <= 0.f) ? 1.f : MaxMs;
	V = FMath::Min(V, M);
	if (V + 0.5f * M > M && !bHalfPassed)
	{
		ASDA_CTRACE(EAsdaCT::Skill, TEXT("полоса каста навыка %d прошла половину: %.0f / %.0f мс"), ActiveSkill, V, M);
		bHalfPassed = true;
	}
	ASDA_CTRACE_V(EAsdaCT::Skill, TEXT("полоса каста кадр: навык %d %.0f / %.0f мс (%.3f)"), ActiveSkill, V, M, M > 0.f ? (V / M) : 0.f);
	if (!FMath::IsNearlyEqual(LastAppliedMax, M)) { LastAppliedMax = M; }
	// «SetRange + SetValue» у UMG-полосы = клип ширины плана ES1 (закон ui-progressbar-animate-existing).
	UAsdaMiniStateSubsystem::ClipFill(Bar, M > 0.f ? (V / M) : 0.f);
}

FString UAsdaCastBarSubsystem::DumpState() const
{
	FString S;
	S += TEXT("── ПОЛОСА ПРИМЕНЕНИЯ НАВЫКА (SKILL_BAR_Win 66000) ──\n");
	S += FString::Printf(TEXT("  окно: %s, контролы: %s, видимо: %s\n"),
		Win ? TEXT("создано") : TEXT("НЕТ"), bControlsReady ? TEXT("связаны") : TEXT("НЕТ"),
		bWindowShown ? TEXT("да") : TEXT("нет"));
	S += FString::Printf(TEXT("  активный навык: %d   состояние действия: %d (полоса видна при 13/14/24/27)\n"),
		ActiveSkill, CharState);
	S += FString::Printf(TEXT("  значение: %.0f мс   максимум (COMBO0): %.0f мс   полная длит. клипа: %.0f мс\n"),
		ValueMs, MaxMs, FullMs);
	S += FString::Printf(TEXT("  ширина заливки ES1: %.0f (левый край %.0f)   «за половиной»: %s\n"),
		Bar.FullW, Bar.BaseLeft, bHalfPassed ? TEXT("да") : TEXT("нет"));
	S += TEXT("  ★ полоса = прогресс АНИМАЦИИ до кадра первого удара, НЕ таймер каста;\n");
	S += TEXT("    поле CastingTime (.sif +0x2D) клиент не читает, по сети длительность не приходит.\n");
	return S;
}

// ══════════════════════════════ КОНСОЛЬ ════════════════════════════════════════════════════════

static UAsdaCastBarSubsystem* CastBarOf(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UAsdaCastBarSubsystem>() : nullptr;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCastBarDump(
	TEXT("asda.Skill.CastBar"),
	TEXT("Полоса применения навыка: состояние (окно 66000, значение, максимум)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaCastBarSubsystem* S = CastBarOf(W)) { UE_LOG(LogAsdaCastBar, Display, TEXT("%s"), *S->DumpState()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCastBarTest(
	TEXT("asda.Skill.CastBar.Test"),
	TEXT("Полоса применения: ПРИНУДИТЕЛЬНО показать — asda.Skill.CastBar.Test <skillId> [максимум, мс = 1000]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaCastBarSubsystem* S = CastBarOf(W);
		if (!S) { return; }
		const int32 Id = A.Num() > 0 ? FCString::Atoi(*A[0]) : 501;
		const float Ms = A.Num() > 1 ? FCString::Atof(*A[1]) : 1000.f;
		S->ForceShowForTest(Id, Ms);
		UE_LOG(LogAsdaCastBar, Display, TEXT("%s"), *S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCastBarOff(
	TEXT("asda.Skill.CastBar.Off"),
	TEXT("Полоса применения: погасить"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaCastBarSubsystem* S = CastBarOf(W)) { S->End(TEXT("консоль asda.Skill.CastBar.Off")); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCastBarState(
	TEXT("asda.Skill.CastBar.State"),
	TEXT("Полоса применения: задать состояние действия персонажа — asda.Skill.CastBar.State <13|14|24|27|прочее>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaCastBarSubsystem* S = CastBarOf(W);
		if (!S) { return; }
		S->SetState(A.Num() > 0 ? FCString::Atoi(*A[0]) : 24);
		UE_LOG(LogAsdaCastBar, Display, TEXT("%s"), *S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE
