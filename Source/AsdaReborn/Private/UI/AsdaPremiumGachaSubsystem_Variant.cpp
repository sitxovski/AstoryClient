// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPremiumGachaSubsystem_Variant.cpp — ★ R2 10.11.2026 (разрез AsdaPremiumGachaSubsystem.cpp, дословный перенос): выбор варианта приза.
//   Общее для частей — в AsdaPremiumGachaSubsystemInternal.h; шапка и реверс — в AsdaPremiumGachaSubsystem.cpp.
#include "UI/AsdaPremiumGachaSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaPremiumPostSubsystem.h"   // отложенный показ приза: сообщаем ящику итог выбора
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaGachaSceneActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"                          // TActorIterator — поиск актора сцены для диагностики
#include "Kismet/KismetRenderingLibrary.h"   // ★ 24.09: выгрузка буфера сцены гачи в PNG (asda.Gacha.DumpRt)
#include "Components/Image.h"
#include "Engine/World.h"
#include "AsdaNetSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Engine/DataTable.h"
#include "Items/AsdaItemDbRows.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaAnimatedPlane.h"      // 68058 `Apple_Make3` — кадровый анимплан (RestartAnim = `CControl_Show`)
// общая плашка подсказки + владелец показа   ★ R3: комментарий снятого включения "UI/AsdaItemTooltipSubsystem.h" (файл им не пользовался) — сохранён
// ★ общая точка модели контейнеров: ApplyExternalRecord (6582)   ★ R3: комментарий снятого включения "UI/AsdaInventorySubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaNotice.h"          // ★ 07.11.2026: окно-сообщение ONEBUT_MSGBOX (`CItem_BuildTooltipText(…, 0, -1, …)`)
#include "UI/AsdaUiHelpers.h"
#include "Components/AudioComponent.h"
#include "HAL/IConsoleManager.h"   // ★ 08.11.2026: ключ asda.Gacha.IdleLoop2D
#include "UI/AsdaJuiComboBox.h"   // ★ ОБЩАЯ точка: выпадающий список скина Bk_ComboBox
#include "UI/AsdaJuiWindowMeta.h"   // мета окон: ручка перетаскивания живёт в данных
#include "Framework/Application/SlateApplication.h"   // курсор для зоны колеса над окном 68310
#include "GameFramework/PlayerController.h"
#include "World/AsdaCoord.h"   // ★ единый перевод UE→игра: тайл входа в сцену (шаг 9)
#include "UI/AsdaCraftSubsystem.h"         // ★ гейт входа: открыты ли окна ремесла 76000/77500 (волна В2)
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "UI/AsdaItemRecordWriter.h"   // ★ U17 (В1 дедупа №2): синтетическая запись предмета 57 б
#include "AsdaPremiumGachaSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaPremiumGachaSubsystem*.cpp

namespace {
	constexpr int32 kLocJobCat1       = 11836;   // подписи профессий в комбобоксе SS1 (`uiItemFilterListPopulate`)
	constexpr int32 kLocJobCat12      = 11837;
	constexpr int32 kLocJobCat23      = 11838;
	constexpr int32 kLocJobCat40      = 12596;
	constexpr int32 kLocJobCat41      = 12597;
	constexpr int32 kLocJobCat42      = 12598;
}

// ═══════════════════════ ОКНО 68500: ВЫБОР ВАРИАНТА ПРИЗА ═══════════════════════
// Кандидаты — точный порт цикла отбора из `uiItemFilterListPopulate@0xB70BB0` и
//   `itemFindVariantBySlotOption@0xB71840` (у обоих он совпадает побайтово):
//     запись подходит, если  rec.SetCode (+232) == base.SetCode        и base.ItemSubType ∉ {200,201}
//                     либо   rec.LinkWordCode (+220) == base.LinkWordCode и base.ItemSubType ∈ {200,201}.
void UAsdaPremiumGachaSubsystem::CollectVariantCandidates(int32 BaseCode, TArray<int32>& OutCodes) const
{
	OutCodes.Reset();
	UAsdaItemTableSubsystem* Items = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Base = Items ? Items->GetItem(BaseCode) : nullptr;
	if (!Items || !Base) { return; }
	UDataTable* Table = Items->GetDataTable(TEXT("item_list"));
	if (!Table) { return; }

	const bool bPackage = (Base->ItemSubType == 200 || Base->ItemSubType == 201);
	// ★ ОТСЕЧКА, КОТОРОЙ В ЭТАЛОНЕ НЕТ (и это его дефект): у предмета без набора `SetCode == 0`
	//   исходное условие `rec.SetCode == 0` истинно для ДЕСЯТКОВ ТЫСЯЧ записей, и «вариантами»
	//   становится половина базы. То же с `LinkWordCode == 0` у коробок. Ключ связи нулевой ⇒
	//   родственных записей нет (закон `fix-original-defects`).
	if ((bPackage && Base->LinkWordCode == 0) || (!bPackage && Base->SetCode == 0)) { return; }
	for (const TPair<FName, uint8*>& It : Table->GetRowMap())
	{
		const FAsdaItemRow* Row = reinterpret_cast<const FAsdaItemRow*>(It.Value);
		if (!Row) { continue; }
		// ⚠ `LinkWordCode` в записи — u16 (эталон сравнивает `*(unsigned __int16 *)(rec + 220)`),
		//   и кэш подсистемы хранит его уже маскированным; у сырой строки таблицы маски нет.
		const bool bMatch = bPackage
			? ((Row->LinkWordCode & 0xFFFF) == Base->LinkWordCode)
			: (Row->SetCode == Base->SetCode);
		if (bMatch) { OutCodes.Add(Row->ItemCode); }
	}
}

