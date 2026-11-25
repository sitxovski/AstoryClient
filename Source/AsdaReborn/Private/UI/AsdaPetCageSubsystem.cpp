// Copyright (c) 2026 @sitxovski (Discord)
// Реализация главного окна питомцев (43000 + 43058 + 43200).
//   Весь реверс с адресами — в шапке `UI/AsdaPetCageSubsystem.h` и в
//   `Reports/pet_windows_re_report.md` §2, §4.6, §6.5.
#include "UI/AsdaPetCageSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaCraftSubsystem.h"   // ★ гейты Г1/Г2: имена окон ремесла 76000/77500 (волна В2)

#include "UI/AsdaJuiWindowHandle.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaPetGrowthSubsystem.h"   // ★ волна 4: кнопка 43055 открывает окно 44200
#include "UI/AsdaPetEvolutionSubsystem.h" // ★ волна 5: кнопка 43056 открывает окно 43500
#include "UI/AsdaPetEvoTipSubsystem.h"   // ★ волна 4: наведение на слот показывает окно 43300
#include "UI/AsdaPetPreviewActor.h"      // ★ волна 9: превью 43052 — свой RT 240×170 и вращение
#include "Engine/TextureRenderTarget2D.h"   // дамп размера буфера превью
#include "Materials/MaterialInstanceDynamic.h"   //   кисть кадра для 43052 (и для 43576 окна 43500)
#include "Materials/MaterialInterface.h"        // ★ 24.09: мастер-материал кисти превью
#include "Kismet/KismetRenderingLibrary.h"      // ★ 24.09: выгрузка буфера превью в PNG (asda.Pet.PreviewDumpRt)
#include "Engine/World.h"                //   спавн актора сцены превью
#include "Items/AsdaItemDbRows.h"
#include "Pet/AsdaPetSubsystem.h"
#include "Net/NetOpcodes.h"          // OP_PET_SLOT_COUNT — диагностическая подача 6123
#include "AsdaNetSubsystem.h"        // ★ волна 6: SetSelfWeight — полоса веса из 6109/6122
#include "AsdaBytes.h"               // ★ волна 6: сборка тел 6109/6122 для стенда

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/EditableText.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Login/AsdaLoginData.h"   // [ПОРТ] 08.10.2026: лимиты ввода в байтах провода (TruncateToWireLimit)
#include "Test/AsdaSelfTestKit.h"   // ★ 21.10.2026: итог самотеста asda.Pet.SelfTestNameRule
#include "Text/AsdaLang.h"          // ★ 21.10.2026: английские названия пород (оверлей pettable) в самотесте кличек
#include "Text/AsdaLangRows.h"       // [ПОРТ] 09.10.2026 (мультиязычность): формат опции питомца на языке сессии
#include "AsdaPetCageSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaPetCageSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaPetCage);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

namespace PetCageLoc {
	constexpr int32 kFeedNeedCall  = 101105;  // network.c:25862 — отправка 6108 из сумки без призванного
}

// ★★★ КЛЮЧИ ОТКАТА. Объявлены НА УРОВНЕ ФАЙЛА, а не внутри ветки обработчика: `static` внутри
//   `case` регистрируется лишь при первом входе в ветку, и до первого пакета 6119 консоль
//   отвечала бы «unknown command» — то есть ключ было невозможно ввести заранее.
// ★ [ИСПРАВЛЕНИЕ] 09.11.2026, пачка 20 баг 6 «после удаления питомца его модель остаётся». Опрос превью эталона (раз в 500 мс,
//   `entity_char.c:7787-7801`) показывает цель окна развития, иначе выбранного (`g_pPetBuffEntry` = `wnd[154]`), а если показывать
//   некого — `mapDecoModelRemoveById`: модель снимается. Приёмник 6111 зануляет `wnd[154]` (`item_enhance.c:4751`), значит после
//   выброса превью пустеет. Порт ветку «некого» не переносил, и в превью 43052 стояла модель выброшенного питомца.
//   1 — снимать модель, когда выбранного нет; 0 — прежнее поведение.
static ASDA_DEV_CVAR(int32, CVarPreviewClearNoSelection,
	TEXT("asda.Pet.PreviewClearNoSelection"), 1,
	TEXT("Превью 43052: 1 — выбранного питомца нет (выброшен, съеден) — модель снимается, как у оригинала; 0 — остаётся прежняя."),
	ECVF_Default);

