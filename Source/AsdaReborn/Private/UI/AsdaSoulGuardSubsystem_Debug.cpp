// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSoulGuardSubsystem_Debug.cpp — ★ R3-c 12.11.2026 (разрез AsdaSoulGuardSubsystem.cpp, дословный перенос): симуляция, дампы, дев-команды GSoul*.
//   Общее для частей — в AsdaSoulGuardSubsystem_Private.h; шапка и реверс — в AsdaSoulGuardSubsystem.cpp.
#include "UI/AsdaSoulGuardSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ U13 (В1 дедупа №2): AsdaNotice::ConsoleEcho — эхо консольных команд
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaUiHelpers.h"   // ★ Аудит №4 (B1): AsdaUi::SlotOf/SlotRect — общая точка слота канваса

#include "UI/AsdaMiniStateSubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"    // ★ ширина окна мини-карты для авто-якоря (эталон GetSize(g_pMiniMapWnd))
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaSkillUseSubsystem.h"   // ★ ОБЩИЙ диспетчер применения: ряды Хранителя ходят через него
#include "UI/AsdaSkillDatabase.h"       // ★ Аудит №3 (A13): имя иконки навыка — только отсюда
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaAnimatedPlane.h"
#include "Items/AsdaItemTableSubsystem.h" // ★ Аудит №3 (A3): SkillCoin и skilltooltip — только через менеджер
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldSubsystem.h"   // ★ 3D-компаньон хранителя: наличие → высота плашки, каст → мотион
// ★ цель-МОБ как точка вылета монеты (FindNetMobByRid отдаёт AAsdaMobActor*)   ★ R3: комментарий снятого включения "Npc/AsdaMobActor.h" (файл им не пользовался) — сохранён
#include "AsdaNetSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "AsdaSoulGuardSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSoulGuardSubsystem*.cpp

// ─────────────────────────── эмулятор сервера (для проверки без сервера) ────────────────────────
// Сервер `sub_48AFF0` (GameServer.c:55620) построчно.
void UAsdaSoulGuardSubsystem::SimSkillUsed(int32 SkillId)
{
	if (SkillId < CoinSkillIdMin || SkillId >= CoinSkillIdMax)
	{
		UE_LOG(LogAsdaSoul, Log, TEXT("[эмулятор] скилл %d вне диапазона [%d..%d) — монет не даёт (сервер)"),
			SkillId, CoinSkillIdMin, CoinSkillIdMax);
		return;
	}
	int32 K[3] = { 0, 0, 0 };
	GetSkillCoins(SkillId, K[0], K[1], K[2]);
	bool bChanged = false;
	int32 NewCoin[3] = { Coin[0], Coin[1], Coin[2] };
	for (int32 i = 0; i < 3; ++i)
	{
		if (K[i] <= 0) { continue; }
		if (i < SimJobCount) { NewCoin[i] = FMath::Min(NewCoin[i] + K[i], CoinCap); bChanged = true; }
		else                 { NewCoin[i] = 0; }
	}
	if (SkillType == 0) { LoadRowSkills(SimSkillType); bChanged = true; }
	NextSimDecayS = FPlatformTime::Seconds() + (double)DecayPeriodMs / 1000.0;   // сервер: +15000 мс
	if (!bChanged) { return; }
	UE_LOG(LogAsdaSoul, Log, TEXT("[эмулятор] [COIN] iAddFlag:1, скилл %d (+%d/+%d/+%d) → %d/%d/%d"),
		SkillId, K[0], K[1], K[2], NewCoin[0], NewCoin[1], NewCoin[2]);
	OnSlotState6157(SelfUserIdx(), 1, SkillType != 0 ? SkillType : SimSkillType, SkillId,
		NewCoin[0], NewCoin[1], NewCoin[2]);
}

// Сервер `sub_48B4C0` → `sub_48B570(uid, 3, 1)`: по −1 с каждого ненулевого ряда.
void UAsdaSoulGuardSubsystem::SimDecayTick()
{
	bool bChanged = false;
	int32 NewCoin[3] = { Coin[0], Coin[1], Coin[2] };
	for (int32 i = 0; i < 3; ++i)
	{
		if (NewCoin[i] > 0) { NewCoin[i] = FMath::Max(0, NewCoin[i] - 1); bChanged = true; }
	}
	if (!bChanged) { return; }
	UE_LOG(LogAsdaSoul, Log, TEXT("[эмулятор] [COIN] AddFlag:0 (распад −1/%d мс) → %d/%d/%d"),
		DecayPeriodMs, NewCoin[0], NewCoin[1], NewCoin[2]);
	OnSlotState6157(SelfUserIdx(), 0, SkillType, -1, NewCoin[0], NewCoin[1], NewCoin[2]);
}

// Сервер `sub_48B570(uid, row, amount)`: списание с конкретного ряда (при использовании скилла — 5).
void UAsdaSoulGuardSubsystem::SimSpend(int32 Row, int32 Amount)
{
	if (Row < 0 || Row > 2) { return; }
	if (Coin[Row] <= 0) { return; }
	int32 NewCoin[3] = { Coin[0], Coin[1], Coin[2] };
	NewCoin[Row] = FMath::Max(0, NewCoin[Row] - Amount);
	UE_LOG(LogAsdaSoul, Log, TEXT("[эмулятор] [COIN] AddFlag:0 (списание −%d с ряда %d) → %d/%d/%d"),
		Amount, Row + 1, NewCoin[0], NewCoin[1], NewCoin[2]);
	OnSlotState6157(SelfUserIdx(), 0, SkillType, -1, NewCoin[0], NewCoin[1], NewCoin[2]);
}