// ★★★ «Есть ли из чего выбирать?» — общая точка отсечки пустого окна 68500 (замечание владельца №1).
//
//   ПРАВИЛО. Окно 68500 предлагает ровно ДВА выбора: профессию (поле записи +92 `UsageCategory`)
//   и ступень уровня (поле +80 `RequiredLevel`). Значит выбор существует тогда и только тогда,
//   когда среди кандидатов группы набирается БОЛЬШЕ ОДНОЙ комбинации «профессия × уровень».
//   Ни одного нового поля и ни одной эвристики: считаются те же два поля, из которых
//   `uiItemFilterListPopulate@0xB70BB0` строит сами списки, и та же группа кандидатов
//   (`SetCode` у вещей, `LinkWordCode` у коробок `subType 200/201`).
//
//   ЧТО ЭТО ОТСЕКАЕТ ПО ДАННЫМ `item_list` (58 878 записей, разбор — `elite_mailbox_fix.md` §0):
//     • 3 135 коробок с нулевым ключом `LinkWordCode` (уже отсекались в `CollectVariantCandidates`);
//     • 235 коробок, у которых в группе ТОЛЬКО они сами (случай владельца: `Crafting Package(2)`,
//       код 68431, `LinkWordCode 199`);
//     • 127 записей, у которых кандидатов несколько, но у ВСЕХ одна и та же профессия и один и
//       тот же уровень (костюмные наборы вроде `Spooky Pumpkin`: 5 частей одного `SetCode`,
//       уровень 1, профессия 1). Там оба списка вырождены в один пункт, а `ResolveVariant`
//       выбрал бы из пяти частей ПЕРВУЮ попавшуюся — то есть не ту вещь.
//   ЧТО ОСТАЁТСЯ: 5 326 записей — оружие, броня и наборы-коробки с настоящим выбором. Ровно то,
//   про что владелец сказал «для оружия, экипировки такое окно нужно».
bool UAsdaPremiumGachaSubsystem::HasVariantChoice(int32 BaseCode) const
{
	UAsdaItemTableSubsystem* Items = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!Items) { return false; }
	TArray<int32> Candidates;
	CollectVariantCandidates(BaseCode, Candidates);
	TSet<int32> Levels, Jobs;
	for (int32 Code : Candidates)
	{
		if (const FAsdaItemRecord* R = Items->GetItem(Code))
		{
			Levels.Add(R->RequiredLevel);
			Jobs.Add(R->UsageCategory);
		}
	}
	return (Levels.Num() * Jobs.Num()) > 1;
}

