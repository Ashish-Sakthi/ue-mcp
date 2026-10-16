#include "SourceControlHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerSourceControl.h"

#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "ISourceControlRevision.h"
#include "ISourceControlChangelist.h"
#include "ISourceControlChangelistState.h"
#include "SourceControlOperations.h"
#include "SourceControlHelpers.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

// ─── Checkout on write ─────────────────────────────────────────────────────

namespace MCPSourceControlState
{
	bool& AutoCheckoutFlag()
	{
		static bool bEnabled = FPlatformMisc::GetEnvironmentVariable(TEXT("UE_MCP_CHECKOUT_MODE")).Equals(TEXT("auto"), ESearchCase::IgnoreCase);
		return bEnabled;
	}

	TArray<FString>& AutoCheckedOutFiles()
	{
		static TArray<FString> Files;
		return Files;
	}
}

bool MCPSourceControl::IsAutoCheckoutEnabled()
{
	return MCPSourceControlState::AutoCheckoutFlag();
}

void MCPSourceControl::SetAutoCheckoutEnabled(bool bEnabled)
{
	MCPSourceControlState::AutoCheckoutFlag() = bEnabled;
}

bool MCPSourceControl::TryAutoCheckout(const FString& FileName, FString& OutNote)
{
	OutNote.Reset();
	if (!IsAutoCheckoutEnabled()) return false;

	ISourceControlModule& Module = ISourceControlModule::Get();
	if (!Module.IsEnabled())
	{
		OutNote = TEXT("Checkout mode is auto, but no source control provider is enabled in this editor.");
		return false;
	}
	ISourceControlProvider& Provider = Module.GetProvider();
	if (!Provider.IsAvailable())
	{
		OutNote = FString::Printf(TEXT("Checkout mode is auto, but the %s provider is not connected."), *Provider.GetName().ToString());
		return false;
	}
	if (!Provider.UsesCheckout())
	{
		OutNote = FString::Printf(TEXT("The %s provider does not use checkout, so source control did not make the file read-only."), *Provider.GetName().ToString());
		return false;
	}

	const FString Full = FPaths::ConvertRelativePathToFull(FileName);
	const FSourceControlStatePtr State = Provider.GetState(Full, EStateCacheUsage::ForceUpdate);
	if (!State.IsValid())
	{
		OutNote = TEXT("The provider returned no state for the file, so it was not checked out.");
		return false;
	}
	if (!State->IsCheckedOut() && !State->IsAdded())
	{
		FString Who;
		if (State->IsCheckedOutOther(&Who) && !State->CanCheckout())
		{
			OutNote = FString::Printf(TEXT("Not checked out: it is locked by %s."), *Who);
			return false;
		}
		if (State->IsSourceControlled() && !State->IsCurrent())
		{
			OutNote = TEXT("Not checked out: the local copy is not at the latest revision. Sync it first.");
			return false;
		}
		if (!State->CanCheckout())
		{
			OutNote = FString::Printf(TEXT("Not checked out: the provider reports '%s', which cannot be checked out."), *State->GetDisplayName().ToString());
			return false;
		}
		if (Provider.Execute(ISourceControlOperation::Create<FCheckOut>(), Full) != ECommandResult::Succeeded)
		{
			OutNote = TEXT("The provider refused the checkout. The output log carries its reason.");
			return false;
		}
	}
	if (IFileManager::Get().IsReadOnly(*Full))
	{
		OutNote = TEXT("The file is open in source control but still read-only on disk.");
		return false;
	}
	MCPSourceControlState::AutoCheckedOutFiles().AddUnique(Full);
	return true;
}

int32 MCPSourceControl::AutoCheckoutMark()
{
	return MCPSourceControlState::AutoCheckedOutFiles().Num();
}

void MCPSourceControl::AttachAutoCheckouts(const TSharedPtr<FJsonValue>& Result, int32 Mark)
{
	TArray<FString>& Files = MCPSourceControlState::AutoCheckedOutFiles();
	if (Mark < 0 || Mark >= Files.Num())
	{
		return;
	}
	TArray<FString> Taken;
	for (int32 Index = Mark; Index < Files.Num(); ++Index)
	{
		Taken.Add(Files[Index]);
	}
	Files.SetNum(Mark);
	if (Result.IsValid() && Result->Type == EJson::Object && Result->AsObject().IsValid())
	{
		Result->AsObject()->SetArrayField(TEXT("autoCheckedOut"), MCPStringListToJson(Taken));
	}
}

// ─── Helpers ───────────────────────────────────────────────────────────────

namespace MCPSourceControlDetail
{
	/** One caller path resolved to the file the provider is asked about. */
	struct FResolvedPath
	{
		FString Input;
		FString File;
		FString PackageName;
		bool bDirectory = false;
		FString Error;
	};

	FString FullFilePath(const FString& Path)
	{
		FString Full = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeFilename(Full);
		return Full;
	}

	FString FullDirectoryPath(const FString& Path)
	{
		FString Full = FullFilePath(Path);
		if (!Full.EndsWith(TEXT("/"))) Full += TEXT("/");
		return Full;
	}

	/** A content path (/Game/X, /Game/X.X, /Game/Folder), or a file or folder
	 *  on disk, absolute or relative to the project directory. */
	FResolvedPath ResolvePath(const FString& InPath)
	{
		FResolvedPath Out;
		Out.Input = InPath;
		FString Path = InPath.TrimStartAndEnd();
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (Path.IsEmpty())
		{
			Out.Error = TEXT("empty path");
			return Out;
		}

		const bool bDrivePath = Path.Len() > 1 && Path[1] == TEXT(':');
		if (Path.StartsWith(TEXT("/")) && !Path.StartsWith(TEXT("//")) && !bDrivePath)
		{
			FString PackageName = Path;
			const bool bTrailingSlash = PackageName.EndsWith(TEXT("/"));
			if (bTrailingSlash) PackageName.LeftChopInline(1);
			int32 Dot = INDEX_NONE;
			if (PackageName.FindLastChar(TEXT('.'), Dot) && PackageName.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd) < Dot)
			{
				PackageName.LeftInline(Dot);
			}

			if (FPackageName::IsValidLongPackageName(PackageName))
			{
				FString Existing;
				if (!bTrailingSlash && FPackageName::DoesPackageExist(PackageName, &Existing))
				{
					Out.File = FullFilePath(Existing);
					Out.PackageName = PackageName;
					return Out;
				}
				FString Directory;
				if (FPackageName::TryConvertLongPackageNameToFilename(PackageName + TEXT("/"), Directory)
					&& IFileManager::Get().DirectoryExists(*Directory))
				{
					Out.File = FullDirectoryPath(Directory);
					Out.bDirectory = true;
					return Out;
				}
				// Loaded but never saved, or deleted on disk: the file it maps to.
				UPackage* Loaded = FindPackage(nullptr, *PackageName);
				const FString& Extension = Loaded ? PackageFileExtension(Loaded) : FPackageName::GetAssetPackageExtension();
				FString FileName;
				if (!bTrailingSlash && FPackageName::TryConvertLongPackageNameToFilename(PackageName, FileName, Extension))
				{
					Out.File = FullFilePath(FileName);
					Out.PackageName = PackageName;
					return Out;
				}
			}
			// An absolute POSIX path is handled below like any other file path.
			if (!IFileManager::Get().FileExists(*Path) && !IFileManager::Get().DirectoryExists(*Path))
			{
				Out.Error = TEXT("names no mounted content path and no file or folder on disk");
				return Out;
			}
		}