// Сервер `sub_48DD60`: обнуление блока + 6157 (AddFlag 0, SkillType 0, skill −1) + 6156 (index −1).
void UAsdaSoulGuardSubsystem::SimDeath()
{
	UE_LOG(LogAsdaSoul, Log, TEXT("[эмулятор] [DEAD] Coin Reset..!! + SoulGuarder Release..!!"));
	OnSlotState6157(SelfUserIdx(), 0, 0, -1, 0, 0, 0);
	SimRecomputeGuard();
	OnSoulGuardIndex6156(SelfUserIdx(), -1);
}

// Сервер `sub_48B210` (GameServer.c:55687) построчно + `sub_4920C0` (наложение/снятие абнормала).
//   Уровень = наибольший ряд i, начиная с 0, у которого монет >= порога 8. Запись хранителя
//   ищется по паре (RowIndex == уровень, SkillType == тип). Пассивный бафф — BuffSkillId (1201..1227),
//   у него `SoulGuarderSkillTable[+83] = 0` (бессрочный), а `id >= 1201` заставляет клиент
//   НЕ рисовать ему таймер — ровно то, что уже умеет ЭТАП 2.
void UAsdaSoulGuardSubsystem::SimRecomputeGuard()
{
	const int32 Lvl = GetGuardLevel();
	const int32 PrevIdx = SoulGuardIndex;
	const FAsdaSoulGuarderRow* PrevRow = FindGuarderByIndex(PrevIdx);
	const int32 EffType = (SkillType != 0) ? SkillType : SimSkillType;
	const FAsdaSoulGuarderRow* NewRow = (Lvl >= 0) ? FindGuarderByTypeRow(EffType, Lvl) : nullptr;
	const int32 NewIdx = NewRow ? NewRow->SoulGuardIndex : -1;
	if (NewIdx == PrevIdx) { return; }

	UAsdaEffectsSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	const int32 Self = SelfUserIdx();
	if (Fx)
	{
		if (PrevRow) { Fx->RemoveBySkillState(Self, PrevRow->BuffSkillId, 0); }
		if (NewRow)
		{
			// Тем же конструктором, каким кладёт скилловый бафф опкод 5272 (type 0):
			//   IdFlag = id баффа (>= 1201 ⇒ таймер скрыт), kind 4 = ПОСТОЯННЫЙ (тик не уменьшает).
			Fx->AddOrUpdateSkill(Self, NewRow->BuffSkillId, NewRow->BuffSkillId, /*stack*/1,
				/*kind*/4, /*remainSec*/0, /*byte9*/99);
		}
	}
	UE_LOG(LogAsdaSoul, Log, TEXT("[эмулятор] [CHANGE] SoulGuardIndex:%d (уровень %d, тип %d) — бафф %d %s"),
		NewIdx, Lvl, EffType, NewRow ? NewRow->BuffSkillId : (PrevRow ? PrevRow->BuffSkillId : -1),
		NewRow ? TEXT("НАЛОЖЕН") : TEXT("СНЯТ"));
	OnSoulGuardIndex6156(Self, NewIdx);
}

