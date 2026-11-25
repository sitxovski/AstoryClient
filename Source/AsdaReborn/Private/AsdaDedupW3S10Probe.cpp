// Copyright (c) 2026 @sitxovski (Discord)
// ★ ДИАГНОСТИКА ВОЛНЫ В3 ДЕДУПА №2 — ЧАСТЬ S10/N12/F7 + ХВОСТ РЕВИЗИИ В2 (12.10.2026). Только не-Shipping.
//   Команды нужны для проверки «было/стало»: один и тот же файл собирается и в копии проекта «до» (код до правок части
//   S10 + откаченные файлы В2-A/В2-B), и в основном проекте «после». Пакетов не шлёт: 5105 — только под перехватом.
//
//   asda.W3.MountDump <файл.tsv>  — N12: `LookupMount` по ВСЕМ кодам `DT_special_itemlist` (+ коды вне таблицы) в файл
//                                   «код<TAB>ок<TAB>модель<TAB>набор<TAB>высота»; в лог — итог и время прохода.
//   asda.W3.LoginKind <метка>     — F7: все прежние признаки «сцена логина» (имя мира, имя карты, режим игры) и новый
//                                   общий (если он уже есть в сборке) для мира GameInstance.
//   asda.W3.DragWin <окно> <dx> <dy> — S10: настоящее перетаскивание окна за первую ручку (`SimulateDrag`).
//   asda.W3.Dilation <x>          — замедление времени мира (`SetGlobalTimeDilation`), 1 — норма.
//   asda.W3.Pause <0|1>           — пауза мира (`UGameplayStatics::SetGamePaused`); последовательность `asda.UI.Seq` идёт
//                                   по стенным часам и паузой не останавливается.
//   asda.W3.PetCall <uid>         — хвост В2: взвести засов «6100 в полёте» боевым `RequestCallPet` (кнопка окна 43000) под
//                                   узким перехватом 6100; `asda.W3.PetLatch <метка>` — состояние засова; `asda.W3.PetCallEnd` — снять.
//   asda.W3.Party5105 <0|1> <номер> — хвост В2: свой номер известен (1) / нет (0); ввод чата в режиме 5 «Отряд» путём Enter
//                                   (`DebugSubmit`) под перехватом исходящих; печать числа 5105 и тела.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaDataTableCache.h"                 // AsdaDt::Cached — та же таблица, что у `LookupMount`
#include "AsdaNetSubsystem.h"                   // свой номер
#include "Test/AsdaSelfTestKit.h"               // перехват исходящих (общая точка S1)
#include "AsdaOutgoingGate.h"                   // ★ ревизия В3: офлайн-гейт команд, способных отправить 6100/5105
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Items/AsdaItemDbRows.h"               // FAsdaSpecialItemlistRow
#include "Kismet/GameplayStatics.h"             // пауза мира
#include "Login/AsdaLoginGameMode.h"            // признак «по режиму игры»
#include "Misc/FileHelper.h"
#include "Pet/AsdaPetSubsystem.h"               // RequestCallPet
#include "UI/AsdaChatWindowSubsystem.h"         // режим ввода 5 и путь Enter
#include "UI/AsdaWindowSubsystem.h"             // ручка перетаскивания окна, SimulateDrag
#include "World/AsdaWorldSubsystem.h"           // LookupMount
#if __has_include("World/AsdaWorldKind.h")
#include "World/AsdaWorldKind.h"                // ★ F7: общий признак (есть только в сборке «после»)
#define ASDA_W3_HAS_WORLDKIND 1
#else
#define ASDA_W3_HAS_WORLDKIND 0
#endif

DEFINE_LOG_CATEGORY_STATIC(LogAsdaW3S10, Log, All);

namespace
{
	// Объектный путь таблицы спец-предметов — тот же, что читает `UAsdaWorldSubsystem::LookupMount`.
	const TCHAR* const kW3SpecialItemlistDT = TEXT("/Game/AsdaAssets/Data/ItemDB/DT_special_itemlist.DT_special_itemlist");

	// GameInstance мира консольной команды.
	UGameInstance* W3Gi(UWorld* W) { return W ? W->GetGameInstance() : nullptr; }

