// Copyright (c) 2026 @sitxovski (Discord)
// AsdaHighRefineSubsystem_Craft.cpp — ★ R3-c 12.11.2026 (разрез AsdaHighRefineSubsystem.cpp, дословный перенос): проверка выбора, запрос, эффекты, ответ и записи, прогресс, мышь/подсказка, дампы, дев-команды.
//   Общее для частей — в AsdaHighRefineSubsystem_Private.h; шапка и реверс — в AsdaHighRefineSubsystem.cpp.
#include "UI/AsdaHighRefineSubsystem.h"


#include "AsdaHighRefineSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaHighRefineSubsystem*.cpp

// ═════════════════════════════ КЛЮЧИ ОТКАТА ИСПРАВЛЕНИЙ ═════════════════════════════════════════
//   Каждое видимое игроку отступление от оригинала — с ключом; значение по умолчанию = «исправлено».
// ★ Д7-1. `Recv_ItemUpdate` показывает эффект 78273 `Ani_DistructnEffect` («разрушение») на ЛЮБОЙ ответ,
//   кроме успеха, — в том числе на ОТКАЗЫ сервера (коды 0/2/5/6/9), когда ничего не списано и не сломано.
static ASDA_DEV_CVAR(int32, CVarRefusalNoFailFx,
	TEXT("asda.HighRefine.RefusalNoFailFx"), 1,
	TEXT("Дефект оригинала Д7-1: 1 = эффект «разрушения» 78273 только при провале (код 8), на отказах эффекта нет; "
	     "0 = как оригинал — эффект на любом ответе, кроме успеха."),
	ECVF_Default);

// ★ Д7-2. Сервер шлёт код 9 (`Recv_UpperRefine(1)` — ключ рецепта не совпал с таблицей сервера,
//   GameServer.c:98084-98089), а клиент для него строки не имеет (`default: break`) — отказ проходит
//   молча (плюс эффект «разрушения», Д7-1). Текст берём СУЩЕСТВУЮЩИЙ: 15130 «Прервано: ошибка данных.» —
//   тот же, что клиент выводит на код 0, которым сервер отвечает на такие же несовпадения таблиц.
static ASDA_DEV_CVAR(int32, CVarUnknownCodeText,
	TEXT("asda.HighRefine.UnknownCodeText"), 1,
	TEXT("Дефект оригинала Д7-2: 1 = на коды ответа 6501 вне таблицы клиента (9 и выше) выводить 15130 "
	     "«Прервано: ошибка данных.»; 0 = как оригинал — молча."),
	ECVF_Default);

// ★ Д7-4. Проверка перед отправкой (`itemComposeValidateSelection`) при отказе (нет рецепта / материалов /
//   золота) прячет полосу и снимает «занято», но НЕ снимает «идёт переплавка» (+4). Этот байт читает
//   `uiCheckRecentActivity` — окно нельзя закрыть X, пока игрок не нажмёт «Отмена» (ESC окно 78200 у
//   оригинала не закрывает вовсе — см. `Initialize`).
static ASDA_DEV_CVAR(int32, CVarClearProgressOnRefuse,
	TEXT("asda.HighRefine.ClearProgressOnRefuse"), 1,
	TEXT("Дефект оригинала Д7-4: 1 = отказ проверки перед 6500 снимает и флаг «идёт переплавка» (окно "
	     "закрывается сразу); 0 = как оригинал — до нажатия «Отмена» окно не закрыть."),
	ECVF_Default);

// ★ Д7-5. Ссылка на материал в 6500 (`itemTableFindRecordById`) — ПОСЛЕДНЯЯ стопка этого кода, а сервер
//   проверяет количество ТОЛЬКО в ней (`Recv_User_UpperRefine(4)`, код 5). Клиент же считает «есть / надо»
//   по СУММЕ стопок — при двух стопках (например 3 + 4 при нужных 5) окно разрешает «ОК», сервер отказывает.
static ASDA_DEV_CVAR(int32, CVarPickSufficientStack,
	TEXT("asda.HighRefine.PickSufficientStack"), 1,
	TEXT("Дефект оригинала Д7-5: 1 = в 6500 ссылаться на стопку материала, которой хватает (последнюю такую), "
	     "иначе — на последнюю; 0 = как оригинал — всегда последняя стопка."),
	ECVF_Default);

// ★ Д7-7 (волна 9, находка ревизии волны 8). Конец эффекта результата (78272 — 1600 мс, 78273 — 1000 мс,
//   `craftWndOnButtonCommand`, item_enhance.c:1571-1581) БЕЗУСЛОВНО ставит +4 = 0, прячет полосу и снимает
//   «занято». А «ОК» после ответа 6501 уже доступен (ответ снимает «занято» сразу) — нажатие в последние
//   ≈390 мс эффекта запускает новую полосу, которую конец эффекта тут же гасит: полоса пропадает, 6500 не
//   уходит, попытка теряется молча. Исправление: если «ОК» нажат ПОСЛЕ последнего ответа (новая попытка
//   идёт), конец эффекта прячет только сам эффект, флаги и полосу новой попытки не трогает.
static ASDA_DEV_CVAR(int32, CVarEffectEndKeepsNewAttempt,
	TEXT("asda.HighRefine.EffectEndKeepsNewAttempt"), 1,
	TEXT("Дефект оригинала Д7-7: 1 = конец эффекта результата не сбрасывает попытку, начатую «ОК» после ответа "
	     "(полоса и отправка 6500 доживают); 0 = как оригинал — конец эффекта гасит полосу, «идёт» и «занято» всегда."),
	ECVF_Default);

namespace {
	const FColor     kColResultChat(0xFF, 0xFF, 0x00, 0xFF);   // −256 = 0xFFFFFF00 — строки 6501 в чат
}

