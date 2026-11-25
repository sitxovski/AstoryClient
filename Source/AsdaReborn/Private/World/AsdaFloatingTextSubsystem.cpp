// Copyright (c) 2026 @sitxovski (Discord)
// Реализация UAsdaFloatingTextSubsystem — см. .h и Reports/combat_floating_text_re.md.
#include "World/AsdaFloatingTextSubsystem.h"
#include "AsdaPerf.h"   // R4, доля a: области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Resources/AsdaTextureSubsystem.h"   // ★ Аудит №3 (A8/B8): текстуры только через менеджер
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Rendering/DrawElements.h"
#include "World/AsdaWorldSubsystem.h"        // VisibleMeshBounds — единая точка «габариты по видимой геометрии»
#include "HAL/IConsoleManager.h"

#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя — каждая показанная (и не показанная) цифра
#include "Npc/AsdaMobActor.h"                // ★ 18.10.2026: журнал боя — имя и rid моба, над которым цифра
#include "World/AsdaPlayerCharacter.h"       // ★ 18.10.2026: журнал боя — имя игрока, над которым цифра
#include "World/AsdaNameplateComponent.h"    // ★ 18.10.2026: журнал боя — имя игрока с плашки

// Диагностика канала: без неё «надписи нет» неотличимо от «спавн даже не дошёл до канваса».
DEFINE_LOG_CATEGORY_STATIC(LogAsdaFloatText, Log, All);

#if ASDA_COMBAT_TRACE
// ★ Журнал боя (18.10.2026): над кем сейчас спавнится надпись (ставит `SpawnOverActor` на время вызова `SpawnStyled`;
//   только игровой поток, только для текста журнала — поведение не читает).
static const AActor* GAsdaCtFtOver = nullptr;

// ★ Журнал боя: смысл стиля надписи (таблица `StyleParams`).
static const TCHAR* AsdaCtFtStyleName(int32 Style)
{
	switch (Style)
	{
	case 0x01: return TEXT("наш урон");          case 0x02: return TEXT("наш крит");
	case 0x03: return TEXT("урон навыка");       case 0x04: return TEXT("крит навыка");
	case 0x07: return TEXT("навык, вариант");    case 0x05: return TEXT("наш MISS/IMMUNE");
	case 0x06: return TEXT("наш DODGE");         case 0x08: return TEXT("синий");
	case 0x09: return TEXT("по нам/чужой урон"); case 0x0A: return TEXT("по нам/чужой крит");
	case 0x0D: return TEXT("чужой MISS");        case 0x0E: return TEXT("чужой DODGE");
	case 0x0B: case 0x0C: case 0x0F: return TEXT("оранжевый");
	case 0x10: return TEXT("тип урона 2/3/6/7/8/16"); case 0x11: return TEXT("тип урона 4/5");
	case 0x12: return TEXT("пустой (эталон no-op)");
	case 0x13: case 0x14: return TEXT("опыт (зелёный)"); case 0x15: return TEXT("системный жёлтый");
	case 0x16: return TEXT("цифра отсчёта (экран)");
	default:   return TEXT("неразобранный");
	}
}

// ★ Журнал боя: над кем надпись — моб (rid, имя, HP актёра), игрок (имя с плашки), иное — имя актора.
static FString AsdaCtFtOver(const AActor* A)
{
	if (!A) { return TEXT("точка мира"); }
	if (const AAsdaMobActor* M = Cast<AAsdaMobActor>(A))
	{
		return FString::Printf(TEXT("моб rid=%d '%s' (HP актёра %d/%d)"), M->NetRenderId, *M->MobName, M->CurHp, M->MaxHp);
	}
	if (const AAsdaPlayerCharacter* P = Cast<AAsdaPlayerCharacter>(A))
	{
		const FString Nm = (P->Nameplate && !P->Nameplate->DisplayName.IsEmpty()) ? P->Nameplate->DisplayName : P->GetName();
		return FString::Printf(TEXT("%s '%s'"), P->IsLocalPlayerCharacter() ? TEXT("свой") : TEXT("игрок"), *Nm);
	}
	return A->GetName();
}

// ★ Журнал боя: вид текста — слово атласа или число.
static const TCHAR* AsdaCtFtTextKind(const FString& T)
{
	return (T == TEXT("MISS") || T == TEXT("DODGE") || T == TEXT("IMMUNE") || T == TEXT("EXP") || T == TEXT("POINT")) ? TEXT("слово") : TEXT("число");
}
#endif