	FAutoConsoleCommandWithWorldAndArgs GW3MountDump(
		TEXT("asda.W3.MountDump"),
		TEXT("asda.W3.MountDump <файл.tsv> — LookupMount по всем кодам DT_special_itemlist (+ коды вне таблицы) в файл."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
		{
			UGameInstance* GI = W3Gi(W);
			UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			const UDataTable* T = AsdaDt::Cached(kW3SpecialItemlistDT);
			if (!WS || !T || A.Num() < 1)
			{
				UE_LOG(LogAsdaW3S10, Warning, TEXT("[N12] MountDump: нет мира (%d), таблицы (%d) или пути файла"), WS ? 1 : 0, T ? 1 : 0);
				return;
			}
			// Коды: все строки таблицы (в порядке строк, без повторов) + коды вне таблицы.
			TArray<int32> Codes;
			TSet<int32> Seen;
			int32 NRows = 0, NSub15 = 0;
			for (const TPair<FName, uint8*>& It : T->GetRowMap())
			{
				const FAsdaSpecialItemlistRow* R = reinterpret_cast<const FAsdaSpecialItemlistRow*>(It.Value);
				if (!R) { continue; }
				++NRows;
				if (R->SubType == 15) { ++NSub15; }
				if (!Seen.Contains(R->Code)) { Seen.Add(R->Code); Codes.Add(R->Code); }
			}
			for (const int32 Extra : { -1, 0, 1, 999999, MAX_int32 }) { if (!Seen.Contains(Extra)) { Seen.Add(Extra); Codes.Add(Extra); } }
			Codes.Sort();
			FString Out;
			int32 NOk = 0;
			const double T0 = FPlatformTime::Seconds();
			for (const int32 Code : Codes)
			{
				FString Model, Anim; float H = 0.f;
				const bool bOk = WS->LookupMount(Code, Model, Anim, H);
				if (bOk) { ++NOk; }
				Out += FString::Printf(TEXT("%d\t%d\t%s\t%s\t%.3f\n"), Code, bOk ? 1 : 0, *Model, *Anim, H);
			}
			const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
			const bool bSaved = FFileHelper::SaveStringToFile(Out, *A[0], FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			UE_LOG(LogAsdaW3S10, Log, TEXT("[N12] MountDump ИТОГ: строк %d (SubType 15: %d), кодов %d, найдено транспортов %d, проход %.3f мс, файл %s — %s"),
				NRows, NSub15, Codes.Num(), NOk, Ms, *A[0], bSaved ? TEXT("записан") : TEXT("НЕ ЗАПИСАН"));
		}));

	FAutoConsoleCommandWithWorldAndArgs GW3LoginKind(
		TEXT("asda.W3.LoginKind"),
		TEXT("asda.W3.LoginKind <метка> — прежние признаки «сцена логина» (имя мира, имя карты, режим игры) и общий."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
		{
			UGameInstance* GI = W3Gi(W);
			UWorld* GW = GI ? GI->GetWorld() : W;
			const FString Label = A.Num() > 0 ? A[0] : FString(TEXT("-"));
			if (!GW) { UE_LOG(LogAsdaW3S10, Warning, TEXT("[F7] %s: мира нет"), *Label); return; }
			const AGameModeBase* GM = GW->GetAuthGameMode();
			// Прежние признаки — дословно выражения 9 мест (до общей точки).
			const bool bByWorldName = GW->GetName().Contains(TEXT("Login"));      // мир 570/604/1480, сброс сессии
			const bool bByMapName   = GW->GetMapName().Contains(TEXT("Login"));   // холдер цикла эффектов
			const bool bByMode      = GM && GM->IsA<AAsdaLoginGameMode>();        // свечение/альфа-тест, окна 822/3979/4036
			const bool bLoading     = GW->GetName().Contains(TEXT("L_Loading"));
#if ASDA_W3_HAS_WORLDKIND
			const FString NewKind = FString::Printf(TEXT(" | общий IsLoginScene=%d IsLoadingMap=%d"),
				AsdaWorldKind::IsLoginScene(GW) ? 1 : 0, AsdaWorldKind::IsLoadingMap(GW) ? 1 : 0);
#else
			const FString NewKind = TEXT(" | общего признака в сборке нет");
#endif
			UE_LOG(LogAsdaW3S10, Log, TEXT("[F7] %s: мир=%s карта=%s режим=%s игровой=%d | по имени мира=%d по имени карты=%d по режиму=%d загрузка=%d | %s%s"),
				*Label, *GW->GetName(), *GW->GetMapName(), GM ? *GM->GetClass()->GetName() : TEXT("нет"), GW->IsGameWorld() ? 1 : 0,
				bByWorldName ? 1 : 0, bByMapName ? 1 : 0, bByMode ? 1 : 0, bLoading ? 1 : 0,
				(bByWorldName == bByMode && bByMapName == bByMode) ? TEXT("СОВПАДАЮТ") : TEXT("РАСХОДЯТСЯ"), *NewKind);
		}));

	FAutoConsoleCommandWithWorldAndArgs GW3DragWin(
		TEXT("asda.W3.DragWin"),
		TEXT("asda.W3.DragWin <окно> <dx> <dy> — НАСТОЯЩЕЕ перетаскивание окна за его первую ручку (SimulateDrag) на (dx,dy)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
		{
			UGameInstance* GI = W3Gi(W);
			UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			FVector2D Mn, Mx;
			if (!WM || A.Num() < 3 || !WM->DebugGetDragHandleRect(FName(*A[0]), 0, Mn, Mx))
			{
				UE_LOG(LogAsdaW3S10, Warning, TEXT("[S10] DragWin: нет окна/ручки (%s)"), A.Num() > 0 ? *A[0] : TEXT("-"));
				return;
			}
			const FVector2D From = (Mn + Mx) * 0.5;
			const FVector2D To = From + FVector2D(FCString::Atof(*A[1]), FCString::Atof(*A[2]));
			WM->SimulateDrag(From, To, 20);
			UE_LOG(LogAsdaW3S10, Log, TEXT("[S10] DragWin %s: ручка (%.0f,%.0f)-(%.0f,%.0f), тянем (%.0f,%.0f) → (%.0f,%.0f)"),
				*A[0], Mn.X, Mn.Y, Mx.X, Mx.Y, From.X, From.Y, To.X, To.Y);
		}));

	FAutoConsoleCommandWithWorldAndArgs GW3Pause(
		TEXT("asda.W3.Pause"),
		TEXT("asda.W3.Pause <0|1> — пауза мира (SetGamePaused)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
		{
			const bool bPause = A.Num() > 0 && FCString::Atoi(*A[0]) != 0;
			const bool bOk = W && UGameplayStatics::SetGamePaused(W, bPause);
			UE_LOG(LogAsdaW3S10, Log, TEXT("[хвост В2] пауза мира = %d (%s), стенные часы %.3f"), bPause ? 1 : 0,
				bOk ? TEXT("принято") : TEXT("ОТКАЗ"), FPlatformTime::Seconds());
		}));

	// Поколение узкого перехвата 6100 пробы питомца (0 — не стоит).
	uint32 GW3PetCaptureGen = 0;

	FAutoConsoleCommandWithWorldAndArgs GW3Dilation(
		TEXT("asda.W3.Dilation"),
		TEXT("asda.W3.Dilation <x> — замедление времени мира (SetGlobalTimeDilation); 1 — норма."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
		{
			const float X = A.Num() > 0 ? FCString::Atof(*A[0]) : 1.f;
			if (W) { UGameplayStatics::SetGlobalTimeDilation(W, X); }
			UE_LOG(LogAsdaW3S10, Log, TEXT("[хвост В2] замедление времени мира = %.2f, стенные часы %.3f"), X, FPlatformTime::Seconds());
		}));

	FAutoConsoleCommandWithWorldAndArgs GW3PetCall(
		TEXT("asda.W3.PetCall"),
		TEXT("asda.W3.PetCall <uid> — узкий перехват 6100 + RequestCallPet (засов 6100 «в полёте»); снять — asda.W3.PetCallEnd."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
		{
			UGameInstance* GI = W3Gi(W);
			UAsdaPetSubsystem* P = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
			UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			if (!P || !Net) { return; }
			// ★ 12.10.2026 (ревизия В3): офлайн-гейт — при живом соединении и при занятом перехвате (отказ `ArmCapture`) 6100
			//   ушёл бы на сервер настоящим вызовом питомца. Теперь в обоих случаях — отказ, ничего не отправлено.
			static const uint16 kGuardOps[] = { 6100 };
			if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.W3.PetCall"), 30.f, MakeArrayView(kGuardOps))) { return; }
			// Без связи 6100 «не уходит», и засов снимается на первом же тике (`Tick`: «сеть не готова») — проверка срока была бы
			//   пустой. Узкий перехват 6100 делает отправку успешной, а в сеть ничего не уходит.
			GW3PetCaptureGen = AsdaSelfTestKit::ArmCapture(Net, [](uint16, const uint8*, int32, bool, TCHAR) {}, 6100, TEXT("asda.W3.PetCall"));
			if (GW3PetCaptureGen == 0) { return; }   // перехват занят — без него 6100 не зовём (отказ каркаса уже в логе)
			const int32 Uid = A.Num() > 0 ? FCString::Atoi(*A[0]) : 1000;
			const bool bOk = P->RequestCallPet(Uid);
			UE_LOG(LogAsdaW3S10, Log, TEXT("[хвост В2] RequestCallPet(%d) = %d, стенные часы %.3f"), Uid, bOk ? 1 : 0, FPlatformTime::Seconds());
		}));

	FAutoConsoleCommandWithWorldAndArgs GW3PetCallEnd(
		TEXT("asda.W3.PetCallEnd"),
		TEXT("asda.W3.PetCallEnd — снять перехват 6100 пробы питомца; печать засова."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
		{
			UGameInstance* GI = W3Gi(W);
			UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			const bool bOff = Net && AsdaSelfTestKit::DisarmCapture(Net, GW3PetCaptureGen);
			GW3PetCaptureGen = 0;
			UE_LOG(LogAsdaW3S10, Log, TEXT("[хвост В2] перехват 6100 снят: %d"), bOff ? 1 : 0);
		}));

	FAutoConsoleCommandWithWorldAndArgs GW3PetLatch(
		TEXT("asda.W3.PetLatch"),
		TEXT("asda.W3.PetLatch <метка> — засов «6100 в полёте» (IsCallRequestInFlight) и стенные часы."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
		{
			UGameInstance* GI = W3Gi(W);
			UAsdaPetSubsystem* P = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
			if (!P) { return; }
			UE_LOG(LogAsdaW3S10, Log, TEXT("[хвост В2] засов 6100 [%s]: %s, пауза мира %d, стенные часы %.3f"),
				A.Num() > 0 ? *A[0] : TEXT("-"), P->IsCallRequestInFlight() ? TEXT("ВЗВЕДЁН") : TEXT("снят"),
				(W && W->IsPaused()) ? 1 : 0, FPlatformTime::Seconds());
		}));

	FAutoConsoleCommandWithWorldAndArgs GW3Party5105(
		TEXT("asda.W3.Party5105"),
		TEXT("asda.W3.Party5105 <0|1> <номер> — ввод чата в режиме «Отряд» под перехватом: сколько ушло 5105."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
		{
			UGameInstance* GI = W3Gi(W);
			UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			UAsdaChatWindowSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr;
			if (!Net || !Chat) { UE_LOG(LogAsdaW3S10, Warning, TEXT("[хвост В2] 5105: нет сети/чата")); return; }
			// ★ 12.10.2026 (ревизия В3): офлайн-гейт — после оживления чата отряда ввод в режиме 5 при живом соединении и
			//   занятом перехвате ушёл бы настоящим 5105. Теперь — отказ; при занятом перехвате ввод не выполняется.
			static const uint16 kGuardOps[] = { 5105 };
			if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.W3.Party5105"), 10.f, MakeArrayView(kGuardOps))) { return; }
			if (AsdaSelfTestKit::IsCaptureBusy(Net)) { UE_LOG(LogAsdaW3S10, Warning, TEXT("[хвост В2] 5105: перехват занят — проба не выполнена")); return; }
			const bool bKnown = A.Num() > 0 && FCString::Atoi(*A[0]) != 0;
			const int32 Num = A.Num() > 1 ? FCString::Atoi(*A[1]) : 7;
			// Состояние «своего номера» — на время пробы, затем вернуть.
			const uint16 NumWas = Net->SelfUserNum;
			const bool bKnownWas = Net->bSelfUserNumKnown;
			const int32 StateWas = Chat->GetInputState();
			Net->SelfUserNum = (uint16)Num;
			Net->bSelfUserNumKnown = bKnown;
			if (!bKnown) { Net->SelfUserNum = 0; }   // до 4003 номер = 0 (поле не заполнено)
			int32 N5105 = 0; TArray<uint8> Last;
			const uint32 Gen = AsdaSelfTestKit::ArmCapture(Net, [&N5105, &Last](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
			{
				if (Op == 5105) { ++N5105; Last.Reset(); if (P && Len > 0) { Last.Append(P, Len); } }
			}, 0, TEXT("asda.W3.Party5105"));
			Chat->SetInputState(5);
			Chat->DebugSubmit(TEXT("отряд"));
			AsdaSelfTestKit::DisarmCapture(Net, Gen);
			Chat->SetInputState(StateWas);
			Net->SelfUserNum = NumWas;
			Net->bSelfUserNumKnown = bKnownWas;
			FString Hex;
			for (int32 i = 0; i < FMath::Min(Last.Num(), 4); ++i) { Hex += FString::Printf(TEXT("%02X "), Last[i]); }
			UE_LOG(LogAsdaW3S10, Log, TEXT("[хвост В2] 5105: свой номер %s (%d) → ушло %d, первые байты тела: %s"),
				bKnown ? TEXT("известен") : TEXT("НЕ известен"), bKnown ? Num : 0, N5105, Hex.IsEmpty() ? TEXT("—") : *Hex);
		}));
}

#endif // !UE_BUILD_SHIPPING