// Порт `uiItemInfoPopupOpen@0xB732F0` (ui_core.c:7971-7987) — наполнение и показ 68500.
//   ★ ЭТАЛОН ОТКРЫВАЕТ ОКНО БЕЗУСЛОВНО: `SetVisible(ctrl 68500, 1)` + `MapInsert` идут без единой
//     проверки, а список вариантов до того наполняет `uiItemFilterListPopulate@0xB70BB0`.
//     Прежний порт открывал окно ТОЛЬКО при `Candidates.Num() > 1`, а иначе молча слал 6581 —
//     это и было отступление, из-за которого кнопка «ничего не делала». Возвращаем 1:1.
//   Возвращает false лишь тогда, когда окна/контролов физически нет (тогда это дефект привязки,
//   и он попадает в лог, а не прячется).
bool UAsdaPremiumGachaSubsystem::OpenVariantWindow(int32 BaseCode)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Base = Items ? Items->GetItem(BaseCode) : nullptr;
	if (!Items || !Select2Win || !VarJobCombo || !VarJobCombo->IsReady()
		|| !VarLevelCombo || !VarLevelCombo->IsReady())
	{
		UE_LOG(LogAsdaGacha, Warning,
			TEXT("[премиум-гача] 68500 не открыть: окно=%s SS1=%s SS2=%s таблица предметов=%s"),
			Select2Win ? TEXT("да") : TEXT("НЕТ"),
			(VarJobCombo && VarJobCombo->IsReady()) ? TEXT("да") : TEXT("НЕТ"),
			(VarLevelCombo && VarLevelCombo->IsReady()) ? TEXT("да") : TEXT("НЕТ"),
			Items ? TEXT("да") : TEXT("НЕТ"));
		return false;
	}
	if (!Base)
	{
		// Эталон: `CItemManager_GetItemRecord` вернул null ⇒ имя/описание просто не ставятся,
		//   окно всё равно показывается. Воспроизводим и это, но пишем в лог.
		UE_LOG(LogAsdaGacha, Warning, TEXT("[премиум-гача] 68500: записи предмета %d нет в item_list"), BaseCode);
	}

	TArray<int32> Candidates;
	CollectVariantCandidates(BaseCode, Candidates);

	// SS2 «Выбран уровень»: значения поля +80 (RequiredLevel), подпись — само число
	//   (`sprintf(textBuf, "%d", subType)`); эталон перебирает subType 0…149.
	//   ★ Замечание владельца №5 «не плейсхолдеры, а нормальные строки»: подписи и здесь, и в
	//     списке профессий берутся ИЗ ДАННЫХ (число уровня / нотис профессии), а дизайнерская рыба
	//     «ABC123» из `CaptionText` гасится самим `UAsdaJuiComboBox::Setup`.
	VarLevelValues.Reset();
	TArray<FString> LevelLabels;
	for (int32 Level = 0; Level < 150; ++Level)
	{
		for (int32 Code : Candidates)
		{
			const FAsdaItemRecord* R = Items->GetItem(Code);
			if (R && R->RequiredLevel == Level)
			{
				VarLevelValues.Add(Level);
				LevelLabels.Add(FString::FromInt(Level));
				break;
			}
		}
	}
	VarLevelCombo->SetItems(LevelLabels);
	// SS1 «Выбрана профессия»: значения поля +92 (UsageCategory), эталон перебирает 0…49 и
	//   подписывает ТОЛЬКО категории 1/12/23/40/41/42 (loc 11836/11837/11838/12596/12597/12598).
	//   ★ Дефект оригинала: у остальных категорий строка в список не добавляется вовсе, хотя
	//     значение в массив кладётся — индексы разъезжаются. Чиним: у безымянной категории
	//     подписью служит её номер (закон `fix-original-defects`).
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	VarJobValues.Reset();
	TArray<FString> JobLabels;
	for (int32 Cat = 0; Cat < 50; ++Cat)
	{
		bool bHas = false;
		for (int32 Code : Candidates)
		{
			const FAsdaItemRecord* R = Items->GetItem(Code);
			if (R && R->UsageCategory == Cat) { bHas = true; break; }
		}
		if (!bHas) { continue; }
		int32 LocId = 0;
		switch (Cat)
		{
		case 1:  LocId = kLocJobCat1;  break;
		case 12: LocId = kLocJobCat12; break;
		case 23: LocId = kLocJobCat23; break;
		case 40: LocId = kLocJobCat40; break;
		case 41: LocId = kLocJobCat41; break;
		case 42: LocId = kLocJobCat42; break;
		default: break;
		}
		FString Label = (LocId && Txt) ? Txt->GetTextById(LocId) : FString();
		if (Label.IsEmpty()) { Label = FString::FromInt(Cat); }
		VarJobValues.Add(Cat);
		JobLabels.Add(Label);
	}
	VarJobCombo->SetItems(JobLabels);
	// Предвыбор — характеристики самого приза (иначе комбобоксы стартуют пустыми и 68511 даёт −1).
	//   Эталонный аналог — `CControl_SetSelectedItem(wnd[104], v13, nullptr)` в конце
	//   `uiItemFilterListPopulate` (ui_core.c:7866).
	if (Base)
	{
		const int32 JobIdx = VarJobValues.IndexOfByKey(Base->UsageCategory);
		if (VarJobValues.Num() > 0)   { VarJobCombo->SetSelectedIndex(JobIdx >= 0 ? JobIdx : 0); }
		const int32 LvlIdx = VarLevelValues.IndexOfByKey(Base->RequiredLevel);
		if (VarLevelValues.Num() > 0) { VarLevelCombo->SetSelectedIndex(LvlIdx >= 0 ? LvlIdx : 0); }
	}

	// Имя (68501), описание (68502) и иконка (68512) — как в `uiItemInfoPopupOpen@0xB732F0`.
	if (VarTitle) { VarTitle->SetText(FText::FromString(Items->GetItemName(BaseCode))); }
	if (VarDesc)
	{
		FAsdaItemTooltipRow Tip;                    // порт `itemTemplateFindByCode(...) + 4`
		if (Items->GetItemTooltip(BaseCode, Tip)) { VarDesc->SetText(FText::FromString(Tip.Description)); }
	}
	if (VarIcon)
	{
		// ★ Волна 1 (14.09.2026, В1 + исправление E3): общая точка иконки. Раньше клетка шла БЕЗ заворота;
		//   эталон рисует приз через `itemTooltipDrawRecordTextures` → `CControl_DrawTextureRect` →
		//   `planeFrame_setUV` + D3DTADDRESS_WRAP (разбор — у `AsdaUi::ApplyItemIcon`), поэтому клетка за краем
		//   узкого атласа повторяет атлас, а не пустеет. Кисть = размер клетки, слот 64×64 — как было.
		//   Откат — `asda.UI.ItemIconWrap 0`.
		if (AsdaUi::ApplyItemIcon(GI, VarIcon, BaseCode, FVector2D::ZeroVector,
			AsdaUi::IconSexSelf, /*bWrap*/ AsdaUi::IsItemIconWrapFixEnabled()))
		{
			if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(VarIcon)) { CS->SetSize(FVector2D(64.f, 64.f)); }
		}
	}

	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(FName(kSel2.Id), true);
		// ★★★ ПОВЕРХ ВСЕХ ОКОН (замечание владельца №2). Это НЕ отступление, а недостающая половина
		//   порта `uiItemInfoPopupOpen@0xB732F0`: последней строкой эталон делает
		//   `CClass_MapInsert(g_pWindowMgr, this->ctrl68500)` — ту самую «регистрацию верхним окном»,
		//   которой в порту соответствует `BringWindowToFront` (см. `ui_window_drag_and_hittest.md`,
		//   `uiWindowMgrDispatchMouse@0x12AF540`). Раньше окно вставало на свой статический z = 408
		//   и оказывалось ПОД окном ящика (z = 730) — ровно то, что владелец и увидел.
		WM->BringWindowToFront(FName(kSel2.Id));
	}
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] 68500: приз %d, профессий %d, уровней %d, кандидатов %d"),
		BaseCode, VarJobValues.Num(), VarLevelValues.Num(), Candidates.Num());
	return true;
}

