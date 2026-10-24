// Test script CRUD and run lifecycle handlers. Members of FPIEHandlers.

#include "Handlers/PIE/PIEHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "Handlers/PIE/MCPPIETestScript.h"
#include "Handlers/PIE/MCPPIEObservationProfile.h"
#include "Handlers/PIE/PIEScriptRunner.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace
{
	using namespace MCPPIE;

	const TCHAR* const ScriptDefaultPackage = TEXT("/Game/PIEStudio/Scripts");

	UMCPPIETestScript* LoadTestScriptAsset(const FString& Path)
	{
		UMCPPIETestScript* Asset = LoadObject<UMCPPIETestScript>(nullptr, *Path);
		if (!Asset && !Path.Contains(TEXT(".")))
		{
			Asset = LoadObject<UMCPPIETestScript>(nullptr, *(Path + TEXT(".") + FPackageName::GetShortName(Path)));
		}
		return Asset;
	}

	void TestScriptFieldsToJson(const UMCPPIETestScript* Asset, const FString& Path, TSharedPtr<FJsonObject> R)
	{
		TArray<FScriptStepDef> Steps;
		ScriptStepsFromAsset(Asset, Steps);
		R->SetStringField(TEXT("path"), Path);
		R->SetStringField(TEXT("name"), Asset->GetName());
		R->SetStringField(TEXT("description"), Asset->Description);
		R->SetStringField(TEXT("profile"), Asset->Profile);
		R->SetNumberField(TEXT("default_within_ms"), Asset->DefaultWithinMs);
		R->SetArrayField(TEXT("steps"), ScriptStepDefsToJson(Steps));
		R->SetNumberField(TEXT("step_count"), Steps.Num());
	}

	// The fields pie_script_update takes, as they stand now: the inverse of an update or replace.
	TSharedPtr<FJsonObject> TestScriptUpdatePayload(const UMCPPIETestScript* Asset, const FString& Path)
	{
		TArray<FScriptStepDef> Steps;
		ScriptStepsFromAsset(Asset, Steps);
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("path"), Path);
		P->SetArrayField(TEXT("steps"), ScriptStepDefsToJson(Steps));
		P->SetStringField(TEXT("description"), Asset->Description);
		P->SetStringField(TEXT("profile"), Asset->Profile);
		P->SetNumberField(TEXT("default_within_ms"), Asset->DefaultWithinMs);
		return P;
	}

	FString TestScriptStateKey(const UMCPPIETestScript* Asset)
	{
		TSharedPtr<FJsonObject> P = TestScriptUpdatePayload(Asset, FString());
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(P.ToSharedRef(), W);
		return Out;
	}

	bool TestScriptProfileExists(const FString& Profile)
	{
		return Profile.IsEmpty() || LoadObject<UMCPPIEObservationProfile>(nullptr, *Profile) != nullptr;
	}

	FString ScriptVerdictForStatus(EScriptStepStatus Status)
	{
		switch (Status)
		{
		case EScriptStepStatus::Passed: return TEXT("pass");
		case EScriptStepStatus::Failed: return TEXT("fail");
		case EScriptStepStatus::Skipped: return TEXT("skip");
		default: return FString();
		}
	}

	void ScriptRunFieldsToJson(const FScriptRun& Run, TSharedPtr<FJsonObject> R, int32 SinceSeq)
	{
		R->SetStringField(TEXT("run_id"), Run.RunId);
		R->SetStringField(TEXT("script"), Run.ScriptPath);
		R->SetStringField(TEXT("script_name"), Run.ScriptName);
		R->SetStringField(TEXT("state"), ScriptRunStateToString(Run.State));
		R->SetNumberField(TEXT("current_step"), Run.CurrentStep);
		R->SetStringField(TEXT("output_dir"), Run.OutputDir);
		R->SetStringField(TEXT("results_path"), Run.ResultsPath);
		R->SetStringField(TEXT("observation_run_id"), Run.ObservationRunId);
		if (!Run.RecordingId.IsEmpty()) R->SetStringField(TEXT("recording_id"), Run.RecordingId);
		if (Run.bReplay) R->SetStringField(TEXT("replay_of"), Run.ReplayOf);
		R->SetNumberField(TEXT("elapsed_ms"), Run.NowMs);

		TArray<TSharedPtr<FJsonValue>> Steps;
		for (int32 I = 0; I < Run.Steps.Num(); ++I)
		{
			Steps.Add(MakeShared<FJsonValueObject>(ScriptStepRunToJson(Run.Steps[I], I)));
		}
		R->SetArrayField(TEXT("steps"), Steps);

		TArray<TSharedPtr<FJsonValue>> Awaiting;
		for (int32 I : ScriptAwaitingSteps(Run))
		{
			TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetNumberField(TEXT("index"), I);
			A->SetStringField(TEXT("instruction"), Run.Steps[I].Def.Instruction);
			Awaiting.Add(MakeShared<FJsonValueObject>(A));
		}
		R->SetArrayField(TEXT("awaiting"), Awaiting);

		TArray<TSharedPtr<FJsonValue>> Events;
		for (const FScriptEvent& E : Run.Events)
		{
			if (E.Seq > SinceSeq) Events.Add(MakeShared<FJsonValueObject>(ScriptEventToJson(E, Run.RunId)));
		}
		R->SetArrayField(TEXT("events"), Events);
		R->SetNumberField(TEXT("last_seq"), Run.NextSeq - 1);
		R->SetObjectField(TEXT("summary"), ScriptSummaryToJson(SummarizeScriptRun(Run), Run.bReplay));
	}
}