// ★ [ИСПРАВЛЕНИЕ] 08.11.2026, пачка 19 баг 7 «после телепорта плашка питомца пропадает и появляется не сразу». Сброс «смена карты»
//   прячет плашку, опрос тут же показывает её снова (до входа своего персонажа), а `RevealWorld` при снятии загрузочного экрана
//   зовёт `UAsdaWindowSubsystem::CloseAll`: тот снимает виджет со вьюпорта мимо `HideStatusPanel`, и признак `bStatusShown`
//   остаётся взведённым. Опрос оригинала (`inventoryAutoUpdateThrottled2s@0xDCC170`) смотрит на флаг показа, который
//   `uiCloseAllWindows` сбрасывает (`sub_92FC88`, ui_core.c:54379), и через ≤ 2 с показывает плашку снова; у порта флаг
//   врал, и плашка ждала постороннего открытия. 1 — снятое со вьюпорта окно считается спрятанным, опрос в тот же кадр;
//   0 — прежнее поведение.
static ASDA_DEV_CVAR(int32, CVarStatusReviveClosed,
	TEXT("asda.Pet.StatusReviveClosed"), 1,
	TEXT("Плашка питомца 43200: 1 — окно, снятое со вьюпорта (CloseAll при входе на карту), считается спрятанным и показывается снова "
	     "на ближайшем опросе; 0 — как было (плашка ждёт постороннего открытия)."),
	ECVF_Default);

// ═════════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════
void UAsdaPetCageSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());

	Cells.SetNum(kSlots);
	RowGroups.SetNum(kRowContainers);

	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// Событие 21 эталона (`uiCloseWindowReset`, ui_core.c:64925) — закрыть себя по Esc.
		WM->RegisterEscCloser(kCageWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaPetCageSubsystem::CloseCage));
		// ★ Волна В4 ремесла (ревизия В3 С-2): эталонный `uiCloseAllWindows@0xD61960` (AsdaStory_game.c:372790-372821,
		//   ui_core.c:54372-54373) безусловно зовёт `uiCloseWindowReset(g_bDeviceReloadPending)` — закрыть клетку 43000 —
		//   и `sub_92FC88(g_bDeviceReloadPending)` — спрятать плашку 43200. Роспись «закрыть всё» — те же точки
		//   закрытия, что у Esc и у эталона (обе идемпотентны). Снимок слотов 9/10 — `AsdaWindowSnapshot.cpp`.
		WM->RegisterWindowCloser(kCageWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaPetCageSubsystem::CloseCage));
		WM->RegisterWindowCloser(kStatusWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaPetCageSubsystem::HideStatusPanel));
	}
	if (UAsdaPetSubsystem* P = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ событие в очередь.
		HPetEvent = P->OnPetEvent.AddLambda([this](int32 Op, int32 Res, int32 Uid)
		{
			PetEvents.Add(TTuple<int32, int32, int32>(Op, Res, Uid));
		});
	}
	UE_LOG(LogAsdaPetCage, Log, TEXT("[питомец-окно] подсистема поднята (43000 + 43058 + 43200)"));
}

void UAsdaPetCageSubsystem::Deinitialize()
{
	if (UAsdaPetSubsystem* P = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPetSubsystem>() : nullptr)
	{
		P->OnPetEvent.Remove(HPetEvent);
	}
	ResetCageWidget();
	ResetPanelWidget();
	ResetStatusWidget();
	Super::Deinitialize();
}

void UAsdaPetCageSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	CloseCage();
	HideStatusPanel();
	HideActionPanel();
	bConfirmOpen = false;
	bDirty = false;
	NameShownForUid = -1;
	HoverSlot = -1;
	TipBar = -1;
	StatusPollLeft = 0.f;
	MarkerAnchor = FVector2D(-1.0, -1.0);
	PetEvents.Reset();
	Mouse.Reset();
	Drag.Disarm();
	StatusIconPress.Clear();       // ★ поправка ревизии: и пара иконки 43205 тоже
	// ★ ВОЛНА 6: стенд — ИНСТРУМЕНТ ПРОГОНА, он не должен пережить ни разрыв, ни смену карты:
	//   при взведённом перехвате в сеть не уходит НИЧЕГО (то же правило, что у волны 3, КРИТ-1).
	//   Снимаем и сам перехват — иначе он остался бы висеть в модели.
	if (bStandArmed)
	{
		if (UAsdaPetSubsystem* P = Pets()) { P->EndSendCapture(); }
		UE_LOG(LogAsdaPetCage, Warning, TEXT("[питомец-окно] сброс сессии снял взведённый СТЕНД %d"), StandOpcode);
	}
	bStandArmed = false;
	bStandDone = false;
	StandLeft = 0.f;
	StandOpcode = 0;
	StandExpectUid = StandExpectBag = StandExpectSlot = -1;
	(void)Reason;
}

void UAsdaPetCageSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.PetCage");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// ★ ВОЛНА 6, НОТИС 101105 «Для этого нужно вызвать питомца». Эталон печатает его прямо в
	//   отправителе `netSendItemPairRequest@0xDC8FC0` (network.c:25860-25863) — в ветке `else`
	//   гейта `useEnhanceSlot || refineWnd[155]`, то есть ТОЛЬКО на пути «предмет из сумки» при
	//   отсутствии ПРИЗВАННОГО питомца. Отправитель этот — метод `CPetData`, то есть окно 43000,
	//   поэтому нотис принадлежит именно сюда (закон `loc-per-handler-not-by-family`).
	//   Модель волны 1 взводит признак в `RequestFeed` и отдаёт его ОДИН раз
	//   (`ConsumeFeedNeedsActivePetNotice`); показываем в кадре, следующем за нажатием — из
	//   колбэка сети нотисы не показываем (закон `no-reentrant-net-callbacks`).
	if (UAsdaPetSubsystem* P = Pets())
	{
		if (P->ConsumeFeedNeedsActivePetNotice())
		{
			NoticeMsgBox(PetCageLoc::kFeedNeedCall);   // 101105
		}
	}
	DrainPetEvents();

	// Отложенная постановка дочернего окна рядом с клеткой (дефект №4) — разбор у определения.
	TickPendingBeside();

	// ★ ВОЛНА 6: стенд отправки. Перехват ГЛУШИТ всю исходящую сеть, поэтому снимается сам —
	//   на ближайшем тике после снятия тела либо по таймауту (образец — стенд 6148 волны 4).
	if (bStandArmed)
	{
		StandLeft -= DeltaTime;
		if (bStandDone || StandLeft <= 0.f)
		{
			if (!bStandDone)
			{
				UE_LOG(LogAsdaPetCage, Error,
					TEXT("[питомец-окно] СТЕНД %d: тело НЕ СНЯТО за отведённое время — пакет из очереди не ушёл ")
					TEXT("(проверьте гейты: для 6108 «из сумки» нужен ПРИЗВАННЫЙ питомец)"), StandOpcode);
			}
			if (UAsdaPetSubsystem* P = Pets()) { P->EndSendCapture(); }
			bStandArmed = false;
			bStandDone = false;
		}
	}

	// ★ Автопоказ плашки 43200 — порт `inventoryAutoUpdateThrottled2s@0xDCC170`
	//   (item_inventory.c:54624-54646): раз в 2000 мс, если призванный есть, а плашка спрятана.
	// ★ [ИСПРАВЛЕНИЕ] 08.11.2026 — разбор у `CVarStatusReviveClosed`: снятое чужим `CloseAll` окно = спрятанное.
	if (bStatusShown && CVarStatusReviveClosed.GetValueOnGameThread() != 0)
	{
		UAsdaWindowSubsystem* WMs = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (WMs && !WMs->IsWindowLive(Status))
		{
			bStatusShown = false;
			bStatusReady = false;
			StatusPollLeft = 0.f;
			UE_LOG(LogAsdaPetCage, Log, TEXT("[питомец-окно] плашка 43200 снята со вьюпорта мимо скрытия (CloseAll) — признак показа сброшен, опрос сейчас"));
		}
	}
	StatusPollLeft -= DeltaTime;
	if (StatusPollLeft <= 0.f)
	{
		StatusPollLeft = kStatusPollSec;
		UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		const bool bGateCraft = WM && WM->IsWindowVisible(kGateCraftWindow);
		const bool bGateQty   = WM && WM->IsWindowVisible(kGateQtyWindow);
		if (!bGateCraft && !bGateQty && Active() != nullptr && !bStatusShown)
		{
			OpenStatusPanel();
		}
		// Призванного не стало — плашку гасим (эталон делает это в приёмниках 6101/6107/6111).
		if (Active() == nullptr && bStatusShown) { HideStatusPanel(); }
	}

	Mouse.Update();
	if (bStatusShown && bStatusReady) { TickStatusIcon(); }
	if (!bCageOpen || !bCageReady) { return; }
	TickPreview();
	TickSlots();
	TickTooltips();
}

