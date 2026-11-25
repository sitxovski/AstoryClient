// Copyright (c) 2026 @sitxovski (Discord)
// ★ 23.10.2026 РАЗНОС ФАЙЛА (слово владельца «огромные файлы разделить … с гарантией 100 %»): методы AAsdaLoginGameMode
//   разнесены по группам в AsdaLoginGameMode_{Soul,Create,Server}.cpp; общее (LogAsdaLogin, чистые константы и хелперы) —
//   в AsdaLoginGameModeInternal.h. Строки перенесены БАЙТ В БАЙТ:
//   `python Tools/asda_split_cpp.py verify --plan Reports/split_2026-10-23/AsdaLoginGameMode.plan.json`.
//   Здесь осталось ядро: StartPlay, шкатулка и перелёт камеры, вход в мир, режим XyT, окно выбора персонажа.
#include "Login/AsdaLoginGameMode.h"
#include "Npc/AsdaMobFxMaterial.h"   // ★ 01.11.2026 (mob_fx_audit): Custom Depth прозрачного задника
#include "Resources/AsdaTextureSubsystem.h"
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10/F16): пути мастеров, имена параметров, экранные ассеты
#include "Resources/AsdaSoundSubsystem.h"
#include "Resources/AsdaModelAnimSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaAdminSubsystem.h"   // сброс прав GM при возврате в меню логина
#include "Login/AsdaLoginData.h"
#include "AsdaConnection.h"           // ★ 16.10.2026: FAsdaConnection::PrefetchHost — фоновый резолв домена заранее
// ★ Аудит №4 (A8): `Login/AsdaGameText.h` больше не нужен — строка помощника теперь
//   `FAsdaLoginhelpertextRow` из `Items/AsdaItemDbRows.h` (тянется через AsdaItemTableSubsystem.h).
#include "Blueprint/WidgetTree.h"    // GetAllWidgets — поиск план-виджетов по префиксу (имена с idx-суффиксом)
#include "LevelSequenceActor.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Components/EditableText.h"
#include "Components/RichTextBlock.h"   // [САМОТЕСТ] 08.10.2026: чтение пузыря ANI_TXT в asda.Login.CreateNameTest
// ── Ctrl+N-пикер разрешения/качества (dev) ──
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"       // FWidgetPath::ContainsWidget — маппинг focus-события на наш контрол
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "AsdaNetSubsystem.h"
#include "Net/ChannelCipher.h"   // ★ профиль ключа канального шифра (наш / XyT)
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"                 // FScreenshotRequest (движковый захват вместо RDP CopyFromScreen)
#include "Styling/SlateBrush.h"       // FSlateBrush — рантайм-фикс рамки пузыря
#include "GameFramework/PlayerController.h"
#include "Animation/SkeletalMeshActor.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "AsdaEffectActor.h"
#include "EngineUtils.h"
#include "Engine/BlueprintGeneratedClass.h"    // ★ 31.10.2026: признак Blueprint-наследника света
#include "Login/AsdaPossessScene.h"   // ★ «одержимость»: правила парных клипов + ловец кликов по сцене
#include "World/AsdaPlayerCharacter.h" // ★ общая точка kAsdaCharPreviewBaseM/F (база пути превью-скелета)
#include "World/AsdaLoadManagerSubsystem.h" // ★ charsel_preload: фоновая предзагрузка превью экрана выбора
#include "PipelineStateCache.h"             // ★ charsel_preload: IsPSOPrecachingEnabled — прогрев PSO только где он есть
#include "AsdaLoginGameModeInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaLoginGameMode*.cpp (LogAsdaLogin, чистые хелперы)
// ★ дедуп №2 В2-C (F9): AsdaTranslucentSlots::MakeSwapMid — общая MID-подмена слота   ★ R3-c: комментарий снятого включения "World/AsdaFadeComponent.h" (части им не пользуются)
// ★ масштаб вьюпорта для раскладки стартовой заставки   ★ R3-c: комментарий снятого включения "Blueprint/WidgetLayoutLibrary.h" (части им не пользуются)
// ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы с `quit`, итог, выход)   ★ R3-c: комментарий снятого включения "Test/AsdaSelfTestKit.h" (части им не пользуются)
// ★ ActiveBoneIndices LOD0 — кости с весами для коробки пика «одержимости»   ★ R3-c: комментарий снятого включения "Rendering/SkeletalMeshRenderData.h" (части им не пользуются)
#include "AsdaLoginGameMode_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaLoginGameMode*.cpp

