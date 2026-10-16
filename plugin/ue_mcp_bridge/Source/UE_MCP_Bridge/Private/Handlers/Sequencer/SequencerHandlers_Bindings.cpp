// What a Level Sequence's bindings resolve to per runtime player in a PIE or
// game world (#1232).

#include "SequencerHandlers.h"
#include "SequencerHandlers_Internal.h"
#include "HandlerSequencerTracks.h"
#include "HandlerUtils.h"

#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "DefaultLevelSequenceInstanceData.h"
#include "MovieScene.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieSceneSequencePlayer.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "Editor.h"

namespace UEMCP::SequencerBindings
{
	using namespace UEMCP::SequencerTracks;

	TSharedPtr<FJsonObject> DescribeBoundObject(UObject* Object)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Object->GetName());
		Obj->SetStringField(TEXT("path"), Object->GetPathName());
		Obj->SetStringField(TEXT("class"), Object->GetClass()->GetName());
		FTransform World;
		bool bHasTransform = false;
		if (AActor* Actor = Cast<AActor>(Object))
		{
			Obj->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
			World = Actor->GetActorTransform();
			bHasTransform = true;
		}
		else if (USceneComponent* Comp = Cast<USceneComponent>(Object))
		{
			if (AActor* Owner = Comp->GetOwner()) Obj->SetStringField(TEXT("ownerActorLabel"), Owner->GetActorLabel());
			World = Comp->GetComponentTransform();
			bHasTransform = true;
		}
		if (bHasTransform)
		{
			Obj->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(World.GetLocation()));
			Obj->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(World.Rotator()));
			Obj->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(World.GetScale3D()));
		}
		return Obj;
	}

	FString BindingName(UMovieScene* MovieScene, const FGuid& Guid)
	{
		if (const FMovieScenePossessable* P = MovieScene->FindPossessable(Guid)) return P->GetName();
		if (const FMovieSceneSpawnable* S = MovieScene->FindSpawnable(Guid)) return S->GetName();
		return FString();
	}

	void CollectBindingGuids(UMovieScene* MovieScene, TArray<FGuid>& Out, TMap<FGuid, FString>& Types)
	{
		for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
		{
			const FGuid G = MovieScene->GetPossessable(i).GetGuid();
			Out.Add(G);
			Types.Add(G, TEXT("possessable"));
		}
		for (int32 i = 0; i < MovieScene->GetSpawnableCount(); ++i)
		{
			const FGuid G = MovieScene->GetSpawnable(i).GetGuid();
			Out.Add(G);
			Types.Add(G, TEXT("spawnable"));
		}
	}
}