// ── SAsdaCombatGlyphs ──────────────────────────────────────────────────────────

void SAsdaCombatGlyphs::Construct(const FArguments& InArgs)
{
	Atlas = InArgs._Atlas;
	Text  = InArgs._Text;
	// ★★★ Слова атласа. Реверс `CombatText_DrawString@0x1162630`: каждое слово рисует СВОЯ функция,
	//   и Δu вместе с шириной квада у них РАЗНЫЕ (раньше здесь у всех стояло 0.375 / 5·scale —
	//   DODGE обрезался, IMMUNE обрезался сильнее всех; проверено по телам QuadA..QuadD).
	if      (Text == TEXT("MISS"))   { bWord = true; WordUV = FVector2D(0.25, 0.875); WordUVW = 0.375f; WordQuadW = 5.f; }  // ПРОМАХ        QuadA
	else if (Text == TEXT("DODGE"))  { bWord = true; WordUV = FVector2D(0.0,  0.625); WordUVW = 0.625f; WordQuadW = 9.f; }  // УКЛОНЕНИЕ     QuadB
	else if (Text == TEXT("IMMUNE")) { bWord = true; WordUV = FVector2D(0.0,  0.5);   WordUVW = 0.75f;  WordQuadW = 9.f; }  // НЕУЯЗВИМОСТЬ  QuadC
	else if (Text == TEXT("EXP"))    { bWord = true; WordUV = FVector2D(0.0,  0.375); WordUVW = 0.57f;  WordQuadW = 9.f; }  // ОПЫТ+         QuadD
	// ★ 30.10.2026 («Битва армий», 6730 «+N POINT»): ветка `strcmp(text, "POINT")` → `combatTextDrawGlyphQuad@0x1163120`:
	//   `combatTextCharToGlyphUV('p')` = (0.5, 0.375), u до 1.0 (Δu 0.5), квад ±4.5·scale = 9·scale.
	else if (Text == TEXT("POINT"))  { bWord = true; WordUV = FVector2D(0.5,  0.375); WordUVW = 0.5f;   WordQuadW = 9.f; }
	SetCanTick(false);
}

FVector2D SAsdaCombatGlyphs::DigitUV(TCHAR C)
{
	// Реверс combatTextCharToGlyphUV@0x11632C0: цифра i → ((i%8)·0.125, 0.75+(i/8)·0.125); '1' u=0.12625.
	if (C < '0' || C > '9') { return FVector2D(0.0, 0.75); }
	const int32 I = C - '0';
	if (I == 1) { return FVector2D(0.12625, 0.75); }
	return FVector2D((I % 8) * 0.125, 0.75 + (I / 8) * 0.125);
}

FVector2D SAsdaCombatGlyphs::ComputeDesiredSize(float) const
{
	// Эталон: глиф 2·scale, шаг 2·scale; слово WordQuadW·scale × 2·scale (ширина своя у каждого).
	if (bWord) { return FVector2D(WordQuadW * Scale, 2.f * Scale); }
	return FVector2D(2.f * Scale * FMath::Max(1, Text.Len()), 2.f * Scale);
}

int32 SAsdaCombatGlyphs::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	UTexture2D* Tex = Atlas.Get();
	if (!Tex || Color.A <= 0.f) { return LayerId; }
	const int32 NGlyphs = bWord ? 1 : Text.Len();
	Brushes.SetNum(NGlyphs);
	const float W = 2.f * Scale, H = 2.f * Scale;
	for (int32 i = 0; i < NGlyphs; ++i)
	{
		FSlateBrush& B = Brushes[i];
		B.SetResourceObject(Tex);
		B.DrawAs = ESlateBrushDrawType::Image;
		if (bWord)
		{
			B.SetUVRegion(FBox2D(WordUV, WordUV + FVector2D(WordUVW, 0.125)));   // Δu своё у каждого слова
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
				AllottedGeometry.ToPaintGeometry(FVector2D(WordQuadW * Scale, H), FSlateLayoutTransform(FVector2D(0.f, 0.f))),
				&B, ESlateDrawEffect::None, Color);
		}
		else
		{
			const FVector2D UV = DigitUV(Text[i]);
			B.SetUVRegion(FBox2D(UV, UV + FVector2D(0.125, 0.125)));
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
				AllottedGeometry.ToPaintGeometry(FVector2D(W, H), FSlateLayoutTransform(FVector2D(W * i, 0.f))),
				&B, ESlateDrawEffect::None, Color);
		}
	}
	return LayerId + 1;
}