// ─────────────────────────────── дампы ──────────────────────────────────────────────────────────
FString UAsdaSoulGuardSubsystem::DumpState() const
{
	FString S = TEXT("══ ХРАНИТЕЛЬ (Soul Guardian) ══\n");
	S += FString::Printf(TEXT("  окно 56000: %s | чекбокс «закрепить»: %s | контролы: %s\n"),
		bWindowShown ? TEXT("ПОКАЗАНО") : TEXT("скрыто"),
		bPinned ? TEXT("взведён") : TEXT("снят"),
		bControlsReady ? TEXT("разобраны") : TEXT("НЕ разобраны"));
	S += FString::Printf(TEXT("  SkillType=%d  SoulGuardIndex=%d  уровень хранителя=%d (порог %d)\n"),
		SkillType, SoulGuardIndex, GetGuardLevel(), GuardLevelThreshold);
	if (const FAsdaSoulGuarderRow* R = FindGuarderByIndex(SoulGuardIndex))
	{
		S += FString::Printf(TEXT("  хранитель: «%s» ряд %d, пассивный бафф id=%d (>=1201 ⇒ таймер не рисуется), скилл=%d\n"),
			*R->Name, R->RowIndex + 1, R->BuffSkillId, R->GuardianSkillId);
	}
	for (int32 i = 0; i < 3; ++i)
	{
		S += FString::Printf(TEXT("  ряд %d: монет %d (было %d) | доступен: %s (порог %d) | скилл %d | SOULBAR(>=%d): %s\n"),
			i + 1, GetCoin(i), PrevCoin.IsValidIndex(i) ? PrevCoin[i] : 0,
			IsRowAvailable(i) ? TEXT("ДА") : TEXT("нет"), AvailableThreshold,
			GetRowSkillCode(i), GuardLevelThreshold,
			GetCoin(i) >= GuardLevelThreshold ? TEXT("виден") : TEXT("скрыт"));
	}
	S += FString::Printf(TEXT("  монет в полёте: %d | цель полёта: (%.0f, %.0f, %.0f)\n"),
		FlyingCoins.Num(), CoinTargetWorld.X, CoinTargetWorld.Y, CoinTargetWorld.Z);
	S += FString::Printf(TEXT("  плашка: альфа %.0f, фаза %s, активный BURST-слот %d\n"),
		BannerAlpha, bBannerFadeOut ? TEXT("гаснет") : TEXT("проявляется"), ActiveBurstSlot);
	S += FString::Printf(TEXT("  эмулятор сервера: %s (грейд рядов %d, тип %d, распад каждые %d мс)\n"),
		bSimulateServer ? TEXT("ВКЛ") : TEXT("выкл"), SimJobCount, SimSkillType, DecayPeriodMs);
	// ── ГЕЙТЫ: у каждого честно указан источник и РЕЗУЛЬТАТ (правило «нет данных → не блокируем») ──
	S += FString::Printf(TEXT("  ГЕЙТ тип карты (Net_GetCurrentMapType): %d — %s\n"), CurrentMapType,
		CurrentMapType < 0 ? TEXT("НЕТ ДАННЫХ, гейт ПРОПУЩЕН (ничего не запрещает)")
		                   : (BannedMapTypes.Contains(CurrentMapType) ? TEXT("ЗАПРЕЩАЕТ скилл") : TEXT("разрешает")));
	S += FString::Printf(TEXT("  ГЕЙТ смерть (состояние 6): переопределение=%d, живой IsSelfDead=%s — %s\n"),
		SelfActionState, IsSelfDeadNow() ? TEXT("да") : TEXT("нет"),
		((SelfActionState >= 0) ? (SelfActionState == 6) : IsSelfDeadNow())
			? TEXT("СБРОС монет и скрытие окна") : TEXT("не срабатывает"));
	{
		const UAsdaEffectsSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
		S += FString::Printf(TEXT("  ГЕЙТ эффект-блокировщик %d: %s\n"), BlockingBuffId,
			(Fx && Fx->FindByTypeSub(SelfUserIdx(), BlockingBuffId, 0)) ? TEXT("АКТИВЕН, скилл запрещён") : TEXT("не активен"));
	}
	S += FString::Printf(TEXT("  авто-привязка окна: %s | якорь: справа %.0f, %s | позиция 56000 (%.0f,%.0f)\n"),
		bAutoAnchor ? TEXT("ВКЛ (пока чекбокс не трогали — эталон)") : TEXT("выкл (позиция ЗАФИКСИРОВАНА там, где стоит)"),
		PinRightMargin,
		bEtalonTopAnchor ? *FString::Printf(TEXT("сверху %.0f (эталон, дословный машинный код)"), PinTop)
		                 : *FString::Printf(TEXT("снизу %.0f (наше отступление, включено вручную)"), PinBottomMargin),
		PinnedPos.X, PinnedPos.Y);
	S += FString::Printf(TEXT("  перетаскивание: %s (чекбокс 56264 %s)\n"),
		bPinned ? TEXT("ВЫКЛЮЧЕНО") : TEXT("разрешено — тяни за боковые полосы"),
		bPinned ? TEXT("взведён") : TEXT("снят"));
	if (RootGroup)
	{
		const FGeometry& G = RootGroup->GetCachedGeometry();
		const FVector2D AbsP = G.GetAbsolutePosition();
		const FVector2D AbsS = G.GetLocalSize() * G.Scale;
		S += FString::Printf(TEXT("  окно на экране: %s, прямоугольник (%.0f,%.0f %.0fx%.0f)\n"),
			IsEffectivelyVisible(RootGroup) ? TEXT("РИСУЕТСЯ") : TEXT("НЕ РИСУЕТСЯ"),
			AbsP.X, AbsP.Y, AbsS.X, AbsS.Y);
	}
	return S;
}