// ═══════════════ ПРЕВЬЮ 43052 И ЕГО ВРАЩЕНИЕ (волна 9) — шапка в `.h` ════════════════════════
AAsdaPetPreviewActor* UAsdaPetCageSubsystem::EnsurePreview()
{
	if (Preview) { return Preview; }
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return nullptr; }
	FActorSpawnParameters P;
	P.ObjectFlags |= RF_Transient;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// Далеко от игрока: сцена превью в кадр игры попадать не должна, а капчер смотрит только
	//   на свой список ShowOnly (образец расстановки — гача, `AsdaPremiumGachaSubsystem.cpp`).
	Preview = W->SpawnActor<AAsdaPetPreviewActor>(AAsdaPetPreviewActor::StaticClass(),
		FVector(0.f, 0.f, 200000.f), FRotator::ZeroRotator, P);
	if (!Preview)
	{
		UE_LOG(LogAsdaPetCage, Error, TEXT("[питомец-окно] не удалось поднять сцену превью 43052"));
	}
	return Preview;
}

UMaterialInstanceDynamic* UAsdaPetCageSubsystem::GetPreviewBrush() const
{
	return Preview ? Preview->GetSceneBrushMaterial() : nullptr;
}

UTextureRenderTarget2D* UAsdaPetCageSubsystem::GetPreviewRenderTarget() const
{
	// Диагностика — разбор у объявления в `.h`.
	return Preview ? Preview->GetRenderTarget() : nullptr;
}

void UAsdaPetCageSubsystem::SetPreviewFacingYaw(float Deg)
{
	// Подбор числа порта - разбор у объявления команды `asda.Pet.PreviewFaceYaw`.
	if (!Preview) { return; }
	Preview->FacingYawDeg = Deg;
	Preview->ResetYaw();   // сброс применяет новую базу к модели немедленно
	UE_LOG(LogAsdaPetCage, Display, TEXT("[питомец-окно] базовый разворот превью ← %.0f град"), Deg);
}

UMaterialInterface* UAsdaPetCageSubsystem::GetPreviewBrushMaster() const
{
	// Разбор — у объявления в `.h`: одна схема подстановки кисти на оба плана превью.
	return Preview ? Preview->GetSceneBrushMaster() : nullptr;
}

FVector2D UAsdaPetCageSubsystem::GetPreviewRenderSize() const
{
	// Размер кадра — разбор у объявления в `.h`. Ноль, пока сцена не поднята: общая точка
	//   `ApplySceneMaterialBrush` на нулевой размер просто не трогает `ImageSize` кисти.
	return Preview ? Preview->GetRenderSize() : FVector2D::ZeroVector;
}

void UAsdaPetCageSubsystem::SetPreviewPet(int32 PetTypeId, const FString& PetName)
{
	if (AAsdaPetPreviewActor* Pv = EnsurePreview())
	{
		Pv->SetPet(PetTypeId, PetName);
	}
}