// ── Подсистема ─────────────────────────────────────────────────────────────────

// Цвета стилей оригинала (реверс gameMsgTableCtor@0x9A7910; float 0.96=0xF4 и т.д.).
bool UAsdaFloatingTextSubsystem::StyleParams(int32 Style, FLinearColor& OutColor, bool& bOutTrack1, float& OutScaleMul)
{
	bOutTrack1 = false; OutScaleMul = 1.f;
	switch (Style)
	{
	case 0x01: OutColor = FLinearColor(0.96f, 0.96f, 0.96f); return true;                       // наш урон (бел.)
	case 0x02: OutColor = FLinearColor(0.96f, 0.96f, 0.96f); bOutTrack1 = true; return true;    // наш крит
	// ★★★ 3 / 4 / 7 — ЦИФРЫ УРОНА НАВЫКА (были НЕ РАЗОБРАНЫ и молча гасились: `default: return false`,
	//   поэтому урон навыка не рисовался бы даже после подключения канала).
	//   Источник: таблица методов `gameMsgTableCtor@0x9A7910` (game_main.c:6756), слот = 21 + N;
	//   цвет — поля ноды [78]=R [79]=G [80]=B [81]=A (`combatTextNode1000Draw@0x9A6210` собирает
	//   ARGB как A<<24|R<<16|G<<8|B); трек задаёт КЛАСС ноды (её vtable +4 = Init):
	//     N=3 → `combatTextSpawnNodeScaled@0x9A9500`,  vtable off_13A2624 → `floatingTextInit@0x9A5FB0`
	//           (пара Update/Draw «1000») ⇒ трек 0; RGB = 0x3F19999A/0x3F76C16F/0x3F76C16F.
	//     N=4 → `combatTextSpawnNodeVariant_9620@0x9A9620`, vtable off_13A266C →
	//           `floatingTextInit_9A6C50@0x9A6C50` (пара `combatTextNode1200UpdateA/DrawA`) ⇒ ТРЕК 1
	//           (1200 мс, крупный) — это КРИТ навыка; RGB = 0.6 / 0.9639 / 1.0.
	//     N=7 → `combatTextSpawnNodeVariant@0x9A99E0`, тот же цвет, что у 4, но трек 0.
	case 0x03: OutColor = FLinearColor(0.6f, 0.9639f, 0.9639f); return true;                    // урон НАВЫКА
	case 0x04: OutColor = FLinearColor(0.6f, 0.9639f, 1.f); bOutTrack1 = true; return true;     // КРИТ навыка
	case 0x07: OutColor = FLinearColor(0.6f, 0.9639f, 1.f); return true;                        // вариант трека 0
	case 0x05:                                                                                   // MISS/IMMUNE (наш)
	case 0x06: OutColor = FLinearColor(0.96f, 0.96f, 0.96f); OutScaleMul = 0.7f; return true;   // DODGE (наш)
	case 0x08: OutColor = FLinearColor(0.f, 0.31f, 1.f); return true;                            // синий
	case 0x09: OutColor = FLinearColor(1.f, 0.f, 0.11f); return true;                            // чужой/по нам урон
	case 0x0A: OutColor = FLinearColor(1.f, 0.f, 0.11f); bOutTrack1 = true; return true;         // чужой/по нам крит
	case 0x0D:                                                                                   // MISS (чужой)
	case 0x0E: OutColor = FLinearColor(1.f, 0.f, 0.11f); return true;                            // DODGE (чужой)
	case 0x0B:
	case 0x0C:
	case 0x0F: OutColor = FLinearColor(1.f, 0.35f, 0.f); return true;                            // оранж-вариант
	case 0x10: OutColor = FLinearColor(1.f, 0.68f, 0.f); return true;                            // dmgType 2/3/6/7/8/16
	case 0x11: OutColor = FLinearColor(0.8f, 0.f, 0.37f); return true;                           // dmgType 4/5
	case 0x12: return false;                                                                     // ПУСТОЙ (эталон no-op)
	// ★★★ 0x13 / 0x14 / 0x15 — СИСТЕМНЫЕ НАДПИСИ (прибавка опыта). Раньше падали в `default` и
	//   молча гасились, поэтому весь показ получения опыта был невидим.
	//   Слот таблицы = 21 + N (`gameMsgTableCtor@0x9A7910`):
	//     0x13 → `combatTextNode_spawnDefault@0x9A9020`   поля ноды [78]=0 [79]=1.0 [80]=0  ⇒ ЗЕЛЁНЫЙ
	//     0x14 → `combatTextNode_spawnZeroColor@0x9A9140` те же значения                    ⇒ ЗЕЛЁНЫЙ
	//     0x15 → `combatTextNode_spawnAtPos@0x9A8F00`     [78]=1.0 [79]=1.0 [80]=0          ⇒ ЖЁЛТЫЙ
	//   Класс ноды у всех трёх один: vtable 0x13A263C → Init `floatingTextInit_9A6420@0x9A6420`,
	//   который берёт кейфрейм-трек `dword_29078CC` — это ВТОРОЙ трек `animBuildDefaultKeyframeTracks`
	//   (10→30→30→17, 1200 мс, подъём −200) ⇒ тот же `bTrack1`, что у критов.
	//   ★ Константа 3000 в паре `combatTextNode3000Update/Draw` — СТОП-значение таймера после
	//   исчерпания кейфреймов, а НЕ длительность (прежняя запись «3000 мс» в отчётах — ошибка).
	case 0x13:
	case 0x14: OutColor = FLinearColor(0.f, 1.f, 0.f); bOutTrack1 = true; return true;            // прибавка опыта
	case 0x15: OutColor = FLinearColor(1.f, 1.f, 0.f); bOutTrack1 = true; return true;            // системный жёлтый
	// ★ 19.10.2026: 0x16 → `poolCreateScaleNode@0x9AA660`, класс VariantB (vtable 0x13A2654): [78]=[79]=[80]=1.0 ⇒ ЧИСТЫЙ
	//   белый (не 0.96); трек свой — неподвижный (см. `bStatic` в `Tick`), жизнь 1000 мс (`0x9A6B60`: готов при t ≥ 1000).
	case 0x16: OutColor = FLinearColor(1.f, 1.f, 1.f); return true;                              // цифра отсчёта 6145
	default:   return false;
	}
}

