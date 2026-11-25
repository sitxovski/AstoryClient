// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkinTuneActor — реализация живого тюнера (см. заголовок).
#include "AsdaSkinTuneActor.h"
#include "ProceduralMeshComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "DrawDebugHelpers.h"

// ---------- мат-помощники (row-vector v·M, как D3D/эталон/UE) ----------
static FMatrix44f MatMul(const FMatrix44f& A, const FMatrix44f& B)
{
	FMatrix44f R;
	for (int i=0;i<4;i++) for (int j=0;j<4;j++)
	{ float s=0; for (int k=0;k<4;k++) s+=A.M[i][k]*B.M[k][j]; R.M[i][j]=s; }
	return R;
}
static FVector3f XformPos(const FMatrix44f& M, const FVector3f& v)
{
	return FVector3f(
		v.X*M.M[0][0]+v.Y*M.M[1][0]+v.Z*M.M[2][0]+M.M[3][0],
		v.X*M.M[0][1]+v.Y*M.M[1][1]+v.Z*M.M[2][1]+M.M[3][1],
		v.X*M.M[0][2]+v.Y*M.M[1][2]+v.Z*M.M[2][2]+M.M[3][2]);
}
static FVector3f XformDir(const FMatrix44f& M, const FVector3f& v)
{
	return FVector3f(
		v.X*M.M[0][0]+v.Y*M.M[1][0]+v.Z*M.M[2][0],
		v.X*M.M[0][1]+v.Y*M.M[1][1]+v.Z*M.M[2][1],
		v.X*M.M[0][2]+v.Y*M.M[1][2]+v.Z*M.M[2][2]);
}

AAsdaSkinTuneActor::AAsdaSkinTuneActor()
{
	PrimaryActorTick.bCanEverTick = true;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->bUseAsyncCooking = true;
	RawDir = FPaths::ProjectDir() / TEXT("RawAssets/Models/_AnimTest/raw");
}

FString AAsdaSkinTuneActor::CurrentRawPath() const
{
	const TCHAR* f = (Model==EAsdaModel::Opara) ? TEXT("opara.asdaraw") : TEXT("scorpion.asdaraw");
	return RawDir / f;
}

// ---------- чтение .asdaraw ----------
struct FCursor { const uint8* P; const uint8* End;
	template<typename T> T Get(){ T v{}; if (P+sizeof(T)<=End){ FMemory::Memcpy(&v,P,sizeof(T)); P+=sizeof(T);} return v; } };