FString UAsdaPetCageSubsystem::DumpPreview() const
{
	FString S = TEXT("\n═══════════ ПРЕВЬЮ 43052: состояние ═══════════\n");
	if (!Preview)
	{
		S += TEXT("  сцена превью НЕ поднята (окно 43000 ни разу не открывалось)\n");
		return S;
	}
	const UTextureRenderTarget2D* Rt = Preview->GetRenderTarget();
	S += FString::Printf(TEXT("  сцена: есть, порода в кадре=%d\n"), Preview->GetPetTypeId());
	S += FString::Printf(TEXT("  буфер: %s (эталон 240x170, `npc_mob_pet.c:5594`)\n"),
		Rt ? *FString::Printf(TEXT("%dx%d"), Rt->SizeX, Rt->SizeY) : TEXT("НЕТ"));
	// ★★★ 24.09.2026: ЧТО В КАДРЕ НА САМОМ ДЕЛЕ. До этой строки дамп говорил только о НАМЕРЕНИЯХ
	//   («сцена есть», «кисть подставлена»), и дефект «превью пустое» дожил до владельца именно
	//   поэтому. Теперь печатаем прочитанные с GPU пиксели; разбор метода — у
	//   `AAsdaPreviewSceneActor::DescribeFrameCoverage`.
	S += FString::Printf(TEXT("  ★ КАДР: %s\n"), *Preview->DescribeFrameCoverage());
	// Жива ли сама модель. Проверка `IsValid`, а не «указатель не пуст»: уничтоженный актор
	//   остаётся ненулевым до сборки мусора и врал бы «модель на месте».
	S += FString::Printf(TEXT("  модель в сцене: %s\n"),
		Preview->IsPetModelAlive() ? TEXT("жива") : TEXT("★ НЕТ (в кадре будет пусто)"));
	// * Анимация: позиция клипа между двумя вызовами обязана вырасти — иначе модель стоит в позе
	//   привязки («Т-поза»), а это ровно то, на что пожаловался владелец.
	S += FString::Printf(TEXT("  анимация: %s\n"), *Preview->DescribePetClip());
	S += FString::Printf(TEXT("  план 43052=%s, кисть подставлена=%s, держит 43500=%s\n"),
		ImgPreview3d ? TEXT("есть") : TEXT("НЕТ"),
		bPreviewBrushSet ? TEXT("да") : TEXT("нет"),
		bEvoUsesPreview ? TEXT("да") : TEXT("нет"));
	// ★★★ 24.09: ЧТО ЛЕЖИТ В КИСТИ НА САМОМ ДЕЛЕ. Признак «кисть подставлена» говорит лишь о том,
	//   что мы ВЫЗВАЛИ `SetBrushResourceObject`, а не о том, что контрол рисует наш кадр. Ровно
	//   такое «да» и увело волну 9 в сторону. Печатаем объект кисти и то, что материал реально
	//   держит в параметре `SceneColor`: если там не наш render-target, контрол покажет ТЕКСТУРУ
	//   ПО УМОЛЧАНИЮ этого параметра (у движка это `DefaultTexture`).
	if (ImgPreview3d)
	{
		const UObject* Res = ImgPreview3d->GetBrush().GetResourceObject();
		S += FString::Printf(TEXT("  объект кисти контрола: %s (%s)\n"),
			Res ? *Res->GetName() : TEXT("НЕТ"),
			Res ? *Res->GetClass()->GetName() : TEXT("-"));
		// ★★★ 24.09 (проверка гачи): UV-РЕГИОН КИСТИ. У гачи контейнер 68056 носил скин `NoImage_icon`
		//   с МИКРО-КУСКОМ атласа (`UV = "0.994 0.000 … 1.000 0.004"`), и `SetBrushResourceObject`
		//   этот регион НЕ СНИМАЕТ — материал сцены сэмплился одной точкой пустого угла
		//   кадра, отчего «автоматов не было видно». Там это лечится пересборкой браша
		//   с `SetUVRegion(ForceInit)` (`AsdaPremiumGachaSubsystem::ApplySceneBrush`). Здесь браш НЕ
		//   пересобирается, поэтому регион надо ВИДЕТЬ: валидный не-единичный регион =
		//   та же беда, что была у гачи.
		const FSlateBrush& Bp = ImgPreview3d->GetBrush();
		const FBox2f Uvp = Bp.GetUVRegion();
		S += FString::Printf(
			TEXT("  ★ UV-регион кисти 43052: валиден=%d [%.4f %.4f]-[%.4f %.4f] draw_as=%d тинт=%s размер=%.0fx%.0f\n"),
			Uvp.bIsValid ? 1 : 0, Uvp.Min.X, Uvp.Min.Y, Uvp.Max.X, Uvp.Max.Y,
			(int32)Bp.DrawAs.GetValue(), *Bp.TintColor.GetSpecifiedColor().ToString(),
			Bp.ImageSize.X, Bp.ImageSize.Y);
	}
	if (UMaterialInstanceDynamic* Mid = const_cast<UMaterialInstanceDynamic*>(Preview->GetSceneBrushMaterial()))
	{
		UTexture* Tex = nullptr;
		const bool bGot = Mid->GetTextureParameterValue(FMaterialParameterInfo(TEXT("SceneColor")), Tex);
		S += FString::Printf(TEXT("  материал кисти: %s, параметр SceneColor: %s%s\n"),
			*Mid->GetName(),
			bGot ? (Tex ? *Tex->GetName() : TEXT("пусто")) : TEXT("★ ПАРАМЕТРА НЕТ"),
			(bGot && Tex && Tex == Preview->GetRenderTarget()) ? TEXT(" (это наш буфер — верно)")
			                                                   : TEXT(" ★ ЭТО НЕ НАШ БУФЕР"));
		// ★★★ ЧТЕНИЕ СВОЕГО ЖЕ ПРИСВОЕНИЯ НИЧЕГО НЕ ДОКАЗЫВАЕТ (закон `false-negatives-method`):
		//   `UMaterialInstanceDynamic::SetTextureParameterValue` кладёт запись в свой список ДАЖЕ
		//   ПРИ ИМЕНИ, КОТОРОГО У МАТЕРИАЛА-РОДИТЕЛЯ НЕТ, и чтение вернёт её обратно. Поэтому
		//   спрашиваем ВТОРЫМ МЕТОДОМ — у САМОГО МАТЕРИАЛА, какие параметры он объявляет.
		TArray<FMaterialParameterInfo> TexInfos, ScalarInfos;
		TArray<FGuid> Guids;
		Mid->GetAllTextureParameterInfo(TexInfos, Guids);
		Mid->GetAllScalarParameterInfo(ScalarInfos, Guids);
		FString Names;
		for (const FMaterialParameterInfo& I : TexInfos) { Names += FString::Printf(TEXT("тек:%s "), *I.Name.ToString()); }
		for (const FMaterialParameterInfo& I : ScalarInfos)
		{
			float V = 0.f;
			Mid->GetScalarParameterValue(I, V);
			Names += FString::Printf(TEXT("скаляр:%s=%.2f "), *I.Name.ToString(), V);
		}
		S += FString::Printf(TEXT("  ПАРАМЕТРЫ САМОГО МАТЕРИАЛА: %s\n"), Names.IsEmpty() ? TEXT("нет ни одного") : *Names);
	}
	else
	{
		S += TEXT("  материал кисти: ★ НЕ СОЗДАН\n");
	}
	// ★ Угол печатаем И в радианах (как у эталона), И в градусах — иначе прогон не прочесть.
	S += FString::Printf(TEXT("  УГОЛ ВРАЩЕНИЯ: %.4f рад = %.2f град (множитель эталона %.2f рад/пиксель)\n"),
		Preview->GetYawRad(), FMath::RadiansToDegrees(Preview->GetYawRad()),
		AAsdaPetPreviewActor::kRadPerPixel);
	return S;
}

