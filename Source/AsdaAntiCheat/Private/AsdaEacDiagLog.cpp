// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEacDiagLog.cpp — диагностический журнал клиентского EAC (описание и правила секретов — в заголовке).
#include "AsdaEacDiagLog.h"
#include "AsdaLogSeal.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/Event.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Containers/Queue.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include <atomic>
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace AsdaEacDiagPriv
{
	// Предел размера одного файла журнала (байт): дальше — ротация.
	constexpr int64 kMaxBytes = 1024 * 1024;
	// Сколько файлов держим: AsdaEac.log + AsdaEac.1.log + AsdaEac.2.log.
	constexpr int32 kKeepFiles = 3;
	// Период сброса очереди нитью записи (мс), если сигнала не было.
	constexpr uint32 kFlushPeriodMs = 200;
	// Минимальная длина «токеноподобной» последовательности, которую маскирует SanitizeForeign.
	constexpr int32 kForeignTokenMinLen = 20;

	// ★ 16.10.2026 (решение владельца): журнал ритейл-клиента — только запечатанным (AsdaLogSeal::IsActive: Shipping всегда,
	//   Development — с -AsdaLogSeal). Расширение файла: .alog (запечатан) / .log (открытый текст, Development как раньше).
	static const TCHAR* LogExt() { return AsdaLogSeal::IsActive() ? TEXT("alog") : TEXT("log"); }

	// Имя файла журнала с номером ротации: 0 → AsdaEac.<ext>, N → AsdaEac.N.<ext>.
	static FString RotatedName(const FString& Base, int32 N)
	{
		return N == 0 ? Base : FPaths::Combine(FPaths::GetPath(Base), FString::Printf(TEXT("AsdaEac.%d.%s"), N, LogExt()));
	}

	// Нить записи журнала: очередь строк (MPSC) → UTF-8 в файл, ротация по размеру.
	class FWriter final : public FRunnable
	{
	public:
		FString Path;   // полный путь AsdaEac.log (вычисляется в потоке, создавшем журнал)

		FWriter()
		{
			Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"), FString::Printf(TEXT("AsdaEac.%s"), LogExt())));
			FPaths::NormalizeFilename(Path);
			// ★ 16.10.2026 (живая проверка под start_protected_game.exe): с нитью записи журнал под защитой EAC оставался пустым
			//   (без bootstrapper — пишется). Строк мало (шаги входа, ошибки), поэтому пишем СИНХРОННО с Flush на каждую
			//   порцию: нить не создаётся, Enqueue сразу зовёт Drain под FileLock.
			if (false && FPlatformProcess::SupportsMultithreading())
			{
				Wake = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset*/false);
				Thread = FRunnableThread::Create(this, TEXT("AsdaEacDiagLog"), 0, TPri_BelowNormal);
				if (!Thread) { FPlatformProcess::ReturnSynchEventToPool(Wake); Wake = nullptr; }
			}
		}

		// Поставить строку в очередь (любой поток). Без нити — запись сразу, под замком файла.
		void Enqueue(FString&& Line)
		{
			Queue.Enqueue(MoveTemp(Line));
			if (Thread && !bStopping) { return; }   // нить сама сбросит не позже чем через kFlushPeriodMs
			Drain();
		}

		// Остановить нить: дописать очередь и дождаться выхода. После этого Enqueue пишет синхронно.
		void Stop()
		{
			bStopping = true;
			if (Thread)
			{
				if (Wake) { Wake->Trigger(); }
				Thread->WaitForCompletion();
				delete Thread;
				Thread = nullptr;
			}
			if (Wake) { FPlatformProcess::ReturnSynchEventToPool(Wake); Wake = nullptr; }
			Drain();
		}

		// FRunnable: цикл нити записи.
		virtual uint32 Run() override
		{
			while (!bStopping)
			{
				if (Wake) { Wake->Wait(kFlushPeriodMs); }
				Drain();
			}
			Drain();
			return 0;
		}

	private:
		TQueue<FString, EQueueMode::Mpsc> Queue;   // строки в ожидании записи
		FEvent* Wake = nullptr;                    // сигнал нити (Stop)
		FRunnableThread* Thread = nullptr;         // нить записи (nullptr — синхронный режим)
		std::atomic<bool> bStopping{ false };      // идёт остановка (ShutdownModule)
		FCriticalSection FileLock;                 // доступ к Handle (нить / синхронная запись / Stop)
		TUniquePtr<IFileHandle> Handle;            // открытый AsdaEac.log / .alog (дозапись)
		bool bOpenFailed = false;                  // открыть файл не удалось — дальше не пытаемся (строки отбрасываются)
		const bool bSealed = AsdaLogSeal::IsActive();   // ★ 16.10: запечатывать (ритейл) — решается один раз на процесс
		AsdaLogSeal::FSegment Seg;                 // ★ 16.10: сегмент запечатанного журнала — открывается вместе с файлом

		// ★ 16.10: записать накопленные сегментом байты (заголовок/кадры) в файл.
		void WriteSealedPending()
		{
			if (Handle && Seg.PendingNum() > 0) { Handle->Write(Seg.PendingData(), Seg.PendingNum()); }
			Seg.ClearPending();
		}

		// Открыть файл на дозапись (с ротацией, если он уже больше предела).
		bool EnsureOpen(IPlatformFile& PF)
		{
			if (Handle) { return true; }
			if (bOpenFailed) { return false; }
			PF.CreateDirectoryTree(*FPaths::GetPath(Path));
			if (PF.FileSize(*Path) >= kMaxBytes) { Rotate(PF); }
			Handle.Reset(PF.OpenWrite(*Path, /*bAppend*/true, /*bAllowRead*/true));
			// ★ 16.10: ритейл — каждое открытие файла (запуск, ротация) начинает новый сегмент со своим эфемерным ключом.
			//   Не вышло запечатать — файл закрывается и строки идут в запасной (тоже запечатанный) AsdaEac_raw.alog; открытым
			//   текстом не пишется ничего.
			if (Handle.IsValid() && bSealed)
			{
				if (Seg.Begin(AsdaLogSeal::EKind::EacJournal, AsdaLogSeal::MakeInfoText(AsdaLogSeal::EKind::EacJournal)))
				{
					WriteSealedPending();
				}
				else
				{
					Seg.ClearPending();
					Handle.Reset();
				}
			}
			bOpenFailed = !Handle.IsValid();
			if (bOpenFailed)
			{
#if PLATFORM_WINDOWS
				FAsdaEacDiag::RawMark(FString::Printf(TEXT("журнал EAC НЕ открылся: %s (GetLastError=%u, запечатывание %d)"), *Path, (uint32)::GetLastError(), bSealed ? 1 : 0));
#else
				FAsdaEacDiag::RawMark(FString::Printf(TEXT("журнал EAC НЕ открылся: %s"), *Path));
#endif
			}
			return Handle.IsValid();
		}

		// Сдвинуть файлы: .1 → .2, основной → .1 (самый старый удаляется).
		void Rotate(IPlatformFile& PF)
		{
			Handle.Reset();
			Seg.End();   // ★ 16.10: новый файл — новый сегмент (EnsureOpen)
			for (int32 N = kKeepFiles - 1; N >= 1; --N)
			{
				const FString To = RotatedName(Path, N);
				const FString From = RotatedName(Path, N - 1);
				if (PF.FileExists(*To)) { PF.DeleteFile(*To); }
				if (PF.FileExists(*From)) { PF.MoveFile(*To, *From); }
			}
		}

		// Выбрать очередь и записать одним блоком (с ротацией по размеру). Зовётся нитью или синхронно.
		void Drain()
		{
			FScopeLock L(&FileLock);
			if (Queue.IsEmpty()) { return; }
			IPlatformFile& PF = IPlatformFile::GetPlatformPhysical();   // мимо pak-слоя: пишем прямо на диск
			FString Line;
			while (Queue.Dequeue(Line))
			{
				if (!EnsureOpen(PF)) { FAsdaEacDiag::RawMark(Line.TrimEnd()); continue; }   // журнал недоступен — строка уходит в запасной файл AsdaEac_raw.log
				if (bSealed)
				{
					// ★ 16.10: ритейл — строка уходит кадром AES-GCM текущего сегмента; ротация — по размеру с учётом кадра.
					if (!Seg.SealText(Line)) { Seg.ClearPending(); continue; }
					if (Handle->Size() > AsdaLogSeal::kSegmentHeaderLen && Handle->Size() + Seg.PendingNum() > kMaxBytes)
					{
						Seg.ClearPending();
						Rotate(PF);
						if (!EnsureOpen(PF) || !Seg.SealText(Line)) { Seg.ClearPending(); continue; }
					}
					WriteSealedPending();
					continue;
				}
				const FTCHARToUTF8 U(*Line);
				if (Handle->Size() > 0 && Handle->Size() + U.Length() > kMaxBytes)
				{
					Rotate(PF);
					if (!EnsureOpen(PF)) { continue; }
				}
				Handle->Write(reinterpret_cast<const uint8*>(U.Get()), U.Length());
			}
			if (Handle) { Handle->Flush(); }
		}
	};

	// Защита создания единственного писателя.
	static FCriticalSection& CreateLock() { static FCriticalSection L; return L; }
	// Единственный писатель журнала (не удаляется до конца процесса: строки могут прийти и после ShutdownModule).
	static FWriter* GWriter = nullptr;

	// Писатель (создаётся при первой строке; первой строкой файла идёт заголовок запуска).
	static FWriter& Writer()
	{
		FScopeLock L(&CreateLock());
		if (!GWriter)
		{
			GWriter = new FWriter();
			const TCHAR* Cfg = LexToString(FApp::GetBuildConfiguration());
			const FString Head = FString::Printf(TEXT("\r\n==== %s  запуск: PID %u, конфигурация %s, проект %s, сборка «%s», exe %s; журнал %s ====\r\n"),
				*FDateTime::Now().ToString(TEXT("%Y.%m.%d %H:%M:%S")), FPlatformProcess::GetCurrentProcessId(), Cfg,
				FApp::GetProjectName(), FApp::GetBuildVersion(), FPlatformProcess::ExecutablePath(), *GWriter->Path);
			GWriter->Enqueue(CopyTemp(Head));
		}
		return *GWriter;
	}

	// Символ «токеноподобной» последовательности (base64url, hex, JWT-сегменты).
	static bool IsTokenChar(TCHAR C)
	{
		return (C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('0') && C <= TEXT('9'))
			|| C == TEXT('_') || C == TEXT('-') || C == TEXT('+') || C == TEXT('=');
	}
}