void UAsdaFloatingTextSubsystem::EnsureCanvas()
{
	if (!Atlas)
	{
		// ★ Аудит №3 (A8/B8): текстура — только через менеджер (кэш + refCount + GC-рут),
		//   закон [[resource-subsystems]]. Прямой `LoadObject` мимо кэша убран.
		if (UAsdaTextureSubsystem* TexMgr = AsdaTexMgr())
		{
			Atlas = TexMgr->LoadTextureCached(TEXT("/Game/AsdaAssets/UI/T_AsdaCombatFont"));
		}
	}
	if (Canvas.IsValid()) { return; }
	UGameViewportClient* VP = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr;
	if (!VP) { return; }
	SAssignNew(Canvas, SConstraintCanvas);
	VP->AddViewportWidgetContent(Canvas.ToSharedRef(), /*ZOrder=*/90);   // над миром, под модалками
}

void UAsdaFloatingTextSubsystem::Deinitialize()
{
	if (Canvas.IsValid())
	{
		if (UGameViewportClient* VP = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr)
		{
			VP->RemoveViewportWidgetContent(Canvas.ToSharedRef());
		}
		Canvas.Reset();
	}
	Entries.Empty();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaFloatingTextSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Цифры урона висят в оверлее вьюпорта и считают экранные координаты от акторов мира.
	Entries.Empty();
	(void)Reason;
}