bool AAsdaSkinTuneActor::LoadRaw()
{
	Meshes.Reset(); Bones.Reset(); bLoaded=false;
	TArray<uint8> Bytes;
	const FString Path = CurrentRawPath();
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{ UE_LOG(LogTemp, Warning, TEXT("[AsdaSkinTune] не найден %s"), *Path); return false; }
	FCursor c{ Bytes.GetData(), Bytes.GetData()+Bytes.Num() };
	if (Bytes.Num()<8 || FMemory::Memcmp(c.P,"ASDR",4)!=0)
	{ UE_LOG(LogTemp, Warning, TEXT("[AsdaSkinTune] не ASDR")); return false; }
	c.P+=4; c.Get<uint32>(); // version
	const uint32 meshCount=c.Get<uint32>();
	for (uint32 mi=0; mi<meshCount; ++mi)
	{
		FAsdaMeshCPU M;
		const uint32 V=c.Get<uint32>(); const uint32 idxN=c.Get<uint32>();
		M.Verts.SetNum(V);
		for (uint32 i=0;i<V;i++){ FAsdaVtx& vx=M.Verts[i];
			vx.Pos.X=c.Get<float>(); vx.Pos.Y=c.Get<float>(); vx.Pos.Z=c.Get<float>();
			vx.Nrm.X=c.Get<float>(); vx.Nrm.Y=c.Get<float>(); vx.Nrm.Z=c.Get<float>();
			vx.UV.X=c.Get<float>(); vx.UV.Y=c.Get<float>(); }
		for (uint32 i=0;i<V;i++){ M.Verts[i].B0=c.Get<int32>(); M.Verts[i].B1=c.Get<int32>(); }
		for (uint32 i=0;i<V;i++){ M.Verts[i].W0=c.Get<float>(); }
		M.Idx.SetNum(idxN);
		for (uint32 i=0;i<idxN;i++) M.Idx[i]=(int32)c.Get<uint32>();
		Meshes.Add(MoveTemp(M));
	}
	const uint32 boneCount=c.Get<uint32>();
	Bones.SetNum(boneCount);
	float dur=0;
	for (uint32 bi=0; bi<boneCount; ++bi)
	{
		FAsdaBone& B=Bones[bi];
		B.Parent=c.Get<int32>(); B.Conv=c.Get<float>();
		const uint32 kc=c.Get<uint32>();
		B.Keys.SetNum(kc);
		for (uint32 k=0;k<kc;k++){ FAsdaKey& K=B.Keys[k];
			K.TimeMs=c.Get<float>();
			K.Pos.X=c.Get<float>(); K.Pos.Y=c.Get<float>(); K.Pos.Z=c.Get<float>();
			K.Quat.X=c.Get<float>(); K.Quat.Y=c.Get<float>(); K.Quat.Z=c.Get<float>(); K.Quat.W=c.Get<float>();
			K.Scale.X=c.Get<float>(); K.Scale.Y=c.Get<float>(); K.Scale.Z=c.Get<float>(); }
		if (kc) dur=FMath::Max(dur, B.Keys[kc-1].TimeMs);
	}
	Fps=c.Get<float>(); if (Fps<=0) Fps=30;
	// имена (для отладки)
	const uint32 nameLen=c.Get<uint32>();
	if (nameLen && c.P+nameLen<=c.End){
		TArray<ANSICHAR> buf; buf.Append((const ANSICHAR*)c.P, nameLen); buf.Add('\0');
		FString all=FString(UTF8_TO_TCHAR(buf.GetData()));
		TArray<FString> parts; all.ParseIntoArray(parts, TEXT("\n"), false);
		for (int i=0;i<parts.Num() && i<Bones.Num();++i) Bones[i].Name=parts[i]; }

	NumBones=Bones.Num(); NumMeshes=Meshes.Num(); NumVerts=0; for (auto&m:Meshes) NumVerts+=m.Verts.Num(); DurationMs=dur;
	ComputeBoneFlags();
	bLoaded=true;
	UE_LOG(LogTemp, Log, TEXT("[AsdaSkinTune] %s: meshes=%d verts=%d bones=%d dur=%.0fms"),
		*FPaths::GetCleanFilename(Path), Meshes.Num(), NumVerts, NumBones, dur);
	return true;
}

// ---------- флаги костей (skinned / kept после prune) ----------
void AAsdaSkinTuneActor::ComputeBoneFlags()
{
	const int B=Bones.Num();
	bSkinnedBone.Init(0,B);
	for (const auto& m:Meshes) for (const auto& v:m.Verts)
	{ if (v.B0>=0&&v.B0<B) bSkinnedBone[v.B0]=1; if (v.B1>=0&&v.B1<B) bSkinnedBone[v.B1]=1; }
	bKeptBone.Init(1,B);
	bool changed=true;
	while (changed){ changed=false;
		TArray<uint8> hasChild; hasChild.Init(0,B);
		for (int i=0;i<B;i++){ const int p=Bones[i].Parent; if (bKeptBone[i]&&p>=0&&p<B) hasChild[p]=1; }
		for (int i=0;i<B;i++) if (bKeptBone[i]&&!bSkinnedBone[i]&&!hasChild[i]){ bKeptBone[i]=0; changed=true; } }
	NumSkinnedBones=0; NumKeptBones=0;
	for (int i=0;i<B;i++){ NumSkinnedBones+=bSkinnedBone[i]; NumKeptBones+=bKeptBone[i]; }
}