bool UAsdaHighRefineSubsystem::ValidateSelection()
{
	// `itemComposeValidateSelection@0xCF5290`: нет рецепта — молча; материалов не хватает — 15303;
	//   золота меньше цены (+64) — 15304 (гайд, текст в контрол).
	if (RecipeKey < 0) { return false; }
	if (!bMatsOk)
	{
		if (TxtGuide) { TxtGuide->SetText(FText::FromString(Loc(AsdaHighRefineLoc::kGuideNoMats))); }
		JournalAdd(TEXT("guide 15303"));
		return false;
	}
	const FAsdaRefinehighRow* R = RecipeAt(RecipeKey);
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const int32 Gold = Inv ? FMath::Max(0, Inv->GetGold()) : 0;   // `getFieldIfPositive`
	if (!R || Gold < R->GoldCost)
	{
		if (TxtGuide) { TxtGuide->SetText(FText::FromString(Loc(AsdaHighRefineLoc::kGuideNoGold))); }
		JournalAdd(TEXT("guide 15304"));
		return false;
	}
	return true;
}

void UAsdaHighRefineSubsystem::SendCraftRequest()
{
	// `itemCompose_SendCraftRequest@0xCF5390` (item_inventory.c:34893-35039).
	auto Refuse = [this]()
	{
		// Ветка «проверка не прошла»: `CloseHide` полосы, значение 0, «занято» = 0. «Идёт» эталон оставляет (Д7-4).
		ProgressStart = -1.0;
		SetProgressVisible(false);
		SetProgressValue(0.f);
		ResetMoneyTexture();
		if (CVarClearProgressOnRefuse.GetValueOnGameThread() != 0) { bInProgress = false; }
	};
	if (!ValidateSelection()) { Refuse(); return; }
	// ★★★ Волна 9 (С-1): код предмета в ячейке обязан совпасть с запомненным при построении сетки. Общая
	//   блокировка `AsdaUi::CheckRecentActivity` уже не даёт тронуть инвентарь во время полосы; это — защита на
	//   случай рассинхрона (пакет сервера сменил ячейку): иначе ушёл бы ДРУГОЙ предмет, чем показан. Отказ —
	//   молча, как ветка «нет рецепта» эталона (своего текста у оригинала нет, не выдумываем).
	{
		// ★ Волна 13 (СРЕДН-1): страница учитывается ВСЕГДА — так же, как сетка, рецепт и проверка
		//   (`RefreshBagPanel`/`RefreshRecipeInfo`/`ValidateSelection`). При ключе Д7-3 = 0 `Page ≡ 0`
		//   (`EnforceSinglePageWhenKeyOff`), и это ровно эталонное `vector[this[93]]`.
		const int32 Idx = SelCell + kGridCells * Page;
		const FAsdaItemInstance* Live = List.IsValidIndex(Idx) ? LiveOf(List[Idx]) : nullptr;
		const int32 LiveCode = (Live && !Live->IsEmpty()) ? Live->ItemCode : 0;
		if (!List.IsValidIndex(Idx) || LiveCode != List[Idx].IconCode)
		{
			UE_LOG(LogAsdaHighRefine, Warning, TEXT("[переплавка] 6500 не отправлен: в ячейке %d предмет %d, а выбран %d (рассинхрон)"),
				List.IsValidIndex(Idx) ? List[Idx].CellIndex : -1, LiveCode, List.IsValidIndex(Idx) ? List[Idx].IconCode : 0);
			JournalAdd(TEXT("send 6500 отказ: код ячейки сменился"));
			Refuse();
			return;
		}
	}
	TArray<uint8> Body;
	if (!DebugBuildRequest(Body))
	{
		UE_LOG(LogAsdaHighRefine, Warning, TEXT("[переплавка] 6500 не собран: нет записи выбранного предмета"));
		Refuse();
		return;
	}
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	// Раскладку повторяет сама отправка (тот же сборщик) — достаём аргументы из собранного тела.
	TArray<uint8> Mats[kMats];
	for (int32 M = 0; M < kMats; ++M)
	{
		const FAsdaRefinehighRow* R = RecipeAt(RecipeKey);
		const int32 MC[kMats] = { R ? R->Mat1Code : 0, R ? R->Mat2Code : 0, R ? R->Mat3Code : 0 };
		const int32 MN[kMats] = { R ? R->Mat1Qty : 0, R ? R->Mat2Qty : 0, R ? R->Mat3Qty : 0 };
		FindMaterialRecord(MC[M], MN[M], Mats[M]);
	}
	const bool bOk = Net && Net->SendUpperRefine6500(AsdaBytes::RdI32(Body.GetData()), AsdaBytes::RdI16(Body.GetData() + 4),
		Body[6], Mats[0], Mats[1], Mats[2]);
	JournalAdd(FString::Printf(TEXT("send 6500 %s"), bOk ? TEXT("OK") : TEXT("FAIL")));
	// Эталон после `CNetwork_Send` — `CloseHide` полосы; «идёт» и «занято» держатся до ответа 6501.
	ProgressStart = -1.0;
	SetProgressVisible(false);
	if (!bOk)
	{
		// ★ Закон «не защёлкивать состояние заранее»: пакет не ушёл — ответа не будет, окно освобождаем.
		UE_LOG(LogAsdaHighRefine, Warning, TEXT("[переплавка] 6500 не ушёл (нет соединения/гейт) — окно освобождено"));
		SetProgressValue(0.f);
		bInProgress = false;
		ResetMoneyTexture();
		return;
	}
	ReplyLatch.Arm(6500);   // ★ S2 (В2-A дедупа №2, 11.10.2026)
}