		FString OnDisk = FPaths::IsRelative(Path) ? FPaths::Combine(FPaths::ProjectDir(), Path) : Path;
		if (IFileManager::Get().DirectoryExists(*OnDisk))
		{
			Out.File = FullDirectoryPath(OnDisk);
			Out.bDirectory = true;
			return Out;
		}
		Out.File = FullFilePath(OnDisk);
		FString PackageName;
		if (FPackageName::TryConvertFilenameToLongPackageName(Out.File, PackageName))
		{
			Out.PackageName = PackageName;
		}
		return Out;
	}

	/** Resolve every entry of a paths array. Returns an error result when one is not a string or names nothing. */
	TSharedPtr<FJsonValue> ResolvePaths(const TArray<TSharedPtr<FJsonValue>>& Values, TArray<FResolvedPath>& Out)
	{
		if (Values.Num() == 0)
		{
			return MCPError(TEXT("'paths' is empty. Name at least one asset, content folder or file."));
		}
		for (const TSharedPtr<FJsonValue>& Value : Values)
		{
			FString Text;
			if (!Value.IsValid() || !Value->TryGetString(Text))
			{
				return MCPError(TEXT("Every entry of 'paths' must be a string."));
			}
			FResolvedPath Resolved = ResolvePath(Text);
			if (!Resolved.Error.IsEmpty())
			{
				TSharedPtr<FJsonObject> Error = MCPErrorObject(FString::Printf(TEXT("'%s' %s. Nothing was done."), *Text, *Resolved.Error));
				Error->SetStringField(TEXT("path"), Text);
				return MCPResult(Error);
			}
			Out.Add(MoveTemp(Resolved));
		}
		return nullptr;
	}

	/** The refusal every action but get_provider returns without a usable provider. */
	TSharedPtr<FJsonValue> RequireProvider(ISourceControlProvider*& OutProvider)
	{
		OutProvider = nullptr;
		ISourceControlModule& Module = ISourceControlModule::Get();
		if (!Module.IsEnabled())
		{
			TSharedPtr<FJsonObject> Error = MCPErrorObject(TEXT("Source control is not enabled in this editor. Configure a provider (Perforce, Git, Unity Version Control or Subversion) under Revision Control, then retry. Nothing was done."));
			Error->SetStringField(TEXT("errorCode"), TEXT("source_control_not_enabled"));
			Error->SetBoolField(TEXT("enabled"), false);
			return MCPResult(Error);
		}
		ISourceControlProvider& Provider = Module.GetProvider();
		if (!Provider.IsAvailable())
		{
			TSharedPtr<FJsonObject> Error = MCPErrorObject(FString::Printf(
				TEXT("The %s provider is configured but not connected: %s. Nothing was done."),
				*Provider.GetName().ToString(), *Provider.GetStatusText().ToString()));
			Error->SetStringField(TEXT("errorCode"), TEXT("source_control_unavailable"));
			Error->SetStringField(TEXT("provider"), Provider.GetName().ToString());
			Error->SetBoolField(TEXT("enabled"), true);
			Error->SetBoolField(TEXT("connected"), false);
			return MCPResult(Error);
		}
		OutProvider = &Provider;
		return nullptr;
	}

	TArray<FString> FilesOf(const TArray<FResolvedPath>& Paths)
	{
		TArray<FString> Files;
		for (const FResolvedPath& Path : Paths) Files.Add(Path.File);
		return Files;
	}

	bool IsPackageFile(const FString& File)
	{
		const FString Extension = TEXT(".") + FPaths::GetExtension(File);
		return Extension == FPackageName::GetAssetPackageExtension() || Extension == FPackageName::GetMapPackageExtension();
	}

	UPackage* LoadedPackageForFile(const FString& File)
	{
		FString PackageName;
		if (!IsPackageFile(File) || !FPackageName::TryConvertFilenameToLongPackageName(File, PackageName)) return nullptr;
		return FindPackage(nullptr, *PackageName);
	}

	TSharedPtr<FJsonObject> StateToJson(const FSourceControlStateRef& State)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		const FString& File = State->GetFilename();
		Obj->SetStringField(TEXT("file"), File);
		FString PackageName;
		if (IsPackageFile(File) && FPackageName::TryConvertFilenameToLongPackageName(File, PackageName))
		{
			Obj->SetStringField(TEXT("packageName"), PackageName);
		}
		Obj->SetStringField(TEXT("state"), State->GetDisplayName().ToString());
		Obj->SetBoolField(TEXT("sourceControlled"), State->IsSourceControlled());
		Obj->SetBoolField(TEXT("checkedOut"), State->IsCheckedOut());
		Obj->SetBoolField(TEXT("added"), State->IsAdded());
		Obj->SetBoolField(TEXT("deleted"), State->IsDeleted());
		Obj->SetBoolField(TEXT("modified"), State->IsModified());
		Obj->SetBoolField(TEXT("conflicted"), State->IsConflicted());
		Obj->SetBoolField(TEXT("ignored"), State->IsIgnored());
		Obj->SetBoolField(TEXT("outOfDate"), State->IsSourceControlled() && !State->IsAdded() && !State->IsCurrent());
		FString Who;
		const bool bOther = State->IsCheckedOutOther(&Who);
		Obj->SetBoolField(TEXT("checkedOutByOther"), bOther);
		if (bOther) Obj->SetStringField(TEXT("otherUser"), Who);
		Obj->SetBoolField(TEXT("canCheckout"), State->CanCheckout());
		Obj->SetBoolField(TEXT("canAdd"), State->CanAdd());
		Obj->SetBoolField(TEXT("canRevert"), State->CanRevert());
		Obj->SetBoolField(TEXT("canSubmit"), State->CanCheckIn());
		if (UPackage* Loaded = LoadedPackageForFile(File))
		{
			Obj->SetBoolField(TEXT("unsavedInEditor"), Loaded->IsDirty());
		}
		return Obj;
	}

	/** Changed in a way list_changes reports: open, added, deleted, modified,
	 *  conflicted, or (with bUntracked) on disk and not under control. */
	bool IsChange(const FSourceControlStateRef& State, bool bUntracked, bool bUsesCheckout)
	{
		// A provider without checkout (Git) reports every tracked file as checked out, so being
		// checked out is a change only where checking out is a step of its own.
		if ((bUsesCheckout && State->IsCheckedOut()) || State->IsAdded() || State->IsDeleted() || State->IsModified() || State->IsConflicted())
		{
			return true;
		}
		return bUntracked && !State->IsSourceControlled() && !State->IsIgnored() && !State->IsUnknown()
			&& IFileManager::Get().FileExists(*State->GetFilename());
	}

	bool IsUnder(const FString& File, const TArray<FString>& Roots)
	{
		for (const FString& Root : Roots)
		{
			if (Root.EndsWith(TEXT("/")) ? File.StartsWith(Root, ESearchCase::IgnoreCase) : File.Equals(Root, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	/** Refresh the provider's cache for Files, then read their states. */
	bool QueryStates(ISourceControlProvider& Provider, const TArray<FString>& Files, TArray<FSourceControlStateRef>& OutStates)
	{
		OutStates.Reset();
		return Provider.GetState(Files, OutStates, EStateCacheUsage::ForceUpdate) == ECommandResult::Succeeded;
	}

	TSharedPtr<FJsonObject> Failure(const FString& Input, const FString& File, const FString& Reason)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("path"), Input);
		Obj->SetStringField(TEXT("file"), File);
		Obj->SetStringField(TEXT("reason"), Reason);
		return Obj;
	}

	TArray<TSharedPtr<FJsonValue>> ToJsonArray(const TArray<TSharedPtr<FJsonObject>>& Objects)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const TSharedPtr<FJsonObject>& Obj : Objects) Out.Add(MakeShared<FJsonValueObject>(Obj));
		return Out;
	}

	/** Mark a result failed, naming how many of the requested files failed, when any did. */
	void NoteFailures(const TSharedPtr<FJsonObject>& Result, const TArray<TSharedPtr<FJsonObject>>& Failed, const TCHAR* Verb)
	{
		Result->SetArrayField(TEXT("failed"), ToJsonArray(Failed));
		if (Failed.Num() == 0) return;
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%d file(s) could not be %s; 'failed' says why for each."), Failed.Num(), Verb));
	}

	// ── Line diff ──

	struct FDiffOp
	{
		TCHAR Kind;
		int32 A;
		int32 B;
	};

	/** Unified diff of two line arrays: LCS over the lines between the common
	 *  prefix and suffix, capped so a huge file reports a summary instead. */
	void UnifiedDiff(const TArray<FString>& A, const TArray<FString>& B, int32 Context, int32 MaxLines,
		TArray<FString>& OutLines, int32& OutAdded, int32& OutRemoved, bool& bOutTruncated, bool& bOutApproximate)
	{
		OutLines.Reset();
		OutAdded = OutRemoved = 0;
		bOutTruncated = bOutApproximate = false;

		int32 Prefix = 0;
		while (Prefix < A.Num() && Prefix < B.Num() && A[Prefix].Equals(B[Prefix], ESearchCase::CaseSensitive)) ++Prefix;
		int32 Suffix = 0;
		while (Suffix < A.Num() - Prefix && Suffix < B.Num() - Prefix
			&& A[A.Num() - 1 - Suffix].Equals(B[B.Num() - 1 - Suffix], ESearchCase::CaseSensitive)) ++Suffix;
		const int32 N = A.Num() - Prefix - Suffix;
		const int32 M = B.Num() - Prefix - Suffix;

		TArray<FDiffOp> Ops;
		for (int32 I = 0; I < Prefix; ++I) Ops.Add({ TEXT(' '), I, I });

		if ((int64)N * (int64)M > 4000000)
		{
			bOutApproximate = true;
			for (int32 I = 0; I < N; ++I) Ops.Add({ TEXT('-'), Prefix + I, Prefix });
			for (int32 J = 0; J < M; ++J) Ops.Add({ TEXT('+'), Prefix + N, Prefix + J });
		}
		else
		{
			const int32 W = M + 1;
			TArray<int32> L;
			L.SetNumZeroed((N + 1) * W);
			for (int32 I = N - 1; I >= 0; --I)
			{
				for (int32 J = M - 1; J >= 0; --J)
				{
					L[I * W + J] = A[Prefix + I].Equals(B[Prefix + J], ESearchCase::CaseSensitive)
						? L[(I + 1) * W + J + 1] + 1
						: FMath::Max(L[(I + 1) * W + J], L[I * W + J + 1]);
				}
			}
			int32 I = 0, J = 0;
			while (I < N && J < M)
			{
				if (A[Prefix + I].Equals(B[Prefix + J], ESearchCase::CaseSensitive)) { Ops.Add({ TEXT(' '), Prefix + I, Prefix + J }); ++I; ++J; }
				else if (L[(I + 1) * W + J] >= L[I * W + J + 1]) { Ops.Add({ TEXT('-'), Prefix + I, Prefix + J }); ++I; }
				else { Ops.Add({ TEXT('+'), Prefix + I, Prefix + J }); ++J; }
			}
			for (; I < N; ++I) Ops.Add({ TEXT('-'), Prefix + I, Prefix + J });
			for (; J < M; ++J) Ops.Add({ TEXT('+'), Prefix + I, Prefix + J });
		}
		for (int32 K = 0; K < Suffix; ++K) Ops.Add({ TEXT(' '), A.Num() - Suffix + K, B.Num() - Suffix + K });

		TArray<bool> Keep;
		Keep.Init(false, Ops.Num());
		for (int32 K = 0; K < Ops.Num(); ++K)
		{
			if (Ops[K].Kind == TEXT(' ')) continue;
			if (Ops[K].Kind == TEXT('+')) ++OutAdded; else ++OutRemoved;
			for (int32 D = FMath::Max(0, K - Context); D <= FMath::Min(Ops.Num() - 1, K + Context); ++D) Keep[D] = true;
		}

		int32 K = 0;
		while (K < Ops.Num())
		{
			if (!Keep[K]) { ++K; continue; }
			const int32 Start = K;
			while (K < Ops.Num() && Keep[K]) ++K;
			int32 ALen = 0, BLen = 0;
			for (int32 Q = Start; Q < K; ++Q)
			{
				if (Ops[Q].Kind != TEXT('+')) ++ALen;
				if (Ops[Q].Kind != TEXT('-')) ++BLen;
			}
			if (OutLines.Num() >= MaxLines) { bOutTruncated = true; return; }
			OutLines.Add(FString::Printf(TEXT("@@ -%d,%d +%d,%d @@"), Ops[Start].A + 1, ALen, Ops[Start].B + 1, BLen));
			for (int32 Q = Start; Q < K; ++Q)
			{
				if (OutLines.Num() >= MaxLines) { bOutTruncated = true; return; }
				const FString& Text = Ops[Q].Kind == TEXT('+') ? B[Ops[Q].B] : A[Ops[Q].A];
				OutLines.Add(FString::Printf(TEXT("%c%s"), Ops[Q].Kind, *Text));
			}
		}
	}

	const TCHAR* StatusKeyName(ISourceControlProvider::EStatus Key)
	{
		// Indexed by value: the enum only ever grows at the end.
		static const TCHAR* Names[] = {
			TEXT("enabled"), TEXT("connected"), TEXT("port"), TEXT("user"), TEXT("client"),
			TEXT("repository"), TEXT("remote"), TEXT("branch"), TEXT("email"), TEXT("scmVersion"),
			TEXT("pluginVersion"), TEXT("workspace"), TEXT("workspacePath"), TEXT("changeset"),
		};
		const int32 Index = static_cast<int32>(Key);
		return Index >= 0 && Index < static_cast<int32>(UE_ARRAY_COUNT(Names)) ? Names[Index] : TEXT("other");
	}
}