void FAsdaEacDiag::Write(const TCHAR* Level, const FString& Line)
{
	using namespace AsdaEacDiagPriv;
	FString Out = FString::Printf(TEXT("%s [%s] %s\r\n"), *FDateTime::Now().ToString(TEXT("%Y.%m.%d %H:%M:%S.%s")), Level ? Level : TEXT("Log"), *Line);
	Writer().Enqueue(MoveTemp(Out));
}

void FAsdaEacDiag::WriteForeign(const TCHAR* Level, const FString& Line)
{
	Write(Level, SanitizeForeign(Line));
}

void FAsdaEacDiag::Shutdown()
{
	using namespace AsdaEacDiagPriv;
	FWriter* W = nullptr;
	{
		FScopeLock L(&CreateLock());
		W = GWriter;
	}
	if (W)
	{
		W->Enqueue(FString::Printf(TEXT("%s [Log] [EAC] журнал: модуль выгружается, очередь сброшена\r\n"),
			*FDateTime::Now().ToString(TEXT("%Y.%m.%d %H:%M:%S.%s"))));
		W->Stop();
	}
}

FString FAsdaEacDiag::GetFilePath()
{
	return AsdaEacDiagPriv::Writer().Path;
}

FString FAsdaEacDiag::MaskId(const FString& Id)
{
	if (Id.IsEmpty()) { return TEXT("—"); }
	return FString::Printf(TEXT("%s…(%d)"), *Id.Left(6), Id.Len());
}