bool UAsdaHighRefineSubsystem::DebugBuildRequest(TArray<uint8>& OutBody) const
{
	// Тело 6500 для текущего выбора — сборщиком сетевого слоя (`BuildUpperRefine6500Body`).
	OutBody.Reset();
	const FAsdaRefinehighRow* R = RecipeAt(RecipeKey);
	if (!R) { return false; }
	// ★ Предмет — `vector[this[93]]` (эталон, БЕЗ страницы); Д7-3 — с учётом страницы. ★ Волна 13 (СРЕДН-1):
	//   страница учитывается всегда (при ключе 0 `Page ≡ 0`, см. `EnforceSinglePageWhenKeyOff`) — иначе при
	//   выключении ключа на ходу сетка и рецепт смотрели на страницу 2, а тело — на страницу 1.
	const int32 Idx = SelCell + kGridCells * Page;
	if (!List.IsValidIndex(Idx)) { return false; }
	const FAsdaItemInstance* T = LiveOf(List[Idx]);
	if (!T || T->IsEmpty() || T->Raw.Num() < 57) { return false; }
	const int32 MC[kMats] = { R->Mat1Code, R->Mat2Code, R->Mat3Code };
	const int32 MN[kMats] = { R->Mat1Qty, R->Mat2Qty, R->Mat3Qty };
	TArray<uint8> Mats[kMats];
	for (int32 M = 0; M < kMats; ++M) { FindMaterialRecord(MC[M], MN[M], Mats[M]); }
	OutBody = UAsdaNetSubsystem::BuildUpperRefine6500Body(RecipeKey, AsdaBytes::RdI16(T->Raw.GetData() + 5), T->Raw[4],
		Mats[0], Mats[1], Mats[2]);
	return OutBody.Num() == 40;
}

void UAsdaHighRefineSubsystem::FinishProgress()
{
	// `craftWndOnButtonCommand` case 78280 (событие 0x18 полосы): `if (+4) itemCompose_SendCraftRequest`.
	ProgressStart = -1.0;
	JournalAdd(TEXT("bar 0x18"));
	if (bInProgress) { SendCraftRequest(); }
}

void UAsdaHighRefineSubsystem::OnEffectFinished(int32 IdNum)
{
	// `craftWndOnButtonCommand` case 78272 / 78273: спрятать полосу и эффект, +4 = 0, «занято» = 0.
	JournalAdd(FString::Printf(TEXT("fx0x18 %d"), IdNum));
	if (IdNum == 78272 && FxE1) { FxE1->SetVisibility(ESlateVisibility::Collapsed); }
	if (IdNum == 78273 && FxE2) { FxE2->SetVisibility(ESlateVisibility::Collapsed); }
	// ★ Д7-7: «ОК» нажат после последнего ответа — идёт НОВАЯ попытка; её полосу и флаги не трогаем.
	if (CVarEffectEndKeepsNewAttempt.GetValueOnGameThread() != 0 && bInProgress
		&& AttemptStartTime >= 0.0 && AttemptStartTime > LastReplyTime)
	{
		JournalAdd(TEXT("fx end: новая попытка сохранена (Д7-7)"));
		return;
	}
	SetProgressVisible(false);
	bInProgress = false;
	ResetMoneyTexture();
}

void UAsdaHighRefineSubsystem::HandleE1Finished() { OnEffectFinished(78272); }
void UAsdaHighRefineSubsystem::HandleE2Finished() { OnEffectFinished(78273); }

// ═════════════════════════════ ПРИЁМ 6501 ═════════════════════════════════════════════════════
void UAsdaHighRefineSubsystem::DrainPendingPackets()
{
	// ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива.
	TArray<TArray<uint8>> Local;
	if (!PendingResults.Drain(Local)) { return; }
	for (const TArray<uint8>& B : Local) { OnResult(B); }
}

void UAsdaHighRefineSubsystem::ReleaseInFlight(const TCHAR* Why)
{
	if (!ReplyLatch.IsArmed()) { return; }
	ReplyLatch.Disarm();
	UE_LOG(LogAsdaHighRefine, Log, TEXT("[переплавка] замок «6500 в полёте» снят: %s (%.1f с)"), Why, FPlatformTime::Seconds() - ReplyLatch.ArmedAt);
}

void UAsdaHighRefineSubsystem::DebugMarkInFlight()
{
	// Хвост `SendCraftRequest` при `bOk`: полоса снята, «идёт»/«занято» держатся до ответа, замок Д7-6.
	bInProgress = true;
	AttemptStartTime = FPlatformTime::Seconds();
	ProgressStart = -1.0;
	SetProgressVisible(false);
	SetBusy();
	ReplyLatch.Arm(6500);   // ★ S2 (В2-A дедупа №2, 11.10.2026)
	JournalAdd(TEXT("debug: 6500 «ушёл»"));
}

void UAsdaHighRefineSubsystem::OnReplyTimeout()
{
	// ★★★ Волна 9 (МЕЛК-5 ревизии волны 8), закон «молчание сервера не даёт тупик»: таймаут ответа 6501
	//   делает то же, что общая часть ответа `Recv_ItemUpdate` после switch: +4 = 0, `CloseHide` полосы и 0,
	//   «занято» = 0 (ОК и X снова принимают ввод). Эффекта и нотиса нет — ответа не было; [96] не ставится
	//   (иначе «недавняя активность» ещё секунду держала бы инвентарь без причины).
	ReleaseInFlight(TEXT("таймаут ответа 6501"));
	bInProgress = false;
	ProgressStart = -1.0;
	AttemptStartTime = -1.0;
	JournalAdd(TEXT("timeout 6501 — окно освобождено"));
	if (!bControlsReady) { bBusy = false; return; }
	SetProgressVisible(false);
	SetProgressValue(0.f);
	ResetMoneyTexture();
}

