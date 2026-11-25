// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ адреса сервера (16.10.2026, решение владельца «сервер — всегда домен»): `asda.SelfTest.ServerAddr [quit] [dns]`.
//   A. Правило подмены адреса из пакетов (AsdaServerAddr::MapPacketHost): RFC1918 / loopback / link-local / 0.0.0.0/8 /
//      localhost → домен; публичный IPv4 и имя → как есть.
//   B. Единая точка UAsdaNetSubsystem::ApplyGameServerFromPacket: подмена хоста, порт из пакета сохраняется, негодный
//      адрес/порт ничего не меняет; дефолт GameServerIP (CDO) — домен.
//   C. Логин-сервер (FAsdaLoginData::LoadLoginServer): сохранённый `[AsdaLogin] ServerIP` НЕ читается (кладётся заведомо
//      другой адрес — хост остаётся доменом, порт — из DT_ServerList); PurgeLegacyServerIp удаляет ключ.
//   D. Экран логина: Server.IP режима = домен; поле SERVER_IP_INPUT показывает домен (ждём сборки окна до 30 с);
//      после подмены текста FillServerAddrField возвращает домен.
//   E. Только с аргументом `dns`: имя домена разрешается тем же путём, что подключение (кэш + фоновый резолв IPv4) —
//      без сокета и без подключения к игровым портам. Результат пишется в журнал.
//   Без `dns` в сеть не обращается ничего (дружелюбен к -AsdaNoNet).
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Login/AsdaServerAddress.h"
#include "Login/AsdaLoginData.h"
#include "Login/AsdaLoginGameMode.h"
#include "AsdaNetSubsystem.h"
#include "AsdaConnection.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Components/EditableText.h"
#include "Blueprint/UserWidget.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaServerAddrTest, Log, All);

