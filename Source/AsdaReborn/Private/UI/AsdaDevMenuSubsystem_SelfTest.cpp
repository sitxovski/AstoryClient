// Copyright (c) 2026 @sitxovski (Discord)
// AsdaDevMenuSubsystem_SelfTest.cpp — ★ R3-c 12.11.2026 (разрез AsdaDevMenuSubsystem.cpp, дословный перенос): самотесты дев-меню.
//   Общее для частей — в AsdaDevMenuSubsystem_Private.h; шапка и реверс — в AsdaDevMenuSubsystem.cpp.
#include "UI/AsdaDevMenuSubsystem.h"
#include "Login/AsdaButtonRelay.h"



#include "AsdaCloudFieldActor.h"                 // самопроверка clouds: состояние полей облаков и их теневых прокси
#include "AsdaSelfTestBatch.h"                   // ★ 11.10.2026: выход самотеста через `quit` движка (перехватывается пакетом asda.SelfTest.Batch)
#include "Test/AsdaSelfTestKit.h"                // ★ 11.10.2026 (В3 S1): разбор аргументов самотеста (`quit` не путается с числом/режимом)
#include "AsdaDevMenuSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaDevMenuSubsystem*.cpp

// (★ R3-c чистка 12.11.2026: перенесено из AsdaDevMenuSubsystem_Private.h — нужно только этой части.)
// ★ 12.10.2026: строки меню, пишущие консольный ключ, — таблица для режимов pins/rows самопроверки и проверки «пин без ручки».
//   Where: 0 — ключ персистится в [AsdaRender] (обязан быть в GRenderCVarKeys); 1 — поле профиля игрока
//   (UAsdaGameUserSettings::IsProfileOwnedCVar, в ini-секциях меню ключа быть не должно); 2 — штатное поле профиля
//   «масштаб рендера» (SetRenderScalePercent). bSlider — строка-ползунок (жест SelfTestSliderDrag вместо клика).
struct FAsdaDevMenuPinRow
{
	const TCHAR* Needle;   // подстрока подписи строки
	const TCHAR* CVar;     // консольный ключ строки
	int32 Where;           // куда пишется (см. выше)
	bool bSlider;          // ползунок (true) или кнопка-цикл (false)
};

// ★★★ Волна теней (08.09.2026): «строку сейчас держит меню, а не уровень». Нужна строкам, у которых нет
//   значения-сентинела вроде −1 (как у asda.Light.ShadowBudget): по флагу видно, снят оверрайд или нет.
static bool IsCVarSetByConsole(const TCHAR* Name)
{
	IConsoleVariable* CV = FindCVar(Name);
	return CV && (((uint32)CV->GetFlags() & ECVF_SetByMask) == ECVF_SetByConsole);
}

static const FAsdaDevMenuPinRow GPinRows[] = {
	{ TEXT("Масштаб рендера"),      TEXT("r.ScreenPercentage"),               2, false },
	{ TEXT("Метод сглаживания"),    TEXT("r.AntiAliasingMethod"),             1, false },
	{ TEXT("Резкость (Sharpen)"),   TEXT("r.Tonemapper.Sharpen"),             1, false },
	{ TEXT("Motion blur"),          TEXT("r.MotionBlur.Amount"),              1, false },
	{ TEXT("Дальность прорисовки"), TEXT("asda.Cull.Scale"),                  1, true  },
	{ TEXT("Плотность тумана"),     TEXT("asda.Fog.DensityScale"),            0, true  },
	{ TEXT("Дальность тумана"),     TEXT("asda.Fog.DistanceScale"),           0, true  },
	{ TEXT("3D-облака"),            TEXT("asda.Clouds.Field"),                1, false },
	{ TEXT("Рим:"),                 TEXT("asda.Rim.Enable"),                  0, false },
	{ TEXT("сила рима"),            TEXT("asda.Rim.Scale"),                   0, false },
	{ TEXT("Без рима"),             TEXT("asda.Rim.OriginalOff"),             0, false },
	{ TEXT("Капсульные тени тел"),  TEXT("asda.Shadow.Capsule"),              0, false },
	{ TEXT("сила пятна"),           TEXT("asda.Shadow.CapsuleMinVisibility"), 0, false },
};

// ───────────────────────────── Итог самопроверки (★ 12.10.2026) ─────────────────────────────
// Счётчики ok/fail всех режимов asda.DevMenu.SelfTest. Итог — одна строка «САМОТЕСТ-МЕНЮ ИТОГ: ok=N fail=M» через
//   AsdaSelfTestKit::Finish (образец — asda.Bench.SelfTest), её ловит пакетный прогон asda.SelfTest.Batch. Раньше режимы
//   кончались строкой «ЗАВЕРШЕНО» без вердикта, а ненайденная строка меню была лишь Warning (03_tester.md §3.3 Д1) — после
//   сокращения состава такой самотест молча «зеленел» бы на удалённых строках.
struct FAsdaDevMenuSelfTestTally
{
	int32 Ok = 0;     // пройденных проверок
	int32 Fail = 0;   // проваленных проверок
};
static FAsdaDevMenuSelfTestTally GDevMenuTally;

// Одна проверка: счётчик + строка лога (провал — Warning, чтобы был виден в сводке пакета).
static void DevMenuCheck(bool bOk, const FString& What)
{
	if (bOk) { ++GDevMenuTally.Ok;   UE_LOG(LogAsdaDevMenu, Log,     TEXT("САМОТЕСТ-МЕНЮ ✔ %s"), *What); }
	else     { ++GDevMenuTally.Fail; UE_LOG(LogAsdaDevMenu, Warning, TEXT("САМОТЕСТ-МЕНЮ ✘ ПРОВАЛ %s"), *What); }
}

// Есть ли ключ в секции GameUserSettings.ini (значение — в OutVal).
static bool DevMenuIniHas(const TCHAR* Section, const TCHAR* Key, FString* OutVal = nullptr)
{
	FString V;
	const bool bHas = GConfig && GConfig->GetString(Section, Key, V, GGameUserSettingsIni);
	if (OutVal) { *OutVal = bHas ? V : FString(); }
	return bHas;
}