// get_sequence_runtime_bindings (#1232): every LevelSequenceActor in a world,
// its player state and instance-data origin, and the objects each binding of
// that player resolves to. This is what tells an actor spawned by a player
// apart from the duplicates an open Sequencer editor spawns.
TSharedPtr<FJsonValue> FSequencerHandlers::GetRuntimeBindings(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UEMCP::SequencerTracks;
	using namespace UEMCP::SequencerBindings;

	const FString Scope = OptionalString(Params, TEXT("world"), TEXT("auto"));
	UWorld* World = ResolveWorldFromParams(Params, TEXT("auto"));
	const FString SequenceFilter = OptionalString(Params, TEXT("sequencePath"));
	const FString ActorFilter = OptionalString(Params, TEXT("actorLabel"));

	if (!World)
	{
		return MCPError(FString::Printf(TEXT("No %s world is available. Start PIE, or pass world 'editor'."), *Scope));
	}

	TArray<TSharedPtr<FJsonValue>> ActorsJson;
	for (TActorIterator<ALevelSequenceActor> It(World); It; ++It)
	{
		ALevelSequenceActor* SeqActor = *It;
		if (!SeqActor) continue;
		if (!ActorFilter.IsEmpty() && !SeqActor->GetActorLabel().Equals(ActorFilter, ESearchCase::IgnoreCase) && !SeqActor->GetName().Equals(ActorFilter, ESearchCase::IgnoreCase)) continue;
		ULevelSequence* Sequence = SeqActor->GetSequence();
		if (!SequenceFilter.IsEmpty())
		{
			if (!Sequence) continue;
			const FString SeqPath = Sequence->GetPathName();
			if (!SeqPath.Equals(SequenceFilter, ESearchCase::IgnoreCase) && !SeqPath.StartsWith(SequenceFilter + TEXT("."), ESearchCase::IgnoreCase)
				&& !Sequence->GetName().Equals(SequenceFilter, ESearchCase::IgnoreCase)) continue;
		}

		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("actorLabel"), SeqActor->GetActorLabel());
		A->SetStringField(TEXT("actorPath"), SeqActor->GetPathName());
		A->SetStringField(TEXT("sequencePath"), Sequence ? Sequence->GetPathName() : FString());
		A->SetBoolField(TEXT("overrideInstanceData"), SeqActor->bOverrideInstanceData != 0);
		if (UDefaultLevelSequenceInstanceData* Instance = Cast<UDefaultLevelSequenceInstanceData>(SeqActor->DefaultInstanceData))
		{
			TSharedPtr<FJsonObject> I = MakeShared<FJsonObject>();
			I->SetObjectField(TEXT("transformOrigin"), MCPTransformToJsonObject(Instance->TransformOrigin));
			I->SetStringField(TEXT("transformOriginActor"), Instance->TransformOriginActor ? Instance->TransformOriginActor->GetPathName() : FString());
			A->SetObjectField(TEXT("instanceData"), I);
		}

		ULevelSequencePlayer* Player = SeqActor->GetSequencePlayer();
		A->SetBoolField(TEXT("hasPlayer"), Player != nullptr);
		if (Player && Sequence)
		{
			A->SetBoolField(TEXT("isPlaying"), Player->IsPlaying());
			A->SetBoolField(TEXT("isPaused"), Player->IsPaused());
			const FQualifiedFrameTime Now = Player->GetCurrentTime();
			TSharedPtr<FJsonObject> T = MakeShared<FJsonObject>();
			T->SetNumberField(TEXT("frame"), Now.Time.AsDecimal());
			T->SetNumberField(TEXT("seconds"), Now.AsSeconds());
			UEMCP::SequencerInfo::SetFrameRateFields(*T, Now.Rate);
			A->SetObjectField(TEXT("currentTime"), T);

			UMovieScene* MovieScene = Sequence->GetMovieScene();
			TArray<FGuid> Guids;
			TMap<FGuid, FString> Types;
			if (MovieScene) CollectBindingGuids(MovieScene, Guids, Types);
			TArray<TSharedPtr<FJsonValue>> BindingsJson;
			for (const FGuid& Guid : Guids)
			{
				TSharedPtr<FJsonObject> B = MakeShared<FJsonObject>();
				B->SetStringField(TEXT("name"), BindingName(MovieScene, Guid));
				B->SetStringField(TEXT("guid"), Guid.ToString());
				B->SetStringField(TEXT("type"), Types.FindRef(Guid));
				if (const FMovieScenePossessable* P = MovieScene->FindPossessable(Guid))
				{
					if (P->GetParent().IsValid()) B->SetStringField(TEXT("parentGuid"), P->GetParent().ToString());
				}
				TArray<TSharedPtr<FJsonValue>> Bound;
				for (UObject* O : Player->GetBoundObjects(FMovieSceneObjectBindingID(UE::MovieScene::FRelativeObjectBindingID(Guid))))
				{
					if (O) Bound.Add(MakeShared<FJsonValueObject>(DescribeBoundObject(O)));
				}
				B->SetArrayField(TEXT("boundObjects"), Bound);
				BindingsJson.Add(MakeShared<FJsonValueObject>(B));
			}
			A->SetArrayField(TEXT("bindings"), BindingsJson);
		}
		ActorsJson.Add(MakeShared<FJsonValueObject>(A));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("world"), World->GetPathName());
	Result->SetStringField(TEXT("worldType"), World->IsPlayInEditor() ? TEXT("pie") : (World->IsGameWorld() ? TEXT("game") : TEXT("editor")));
	Result->SetArrayField(TEXT("sequenceActors"), ActorsJson);
	Result->SetNumberField(TEXT("sequenceActorCount"), ActorsJson.Num());
	return MCPResult(Result);
}
