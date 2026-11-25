// Copyright (c) 2026 @sitxovski (Discord)
// Реализация выборки треков анимации login-задника. См. комментарий-шапку в AsdaBackdropAnimData.h.
#include "Login/AsdaBackdropAnimData.h"

bool UAsdaBackdropAnimData::SampleTrack(int32 TrackIndex, float Time, FTransform& OutDelta) const
{
	// Шаг ключей — 7 float (loc xyz + quat xyzw); задан экспортёром и продублирован здесь константой,
	// чтобы формула индекса читалась без обращения к питон-скрипту.
	constexpr int32 Stride = 7;

	if (!Tracks.IsValidIndex(TrackIndex) || NumKeys < 1 || FrameRate <= 0.f)
	{
		return false;
	}
	const FAsdaBackdropAnimTrack& Track = Tracks[TrackIndex];
	if (Track.Keys.Num() < NumKeys * Stride)
	{
		return false;
	}

	// Позиция во времени → дробный индекс ключа. Шаг равномерный (проверено экспортёром), поэтому
	// бинарный поиск по временам не нужен — индекс считается прямо.
	const float KeyPos = FMath::Clamp(Time * FrameRate, 0.f, static_cast<float>(NumKeys - 1));
	const int32 K0 = FMath::Clamp(FMath::FloorToInt(KeyPos), 0, NumKeys - 1);
	const int32 K1 = FMath::Min(K0 + 1, NumKeys - 1);
	const float Alpha = KeyPos - static_cast<float>(K0);

	const float* A = Track.Keys.GetData() + K0 * Stride;
	const float* B = Track.Keys.GetData() + K1 * Stride;

	const FVector LocA(A[0], A[1], A[2]);
	const FVector LocB(B[0], B[1], B[2]);
	// Кватернионы экспортёр выдаёт с НЕПРЕРЫВНЫМ знаком вдоль трека, но нормализацию всё равно
	// делаем здесь: запись в JSON округлена, а FQuat::Slerp требует единичной нормы.
	const FQuat RotA = FQuat(A[3], A[4], A[5], A[6]).GetNormalized();
	const FQuat RotB = FQuat(B[3], B[4], B[5], B[6]).GetNormalized();

	OutDelta = FTransform(FQuat::Slerp(RotA, RotB, Alpha).GetNormalized(),
	                      FMath::Lerp(LocA, LocB, static_cast<double>(Alpha)),
	                      FVector::OneVector);
	return true;
}