// Печать проверяемых значений (CVar / MPC / туман) — общая для режимов toggle и verify.
//   ★ 12.10.2026: набор ключей — оставшиеся строки меню; печать пресетных ключей (GPresetManagedCVarKeys) сохранена —
//   по ней идёт приёмка пресетов без глаза (03_tester.md §1.3: SetBy Scalability у всех, любой Console = пин, который надо объяснить).
static void AsdaDevMenuLogState(const TCHAR* Tag)
{
	UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[%s]: r.AntiAliasingMethod=%d r.Tonemapper.Sharpen=%.2f r.MotionBlur.Amount=%.2f asda.Cull.Scale=%.3f asda.Fog.DensityScale=%.3f asda.Fog.DistanceScale=%.3f asda.Clouds.Field=%d asda.Shadow.Capsule=%d asda.Shadow.CapsuleMinVisibility=%.2f asda.Rim.Enable=%d asda.Rim.Scale=%.2f"),
		Tag, GetCVarInt(TEXT("r.AntiAliasingMethod")), GetCVarFloat(TEXT("r.Tonemapper.Sharpen")), GetCVarFloat(TEXT("r.MotionBlur.Amount")), GetCVarFloat(TEXT("asda.Cull.Scale"), 1.f),
		GetCVarFloat(TEXT("asda.Fog.DensityScale")), GetCVarFloat(TEXT("asda.Fog.DistanceScale")), GetCVarInt(TEXT("asda.Clouds.Field")), GetCVarInt(TEXT("asda.Shadow.Capsule")),
		GetCVarFloat(TEXT("asda.Shadow.CapsuleMinVisibility")), GetCVarInt(TEXT("asda.Rim.Enable")), GetCVarFloat(TEXT("asda.Rim.Scale")));
	// ★ E-0b: уровни и SetBy пер-уровневых ключей — проверка «Low реально Low» и персиста пресета после перезапуска (setE0_report.md).
	{
		UAsdaVideoSettingsSubsystem* V = nullptr;
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts()) { if (Ctx.OwningGameInstance) { V = Ctx.OwningGameInstance->GetSubsystem<UAsdaVideoSettingsSubsystem>(); if (V) { break; } } }
		UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[%s]: пресет=%s%s; %s; масштаб рендера %d %%"), Tag, V ? UAsdaVideoSettingsSubsystem::PresetName(V->GetQualityPreset()) : TEXT("?"),
			(V && V->IsQualityMixed()) ? TEXT(" (группы вразнобой)") : TEXT(""), *UAsdaVideoSettingsSubsystem::DescribeQualityLevels(), V ? V->GetRenderScalePercent() : -1);
		FString L;
		for (const TCHAR* K : GPresetManagedCVarKeys)
		{
			IConsoleVariable* CV = FindCVar(K);
			L += FString::Printf(TEXT(" %s=%s(%s)"), K, CV ? *CV->GetString() : TEXT("-"), CVarSetByName(CV));
		}
		UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[%s]: пресетные ключи:%s"), Tag, *L);
	}
	if (UMaterialParameterCollection* MPC = LoadObject<UMaterialParameterCollection>(nullptr, AsdaMat::kNormalsMpcPath))   // ★ В1-C F10: путь — та же константа, что у геттера
	{
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (!Ctx.World() || !Ctx.World()->IsGameWorld()) { continue; }
			FString L;
			for (const FName& P : GNormalParams) { L += FString::Printf(TEXT(" %s=%.2f"), *P.ToString(), UKismetMaterialLibrary::GetScalarParameterValue(Ctx.World(), MPC, P)); }
			UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[%s]: MPC_AsdaNormals [мир %s]:%s"), Tag, *Ctx.World()->GetName(), *L);
		}
	}
	for (TObjectIterator<UExponentialHeightFogComponent> It; It; ++It)
	{
		if (!*It || It->IsTemplate() || !It->GetWorld() || !It->GetWorld()->IsGameWorld()) { continue; }
		UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[%s]: туман %s FogDensity=%.5f"), Tag, *It->GetPathName(), It->FogDensity);
	}
}

// Состояние полей 3D-облаков всех игровых миров (самопроверка clouds: В-1/В-2 — скрытие, авторское, теневые прокси). Возвращает число полей.
static int32 AsdaDevMenuLogCloudFields(const TCHAR* Tag)
{
	int32 N = 0;   // сколько полей найдено
	for (TObjectIterator<AAsdaCloudFieldActor> It; It; ++It)
	{
		if (!*It || It->IsTemplate() || !It->GetWorld() || !It->GetWorld()->IsGameWorld()) { continue; }
		UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[%s]: облака %s"), Tag, *It->DescribeFieldState());
		++N;
	}
	UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[%s]: asda.Clouds.Field=%d, полей облаков: %d"), Tag, GetCVarInt(TEXT("asda.Clouds.Field"), 1), N);
	return N;
}

// Состояние тумана всех игровых миров (самопроверка fogdist): множители + StartDistance/FogDensity каждого компонента. Возвращает число компонентов.
static int32 AsdaDevMenuLogFog(const TCHAR* Tag)
{
	int32 N = 0;   // сколько компонентов тумана найдено
	for (TObjectIterator<UExponentialHeightFogComponent> It; It; ++It)
	{
		if (!*It || It->IsTemplate() || !It->GetWorld() || !It->GetWorld()->IsGameWorld()) { continue; }
		UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[%s]: туман %s StartDistance=%.1f FogDensity=%.5f"), Tag, *It->GetPathName(), It->StartDistance, It->FogDensity);
		++N;
	}
	UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[%s]: asda.Fog.DensityScale=%.2f DistanceScale=%.2f, компонентов тумана: %d"),
		Tag, GetCVarFloat(TEXT("asda.Fog.DensityScale"), 1.f), GetCVarFloat(TEXT("asda.Fog.DistanceScale"), 1.f), N);
	return N;
}

// Ключи режима reset (★ 12.10.2026: оставшиеся строки «Качество»/«Персонажи» — профильные и [AsdaRender]).
static const TCHAR* GDevMenuResetKeys[] = {
	TEXT("r.AntiAliasingMethod"), TEXT("r.Tonemapper.Sharpen"), TEXT("r.MotionBlur.Amount"), TEXT("asda.Cull.Scale"),
	TEXT("asda.Rim.Enable"), TEXT("asda.Rim.Scale"), TEXT("asda.Shadow.Capsule"), TEXT("asda.Shadow.CapsuleMinVisibility"),
	TEXT("asda.Rim.OriginalOff"),
};

// Состояние ключей сброса (самопроверка reset: В-3/В-5) — значение, кем выставлено, есть ли ключ в ini-секциях меню, дефолт проекта.
static void AsdaDevMenuLogResetState(const TCHAR* Tag)
{
	for (const TCHAR* K : GDevMenuResetKeys)
	{
		IConsoleVariable* CV = FindCVar(K);
		FString IniV, IniR;
		const bool bIniV = DevMenuIniHas(TEXT("AsdaVideo"), K, &IniV);
		const bool bIniR = DevMenuIniHas(TEXT("AsdaRender"), K, &IniR);
		FString Src;
		const FString Def = UAsdaDevMenuSubsystem::ProjectDefault(K, &Src);
		UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[%s]: %s=%s (SetBy %s) ini[AsdaVideo]=%s ini[AsdaRender]=%s профиль-ключ=%s проект=%s (%s)"),
			Tag, K, CV ? *CV->GetString() : TEXT("-"), CVarSetByName(CV), bIniV ? *IniV : TEXT("нет"), bIniR ? *IniR : TEXT("нет"),
			UAsdaGameUserSettings::IsProfileOwnedCVar(K) ? TEXT("да") : TEXT("нет"), Def.IsEmpty() ? TEXT("-") : *Def, *Src);
	}
	if (UGameUserSettings* GS = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[%s]: sg.TextureQuality=%d sg.PostProcessQuality=%d sg.ShadowQuality=%d overall=%d"),
			Tag, GS->GetTextureQuality(), GS->GetPostProcessingQuality(), GS->GetShadowQuality(), GS->GetOverallScalabilityLevel());
	}
}

// asda.DevMenu.SelfTest <задержкаСек> <shots|toggle|verify|rows|pins|reset|clouds|fogdist|slider|culldrag> [quit] [x y z pitch yaw [roll]]
//   (регистрация единственная в Source/, grep 05.09.2026 = 0). ★ 12.10.2026: все режимы считают ok/fail и кончаются строкой
//   «САМОТЕСТ-МЕНЮ ИТОГ: ok=N fail=M» (AsdaSelfTestKit::Finish); режим shadowrows удалён вместе со строками волны теней.
//   shots  — открыть панель, по очереди показать каждую вкладку и снять Shot showui (devmenu_tab<N>.png), напечатать ширины подписей;
//            ПРОВАЛ, если есть подписи шире поля (Р11: «шире поля: 0»);
//   toggle — переключить набор строк через те же обработчики, что и кнопки, и напечатать состояние (персист проверяется verify
//            после перезапуска); ПРОВАЛ, если подпись строки после клика не изменилась;
//   verify — напечатать состояние (после перезапуска: значения на месте?) + ПРОВАЛ на каждый устаревший пин в ini (SweepRetiredPins);
//   rows   — (★ 12.10.2026) инвентарь строк по вкладкам + ожидания нового состава: каждая ожидаемая есть, каждая убранная
//            отсутствует, у каждого ключа [AsdaRender] есть строка-ручка, подписей шире поля нет;
//   pins   — (★ 12.10.2026) для каждой строки с ключом: клик/жест → ключ лёг туда, куда пишет ApplyCVarPersist, и только туда →
//            имитация перезапуска (Unset + накат [AsdaRender]) → ResetTab всех вкладок: ключей нет, SetBy не Console;
//   reset  — (В-3/В-5) увести AA/резкость/размытие/силу рима кнопками панели, затем ResetTab(Качество)+ResetTab(Персонажи):
//            профильные поля «не менялось», ключей [AsdaRender] нет, SetBy не Console;
//   clouds — (В-1/В-2) камера BugItGo по числам после mode (кадр nightvalley-1 сета 0: -47025.9 18696.7 238 -10 17.859), кадры
//            devmenu_clouds_before/off/on.png при asda.Clouds.Field 1→0→1 + состояние полей/прокси в лог (тени прокси должны
//            исчезать при 0 и возвращаться при 1). Ключ ставится тем же CVar-путём, что и кнопка (без записи ini);
//   fogdist— (★ ползунок дальности тумана) камера BugItGo по числам после mode, кадры devmenu_fogdist_near/far.png при
//            asda.Fog.DistanceScale 0.5 (туман ближе) и 2.0 (туман дальше) + StartDistance/FogDensity компонентов в лог;
//   slider — (В-1/М-1) консоль ставит asda.Fog.DistanceScale 1.7 → ручка и подпись совпали; «Сбросить: Небо и туман» → ручка на
//            дефолте проекта; OnValueChanged без захвата → отложенный персист в [AsdaRender];
//   culldrag—(★★★ ПЕРЕТАСКИВАНИЕ ползунка «Дальность прорисовки», ревизия review_drawdistance.md §6) полный жест мышью
//            по кадрам: захват ручки → 25 значений → отпускание. ПРОВАЛ, если значение не доехало до мира или проход на каждый кадр.
// Число полных проходов мира и номер кадра на момент начала жеста culldrag — важен ПРИРОСТ, не итог.
// ★ Кадр считаем ЧЕСТНО (GFrameCounter), а не по числу запланированных шагов: на офлайн-стенде кадр бывает по 5-20 с,
//   и весь жест укладывается в один тик — «шагов 26» ничего не сказало бы о частоте проходов.
static int32 GAsdaCullDragPassesBefore = 0;
static uint64 GAsdaCullDragFrameBefore = 0;