DEFINE_LOG_CATEGORY(LogAsdaLogin);   // ★ 23.10.2026 разнос: было DEFINE_LOG_CATEGORY_STATIC; объявление — AsdaLoginGameModeInternal.h (один экземпляр на все части)

// ★ Клипы ПОВОРОТА карусели-шкатулки (эталон BuildMotionPath@0xF9F870): в нашем GLB anim_06..08 = lturn
//   слот 0/1/2, anim_09..11 = rturn слот 0/1/2. curSlot выбирает конкретный клип (стартовая ориентация подставки).
//   Путь строится в StartCasketTurn через FString::Printf(...anim_%02d..., ClipIdx).
static const TCHAR* GBackdropAnimPath =
	TEXT("/Game/AsdaAssets/Models/Char/cre_creback-a_00/SkeletalMeshes/cre_creback-a_00_Anim");

AAsdaLoginGameMode::AAsdaLoginGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AAsdaLoginGameMode::StartPlay()
{
	Super::StartPlay();

	// ★★★ ПЕРВЫМ ДЕЛОМ — стартовая заставка. Всё, что ниже (коннект, атласы, построение UI),
	//   происходит уже ПОД ней, поэтому игрок не видит ни сборки экрана, ни чёрного кадра.
	// ★ 10.10.2026: уровень перезагружен ради языка, выбранного в окне первого запуска (`ApplyLiveAndReloadLogin`), —
	//   заставку игрок уже видел в этом процессе, второй раз не показываем.
	//   ★ 12.10.2026: и после возврата с карты замера первого запуска (`UAsdaBenchSubsystem`) — заставку уже видели.
	UAsdaLangSelectSubsystem* LangSelEarly = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaLangSelectSubsystem>() : nullptr;
	UAsdaBenchSubsystem* BenchEarly = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaBenchSubsystem>() : nullptr;
	const bool bSkipLogoLang = LangSelEarly && LangSelEarly->ConsumeSkipStartLogo();
	const bool bSkipLogoBench = BenchEarly && BenchEarly->ConsumeSkipStartLogo();
	if (!bSkipLogoLang && !bSkipLogoBench)
	{
		ShowStartLogo();
	}

	// ★★★ 12.10.2026 (владелец: «музыки нет при окне выбора языка — инициализация аудио раньше, окно не блокирует»):
	//   музыка логина стартует ЗДЕСЬ, первым делом, под заставкой и окном языка, а не вместе с экраном логина.
	//   Эталон CSoundStream_PlayMusic("Login.mp3", loop=1) state 4/9; фейд-ин 16 кадров ≈ 0.53 с. Если трек уже играет
	//   (уровень перезагружен после выбора языка / возврат с карты замера — стрим persist переживает травел), не трогаем:
	//   иначе он начинался бы заново с fade-in (ревизия МЕЛК-3 10.10).
	if (UAsdaSoundSubsystem* SoundMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		static const FName kLoginTrack(TEXT("Login"));
		if (SoundMgr->IsMusicPlaying() && SoundMgr->GetMusicTrack() == kLoginTrack)
		{
			UE_LOG(LogAsdaLogin, Log, TEXT("[логин] музыка Login уже играет (пережила смену уровня)"));
		}
		else
		{
			SoundMgr->PlayMusic(kLoginTrack, /*bLoop*/true, /*FadeSec*/0.533f);
			UE_LOG(LogAsdaLogin, Log, TEXT("[логин] музыка Login запущена"));
		}
	}

	// ★ Dev-меню настроек (Ctrl+N) теперь ОБЩЕЕ: UAsdaDevMenuSubsystem (GameInstance) регистрирует
	//   Slate-перехват один раз и работает на ВСЕХ картах (логин/мир) — здесь ничего не регистрируем.

	// ★★ 16.10.2026: хост — домен сервера, порт (и group/name) — из АССЕТА DT_ServerList. IP таблицы, сохранённый адрес
	//   и поле ввода не читаются (решение владельца). Прежний сохранённый адрес удаляется из настроек UE.
	if (FAsdaLoginData::PurgeLegacyServerIp())
	{
		UE_LOG(LogAsdaLogin, Log, TEXT("Сервер: прежний сохранённый адрес ([AsdaLogin] ServerIP) удалён из настроек"));
	}
	FAsdaLoginData::LoadLoginServer(Server);
	UE_LOG(LogAsdaLogin, Log, TEXT("Сервер: '%s' %s:%d (%s) [хост — домен, порт — DT_ServerList]"),
		*Server.Group, *Server.IP, Server.Port, *Server.Name);
	// Фоновый резолв домена — сейчас, до раннего коннекта: подключение возьмёт готовый адрес без ожидания DNS.
	FAsdaConnection::PrefetchHost(Server.IP);
	// ★ РАННИЙ коннект к логин-серверу (эталон netConnectLoginServer@0x973040) — если порт известен (DT_ServerList
	//   загружен; хост — всегда домен). Нет порта → коннекта нет, вход откажет с причиной (DoLoginFromWidget).
	// ★★★ ИСПРАВЛЕНИЕ (аудит 2026-09-02, К-4): коннект ПЕРЕНЕСЁН из StartPlay в первый Tick.
	//   Было: коннект стоял здесь, то есть ДО возврата из StartPlay — до первого тика и первого кадра.
	//   Заставка уже создана, но ещё не отрисована, поэтому любая задержка коннекта выглядела как
	//   «клиент замер и даже не пытается соединиться» (жалоба владельца). Теперь первый кадр успевает
	//   выйти на экран, а коннект уходит следующим тиком — сам коннект вдобавок ограничен по времени
	//   (asda.Net.ConnectTimeoutSec). Порядок «коннект задолго до 1003» сохранён: между первым тиком и
	//   вводом логина проходят секунды, барьер connect→1003 не затрагивается.
	bEarlyConnectPending = Server.IsValid();

	// ★★★ [ПОРТ] 09.10.2026 (мультиязычность): язык ещё не выбран — модальное окно первого выбора поверх логина
	//   (English / Русский / العربية-неактивна).
	//   ★ 10.10.2026 (требование владельца: выбор применяется СРАЗУ): окно показывается ДО сборки экрана логина, а сам
	//   экран, прогрев атласов и окна логина откладываются до выбора — тогда ни один атлас с надписями не загружен
	//   окнами, переживающими уровень (реестр окон живёт на GameInstance), и после выбора `UAsdaLangSelectSubsystem`
	//   перезагружает уровень: атласы грузятся уже нужной культуры (L10N), экран логина строится на выбранном языке.
	//   ★ 12.10.2026 (владелец): окно выбора появлялось ПОД стартовой заставкой и было видно сквозь её растворение.
	//   Теперь при показанной заставке окно открывается в момент её снятия (`TickStartLogo`), а экран логина
	//   по-прежнему отложен до выбора. Без заставки — сразу, как раньше.
	UAsdaLangSelectSubsystem* LangSel = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaLangSelectSubsystem>() : nullptr;
	bool bLangFirstRun = LangSel && LangSel->NeedsFirstRun();
	bLangSelectAfterStartLogo = false;
	if (bLangFirstRun)
	{
		if (StartLogoWidget)
		{
			bLangSelectAfterStartLogo = true;
			UE_LOG(LogAsdaLogin, Log, TEXT("[язык] язык не выбран — окно первого выбора откроется после снятия заставки; экран логина отложен"));
		}
		else
		{
			// Окно не загрузилось (нет ассета) — язык спросить нечем, экран логина показываем обычным путём.
			bLangFirstRun = LangSel->ShowFirstRunIfNeeded();
		}
	}

	// ★★★ [ПОРТ] 12.10.2026 АВТОПОДБОР ГРАФИКИ ПРИ ПЕРВОМ ЗАПУСКЕ (`UAsdaBenchSubsystem`, решение владельца: замер настоящего
	//   кадра с окном хода и применением по «ОК»). Язык ещё не выбран — замер запустит подсистема языка после выбора
	//   (`ApplyLiveAndReloadLogin`); язык выбран — после снятия заставки (`TickStartLogo`), без заставки — сразу.
	//   Экран логина отложен: после замера уровень логина загружается заново, и этот StartPlay строит его обычным путём.
	UAsdaBenchSubsystem* Bench = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaBenchSubsystem>() : nullptr;
	bool bBenchFirstRun = !bLangFirstRun && Bench && Bench->ShouldRunFirstRun();
	bBenchAfterStartLogo = false;
	if (bBenchFirstRun)
	{
		if (StartLogoWidget)
		{
			bBenchAfterStartLogo = true;
			UE_LOG(LogAsdaLogin, Log, TEXT("[замер] первый запуск — замер графики начнётся после снятия заставки; экран логина отложен"));
		}
		else
		{
			bBenchFirstRun = Bench->StartFirstRun(GetWorld());   // не запустился — экран логина обычным путём
			// ★ Ревизия КРИТ-1: иначе первый Tick поднял бы ранний коннект уже ПОСЛЕ отключения замера — открытый сокет на
			//   карте замера и анти-реплей-счётчик, сбитый вето-отправками. Режим логина после возврата поднимет свежий.
			if (bBenchFirstRun) { bEarlyConnectPending = false; }
		}
	}

	// Прогрев атласов, оригинальный Login.JUI-UI на UMG (WBP_LoginScreen) и музыка логина — `PresentLoginScreen()`;
	//   при открытом окне первого выбора языка — после выбора (уровень перезагружается); при замере — после возврата.
	if (!bLangFirstRun && !bBenchFirstRun)
	{
		PresentLoginScreen();
	}

	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		PC->bShowMouseCursor = true;
		// ★ Меню-экраны (логин/сервер/char-select) = чистый UI → UIOnly надёжно роутит клики в UMG
		//   (GameAndUI над 3D-вьюпортом клики по кнопкам терял). При входе в мир (P2) переключим на GameAndUI.
		FInputModeUIOnly Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
	}
	// Тот же режим — как БАЗОВЫЙ для менеджера окон (его восстанавливает RefreshModalInput после закрытия модалок).
	if (UAsdaWindowSubsystem* WinMgr0 = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WinMgr0->SetBaseInputMode(/*bWorldGameAndUI*/false);
	}
	// ★ Права администратора живут ТОЛЬКО в пределах игровой сессии: возврат в меню логина их гасит
	//   (вместе с окнами GM и свободной камерой). См. UAsdaAdminSubsystem.
	if (UAsdaAdminSubsystem* Adm = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaAdminSubsystem>() : nullptr)
	{
		Adm->ResetSession();
	}

	// ★ Курсор как в оригинале (Cursor_LoadAll@0xEC8AB0): idle/дефолт = BO.cur ("nomal"→индекс 0). Полный
	//   маппинг состояний (battle=AO/talk=TK/hand=ID/loot=HO/hammer=HM/…) — для in-world (P2). BO hotspot=(0,0).
	//   .cur лежат в Content/Slate/Cursors/ (SetHardwareCursor ищет по пути без расширения, пробует .ani/.cur/.png).
	UWidgetBlueprintLibrary::SetHardwareCursor(this, EMouseCursor::Default,
		FName(TEXT("Slate/Cursors/BO")), FVector2D(0.f, 0.f));
	UWidgetBlueprintLibrary::SetHardwareCursor(this, EMouseCursor::Hand,
		FName(TEXT("Slate/Cursors/BO")), FVector2D(0.f, 0.f));   // ховер кнопок = тот же idle (оригинал не меняет в UI)

	// Актёры сцены логина (по имени SkeletalMesh-ассета: label в -game недоступен).
	for (TActorIterator<ASkeletalMeshActor> It(GetWorld()); It; ++It)
	{
		USkeletalMeshComponent* C = It->GetSkeletalMeshComponent();
		USkeletalMesh* M = C ? C->GetSkeletalMeshAsset() : nullptr;
		if (!M) continue;
		const FString N = M->GetName();
		if (N.Contains(TEXT("cre_creation01")))     { CasketActor = *It; }
		else if (N.Contains(TEXT("cre_creback")))   { BackdropActor = *It; }
	}
	// ★ 01.11.2026 (mob_fx_audit) [ИСПРАВЛЕНИЕ] задник создания/выбора: подсетки с битом очереди (MIC MI_*_blend, мастер
	//   M_AsdaMobFxBlend — в АССЕТАХ) полупрозрачные с «записью глубины» оригинала; здесь только флаг Custom Depth мешей сцены
	//   (материалы не меняются, уровень не меняется).
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		It->ForEachComponent<UMeshComponent>(false, [](UMeshComponent* C) { AsdaMobFx::ApplyDepth(C); });
	}
	// Эффект-акторы char-select (стоят в уровне скрытыми): eft_creation01=эффект шкатулки; eft_creation02/03/04=
	//   глифы пустых слотов 0/1/2. Различаем по EffectName (place-скрипты пометили).
	GlyphActors.SetNum(3);
	for (TActorIterator<AAsdaEffectActor> It(GetWorld()); It; ++It)
	{
		const FString EN = It->EffectName;
		if      (EN.Contains(TEXT("creation02"))) { GlyphActors[0] = *It; }
		else if (EN.Contains(TEXT("creation03"))) { GlyphActors[1] = *It; }
		else if (EN.Contains(TEXT("creation04"))) { GlyphActors[2] = *It; }
		else if (EN.Contains(TEXT("creation01"))) { CasketEffectActor = *It; }
	}
	// ★ Гарантии видимости eft_creation01 (свечение форджинга «между крышкой и основанием»):
	//   1) Кулл-дистанция: 1e9 «на всякий случай». ★ 12.09.2026 — в ×1-сцене запас перестал быть
	//      обязательным (камера char-select ~800 UE от шкатулки, глоу-фаза ~1700 против дефолтных
	//      10 000), но и вреда не приносит; в ×100-сцене без него эффект куллился ВСЕГДА (было
	//      80 000 и 170 000 UE). Глифам place-скрипт ставит 1e9 сам — держим max, дизайнерское
	//      бо́льшее значение не трогаем.
	//   2) МИРОВОЙ масштаб: place-скрипт ставит актор с трансформом шкатулки-АКТОРА, а масштаб шкатулки
	//      несёт масштаб СЦЕНЫ (в ×1 это 0.01: меш шкатулки запечён glTF-импортом в ×100). Геометрия
	//      .jem задана в ИГРОВЫХ единицах (диски X±435, Y±368, Z 32..127 — ровно между основанием и
	//      крышкой, крышки Y≈63..88), поэтому мировой масштаб эффекта должен равняться масштабу сцены
	//      GLoginScale, иначе диски либо пылинка, либо накрывают полсцены.
	//      Верты уже в UE-осях (asda_jem_parse: UE=(y,x,z)); мешам базой служит трансформ актора.
	if (AAsdaEffectActor* CasketFx = Cast<AAsdaEffectActor>(CasketEffectActor))
	{
		CasketFx->CullDistance = FMath::Max(CasketFx->CullDistance, 1.0e9f);
		const FVector S = CasketFx->GetActorScale3D();
		if (S.GetAbsMax() < 2.f && CasketFx->GetRootComponent())
		{
			// ★ ЗНАК скейла СОХРАНЯЕМ: актор шкатулки в уровне ЗЕРКАЛЬНЫЙ (scale.X=−1) — эффект скопировал его
			//   трансформ, и лучи авторски совпадают с силуэтом только в ТОЙ ЖЕ хиральности. Голый
			//   SetWorldScale3D(+100) снимал зеркало → лучи не по силуэту («ромбы» мимо шкатулки).
			// ★ 12.09.2026: было 100.f (×100-сцена). Геометрия .jem задана в ИГРОВЫХ единицах, поэтому
			//   мировой масштаб эффекта = масштаб сцены: GLoginScale (см. комментарий у константы).
			CasketFx->GetRootComponent()->SetWorldScale3D(FVector(
				GLoginScale * FMath::Sign(S.X != 0.f ? S.X : 1.f),
				GLoginScale * FMath::Sign(S.Y != 0.f ? S.Y : 1.f),
				GLoginScale * FMath::Sign(S.Z != 0.f ? S.Z : 1.f)));
		}
	}
	UE_LOG(LogAsdaLogin, Log, TEXT("Эффекты char-select: шкатулка=%s глифы=%d/%d/%d"),
		CasketEffectActor ? TEXT("OK") : TEXT("НЕТ"),
		GlyphActors[0] ? 1 : 0, GlyphActors[1] ? 1 : 0, GlyphActors[2] ? 1 : 0);
	UE_LOG(LogAsdaLogin, Log, TEXT("Сцена: задник=%s шкатулка=%s"),
		BackdropActor ? TEXT("OK") : TEXT("НЕТ"), CasketActor ? TEXT("OK") : TEXT("НЕТ"));

	SetupFlightSequence();         // сцена в ДАЛЬНЕЙ позе якоря (frame0 секвенции LS_LoginFlight; фолбэк — массив)

	// Анимации стартуем СЛЕДУЮЩИМ тиком (anim-инстансы компонентов создаются в BeginPlay —
	// вызовы из StartPlay теряются): задник = луп сцена-клипа, шкатулка = клип _0 кадр 0 ПАУЗА (закрыта).
	GetWorld()->GetTimerManager().SetTimerForNextTick([this]()
	{
		// Эталон: задник анимируется КАЖДЫЙ кадр (animSetTrackValueByIndex(&renderCtxA,0,dt) стр.539955).
		if (BackdropActor)
		{
			if (USkeletalMeshComponent* B = BackdropActor->GetSkeletalMeshComponent())
			{
				if (UAnimSequence* A = LoadClip(GBackdropAnimPath))
				{
					B->SetAnimationMode(EAnimationMode::AnimationSingleNode);
					B->SetAnimation(A);
					B->SetPlayRate(1.f);
					B->Play(true);   // луп
				}
			}
		}
		FreezeCasketClosed();
	});

	// Подписка: 1004 (логин) → скрыть логин/показать server-select; 1006 (char-list) → char-select (перелёт+открытие).
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnLoginResult.AddUObject(this, &AAsdaLoginGameMode::OnLoginResult);
			// ★ 4101 (кик login-сервера) и обрыв соединения — отдельный путь эталона (не статус 1004).
			Net->OnLoginKick.AddUObject(this, &AAsdaLoginGameMode::OnLoginKick);
			Net->OnCharListReady.AddUObject(this, &AAsdaLoginGameMode::OnCharListReady);
			// ★ 1019 (результат УДАЛЕНИЯ) — подписка СРАЗУ (не лениво, как create: первый 1019 терялся,
			//   когда create-окно ещё не открывалось → «удаление срабатывало со второго раза»).
			Net->OnCharDeleteResult.AddUObject(this, &AAsdaLoginGameMode::OnCharDeleteResult);
			// 1013 (счётчики онлайна каналов) → обновить бары UI реальными данными (если сервер их шлёт).
			Net->OnChannelListReady.AddUObject(this, &AAsdaLoginGameMode::OnChannelOnlineUpdated);
			// ★ ФАЗА A4: 1021 получен → скрыть логин-сцену + загрузочный экран (мир грузится, коннект к game-серверу идёт).
			Net->OnEnterWorld.AddUObject(this, &AAsdaLoginGameMode::OnEnterWorld);
			// ★★★ «ОДЕРЖИМОСТЬ». 1025 закрывает транзакцию 1005 — только после него набор персонажей
			//   соулмейта полон (1024 приходит РАНЬШЕ 1023, оба нужны). Подписка сразу, как у 1019:
			//   пакет приходит ДО показа char-select, и ленивая подписка его бы потеряла.
			Net->OnSoulmateSharedCharsUpdated.AddUObject(this, &AAsdaLoginGameMode::OnSoulmateSharedCharsUpdated);
			// Отказ логин-сервера на выбор персонажа (1027 ≠ 1): код 2 = соулмейт сейчас в игре.
			Net->OnCharSelectRefused.AddUObject(this, &AAsdaLoginGameMode::OnCharSelectRefused);
		}
	}

	// ★ ВРЕМЕННО (визуальная проверка ПОЛНОЙ последовательности БЕЗ сервера): ASDA_TEST_FLOW=1 → через 3с
	//   запустить char-select как после логина+канала: перелёт → шкатулка ОТКРЫВАЕТСЯ → и ТОЛЬКО потом персонаж.
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_TEST_FLOW")) == TEXT("1"))
	{
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, [this]()
		{
			// Как реальный путь после логина: спрятать UI логина (иначе оверлей перекрывает 3D-сцену).
			if (LoginWidget) { LoginWidget->SetVisibility(ESlateVisibility::Collapsed); }
			// ★ РЕГРЕСС-ТЕСТ КРАША char-select в билде (ASDA_TEST_GC=1): принудительный GC перед сборкой
			//   превью. Именно GC ронял билд — ассеты, лежащие в кэшах БЕЗ GC-рута (UPROPERTY/TWeakObjectPtr),
			//   собирались, а кэш продолжал отдавать висячий указатель (DT_hairtable → GetRowNames → падение).
			//   Хук воспроизводит это детерминированно, не дожидаясь штатного 61-секундного цикла GC.
			if (FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_TEST_GC")) == TEXT("1") && GEngine)
			{
				UE_LOG(LogAsdaLogin, Warning, TEXT("[ТЕСТ] Принудительный GC перед char-select"));
				GEngine->ForceGarbageCollection(true);
			}
			OnCharListReady();
		}, 3.0f, false);
	}

	// ★ ВРЕМЕННО (проверка UMG-логина end-to-end): ASDA_TEST_UMGLOGIN=1 → заполнить поле пароля (ASDA_TEST_PW)
	//   и пройти путь ВИДЖЕТА (DoLoginFromWidget) — проверить, что клик OK → сеть работает.
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_TEST_UMGLOGIN")) == TEXT("1"))
	{
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, [this]()
		{
			if (LoginWidget)
			{
				FString Pw = FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_TEST_PW"));
				if (Pw.IsEmpty()) { Pw = TEXT("1"); }
				if (UEditableText* E = Cast<UEditableText>(LoginWidget->GetWidgetFromName(TEXT("LOGIN_EDIT01"))))
					E->SetText(FText::FromString(Pw));
				// ★ Тест ПУТИ КНОПКИ (не DoLoginFromWidget напрямую): broadcast OnClicked стилизованной кнопки
				//   → релей → DoLoginFromWidget. Подтверждает, что клик по стилизованной OK доходит.
				if (UButton* Ok = Cast<UButton>(LoginWidget->GetWidgetFromName(TEXT("LOGIN_CONFORM"))))
				{
					UE_LOG(LogAsdaLogin, Log, TEXT("[TEST] broadcast LOGIN_CONFORM.OnClicked (делегатов=%d)"),
						Ok->OnClicked.IsBound() ? 1 : 0);
					Ok->OnClicked.Broadcast();
				}
				else { UE_LOG(LogAsdaLogin, Warning, TEXT("[TEST] LOGIN_CONFORM не найден/не UButton")); }
			}
		}, 3.0f, false);
	}

	// ★ ВРЕМЕННО (проверка UMG-логина): ASDA_TEST_LOGINUI=1 → скриншот логин-экрана через 3с.
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_TEST_LOGINUI")) == TEXT("1"))
	{
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, [this]()
		{
			FScreenshotRequest::RequestScreenshot(TEXT("loginui_check"), true, false);
		}, 3.0f, false);
	}

	// ★ ВРЕМЕННО (проверка UI выбора сервера/канала без сервера): ASDA_TEST_SRVUI=1 (серверы) / =2 (каналы) → показать + скриншот.
	{
		const FString SrvUiMode = FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_TEST_SRVUI"));
		if (SrvUiMode == TEXT("1") || SrvUiMode == TEXT("2"))
		{
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, [this]()
		{
			if (LoginWidget) { LoginWidget->SetVisibility(ESlateVisibility::Collapsed); }
			ShowServerSelect();
			// ASDA_TEST_SRVUI=2 → сразу переключить на вид каналов (проверка числа каналов).
			if (FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_TEST_SRVUI")) == TEXT("2"))
			{
				ShowServerOrChannelView(true);
			}
			FTimerHandle S;
			GetWorldTimerManager().SetTimer(S, [this]()
			{
				FScreenshotRequest::RequestScreenshot(TEXT("srvui_check"), true, false);   // bShowUI=true → Slate-оверлей
			}, 2.0f, false);
		}, 2.0f, false);
		}
	}

	// ★ ВРЕМЕННО (проверка крипто-фикса без дисплея): ASDA_TEST_LOGIN=1 → авто-логин сохранённым ID + ASDA_TEST_PW.
	//   Цель — увидеть в логе, отвечает ли сервер опкодом 1004 (крипто ок), а не мусором 0x1005.
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_TEST_LOGIN")) == TEXT("1"))
	{
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, [this]()
		{
			FString Id, Pw;
			bool bSaved = false;
			FAsdaLoginData::LoadSavedId(bSaved, Id);
			if (Id.IsEmpty()) { Id = TEXT("sitxovski"); }
			Pw = FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_TEST_PW"));
			if (Pw.IsEmpty()) { Pw = TEXT("1"); }
			UE_LOG(LogAsdaLogin, Warning, TEXT("[TEST] авто-логин id='%s'"), *Id);
			DoLogin(Id, Pw);
		}, 3.0f, false);
	}
}

void AAsdaLoginGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterHelperFocus();   // снять подписку на смену фокуса Slate (иначе делегат зовёт уничтоженный this)
	// ★ Ловец кликов по 3D-сцене держит делегат на `this` — снимаем его РАНЬШЕ, чем умрёт режим,
	//   и убираем виджет с вьюпорта (он полноэкранный и ловит мышь).
	if (ScenePick)
	{
		ScenePick->OnSceneClicked.Unbind();
		ScenePick->RemoveFromParent();
		ScenePick = nullptr;
	}
	// ★ charsel_preload: уход с уровня логина — отпустить предзагрузку превью (актор прогрева умирает с миром).
	CharSelPsoWarmActor = nullptr;
	UAsdaTextureSubsystem::ReleaseAllFullMips(CharSelPreviewMips);
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>()) { LM->ReleaseCharSelectPreload(); }
	}
	Super::EndPlay(EndPlayReason);
}

void AAsdaLoginGameMode::FocusUI(UUserWidget* W)
{
	// Уступаем модальному окну (DIV-4): если открыто модальное — фокусом управляет оконный менеджер, не перебиваем.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		if (WM->IsAnyModalOpen()) { return; }
	}
	// UIOnly + ЯВНЫЙ фокус на текущий экран: без этого клики после смены виджета (логин→сервер→char-select)
	//   не доходят до нового виджета (фокус остаётся на старом/вьюпорте).
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		FInputModeUIOnly Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		// ★ 25.10.2026 (`asda.UI.ModalFocusNoRoot`): корень экрана фокус не принимает — фокус вьюпорту, куда Slate его и так отдавал
		//   (без ошибки «Attempting to focus Non-Focusable widget»); корень, принимающий фокус, — как прежде.
		if (W) { Mode.SetWidgetToFocus(AsdaUiOnlyFocusTarget(W)); }
		PC->SetInputMode(Mode);
		PC->bShowMouseCursor = true;
	}
}