FString UAsdaSoulGuardSubsystem::DumpControls() const
{
	auto Line = [](const TCHAR* Tag, const UWidget* W) -> FString
	{
		if (!W) { return FString::Printf(TEXT("  %-28s НЕ НАЙДЕН\n"), Tag); }
		FVector2D P(0, 0), S(0, 0);
		GetSlotRect(W, P, S);
		// ★ ЭКРАННЫЙ прямоугольник и ЭФФЕКТИВНАЯ видимость. Без них «узел виден» ничего не доказывает:
		//   узел может быть видим сам, но лежать под свёрнутым родителем или за краем экрана —
		//   ровно тот случай, когда «окно показано», а на экране пусто.
		const FGeometry& G = W->GetCachedGeometry();
		const FVector2D AbsP = G.GetAbsolutePosition();
		const FVector2D AbsS = G.GetLocalSize() * G.Scale;
		const UImage* Im = Cast<const UImage>(W);
		const UObject* Res = Im ? Im->GetBrush().GetResourceObject() : nullptr;
		return FString::Printf(TEXT("  %-28s %-6s %-11s локаль(%.0f,%.0f %.0fx%.0f) экран(%.0f,%.0f %.0fx%.0f) alpha=%.2f тек=%s%s\n"),
			Tag, W->GetVisibility() == ESlateVisibility::Collapsed ? TEXT("скрыт") : TEXT("виден"),
			IsEffectivelyVisible(W) ? TEXT("НА ЭКРАНЕ") : TEXT("НЕ РИСУЕТСЯ"),
			P.X, P.Y, S.X, S.Y, AbsP.X, AbsP.Y, AbsS.X, AbsS.Y, W->GetRenderOpacity(),
			Res ? *Res->GetName() : (Im ? TEXT("НЕТ") : TEXT("—")),
			Cast<const UAsdaAnimatedPlane>(W) ? TEXT(" [анимир.]") : TEXT(""));
	};
	FString S = TEXT("══ ХРАНИТЕЛЬ: все контролы Soul_Boost* ══\n");
	S += Line(TEXT("56000 Win (группа)"), RootGroup);
	S += Line(TEXT("56000 Win (хит)"), RootHit);
	S += Line(TEXT("56346 SOULBar_left"), BarLeft);
	S += Line(TEXT("56345 SOULBar_right"), BarRight);
	S += Line(TEXT("ручка left DRAG"), BarLeftDrag);
	S += Line(TEXT("ручка right DRAG"), BarRightDrag);
	S += Line(TEXT("56264 cbox"), PinCheck);
	S += Line(TEXT("56331 Ball_Red"), FlashRed);
	S += Line(TEXT("56332 Ball_Blue"), FlashBlue);
	S += Line(TEXT("56333 Ball_green"), FlashGreen);
	S += Line(TEXT("56208 b_ani2 (вверх)"), ArrowUp);
	S += Line(TEXT("56207 b_ani1 (вниз)"), ArrowDown);
	for (int32 i = 0; i < 3; ++i)
	{
		S += FString::Printf(TEXT(" — ряд %d —\n"), i + 1);
		S += Line(*FString::Printf(TEXT("BOX_%d"), i + 1), RowBox[i]);
		S += Line(*FString::Printf(TEXT("backg_%d"), i + 1), RowBackg[i]);
		S += Line(*FString::Printf(TEXT("BLACKCIRCLE %d"), i + 1), RowBlackCircle[i]);
		S += Line(*FString::Printf(TEXT("bb_back_%d"), i + 1), RowBallBack[i]);
		S += Line(*FString::Printf(TEXT("MARK_%d"), i + 1), RowMark[i]);
		S += Line(*FString::Printf(TEXT("main_%d"), i + 1), RowMain[i]);
		S += Line(*FString::Printf(TEXT("GTEXT_%d"), i + 1), RowGText[i]);
		S += Line(*FString::Printf(TEXT("BOLT %d"), i + 1), RowBolt[i]);
		S += Line(*FString::Printf(TEXT("D%d (контейнер)"), i + 1), RowDn[i]);
		S += Line(*FString::Printf(TEXT("mark_ani_%d"), i + 1), RowMarkAni[i]);
		S += Line(*FString::Printf(TEXT("ballani_%d"), i + 1), RowBallAni[i]);
		S += Line(*FString::Printf(TEXT("hot%d"), i + 1), RowHotText[i]);
		if (RowHotText[i])
		{
			S += FString::Printf(TEXT("      подпись хоткея: «%s»\n"), *RowHotText[i]->GetText().ToString());
		}
		FString Balls;
		for (int32 j = 0; j < 10; ++j)
		{
			const UWidget* W = RowBall[i * 10 + j];
			Balls += W ? (W->GetVisibility() == ESlateVisibility::Collapsed ? TEXT("·") : TEXT("●")) : TEXT("?");
		}
		S += FString::Printf(TEXT("      шары (D_10→D_1): %s  (монет %d)\n"), *Balls, GetCoin(i));
	}
	S += TEXT(" — плашка 56200 —\n");
	S += Line(TEXT("Texts (группа)"), TextsGroup);
	S += Line(TEXT("Texts (хит)"), TextsHit);
	for (int32 i = 0; i < 3; ++i)
	{
		S += Line(*FString::Printf(TEXT("Texts_%d BURST грп"), i + 1), BurstGroup[i]);
		S += Line(*FString::Printf(TEXT("Texts_%d BURST"), i + 1), BurstPanel[i]);
		S += Line(*FString::Printf(TEXT("  Text_burst %d"), i + 1), BurstLabel[i]);
		S += Line(*FString::Printf(TEXT("  SOUL_%d"), i + 1), BurstDigit[i]);
	}
	for (int32 i = 0; i < 3; ++i)
	{
		S += Line(*FString::Printf(TEXT("Texts_%d GUARDER грп"), i + 4), GuardGroup[i]);
		S += Line(*FString::Printf(TEXT("Texts_%d GUARDER"), i + 4), GuardPanel[i]);
		S += Line(*FString::Printf(TEXT("  Text_gaurder %d"), i + 1), GuardLabel[i]);
		S += Line(*FString::Printf(TEXT("  SOUL_%d"), i + 1), GuardDigit[i]);
	}
	return S;
}