void UAsdaHighRefineSubsystem::OnResult(const TArray<uint8>& Body)
{
	// `Recv_ItemUpdate@0xCF5B90` (network.c:16857-16969).
	if (Body.Num() < 235) { return; }
	ReleaseInFlight(TEXT("ответ 6501"));
	UGameInstance* GI = GetGameInstance();
	bDone = false;   // +3 = 0
	const int32 Code = (int32)Body[0];
	JournalAdd(FString::Printf(TEXT("recv 6501 код %d"), Code));
	UE_LOG(LogAsdaHighRefine, Log, TEXT("[переплавка] 6501 код=%d"), Code);
	auto Chat = [this, GI](int32 LocId)
	{
		// `CChat_AddTokenizedLines(g_pChatWnd, текст, −256, 10000, 0)` — системный чат, жёлтый, 10 с, буфер A.
		const FString T = Loc(LocId);
		if (!T.IsEmpty()) { AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, T, kColResultChat, 10000.f); }
		JournalAdd(FString::Printf(TEXT("chat %d"), LocId));
	};
	switch (Code)
	{
	case 0: Chat(AsdaHighRefineLoc::kRes0); break;
	case 1:
	case 8:
	{
		ApplyResultRecords(Body.GetData(), Code);
		if (Code == 1) { bDone = true; }
		// `uiSetHonorPointsDisplay(g_pGameClient, *(int*)(a2 + 231))` — новая сумма золота (миснейм); вес — сетевой слой.
		if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
		{
			Inv->SetGoldDisplay(AsdaBytes::RdI32(Body.GetData() + 231));
		}
		if (bControlsReady) { RefreshRecipeInfo(-1); }
		break;
	}
	case 2: Chat(AsdaHighRefineLoc::kRes2); break;
	case 3: Chat(AsdaHighRefineLoc::kRes3); break;
	case 4: Chat(AsdaHighRefineLoc::kRes4); break;
	case 5: Chat(AsdaHighRefineLoc::kRes5); break;
	case 6: Chat(AsdaHighRefineLoc::kRes6); break;
	case 7: Chat(AsdaHighRefineLoc::kRes7); break;
	default:
		// Эталон: `default: break` — молча (Д7-2: сервер шлёт 9 при несовпадении ключа рецепта).
		if (CVarUnknownCodeText.GetValueOnGameThread() != 0) { Chat(AsdaHighRefineLoc::kRes0); }
		else { UE_LOG(LogAsdaHighRefine, Warning, TEXT("[переплавка] 6501 код %d — эталон молчит (Д7-2 выключен)"), Code); }
		break;
	}
	// Всегда: текст золота, `CloseHide` полосы и 0, «занято» = 0, +4 = 0, эффект, [96] = сейчас.
	LastReplyTime = FPlatformTime::Seconds();
	bInProgress = false;
	ProgressStart = -1.0;
	if (!bControlsReady) { bBusy = false; return; }
	RefreshMoneyText();
	SetProgressVisible(false);
	SetProgressValue(0.f);
	ResetMoneyTexture();
	if (bDone)
	{
		ShowPlane(FxE1, true);   // 78272 `Ani_ShellEffect`
		JournalAdd(TEXT("fx+ 78272"));
	}
	else if (Code == 8 || CVarRefusalNoFailFx.GetValueOnGameThread() == 0)
	{
		ShowPlane(FxE2, true);   // 78273 `Ani_DistructnEffect`
		JournalAdd(TEXT("fx+ 78273"));
	}
}

void UAsdaHighRefineSubsystem::ApplyResultRecords(const uint8* Body, int32 Code)
{
	// Три записи материалов @1/@58/@115: количество ≠ 0 ⇒ `SlotTable_SetFieldById@0x99A560` — в ячейке
	//   (контейнер, слот) меняется ТОЛЬКО количество (+9); = 0 ⇒ `OnRecv_RemoveSlot@0xD0CF50` — ячейка
	//   очищается (контейнер 1 или 2). При коде 1 запись @172 — `OnRecv_ItemUpdate@0xD0CC20`: запись
	//   целиком в контейнер 1/2/3 по её полям.
	UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv || !Body) { return; }
	const UAsdaItemTableSubsystem* IT = GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>();
	Inv->BeginCellBatch();
	for (int32 I = 0; I < kMats; ++I)
	{
		const uint8* Rec = Body + 1 + 57 * I;
		const int32 Cont = (int32)Rec[4];
		const int32 Slot = AsdaBytes::RdI16(Rec + 5);
		const int32 Count = AsdaBytes::RdI32(Rec + 9);
		if (Cont != 1 && Cont != 2) { continue; }   // эталон: прочие контейнеры не трогает (неиспользованный материал)
		// ★ Наше (как N-3 заточки): эталон пишет по слоту из пакета без проверки границ — у нас кламп.
		const int32 Limit = FMath::Min(Cont == 1 ? kShellCells : kSpendCells, Inv->GetContainerSize(Cont));
		if (Slot < 0 || Slot >= Limit)
		{
			UE_LOG(LogAsdaHighRefine, Warning, TEXT("[переплавка] 6501 материал %d: контейнер %d слот %d вне границ — отброшен"), I, Cont, Slot);
			continue;
		}
		if (Count != 0)
		{
			const FAsdaItemInstance* C = Inv->GetCell(Cont, Slot);
			if (!C || C->IsEmpty() || C->Raw.Num() < 57) { continue; }
			TArray<uint8> R57 = C->Raw;
			R57[4] = (uint8)Cont;
			AsdaBytes::WrI16(R57.GetData() + 5, Slot);
			AsdaBytes::WrI32(R57.GetData() + 9, Count);   // только количество (+9), остальное — как было
			Inv->ApplyExternalRecord(R57);
		}
		else
		{
			Inv->ClearCell(Cont, Slot);
		}
	}
	if (Code == 1)
	{
		const uint8* Rec = Body + 172;
		const int32 Cont = (int32)Rec[4];
		const int32 ItemCode = AsdaBytes::RdI32(Rec);
		// `OnRecv_ItemUpdate`: код самоцвета в сумке/экипировке — окно «Wrong Sowell/Equip Item Info» (1:1).
		if ((Cont == 1 || Cont == 3) && IT && IT->IsSowelCode(ItemCode))
		{
			AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::ItemRefused,
				Cont == 1 ? TEXT("Wrong Sowell Item Info") : TEXT("Wrong Equip Item Info"));
		}
		if (Cont == 1 || Cont == 2 || Cont == 3)
		{
			TArray<uint8> R57; R57.Append(Rec, 57);
			Inv->ApplyExternalRecord(R57);
		}
	}
	Inv->EndCellBatch();
}