namespace AsdaServerAddrTest
{
	// Состояние прогона (живёт в замыкании тикера ядра).
	struct FRun
	{
		int32 Ok = 0, Fail = 0;        // счётчики проверок
		bool bQuit = false;            // выйти по итогу (пакет перехватывает `quit`)
		bool bDns = false;             // выполнить проверку резолва домена (E)
		double StartSec = 0.0;         // начало ожидания окна логина
		TWeakObjectPtr<UWorld> World;  // мир самотеста
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaServerAddrTest, Log, TEXT("АДРЕС-СЕРВЕРА ok   %s"), *What); }
			else { ++Fail; UE_LOG(LogAsdaServerAddrTest, Error, TEXT("АДРЕС-СЕРВЕРА FAIL %s"), *What); }
		}
	};

	static const FString Domain() { return FString(AsdaServerAddr::LoginHost()); }   // домен сервера (строкой)

	// A. Таблица правила подмены.
	static void CaseRule(FRun& R)
	{
		struct FVec { const TCHAR* In; bool bSub; };
		static const FVec V[] =
		{
			{ TEXT("192.168.10.70"),   true  },   // адрес стойки из конфига сервера
			{ TEXT(" 192.168.10.70 "), true  },   // с пробелами (поле пакета обрезается — правило тоже)
			{ TEXT("10.0.0.1"),        true  },
			{ TEXT("172.16.0.5"),      true  },
			{ TEXT("172.31.255.255"),  true  },
			{ TEXT("172.32.0.1"),      false },   // за границей 172.16/12 — публичный
			{ TEXT("172.15.0.1"),      false },
			{ TEXT("127.0.0.1"),       true  },
			{ TEXT("169.254.10.20"),   true  },
			{ TEXT("0.0.0.0"),         true  },
			{ TEXT("localhost"),       true  },
			{ TEXT("198.51.100.177"),  false },   // публичный — как есть
			{ TEXT("198.51.100.250"),  false },
			{ TEXT("@@ASTORY_AUTH_HOST@@"),  false },   // имя — как есть
			{ TEXT("192.168.10.700"),  false },   // не IPv4-литерал — имя, как есть
			{ TEXT("192.168.10"),      false },
		};
		for (const FVec& X : V)
		{
			bool bSub = !X.bSub;
			const FString Out = AsdaServerAddr::MapPacketHost(X.In, &bSub);
			const FString Want = X.bSub ? Domain() : FString(X.In).TrimStartAndEnd();
			R.Expect(bSub == X.bSub && Out == Want,
				FString::Printf(TEXT("A правило: '%s' → '%s' (ожидалось '%s', подмена %d)"), X.In, *Out, *Want, X.bSub ? 1 : 0));
		}
	}

	// B. Единая точка записи адреса игрового сервера.
	static void CaseApply(FRun& R, UWorld* W)
	{
		R.Expect(GetDefault<UAsdaNetSubsystem>()->GameServerIP == Domain(),
			FString::Printf(TEXT("B дефолт GameServerIP (CDO) = '%s'"), *GetDefault<UAsdaNetSubsystem>()->GameServerIP));
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* N = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!N) { R.Expect(false, TEXT("B нет UAsdaNetSubsystem")); return; }
		const FString SaveIp = N->GameServerIP;   // состояние подсистемы возвращается после проверки
		const int32 SavePort = N->GameServerPort;

		bool b = N->ApplyGameServerFromPacket(TEXT("192.168.10.70"), 15601, TEXT("самотест"));
		R.Expect(b && N->GameServerIP == Domain() && N->GameServerPort == 15601,
			FString::Printf(TEXT("B приватный 192.168.10.70:15601 → %s:%d"), *N->GameServerIP, N->GameServerPort));
		b = N->ApplyGameServerFromPacket(TEXT("10.1.2.3"), 15655, TEXT("самотест"));
		R.Expect(b && N->GameServerIP == Domain() && N->GameServerPort == 15655,
			FString::Printf(TEXT("B приватный 10.1.2.3:15655 → %s:%d (порт из пакета)"), *N->GameServerIP, N->GameServerPort));
		b = N->ApplyGameServerFromPacket(TEXT("198.51.100.177"), 15602, TEXT("самотест"));
		R.Expect(b && N->GameServerIP == TEXT("198.51.100.177") && N->GameServerPort == 15602,
			FString::Printf(TEXT("B публичный 198.51.100.177:15602 → %s:%d (как есть)"), *N->GameServerIP, N->GameServerPort));
		b = N->ApplyGameServerFromPacket(TEXT(""), 15601, TEXT("самотест"));
		R.Expect(!b && N->GameServerIP == TEXT("198.51.100.177") && N->GameServerPort == 15602, TEXT("B пустой адрес — ничего не меняет"));
		b = N->ApplyGameServerFromPacket(TEXT("192.168.10.70"), 0, TEXT("самотест"));
		R.Expect(!b && N->GameServerIP == TEXT("198.51.100.177") && N->GameServerPort == 15602, TEXT("B порт 0 — ничего не меняет"));
		b = N->ApplyGameServerFromPacket(TEXT("192.168.10.70"), 65536, TEXT("самотест"));
		R.Expect(!b && N->GameServerPort == 15602, TEXT("B порт 65536 — ничего не меняет"));

		N->GameServerIP = SaveIp;
		N->GameServerPort = SavePort;
	}

	// C. Логин-сервер: сохранённый адрес не читается, прежний ключ удаляется.
	static void CaseLoginData(FRun& R)
	{
		FAsdaServerEntry Table;
		const bool bTable = FAsdaLoginData::LoadActiveServer(Table);
		R.Expect(bTable && Table.Port > 0, FString::Printf(TEXT("C DT_ServerList загружен: порт %d"), Table.Port));
		if (!GConfig) { R.Expect(false, TEXT("C нет GConfig")); return; }
		const TCHAR* Sec = FAsdaLoginData::LegacyServerIpSection;
		const TCHAR* Key = FAsdaLoginData::LegacyServerIpKey;
		GConfig->SetString(Sec, Key, TEXT("10.9.8.7:1234"), GGameUserSettingsIni);   // заведомо другой адрес и порт

		FAsdaServerEntry E;
		const bool bOk = FAsdaLoginData::LoadLoginServer(E);
		R.Expect(bOk && E.IP == Domain() && E.Port == Table.Port,
			FString::Printf(TEXT("C LoadLoginServer при сохранённом '10.9.8.7:1234' → %s:%d (хост — домен, порт — таблица %d)"),
				*E.IP, E.Port, Table.Port));
		R.Expect(FAsdaLoginData::PurgeLegacyServerIp(), TEXT("C PurgeLegacyServerIp: ключ был — удалён"));
		FString Left;
		R.Expect(!GConfig->GetString(Sec, Key, Left, GGameUserSettingsIni), TEXT("C ключ [AsdaLogin] ServerIP отсутствует после удаления"));
		R.Expect(!FAsdaLoginData::PurgeLegacyServerIp(), TEXT("C повторное удаление — ключа нет"));
	}

	// D. Экран логина: адрес режима и текст поля. true — проверка выполнена (окно найдено или срок вышел).
	static bool CaseLoginScreen(FRun& R, UWorld* W, bool bLast)
	{
		AAsdaLoginGameMode* GM = W ? W->GetAuthGameMode<AAsdaLoginGameMode>() : nullptr;
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WinMgr = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UUserWidget* Win = WinMgr ? WinMgr->FindWindow(FName(TEXT("LOGIN_WIN"))) : nullptr;
		UEditableText* Field = Win ? Cast<UEditableText>(Win->GetWidgetFromName(TEXT("SERVER_IP_INPUT"))) : nullptr;
		if (!GM || !Field)
		{
			if (!bLast) { return false; }   // окно ещё не собрано — ждём
			R.Expect(false, FString::Printf(TEXT("D экран логина: режим %s, поле SERVER_IP_INPUT %s (карта логина?)"),
				GM ? TEXT("есть") : TEXT("НЕТ"), Field ? TEXT("есть") : TEXT("НЕТ")));
			return true;
		}
		R.Expect(GM->GetServer().IP == Domain(),
			FString::Printf(TEXT("D адрес режима логина: %s:%d"), *GM->GetServer().IP, GM->GetServer().Port));
		R.Expect(Field->GetText().ToString() == Domain(),
			FString::Printf(TEXT("D поле SERVER_IP_INPUT = '%s'"), *Field->GetText().ToString()));
		const FText Was = Field->GetText();
		Field->SetText(FText::FromString(TEXT("1.2.3.4:9999")));
		GM->FillServerAddrField();
		R.Expect(Field->GetText().ToString() == Domain(),
			FString::Printf(TEXT("D после подмены текста FillServerAddrField → '%s'"), *Field->GetText().ToString()));
		Field->SetText(Was);
		return true;
	}

	// E. Резолв домена тем же путём, что подключение (без сокета).
	static void CaseDns(FRun& R)
	{
		FString Ip;
		const double T0 = FPlatformTime::Seconds();
		const bool b = FAsdaConnection::ResolveHostForDiag(Domain(), 5.f, Ip);
		const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
		R.Expect(b && !Ip.IsEmpty() && !AsdaServerAddr::IsNonPublicHost(Ip),
			FString::Printf(TEXT("E резолв '%s' → '%s' за %.0f мс (IPv4, не частный)"), *Domain(), *Ip, Ms));
		UE_LOG(LogAsdaServerAddrTest, Log, TEXT("АДРЕС-СЕРВЕРА инфо: A-запись %s = %s (публичная по DNS — @@ASTORY_SERVER_WAN_IP@@ (с 24.10.2026); иное значение даёт локальный DNS/прокси машины)"),
			*Domain(), *Ip);
		FString Ip2;
		const double T1 = FPlatformTime::Seconds();
		const bool b2 = FAsdaConnection::ResolveHostForDiag(Domain(), 5.f, Ip2);
		const double Ms2 = (FPlatformTime::Seconds() - T1) * 1000.0;
		R.Expect(b2 && Ip2 == Ip && Ms2 < 50.0, FString::Printf(TEXT("E повторный резолв из кэша: '%s' за %.1f мс"), *Ip2, Ms2));
	}

	static void Finish(const TSharedRef<FRun>& R)
	{
		UE_LOG(LogAsdaServerAddrTest, Log, TEXT("АДРЕС-СЕРВЕРА ИТОГ: ok=%d fail=%d"), R->Ok, R->Fail);
		if (R->bQuit && GEngine) { GEngine->Exec(R->World.Get(), TEXT("quit")); }
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestServerAddr(
	TEXT("asda.SelfTest.ServerAddr"),
	TEXT("asda.SelfTest.ServerAddr [quit] [dns] — адрес сервера = домен: правило подмены непубличного адреса из пакетов, единая точка ")
	TEXT("ApplyGameServerFromPacket, логин без чтения [AsdaLogin] ServerIP и удаление ключа, поле SERVER_IP_INPUT. dns — резолв домена ")
	TEXT("(без подключения). Карта логина."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaServerAddrTest;
		TSharedRef<FRun> R = MakeShared<FRun>();
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { R->bQuit = true; }
			if (Arg.Equals(TEXT("dns"), ESearchCase::IgnoreCase))  { R->bDns = true; }
		}
		R->World = W;
		R->StartSec = FPlatformTime::Seconds();
		CaseRule(*R);
		CaseApply(*R, W);
		CaseLoginData(*R);
		if (R->bDns) { CaseDns(*R); }
		// D — когда окно логина собрано (после заставки/выбора языка); предел ожидания 30 с.
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([R](float) -> bool
		{
			const bool bLast = FPlatformTime::Seconds() - R->StartSec > 30.0;
			if (!CaseLoginScreen(*R, R->World.Get(), bLast)) { return true; }
			Finish(R);
			return false;
		}), 0.25f);
	}));

#endif // !UE_BUILD_SHIPPING