// ★ Сводка сетевого тракта хранителя (asda.Soul.Net) — что подключено и что уже приезжало.
FString UAsdaSoulGuardSubsystem::DumpNet() const
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	FString S = TEXT("── ХРАНИТЕЛЬ: СЕТЕВОЙ ТРАКТ ──\n");
	S += FString::Printf(TEXT("  соединение: %s | свой userIdx=%d, свой charId=%d\n"),
		(Net && Net->IsConnected()) ? TEXT("ЕСТЬ") : TEXT("НЕТ"),
		SelfUserIdx(), Net ? Net->SelectedCharId : 0);
	S += FString::Printf(TEXT("  6157 монеты (S→C, только владельцу): принято %d, последний AddFlag=%d, монет %d/%d/%d\n"),
		NetCnt6157, LastCoinAddFlag, Coin[0], Coin[1], Coin[2]);
	S += FString::Printf(TEXT("  6156 индекс хранителя (S→C, по сектору): принято %d, свой индекс=%d, записей карты=%d\n"),
		NetCnt6156, SoulGuardIndex, SoulGuardIndexByUser.Num());
	S += FString::Printf(TEXT("  6158 применение скилла (C→S): отправлено %d\n"), NetSent6158);
	S += FString::Printf(TEXT("  6159 результат каста (S→C, ПО СЕКТОРУ = свой И чужие): принято %d\n"), NetCnt6159);
	S += FString::Printf(TEXT("  6160 начало каста (S→C, по сектору): принято %d\n"), NetCnt6160);
	S += FString::Printf(TEXT("  6161 состояние каста (S→C, по сектору): принято %d\n"), NetCnt6161);
	S += FString::Printf(TEXT("  последний каст: result=%d скилл=%d кастер(userIdx)=%d\n"),
		LastCastResult, LastCastSkill, LastCastUser);
	S += FString::Printf(TEXT("  предсказание мотиона: режим %d (%s)\n"), PredictGuardMotionMode,
		PredictGuardMotionMode > 0 ? TEXT("всегда") : (PredictGuardMotionMode < 0 ? TEXT("авто: только без сервера") : TEXT("никогда")));
	S += FString::Printf(TEXT("  эмулятор сервера: %s\n"), bSimulateServer ? TEXT("ВКЛ") : TEXT("выкл"));
	return S;
}

FString UAsdaSoulGuardSubsystem::DumpTables() const
{
	FString S = FString::Printf(TEXT("══ ТАБЛИЦЫ ХРАНИТЕЛЯ ══\n  SoulGuarderTable: %d записей, SkillCoinTable: %d записей (менеджер)\n"),
		GuarderTable.Num(), SkillCoinCount());
	for (const FAsdaSoulGuarderRow& R : GuarderTable)
	{
		S += FString::Printf(TEXT("  [%2d] тип %d ряд %d бафф %d скилл %d «%s» модель %s\n"),
			R.SoulGuardIndex, R.SkillType, R.RowIndex + 1, R.BuffSkillId, R.GuardianSkillId, *R.Name, *R.Model);
	}
	// ★ Аудит №3 (A3): считаем по строкам ТОЙ ЖЕ единственной таблицы — у менеджера.
	//   Своей копии строк у хранителя нет.
	int32 NonZero = 0;
	{
		UGameInstance* GI = GetGameInstance();
		UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		const UDataTable* Dt = IT ? IT->GetDataTable(FName(TEXT("SkillCoin"))) : nullptr;
		if (Dt)
		{
			for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
			{
				const FAsdaSkillCoinRow* R = reinterpret_cast<const FAsdaSkillCoinRow*>(It.Value);
				if (R && (R->Coin0 || R->Coin1 || R->Coin2)) { ++NonZero; }
			}
		}
	}
	S += FString::Printf(TEXT("  скиллов, дающих монеты: %d (диапазон начисления сервера [%d..%d))\n"),
		NonZero, CoinSkillIdMin, CoinSkillIdMax);
	return S;
}

// ─────────────────────────────── консоль ────────────────────────────────────────────────────────
static UAsdaSoulGuardSubsystem* SoulOf(UWorld* W)
{
	return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaSoulGuardSubsystem>() : nullptr;
}

// Вывод и в лог, и в системный журнал чата — как у asda.Mini.* (юзер играет, а не читает лог).
// ★ U13 (В1 дедупа №2): тело — общая точка `AsdaNotice::ConsoleEcho` (прежняя копия была дословной, менялась только категория лога).
#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
static void SoulEcho(UWorld* W, const FString& Text)
{
	AsdaNotice::ConsoleEcho(W, Text, LogAsdaSoul);
}
#endif // !UE_BUILD_SHIPPING

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulDump(
	TEXT("asda.Soul.Dump"),
	TEXT("Хранитель: состояние (монеты, пороги, уровень, скиллы рядов, плашка, эмулятор)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaSoulGuardSubsystem* S = SoulOf(W)) { SoulEcho(W, S->DumpState()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulControls(
	TEXT("asda.Soul.Controls"),
	TEXT("Хранитель: дамп КАЖДОГО контрола окна 56000 и плашки 56200"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaSoulGuardSubsystem* S = SoulOf(W)) { SoulEcho(W, S->DumpControls()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulTables(
	TEXT("asda.Soul.Tables"),
	TEXT("Хранитель: 27 записей SoulGuarderTable + сводка SkillCoinTable"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaSoulGuardSubsystem* S = SoulOf(W)) { SoulEcho(W, S->DumpTables()); }
	}));
#endif // ASDA_DEV_CONSOLE

// asda.Soul.Coins <c0> <c1> <c2> [addFlag] — эмулировать ПАКЕТ 6157 напрямую (сырой приём).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulCoins(
	TEXT("asda.Soul.Coins"),
	TEXT("Хранитель: эмулировать пакет 6157 — asda.Soul.Coins <ряд1> <ряд2> <ряд3> [addFlag 0|1|2] [skillId]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 6157 (монеты) открывает гейт рядов — клик/Soul.Use отправил бы 6158 по фальшивой доступности.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Soul.Coins"))) { return; }
		UAsdaSoulGuardSubsystem* S = SoulOf(W);
		if (!S) { return; }
		const int32 C0 = A.Num() > 0 ? FCString::Atoi(*A[0]) : 0;
		const int32 C1 = A.Num() > 1 ? FCString::Atoi(*A[1]) : 0;
		const int32 C2 = A.Num() > 2 ? FCString::Atoi(*A[2]) : 0;
		const int32 Fl = A.Num() > 3 ? FCString::Atoi(*A[3]) : 0;
		const int32 Sk = A.Num() > 4 ? FCString::Atoi(*A[4]) : -1;
		S->OnSlotState6157(-1, Fl, S->SkillType != 0 ? S->SkillType : S->SimSkillType, Sk, C0, C1, C2);
		SoulEcho(W, S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

// ★ asda.Soul.Cast <userIdx> <skillId> [result] — эмулировать ПАКЕТ 6159 (каст скилла хранителя
//   ЛЮБЫМ игроком, включая соседа). Ровно тот путь, которым идёт живой сервер: 6159 рассылается
//   по всему сектору и кастеру, и соседям.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulCast(
	TEXT("asda.Soul.Cast"),
	TEXT("Хранитель: эмулировать пакет 6159 — asda.Soul.Cast <userIdx кастера> <skillId 1228..1254> [result=1]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 6159 снимает замок «навык в полёте».
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Soul.Cast"))) { return; }
		UAsdaSoulGuardSubsystem* S = SoulOf(W);
		if (!S) { return; }
		const int32 Uid = A.Num() > 0 ? FCString::Atoi(*A[0]) : -1;
		const int32 Skl = A.Num() > 1 ? FCString::Atoi(*A[1]) : 1228;
		const int32 Res = A.Num() > 2 ? FCString::Atoi(*A[2]) : 1;
		S->OnSkillCast6159(Res, Uid, Skl, 1, 0);
		SoulEcho(W, FString::Printf(TEXT("Хранитель: эмулирован 6159 — кастер userIdx=%d, скилл=%d, result=%d"),
			Uid, Skl, Res));
	}));