// ---------- матрица кости (вся конвенция на контроллерах) ----------
FMatrix44f AAsdaSkinTuneActor::BoneLocalMatrix(const FAsdaKey& K, float Conv) const
{
	float x=K.Quat.X, y=K.Quat.Y, z=K.Quat.Z, w=K.Quat.W;
	if (bNegateQuatW) w=-w;
	if (bConjugateQuat){ x=-x; y=-y; z=-z; }
	FMatrix44f M=FMatrix44f::Identity;
	if (QuatMat==EAsdaQuatMat::EngineExact)
	{ // ДОСЛОВНО mathQuaternionToMatrix4x4 (row-major, this[i] → M.M[row][col])
		M.M[0][0]=1-2*(y*y+z*z); M.M[0][1]=2*(x*y-w*z);   M.M[0][2]=2*(x*z+w*y);
		M.M[1][0]=2*(x*y+w*z);   M.M[1][1]=1-2*(x*x+z*z); M.M[1][2]=2*(y*z-w*x);
		M.M[2][0]=2*(x*z-w*y);   M.M[2][1]=2*(y*z+w*x);   M.M[2][2]=1-2*(x*x+y*y);
	}
	else
	{ // Standard = транспонированная (другие знаки off-diagonal)
		M.M[0][0]=1-2*(y*y+z*z); M.M[0][1]=2*(x*y+w*z);   M.M[0][2]=2*(x*z-w*y);
		M.M[1][0]=2*(x*y-w*z);   M.M[1][1]=1-2*(x*x+z*z); M.M[1][2]=2*(y*z+w*x);
		M.M[2][0]=2*(x*z+w*y);   M.M[2][1]=2*(y*z-w*x);   M.M[2][2]=1-2*(x*x+y*y);
	}
	const float c = bApplyConvert ? Conv : 1.0f;
	for (int i=0;i<3;i++) for (int j=0;j<3;j++) M.M[i][j]*=c;
	const FVector3f& s=K.Scale;
	if (ScaleMode==EAsdaScaleMode::ScaleRows)
	{ for (int j=0;j<3;j++){ M.M[0][j]*=s.X; M.M[1][j]*=s.Y; M.M[2][j]*=s.Z; } }
	else
	{ for (int i=0;i<3;i++){ M.M[i][0]*=s.X; M.M[i][1]*=s.Y; M.M[i][2]*=s.Z; } }
	M.M[3][0]=K.Pos.X; M.M[3][1]=K.Pos.Y; M.M[3][2]=K.Pos.Z;
	return M;
}

void AAsdaSkinTuneActor::SampleBone(const FAsdaBone& B, float TimeMs, FAsdaKey& Out) const
{
	const int n=B.Keys.Num();
	if (n==0){ Out.Pos=FVector3f::ZeroVector; Out.Quat=FQuat4f::Identity; Out.Scale=FVector3f::OneVector; return; }
	if (n==1){ Out=B.Keys[0]; return; }
	const float last=B.Keys[n-1].TimeMs;
	float tt = (last>0) ? FMath::Fmod(TimeMs, last) : 0.0f; if (tt<0) tt+=last;
	int k=0; while (k<n-1 && B.Keys[k+1].TimeMs<=tt) ++k;
	const int k2=FMath::Min(k+1,n-1);
	const float t0=B.Keys[k].TimeMs, t1=B.Keys[k2].TimeMs;
	const float f=(t1>t0)?(tt-t0)/(t1-t0):0.0f;
	Out.Pos=FMath::Lerp(B.Keys[k].Pos, B.Keys[k2].Pos, f);
	Out.Scale=FMath::Lerp(B.Keys[k].Scale, B.Keys[k2].Scale, f);
	FQuat4f q0=B.Keys[k].Quat, q1=B.Keys[k2].Quat;
	Out.Quat = bSlerp ? FQuat4f::Slerp(q0,q1,f) : FMath::Lerp(q0,q1,f).GetNormalized();
}