using namespace MCPSourceControlDetail;

// ─── Actions ───────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FSourceControlHandlers::GetProvider(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MCPSuccess();
	ISourceControlModule& Module = ISourceControlModule::Get();
	const bool bEnabled = Module.IsEnabled();
	Result->SetBoolField(TEXT("enabled"), bEnabled);
	Result->SetStringField(TEXT("checkoutMode"), MCPSourceControl::IsAutoCheckoutEnabled() ? TEXT("auto") : TEXT("manual"));
	if (!bEnabled)
	{
		Result->SetStringField(TEXT("provider"), TEXT("None"));
		Result->SetBoolField(TEXT("connected"), false);
		Result->SetStringField(TEXT("note"), TEXT("No source control provider is enabled. Configure Perforce, Git, Unity Version Control or Subversion under Revision Control in the editor; every other source_control action refuses until then."));
		return MCPResult(Result);
	}

	ISourceControlProvider& Provider = Module.GetProvider();
	Result->SetStringField(TEXT("provider"), Provider.GetName().ToString());
	Result->SetBoolField(TEXT("connected"), Provider.IsAvailable());
	Result->SetStringField(TEXT("statusText"), Provider.GetStatusText().ToString());

	TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
	for (const TPair<ISourceControlProvider::EStatus, FString>& Pair : Provider.GetStatus())
	{
		Info->SetStringField(StatusKeyName(Pair.Key), Pair.Value);
	}
	Result->SetObjectField(TEXT("info"), Info);

	TSharedPtr<FJsonObject> Caps = MakeShared<FJsonObject>();
	Caps->SetBoolField(TEXT("usesCheckout"), Provider.UsesCheckout());
	Caps->SetBoolField(TEXT("usesChangelists"), Provider.UsesChangelists());
	Caps->SetBoolField(TEXT("usesLocalReadOnlyState"), Provider.UsesLocalReadOnlyState());
	Caps->SetBoolField(TEXT("usesFileRevisions"), Provider.UsesFileRevisions());
	Caps->SetBoolField(TEXT("allowsDiffAgainstDepot"), Provider.AllowsDiffAgainstDepot());
	Result->SetObjectField(TEXT("capabilities"), Caps);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSourceControlHandlers::GetStatus(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* PathValues = nullptr;
	if (auto Err = RequireArray(Params, TEXT("paths"), PathValues)) return Err;
	const int32 Limit = FMath::Max(1, OptionalInt(Params, TEXT("limit"), 500));

	TArray<FResolvedPath> Paths;
	if (auto Err = ResolvePaths(*PathValues, Paths)) return Err;
	ISourceControlProvider* Provider = nullptr;
	if (auto Err = RequireProvider(Provider)) return Err;

	TArray<FString> Files, Directories;
	for (const FResolvedPath& Path : Paths)
	{
		(Path.bDirectory ? Directories : Files).Add(Path.File);
	}

	TArray<FSourceControlStateRef> States;
	if (Files.Num() > 0 && !QueryStates(*Provider, Files, States))
	{
		return MCPError(TEXT("The provider could not report the state of the named files. The output log carries its reason."));
	}
	if (Directories.Num() > 0)
	{
		TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> Update = ISourceControlOperation::Create<FUpdateStatus>();
		Provider->Execute(Update, Directories);
		States.Append(Provider->GetCachedStateByPredicate([&Directories](const FSourceControlStateRef& State)
		{
			return IsUnder(State->GetFilename(), Directories);
		}));
	}

	TArray<TSharedPtr<FJsonValue>> Out;
	TSet<FString> Seen;
	bool bTruncated = false;
	for (const FSourceControlStateRef& State : States)
	{
		if (Seen.Contains(State->GetFilename())) continue;
		if (Out.Num() >= Limit) { bTruncated = true; break; }
		Seen.Add(State->GetFilename());
		Out.Add(MakeShared<FJsonValueObject>(StateToJson(State)));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("provider"), Provider->GetName().ToString());
	Result->SetArrayField(TEXT("files"), Out);
	Result->SetNumberField(TEXT("count"), Out.Num());
	Result->SetBoolField(TEXT("truncated"), bTruncated);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSourceControlHandlers::ListChanges(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* PathValues = nullptr;
	const bool bHasPaths = TryGetArrayParam(Params, TEXT("paths"), PathValues) && PathValues;
	const int32 Limit = FMath::Max(1, OptionalInt(Params, TEXT("limit"), 500));
	const bool bUntracked = OptionalBool(Params, TEXT("includeUntracked"), true);

	TArray<FString> Roots;
	if (bHasPaths)
	{
		TArray<FResolvedPath> Paths;
		if (auto Err = ResolvePaths(*PathValues, Paths)) return Err;
		Roots = FilesOf(Paths);
	}
	ISourceControlProvider* Provider = nullptr;
	if (auto Err = RequireProvider(Provider)) return Err;

	if (!bHasPaths)
	{
		Roots.Add(FullDirectoryPath(FPaths::ProjectContentDir()));
		Roots.Add(FullDirectoryPath(FPaths::ProjectConfigDir()));
		if (IFileManager::Get().DirectoryExists(*FPaths::GameSourceDir())) Roots.Add(FullDirectoryPath(FPaths::GameSourceDir()));
		if (IFileManager::Get().DirectoryExists(*FPaths::ProjectPluginsDir())) Roots.Add(FullDirectoryPath(FPaths::ProjectPluginsDir()));
		if (FPaths::IsProjectFilePathSet()) Roots.Add(FullFilePath(FPaths::GetProjectFilePath()));
	}

	TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> Update = ISourceControlOperation::Create<FUpdateStatus>();
	Update->SetGetOpenedOnly(!bUntracked);
	const bool bUpdated = Provider->Execute(Update, Roots) == ECommandResult::Succeeded;

	const bool bUsesCheckout = Provider->UsesCheckout();
	TArray<FSourceControlStateRef> States = Provider->GetCachedStateByPredicate([&Roots, bUntracked, bUsesCheckout](const FSourceControlStateRef& State)
	{
		return IsUnder(State->GetFilename(), Roots) && IsChange(State, bUntracked, bUsesCheckout);
	});
	States.Sort([](const FSourceControlStateRef& L, const FSourceControlStateRef& R) { return L->GetFilename() < R->GetFilename(); });

	// File -> changelist display name, where the provider groups by changelist.
	TMap<FString, FString> FileChangelist;
	TArray<TSharedPtr<FJsonValue>> Changelists;
	if (Provider->UsesChangelists())
	{
		TSharedRef<FUpdatePendingChangelistsStatus, ESPMode::ThreadSafe> Pending = ISourceControlOperation::Create<FUpdatePendingChangelistsStatus>();
		Pending->SetUpdateAllChangelists(true);
		Pending->SetUpdateFilesStates(true);
		Provider->Execute(Pending);
		for (const FSourceControlChangelistRef& Changelist : Provider->GetChangelists(EStateCacheUsage::Use))
		{
			const FSourceControlChangelistStatePtr ChangelistState = Provider->GetState(Changelist, EStateCacheUsage::Use);
			if (!ChangelistState.IsValid()) continue;
			const FString Name = ChangelistState->GetDisplayText().ToString();
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("changelist"), Name);
			Entry->SetStringField(TEXT("description"), ChangelistState->GetDescriptionText().ToString());
			Entry->SetBoolField(TEXT("isDefault"), Changelist->IsDefault());
			TArray<FString> ChangelistFiles;
			for (const FSourceControlStateRef& FileState : ChangelistState->GetFilesStates())
			{
				ChangelistFiles.Add(FileState->GetFilename());
				FileChangelist.Add(FileState->GetFilename(), Name);
			}
			Entry->SetArrayField(TEXT("files"), MCPStringListToJson(ChangelistFiles));
			Changelists.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	TArray<TSharedPtr<FJsonValue>> Out;
	bool bTruncated = false;
	for (const FSourceControlStateRef& State : States)
	{
		if (Out.Num() >= Limit) { bTruncated = true; break; }
		TSharedPtr<FJsonObject> Entry = StateToJson(State);
		if (const FString* Changelist = FileChangelist.Find(State->GetFilename()))
		{
			Entry->SetStringField(TEXT("changelist"), *Changelist);
		}
		Out.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("provider"), Provider->GetName().ToString());
	Result->SetArrayField(TEXT("roots"), MCPStringListToJson(Roots));
	Result->SetArrayField(TEXT("files"), Out);
	Result->SetNumberField(TEXT("count"), Out.Num());
	Result->SetNumberField(TEXT("total"), States.Num());
	Result->SetBoolField(TEXT("truncated"), bTruncated);
	if (Provider->UsesChangelists()) Result->SetArrayField(TEXT("changelists"), Changelists);
	if (!bUpdated)
	{
		Result->SetStringField(TEXT("warning"), TEXT("The provider's status refresh failed, so this list comes from its cache and may be stale."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSourceControlHandlers::Checkout(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* PathValues = nullptr;
	if (auto Err = RequireArray(Params, TEXT("paths"), PathValues)) return Err;
	TArray<FResolvedPath> Paths;
	if (auto Err = ResolvePaths(*PathValues, Paths)) return Err;
	ISourceControlProvider* Provider = nullptr;
	if (auto Err = RequireProvider(Provider)) return Err;

	TArray<TSharedPtr<FJsonObject>> Failed;
	TArray<FString> Already, NotRequired, ToCheckout;
	TMap<FString, FString> InputOf;
	for (const FResolvedPath& Path : Paths)
	{
		InputOf.Add(Path.File, Path.Input);
		if (Path.bDirectory) Failed.Add(Failure(Path.Input, Path.File, TEXT("a folder; name the files to check out")));
	}

	TArray<FSourceControlStateRef> States;
	TArray<FString> Files;
	for (const FResolvedPath& Path : Paths) if (!Path.bDirectory) Files.Add(Path.File);
	if (Files.Num() > 0 && !QueryStates(*Provider, Files, States))
	{
		return MCPError(TEXT("The provider could not report the state of the named files, so nothing was checked out."));
	}
	for (const FSourceControlStateRef& State : States)
	{
		const FString& File = State->GetFilename();
		const FString Input = InputOf.FindRef(File);
		FString Who;
		if (!Provider->UsesCheckout()) NotRequired.Add(File);
		else if (State->IsCheckedOut() || State->IsAdded()) Already.Add(File);
		else if (!State->IsSourceControlled()) Failed.Add(Failure(Input, File, TEXT("not under source control; use add")));
		else if (State->IsCheckedOutOther(&Who) && !State->CanCheckout()) Failed.Add(Failure(Input, File, FString::Printf(TEXT("locked by %s"), *Who)));
		else if (!State->IsCurrent()) Failed.Add(Failure(Input, File, TEXT("not at the latest revision; sync it first")));
		else if (!State->CanCheckout()) Failed.Add(Failure(Input, File, FString::Printf(TEXT("cannot be checked out in state '%s'"), *State->GetDisplayName().ToString())));
		else ToCheckout.Add(File);
	}

	TArray<FString> CheckedOut;
	if (ToCheckout.Num() > 0)
	{
		Provider->Execute(ISourceControlOperation::Create<FCheckOut>(), ToCheckout);
		TArray<FSourceControlStateRef> After;
		QueryStates(*Provider, ToCheckout, After);
		for (const FSourceControlStateRef& State : After)
		{
			if (State->IsCheckedOut()) CheckedOut.Add(State->GetFilename());
			else Failed.Add(Failure(InputOf.FindRef(State->GetFilename()), State->GetFilename(), TEXT("the provider refused the checkout; the output log carries its reason")));
		}
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("provider"), Provider->GetName().ToString());
	Result->SetArrayField(TEXT("checkedOut"), MCPStringListToJson(CheckedOut));
	Result->SetArrayField(TEXT("alreadyCheckedOut"), MCPStringListToJson(Already));
	if (NotRequired.Num() > 0)
	{
		Result->SetArrayField(TEXT("checkoutNotRequired"), MCPStringListToJson(NotRequired));
		Result->SetStringField(TEXT("note"), FString::Printf(TEXT("The %s provider does not use checkout; those files are writable as they are."), *Provider->GetName().ToString()));
	}
	Result->SetBoolField(TEXT("unchanged"), CheckedOut.Num() == 0);
	NoteFailures(Result, Failed, TEXT("checked out"));
	if (CheckedOut.Num() > 0)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("paths"), MCPStringListToJson(CheckedOut));
		MCPSetRollback(Result, TEXT("source_control_revert"), Payload);
	}
	else
	{
		MCPSetNoRollback(Result, TEXT("Nothing was checked out by this call, so there is nothing to revert."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSourceControlHandlers::MarkForAdd(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* PathValues = nullptr;
	if (auto Err = RequireArray(Params, TEXT("paths"), PathValues)) return Err;
	TArray<FResolvedPath> Paths;
	if (auto Err = ResolvePaths(*PathValues, Paths)) return Err;
	ISourceControlProvider* Provider = nullptr;
	if (auto Err = RequireProvider(Provider)) return Err;

	TArray<TSharedPtr<FJsonObject>> Failed;
	TArray<FString> Already, ToAdd, Files;
	TMap<FString, FString> InputOf;
	for (const FResolvedPath& Path : Paths)
	{
		InputOf.Add(Path.File, Path.Input);
		if (Path.bDirectory) Failed.Add(Failure(Path.Input, Path.File, TEXT("a folder; name the files to add")));
		else if (!IFileManager::Get().FileExists(*Path.File)) Failed.Add(Failure(Path.Input, Path.File, TEXT("not on disk; save it first")));
		else Files.Add(Path.File);
	}

	TArray<FSourceControlStateRef> States;
	if (Files.Num() > 0 && !QueryStates(*Provider, Files, States))
	{
		return MCPError(TEXT("The provider could not report the state of the named files, so nothing was added."));
	}
	for (const FSourceControlStateRef& State : States)
	{
		const FString& File = State->GetFilename();
		if (State->IsAdded() || (State->IsSourceControlled() && !State->IsDeleted())) Already.Add(File);
		else if (State->IsIgnored()) Failed.Add(Failure(InputOf.FindRef(File), File, TEXT("ignored by the provider")));
		else if (!State->CanAdd()) Failed.Add(Failure(InputOf.FindRef(File), File, FString::Printf(TEXT("cannot be added in state '%s'"), *State->GetDisplayName().ToString())));
		else ToAdd.Add(File);
	}

	TArray<FString> Added;
	if (ToAdd.Num() > 0)
	{
		Provider->Execute(ISourceControlOperation::Create<FMarkForAdd>(), ToAdd);
		TArray<FSourceControlStateRef> After;
		QueryStates(*Provider, ToAdd, After);
		for (const FSourceControlStateRef& State : After)
		{
			if (State->IsAdded() || State->IsSourceControlled()) Added.Add(State->GetFilename());
			else Failed.Add(Failure(InputOf.FindRef(State->GetFilename()), State->GetFilename(), TEXT("the provider refused the add; the output log carries its reason")));
		}
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("provider"), Provider->GetName().ToString());
	Result->SetArrayField(TEXT("added"), MCPStringListToJson(Added));
	Result->SetArrayField(TEXT("alreadyControlled"), MCPStringListToJson(Already));
	Result->SetBoolField(TEXT("unchanged"), Added.Num() == 0);
	NoteFailures(Result, Failed, TEXT("added"));
	if (Added.Num() > 0)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("paths"), MCPStringListToJson(Added));
		MCPSetRollback(Result, TEXT("source_control_revert"), Payload);
	}
	else
	{
		MCPSetNoRollback(Result, TEXT("Nothing was marked for add by this call, so there is nothing to revert."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSourceControlHandlers::Revert(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* PathValues = nullptr;
	if (auto Err = RequireArray(Params, TEXT("paths"), PathValues)) return Err;
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const bool bDiscardUnsaved = OptionalBool(Params, TEXT("discardUnsaved"), false);
	TArray<FResolvedPath> Paths;
	if (auto Err = ResolvePaths(*PathValues, Paths)) return Err;
	ISourceControlProvider* Provider = nullptr;
	if (auto Err = RequireProvider(Provider)) return Err;

	TArray<TSharedPtr<FJsonObject>> Failed;
	TArray<FString> Files, NotOpen;
	TMap<FString, FString> InputOf;
	for (const FResolvedPath& Path : Paths)
	{
		InputOf.Add(Path.File, Path.Input);
		if (Path.bDirectory) Failed.Add(Failure(Path.Input, Path.File, TEXT("a folder; name the files to revert")));
		else Files.Add(Path.File);
	}

	TArray<FSourceControlStateRef> States;
	if (Files.Num() > 0 && !QueryStates(*Provider, Files, States))
	{
		return MCPError(TEXT("The provider could not report the state of the named files, so nothing was reverted."));
	}

	TArray<FString> Candidates, Unsaved;
	TArray<TSharedPtr<FJsonValue>> WouldLose;
	for (const FSourceControlStateRef& State : States)
	{
		const FString& File = State->GetFilename();
		if (!State->CanRevert())
		{
			NotOpen.Add(File);
			continue;
		}
		Candidates.Add(File);
		TSharedPtr<FJsonObject> Entry = StateToJson(State);
		if (UPackage* Loaded = LoadedPackageForFile(File))
		{
			if (Loaded->IsDirty()) Unsaved.Add(File);
		}
		WouldLose.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("provider"), Provider->GetName().ToString());
	Result->SetArrayField(TEXT("notOpen"), MCPStringListToJson(NotOpen));
	Result->SetArrayField(TEXT("unsavedInEditor"), MCPStringListToJson(Unsaved));
	MCPSetNoRollback(Result, TEXT("A revert discards the local edits, and nothing restores them."));

	if (bDryRun)
	{
		Result->SetBoolField(TEXT("dryRun"), true);
		Result->SetArrayField(TEXT("wouldRevert"), WouldLose);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetArrayField(TEXT("failed"), ToJsonArray(Failed));
		return MCPResult(Result);
	}
	if (Unsaved.Num() > 0 && !bDiscardUnsaved)
	{
		TSharedPtr<FJsonObject> Error = MCPErrorObject(FString::Printf(
			TEXT("%d file(s) have unsaved edits in the editor that a revert would discard. Save them, or pass discardUnsaved=true to drop them. Nothing was reverted."),
			Unsaved.Num()));
		Error->SetArrayField(TEXT("unsavedInEditor"), MCPStringListToJson(Unsaved));
		Error->SetArrayField(TEXT("wouldRevert"), WouldLose);
		return MCPResult(Error);
	}

	TArray<FString> PackageFiles, OtherFiles;
	for (const FString& File : Candidates)
	{
		if (UPackage* Loaded = LoadedPackageForFile(File))
		{
			// The reload below would otherwise ask whether to discard them.
			if (bDiscardUnsaved && Loaded->IsDirty()) Loaded->SetDirtyFlag(false);
		}
		(IsPackageFile(File) ? PackageFiles : OtherFiles).Add(File);
	}
	// Packages are reverted through the helper that unloads and reloads them,
	// so a loaded asset does not keep the reverted file open or stale.
	if (PackageFiles.Num() > 0) USourceControlHelpers::RevertAndReloadPackages(PackageFiles);
	if (OtherFiles.Num() > 0) Provider->Execute(ISourceControlOperation::Create<FRevert>(), OtherFiles);

	TArray<FString> Reverted;
	if (Candidates.Num() > 0)
	{
		TArray<FSourceControlStateRef> After;
		QueryStates(*Provider, Candidates, After);
		for (const FSourceControlStateRef& State : After)
		{
			if (!State->CanRevert()) Reverted.Add(State->GetFilename());
			else Failed.Add(Failure(InputOf.FindRef(State->GetFilename()), State->GetFilename(),
				TEXT("still open after the revert; a loaded map or external actor package must be unloaded first, and the output log carries the provider's reason")));
		}
	}
	Result->SetArrayField(TEXT("reverted"), MCPStringListToJson(Reverted));
	Result->SetBoolField(TEXT("unchanged"), Reverted.Num() == 0);
	NoteFailures(Result, Failed, TEXT("reverted"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSourceControlHandlers::Diff(const TSharedPtr<FJsonObject>& Params)
{
	FString InPath;
	if (auto Err = RequireString(Params, TEXT("path"), InPath)) return Err;
	const bool bOpenEditor = OptionalBool(Params, TEXT("openEditor"), false);
	const int32 Context = FMath::Clamp(OptionalInt(Params, TEXT("contextLines"), 3), 0, 50);
	const int32 MaxLines = FMath::Clamp(OptionalInt(Params, TEXT("maxLines"), 400), 1, 20000);

	const FResolvedPath Path = ResolvePath(InPath);
	if (!Path.Error.IsEmpty()) return MCPError(FString::Printf(TEXT("'%s' %s."), *InPath, *Path.Error));
	if (Path.bDirectory) return MCPError(FString::Printf(TEXT("'%s' is a folder. diff takes one file."), *InPath));
	ISourceControlProvider* Provider = nullptr;
	if (auto Err = RequireProvider(Provider)) return Err;

	TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> Update = ISourceControlOperation::Create<FUpdateStatus>();
	Update->SetUpdateHistory(true);
	Provider->Execute(Update, Path.File);
	const FSourceControlStatePtr State = Provider->GetState(Path.File, EStateCacheUsage::Use);
	if (!State.IsValid() || !State->IsSourceControlled())
	{
		return MCPError(FString::Printf(TEXT("'%s' is not under source control, so there is no revision to diff against."), *Path.File));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("provider"), Provider->GetName().ToString());
	Result->SetStringField(TEXT("file"), Path.File);
	Result->SetObjectField(TEXT("status"), StateToJson(State.ToSharedRef()));
	if (State->IsAdded())
	{
		Result->SetBoolField(TEXT("newFile"), true);
		Result->SetStringField(TEXT("note"), TEXT("The file is marked for add and has no depot revision; all of it is new."));
		return MCPResult(Result);
	}
	if (State->GetHistorySize() == 0)
	{
		return MCPError(FString::Printf(TEXT("The %s provider returned no history for '%s', so it cannot be diffed here."), *Provider->GetName().ToString(), *Path.File));
	}
	const TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> Revision = State->GetHistoryItem(0);
	if (!Revision.IsValid())
	{
		return MCPError(TEXT("The provider's latest revision entry is empty, so there is nothing to diff against."));
	}
	TSharedPtr<FJsonObject> Against = MakeShared<FJsonObject>();
	Against->SetStringField(TEXT("revision"), Revision->GetRevision());
	Against->SetStringField(TEXT("user"), Revision->GetUserName());
	Against->SetStringField(TEXT("description"), Revision->GetDescription());
	Against->SetStringField(TEXT("date"), Revision->GetDate().ToIso8601());
	Result->SetObjectField(TEXT("against"), Against);

	if (IsPackageFile(Path.File))
	{
		Result->SetStringField(TEXT("kind"), TEXT("package"));
		Result->SetNumberField(TEXT("localBytes"), (double)IFileManager::Get().FileSize(*Path.File));
		Result->SetNumberField(TEXT("depotBytes"), Revision->GetFileSize());
		if (!bOpenEditor)
		{
			Result->SetStringField(TEXT("note"), TEXT("Packages are binary. Pass openEditor=true to open the editor's asset diff against this revision."));
			return MCPResult(Result);
		}
		if (!Provider->AllowsDiffAgainstDepot())
		{
			return MCPError(FString::Printf(TEXT("The %s provider does not support diffing against the depot."), *Provider->GetName().ToString()));
		}
		UPackage* Package = Path.PackageName.IsEmpty() ? nullptr : LoadPackage(nullptr, *Path.PackageName, LOAD_None);
		UObject* Asset = Package ? Package->FindAssetInPackage() : nullptr;
		if (!Asset)
		{
			return MCPError(FString::Printf(TEXT("'%s' holds no asset the editor can open a diff for."), *Path.File));
		}
		FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		AssetTools.Get().DiffAgainstDepot(Asset, Path.PackageName, Asset->GetName());
		Result->SetBoolField(TEXT("editorDiffOpened"), true);
		return MCPResult(Result);
	}

	FString DepotCopy;
	if (!Revision->Get(DepotCopy))
	{
		return MCPError(TEXT("The provider could not fetch the depot revision's content."));
	}
	TArray<FString> DepotLines, LocalLines;
	FFileHelper::LoadFileToStringArray(DepotLines, *DepotCopy);
	const bool bLocalExists = IFileManager::Get().FileExists(*Path.File);
	if (bLocalExists) FFileHelper::LoadFileToStringArray(LocalLines, *Path.File);

	TArray<FString> Lines;
	int32 Added = 0, Removed = 0;
	bool bTruncated = false, bApproximate = false;
	UnifiedDiff(DepotLines, LocalLines, Context, MaxLines, Lines, Added, Removed, bTruncated, bApproximate);
	Result->SetStringField(TEXT("kind"), TEXT("text"));
	Result->SetBoolField(TEXT("localExists"), bLocalExists);
	Result->SetBoolField(TEXT("identical"), Added == 0 && Removed == 0);
	Result->SetNumberField(TEXT("addedLines"), Added);
	Result->SetNumberField(TEXT("removedLines"), Removed);
	Result->SetStringField(TEXT("diff"), FString::Join(Lines, TEXT("\n")));
	Result->SetBoolField(TEXT("truncated"), bTruncated);
	if (bApproximate)
	{
		Result->SetStringField(TEXT("note"), TEXT("The changed region is too large to align line by line, so it is shown as one removal and one addition."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSourceControlHandlers::Submit(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* PathValues = nullptr;
	if (auto Err = RequireArray(Params, TEXT("paths"), PathValues)) return Err;
	FString Description;
	if (auto Err = RequireString(Params, TEXT("description"), Description)) return Err;
	TArray<FResolvedPath> Paths;
	if (auto Err = ResolvePaths(*PathValues, Paths)) return Err;
	ISourceControlProvider* Provider = nullptr;
	if (auto Err = RequireProvider(Provider)) return Err;

	TArray<TSharedPtr<FJsonObject>> Failed;
	TArray<FString> Files, ToSubmit, Unsaved;
	TMap<FString, FString> InputOf;
	for (const FResolvedPath& Path : Paths)
	{
		InputOf.Add(Path.File, Path.Input);
		if (Path.bDirectory) Failed.Add(Failure(Path.Input, Path.File, TEXT("a folder; name the files to submit")));
		else Files.Add(Path.File);
	}
	TArray<FSourceControlStateRef> States;
	if (Files.Num() > 0 && !QueryStates(*Provider, Files, States))
	{
		return MCPError(TEXT("The provider could not report the state of the named files, so nothing was submitted."));
	}
	for (const FSourceControlStateRef& State : States)
	{
		const FString& File = State->GetFilename();
		if (!State->CanCheckIn())
		{
			Failed.Add(Failure(InputOf.FindRef(File), File, FString::Printf(TEXT("nothing to submit in state '%s'"), *State->GetDisplayName().ToString())));
			continue;
		}
		if (UPackage* Loaded = LoadedPackageForFile(File))
		{
			if (Loaded->IsDirty()) Unsaved.Add(File);
		}
		ToSubmit.Add(File);
	}
	if (Unsaved.Num() > 0)
	{
		TSharedPtr<FJsonObject> Error = MCPErrorObject(TEXT("Some files have unsaved edits in the editor, which a submit would leave out. Save them first. Nothing was submitted."));
		Error->SetArrayField(TEXT("unsavedInEditor"), MCPStringListToJson(Unsaved));
		return MCPResult(Error);
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("provider"), Provider->GetName().ToString());
	MCPSetNoRollback(Result, TEXT("A submit is recorded in the repository history; undoing it is a new change, not a rollback."));
	TArray<FString> Submitted;
	if (ToSubmit.Num() > 0)
	{
		TSharedRef<FCheckIn, ESPMode::ThreadSafe> CheckIn = ISourceControlOperation::Create<FCheckIn>();
		CheckIn->SetDescription(FText::FromString(Description));
		if (Provider->Execute(CheckIn, ToSubmit) == ECommandResult::Succeeded)
		{
			Submitted = ToSubmit;
			Result->SetStringField(TEXT("message"), CheckIn->GetSuccessMessage().ToString());
		}
		else
		{
			for (const FString& File : ToSubmit)
			{
				Failed.Add(Failure(InputOf.FindRef(File), File, TEXT("the provider refused the submit; the output log carries its reason")));
			}
		}
	}
	Result->SetArrayField(TEXT("submitted"), MCPStringListToJson(Submitted));
	Result->SetBoolField(TEXT("unchanged"), Submitted.Num() == 0);
	NoteFailures(Result, Failed, TEXT("submitted"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSourceControlHandlers::SetCheckoutMode(const TSharedPtr<FJsonObject>& Params)
{
	FString Mode;
	if (auto Err = RequireString(Params, TEXT("mode"), Mode)) return Err;
	const bool bAuto = Mode.Equals(TEXT("auto"), ESearchCase::IgnoreCase);
	if (!bAuto && !Mode.Equals(TEXT("manual"), ESearchCase::IgnoreCase))
	{
		return MCPError(FString::Printf(TEXT("mode '%s' is not one of manual, auto."), *Mode));
	}
	const bool bWasAuto = MCPSourceControl::IsAutoCheckoutEnabled();
	MCPSourceControl::SetAutoCheckoutEnabled(bAuto);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("mode"), bAuto ? TEXT("auto") : TEXT("manual"));
	Result->SetStringField(TEXT("previousMode"), bWasAuto ? TEXT("auto") : TEXT("manual"));
	Result->SetBoolField(TEXT("unchanged"), bWasAuto == bAuto);
	Result->SetBoolField(TEXT("providerEnabled"), ISourceControlModule::Get().IsEnabled());
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("mode"), bWasAuto ? TEXT("auto") : TEXT("manual"));
	MCPSetRollback(Result, TEXT("source_control_set_checkout_mode"), Payload);
	return MCPResult(Result);
}

// ─── Registration ──────────────────────────────────────────────────────────

void FSourceControlHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("source_control"));
	using EType = EMCPParamType;
	const FMCPParamSpec PathsSpec = MCPParam::Required(TEXT("paths"), EType::Array,
		TEXT("Asset or package paths (/Game/X), content folders, or files on disk (absolute or relative to the project)")).Items(EType::String);

	Registry.RegisterHandler(TEXT("source_control_get_provider"), &GetProvider, {});
	Registry.RegisterHandlerWithTimeout(TEXT("source_control_get_status"), &GetStatus, 120.0f, {
		PathsSpec,
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Most files to report (default 500); truncated says when more matched")),
	});
	Registry.RegisterHandlerWithTimeout(TEXT("source_control_list_changes"), &ListChanges, 120.0f, {
		MCPParam::Optional(TEXT("paths"), EType::Array, TEXT("Folders or files to look under (default the project's Content, Config, Source and Plugins folders and its .uproject)")).Items(EType::String),
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Most files to report (default 500); truncated says when more matched")),
		MCPParam::Optional(TEXT("includeUntracked"), EType::Boolean, TEXT("Also list files on disk the provider does not track (default true)")),
	});
	Registry.RegisterHandler(TEXT("source_control_checkout"), &Checkout, { PathsSpec },
		MCPSpec::ContractExempt(TEXT("Checks files out of the configured provider when called")));
	Registry.RegisterHandler(TEXT("source_control_add"), &MarkForAdd, { PathsSpec },
		MCPSpec::ContractExempt(TEXT("Marks files for add in the configured provider when called")));
	Registry.RegisterHandler(TEXT("source_control_revert"), &Revert, {
		PathsSpec,
		MCPParam::Optional(TEXT("dryRun"), EType::Boolean, TEXT("Report what would be reverted and lost, and change nothing")),
		MCPParam::Optional(TEXT("discardUnsaved"), EType::Boolean, TEXT("Drop unsaved editor edits to the named packages instead of refusing (default false)")),
	}, MCPSpec::ContractExempt(TEXT("Reverts files in the configured provider when called")));
	Registry.RegisterHandlerWithTimeout(TEXT("source_control_diff"), &Diff, 120.0f, {
		MCPParam::Required(TEXT("path"), EType::String, TEXT("One asset path or file to diff against its latest repository revision")),
		MCPParam::Optional(TEXT("openEditor"), EType::Boolean, TEXT("For a package, open the editor's asset diff window (default false)")),
		MCPParam::Optional(TEXT("contextLines"), EType::Integer, TEXT("Unchanged lines around each text change (default 3)")),
		MCPParam::Optional(TEXT("maxLines"), EType::Integer, TEXT("Most diff lines to return (default 400); truncated says when cut")),
	});
	Registry.RegisterHandlerWithTimeout(TEXT("source_control_submit"), &Submit, 120.0f, {
		PathsSpec,
		MCPParam::Required(TEXT("description"), EType::String, TEXT("The change description or commit message")),
	}, MCPSpec::ContractExempt(TEXT("Submits files to the repository when called")));
	Registry.RegisterHandler(TEXT("source_control_set_checkout_mode"), &SetCheckoutMode, {
		MCPParam::Required(TEXT("mode"), EType::String, TEXT("manual refuses a write to a read-only file; auto checks it out first")).Enum({ TEXT("manual"), TEXT("auto") }),
	}, MCPSpec::ContractExempt(TEXT("Changes the editor session's checkout mode when called")));
}