void UAsdaFloatingTextSubsystem::SpawnOverActor(int32 Style, const FString& Text, const AActor* Over,
                                                const FVector2D& ScreenOffset, const FVector2D& ScreenAlign)
{
	if (!Over)
	{
		UE_LOG(LogAsdaFloatText, Verbose, TEXT("[всплывашка] стиль 0x%X «%s»: цели нет — спавна нет"), Style, *Text);
		ASDA_CTRACE(EAsdaCT::DmgText, TEXT("«%s» (%s, стиль 0x%X %s) НЕ показана: актёра-цели нет"),
			*Text, AsdaCtFtTextKind(Text), Style, AsdaCtFtStyleName(Style));
		return;
	}
	// ★★★ ЯКОРЬ = ЦЕНТР ВИДИМОЙ ГЕОМЕТРИИ, А НЕ `GetActorBounds`.
	//   Эталон берёт `Entity_GetScreenPos(..., soundPosX/Y/Z)` — это ЦЕНТР ТЕЛА сущности
	//   (та же точка, от которой считается 3D-звук). Порт брал `GetActorBounds(false)`, то есть
	//   объединение габаритов ВСЕХ зарегистрированных компонентов. У мобов и NPC разница мелкая
	//   (капсула пикинга, плашка имени), а у СВОЕГО персонажа она катастрофическая:
	//   `AAsdaPlayerCharacter` держит `USpringArmComponent` (700…1270 юнитов) и `UCameraComponent`
	//   на его конце, поэтому «центр» уезжал на ~350 юнитов ЗА спину и вверх — надпись «ОПЫТ+»
	//   при сдаче задания рисовалась не над персонажем, а в стороне (и легко уходила под
	//   центрированный попап наград 540). Берём единую точку `VisibleMeshBounds` — ту же, по
	//   которой считается подошва кольца выбора.
	FVector Origin, Extent;
	if (!UAsdaWorldSubsystem::VisibleMeshBounds(Over, Origin, Extent))
	{
		// Видимых мешей нет (модель ещё не собрана) — честно падаем на прежнее поведение и говорим об этом.
		Over->GetActorBounds(/*bOnlyCollidingComponents=*/false, Origin, Extent);
		UE_LOG(LogAsdaFloatText, Verbose,
			TEXT("[всплывашка] у «%s» видимой геометрии нет — якорь взят по GetActorBounds"), *GetNameSafe(Over));
	}
#if ASDA_COMBAT_TRACE
	GAsdaCtFtOver = Over;     // журнал боя: строка `SpawnStyled` назовёт, над кем цифра
#endif
	SpawnStyled(Style, Text, Origin, ScreenOffset, ScreenAlign);
#if ASDA_COMBAT_TRACE
	GAsdaCtFtOver = nullptr;
#endif
}