// Порт `itemFindVariantBySlotOption@0xB71840`: среди кандидатов ищем запись, у которой
//   +80 (RequiredLevel) == значение SS2, +92 (UsageCategory) == значение SS1 и
//   +236 (ItemGrade) == грейд БАЗОВОГО приза. Возвращается код найденной записи, иначе −1.
// ★★★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА (закон `fix-original-defects`, wave_DATA 13.10.2026, решение координатора).
//   Эталон (item_inventory.c:5525-5545) возвращает ПЕРВУЮ подходящую запись в порядке таблицы. Но семейство
//   LinkWord бывает сборным: у сервера и у RU-клиента в семействе 124 лежат наборы Санты 68969…69004 и
//   Bunny Costume 69298…69306 с одинаковыми (профессия, уровень, грейд) — за приз «Bunny Costume Set» игрок
//   получал «Набор капитана Санты» (то же у Precieux Noble's Set 72903 → «Набор охотника пустоты» 62370…).
//   Порядок выбора среди РАВНЫХ по (уровень, профессия, грейд) кандидатов:
//     1) сам приз;  2) кандидат из НЕПРЕРЫВНОГО блока кодов семейства, содержащего приз (наборы лежат
//     подряд: 69298…69306); 3) первый подходящий — как эталон.
//   Кандидаты те же (то же семейство), поэтому серверный P67 (LinkWord варианта == LinkWord приза) пропускает
//   выбранный вариант так же, как прежний. Разбор и проверка — `Reports/anticheat/wave_DATA_20261013.md` §6.
int32 UAsdaPremiumGachaSubsystem::ResolveVariant(int32 BaseCode) const
{
	UAsdaItemTableSubsystem* Items = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Base = Items ? Items->GetItem(BaseCode) : nullptr;
	if (!Items || !Base || !VarJobCombo || !VarLevelCombo || !VarJobCombo->IsReady()) { return -1; }

	const int32 JobIdx = VarJobCombo->GetSelectedIndex();
	const int32 LvlIdx = VarLevelCombo->GetSelectedIndex();
	// Эталонная отсечка `slotOption >= 0xA → return -1` — там комбобокс отдаёт индекс, а массив
	//   значений всего на 10 позиций. У нас массив ровно по числу пунктов, поэтому проверяем его.
	if (!VarJobValues.IsValidIndex(JobIdx) || !VarLevelValues.IsValidIndex(LvlIdx)) { return -1; }
	const int32 WantJob   = VarJobValues[JobIdx];
	const int32 WantLevel = VarLevelValues[LvlIdx];

	TArray<int32> Candidates;
	CollectVariantCandidates(BaseCode, Candidates);
	// Непрерывный блок кодов семейства вокруг приза (кандидаты идут в порядке таблицы = по возрастанию кода).
	const TSet<int32> Family(Candidates);
	int32 RunLo = BaseCode, RunHi = BaseCode;
	while (Family.Contains(RunLo - 1)) { --RunLo; }
	while (Family.Contains(RunHi + 1)) { ++RunHi; }
	int32 First = -1;   // эталон: первый подходящий
	int32 InRun = -1;   // первый подходящий из блока приза
	for (int32 Code : Candidates)
	{
		const FAsdaItemRecord* R = Items->GetItem(Code);
		if (R && R->RequiredLevel == WantLevel && R->UsageCategory == WantJob && R->ItemGrade == Base->ItemGrade)
		{
			if (R->ItemCode == BaseCode) { return BaseCode; }                       // 1) сам приз
			if (InRun < 0 && R->ItemCode >= RunLo && R->ItemCode <= RunHi) { InRun = R->ItemCode; }
			if (First < 0) { First = R->ItemCode; }
		}
	}
	if (InRun >= 0 && InRun != First)
	{
		UE_LOG(LogAsdaGacha, Log,
			TEXT("[премиум-гача] вариант приза %d: из блока приза %d вместо первого в семействе %d ")
			TEXT("(исправление дефекта оригинала, эталон брал первый)"), BaseCode, InRun, First);
	}
	return (InRun >= 0) ? InRun : First;                                            // 2) блок приза, 3) эталон
}