void AAsdaSkinTuneActor::ComputeWorld(float TimeMs, TArray<FMatrix44f>& W) const
{
	const int B=Bones.Num(); W.SetNum(B);
	TArray<FMatrix44f> L; L.SetNum(B);
	for (int i=0;i<B;i++){
		// заморозка в rest: руч. срез по индексу ИЛИ обрезка не-скиннутых листьев (как prune_bones).
		const bool frozen = (FreezeBonesFromIndex>=0 && i>=FreezeBonesFromIndex)
			|| (bStripUnskinnedLeaves && bKeptBone.IsValidIndex(i) && !bKeptBone[i]);
		FAsdaKey k; SampleBone(Bones[i], frozen?0.0f:TimeMs, k); L[i]=BoneLocalMatrix(k,Bones[i].Conv); }
	if (SkelMode==EAsdaSkelMode::Flat){ W=L; return; }
	auto RotPart=[](FMatrix44f M){ M.M[3][0]=0; M.M[3][1]=0; M.M[3][2]=0; return M; };
	TArray<uint8> done; done.Init(0,B);
	int safety=0;
	while (safety++<=B+2)
	{
		bool any=false;
		for (int i=0;i<B;i++){ if (done[i]) continue;
			const int p=Bones[i].Parent;
			if (p<0 || p>=B){ W[i]=L[i]; done[i]=1; any=true; }
			else if (done[p]){
				if (SkelMode==EAsdaSkelMode::HierRotOnly)
				{ // от родителя только ВРАЩЕНИЕ; позиция = абсолютная из кейфрейма (не улетает)
					FMatrix44f c=MatMul(RotPart(L[i]), RotPart(W[p]));
					c.M[3][0]=L[i].M[3][0]; c.M[3][1]=L[i].M[3][1]; c.M[3][2]=L[i].M[3][2];
					W[i]=c;
				}
				else
					W[i]= bHierParentFirst ? MatMul(W[p],L[i]) : MatMul(L[i],W[p]);
				done[i]=1; any=true; } }
		if (!any) break;
	}
	for (int i=0;i<B;i++) if (!done[i]) W[i]=L[i];
}

FMatrix44f AAsdaSkinTuneActor::GlobalBasis() const
{
	int px=0,py=1,pz=2;
	switch (AxisPerm){
		case EAsdaAxisPerm::XYZ: px=0;py=1;pz=2; break;
		case EAsdaAxisPerm::XZY: px=0;py=2;pz=1; break;
		case EAsdaAxisPerm::YXZ: px=1;py=0;pz=2; break;
		case EAsdaAxisPerm::YZX: px=1;py=2;pz=0; break;
		case EAsdaAxisPerm::ZXY: px=2;py=0;pz=1; break;
		case EAsdaAxisPerm::ZYX: px=2;py=1;pz=0; break;
	}
	const float sx=(bNegX?-1.f:1.f)*GlobalScale, sy=(bNegY?-1.f:1.f)*GlobalScale, sz=(bNegZ?-1.f:1.f)*GlobalScale;
	FMatrix44f Bm=FMatrix44f(EForceInit::ForceInitToZero); Bm.M[3][3]=1.f;
	Bm.M[px][0]=sx; Bm.M[py][1]=sy; Bm.M[pz][2]=sz;   // out.col = in[src]*sign*scale
	// ExtraRotate
	if (!ExtraRotate.IsNearlyZero())
	{
		const FMatrix Rd=FQuatRotationMatrix(ExtraRotate.Quaternion());
		FMatrix44f R; for (int i=0;i<4;i++) for (int j=0;j<4;j++) R.M[i][j]=(float)Rd.M[i][j];
		Bm=MatMul(Bm,R);
	}
	return Bm;
}

void AAsdaSkinTuneActor::BuildSections()
{
	if (!bLoaded) return;
	Mesh->ClearAllMeshSections();
	UpdateSkin(); // создаст секции (см. ниже: если секции нет — Create)
}