// ═════════════════════════════ ПОЛОСА 78280 ═══════════════════════════════════════════════════
void UAsdaHighRefineSubsystem::SetProgressValue(float Value)
{
	// `CProgressBar_SetValue@0x12E8D00` (флаг 4, слева направо): значение зажато в [0, 100], заливка =
	//   ширина плана 0 × значение / 100. Кисть не подменяем — режем ширину плана заливки (общая точка).
	ProgressValue = FMath::Clamp(Value, 0.f, 100.f);
	FAsdaGaugeFill::Clip(BarFill, BarBaseLeft, BarFullW, ProgressValue / 100.f);
}

void UAsdaHighRefineSubsystem::SetProgressVisible(bool bVisible)
{
	// U3 (дедуп №2, В2-A, 11.10.2026): правило «показать без хит-теста» — общая `AsdaUi::SetShownNoHit` (вынос, поведение то же).
	AsdaUi::SetShownNoHit(BarBg, bVisible);
	AsdaUi::SetShownNoHit(BarFill, bVisible);
}

void UAsdaHighRefineSubsystem::TickProgress()
{
	// `CProgressBar_UpdateAnim@0x12E9400`: v = (now − t0) / 1000 × (animMax − animMin), animMax = ширина
	//   плана 0 (258). На v ≥ 100 — событие 0x18 и стоп. ⇒ полоса заполняется за 1000 × 100 / 258 ≈ 388 мс
	//   (пиксели и проценты у оригинала перепутаны — так же, как у полосы гачи 10066; воспроизводим).
	if (ProgressStart < 0.0) { return; }
	const double ElapsedMs = (FPlatformTime::Seconds() - ProgressStart) * 1000.0;
	const float AnimMax = BarFullW > 0.f ? BarFullW : 100.f;
	const float V = (float)(ElapsedMs / kProgressAnimMs) * AnimMax;
	SetProgressValue(V);
	if (V >= 100.f) { FinishProgress(); }
}

// ═════════════════════════════ ДАННЫЕ ═════════════════════════════════════════════════════════
const FAsdaItemInstance* UAsdaHighRefineSubsystem::LiveOf(const FAsdaHighRefineEntry& E) const
{
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	return (Inv && E.CellIndex >= 0) ? Inv->GetCell(1, E.CellIndex) : nullptr;
}

const FAsdaRefinehighRow* UAsdaHighRefineSubsystem::RecipeAt(int32 Index) const
{
	// `containerFindByKey(this+84, k)` — доступ ПО ПОЗИЦИИ в списке (ключ [94] = поле +0 = позиция).
	return Recipes.IsValidIndex(Index) ? &Recipes[Index] : nullptr;
}

bool UAsdaHighRefineSubsystem::FindMaterialRecord(int32 ItemCode, int32 Need, TArray<uint8>& OutRec57) const
{
	// `itemTableFindRecordById@0x999C80`: снаряжение — сумка 0…29, прочее — расходники 0…59; совпадений
	//   может быть несколько — берётся ПОСЛЕДНЕЕ (цикл без `break`). Ненайденный — пустой массив
	//   (ссылка останется инициализатором `helper_initInvalidIds`).
	OutRec57.Reset();
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!Inv || !IT || ItemCode <= 0) { return false; }
	const bool bEquip = IT->IsWeaponOrEquipClass(ItemCode);   // `itemIsWeaponOrEquipClass` — общая точка
	const int32 Cont = bEquip ? 1 : 2;
	const int32 N = bEquip ? kShellCells : kSpendCells;
	const FAsdaItemInstance* Last = nullptr;
	const FAsdaItemInstance* LastEnough = nullptr;
	for (int32 I = 0; I < N; ++I)
	{
		const FAsdaItemInstance* C = Inv->GetCell(Cont, I);
		if (!C || C->ItemCode != ItemCode || C->Raw.Num() < 57) { continue; }
		Last = C;
		if (C->Amount >= Need) { LastEnough = C; }
	}
	// ★ Д7-5: ссылаться на стопку, которой хватает (сервер проверяет количество только в указанной).
	const FAsdaItemInstance* Pick = (CVarPickSufficientStack.GetValueOnGameThread() != 0 && LastEnough) ? LastEnough : Last;
	if (!Pick) { return false; }
	OutRec57 = Pick->Raw;
	return true;
}