// Кнопка 68511 «ОК» окна 68500 → (эталон) `itemFindVariantBySlotOption` → `ItemInfoBuyPanel_Populate`
//   → подтверждение → `sowelWndConfirmAction@0xB73620` → `sowelWndSendSocketRequest@0xB71D70` = 6581.
//   ★ Промежуточного «окна покупки» `ItemInfoBuyPanel_Populate@0xD7A270` в порту нет (это чужая
//     подсистема витрины предмета), поэтому 6581 уходит прямо отсюда — это ЕДИНСТВЕННОЕ сокращение
//     цепочки, и оно не меняет ни одного поля пакета.
//
// ★★★ ТЕЛО 6581 — ПОБАЙТОВО ИЗ ЗАПИСИ ПРИЗА (`sowelWndSendSocketRequest`, item_inventory.c:5655-5659):
//     bodyStruct[0]                     = thisCtx[180]                    // запись+0  = КОД предмета
//     LOWORD(bodyStruct[1])             = *(WORD *)((char *)thisCtx + 725)// запись+5  = слот   (i16)
//     BYTE2(bodyStruct[1])              = *((BYTE *)thisCtx + 724)        // запись+4  = контейнер (u8)
//     *(DWORD *)((char *)&bodyStruct[1] + 3) = itemVariant                // +7        = код варианта
//   (`thisCtx + 720` — начало записи, см. карту полей в заголовке.)
//   Раньше порт слал `слот = −1, контейнер = 0` КОНСТАНТАМИ — теперь берём их из самой записи,
//   как эталон. Сервер (`Recv_UserPrimiumGaChaReward@0x4F72B0`, разобран по GameServer.exe) делает
//   `sub_4D61E0(userNum, контейнер, слот, &запись)`: контейнер 1 → сумка, 2 → второй контейнер,
//   3 → надетое, 5 → склад; при неизвестном контейнере запись не находится и уходит **6582 код 10**.
void UAsdaPremiumGachaSubsystem::ConfirmVariant()
{
	if (VariantBaseCode < 0) { return; }
	const int32 Variant = ResolveVariant(VariantBaseCode);
	if (Variant < 0)
	{
		// Эталон печатает в подсказку НЕЛОКАЛИЗОВАННЫЙ литерал «NotFound» (ui_core.c:7588) —
		//   это дефект оригинала. Отдаём вменяемый нотис вместо мусора (закон `fix-original-defects`).
		if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
		{
			Msg->PostLoc(EAsdaMsgCategory::ItemRefused, 990474);   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «Такого варианта предмета нет»
		}
		UE_LOG(LogAsdaGacha, Warning, TEXT("[премиум-гача] вариант не найден для предмета %d"), VariantBaseCode);
		// «Выбор до получения»: заказ ещё в ящике — окно остаётся, игрок выбирает другое или жмёт крестик.
		if (bPreTakeChoice) { return; }
		// Обмена не будет ⇒ отложенная запись ящика обязана попасть в сумку.
		NotifyPostVariantFinished(false);
		return;
	}
	CloseVariantWindow();   // `uiResetHideControl@0xB735D0` — эталон гасит 68500 в конце `sowelWndConfirmAction`
	// [ИСПРАВЛЕНИЕ] 10.11.2026 «выбор до получения»: 6581 сейчас слать не по чему — предмет ещё в ящике.
	//   Вариант уходит ящику; он отправит 11006, а 6581 пошлёт мост, когда 11007 назовёт ячейку.
	if (bPreTakeChoice)
	{
		const int32 Base = VariantBaseCode;
		bPreTakeChoice = false;
		VariantBaseCode = PreTakeSavedBase;
		UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] выбор до получения: предмет %d → вариант %d, ящик заберёт заказ"), Base, Variant);
		if (UAsdaPremiumPostSubsystem* Post = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPremiumPostSubsystem>() : nullptr)
		{
			Post->OnPreTakeChoiceDone(true, Base, Variant);
		}
		return;
	}
	// `CSoundFlag_PlaySound2D("S_e_pr_gc_idle_00.wav", 1)` — первая строка `sowelWndConfirmAction`.
	// ★ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ: в эталоне сюда попадают ТОЛЬКО из открытой сцены гачи, поэтому
	//   петля покоя автомата всегда уместна. У нас появился второй вход — окно 68500, открытое
	//   мостом из 11007 элитного ящика (`OnPrizeTakenFromPost`), и сцена при этом может быть
	//   закрыта. Петля «работающего автомата» без автомата на экране — звук из ниоткуда, поэтому
	//   заводим её только при открытой сцене.
	if (bSceneOpen) { StartIdleLoop(); }
	SendVariantSwap(Variant);
}