void UAsdaPetCageSubsystem::HoldPreviewForEvolution(bool bHold)
{
	bEvoUsesPreview = bHold;
	// Гейт рендера эталона: «открыто 43000 ИЛИ открыт диалог развития» (`npc_mob_pet.c:5591`).
	if (AAsdaPetPreviewActor* Pv = EnsurePreview())
	{
		Pv->SetCaptureEnabled(bHold || bCageOpen);
	}
}

void UAsdaPetCageSubsystem::TickPreview()
{
	AAsdaPetPreviewActor* Pv = EnsurePreview();
	if (!Pv) { return; }

	// Показать выбранного в списке. Эталон выбирает модель превью так же: при открытом диалоге
	//   развития — его цель, иначе — выбранного/призванного (`entity_char.c:7790-7800`).
	if (const FAsdaPetSlot* Sel = Selected())
	{
		Pv->SetPet(Sel->PetIndex, Sel->Name);
	}
	else if (!bEvoUsesPreview && CVarPreviewClearNoSelection.GetValueOnGameThread() != 0)
	{
		// ★ [ИСПРАВЛЕНИЕ] 09.11.2026, пачка 20 баг 6 — разбор у `CVarPreviewClearNoSelection`.
		Pv->ClearPet();
	}

	// ★★★ ДОВОДКА КАДРА ПРЕВЬЮ ПО ПИКСЕЛЯМ (дефект владельца №2). Зовётся отсюда, потому что у
	//   самого актора превью тик ВЫКЛЮЧЕН намеренно (иначе модель уничтожалась бы без хозяина —
	//   разбор у `AAsdaPetPreviewActor::SetPet`). Сама функция работает только пока взведены
	//   попытки после смены породы и молча выходит в остальное время.
	Pv->TickAutoFit();

	// Кисть ставим ОДИН раз: материал переживает кадры, а `SetBrushResourceObject` каждый кадр
	//   заставлял бы Slate пересобирать кисть впустую.
	if (!bPreviewBrushSet && ImgPreview3d)
	{
		// ★★★ ПРИЧИНА ДЕФЕКТА ВЛАДЕЛЬЦА «ПРЕВЬЮ ПУСТОЕ», ЧАСТЬ ВТОРАЯ — UV-РЕГИОН КИСТИ.
		//   Первая часть (модель уничтожалась на первом тике) вылечена в `AAsdaPetPreviewActor`.
		//   Но даже с живой моделью кадр до экрана не доходил, и виноват был НЕ материал:
		//   контрол 43052 носит скин-АТЛАС `NoImage_icon`, у которого план указывает микро-кусок
		//   `N_01.tga` — замер живого прогона дал `uv [0.9940 0.0000]-[1.0000 0.0040]`. Импортёр
		//   кладёт это в браш, а `SetBrushResourceObject(материал)` UV-регион НЕ СНИМАЕТ ⇒ материал
		//   сэмплился одной точкой ПУСТОГО правого верхнего угла кадра (там фон, альфа 255), и при
		//   `Opacity = 1 − A` контрол выходил абсолютно прозрачным. Игрок видел задник `PetCage_BG`
		//   (узел 138) и считал, что превью не работает.
		//   ⚠ Это ОБЩАЯ грабля скинов-атласов, а не частный случай: тем же болел контейнер 68056
		//   гачи (вылечен 18.08) и болеет 43576 окна 43500. Тело лечения ОДНО — `AsdaUi::ApplySceneMaterialBrush`.
		UMaterialInterface* Master = Pv->GetSceneBrushMaster();
		UTextureRenderTarget2D* Rt = Pv->GetRenderTarget();
		if (Master && Rt)
		{
			// Кисть собирается заново И тут же отдаёт экземпляр материала виджета — порядок вшит
			//   в общую точку, переставить его местами уже нельзя (поправка ревизии, СРЕДН-8).
			if (UMaterialInstanceDynamic* Own = AsdaUi::ApplySceneMaterialBrush(
					ImgPreview3d, Master, FVector2D(Rt->SizeX, Rt->SizeY), TEXT("превью 43052")))
			{
				Own->SetTextureParameterValue(TEXT("SceneColor"), Rt);
				bPreviewBrushSet = true;
				UE_LOG(LogAsdaPetCage, Log,
					TEXT("[питомец-окно] превью 43052: кисть кадра собрана заново (экземпляр «%s», буфер %dx%d)"),
					*Own->GetName(), Rt->SizeX, Rt->SizeY);
			}
			else
			{
				UE_LOG(LogAsdaPetCage, Error,
					TEXT("[питомец-окно] превью 43052: виджет не отдал свой экземпляр материала — кадр рисовать нечем"));
			}
		}
	}

	// ★★★ ЛКМ-ВРАЩЕНИЕ (П8-1). Взвод — нажатие НАД контролом превью (эталонное событие 0xB по
	//   43052, `item_enhance.c:4011-4016`); снятие — простое отсутствие зажатой ЛКМ, потому что
	//   отдельного обработчика отпускания у эталона НЕТ (`entity_char.c:61892`).
	// *** ЧЕРЕДОВАНИЕ КЛИПОВ ПОКОЯ (поправка ревизии, КРИТ-2). Сам актор превью не тикает —
	//   тик выключен намеренно, — поэтому покадровый шаг клипов зовёт окно. Разбор эталона и
	//   почему трек 2 в нашей сцене недостижим — у `AAsdaPetPreviewActor::TickIdleClips`.
	Pv->TickIdleClips(GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f);

	const bool bOverPreview = ImgPreview3d && AsdaUi::IsCursorOverWidget(ImgPreview3d);
	Pv->TickRotation(Mouse.bLmb, Mouse.bLmbDown && bOverPreview, Mouse.Cursor.X);
}