FString UAsdaHighRefineSubsystem::Loc(int32 LocId) const
{
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

FString UAsdaHighRefineSubsystem::GetGuideText() const
{
	return TxtGuide ? TxtGuide->GetText().ToString() : FString();
}

void UAsdaHighRefineSubsystem::PlaySfx(const TCHAR* Name)
{
	// `CSoundFlag_PlaySound2D(g_pSoundMgr, "имя.wav", 0)` — 2D по имени, без петли; доигрывает до конца.
	JournalAdd(FString::Printf(TEXT("snd %s"), Name));
	AsdaUi::PlaySound2DByName(GetGameInstance(), Name);   // ★ R5-b 12.11.2026: общая точка (то же тело)
}

void UAsdaHighRefineSubsystem::JournalAdd(const FString& Event)
{
	AsdaUi::JournalAppend(Journal, JournalStart, Event);   // ★ R5-b 12.11.2026: общая точка (то же тело, хвост 256 строк)
}

// ═════════════════════════════ ОПРОС МЫШИ И ТУЛТИПЫ ═══════════════════════════════════════════
void UAsdaHighRefineSubsystem::TickMouse()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	// Закон шапки `AsdaWindowSubsystem.h`: ПЕРВОЙ строкой — чужой всплывающий слой под курсором.
	//   ★ Волна 9 (МЕЛК-7): и ОБЫЧНОЕ окно поверх (20000 перекрывает 78200 полосой 56 px) — мышь его.
	if (!WM || WM->IsPointerBlockedByTopmost(kWindowId) || WM->IsCursorOverHigherWindow(kWindowId))
	{
		TickTooltip(-1, -1);
		Mouse.Reset();
		ListScroll.bThumbGrab = false;   // волна 12: захват бегунка не переживает перекрытие окна
		return;
	}
	// ★ Волна 12: полоса 78226 — раньше ячеек: клик по ней не уходит ни в ячейку, ни в «текст золота».
	if (TickListScroll())
	{
		TickTooltip(-1, -1);
		return;
	}
	int32 Kind = -1, Idx = -1;
	for (int32 C = 0; C < kGridCells && Kind < 0; ++C)
	{
		if (GridIcons.IsValidIndex(C) && GridIcons[C] && AsdaUi::IsCursorOverWidget(GridIcons[C])) { Kind = kTipGrid; Idx = C; }
	}
	if (Kind < 0 && ResultIcon && AsdaUi::IsCursorOverWidget(ResultIcon)) { Kind = kTipResult; Idx = 0; }
	for (int32 M = 0; M < kMats && Kind < 0; ++M)
	{
		if (MatIcons.IsValidIndex(M) && MatIcons[M] && AsdaUi::IsCursorOverWidget(MatIcons[M])) { Kind = kTipMat; Idx = M; }
	}
	TickTooltip(Kind, Idx);
	if (!Mouse.bLmbDown) { return; }
	// msg 0xB — клик по ЛЮБОМУ контролу окна: при «свободно» текст золота, по ячейке сетки — ещё и рецепт.
	if (Kind == kTipGrid) { OnGridCellClicked(Idx); }
	else if (!bBusy && WM->IsCursorOverWindow(kWindowId)) { RefreshMoneyText(); }
}