// Хвост `ConfirmVariant` = `sowelWndSendSocketRequest@0xB71D70`: тело 6581 из записи приза + код варианта.
//   Вынесен, чтобы мост из ящика слал вариант, выбранный ДО получения, тем же кодом.
void UAsdaPremiumGachaSubsystem::SendVariantSwap(int32 Variant)
{
	// Поля ячейки берём ИЗ ЗАПИСИ ПРИЗА (запись+4 контейнер, запись+5 слот i16).
	int32 RecCode = PrizeItemCode;
	int32 RecSlot = -1;
	int32 RecCont = 0;
	if (PrizeRecord.Num() >= 7)
	{
		RecCode = *reinterpret_cast<const int32*>(PrizeRecord.GetData());
		RecCont = (int32)PrizeRecord[4];
		RecSlot = (int32)(*reinterpret_cast<const int16*>(PrizeRecord.GetData() + 5));
	}
	if (RecCont == 0 || RecSlot < 0)
	{
		// ★ ДИАГНОСТИКА ДЛЯ ЖИВОГО ЗАПУСКА. Сервер обязан прислать в ответе 6578 запись С ЯЧЕЙКОЙ
		//   (её проставляет `sub_4D63F0`, GameServer.exe 0x4D63F0: контейнер = запись+4, слот = запись+5).
		//   Если ячейки нет, `sub_4D61E0` на стороне сервера запись не найдёт и ответит 6582 код 10.
		UE_LOG(LogAsdaGacha, Warning,
			TEXT("[премиум-гача] 6581: в записи приза НЕТ ЯЧЕЙКИ (контейнер=%d слот=%d) — сервер ")
			TEXT("ответит отказом 10; причина на стороне сервера, см. Reports/gacha_prize_fix.md §4"),
			RecCont, RecSlot);
	}
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->SendPremiumGachaReward6581(RecCode, RecSlot, (uint8)RecCont, Variant);
	}
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] 6581: код %d, ячейка %d/%d, вариант %d"),
		RecCode, RecCont, RecSlot, Variant);
}

// ═══════════ ★★★ МОСТ «11007 → ГАЧА» (разбор — в заголовке у объявления) ═══════════
bool UAsdaPremiumGachaSubsystem::OnPrizeTakenFromPost(const TArray<uint8>& Rec57)
{
	if (Rec57.Num() < 57)
	{
		UE_LOG(LogAsdaGacha, Warning, TEXT("[премиум-гача] мост из ящика: запись %d б < 57 — пропуск"), Rec57.Num());
		return false;
	}
	// Окна сцены могли ни разу не открываться (игрок пришёл из ящика, а не с кнопки 73410).
	if (!EnsureWindows())
	{
		UE_LOG(LogAsdaGacha, Error, TEXT("[премиум-гача] мост из ящика: окна сцены не поднялись"));
		return false;
	}
	const int32 Code = *reinterpret_cast<const int32*>(Rec57.GetData());
	const int32 Cont = (int32)Rec57[4];
	const int32 Slot = (int32)(*reinterpret_cast<const int16*>(Rec57.GetData() + 5));

	// Запись приза (а с ней контейнер и слот) кладётся в состояние сцены. `TakePrize`/`ConfirmVariant`
	//   берут тело 6581 именно отсюда (`PrizeRecord` = порт `CGachaScene+720`, откуда читаются
	//   +724 контейнер / +725 слот).
	//   ★ ЭТО ДЕЛАЕТ И ЭТАЛОН, вопреки прежней записи в `elite_mailbox_impl.md` §4.1. В
	//     `inventorySyncApplyPacket` (item_inventory.c:66374) прямо перед ветвлением стоит
	//     `qmemcpy(&unk_2BDE400, v14, 0x38u); unk_2BDE438 = v15;`. Арифметика адресов: код приза
	//     пишется в `dword_2BDE4A4` = `gap_254[288]` = `CGachaScene + 884` ⇒ база сцены
	//     `0x2BDE4A4 − 884 = 0x2BDE130`, а значит `unk_2BDE400` = база + 720 (ровно `PrizeRecord`)
	//     и `unk_2BDE438` = база + 776. Копия идёт ДО всех условий — поэтому и здесь она безусловна.
	PrizeRecord.SetNumUninitialized(56);
	FMemory::Memcpy(PrizeRecord.GetData(), Rec57.GetData(), 56);
	PrizeItemCode = Code;

	bPostSwapPending = false;   // новый 11007 — прежнее ожидание обмена (если 6582 потерялся) больше не действует
	// [ИСПРАВЛЕНИЕ] 10.11.2026 «выбор до получения»: вариант выбран до 11006 — окно второй раз не открываем,
	//   сразу 6581 по ячейке из этой записи. true = «выбор в работе»: ящик держит запись до 6582.
	if (ArmedPostBase >= 0)
	{
		const int32 ArmBase = ArmedPostBase;
		const int32 ArmVariant = ArmedPostVariant;
		DisarmPostVariant();
		if (ArmBase == Code)
		{
			bPostSwapPending = true;
			LastPostSwapVariant = ArmVariant;
			UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] мост из элитного ящика: приз %d в ячейке %d/%d, вариант %d выбран до получения — 6581"),
				Code, Cont, Slot, ArmVariant);
			SendVariantSwap(ArmVariant);
			return true;
		}
		UE_LOG(LogAsdaGacha, Warning, TEXT("[премиум-гача] мост из ящика: заряжен вариант для %d, а пришёл %d — заряд снят"), ArmBase, Code);
	}

	// ★★★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА (замечание владельца №1, закон `fix-original-defects`).
	//   Эталон открывает 68500 для ЛЮБОГО приза, прошедшего фильтр `itemType/subType` + `BuyType==3`,
	//   ни разу не спросив, ЕСТЬ ЛИ из чего выбирать. А выбирать есть из чего далеко не всегда:
	//   кандидаты набираются группой (`SetCode` у вещей, `LinkWordCode` у коробок `subType 200/201`),
	//   и по данным `item_list` (58 878 записей) из 5 361 коробки **3 135 имеют `LinkWordCode == 0`
	//   и ещё 235 — группу из одной записи, то есть из САМОГО СЕБЯ**. Итого 3 370 коробок (62,9 %)
	//   открывают окно «выбери профессию и уровень», где выбирать не из чего, да ещё с красным
	//   предупреждением о привязке (68503) — на предмете, который ничего не привязывает.
	//   Ровно это владелец и увидел на `Crafting Package(2)` (код 68431, `subType 200`,
	//   `LinkWordCode 199`, группа = только он сам). Разбор — `Reports/elite_mailbox_fix.md` §0.
	//   Отсечку `LinkWordCode == 0` порт уже делал в `CollectVariantCandidates`; здесь она доведена
	//   до конца: ВАРИАНТ — это запись, ОТЛИЧНАЯ от самого приза.
	//   ⚠ Гейт стоит ТОЛЬКО на маршруте «из ящика». Кнопка 68452 внутри сцены гачи открывает окно
	//     по-прежнему безусловно: там игрок жмёт её сам, и окно — часть уже принятого владельцем
	//     потока гачи (`Docs/Port/systems/GACHA_FLOW.md`).
	if (!HasVariantChoice(Code))
	{
		UE_LOG(LogAsdaGacha, Log,
			TEXT("[премиум-гача] мост из элитного ящика: у приза %d НЕТ выбора «профессия × уровень» — ")
			TEXT("окно 68500 не открываем (исправление дефекта оригинала, замечание владельца №1); ")
			TEXT("ящик напечатает нотис 110671 «Покупка положена в рюкзак»"),
			Code);
		return false;
	}
	VariantBaseCode = Code;   // эталонное `gap_254[288]`: «для этого предмета выбирают вариант»

	const bool bShown = OpenVariantWindow(Code);
	UE_LOG(LogAsdaGacha, Log,
		TEXT("[премиум-гача] мост из элитного ящика: приз %d, ячейка контейнер=%d слот=%d, окно 68500=%s"),
		Code, Cont, Slot, bShown ? TEXT("показано") : TEXT("НЕ показано"));
	if (Cont == 0 || Slot < 0)
	{
		UE_LOG(LogAsdaGacha, Warning,
			TEXT("[премиум-гача] мост из ящика: сервер прислал запись БЕЗ ЯЧЕЙКИ (контейнер=%d слот=%d) — ")
			TEXT("обмен варианта (6581) получит отказ 10"), Cont, Slot);
	}
	return bShown;
}