void FPIEHandlers::RegisterScriptHandlers(FMCPHandlerRegistry& Registry)
{
	using EType = EMCPParamType;
	const TCHAR* const PathDoc = TEXT("Test script asset path");
	const TCHAR* const RunIdDoc = TEXT("Script run id (default the current or last run)");
	const TCHAR* const WaitDoc = TEXT("Block until the run finishes or awaits a verdict");
	const TCHAR* const TimeoutDoc = TEXT("Most seconds to wait when wait is set");
	const TCHAR* const PieInstanceDoc = TEXT("FWorldContext PIEInstance to observe (default the active PIE world)");
	const TCHAR* const ClientIdDoc = TEXT("Local-player index inside the selected PIE world (default 0)");

	// One step: {instruction, judge auto|manual, expect {value, op decreasing|increasing|eq|gt|lt|visible|exists, target, within_ms}}.
	// The shape is validated in the handler, so the replay actions' inline steps and these share one key type.
	const TCHAR* const StepsShape = TEXT("{instruction, judge: auto|manual (default auto), expect: {value (tracked value path or last segment, actor id for exists, widget for visible), op: decreasing|increasing|eq|gt|lt|visible|exists, target (number for eq/gt/lt, boolean for visible/exists, default true), within_ms}}");

	Registry.RegisterHandler(TEXT("pie_script_create"), &PieScriptCreate, {
		MCPParam::Required(TEXT("name"), EType::String, TEXT("Asset name")),
		MCPParam::Optional(TEXT("package_path"), EType::String, TEXT("Package folder (default /Game/PIEStudio/Scripts)")),
		MCPParam::Required(TEXT("steps"), EType::Array, *FString::Printf(TEXT("Steps, run in order: %s"), StepsShape)).Items(EType::Object),
		MCPParam::Optional(TEXT("description"), EType::String, TEXT("What the script tests")),
		MCPParam::Optional(TEXT("profile"), EType::String, TEXT("Observation profile asset path")),
		MCPParam::Optional(TEXT("default_within_ms"), EType::Integer, TEXT("Default auto-step window in game ms (default 5000)")).Min(1),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("When the asset exists: skip (default), update or replace (overwrite it), error")).Enum({
			TEXT("skip"), TEXT("update"), TEXT("replace"), TEXT("error") }),
	}, MCPSpec::ContractExempt(TEXT("Creates and saves a script asset")));
	Registry.RegisterHandler(TEXT("pie_script_read"), &PieScriptRead, {
		MCPParam::Required(TEXT("path"), EType::String, PathDoc),
	});
	Registry.RegisterHandler(TEXT("pie_script_update"), &PieScriptUpdate, {
		MCPParam::Required(TEXT("path"), EType::String, PathDoc),
		MCPParam::Optional(TEXT("steps"), EType::Array, *FString::Printf(TEXT("Replacement steps: %s"), StepsShape)).Items(EType::Object),
		MCPParam::Optional(TEXT("description"), EType::String, TEXT("What the script tests")),
		MCPParam::Optional(TEXT("profile"), EType::String, TEXT("Observation profile asset path; empty clears it")),
		MCPParam::Optional(TEXT("default_within_ms"), EType::Integer, TEXT("Default auto-step window in game ms")).Min(1),
	});
	Registry.RegisterHandler(TEXT("pie_script_delete"), &PieScriptDelete, {
		MCPParam::Required(TEXT("path"), EType::String, PathDoc),
		MCPParam::Optional(TEXT("confirm"), EType::Boolean, TEXT("Must be true to delete")),
	});
	Registry.RegisterHandler(TEXT("pie_script_list"), &PieScriptList, {
		MCPParam::Optional(TEXT("directory"), EType::String, TEXT("Content folder to list under (default /Game)")),
	});
	Registry.RegisterHandler(TEXT("pie_script_run"), &PieScriptRun, {
		MCPParam::Optional(TEXT("script"), EType::String, TEXT("Test script asset path")),
		MCPParam::Optional(TEXT("steps"), EType::Array, *FString::Printf(TEXT("Inline steps instead of a script asset: %s"), StepsShape)).Items(EType::Object),
		MCPParam::Optional(TEXT("name"), EType::String, TEXT("Name for an inline run (default inline)")),
		MCPParam::Optional(TEXT("profile"), EType::String, TEXT("Observation profile asset path; overrides the script's")),
		MCPParam::Optional(TEXT("record"), EType::Boolean, TEXT("Record input so pie_script_replay can rerun it (default true)")),
		MCPParam::Optional(TEXT("start_pie"), EType::Boolean, TEXT("Start PIE when it is not running (default false)")),
		MCPParam::Optional(TEXT("block_on_manual"), EType::Boolean, TEXT("Hold the run at each manual block until every step in it has a verdict (default true)")),
		MCPParam::Optional(TEXT("default_within_ms"), EType::Integer, TEXT("Default auto-step window in game ms; overrides the script's (default 5000)")).Min(1),
		MCPParam::Optional(TEXT("pie_instance"), EType::Integer, PieInstanceDoc).Min(0),
		MCPParam::Optional(TEXT("client_id"), EType::Integer, ClientIdDoc).Min(0),
		MCPParam::Optional(TEXT("wait"), EType::Boolean, WaitDoc),
		MCPParam::Optional(TEXT("timeout_seconds"), EType::Number, TimeoutDoc).Min(0),
	}, MCPSpec::ExactlyOne({ { TEXT("script") }, { TEXT("steps") } })
		.ContractExempt(TEXT("Arms the observer and recorder and can start PIE")));
	Registry.RegisterHandler(TEXT("pie_script_status"), &PieScriptStatus, {
		MCPParam::Optional(TEXT("run_id"), EType::String, RunIdDoc),
		MCPParam::Optional(TEXT("since_seq"), EType::Integer, TEXT("Only events with a larger seq (default 0, every event)")).Min(0),
		MCPParam::Optional(TEXT("wait"), EType::Boolean, WaitDoc),
		MCPParam::Optional(TEXT("timeout_seconds"), EType::Number, TimeoutDoc).Min(0),
	});
	Registry.RegisterHandler(TEXT("pie_script_stop"), &PieScriptStop, {
		MCPParam::Optional(TEXT("run_id"), EType::String, TEXT("Run to stop; must be the active run when given")),
		MCPParam::Optional(TEXT("reason"), EType::String, TEXT("Recorded as the run's stop reason")),
	});
	Registry.RegisterHandler(TEXT("pie_script_verdict"), &PieScriptVerdict, {
		MCPParam::Required(TEXT("step"), EType::Integer, TEXT("Manual step index, 0-based")).Min(0),
		MCPParam::Required(TEXT("verdict"), EType::String, TEXT("Verdict")).Enum({ TEXT("pass"), TEXT("fail"), TEXT("skip") }),
		MCPParam::Optional(TEXT("note"), EType::String, TEXT("Tester's note")),
		MCPParam::Optional(TEXT("run_id"), EType::String, TEXT("Run the step belongs to (default the current or last run)")),
		MCPParam::Optional(TEXT("source"), EType::String, TEXT("Where the verdict came from (default action)")).Enum({ TEXT("action"), TEXT("elicitation") }),
	});
	Registry.RegisterHandler(TEXT("pie_script_results"), &PieScriptResults, {
		MCPParam::Optional(TEXT("run_id"), EType::String, RunIdDoc),
		MCPParam::Optional(TEXT("results_path"), EType::String, TEXT("results.json path, instead of a run id")),
	});
	Registry.RegisterHandler(TEXT("pie_script_replay"), &PieScriptReplay, {
		MCPParam::Optional(TEXT("run_id"), EType::String, TEXT("Finished run to replay (default the last run)")),
		MCPParam::Optional(TEXT("results_path"), EType::String, TEXT("results.json of the run to replay, instead of a run id")),
		MCPParam::Optional(TEXT("time_scale"), EType::Number, TEXT("Global time dilation during the replay (default 1)")).Min(0),
		MCPParam::Optional(TEXT("auto_stop_pie"), EType::Boolean, TEXT("End PIE when the replay finishes (default true)")),
		MCPParam::Optional(TEXT("pie_instance"), EType::Integer, PieInstanceDoc).Min(0),
		MCPParam::Optional(TEXT("client_id"), EType::Integer, ClientIdDoc).Min(0),
		MCPParam::Optional(TEXT("wait"), EType::Boolean, WaitDoc),
		MCPParam::Optional(TEXT("timeout_seconds"), EType::Number, TimeoutDoc).Min(0),
	}, MCPSpec::ContractExempt(TEXT("Arms the replayer and observer and starts PIE")));
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptCreate(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Name;
	TSharedPtr<FJsonValue> NameErr = RequireString(Params, TEXT("name"), Name);
	const FString PackagePath = OptionalString(Params, TEXT("package_path"), ScriptDefaultPackage);
	const TArray<TSharedPtr<FJsonValue>>* StepsArr = nullptr;
	TSharedPtr<FJsonValue> StepsErr = RequireArray(Params, TEXT("steps"), StepsArr);
	const FString Description = OptionalString(Params, TEXT("description"));
	const FString Profile = OptionalString(Params, TEXT("profile"));
	const int32 DefaultWithinMs = OptionalInt(Params, TEXT("default_within_ms"), 5000);
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip")).ToLower();
	if (NameErr) return NameErr;
	if (StepsErr) return StepsErr;

	if (OnConflict != TEXT("skip") && OnConflict != TEXT("update") && OnConflict != TEXT("replace") && OnConflict != TEXT("error"))
	{
		return MCPError(FString::Printf(TEXT("onConflict must be skip, update, replace or error, got '%s'"), *OnConflict));
	}
	if (DefaultWithinMs <= 0) return MCPError(TEXT("default_within_ms must be at least 1"));
	TArray<FScriptStepDef> Steps;
	FString ParseErr;
	if (!ParseScriptStepsJson(*StepsArr, Steps, ParseErr)) return MCPError(ParseErr);
	if (!TestScriptProfileExists(Profile)) return MCPError(FString::Printf(TEXT("Profile not found: %s"), *Profile));

	const FString AssetPath = PackagePath / Name;
	if (OnConflict == TEXT("update") || OnConflict == TEXT("replace"))
	{
		if (UMCPPIETestScript* Existing = LoadTestScriptAsset(AssetPath))
		{
			const FString Before = TestScriptStateKey(Existing);
			TSharedPtr<FJsonObject> Inverse = TestScriptUpdatePayload(Existing, AssetPath);
			ScriptStepsToAsset(Steps, Existing);
			Existing->Description = Description;
			Existing->Profile = Profile;
			Existing->DefaultWithinMs = DefaultWithinMs;
			auto Result = MCPSuccess();
			if (TestScriptStateKey(Existing) == Before)
			{
				Result->SetBoolField(TEXT("unchanged"), true);
				MCPSetExisted(Result);
			}
			else
			{
				if (!SaveAssetPackage(Existing)) return MCPError(FString::Printf(TEXT("Failed to save %s"), *AssetPath));
				MCPSetUpdated(Result);
				MCPSetRollback(Result, TEXT("pie_script_update"), Inverse);
			}
			TestScriptFieldsToJson(Existing, AssetPath, Result);
			return MCPResult(Result);
		}
	}

	auto Create = MCPCreateAssetIdempotentNewObject<UMCPPIETestScript>(
		Name, PackagePath, OnConflict == TEXT("error") ? OnConflict : FString(TEXT("skip")), TEXT("TestScript"));
	if (Create.EarlyReturn) return Create.EarlyReturn;

	UMCPPIETestScript* Script = Create.Asset;
	ScriptStepsToAsset(Steps, Script);
	Script->Description = Description;
	Script->Profile = Profile;
	Script->DefaultWithinMs = DefaultWithinMs;
	FString SaveError;
	if (!SaveAssetPackageChecked(Script, SaveError))
	{
		return MCPError(FString::Printf(TEXT("Saving the script failed: %s"), *SaveError));
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	TestScriptFieldsToJson(Script, AssetPath, Result);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetBoolField(TEXT("confirm"), true);
	MCPSetRollback(Result, TEXT("pie_script_delete"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptRead(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Path;
	if (auto Err = RequireString(Params, TEXT("path"), Path)) return Err;

	UMCPPIETestScript* Script = LoadTestScriptAsset(Path);
	if (!Script) return MCPError(FString::Printf(TEXT("Test script not found: %s"), *Path));

	auto Result = MCPSuccess();
	TestScriptFieldsToJson(Script, Path, Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptUpdate(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Path;
	TSharedPtr<FJsonValue> PathErr = RequireString(Params, TEXT("path"), Path);
	const TArray<TSharedPtr<FJsonValue>>* StepsArr = nullptr;
	const bool bHasSteps = TryGetArrayParam(Params, TEXT("steps"), StepsArr) && StepsArr;
	FString Description;
	const bool bHasDescription = TryGetStringParam(Params, TEXT("description"), Description);
	FString Profile;
	const bool bHasProfile = TryGetStringParam(Params, TEXT("profile"), Profile);
	int32 DefaultWithinMs = 0;
	const bool bHasWithin = TryGetNumberParam(Params, TEXT("default_within_ms"), DefaultWithinMs);
	if (PathErr) return PathErr;

	TArray<FScriptStepDef> Steps;
	if (bHasSteps)
	{
		FString ParseErr;
		if (!ParseScriptStepsJson(*StepsArr, Steps, ParseErr)) return MCPError(ParseErr);
	}
	if (bHasWithin && DefaultWithinMs <= 0) return MCPError(TEXT("default_within_ms must be at least 1"));

	UMCPPIETestScript* Script = LoadTestScriptAsset(Path);
	if (!Script) return MCPError(FString::Printf(TEXT("Test script not found: %s"), *Path));
	if (bHasProfile && !TestScriptProfileExists(Profile)) return MCPError(FString::Printf(TEXT("Profile not found: %s"), *Profile));

	const FString Before = TestScriptStateKey(Script);
	TSharedPtr<FJsonObject> Inverse = TestScriptUpdatePayload(Script, Path);
	if (bHasSteps) ScriptStepsToAsset(Steps, Script);
	if (bHasDescription) Script->Description = Description;
	if (bHasProfile) Script->Profile = Profile;
	if (bHasWithin) Script->DefaultWithinMs = DefaultWithinMs;

	auto Result = MCPSuccess();
	if (TestScriptStateKey(Script) == Before)
	{
		Result->SetBoolField(TEXT("unchanged"), true);
	}
	else
	{
		if (!SaveAssetPackage(Script)) return MCPError(FString::Printf(TEXT("Failed to save %s"), *Path));
		MCPSetUpdated(Result);
		MCPSetRollback(Result, TEXT("pie_script_update"), Inverse);
	}
	TestScriptFieldsToJson(Script, Path, Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptDelete(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Path;
	TSharedPtr<FJsonValue> PathErr = RequireString(Params, TEXT("path"), Path);
	const bool bConfirm = OptionalBool(Params, TEXT("confirm"), false);
	if (PathErr) return PathErr;
	if (!bConfirm) return MCPError(TEXT("confirm=true required to delete a test script"));
	if (MCPIsProtectedAssetPath(Path)) return MCPError(FString::Printf(TEXT("Refusing to delete a protected path: %s"), *Path));

	if (!UEditorAssetLibrary::DoesAssetExist(Path))
	{
		auto Result = MCPSuccess();
		Result->SetBoolField(TEXT("already_deleted"), true);
		Result->SetBoolField(TEXT("changed"), false);
		Result->SetStringField(TEXT("path"), Path);
		return MCPResult(Result);
	}

	UMCPPIETestScript* Script = LoadTestScriptAsset(Path);
	if (!Script) return MCPError(FString::Printf(TEXT("%s is not a test script"), *Path));

	TArray<FScriptStepDef> Steps;
	ScriptStepsFromAsset(Script, Steps);
	TSharedPtr<FJsonObject> Recreate = MakeShared<FJsonObject>();
	Recreate->SetStringField(TEXT("name"), Script->GetName());
	Recreate->SetStringField(TEXT("package_path"), FPackageName::GetLongPackagePath(Script->GetOutermost()->GetName()));
	Recreate->SetArrayField(TEXT("steps"), ScriptStepDefsToJson(Steps));
	Recreate->SetStringField(TEXT("description"), Script->Description);
	Recreate->SetStringField(TEXT("profile"), Script->Profile);
	Recreate->SetNumberField(TEXT("default_within_ms"), Script->DefaultWithinMs);

	if (!UEditorAssetLibrary::DeleteAsset(Path)) return MCPError(FString::Printf(TEXT("Failed to delete: %s"), *Path));

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("deleted"), true);
	Result->SetBoolField(TEXT("changed"), true);
	Result->SetStringField(TEXT("path"), Path);
	MCPSetRollback(Result, TEXT("pie_script_create"), Recreate);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptList(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString Directory = OptionalString(Params, TEXT("directory"), TEXT("/Game"));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	ARM.Get().GetAssetsByClass(UMCPPIETestScript::StaticClass()->GetClassPathName(), Assets, true);

	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FAssetData& A : Assets)
	{
		const FString PackageName = A.PackageName.ToString();
		if (!PackageName.StartsWith(Directory)) continue;
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("path"), PackageName);
		Item->SetStringField(TEXT("object_path"), A.GetObjectPathString());
		Item->SetStringField(TEXT("name"), A.AssetName.ToString());
		Items.Add(MakeShared<FJsonValueObject>(Item));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("scripts"), Items);
	Result->SetNumberField(TEXT("count"), Items.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptRun(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	MCPReadParamsAhead(Params, { TEXT("wait"), TEXT("timeout_seconds") });
	int32 PIEInstance = INDEX_NONE;
	FString PIEInstanceError;
	const bool bPIEInstanceOk = FPIEHandlers::ParsePIEInstance(Params, PIEInstance, PIEInstanceError);
	const FString ScriptPath = OptionalString(Params, TEXT("script"));
	const TArray<TSharedPtr<FJsonValue>>* StepsArr = nullptr;
	const bool bHasSteps = TryGetArrayParam(Params, TEXT("steps"), StepsArr) && StepsArr;
	const FString Name = OptionalString(Params, TEXT("name"));
	FString Profile;
	const bool bHasProfile = TryGetStringParam(Params, TEXT("profile"), Profile);
	const bool bRecord = OptionalBool(Params, TEXT("record"), true);
	const bool bStartPIE = OptionalBool(Params, TEXT("start_pie"), false);
	const bool bBlockOnManual = OptionalBool(Params, TEXT("block_on_manual"), true);
	int32 DefaultWithinMs = 0;
	const bool bHasWithin = TryGetNumberParam(Params, TEXT("default_within_ms"), DefaultWithinMs);
	const int32 ClientId = OptionalInt(Params, TEXT("client_id"), 0);
	if (!bPIEInstanceOk) return MCPError(PIEInstanceError);

	if (ScriptPath.IsEmpty() == !bHasSteps)
	{
		return MCPError(TEXT("Give exactly one of script (asset path) or steps (inline)"));
	}
	if (bHasWithin && DefaultWithinMs <= 0) return MCPError(TEXT("default_within_ms must be at least 1"));
	if (ClientId < 0) return MCPError(TEXT("client_id must be 0 or more"));

	FScriptRunConfig Cfg;
	if (!ScriptPath.IsEmpty())
	{
		UMCPPIETestScript* Script = LoadTestScriptAsset(ScriptPath);
		if (!Script) return MCPError(FString::Printf(TEXT("Test script not found: %s"), *ScriptPath));
		Cfg.Script = ScriptDefFromAsset(Script, ScriptPath);
		if (Cfg.Script.Steps.Num() == 0) return MCPError(FString::Printf(TEXT("Test script %s has no steps"), *ScriptPath));
	}
	else
	{
		FString ParseErr;
		if (!ParseScriptStepsJson(*StepsArr, Cfg.Script.Steps, ParseErr)) return MCPError(ParseErr);
		Cfg.Script.Name = Name.IsEmpty() ? FString(TEXT("inline")) : Name;
	}
	if (!Name.IsEmpty()) Cfg.Script.Name = Name;
	if (bHasProfile) Cfg.Script.Profile = Profile;
	if (bHasWithin) Cfg.Script.DefaultWithinMs = DefaultWithinMs;
	Cfg.bRecord = bRecord;
	Cfg.bStartPIE = bStartPIE;
	Cfg.bBlockOnManual = bBlockOnManual;
	Cfg.ClientId = ClientId;
	Cfg.PIEInstance = PIEInstance;

	FString Err, Msg;
	FPIEScriptRunner& Runner = FPIEScriptRunner::Get();
	if (!Runner.Start(Cfg, Err, Msg)) return MCPError(Err);

	const FScriptRun* Run = Runner.GetRun();
	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	ScriptRunFieldsToJson(*Run, Result, 0);
	Result->SetBoolField(TEXT("pie_started"), Run->bStartedPIE);
	Result->SetBoolField(TEXT("pie_running"), GEditor && GEditor->PlayWorld != nullptr);
	Result->SetStringField(TEXT("message"), Msg);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("run_id"), Run->RunId);
	Payload->SetStringField(TEXT("reason"), TEXT("rollback"));
	MCPSetRollback(Result, TEXT("pie_script_stop"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptStatus(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	MCPReadParamsAhead(Params, { TEXT("wait"), TEXT("timeout_seconds") });
	const FString RunId = OptionalString(Params, TEXT("run_id"));
	const int32 SinceSeq = FMath::Max(0, OptionalInt(Params, TEXT("since_seq"), 0));

	const FScriptRun* Run = FPIEScriptRunner::Get().GetRun();
	auto Result = MCPSuccess();
	if (!Run || (!RunId.IsEmpty() && Run->RunId != RunId))
	{
		// A finished run the editor no longer holds answers from its results.json.
		const TSharedPtr<FJsonObject> Saved = RunId.IsEmpty() ? nullptr
			: LoadJsonFile(DefaultScriptObservationsRoot() / RunId / TEXT("results.json"));
		if (Saved.IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Saved->Values)
			{
				if (Field.Key != TEXT("events")) Result->SetField(Field.Key, Field.Value);
			}
			TArray<TSharedPtr<FJsonValue>> Events;
			const TArray<TSharedPtr<FJsonValue>>* AllEvents = nullptr;
			if (Saved->TryGetArrayField(TEXT("events"), AllEvents))
			{
				for (const TSharedPtr<FJsonValue>& E : *AllEvents)
				{
					const TSharedPtr<FJsonObject>* EO = nullptr;
					double Seq = 0;
					if (E->TryGetObject(EO) && (*EO)->TryGetNumberField(TEXT("seq"), Seq) && Seq > SinceSeq) Events.Add(E);
				}
			}
			TArray<TSharedPtr<FJsonValue>> Awaiting;
			const TArray<TSharedPtr<FJsonValue>>* SavedSteps = nullptr;
			if (Saved->TryGetArrayField(TEXT("steps"), SavedSteps))
			{
				for (const TSharedPtr<FJsonValue>& S : *SavedSteps)
				{
					const TSharedPtr<FJsonObject>* SO = nullptr;
					FString Status;
					if (S->TryGetObject(SO) && (*SO)->TryGetStringField(TEXT("status"), Status) && Status == TEXT("awaiting_verdict"))
					{
						TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
						A->SetNumberField(TEXT("index"), (*SO)->GetNumberField(TEXT("index")));
						A->SetStringField(TEXT("instruction"), (*SO)->GetStringField(TEXT("instruction")));
						Awaiting.Add(MakeShared<FJsonValueObject>(A));
					}
				}
			}
			Result->SetArrayField(TEXT("events"), Events);
			Result->SetArrayField(TEXT("awaiting"), Awaiting);
			Result->SetBoolField(TEXT("active"), false);
			Result->SetBoolField(TEXT("live"), false);
			return MCPResult(Result);
		}
		Result->SetStringField(TEXT("state"), TEXT("idle"));
		if (!RunId.IsEmpty()) Result->SetStringField(TEXT("run_id"), RunId);
		Result->SetArrayField(TEXT("steps"), TArray<TSharedPtr<FJsonValue>>());
		Result->SetArrayField(TEXT("awaiting"), TArray<TSharedPtr<FJsonValue>>());
		Result->SetArrayField(TEXT("events"), TArray<TSharedPtr<FJsonValue>>());
		Result->SetNumberField(TEXT("last_seq"), SinceSeq);
		return MCPResult(Result);
	}
	ScriptRunFieldsToJson(*Run, Result, SinceSeq);
	Result->SetBoolField(TEXT("active"), FPIEScriptRunner::Get().IsActive());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptStop(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString RunId = OptionalString(Params, TEXT("run_id"));
	const FString Reason = OptionalString(Params, TEXT("reason"));

	FPIEScriptRunner& Runner = FPIEScriptRunner::Get();
	const FScriptRun* Run = Runner.GetRun();
	if (!Runner.IsActive())
	{
		TSharedPtr<FJsonObject> Already = MCPErrorObject(TEXT("No script run is active, so nothing was stopped."));
		Already->SetBoolField(TEXT("alreadyStopped"), true);
		Already->SetBoolField(TEXT("changed"), false);
		if (Run && (RunId.IsEmpty() || Run->RunId == RunId))
		{
			Already->SetStringField(TEXT("run_id"), Run->RunId);
			Already->SetStringField(TEXT("state"), ScriptRunStateToString(Run->State));
			Already->SetStringField(TEXT("results_path"), Run->ResultsPath);
		}
		return MCPResult(Already);
	}
	if (!RunId.IsEmpty() && Run->RunId != RunId)
	{
		return MCPError(FString::Printf(TEXT("Run %s is not the active run (%s)"), *RunId, *Run->RunId));
	}

	FString Err;
	if (!Runner.Stop(Reason, Err)) return MCPError(Err);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("stopped"), true);
	Result->SetBoolField(TEXT("changed"), true);
	ScriptRunFieldsToJson(*Runner.GetRun(), Result, 0);
	MCPSetNoRollback(Result, TEXT("A stopped run cannot be resumed; start a new run with pie_script_run."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptVerdict(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	int32 Step = 0;
	TSharedPtr<FJsonValue> StepErr = RequireNumber(Params, TEXT("step"), Step);
	FString Verdict;
	TSharedPtr<FJsonValue> VerdictErr = RequireString(Params, TEXT("verdict"), Verdict);
	const FString Note = OptionalString(Params, TEXT("note"));
	const FString RunId = OptionalString(Params, TEXT("run_id"));
	const FString Source = OptionalString(Params, TEXT("source"), TEXT("action"));
	if (StepErr) return StepErr;
	if (VerdictErr) return VerdictErr;

	FPIEScriptRunner& Runner = FPIEScriptRunner::Get();
	const FScriptRun* Run = Runner.GetRun();
	if (!Run) return MCPError(TEXT("No script run to give a verdict on"));
	if (!RunId.IsEmpty() && Run->RunId != RunId)
	{
		return MCPError(FString::Printf(TEXT("Run %s is not the current run (%s)"), *RunId, *Run->RunId));
	}

	const bool bValidStep = Run->Steps.IsValidIndex(Step);
	const FString PrevVerdict = bValidStep ? ScriptVerdictForStatus(Run->Steps[Step].Status) : FString();
	const FString PrevNote = bValidStep ? Run->Steps[Step].Note : FString();
	const FString PrevSource = bValidStep ? Run->Steps[Step].VerdictSource : FString();

	bool bUnchanged = false;
	FString Err;
	if (!Runner.Verdict(Step, Verdict, Note, Source, bUnchanged, Err)) return MCPError(Err);

	Run = Runner.GetRun();
	const FScriptStepRun& St = Run->Steps[Step];
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("run_id"), Run->RunId);
	Result->SetNumberField(TEXT("step"), Step);
	Result->SetStringField(TEXT("status"), ScriptStepStatusToString(St.Status));
	Result->SetStringField(TEXT("verdict"), Verdict.ToLower());
	Result->SetStringField(TEXT("verdict_source"), St.VerdictSource);
	if (!St.Note.IsEmpty()) Result->SetStringField(TEXT("note"), St.Note);
	Result->SetStringField(TEXT("state"), ScriptRunStateToString(Run->State));
	TArray<TSharedPtr<FJsonValue>> Awaiting;
	for (int32 I : ScriptAwaitingSteps(*Run)) Awaiting.Add(MakeShared<FJsonValueNumber>(I));
	Result->SetArrayField(TEXT("awaiting"), Awaiting);
	if (bUnchanged)
	{
		Result->SetBoolField(TEXT("unchanged"), true);
		MCPSetExisted(Result);
		return MCPResult(Result);
	}
	MCPSetUpdated(Result);
	if (!PrevVerdict.IsEmpty() && PrevSource != TEXT("source_run"))
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("run_id"), Run->RunId);
		Payload->SetNumberField(TEXT("step"), Step);
		Payload->SetStringField(TEXT("verdict"), PrevVerdict);
		Payload->SetStringField(TEXT("note"), PrevNote);
		Payload->SetStringField(TEXT("source"), PrevSource.IsEmpty() ? FString(TEXT("action")) : PrevSource);
		MCPSetRollback(Result, TEXT("pie_script_verdict"), Payload);
	}
	else
	{
		MCPSetNoRollback(Result, TEXT("The step was awaiting its first verdict; a verdict cannot be withdrawn, only replaced."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptResults(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString RunId = OptionalString(Params, TEXT("run_id"));
	FString ResultsPath = OptionalString(Params, TEXT("results_path"));

	const FPIEScriptRunner& Runner = FPIEScriptRunner::Get();
	const FScriptRun* Run = Runner.GetRun();
	if (ResultsPath.IsEmpty())
	{
		const FString Id = !RunId.IsEmpty() ? RunId : (Run ? Run->RunId : FString());
		if (Id.IsEmpty()) return MCPError(TEXT("No script run yet; give run_id or results_path"));
		if (Run && Run->RunId == Id && Runner.IsActive())
		{
			TSharedPtr<FJsonObject> Live = ScriptRunToResultsJson(*Run);
			Live->SetBoolField(TEXT("success"), true);
			Live->SetBoolField(TEXT("live"), true);
			return MCPResult(Live);
		}
		ResultsPath = DefaultScriptObservationsRoot() / Id / TEXT("results.json");
	}

	TSharedPtr<FJsonObject> Obj = LoadJsonFile(ResultsPath);
	if (!Obj.IsValid()) return MCPError(FString::Printf(TEXT("results.json not found or unreadable: %s"), *ResultsPath));
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetBoolField(TEXT("live"), false);
	Obj->SetStringField(TEXT("results_path"), ResultsPath);
	return MCPResult(Obj);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptReplay(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	MCPReadParamsAhead(Params, { TEXT("wait"), TEXT("timeout_seconds") });
	int32 PIEInstance = INDEX_NONE;
	FString PIEInstanceError;
	const bool bPIEInstanceOk = FPIEHandlers::ParsePIEInstance(Params, PIEInstance, PIEInstanceError);
	const FString RunId = OptionalString(Params, TEXT("run_id"));
	const FString ResultsPath = OptionalString(Params, TEXT("results_path"));
	const double TimeScale = OptionalNumber(Params, TEXT("time_scale"), 1.0);
	const bool bAutoStop = OptionalBool(Params, TEXT("auto_stop_pie"), true);
	const int32 ClientId = OptionalInt(Params, TEXT("client_id"), 0);
	if (!bPIEInstanceOk) return MCPError(PIEInstanceError);
	if (!(TimeScale > 0.0)) return MCPError(TEXT("time_scale must be greater than 0"));
	if (ClientId < 0) return MCPError(TEXT("client_id must be 0 or more"));

	FScriptReplayConfig Cfg;
	Cfg.SourceRunId = RunId;
	Cfg.ResultsPath = ResultsPath;
	Cfg.TimeScale = static_cast<float>(TimeScale);
	Cfg.bAutoStopPIE = bAutoStop;
	Cfg.ClientId = ClientId;
	Cfg.PIEInstance = PIEInstance;

	FString Err, Msg;
	FPIEScriptRunner& Runner = FPIEScriptRunner::Get();
	if (!Runner.StartReplay(Cfg, Err, Msg)) return MCPError(Err);

	const FScriptRun* Run = Runner.GetRun();
	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	ScriptRunFieldsToJson(*Run, Result, 0);
	Result->SetBoolField(TEXT("pie_started"), true);
	Result->SetBoolField(TEXT("auto_stop_pie"), bAutoStop);
	Result->SetNumberField(TEXT("time_scale"), TimeScale);
	Result->SetStringField(TEXT("message"), Msg);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("run_id"), Run->RunId);
	Payload->SetStringField(TEXT("reason"), TEXT("rollback"));
	MCPSetRollback(Result, TEXT("pie_script_stop"), Payload);
	return MCPResult(Result);
}