// Проверка «отпущенный ползунок доехал до мира». Ждёт РЕАЛЬНЫХ тиков: применение отложено throttle-ограничителем
// (UAsdaCullSettings::ApplyThrottleSec), а на офлайн-стенде между двумя тиками проходят секунды — одной фиксированной
// паузы мало. TriesLeft попыток с интервалом 0.5 с; первое совпадение = успех, иначе честный ПРОВАЛ.
static void AsdaCullDragVerify(int32 TriesLeft, TFunction<void()> Done)
{
	UAsdaDevMenuSubsystem* M = FindDevMenuSubsystem();
	UWorld* W = (M && M->GetGameInstance()) ? M->GetGameInstance()->GetWorld() : nullptr;
	UAsdaCullDistanceSubsystem* CS = W ? W->GetSubsystem<UAsdaCullDistanceSubsystem>() : nullptr;
	const float Scale = GetCVarFloat(TEXT("asda.Cull.Scale"), 1.f);
	const float Applied = CS ? CS->LastAppliedScale : -1.f;
	const bool bOk = FMath::IsNearlyEqual(Scale, Applied, 0.01f);
	if (!bOk && TriesLeft > 0)
	{
		AsdaDevMenuAfter(0.5f, [TriesLeft, Done]() { AsdaCullDragVerify(TriesLeft - 1, Done); });
		return;
	}
	const int32 Passes = UAsdaCullDistanceSubsystem::GetApplyPassCount() - GAsdaCullDragPassesBefore;
	const int64 Frames = (int64)(GFrameCounter - GAsdaCullDragFrameBefore);
	DevMenuCheck(bOk, FString::Printf(TEXT("culldrag: asda.Cull.Scale=%.2f, применено подсистемой %.2f — значение доехало до мира"), Scale, Applied));
	DevMenuCheck(Frames <= 2 || Passes < (int32)Frames, FString::Printf(TEXT("culldrag: полных проходов мира за жест %d при %lld реальных кадрах — проход НЕ на каждый кадр (throttle)"), Passes, Frames));
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команды в Shipping нет
	if (GEngine && W) { GEngine->Exec(W, TEXT("asda.Cull.Dump")); }
#endif
	if (Done) { Done(); }
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommand CmdAsdaDevMenuSelfTest(
	TEXT("asda.DevMenu.SelfTest"),
	TEXT("asda.DevMenu.SelfTest <задержкаСек> <shots|toggle|verify|rows|pins|reset|clouds|fogdist|slider|culldrag> [quit] [x y z pitch yaw [roll]] — самопроверка dev-меню Ctrl+N путём игрока; итог «САМОТЕСТ-МЕНЮ ИТОГ: ok=N fail=M»"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		// ★ 11.10.2026 (В3 S1): разбор каркаса — `quit` в любой позиции не становится режимом/задержкой (`… 8 quit` прежде давал режим «quit»).
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		const float Delay = KA.Float(0, 5.f);
		const FString Mode = KA.Str(1, TEXT("shots")).ToLower();
		const bool bQuit = KA.bQuit;
		TArray<float> Cam;   // clouds/fogdist: x y z pitch yaw [roll] — числовые аргументы после режима
		for (int32 i = 2; i < KA.Pos.Num(); ++i) { if (KA.Pos[i].IsNumeric()) { Cam.Add(FCString::Atof(*KA.Pos[i])); } }
		GDevMenuTally = FAsdaDevMenuSelfTestTally();   // счётчики — с нуля на каждый запуск
		// ★ 12.10.2026: итог — единая строка ИТОГ через каркас самотестов (видна пакету asda.SelfTest.Batch), затем `quit` при bQuit.
		auto Quit = [bQuit, Mode]()
		{
			UAsdaDevMenuSubsystem* M = FindDevMenuSubsystem();
			UWorld* W = (M && M->GetGameInstance()) ? M->GetGameInstance()->GetWorld() : nullptr;
			AsdaSelfTestKit::Finish(W, TEXT("asda.DevMenu.SelfTest"), bQuit, LogAsdaDevMenu, TEXT("САМОТЕСТ-МЕНЮ"), GDevMenuTally.Ok, GDevMenuTally.Fail,
				*FString::Printf(TEXT(" (режим %s)"), *Mode));
		};
		AsdaDevMenuAfter(Delay, [Mode, Quit, Cam]()
		{
			UAsdaDevMenuSubsystem* S = FindDevMenuSubsystem();
			if (!S) { DevMenuCheck(false, TEXT("подсистема dev-меню не найдена")); Quit(); return; }
			if (Mode == TEXT("verify"))
			{
				AsdaDevMenuLogState(TEXT("verify"));
				// ★ 12.10.2026: устаревших пинов в ini быть не должно — чистка при старте их снимает; каждый найденный = ПРОВАЛ.
				const int32 Ghost = SweepRetiredPins(TEXT("AsdaVideo"), false) + SweepRetiredPins(TEXT("AsdaRender"), false) + SweepRetiredNormals(false);
				DevMenuCheck(Ghost == 0, FString::Printf(TEXT("verify: устаревших пинов в [AsdaVideo]/[AsdaRender]/[AsdaNormals]: %d"), Ghost));
				Quit();
				return;
			}
			if (Mode == TEXT("rows"))
			{
				S->SelfTestRows();
				Quit();
				return;
			}
			if (Mode == TEXT("pins"))
			{
				S->SelfTestPins([Quit]() { Quit(); });
				return;
			}
			if (Mode == TEXT("clouds"))
			{
				UWorld* W = S->GetGameInstance() ? S->GetGameInstance()->GetWorld() : nullptr;
				APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
				if (PC && Cam.Num() >= 5)
				{
					const FString Go = FString::Printf(TEXT("BugItGo %.1f %.1f %.1f %.3f %.3f %.1f"), Cam[0], Cam[1], Cam[2], Cam[3], Cam[4], Cam.Num() > 5 ? Cam[5] : 0.f);
					PC->ConsoleCommand(Go);
					UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[clouds]: %s"), *Go);
				}
				else { UE_LOG(LogAsdaDevMenu, Warning, TEXT("SelfTest[clouds]: камера не задана/нет контроллера (аргументов %d) — кадр с места спавна"), Cam.Num()); }
				auto SetField = [](int32 V) { if (IConsoleVariable* CV = FindCVar(TEXT("asda.Clouds.Field"))) { CV->Set(V, ECVF_SetByConsole); } };
				// Кадр + состояние полей; ожидание — ключ в заданном состоянии и хотя бы одно поле облаков в мире.
				auto Shot = [](const TCHAR* Name, int32 WantField)
				{
					const int32 N = AsdaDevMenuLogCloudFields(Name);
					DevMenuCheck(N > 0, FString::Printf(TEXT("clouds: полей облаков в мире %d (%s)"), N, Name));
					DevMenuCheck(GetCVarInt(TEXT("asda.Clouds.Field"), 1) == WantField, FString::Printf(TEXT("clouds: asda.Clouds.Field=%d (ждали %d, %s)"), GetCVarInt(TEXT("asda.Clouds.Field"), 1), WantField, Name));
					FScreenshotRequest::RequestScreenshot(Name, /*bShowUI*/false, /*bAddFilenameSuffix*/false);
				};
				// ★ Ветер полей — в ноль на время прогона: тень облака дрейфует 800 юн/с и за 5 с уходит с места сама
				//   (нулевая гипотеза 05.09: без единого переключения кадр «до»→«через 5 с» светлел на 5.5 ед.) — иначе
				//   кадры до/0/1 несравнимы. Только рантайм прогона, ассеты не трогаются.
				for (TObjectIterator<AAsdaCloudFieldActor> It; It; ++It)
				{
					if (!*It || It->IsTemplate() || !It->GetWorld() || !It->GetWorld()->IsGameWorld()) { continue; }
					It->WindSpeed = 0.f;
					UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[clouds]: %s WindSpeed=0 (стоп дрейфа на время прогона)"), *It->GetName());
				}
				const int32 Field0 = GetCVarInt(TEXT("asda.Clouds.Field"), 1);
				AsdaDevMenuAfter(8.0f,  [Shot, Field0]() { Shot(TEXT("devmenu_clouds_before"), Field0); });   // 8 с после телепорта: VSM/TSR устоялись
				AsdaDevMenuAfter(9.0f,  [SetField]() { SetField(0); });
				AsdaDevMenuAfter(14.0f, [Shot]() { Shot(TEXT("devmenu_clouds_off"), 0); });
				AsdaDevMenuAfter(15.0f, [SetField]() { SetField(1); });
				AsdaDevMenuAfter(20.0f, [Shot]() { Shot(TEXT("devmenu_clouds_on"), 1); });
				AsdaDevMenuAfter(21.5f, [Quit]() { Quit(); });
				return;
			}
			if (Mode == TEXT("fogdist"))
			{
				UWorld* W = S->GetGameInstance() ? S->GetGameInstance()->GetWorld() : nullptr;
				APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
				if (PC && Cam.Num() >= 5)
				{
					const FString Go = FString::Printf(TEXT("BugItGo %.1f %.1f %.1f %.3f %.3f %.1f"), Cam[0], Cam[1], Cam[2], Cam[3], Cam[4], Cam.Num() > 5 ? Cam[5] : 0.f);
					PC->ConsoleCommand(Go);
					UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[fogdist]: %s"), *Go);
				}
				else { UE_LOG(LogAsdaDevMenu, Warning, TEXT("SelfTest[fogdist]: камера не задана/нет контроллера (аргументов %d) — кадр с места спавна"), Cam.Num()); }
				// Ставим ключ тем же CVar-путём, что и ползунок (ECVF_SetByConsole → колбэк OnAsdaFogChanged пересчитает туман).
				auto SetDist = [](float V) { if (IConsoleVariable* CV = FindCVar(TEXT("asda.Fog.DistanceScale"))) { CV->Set(V, ECVF_SetByConsole); } };
				// Кадр + состояние тумана; ожидание — множитель применён и хотя бы один компонент тумана в мире.
				auto ShotFog = [](const TCHAR* Name, float Want)
				{
					const int32 N = AsdaDevMenuLogFog(Name);
					DevMenuCheck(N > 0, FString::Printf(TEXT("fogdist: компонентов тумана в мире %d (%s)"), N, Name));
					DevMenuCheck(FMath::IsNearlyEqual(GetCVarFloat(TEXT("asda.Fog.DistanceScale"), 1.f), Want, 0.01f), FString::Printf(TEXT("fogdist: asda.Fog.DistanceScale=%.2f (ждали %.2f, %s)"), GetCVarFloat(TEXT("asda.Fog.DistanceScale"), 1.f), Want, Name));
					FScreenshotRequest::RequestScreenshot(Name, /*bShowUI*/false, /*bAddFilenameSuffix*/false);
				};
				AsdaDevMenuAfter(3.0f,  [SetDist]() { SetDist(0.5f); });                        // туман ближе
				AsdaDevMenuAfter(6.0f,  [ShotFog]() { ShotFog(TEXT("devmenu_fogdist_near"), 0.5f); });
				AsdaDevMenuAfter(7.0f,  [SetDist]() { SetDist(2.0f); });                        // туман дальше
				AsdaDevMenuAfter(10.0f, [ShotFog]() { ShotFog(TEXT("devmenu_fogdist_far"), 2.0f); });
				AsdaDevMenuAfter(11.5f, [Quit]() { Quit(); });
				return;
			}
			if (Mode == TEXT("slider"))
			{
				// В-1/М-1 (review_devmenu_fog_slider.md): панель открыта на «Небо и туман»; консоль ставит asda.Fog.DistanceScale 1.7 →
				// ручка и подпись должны совпасть; «Сбросить: Небо и туман» → ручка на дефолте проекта; затем OnValueChanged БЕЗ захвата
				// (как с клавиатуры) → через 0.6 с ключ должен появиться в [AsdaRender] (отложенный персист).
				if (!S->IsMenuOpen()) { S->ToggleMenu(); }
				S->SetActiveTab(EAsdaDevTab::Sky);
				auto Shot = [](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(Name, /*bShowUI*/true, /*bAddFilenameSuffix*/false); };
				auto Exec = [](const TCHAR* Cmd)
				{
					UAsdaDevMenuSubsystem* M = FindDevMenuSubsystem();
					UWorld* W = (M && M->GetGameInstance()) ? M->GetGameInstance()->GetWorld() : nullptr;
					if (GEngine && W) { GEngine->Exec(W, Cmd); }
					UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[slider]: консоль «%s»"), Cmd);
				};
				AsdaDevMenuAfter(1.0f, []() { if (UAsdaDevMenuSubsystem* M = FindDevMenuSubsystem()) { M->SelfTestLogSliders(TEXT("slider-before")); } });
				AsdaDevMenuAfter(1.5f, [Exec]() { Exec(TEXT("asda.Fog.DistanceScale 1.7")); });
				AsdaDevMenuAfter(2.5f, [Shot]()
				{
					if (UAsdaDevMenuSubsystem* M = FindDevMenuSubsystem()) { DevMenuCheck(M->SelfTestLogSliders(TEXT("slider-170")) == 0, TEXT("slider: после консоли 1.7 ручки всех ползунков совпадают с CVar")); }
					DevMenuCheck(FMath::IsNearlyEqual(GetCVarFloat(TEXT("asda.Fog.DistanceScale"), 1.f), 1.7f, 0.01f), TEXT("slider: asda.Fog.DistanceScale=1.7 после консоли"));
					Shot(TEXT("devmenu_slider_170"));
				});
				AsdaDevMenuAfter(3.5f, []() { if (UAsdaDevMenuSubsystem* M = FindDevMenuSubsystem()) { M->ResetTab(EAsdaDevTab::Sky); } });
				AsdaDevMenuAfter(4.5f, [Shot]()
				{
					if (UAsdaDevMenuSubsystem* M = FindDevMenuSubsystem()) { DevMenuCheck(M->SelfTestLogSliders(TEXT("slider-reset")) == 0, TEXT("slider: после сброса вкладки ручки совпадают с CVar")); }
					const FString Def = UAsdaDevMenuSubsystem::ProjectDefault(TEXT("asda.Fog.DistanceScale"));
					const float Have = GetCVarFloat(TEXT("asda.Fog.DistanceScale"), 1.f);
					DevMenuCheck(!Def.IsEmpty() && FMath::IsNearlyEqual(FCString::Atof(*Def), Have, 0.01f), FString::Printf(TEXT("slider: после сброса asda.Fog.DistanceScale=%.3f = дефолт проекта «%s»"), Have, *Def));
					DevMenuCheck(!DevMenuIniHas(TEXT("AsdaRender"), TEXT("asda.Fog.DistanceScale")), TEXT("slider: после сброса ключа asda.Fog.DistanceScale в [AsdaRender] нет"));
					Shot(TEXT("devmenu_slider_reset"));
				});
				// Клавиатурный путь: OnValueChanged без захвата (bSliderCaptured=false) у ползунка дальности тумана → отложенный персист.
				//   ★ 12.10.2026: подстрока «Дальность тумана» (не «Дальность» — та цепляла и ползунок «Дальность прорисовки» вкладки «Качество»).
				AsdaDevMenuAfter(5.5f, []() { if (UAsdaDevMenuSubsystem* M = FindDevMenuSubsystem()) { M->SelfTestSliderKeyboard(DmT(TEXT("Дальность тумана"), TEXT("Fog distance")), 0.6f); } });   // ★ 11.11.2026 (D4b): подпись на языке сессии — на EN ползунок не находился, записи в ini не было
				AsdaDevMenuAfter(7.0f, [Quit]()
				{
					FString V;
					const bool bHas = DevMenuIniHas(TEXT("AsdaRender"), TEXT("asda.Fog.DistanceScale"), &V);
					DevMenuCheck(bHas, FString::Printf(TEXT("slider-kbd: отложенный персист — ini [AsdaRender] asda.Fog.DistanceScale=%s"), bHas ? *V : TEXT("<нет>")));
					if (UAsdaDevMenuSubsystem* M = FindDevMenuSubsystem()) { M->ResetTab(EAsdaDevTab::Sky); }   // не оставлять пин стенду
					Quit();
				});
				return;
			}
			if (Mode == TEXT("culldrag"))
			{
				// ★★★ ПЕРЕТАСКИВАНИЕ ползунка «Дальность прорисовки» (ревизия review_drawdistance.md §6). Проверяются ТРИ вещи:
				//   1) throttle: за N кадров перетаскивания полных проходов мира должно быть ≈ длительность/ApplyThrottleSec,
				//      а не по проходу на кадр (счётчик UAsdaCullDistanceSubsystem::GetApplyPassCount);
				//   2) значение ДОЕХАЛО: после отпускания LastAppliedScale совпадает с asda.Cull.Scale и с положением ручки;
				//   3) кадр не проваливается: печатается максимальный шаг времени за время жеста.
				//   Шаги идут по одному на кадр (AsdaDevMenuAfter с шагом 0.05 с) — иначе жест не воспроизводится.
				if (!S->IsMenuOpen()) { S->ToggleMenu(); }
				S->SetActiveTab(EAsdaDevTab::Quality);
				const int32 Steps = 24;          // 24 шага × 0.05 с = 1.2 с перетаскивания вправо (ползунок 0.5…2.0)
				const float StepSec = 0.05f;
				// Вернуть стенду после жеста: поле профиля DrawDistanceScale (−1 = «не менялось» → ClearRenderOverride, иначе прежнее число).
				const UAsdaGameUserSettings* P0 = UAsdaGameUserSettings::Get();
				const float DrawBefore = P0 ? P0->DrawDistanceScale : -1.f;
				// Снимок «до»: проходы мира, значение ключа, положение ручки
				AsdaDevMenuAfter(0.5f, []()
				{
					UAsdaDevMenuSubsystem* M = FindDevMenuSubsystem();
					if (M) { M->SelfTestLogSliders(TEXT("culldrag-before")); }
					GAsdaCullDragPassesBefore = UAsdaCullDistanceSubsystem::GetApplyPassCount();
					GAsdaCullDragFrameBefore = GFrameCounter;   // ★ честный счёт кадров: на стенде кадр бывает по 5-20 с
					UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[culldrag]: ДО — asda.Cull.Scale=%.2f, полных проходов мира %d, кадр %llu"),
						GetCVarFloat(TEXT("asda.Cull.Scale"), 1.f), GAsdaCullDragPassesBefore, GFrameCounter);
				});
				// Сам жест: захват → 24 значения от 0.0 до 1.0 → отпускание на 1.0 (asda.Cull.Scale = 2.0)
				for (int32 i = 0; i <= Steps; ++i)
				{
					const float T = (float)i / (float)Steps;
					const int32 Phase = (i == 0) ? 0 : 1;
					AsdaDevMenuAfter(1.0f + i * StepSec, [T, Phase]()
					{
						if (UAsdaDevMenuSubsystem* M = FindDevMenuSubsystem()) { M->SelfTestSliderDrag(TEXT("Дальность прорисовки"), T, Phase); }
					});
				}
				AsdaDevMenuAfter(1.0f + (Steps + 1) * StepSec, []()
				{
					if (UAsdaDevMenuSubsystem* M = FindDevMenuSubsystem()) { M->SelfTestSliderDrag(TEXT("Дальность прорисовки"), 1.f, 2); }
				});
				// Итог: значение доехало до мира? throttle сработал? Проверка ЖДЁТ реальных тиков (см. AsdaCullDragVerify).
				AsdaDevMenuAfter(1.0f + (Steps + 2) * StepSec, [Quit, DrawBefore]()
				{
					if (UAsdaDevMenuSubsystem* M = FindDevMenuSubsystem()) { DevMenuCheck(M->SelfTestLogSliders(TEXT("culldrag-after")) == 0, TEXT("culldrag: ручка ползунка после отпускания совпадает с CVar")); }
					AsdaCullDragVerify(/*TriesLeft*/8, [Quit, DrawBefore]()
					{
						// Вернуть стенду прежний множитель тем же путём, что и ползунок (поле профиля игрока).
						UAsdaDevMenuSubsystem* M = FindDevMenuSubsystem();
						UAsdaVideoSettingsSubsystem* V = (M && M->GetGameInstance()) ? M->GetGameInstance()->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr;
						if (V)
						{
							if (DrawBefore < 0.f) { V->ClearRenderOverride(TEXT("asda.Cull.Scale"), /*bSave*/true); }
							else                  { V->SetRenderOverride(TEXT("asda.Cull.Scale"), FString::SanitizeFloat(DrawBefore), /*bSave*/true); }
						}
						AsdaDevMenuAfter(0.5f, [Quit]() { Quit(); });
					});
				});
				return;
			}
			if (Mode == TEXT("reset"))
			{
				AsdaDevMenuLogResetState(TEXT("before"));
				if (!S->IsMenuOpen()) { S->ToggleMenu(); }
				// Уводим от дефолтов теми же обработчиками, что и кнопки: AA → следующий метод, резкость → следующая, размытие вкл/выкл, сила рима ×.
				//   ★ 12.10.2026: прежние строки «анизотропия», «Дальность динамических теней», «Bloom» убраны из меню — их тут больше нет.
				const Scalability::FQualityLevels LevelsBefore = Scalability::GetQualityLevels();   // ResetTab(Quality) ставит пресет по умолчанию — вернуть стенду
				UAsdaVideoSettingsSubsystem* V = S->GetGameInstance() ? S->GetGameInstance()->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr;
				const int32 GlowBefore = V ? V->GetGlowLevel() : 4;
				TMap<FString, FString> Before;   // значения ключей до кликов
				for (const TCHAR* K : GDevMenuResetKeys) { IConsoleVariable* CV = FindCVar(K); Before.Add(K, CV ? CV->GetString() : FString()); }
				S->SelfTestClickRows({ TEXT("Метод сглаживания"), TEXT("Резкость (Sharpen)"), TEXT("Motion blur"), TEXT("сила рима") });
				AsdaDevMenuAfter(1.0f, [Quit, Before, LevelsBefore, GlowBefore]()
				{
					AsdaDevMenuLogResetState(TEXT("changed"));
					for (const TCHAR* K : { TEXT("r.AntiAliasingMethod"), TEXT("r.Tonemapper.Sharpen"), TEXT("r.MotionBlur.Amount"), TEXT("asda.Rim.Scale") })
					{
						IConsoleVariable* CV = FindCVar(K);
						const FString Now = CV ? CV->GetString() : FString();
						DevMenuCheck(Now != Before.FindRef(K), FString::Printf(TEXT("reset: клик изменил %s: «%s» → «%s»"), K, *Before.FindRef(K), *Now));
					}
					DevMenuCheck(DevMenuIniHas(TEXT("AsdaRender"), TEXT("asda.Rim.Scale")), TEXT("reset: после клика asda.Rim.Scale записан в [AsdaRender]"));
					if (UAsdaDevMenuSubsystem* M = FindDevMenuSubsystem()) { M->ResetTab(EAsdaDevTab::Quality); M->ResetTab(EAsdaDevTab::Chars); }
					AsdaDevMenuAfter(1.0f, [Quit, LevelsBefore, GlowBefore]()
					{
						AsdaDevMenuLogResetState(TEXT("after"));
						const UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get();
						DevMenuCheck(P && P->AntiAliasingMethod < 0 && P->TonemapperSharpen < 0.f && P->MotionBlur < 0, FString::Printf(TEXT("reset: профильные поля «не менялось» (AA %d, резкость %.2f, размытие %d)"), P ? P->AntiAliasingMethod : -9, P ? P->TonemapperSharpen : -9.f, P ? P->MotionBlur : -9));
						for (const TCHAR* K : GDevMenuResetKeys)
						{
							DevMenuCheck(!DevMenuIniHas(TEXT("AsdaVideo"), K) && !DevMenuIniHas(TEXT("AsdaRender"), K), FString::Printf(TEXT("reset: ключа %s нет в [AsdaVideo]/[AsdaRender]"), K));
							DevMenuCheck(!IsCVarSetByConsole(K), FString::Printf(TEXT("reset: %s не SetBy Console (SetBy %s)"), K, CVarSetByName(FindCVar(K))));
						}
						// Вернуть стенду уровни и свечение, что были до самотеста (ResetTab(Quality) ставит пресет по умолчанию и glow 4).
						UAsdaDevMenuSubsystem* M = FindDevMenuSubsystem();
						UAsdaVideoSettingsSubsystem* V2 = (M && M->GetGameInstance()) ? M->GetGameInstance()->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr;
						Scalability::SetQualityLevels(LevelsBefore);
						Scalability::SaveState(GGameUserSettingsIni);
						if (UGameUserSettings* GS = GEngine ? GEngine->GetGameUserSettings() : nullptr) { GS->SaveSettings(); }
						if (V2) { V2->SetGlowLevel(GlowBefore, /*bSave*/true); }
						Quit();
					});
				});
				return;
			}
			if (Mode == TEXT("toggle"))
			{
				AsdaDevMenuLogState(TEXT("before"));
				if (!S->IsMenuOpen()) { S->ToggleMenu(); }
				// Кнопки панели: имитируем клики по строкам-цикл-кнопкам (ползунки тумана проверяет режим fogdist/slider).
				//   ★ 12.10.2026: «Тени облаков» убраны из меню — вместо них «Motion blur»; ПРОВАЛ, если подпись после клика не изменилась.
				const TArray<FString> Needles = { TEXT("сила пятна"), TEXT("Сила нормали"), TEXT("сила рима"), TEXT("Резкость (Sharpen)"), TEXT("Motion blur") };
				TArray<FString> LabelsBefore;   // подписи до кликов (в порядке Needles)
				for (const FString& N : Needles) { LabelsBefore.Add(S->SelfTestRowLabel(N)); }
				S->SelfTestClickRows(Needles);
				AsdaDevMenuAfter(1.0f, [Quit, Needles, LabelsBefore]()
				{
					AsdaDevMenuLogState(TEXT("after"));
					if (UAsdaDevMenuSubsystem* M = FindDevMenuSubsystem())
					{
						for (int32 i = 0; i < Needles.Num(); ++i)
						{
							const FString After = M->SelfTestRowLabel(Needles[i]);
							DevMenuCheck(!After.IsEmpty() && After != LabelsBefore[i], FString::Printf(TEXT("toggle: «%s» → «%s»"), *LabelsBefore[i], *After));
						}
					}
					Quit();
				});
				return;
			}
			if (Mode != TEXT("shots"))
			{
				DevMenuCheck(false, FString::Printf(TEXT("неизвестный режим «%s» (shots|toggle|verify|rows|pins|reset|clouds|fogdist|slider|culldrag)"), *Mode));
				Quit();
				return;
			}
			// shots
			if (!S->IsMenuOpen()) { S->ToggleMenu(); }
			const int32 N = (int32)EAsdaDevTab::Count;
			for (int32 i = 0; i < N; ++i)
			{
				AsdaDevMenuAfter(0.5f + i * 1.5f, [i]() { if (UAsdaDevMenuSubsystem* M = FindDevMenuSubsystem()) { M->SetActiveTab((EAsdaDevTab)i); } });
				AsdaDevMenuAfter(1.2f + i * 1.5f, [i, N]()
				{
					if (UAsdaDevMenuSubsystem* M = FindDevMenuSubsystem())
					{
						const int32 Wide = M->LogRowWidths();
						if (i == N - 1) { DevMenuCheck(Wide == 0, FString::Printf(TEXT("shots: подписей шире поля (перенос на 2 строки): %d"), Wide)); }   // один раз — список строк общий для всех вкладок
					}
					FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("devmenu_tab%d"), i), /*bShowUI*/true, /*bAddFilenameSuffix*/false);
				});
			}
			AsdaDevMenuAfter(1.2f + N * 1.5f, [Quit]() { Quit(); });
		});
	}));