#endif // ASDA_DEV_CONSOLE

// ★ asda.Soul.Predict <-1|0|1> — предсказывать ли боевой мотион компаньона по ОТПРАВКЕ 6158.
//   Эталон мотион по отправке НЕ ставит (его ставит пришедший 6159/6160), поэтому дефолт «авто»:
//   предсказываем только когда соединения нет.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulPredict(
	TEXT("asda.Soul.Predict"),
	TEXT("Хранитель: предсказание мотиона по отправке 6158 — asda.Soul.Predict <-1 авто | 0 никогда | 1 всегда>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSoulGuardSubsystem* S = SoulOf(W);
		if (!S) { return; }
		if (A.Num() > 0) { S->PredictGuardMotionMode = FCString::Atoi(*A[0]); }
		SoulEcho(W, FString::Printf(TEXT("Хранитель: предсказание мотиона = %d (−1 авто / 0 никогда / 1 всегда)"),
			S->PredictGuardMotionMode));
	}));
#endif // ASDA_DEV_CONSOLE

// ★ asda.Soul.Banner <burst|guard|взрыв|хранит> <1..3> [ключ соседа] — ПОКАЗАТЬ надпись без сервера.
//   Без ключа (или 0) — на СВОЁМ игроке ровно теми контролами, что показывает эталон.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulBanner(
	TEXT("asda.Soul.Banner"),
	TEXT("Хранитель: показать надпись — asda.Soul.Banner <burst|guard> <уровень 1..3> [charId/userIdx соседа]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSoulGuardSubsystem* S = SoulOf(W);
		if (!S) { return; }
		if (A.Num() < 1)
		{
			SoulEcho(W, TEXT("asda.Soul.Banner <burst|guard> <1..3> [charId/userIdx соседа]"));
			return;
		}
		const FString Kind = A[0].ToLower();
		const bool bGuard = Kind.StartsWith(TEXT("g")) || Kind.StartsWith(TEXT("х")) || Kind.StartsWith(TEXT("Х"));
		const int32 Lvl  = A.Num() > 1 ? FCString::Atoi(*A[1]) : 1;
		const int32 Peer = A.Num() > 2 ? FCString::Atoi(*A[2]) : 0;
		SoulEcho(W, S->DebugShowBanner(bGuard, Lvl, Peer));
	}));
#endif // ASDA_DEV_CONSOLE

// ★ asda.Soul.Banners — что сейчас показано (своя надпись + список чужих).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulBanners(
	TEXT("asda.Soul.Banners"),
	TEXT("Хранитель: состояние надписей (своя + чужие)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaSoulGuardSubsystem* S = SoulOf(W)) { SoulEcho(W, S->DumpBanners()); }
	}));
#endif // ASDA_DEV_CONSOLE

// ★ asda.Soul.BannerOthers <0|1> — надписи над ЧУЖИМИ игроками (в оригинале их НЕТ, отступление).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulBannerOthers(
	TEXT("asda.Soul.BannerOthers"),
	TEXT("Хранитель: надписи над чужими игроками (отступление от эталона) — asda.Soul.BannerOthers <0|1> [взрыв-на-касте 0|1]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSoulGuardSubsystem* S = SoulOf(W);
		if (!S) { return; }
		if (A.Num() > 0) { S->bPeerBanners = (FCString::Atoi(*A[0]) != 0); }
		if (A.Num() > 1) { S->bPeerBurstOnCast = (FCString::Atoi(*A[1]) != 0); }
		SoulEcho(W, FString::Printf(TEXT("Хранитель: надписи над чужими = %d, «Взрыв» соседа на касте = %d ")
			TEXT("(в ОРИГИНАЛЕ надписей над чужими нет вообще)"),
			S->bPeerBanners ? 1 : 0, S->bPeerBurstOnCast ? 1 : 0));
	}));
#endif // ASDA_DEV_CONSOLE