// Кадровый разбор мыши для окна 68500: раскрытие/сворачивание списков и выбор пункта.
//   Один опрос `FAsdaMouseFrame` на окно (закон общей точки), оба списка получают ОДИН кадр.
void UAsdaPremiumGachaSubsystem::TickVariantWindow()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const bool bShown = WM && WM->IsWindowVisible(FName(kSel2.Id));
	if (!bShown)
	{
		if (VarJobCombo   && VarJobCombo->IsOpen())   { VarJobCombo->SetOpen(false); }
		if (VarLevelCombo && VarLevelCombo->IsOpen()) { VarLevelCombo->SetOpen(false); }
		VarMouse.Reset();
		return;
	}
	VarMouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлку кадра сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия
	//   сошлись бы в выбор пункта списка, которого игрок не делал. Раскрытый список НЕ схлопываем:
	//   у эталона захват модалки контролы ЗАМОРАЖИВАЕТ, а не сбрасывает их состояние.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		VarMouse.Reset();
		return;
	}
	// Раскрытый список — модальнее соседа: пока один открыт, второй мышь не разбирает, иначе
	//   клик по пункту, накрывшему чужое тело, раскрыл бы заодно и его.
	const bool bJobOpen = VarJobCombo && VarJobCombo->IsOpen();
	const bool bLvlOpen = VarLevelCombo && VarLevelCombo->IsOpen();
	if (VarJobCombo   && !bLvlOpen) { VarJobCombo->Tick(VarMouse); }
	if (VarLevelCombo && !bJobOpen) { VarLevelCombo->Tick(VarMouse); }
}

// ★ СООБЩИТЬ ЯЩИКУ, что история с выбором варианта закончена.
//   Нужно только отступлению «не показывать предмет до выбора» (`asda.Post.HideUntilChoice`,
//   разбор — `Reports/elite_mailbox_fix3.md` §2): ящик держит запись приза в ожидании и
//   должен узнать, чем всё кончилось. При выключённом ключе вызов ничего не делает.
void UAsdaPremiumGachaSubsystem::NotifyPostVariantFinished(bool bSwapApplied)
{
	bPostSwapPending = false;   // 6582 пришёл (успех или отказ) либо выбор закрыт — обмен по заряду больше не ждём
	if (UAsdaPremiumPostSubsystem* Post = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPremiumPostSubsystem>() : nullptr)
	{
		Post->OnVariantChoiceFinished(bSwapApplied);
	}
}