#endif // ASDA_DEV_CONSOLE

// Имитация кликов по строкам панели: ищет строку по подстроке подписи и вызывает её обработчик (как UButton::OnClicked).
//   ★ 12.10.2026: ненайденная строка = ПРОВАЛ в счётчике итога (а не Warning); возвращает число ненайденных.
int32 UAsdaDevMenuSubsystem::SelfTestClickRows(const TArray<FString>& Needles)
{
	int32 Missing = 0;   // строк не найдено
	for (const FString& Needle : Needles)
	{
		bool bHit = false;
		for (int32 i = 0; i < Rows.Num() && !bHit; ++i)
		{
			if (Rows[i].Label && Rows[i].Label().Contains(Needle) && Relays.IsValidIndex(RowRelayIndex.IsValidIndex(i) ? RowRelayIndex[i] : -1))
			{
				Relays[RowRelayIndex[i]]->OnClicked();
				UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest: клик «%s» → «%s»"), *Needle, *Rows[i].Label());
				bHit = true;
			}
		}
		if (!bHit) { ++Missing; DevMenuCheck(false, FString::Printf(TEXT("строка меню «%s» не найдена (кнопка)"), *Needle)); }
	}
	return Missing;
}

FString UAsdaDevMenuSubsystem::SelfTestRowLabel(const FString& Needle) const
{
	for (const FRow& R : Rows)
	{
		if (R.Label && R.Label().Contains(Needle)) { return R.Label(); }
	}
	return FString();
}