FString FAsdaEacDiag::MaskLogin(const FString& Login)
{
	if (Login.IsEmpty()) { return TEXT("—"); }
	return FString::Printf(TEXT("%s***(%d)"), *Login.Left(2), Login.Len());
}

FString FAsdaEacDiag::SanitizeForeign(const FString& Line)
{
	using namespace AsdaEacDiagPriv;
	FString Out;
	Out.Reserve(Line.Len());
	int32 I = 0;
	const int32 N = Line.Len();
	while (I < N)
	{
		if (!IsTokenChar(Line[I])) { Out.AppendChar(Line[I]); ++I; continue; }
		int32 J = I;
		while (J < N && IsTokenChar(Line[J])) { ++J; }
		const int32 RunLen = J - I;
		if (RunLen >= kForeignTokenMinLen)
		{
			Out += Line.Mid(I, 6);
			Out += FString::Printf(TEXT("…(%d)"), RunLen);
		}
		else
		{
			Out += Line.Mid(I, RunLen);
		}
		I = J;
	}
	return Out;
}

void FAsdaEacDiag::RawMark(const FString& Line)
{
#if PLATFORM_WINDOWS
	// Отдельный замок: зовётся и из Drain (под FileLock), и из StartupModule.
	static FCriticalSection RawLock;
	FScopeLock L(&RawLock);
	const FString Dir = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA")) / TEXT("AsdaReborn");
	::CreateDirectoryW(*Dir, nullptr);
	// ★ 16.10: ритейл — AsdaEac_raw.alog; каждая метка — свой самодостаточный сегмент (заголовок + сведения + строка), потому
	//   что файл открывается на каждую строку и в него могут дописывать два процесса сразу: кадры разных сегментов не перемешаются.
	const bool bSealed = AsdaLogSeal::IsActive();
	const FString File = Dir / (bSealed ? TEXT("AsdaEac_raw.alog") : TEXT("AsdaEac_raw.log"));
	const FString Out = FString::Printf(TEXT("%s [pid %u] %s\r\n"), *FDateTime::Now().ToString(TEXT("%Y.%m.%d %H:%M:%S.%s")),
		FPlatformProcess::GetCurrentProcessId(), *SanitizeForeign(Line));
	TArray<uint8> Bytes;
	if (bSealed)
	{
		AsdaLogSeal::FSegment Seg;
		if (!Seg.Begin(AsdaLogSeal::EKind::EacRaw, AsdaLogSeal::MakeInfoText(AsdaLogSeal::EKind::EacRaw)) || !Seg.SealText(Out)) { return; }   // открытым текстом — никогда
		Bytes.Append(Seg.PendingData(), Seg.PendingNum());
		Seg.End();
	}
	else
	{
		const FTCHARToUTF8 U(*Out);
		Bytes.Append(reinterpret_cast<const uint8*>(U.Get()), U.Length());
	}
	HANDLE H = ::CreateFileW(*File, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (H == INVALID_HANDLE_VALUE) { return; }
	DWORD Written = 0;
	::WriteFile(H, Bytes.GetData(), (DWORD)Bytes.Num(), &Written, nullptr);   // одна запись FILE_APPEND_DATA — сегмент целиком
	::CloseHandle(H);
#endif
}