void UAsdaHighRefineSubsystem::TickTooltip(int32 Kind, int32 Idx)
{
	// `uiItemTooltipOnHoverDispatch@0xCF4120` (msg 8), msg 0xA — `CControl_KillFocus(&g_ItemTooltip)`.
	// ★ Дедуп №2, В1-B (U18, 11.10.2026): шапка трекера (ключ «что показано», гашение при уходе курсора) — общая точка
	//   `UAsdaItemTooltipSubsystem::BeginHoverTip`; тело показа (эталонные билдеры окна) — ниже, как было.
	UAsdaItemTooltipSubsystem* Tip = UAsdaItemTooltipSubsystem::BeginHoverTip(GetGameInstance(), EAsdaTipOwner::HighRefine, TipKind, TipIdx, Kind, Idx);
	if (!Tip) { return; }
	const UAsdaItemTableSubsystem* IT = GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>();
	bool bShown = false;
	if (Kind == kTipGrid)
	{
		// `a3 − 78211 + 14 × [92] < size` ⇒ `ItemTooltip_Build(живая запись, −1, 0, 1, 1, 0, 0)`.
		const int32 Abs = Idx + kGridCells * Page;
		const FAsdaItemInstance* It = List.IsValidIndex(Abs) ? LiveOf(List[Abs]) : nullptr;
		if (It && !It->IsEmpty()) { Tip->ShowItemTooltip(*It, /*bSuppressCompare*/false, EAsdaTipOwner::HighRefine, 1); bShown = true; }
	}
	else if (Kind == kTipResult)
	{
		// 78271: эталонный гейт `(uint32)([93] + 14 × [92]) <= 13` (Д7-3: с ключом — выбор есть и в пределах списка).
		const int32 Abs = SelCell + kGridCells * Page;
		const bool bOk = (CVarWheelPages.GetValueOnGameThread() != 0)
			? (SelCell >= 0 && List.IsValidIndex(Abs))
			: ((uint32)Abs <= 13u && List.IsValidIndex(Abs));
		const FAsdaItemInstance* It = bOk ? LiveOf(List[Abs]) : nullptr;
		if (It && !It->IsEmpty()) { Tip->ShowItemTooltip(*It, /*bSuppressCompare*/false, EAsdaTipOwner::HighRefine, 1); bShown = true; }
	}
	else if (Kind == kTipMat && RecipeKey >= 0)
	{
		// 78241/78251/78261 при [94] ≥ 0: код материала рецепта; снаряжение — синтетическая запись и полный
		//   билдер (`CSlot_Init` + поля +50/+13/+14 = `ShowPreviewSlotTooltip`), прочее — простой билдер
		//   `CControl_SetState(&g_ItemTooltip, 0, код, 1, 1, −1, 0)` (количество 1).
		const FAsdaRefinehighRow* R = RecipeAt(RecipeKey);
		const int32 Code = !R ? 0 : (Idx == 0 ? R->Mat1Code : Idx == 1 ? R->Mat2Code : R->Mat3Code);
		const FAsdaItemRecord* MR = (IT && Code > 0) ? IT->GetItem(Code) : nullptr;
		if (MR && IT->IsWeaponOrEquipClass(Code))
		{
			Tip->ShowPreviewSlotTooltip(Code, EAsdaTipOwner::HighRefine);
			bShown = true;
		}
		else if (MR)
		{
			FAsdaItemInstance Slot;
			const TArray<uint8> Rec = FAsdaItemRecordWriter::ByCodeQty(Code, 1);   // ★ U17
			Slot.SetRaw(Rec.GetData(), Rec.Num());
			Tip->ShowItemTooltip(Slot, /*bSuppressCompare*/false, EAsdaTipOwner::HighRefine, 2);
			bShown = true;
		}
		else if (Code > 0)
		{
			Tip->ShowItemTooltipByCode(Code, EAsdaTipOwner::HighRefine);
			bShown = true;
		}
	}
	if (!bShown) { Tip->HideTooltipFor(EAsdaTipOwner::HighRefine); }
}

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════
FString UAsdaHighRefineSubsystem::DumpControls() const
{
	FString S = TEXT("=== окно высокой переплавки 78200: разобранные контролы ===\n");
	int32 Found = 0, Total = 0;
	auto Line = [&S, &Found, &Total](const TCHAR* Id, const UObject* W)
	{
		++Total; if (W) { ++Found; }
		S += FString::Printf(TEXT("  %-44s %s\n"), Id, W ? TEXT("есть") : TEXT("НЕТ"));
	};
	Line(TEXT("78201 movetext «заголовок»"),      TxtTitle);
	Line(TEXT("78201 _DRAG — ручка окна"),         DragHandle);
	Line(TEXT("78202 Top_text1"),                  TxtTop);
	Line(TEXT("78210 T1_1 (подпись вкладки)"),     TxtOptTitle);
	Line(TEXT("78225 M1_Check (рамка выбора)"),    CheckFrame);
	Line(TEXT("78230 T2_1 гайд"),                  TxtGuide);
	Line(TEXT("78231 T3_1 «Необходимые материалы»"),TxtMatTitle);
	for (int32 M = 0; M < kMats; ++M)
	{
		Line(*FString::Printf(TEXT("%d MARK2_%d (план)"), 78241 + 10 * M, M + 1), MatIcons.IsValidIndex(M) ? MatIcons[M].Get() : nullptr);
		Line(*FString::Printf(TEXT("%d MARK2_%d (текст)"), 78241 + 10 * M, M + 1), MatIconLabels.IsValidIndex(M) ? MatIconLabels[M].Get() : nullptr);
		Line(*FString::Printf(TEXT("%d имя материала"), 78242 + 10 * M), MatNames.IsValidIndex(M) ? MatNames[M].Get() : nullptr);
		Line(*FString::Printf(TEXT("%d «есть / надо»"), 78243 + 10 * M), MatCounts.IsValidIndex(M) ? MatCounts[M].Get() : nullptr);
	}
	Line(TEXT("78271 DMARK_1 (план 64×64)"),       ResultIcon);
	Line(TEXT("78271 DMARK_1 (текст)"),            ResultLabel);
	Line(TEXT("78272 DMARK_1_E1 (анимация)"),      FxE1);
	Line(TEXT("78273 DMARK_1_E2 (анимация)"),      FxE2);
	Line(TEXT("78280 bar_G план 0 (фон)"),         BarBg);
	Line(TEXT("78280 bar_G план 1 (заливка)"),     BarFill);
	Line(TEXT("78281 T4_1 «Опыт за успех»"),       TxtExpLabel);
	Line(TEXT("78282 T4_2 опыт"),                  TxtExp);
	Line(TEXT("78284 T4_3 «Стоимость»"),           TxtUseGoldLbl);
	Line(TEXT("78285 T4_4 цена"),                  TxtUseGold);
	Line(TEXT("78286 T4_5 «Золото»"),              TxtHaveGoldLbl);
	Line(TEXT("78287 T4_6 золото"),                TxtHaveGold);
	Line(TEXT("78291 OKbtn"),                      BtnOk);
	Line(TEXT("78291 подпись"),                    TxtBtnOk);
	Line(TEXT("78292 XXbtn"),                      BtnCancel);
	Line(TEXT("78292 подпись"),                    TxtBtnCancel);
	Line(TEXT("78299 Xbtn"),                       BtnX);
	int32 Gi = 0, Gl = 0;
	for (const auto& P : GridIcons) { if (P) { ++Gi; } }
	for (const auto& P : GridLabels) { if (P) { ++Gl; } }
	S += FString::Printf(TEXT("  78211…78224 сетка: планы %d/14, тексты %d/14\n"), Gi, Gl);
	Found += Gi + Gl; Total += 28;
	Line(TEXT("78226 list_1 (зона `WheelWin_Nomal`)"), ListZone1);
	Line(TEXT("78265 list_2 (зона, скрыта как в .JUI)"), ListZone2);
	Line(TEXT("78226 полоса: дорожка"),         ListScroll.Track);
	Line(TEXT("78226 полоса: ↑"),               ListScroll.Up);
	Line(TEXT("78226 полоса: ↓"),               ListScroll.Down);
	Line(TEXT("78226 полоса: бегунок"),         ListScroll.Thumb);
	S += FString::Printf(TEXT("  полоса: ширина плана 0 = %.0f, левый край заливки = %.0f\n"), BarFullW, BarBaseLeft);
	S += FString::Printf(TEXT("  ИТОГО: найдено %d/%d\n"), Found, Total);
	return S;
}