void AAsdaSkinTuneActor::UpdateSkin()
{
	if (!bLoaded || Bones.Num()==0) return;
	const float t = bAnimated ? PlayTimeMs : ManualTimeMs;
	const int B=Bones.Num();
	TArray<FMatrix44f> world; ComputeWorld(t, world);
	TArray<FMatrix44f> skin; skin.SetNum(B);
	if (BindMode==EAsdaBindMode::Identity){ skin=world; }
	else { TArray<FMatrix44f> rest; ComputeWorld(0.0f, rest);
		for (int i=0;i<B;i++) skin[i]=MatMul(rest[i].Inverse(), world[i]); }
	const FMatrix44f G=GlobalBasis();

	for (int si=0; si<Meshes.Num(); ++si)
	{
		const FAsdaMeshCPU& M=Meshes[si];
		const int V=M.Verts.Num();
		TArray<FVector> Pos; Pos.SetNum(V);
		TArray<FVector> Nrm; Nrm.SetNum(V);
		TArray<FVector2D> UV; UV.SetNum(V);
		TArray<FLinearColor> Col; Col.Init(FLinearColor::White, V);
		TArray<FProcMeshTangent> Tan; Tan.Init(FProcMeshTangent(), V);
		for (int i=0;i<V;i++)
		{
			const FAsdaVtx& vx=M.Verts[i];
			FVector3f p, n;
			if (bRawNoSkin){ p=vx.Pos; n=vx.Nrm; }   // диагностика: сырьё .jbm без костей
			else
			{
				const int b0=FMath::Clamp(vx.B0,0,B-1);
				p=XformPos(skin[b0], vx.Pos); n=XformDir(skin[b0], vx.Nrm);
				if (bTwoBoneBlend && vx.B1>=0 && vx.W0<0.999f)
				{ const int b1=FMath::Clamp(vx.B1,0,B-1); const float w1=1.f-vx.W0;
					p=p*vx.W0 + XformPos(skin[b1],vx.Pos)*w1;
					n=n*vx.W0 + XformDir(skin[b1],vx.Nrm)*w1; }
			}
			p=XformPos(G,p); n=XformDir(G,n);
			if (ClampVertDist>0.f){ const float d=p.Size(); if (d>ClampVertDist) p*=(ClampVertDist/d); }
			Pos[i]=FVector(p); Nrm[i]=FVector(n.GetSafeNormal()); UV[i]=FVector2D(vx.UV.X, vx.UV.Y);
		}
		if (Mesh->GetNumSections()>si)
			Mesh->UpdateMeshSection_LinearColor(si, Pos, Nrm, UV, Col, Tan);
		else
		{
			TArray<int32> Tri=M.Idx;
			if (bReverseWinding) for (int i=0;i+2<Tri.Num();i+=3) Swap(Tri[i+1],Tri[i+2]);
			Mesh->CreateMeshSection_LinearColor(si, Pos, Tri, Nrm, UV, Col, Tan, false);
		}
		Mesh->SetMeshSectionVisible(si, SingleMeshIndex<0 || si==SingleMeshIndex);
	}
}

void AAsdaSkinTuneActor::OnConstruction(const FTransform& Xform)
{
	Super::OnConstruction(Xform);
	LoadRaw();
	BuildSections();
}

void AAsdaSkinTuneActor::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!bLoaded) return;
	if (bAnimated) PlayTimeMs += Dt*1000.0f*TimeScale;
	UpdateSkin();
	if (bDrawBones)
	{
		TArray<FMatrix44f> world; ComputeWorld(bAnimated?PlayTimeMs:ManualTimeMs, world);
		const FMatrix44f G=GlobalBasis(); const FTransform AX=GetActorTransform();
		auto Shown=[&](int i){ if (bDrawOnlySkinned && bSkinnedBone.IsValidIndex(i) && !bSkinnedBone[i]) return false;
			if (bStripUnskinnedLeaves && bKeptBone.IsValidIndex(i) && !bKeptBone[i]) return false; return true; };
		for (int i=0;i<Bones.Num();++i)
		{
			if (!Shown(i)) continue;
			FVector3f o=XformPos(G, XformPos(world[i], FVector3f::ZeroVector));
			FVector wp=AX.TransformPosition(FVector(o));
			const FColor col = bSkinnedBone.IsValidIndex(i)&&bSkinnedBone[i] ? FColor::Green : FColor::Yellow;
			DrawDebugPoint(GetWorld(), wp, 7.f, col, false, -1.f);
			if (bDrawBoneNames) DrawDebugString(GetWorld(), wp, FString::Printf(TEXT("%d:%s"),i,*Bones[i].Name), nullptr, FColor::White, 0.f);
			const int p=Bones[i].Parent;
			if (p>=0 && p<Bones.Num() && Shown(p)){ FVector3f po=XformPos(G,XformPos(world[p],FVector3f::ZeroVector));
				DrawDebugLine(GetWorld(), AX.TransformPosition(FVector(po)), wp, FColor::Cyan, false, -1.f, 0, 0.5f); }
		}
	}
}

#if WITH_EDITOR
void AAsdaSkinTuneActor::PostEditChangeProperty(FPropertyChangedEvent& E)
{
	Super::PostEditChangeProperty(E);
	const FName N = E.Property ? E.Property->GetFName() : NAME_None;
	if (N==GET_MEMBER_NAME_CHECKED(AAsdaSkinTuneActor, Model) ||
		N==GET_MEMBER_NAME_CHECKED(AAsdaSkinTuneActor, RawDir))
		LoadRaw();
	BuildSections();   // winding/геометрия + первый скин
	UpdateSkin();
}
#endif