// ★ 12.10.2026 — режим rows: инвентарь и ожидания нового состава.
void UAsdaDevMenuSubsystem::SelfTestRows()
{
	if (!IsMenuOpen()) { ToggleMenu(); }
	// Инвентарь: все подписи по вкладкам — в лог (снимок «после сокращения» для отчёта).
	for (int32 t = 0; t < (int32)EAsdaDevTab::Count; ++t)
	{
		int32 N = 0;   // строк во вкладке
		for (const FRow& R : Rows) { if ((int32)R.Tab == t && R.Label) { ++N; UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[rows]: [%s] %s«%s»"), GDevTabNames[t], R.Slider.IsValid() ? TEXT("(ползунок) ") : TEXT(""), *R.Label()); } }
		UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[rows]: вкладка «%s» — строк %d"), GDevTabNames[t], N);
	}
	// Ожидаемые строки нового состава (вкладка + подстрока подписи) — 01_designer.md §1 с уточнениями владельца 12.10.2026.
	struct FExpect { EAsdaDevTab Tab; const TCHAR* Needle; };
	static const FExpect Expected[] = {
		{ EAsdaDevTab::Screen,  TEXT("[ Во весь экран (desktop) ]") }, { EAsdaDevTab::Screen, TEXT("Масштаб рендера") }, { EAsdaDevTab::Screen, TEXT("Лимит FPS") },
		{ EAsdaDevTab::Screen,  TEXT("VSync") }, { EAsdaDevTab::Screen, TEXT("Гамма") }, { EAsdaDevTab::Screen, TEXT("Графический API") },
		{ EAsdaDevTab::Quality, TEXT("Пресет качества") }, { EAsdaDevTab::Quality, TEXT("[ Определить автоматически") }, { EAsdaDevTab::Quality, TEXT("Свечение (LEVEL OF GLOW)") },
		{ EAsdaDevTab::Quality, TEXT("Текстуры (sg)") }, { EAsdaDevTab::Quality, TEXT("Тени (sg)") }, { EAsdaDevTab::Quality, TEXT("Эффекты (sg)") }, { EAsdaDevTab::Quality, TEXT("Пост-обработка (sg)") },
		{ EAsdaDevTab::Quality, TEXT("Дальность (sg)") }, { EAsdaDevTab::Quality, TEXT("Сглаживание (sg)") }, { EAsdaDevTab::Quality, TEXT("Отражения (sg)") }, { EAsdaDevTab::Quality, TEXT("Глобальное освещение (sg)") },
		{ EAsdaDevTab::Quality, TEXT("Дальность прорисовки") }, { EAsdaDevTab::Quality, TEXT("Метод сглаживания") }, { EAsdaDevTab::Quality, TEXT("Резкость (Sharpen)") }, { EAsdaDevTab::Quality, TEXT("Motion blur") },
		{ EAsdaDevTab::Sky,     TEXT("Плотность тумана") }, { EAsdaDevTab::Sky, TEXT("Дальность тумана") }, { EAsdaDevTab::Sky, TEXT("3D-облака") },
		{ EAsdaDevTab::Relief,  TEXT("Сила нормали") },
		{ EAsdaDevTab::Chars,   TEXT("Рим:") }, { EAsdaDevTab::Chars, TEXT("сила рима") }, { EAsdaDevTab::Chars, TEXT("Капсульные тени тел") }, { EAsdaDevTab::Chars, TEXT("сила пятна") },
		{ EAsdaDevTab::Reset,   TEXT("[ Сбросить: Экран ]") }, { EAsdaDevTab::Reset, TEXT("[ Сбросить: Качество ]") }, { EAsdaDevTab::Reset, TEXT("[ Сбросить: Небо и туман ]") },
		{ EAsdaDevTab::Reset,   TEXT("[ Сбросить: Рельеф ]") }, { EAsdaDevTab::Reset, TEXT("[ Сбросить: Персонажи ]") }, { EAsdaDevTab::Reset, TEXT("[ Сбросить ВСЁ") },
	};
	for (const FExpect& E : Expected)
	{
		bool bFound = false;
		for (const FRow& R : Rows) { if (R.Tab == E.Tab && R.Label && R.Label().Contains(E.Needle)) { bFound = true; break; } }
		DevMenuCheck(bFound, FString::Printf(TEXT("rows: строка «%s» во вкладке «%s» есть"), E.Needle, GDevTabNames[(int32)E.Tab]));
	}
	// Убранные строки — их быть не должно ни в одной вкладке.
	static const TCHAR* Retired[] = {
		TEXT("пробник железа"), TEXT("Листва (sg)"), TEXT("Шейдинг (sg)"), TEXT("Отсечка дальности"), TEXT("множитель уровня"), TEXT("Bloom"), TEXT("Lens flare"),
		TEXT("анизотропия"), TEXT("смещение мипов"), TEXT("Оптимизация видимости"), TEXT("шаг облаков"), TEXT("Тени: "), TEXT("Качество теней"),
		TEXT("Дальность динамических теней"), TEXT("Контактные тени"), TEXT("Мягкость тени"), TEXT("Отсев мелких теней"), TEXT("Подробность карты теней"),
		TEXT("Тени локальных светов"), TEXT("Тени эффектов"), TEXT("Дальность локальных светов"), TEXT("Ambient Occlusion"), TEXT("GTAO"),
		TEXT("Туман (r.Fog)"), TEXT("Тени облаков"), TEXT("Рельеф (Enable)"), TEXT("Шероховатость из карты"), TEXT("AO из карты"), TEXT("Делайтинг"),
		TEXT("Свечение из карты"), TEXT("Skin cache"), TEXT("Сбросить: Свет и тени"),
	};
	for (const TCHAR* N : Retired)
	{
		bool bFound = false;
		for (const FRow& R : Rows) { if (R.Label && R.Label().Contains(N)) { bFound = true; break; } }
		DevMenuCheck(!bFound, FString::Printf(TEXT("rows: убранной строки «%s» нет"), N));
	}
	// «Пин без ручки»: у каждого накатываемого ключа [AsdaRender] есть строка меню (GPinRows Where=0), и наоборот.
	for (const TCHAR* K : GRenderCVarKeys)
	{
		const FAsdaDevMenuPinRow* Row = nullptr;
		for (const FAsdaDevMenuPinRow& P : GPinRows) { if (FCString::Stricmp(P.CVar, K) == 0 && P.Where == 0) { Row = &P; break; } }
		bool bRowFound = false;
		if (Row) { for (const FRow& R : Rows) { if (R.Label && R.Label().Contains(Row->Needle)) { bRowFound = true; break; } } }
		DevMenuCheck(Row && bRowFound, FString::Printf(TEXT("rows: у ключа [AsdaRender] %s есть строка-ручка%s"), K, Row ? Row->Needle : TEXT(" — НЕТ в GPinRows")));
	}
	for (const FAsdaDevMenuPinRow& P : GPinRows)
	{
		if (P.Where != 0) { continue; }
		bool bInList = false;
		for (const TCHAR* K : GRenderCVarKeys) { if (FCString::Stricmp(P.CVar, K) == 0) { bInList = true; break; } }
		DevMenuCheck(bInList, FString::Printf(TEXT("rows: ключ строки «%s» (%s) накатывается при старте (есть в GRenderCVarKeys)"), P.Needle, P.CVar));
	}
	for (const FAsdaDevMenuPinRow& P : GPinRows)
	{
		if (P.Where == 1) { DevMenuCheck(UAsdaGameUserSettings::IsProfileOwnedCVar(P.CVar), FString::Printf(TEXT("rows: ключ строки «%s» (%s) ведёт профиль игрока"), P.Needle, P.CVar)); }
	}
	const int32 Wide = LogRowWidths();
	DevMenuCheck(Wide == 0, FString::Printf(TEXT("rows: подписей шире поля: %d"), Wide));
}

// ★ 12.10.2026 — режим pins: клик → пин на месте → имитация перезапуска → сброс → пинов нет.
void UAsdaDevMenuSubsystem::SelfTestPins(TFunction<void()> Done)
{
	if (!IsMenuOpen()) { ToggleMenu(); }
	UAsdaVideoSettingsSubsystem* Video = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr;
	if (!Video) { DevMenuCheck(false, TEXT("pins: нет видео-подсистемы")); if (Done) { Done(); } return; }
	// Снимок стенда — вернуть в конце (ResetTab(Quality) ставит пресет по умолчанию и glow 4, клик по масштабу меняет масштаб).
	struct FPinsState
	{
		TMap<FString, FString> Before;            // значение ключа до клика
		TMap<FString, FString> Changed;           // значение ключа после клика
		int32 ScaleBefore = 100;                  // масштаб рендера до
		int32 GlowBefore = 4;                     // свечение до
		Scalability::FQualityLevels LevelsBefore; // уровни до
	};
	TSharedRef<FPinsState> St = MakeShared<FPinsState>();
	St->ScaleBefore = Video->GetRenderScalePercent();
	St->GlowBefore = Video->GetGlowLevel();
	St->LevelsBefore = Scalability::GetQualityLevels();
	for (const FAsdaDevMenuPinRow& P : GPinRows) { IConsoleVariable* CV = FindCVar(P.CVar); St->Before.Add(P.CVar, CV ? CV->GetString() : FString()); }
	// Шаг 1: клик/жест по каждой строке (кнопки — обработчик кнопки; ползунки — полный жест захват → 0.75 → отпускание).
	for (const FAsdaDevMenuPinRow& P : GPinRows)
	{
		if (P.bSlider)
		{
			bool bFound = false;
			for (const FRow& R : Rows) { if (R.Slider.IsValid() && R.Label && R.Label().Contains(P.Needle)) { bFound = true; break; } }
			if (!bFound) { DevMenuCheck(false, FString::Printf(TEXT("строка меню «%s» не найдена (ползунок)"), P.Needle)); continue; }
			SelfTestSliderDrag(P.Needle, 0.75f, 0);
			SelfTestSliderDrag(P.Needle, 0.75f, 1);
			SelfTestSliderDrag(P.Needle, 0.75f, 2);
		}
		else { SelfTestClickRows({ P.Needle }); }
	}
	TWeakObjectPtr<UAsdaDevMenuSubsystem> WeakThis(this);
	// Шаг 2 (+0.8 с): ключ изменился и лёг РОВНО туда, куда пишет ApplyCVarPersist; имитация перезапуска; «пин без ручки»; сброс.
	AsdaDevMenuAfter(0.8f, [WeakThis, St, Done]()
	{
		UAsdaDevMenuSubsystem* M = WeakThis.Get();
		UAsdaVideoSettingsSubsystem* V = (M && M->GetGameInstance()) ? M->GetGameInstance()->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr;
		if (!M || !V) { DevMenuCheck(false, TEXT("pins: подсистема исчезла во время прогона")); if (Done) { Done(); } return; }
		for (const FAsdaDevMenuPinRow& P : GPinRows)
		{
			IConsoleVariable* CV = FindCVar(P.CVar);
			const FString Now = CV ? CV->GetString() : FString();
			St->Changed.Add(P.CVar, Now);
			const FString& Was = St->Before.FindRef(P.CVar);
			if (P.Where == 2)
			{
				DevMenuCheck(V->GetRenderScalePercent() != St->ScaleBefore, FString::Printf(TEXT("pins: «%s» — масштаб рендера %d → %d (поле профиля)"), P.Needle, St->ScaleBefore, V->GetRenderScalePercent()));
				DevMenuCheck(!DevMenuIniHas(TEXT("AsdaVideo"), P.CVar), FString::Printf(TEXT("pins: «%s» — %s НЕ записан в [AsdaVideo] (один источник — профиль)"), P.Needle, P.CVar));
				continue;
			}
			DevMenuCheck(Now != Was, FString::Printf(TEXT("pins: «%s» — %s изменился «%s» → «%s»"), P.Needle, P.CVar, *Was, *Now));
			DevMenuCheck(CV && IsCVarSetByConsole(P.CVar), FString::Printf(TEXT("pins: «%s» — %s SetBy Console после клика (SetBy %s)"), P.Needle, P.CVar, CVarSetByName(CV)));
			FString IniV;
			const bool bInRender = DevMenuIniHas(TEXT("AsdaRender"), P.CVar, &IniV);
			const bool bInVideo  = DevMenuIniHas(TEXT("AsdaVideo"),  P.CVar);
			if (P.Where == 0)
			{
				const bool bSame = bInRender && (IniV.IsNumeric() && Now.IsNumeric() ? FMath::IsNearlyEqual(FCString::Atof(*IniV), FCString::Atof(*Now), 1e-3f) : IniV.Equals(Now, ESearchCase::IgnoreCase));
				DevMenuCheck(bSame, FString::Printf(TEXT("pins: «%s» — [AsdaRender] %s=%s (CVar %s)"), P.Needle, P.CVar, bInRender ? *IniV : TEXT("<нет>"), *Now));
				DevMenuCheck(!bInVideo, FString::Printf(TEXT("pins: «%s» — %s не попал в [AsdaVideo]"), P.Needle, P.CVar));
			}
			else
			{
				DevMenuCheck(UAsdaGameUserSettings::IsProfileOwnedCVar(P.CVar), FString::Printf(TEXT("pins: «%s» — %s ведёт профиль игрока"), P.Needle, P.CVar));
				DevMenuCheck(!bInRender && !bInVideo, FString::Printf(TEXT("pins: «%s» — %s НЕ записан в [AsdaRender]/[AsdaVideo] (один источник — профиль)"), P.Needle, P.CVar));
			}
		}
		// Имитация перезапуска для ключей [AsdaRender]: снять Console-значение и накатить сохранённое, как при старте.
		for (const FAsdaDevMenuPinRow& P : GPinRows) { if (P.Where == 0) { if (IConsoleVariable* CV = FindCVar(P.CVar)) { CV->Unset(ECVF_SetByConsole); } } }
		M->SelfTestReapplySaved();
		for (const FAsdaDevMenuPinRow& P : GPinRows)
		{
			if (P.Where != 0) { continue; }
			IConsoleVariable* CV = FindCVar(P.CVar);
			const FString Now = CV ? CV->GetString() : FString();
			const FString& Want = St->Changed.FindRef(P.CVar);
			const bool bSame = Now.IsNumeric() && Want.IsNumeric() ? FMath::IsNearlyEqual(FCString::Atof(*Now), FCString::Atof(*Want), 1e-3f) : Now.Equals(Want, ESearchCase::IgnoreCase);
			DevMenuCheck(bSame, FString::Printf(TEXT("pins: «%s» — после имитации перезапуска %s=%s (ждали %s)"), P.Needle, P.CVar, *Now, *Want));
		}
		// Сброс всех вкладок, кроме «Экран» (её сброс переключает разрешение/режим окна стенда) — масштаб возвращается отдельно.
		M->ResetTab(EAsdaDevTab::Quality); M->ResetTab(EAsdaDevTab::Sky); M->ResetTab(EAsdaDevTab::Relief); M->ResetTab(EAsdaDevTab::Chars);
		V->SetRenderScalePercent(St->ScaleBefore, /*bSave*/true);
	});
	// Шаг 3 (+1.6 с): после сброса — ключей в ini нет, SetBy не Console; вернуть стенду уровни/свечение.
	AsdaDevMenuAfter(1.6f, [WeakThis, St, Done]()
	{
		UAsdaDevMenuSubsystem* M = WeakThis.Get();
		UAsdaVideoSettingsSubsystem* V = (M && M->GetGameInstance()) ? M->GetGameInstance()->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr;
		for (const FAsdaDevMenuPinRow& P : GPinRows)
		{
			DevMenuCheck(!DevMenuIniHas(TEXT("AsdaRender"), P.CVar) && !DevMenuIniHas(TEXT("AsdaVideo"), P.CVar), FString::Printf(TEXT("pins: после сброса ключа %s нет в [AsdaRender]/[AsdaVideo]"), P.CVar));
			if (P.Where == 2) { DevMenuCheck(V && V->GetRenderScalePercent() == St->ScaleBefore, FString::Printf(TEXT("pins: масштаб рендера возвращён %d"), V ? V->GetRenderScalePercent() : -1)); continue; }
			DevMenuCheck(!IsCVarSetByConsole(P.CVar), FString::Printf(TEXT("pins: после сброса %s не SetBy Console (SetBy %s)"), P.CVar, CVarSetByName(FindCVar(P.CVar))));
		}
		Scalability::SetQualityLevels(St->LevelsBefore);
		Scalability::SaveState(GGameUserSettingsIni);
		if (UGameUserSettings* GS = GEngine ? GEngine->GetGameUserSettings() : nullptr) { GS->SaveSettings(); }
		if (V) { V->SetGlowLevel(St->GlowBefore, /*bSave*/true); }
		if (Done) { Done(); }
	});
}