void UAsdaFloatingTextSubsystem::SpawnStyled(int32 Style, const FString& Text, const FVector& WorldPos,
                                             const FVector2D& ScreenOffset, const FVector2D& ScreenAlign)
{
	// ★★★ КАЖДЫЙ ОТКАЗ ТЕПЕРЬ ГОВОРИТ ПРИЧИНУ. Раньше здесь стояли ЧЕТЫРЕ немых `return`, и
	//   «надписи нет» выглядело в логе как «показ прошёл» (вызывающий печатал успех ДО того, как
	//   канал молча всё выбрасывал). Именно так пряталась жалоба «сдал квест — надписи не увидел»:
	//   в логе владельца стояло «[EXP] +1395 … показ над AsdaPlayerCharacter_0», а на экране пусто.
	// ★★★ 17.10.2026 (Ф5) [ОРИГИНАЛ]: общий гейт `FloatingText_Spawn@0x9A8CF0` (entity_char.c:3209–3216) — при режиме показа
	//   цифр 2 НИ ОДНА всплывающая надпись со стилем ≤ 0x16 не рисуется (цифры урона, «опыт», дельты HP). Режим — одна точка
	//   `UAsdaWorldSubsystem::GetDamageTextMode` (профиль `DamageTextMode`, разбор у поля).
	if (Style >= 0 && Style <= 0x16 && UAsdaWorldSubsystem::GetDamageTextMode() == 2)
	{
		UE_LOG(LogAsdaFloatText, Verbose, TEXT("[всплывашка] «%s» (стиль 0x%X) не рисуется: режим показа цифр 2"), *Text, Style);
		ASDA_CTRACE(EAsdaCT::DmgText, TEXT("«%s» (%s, стиль 0x%X %s) над %s НЕ показана: режим показа цифр 2 (эталонный гейт)"),
			*Text, AsdaCtFtTextKind(Text), Style, AsdaCtFtStyleName(Style), *AsdaCtFtOver(GAsdaCtFtOver));
		return;
	}
	FLinearColor Color; bool bTrack1; float ScaleMul;
	if (!StyleParams(Style, Color, bTrack1, ScaleMul))
	{
		// 0x12 — эталонный пустой стиль (no-op), остальное — дыра в таблице стилей.
		UE_LOG(LogAsdaFloatText, Verbose, TEXT("[всплывашка] стиль 0x%X не разобран — «%s» не рисуется"), Style, *Text);
		ASDA_CTRACE(EAsdaCT::DmgText, TEXT("«%s» (%s, стиль 0x%X %s) над %s НЕ показана: стиль не рисуется"),
			*Text, AsdaCtFtTextKind(Text), Style, AsdaCtFtStyleName(Style), *AsdaCtFtOver(GAsdaCtFtOver));
		return;
	}
	APlayerController* PC = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
	if (!PC)
	{
		UE_LOG(LogAsdaFloatText, Warning, TEXT("[всплывашка] «%s»: нет локального контроллера — проекции нет"), *Text);
		ASDA_CTRACE(EAsdaCT::DmgText, TEXT("«%s» (стиль 0x%X %s) над %s НЕ показана: нет локального контроллера"),
			*Text, Style, AsdaCtFtStyleName(Style), *AsdaCtFtOver(GAsdaCtFtOver));
		return;
	}
	FVector2D Screen;
	if (!PC->ProjectWorldLocationToScreen(WorldPos, Screen, /*bPlayerViewportRelative=*/true))
	{
		UE_LOG(LogAsdaFloatText, Warning,
			TEXT("[всплывашка] «%s»: точка (%.0f,%.0f,%.0f) НЕ проецируется в экран (за камерой) — надписи не будет"),
			*Text, WorldPos.X, WorldPos.Y, WorldPos.Z);
		ASDA_CTRACE(EAsdaCT::DmgText, TEXT("«%s» (стиль 0x%X %s) над %s НЕ показана: точка за камерой"),
			*Text, Style, AsdaCtFtStyleName(Style), *AsdaCtFtOver(GAsdaCtFtOver));
		return;
	}
	Screen += ScreenOffset;   // эталонные пиксельные оффсеты вызывающего (у цифр урона — нулевые)
	EnsureCanvas();
	if (!Canvas.IsValid() || !Atlas)
	{
		UE_LOG(LogAsdaFloatText, Warning,
			TEXT("[всплывашка] «%s»: канвас=%s атлас=%s — рисовать не во что"),
			*Text, Canvas.IsValid() ? TEXT("есть") : TEXT("НЕТ"), Atlas ? TEXT("есть") : TEXT("НЕТ"));
		ASDA_CTRACE(EAsdaCT::DmgText, TEXT("«%s» (стиль 0x%X %s) над %s НЕ показана: канвас=%s атлас=%s"),
			*Text, Style, AsdaCtFtStyleName(Style), *AsdaCtFtOver(GAsdaCtFtOver),
			Canvas.IsValid() ? TEXT("есть") : TEXT("НЕТ"), Atlas ? TEXT("есть") : TEXT("НЕТ"));
		return;
	}
	UE_LOG(LogAsdaFloatText, Verbose,
		TEXT("[всплывашка] «%s» стиль 0x%X: мир (%.0f,%.0f,%.0f) → экран (%.0f,%.0f), якорь (%.2f,%.2f)"),
		*Text, Style, WorldPos.X, WorldPos.Y, WorldPos.Z, Screen.X, Screen.Y, ScreenAlign.X, ScreenAlign.Y);

	FEntry E;
	E.Pos0 = Screen;
	E.LifeMs = bTrack1 ? 1200.f : 1000.f;
	E.bTrack1 = bTrack1;
	E.ScaleMul = ScaleMul;
	E.Color = Color;
	E.Widget = SNew(SAsdaCombatGlyphs).Atlas(Atlas).Text(Text);
	E.Widget->SetGlyphState(10.f * ScaleMul, Color);
	Canvas->AddSlot()
		.Anchors(FAnchors(0.f, 0.f))
		.Offset(FMargin(Screen.X, Screen.Y, 0.f, 0.f))
		.AutoSize(true)
		.Alignment(ScreenAlign)
		[ E.Widget.ToSharedRef() ];
	ASDA_CTRACE(EAsdaCT::DmgText, TEXT("ПОКАЗАНА «%s» (%s) стиль 0x%X %s, цвет (%.2f,%.2f,%.2f), трек %d (%.0f мс, масштаб ×%.1f), над %s, экран (%.0f,%.0f), живых надписей %d"),
		*Text, AsdaCtFtTextKind(Text), Style, AsdaCtFtStyleName(Style), Color.R, Color.G, Color.B, bTrack1 ? 1 : 0, E.LifeMs, ScaleMul,
		*AsdaCtFtOver(GAsdaCtFtOver), Screen.X, Screen.Y, Entries.Num() + 1);
	Entries.Add(MoveTemp(E));
}