// ★ asda.Soul.BurstOnCast <0|1> — показывать «Душа Взрыв N» СЕБЕ в момент каста.
//   ЭТАЛОН ЭТОГО НЕ ДЕЛАЕТ (у него «Взрыв» = ряд достиг 5 монет), дефолт 0.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulBurstOnCast(
	TEXT("asda.Soul.BurstOnCast"),
	TEXT("Хранитель: «Душа Взрыв» себе в момент каста (НЕ эталон) — asda.Soul.BurstOnCast <0|1>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSoulGuardSubsystem* S = SoulOf(W);
		if (!S) { return; }
		if (A.Num() > 0) { S->bBurstOnCast = (FCString::Atoi(*A[0]) != 0); }
		SoulEcho(W, FString::Printf(TEXT("Хранитель: «Взрыв» на своём касте = %d (эталон = 0: там надпись ")
			TEXT("показывается по достижению 5 монет в ряду)"), S->bBurstOnCast ? 1 : 0));
	}));
#endif // ASDA_DEV_CONSOLE

// ★ asda.Soul.Net — сводка сетевого тракта хранителя (что подключено, что приходило).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulNet(
	TEXT("asda.Soul.Net"),
	TEXT("Хранитель: сводка СЕТЕВОГО ТРАКТА (6156/6157/6158/6159/6160/6161)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaSoulGuardSubsystem* S = SoulOf(W)) { SoulEcho(W, S->DumpNet()); }
	}));
#endif // ASDA_DEV_CONSOLE