FString UAsdaHighRefineSubsystem::DumpState() const
{
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	FString S = FString::Printf(
		TEXT("=== окно высокой переплавки: состояние ===\n")
		TEXT("  открыто=%d init(+0)=%d занято(+2)=%d успех(+3)=%d идёт(+4)=%d страница[92]=%d ячейка[93]=%d рецепт[94]=%d хватает[95]=%d\n")
		TEXT("  полоса: идёт=%d значение=%.1f · замок 6500=%d · недавняя активность=%d · золото=%d · рецептов=%d\n"),
		bOpen ? 1 : 0, bInited ? 1 : 0, bBusy ? 1 : 0, bDone ? 1 : 0, bInProgress ? 1 : 0, Page, SelCell, RecipeKey, bMatsOk ? 1 : 0,
		ProgressStart >= 0.0 ? 1 : 0, ProgressValue, ReplyLatch.IsArmed() ? 1 : 0, IsRecentActivity() ? 1 : 0, Inv ? Inv->GetGold() : -1, Recipes.Num());
	S += FString::Printf(TEXT("  гайд: «%s»\n"), *GetGuideText());
	for (int32 M = 0; M < kMats; ++M)
	{
		S += FString::Printf(TEXT("  материал %d: код=%d имя=«%s» счёт=«%s»\n"), M, MatCodes[M],
			(MatNames.IsValidIndex(M) && MatNames[M]) ? *MatNames[M]->GetText().ToString() : TEXT("-"),
			(MatCounts.IsValidIndex(M) && MatCounts[M]) ? *MatCounts[M]->GetText().ToString() : TEXT("-"));
	}
	S += FString::Printf(TEXT("  цена=«%s» опыт=«%s» золото=«%s» результат=%d\n"),
		TxtUseGold ? *TxtUseGold->GetText().ToString() : TEXT("-"), TxtExp ? *TxtExp->GetText().ToString() : TEXT("-"),
		TxtHaveGold ? *TxtHaveGold->GetText().ToString() : TEXT("-"), ResultCode);
	S += FString::Printf(TEXT("  список (%d):"), List.Num());
	for (int32 I = 0; I < List.Num(); ++I) { S += FString::Printf(TEXT(" [%d]яч%d=%d"), I, List[I].CellIndex, List[I].IconCode); }
	S += TEXT("\n");
	return S;
}

bool UAsdaHighRefineSubsystem::DebugBuildResult(int32 Code, TArray<uint8>& OutBody) const
{
	// Тело 6501 из ТЕКУЩЕГО выбора — так, как его собирает `recvDBUpperRefine` (GameServer.c:98150-98195):
	//   записи материалов после списания (кончилось — код 0), запись предмета, вес, золото после цены.
	OutBody.SetNumZeroed(235);
	OutBody[0] = (uint8)Code;
	const FAsdaRefinehighRow* R = RecipeAt(RecipeKey);
	const int32 Abs = SelCell + kGridCells * Page;
	const FAsdaItemInstance* T = List.IsValidIndex(Abs) ? LiveOf(List[Abs]) : nullptr;
	if (!R || !T || T->Raw.Num() < 57) { return Code != 1 && Code != 8; }
	const int32 MC[kMats] = { R->Mat1Code, R->Mat2Code, R->Mat3Code };
	const int32 MN[kMats] = { R->Mat1Qty, R->Mat2Qty, R->Mat3Qty };
	for (int32 M = 0; M < kMats; ++M)
	{
		TArray<uint8> Rec;
		if (!FindMaterialRecord(MC[M], MN[M], Rec)) { continue; }
		const int32 Left = AsdaBytes::RdI32(Rec.GetData() + 9) - MN[M];
		if (Left > 0) { AsdaBytes::WrI32(Rec.GetData() + 9, Left); } else { AsdaBytes::WrI32(Rec.GetData() + 9, 0); }
		FMemory::Memcpy(OutBody.GetData() + 1 + 57 * M, Rec.GetData(), 57);
	}
	FMemory::Memcpy(OutBody.GetData() + 172, T->Raw.GetData(), 57);
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	AsdaBytes::WrI16(OutBody.GetData() + 229, 0);
	AsdaBytes::WrI32(OutBody.GetData() + 231, FMath::Max(0, (Inv ? Inv->GetGold() : 0) - R->GoldCost));
	return true;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
	FAutoConsoleCommandWithWorldAndArgs GCmdHighRefineWindow(
		TEXT("asda.HighRefine.Window"),
		TEXT("asda.HighRefine.Window — открыть окно 78200 (порт craftWnd_Open; видимое не трогает)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
		{
			if (UAsdaHighRefineSubsystem* S = HighRefineFromWorld(W)) { S->OpenWindow(); }
		}));
}
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
	FAutoConsoleCommandWithWorldAndArgs GCmdHighRefineState(
		TEXT("asda.HighRefine.State"),
		TEXT("asda.HighRefine.State — контролы, флаги эталона, выбор, рецепт, журнал."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
		{
			if (UAsdaHighRefineSubsystem* S = HighRefineFromWorld(W))
			{
				UE_LOG(LogAsdaHighRefine, Log, TEXT("%s%s"), *S->DumpControls(), *S->DumpState());
				for (const FString& L : S->GetJournal()) { UE_LOG(LogAsdaHighRefine, Log, TEXT("  журнал: %s"), *L); }
			}
		}));
}
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
	FAutoConsoleCommandWithWorldAndArgs GCmdHighRefineClick(
		TEXT("asda.HighRefine.Click"),
		TEXT("asda.HighRefine.Click <IDNum> — нажать контрол окна 78200 боевым обработчиком (78211…78224, 78291, 78292, 78299)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			UAsdaHighRefineSubsystem* S = HighRefineFromWorld(W);
			if (!S || Args.Num() < 1) { return; }
			const int32 Id = FCString::Atoi(*Args[0]);
			if (Id >= 78211 && Id <= 78224) { S->OnGridCellClicked(Id - 78211); }
			else if (Id == 78291) { S->OnOkClicked(); }
			else if (Id == 78292) { S->OnCancelClicked(); }
			else if (Id == 78299) { S->OnCloseClicked(); }
		}));
}
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
	FAutoConsoleCommandWithWorldAndArgs GCmdHighRefineSimResult(
		TEXT("asda.HighRefine.SimResult"),
		TEXT("asda.HighRefine.SimResult <код> — синтез тела 6501 из текущего выбора и прогон через БОЕВОЙ приёмник."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 6501 подделывает записи и золото и снимает замок «6500 в полёте».
			if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.HighRefine.SimResult"))) { return; }
			UAsdaHighRefineSubsystem* S = HighRefineFromWorld(W);
			if (!S || Args.Num() < 1) { return; }
			TArray<uint8> B;
			if (S->DebugBuildResult(FCString::Atoi(*Args[0]), B)) { S->DebugInjectResult(B); }
		}));
}
#endif // ASDA_DEV_CONSOLE