void UAsdaFloatingTextSubsystem::SpawnScreen(int32 Style, const FString& Text, const FVector2D& ScreenPos, const FVector2D& ScreenAlign)
{
	if (Style >= 0 && Style <= 0x16 && UAsdaWorldSubsystem::GetDamageTextMode() == 2)
	{
		UE_LOG(LogAsdaFloatText, Verbose, TEXT("[всплывашка] «%s» (стиль 0x%X, экран) не рисуется: режим показа цифр 2"), *Text, Style);
		return;
	}
	FLinearColor Color; bool bTrack1; float ScaleMul;
	if (!StyleParams(Style, Color, bTrack1, ScaleMul))
	{
		UE_LOG(LogAsdaFloatText, Verbose, TEXT("[всплывашка] стиль 0x%X не разобран — «%s» (экран) не рисуется"), Style, *Text);
		return;
	}
	EnsureCanvas();
	if (!Canvas.IsValid() || !Atlas)
	{
		UE_LOG(LogAsdaFloatText, Warning, TEXT("[всплывашка] «%s» (экран): канвас=%s атлас=%s — рисовать не во что"),
			*Text, Canvas.IsValid() ? TEXT("есть") : TEXT("НЕТ"), Atlas ? TEXT("есть") : TEXT("НЕТ"));
		return;
	}
	FEntry E;
	E.Pos0 = ScreenPos;
	E.bStatic = (Style == 0x16);
	E.LifeMs = E.bStatic ? 1000.f : (bTrack1 ? 1200.f : 1000.f);
	E.bTrack1 = bTrack1 && !E.bStatic;
	E.ScaleMul = ScaleMul;
	E.Color = Color;
	E.Widget = SNew(SAsdaCombatGlyphs).Atlas(Atlas).Text(Text);
	E.Widget->SetGlyphState(E.bStatic ? 0.f : 10.f * ScaleMul, Color);   // у 0x16 масштаб 0 до первого ключа (10 мс)
	Canvas->AddSlot()
		.Anchors(FAnchors(0.f, 0.f))
		.Offset(FMargin(ScreenPos.X, ScreenPos.Y, 0.f, 0.f))
		.AutoSize(true)
		.Alignment(ScreenAlign)
		[ E.Widget.ToSharedRef() ];
	UE_LOG(LogAsdaFloatText, Verbose, TEXT("[всплывашка] «%s» стиль 0x%X: экран (%.0f,%.0f), якорь (%.2f,%.2f)"),
		*Text, Style, ScreenPos.X, ScreenPos.Y, ScreenAlign.X, ScreenAlign.Y);
	Entries.Add(MoveTemp(E));
}

void UAsdaFloatingTextSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.FloatingText");   // R4, доля a: область замера (AsdaPerf.h; в Shipping пусто)
	if (Entries.Num() == 0 || !Canvas.IsValid()) { return; }
	const float DtMs = DeltaTime * 1000.f;
	for (int32 i = Entries.Num() - 1; i >= 0; --i)
	{
		FEntry& E = Entries[i];
		E.T += DtMs;
		if (E.T >= E.LifeMs || !E.Widget.IsValid())
		{
			if (E.Widget.IsValid()) { Canvas->RemoveSlot(E.Widget.ToSharedRef()); }
			Entries.RemoveAt(i);
			continue;
		}
		// ── Кривая эталона (треки @0x9A7D00) ──
		// Трек 0: (10мс,10)→(50,20)→(80,20)→(100,11)→(1000, dy=-200, alpha→0)
		// Трек 1: (10,10)→(50,30)→(90,30)→(110,17)→(1200, dy=-200, alpha→0)
		if (E.bStatic)
		{
			// ★ 0x16 (`0x9A6980` Update): на каждом ключе [76]=[72]=30; позиция и альфа не меняются ⇒ 0 до 10 мс, потом 30.
			E.Widget->SetGlyphState(E.T < 10.f ? 0.f : 30.f * E.ScaleMul, E.Color);
			continue;
		}
		const float PeakS  = E.bTrack1 ? 30.f : 20.f;
		const float HoldT  = E.bTrack1 ? 90.f : 80.f;
		const float ShrT   = E.bTrack1 ? 110.f : 100.f;
		const float FinalS = E.bTrack1 ? 17.f : 11.f;
		float Scale, Rise = 0.f, Alpha = 1.f;
		if      (E.T < 10.f)  { Scale = 10.f; }
		else if (E.T < 50.f)  { Scale = FMath::Lerp(10.f, PeakS, (E.T - 10.f) / 40.f); }
		else if (E.T < HoldT) { Scale = PeakS; }
		else if (E.T < ShrT)  { Scale = FMath::Lerp(PeakS, FinalS, (E.T - HoldT) / (ShrT - HoldT)); }
		else
		{
			Scale = FinalS;
			const float K = (E.T - ShrT) / (E.LifeMs - ShrT);   // 0..1 последний сегмент
			Rise  = -200.f * K;                                  // подъём строго вверх на 200px
			Alpha = 1.f - K;                                     // линейное затухание (только альфа)
		}
		Scale *= E.ScaleMul;
		FLinearColor C = E.Color; C.A = Alpha;
		E.Widget->SetGlyphState(Scale, C);
		E.Widget->SetRenderTransform(FSlateRenderTransform(FVector2D(0.f, Rise)));
	}
}

// ★ ЖИВОЙ ДАМП КАНАЛА: сколько надписей живёт прямо сейчас, где они на экране и в каком кадре
//   анимации. Нужен, потому что всплывашка — Slate-виджет в канвасе вьюпорта: офлайн-дамп ассетов
//   её не видит, а по одному кадру-снимку нельзя отличить «не спавнилось» от «спавнилось, но за
//   окном/за экраном» (закон `false-negatives-method` — доказываем вторым методом).
//   Печатает ещё и z-order канваса: всплывающий текст эталона рисуется ДО окон
//   (`animTimeline_UpdateTracks(g_pFloatingText, …)` идёт в кадре раньше `WindowMgr_RenderControls`),
//   поэтому любое окно поверх него — поведение ОРИГИНАЛА, а не наша ошибка.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GAsdaFxFloatDump(
	TEXT("asda.Fx.FloatDump"),
	TEXT("Дамп живых всплывающих надписей (позиция, масштаб, время жизни)"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		UAsdaFloatingTextSubsystem* FT = W && W->GetGameInstance()
			? W->GetGameInstance()->GetSubsystem<UAsdaFloatingTextSubsystem>() : nullptr;
		if (!FT) { UE_LOG(LogAsdaFloatText, Display, TEXT("[всплывашка] подсистемы нет")); return; }
		FT->DumpLive();
	}));
#endif // ASDA_DEV_CONSOLE

void UAsdaFloatingTextSubsystem::DumpLive() const
{
	FVector2D VpSize = FVector2D::ZeroVector;
	if (UGameViewportClient* VP = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr)
	{
		VP->GetViewportSize(VpSize);
	}
	UE_LOG(LogAsdaFloatText, Display,
		TEXT("[всплывашка-дамп] канвас=%s (z=90, эталон рисует текст ДО окон), атлас=%s, вьюпорт %.0f×%.0f, живых надписей %d"),
		Canvas.IsValid() ? TEXT("есть") : TEXT("НЕТ"), Atlas ? TEXT("есть") : TEXT("НЕТ"),
		VpSize.X, VpSize.Y, Entries.Num());
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		const FEntry& E = Entries[i];
		UE_LOG(LogAsdaFloatText, Display,
			TEXT("[всплывашка-дамп] #%d: экран (%.0f,%.0f) t=%.0f/%.0f мс трек%d цвет(%.2f,%.2f,%.2f,%.2f) виджет=%s"),
			i, E.Pos0.X, E.Pos0.Y, E.T, E.LifeMs, E.bTrack1 ? 1 : 0,
			E.Color.R, E.Color.G, E.Color.B, E.Color.A, E.Widget.IsValid() ? TEXT("жив") : TEXT("НЕТ"));
	}
}