// ★ ЗАКРЫТЬ ОКНО 68500 БЕЗ ВЫБОРА. Одна точка на крестик 68519 и на консольную проверку: 6581 не
//   уходит, обмена нет, игрок остаётся с вариантом «по умолчанию» — и отложенная запись элитного
//   ящика (если включён `asda.Post.HideUntilChoice`) обязана попасть в сумку именно здесь.
void UAsdaPremiumGachaSubsystem::AbortVariantChoice()
{
	// [ИСПРАВЛЕНИЕ] 10.11.2026 «выбор до получения»: крестик = отмена получения. 11006 не уходил, заказ остаётся в ящике.
	if (bPreTakeChoice)
	{
		const int32 Base = VariantBaseCode;
		CancelPreTakeChoice();
		UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] окно 68500 закрыто до получения: предмет %d остаётся в ящике"), Base);
		if (UAsdaPremiumPostSubsystem* Post = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPremiumPostSubsystem>() : nullptr)
		{
			Post->OnPreTakeChoiceDone(false, Base, -1);
		}
		return;
	}
	CloseVariantWindow();
	NotifyPostVariantFinished(false);
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] окно 68500 закрыто без выбора — обмена не будет"));
}

// ═══ [ИСПРАВЛЕНИЕ] 10.11.2026 «ВЫБОР ДО ПОЛУЧЕНИЯ» (ключ `asda.Post.ChooseBeforeTake`, закон `fix-original-defects`) ═══
//   ЖАЛОБА ТЕСТЕРА: «костюм из премиум-ящика: окно спрашивает профессию и уровень, жму X — выдаётся первый вариант
//   (Воин, 1 ур.) вместо отмены». ЭТАЛОН: 68500 открывается ТОЛЬКО из `inventorySyncApplyPacket@0xEF1320`, то есть
//   ПОСЛЕ ответа 11007 статус 1 — сервер (`Recv_PremiumItemToInven_Req@0x4F8340`) уже удалил заказ из ящика и положил
//   базовый предмет коробки в сумку. Крестик 68519 в `GachaScene_OnControlMessage` не обрабатывается вовсе (пакета
//   нет), а кнопка повторного выбора 68452 работает лишь для приза автомата (`gap_254[288]` маршрут ящика не ставит).
//   Итог в оригинале: отказаться нельзя, после X у игрока навсегда вариант «по умолчанию».
//   ИСПРАВЛЕНИЕ без смены протокола: ящик открывает 68500 ДО 11006. ОК → 11006 → 11007 → мост сам шлёт 6581 с
//   выбранным вариантом (тело то же, ячейка из 11007). X → ничего не уходит, заказ остаётся в ящике.
bool UAsdaPremiumGachaSubsystem::PostItemHasVariantChoice(int32 BaseCode) const
{
	return HasVariantChoice(BaseCode);
}

bool UAsdaPremiumGachaSubsystem::OpenPreTakeChoice(int32 BaseCode)
{
	if (!EnsureWindows()) { return false; }
	// Окно одно: если в нём уже идёт выбор другого маршрута (приз сцены, приз после получения), чужой выбор не перехватываем.
	if (const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		if (WM->IsWindowVisible(FName(kSel2.Id)))
		{
			UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] выбор до получения для %d не открыт: окно 68500 занято другим выбором"), BaseCode);
			return false;
		}
	}
	PreTakeSavedBase = VariantBaseCode;
	VariantBaseCode = BaseCode;
	if (!OpenVariantWindow(BaseCode))
	{
		VariantBaseCode = PreTakeSavedBase;
		return false;
	}
	bPreTakeChoice = true;
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] окно 68500 открыто ДО получения: предмет %d ещё в ящике"), BaseCode);
	return true;
}

bool UAsdaPremiumGachaSubsystem::IsPreTakeChoiceOpen() const
{
	const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return bPreTakeChoice && WM && WM->IsWindowVisible(FName(kSel2.Id));
}

void UAsdaPremiumGachaSubsystem::CancelPreTakeChoice()
{
	if (!bPreTakeChoice) { return; }
	bPreTakeChoice = false;
	VariantBaseCode = PreTakeSavedBase;
	CloseVariantWindow();
}

void UAsdaPremiumGachaSubsystem::SetSceneVariantBase(int32 Code)
{
	if (bPreTakeChoice) { PreTakeSavedBase = Code; }   // окно сейчас занято выбором из ящика — значение сцены вернётся после него
	else { VariantBaseCode = Code; }
}

void UAsdaPremiumGachaSubsystem::ArmPostVariant(int32 BaseCode, int32 Variant)
{
	ArmedPostBase = BaseCode;
	ArmedPostVariant = Variant;
}

void UAsdaPremiumGachaSubsystem::DisarmPostVariant()
{
	ArmedPostBase = -1;
	ArmedPostVariant = -1;
}

void UAsdaPremiumGachaSubsystem::CloseVariantWindow()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(FName(kSel2.Id), false);
	}
}