// asda.Soul.Guard <index> — эмулировать пакет 6156 (призвать/снять хранителя).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulGuard(
	TEXT("asda.Soul.Guard"),
	TEXT("Хранитель: эмулировать пакет 6156 — asda.Soul.Guard <SoulGuardIndex 0..26, −1 = снять>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 6156 (индекс хранителя) в живой сессии.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Soul.Guard"))) { return; }
		UAsdaSoulGuardSubsystem* S = SoulOf(W);
		if (!S) { return; }
		S->OnSoulGuardIndex6156(-1, A.Num() > 0 ? FCString::Atoi(*A[0]) : -1);
		SoulEcho(W, S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

// asda.Soul.Type <skillType> — перезагрузить скиллы рядов по типу хранителя (1..9).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulType(
	TEXT("asda.Soul.Type"),
	TEXT("Хранитель: задать тип (элемент) и перезагрузить скиллы рядов — asda.Soul.Type <1..9, 0 = сброс>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSoulGuardSubsystem* S = SoulOf(W);
		if (!S) { return; }
		const int32 T = A.Num() > 0 ? FCString::Atoi(*A[0]) : 1;
		S->SimSkillType = T > 0 ? T : S->SimSkillType;
		S->LoadRowSkills(T);
		SoulEcho(W, S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

// asda.Soul.Use <ряд 1..3> — то же, что хоткей Insert/Home/PageUp.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulUse(
	TEXT("asda.Soul.Use"),
	TEXT("Хранитель: применить скилл ряда — asda.Soul.Use <1|2|3> (эквивалент хоткеев 45/46/47)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSoulGuardSubsystem* S = SoulOf(W);
		if (!S) { return; }
		const int32 Row = (A.Num() > 0 ? FCString::Atoi(*A[0]) : 1) - 1;
		const bool bOk = S->TryUseSkill(Row);
		SoulEcho(W, FString::Printf(TEXT("Хранитель: применение ряда %d → %s"),
			Row + 1, bOk ? TEXT("пакет 6158 ушёл") : TEXT("отказ (см. лог/чат)")));
	}));
#endif // ASDA_DEV_CONSOLE

// ── эмулятор сервера ──
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulSim(
	TEXT("asda.Soul.SimServer"),
	TEXT("Хранитель: включить/выключить ЛОКАЛЬНЫЙ эмулятор сервера — asda.Soul.SimServer <0|1> [грейд рядов 0..3] [тип 1..9]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSoulGuardSubsystem* S = SoulOf(W);
		if (!S) { return; }
		// ★ С-Р1 (26.09.2026), офлайн-гейт: ВКЛЮЧАТЬ локальный эмулятор сервера (поддельные монеты/бафф 1201+) можно только
		//   офлайн. Выключение (`0`) разрешено всегда — эмулятор, включённый офлайн, снимается и после подключения.
		const bool bWantSim = A.Num() > 0 ? (FCString::Atoi(*A[0]) != 0) : true;
		if (bWantSim && FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Soul.SimServer 1"))) { return; }
		S->bSimulateServer = bWantSim;
		if (A.Num() > 1) { S->SimJobCount = FMath::Clamp(FCString::Atoi(*A[1]), 0, 3); }
		if (A.Num() > 2) { S->SimSkillType = FCString::Atoi(*A[2]); }
		if (S->bSimulateServer && S->SkillType == 0) { S->LoadRowSkills(S->SimSkillType); }
		SoulEcho(W, S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulSkill(
	TEXT("asda.Soul.SimSkill"),
	TEXT("Хранитель (эмулятор): «применён боевой скилл N» → начисление монет по SkillCoinTable"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: эмулятор сервера: поддельное начисление монет (путь к 6158).
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Soul.SimSkill"))) { return; }
		UAsdaSoulGuardSubsystem* S = SoulOf(W);
		if (!S) { return; }
		S->SimSkillUsed(A.Num() > 0 ? FCString::Atoi(*A[0]) : 501);
		SoulEcho(W, S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulDecay(
	TEXT("asda.Soul.SimDecay"),
	TEXT("Хранитель (эмулятор): один тик распада (−1 с каждого ряда)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: эмулятор сервера: поддельный распад монет.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Soul.SimDecay"))) { return; }
		UAsdaSoulGuardSubsystem* S = SoulOf(W);
		if (!S) { return; }
		S->SimDecayTick();
		SoulEcho(W, S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulSpend(
	TEXT("asda.Soul.SimSpend"),
	TEXT("Хранитель (эмулятор): списать монеты — asda.Soul.SimSpend <ряд 1..3> [сколько, по умолчанию 5]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: эмулятор сервера: поддельная трата монет.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Soul.SimSpend"))) { return; }
		UAsdaSoulGuardSubsystem* S = SoulOf(W);
		if (!S) { return; }
		const int32 Row = (A.Num() > 0 ? FCString::Atoi(*A[0]) : 1) - 1;
		S->SimSpend(Row, A.Num() > 1 ? FCString::Atoi(*A[1]) : S->SkillCoinCost);
		SoulEcho(W, S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulDeath(
	TEXT("asda.Soul.SimDeath"),
	TEXT("Хранитель (эмулятор): смерть игрока — сброс монет и снятие хранителя"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: эмулятор сервера: поддельные 6157/6156, снятие баффа.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Soul.SimDeath"))) { return; }
		UAsdaSoulGuardSubsystem* S = SoulOf(W);
		if (!S) { return; }
		S->SimDeath();
		SoulEcho(W, S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

// Отступы «авто-якоря» по умолчанию: справа (эталонные 145) и снизу. Позволяет подогнать место
//   окна в игре без пересборки; 0 аргументов — просто показать текущее.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulAnchor(
	TEXT("asda.Soul.Anchor"),
	TEXT("Хранитель: отступы позиции окна — asda.Soul.Anchor [справа=145] [снизу=145] [top 0|1] (top=1 — эталон, верх)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSoulGuardSubsystem* S = SoulOf(W);
		if (!S) { return; }
		if (A.Num() > 0) { S->PinRightMargin  = FCString::Atof(*A[0]); }
		if (A.Num() > 1) { S->PinBottomMargin = FCString::Atof(*A[1]); }
		if (A.Num() > 2) { S->bEtalonTopAnchor = (FCString::Atoi(*A[2]) != 0); }
		S->ReanchorNow();
		SoulEcho(W, S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ ПОКАЗАТЬ ОКНО — и ТОЛЬКО. Никаких координат и никакого снятия авто-якоря: окно встаёт туда,
//   куда его ставит ЭТАЛОННАЯ формула. (Прежняя версия принимала x/y и гасила авто-якорь — из-за
//   этого эталонная позиция никогда не применялась, а чекбокс «не работал».)
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulShow(
	TEXT("asda.Soul.Show"),
	TEXT("Хранитель: показать окно (зарядить по монете, если пусто). Позицию НЕ меняет — ставит по эталону"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaSoulGuardSubsystem* S = SoulOf(W);
		if (!S) { return; }
		S->ShowWindowForTest();
		SoulEcho(W, S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

// Ручное перемещение — отдельной командой, имя честно говорит, что она двигает окно.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulMoveTo(
	TEXT("asda.Soul.MoveTo"),
	TEXT("Хранитель: подвинуть окно вручную — asda.Soul.MoveTo <x> <y> (снимает авто-якорь, как перетаскивание)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSoulGuardSubsystem* S = SoulOf(W);
		if (!S) { return; }
		S->MoveWindowTo(FVector2D(A.Num() > 0 ? FCString::Atof(*A[0]) : 400.f,
		                          A.Num() > 1 ? FCString::Atof(*A[1]) : 200.f));
		SoulEcho(W, S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulTarget(
	TEXT("asda.Soul.Target"),
	TEXT("Хранитель: userIdx цели в теле 6158 (индекс выбранной цели эталона g_nTargetUserIdx/targetRenderId; −1 = нет цели)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSoulGuardSubsystem* S = SoulOf(W);
		if (!S) { return; }
		S->OverrideTargetUserIdx = A.Num() > 0 ? FCString::Atoi(*A[0]) : -1;
		SoulEcho(W, FString::Printf(TEXT("Хранитель: цель в 6158 = %d"), S->OverrideTargetUserIdx));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulMapType(
	TEXT("asda.Soul.MapType"),
	TEXT("Хранитель: значение Net_GetCurrentMapType для проверки гейта (13/14/15/24/25/27 запрещают скилл; −1 = не задано)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSoulGuardSubsystem* S = SoulOf(W);
		if (!S) { return; }
		S->CurrentMapType = A.Num() > 0 ? FCString::Atoi(*A[0]) : -1;
		SoulEcho(W, FString::Printf(TEXT("Хранитель: Net_GetCurrentMapType = %d"), S->CurrentMapType));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulState(
	TEXT("asda.Soul.State"),
	TEXT("Хранитель: числовое состояние персонажа (effectSlots[112].field_8); 6 ⇒ сброс монет и скрытие окна"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSoulGuardSubsystem* S = SoulOf(W);
		if (!S) { return; }
		S->SelfActionState = A.Num() > 0 ? FCString::Atoi(*A[0]) : -1;
		SoulEcho(W, FString::Printf(TEXT("Хранитель: состояние персонажа = %d"), S->SelfActionState));
	}));
#endif // ASDA_DEV_CONSOLE
