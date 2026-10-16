// PCG graph authoring through the registry: graph pins, selectors, settings readback, import upserts,
// graph parameters and graph instances. Graphs live on a private mount, never the open project.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/PCG/PCGHandlers.h"
#include "Tests/MCPScopedTestMount.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "Metadata/PCGAttributePropertySelector.h"

namespace PCGGraphAuthoringTests
{
	const TCHAR* const Root = TEXT("/UEMCPPCGAuthoring/");

	TSharedPtr<FJsonObject> Invoke(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
	{
		const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Params);
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : MakeShared<FJsonObject>();
	}

	bool Ok(const TSharedPtr<FJsonObject>& Result)
	{
		bool bSuccess = false;
		return Result->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
	}

	/** A new empty graph on the test mount; its object path. */
	FString NewGraph(FMCPHandlerRegistry& Registry)
	{
		auto Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("name"), TEXT("PCG_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
		Params->SetStringField(TEXT("packagePath"), FString(Root) + TEXT("Graphs"));
		const TSharedPtr<FJsonObject> Created = Invoke(Registry, TEXT("create_pcg_graph"), Params);
		return Ok(Created) ? Created->GetStringField(TEXT("path")) : FString();
	}

	TArray<FString> Labels(const TArray<TObjectPtr<UPCGPin>>& Pins)
	{
		TArray<FString> Out;
		for (const UPCGPin* Pin : Pins) if (Pin) Out.Add(Pin->Properties.Label.ToString());
		return Out;
	}

	/** add_pcg_node; the new node's engine name, empty on failure. */
	FString AddNode(FMCPHandlerRegistry& Registry, const FString& GraphPath, const TCHAR* NodeType)
	{
		auto Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), GraphPath);
		Params->SetStringField(TEXT("nodeType"), NodeType);
		const TSharedPtr<FJsonObject> Added = Invoke(Registry, TEXT("add_pcg_node"), Params);
		return Ok(Added) ? Added->GetStringField(TEXT("nodeName")) : FString();
	}

	TSharedPtr<FJsonObject> WriteSettings(FMCPHandlerRegistry& Registry, const FString& GraphPath, const FString& NodeName, const TSharedPtr<FJsonObject>& Settings)
	{
		auto Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), GraphPath);
		Params->SetStringField(TEXT("nodeName"), NodeName);
		Params->SetObjectField(TEXT("settings"), Settings);
		return Invoke(Registry, TEXT("set_pcg_node_settings"), Params);
	}

	UPCGNode* FindNode(UPCGGraph* Graph, const FString& Name)
	{
		for (UPCGNode* Node : Graph->GetNodes()) if (Node && Node->GetName() == Name) return Node;
		return nullptr;
	}

	/** A selector property on a node's settings, in editor syntax; empty when the node or property is missing. */
	FString SelectorText(UPCGGraph* Graph, const FString& NodeName, const TCHAR* Property)
	{
		const UPCGNode* Node = FindNode(Graph, NodeName);
		const UPCGSettings* Settings = Node ? Node->GetSettings() : nullptr;
		const FStructProperty* Prop = Settings ? CastField<FStructProperty>(Settings->GetClass()->FindPropertyByName(Property)) : nullptr;
		if (!Prop) return FString();
		return Prop->ContainerPtrToValuePtr<FPCGAttributePropertySelector>(const_cast<UPCGSettings*>(Settings))->GetDisplayText().ToString();
	}

	TSharedPtr<FJsonValue> Pin(const TCHAR* Label, const TCHAR* Type = nullptr)
	{
		auto Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("label"), Label);
		if (Type) Obj->SetStringField(TEXT("type"), Type);
		return MakeShared<FJsonValueObject>(Obj);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPPCGGraphPinsTest,
	"UE.MCP.PCG.Authoring.GraphPins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPCGGraphPinsTest::RunTest(const FString& Parameters)
{
	using namespace PCGGraphAuthoringTests;
	const FMCPScopedTestMount Mount{ FString(Root), TEXT("UEMCPPCGAuthoring") };
	FMCPHandlerRegistry Registry;
	FPCGHandlers::RegisterHandlers(Registry);
	const FString GraphPath = NewGraph(Registry);
	if (!TestFalse(TEXT("a graph is created"), GraphPath.IsEmpty())) return false;
	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!TestNotNull(TEXT("the graph loads"), Graph)) return false;

	auto SetPins = [&](const TCHAR* Side, const TArray<TSharedPtr<FJsonValue>>& Pins, const TCHAR* Mode)
	{
		auto Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), GraphPath);
		Params->SetStringField(TEXT("side"), Side);
		Params->SetArrayField(TEXT("pins"), Pins);
		if (Mode) Params->SetStringField(TEXT("mode"), Mode);
		return Invoke(Registry, TEXT("set_pcg_graph_pins"), Params);
	};

	const TArray<FString> Before = Labels(Graph->GetInputNode()->GetOutputPins());
	const TSharedPtr<FJsonObject> Appended = SetPins(TEXT("input"), { Pin(TEXT("Mass"), TEXT("Point")) }, nullptr);
	TestTrue(TEXT("append succeeds"), Ok(Appended));
	TestTrue(TEXT("the input node grows the pin"), Labels(Graph->GetInputNode()->GetOutputPins()).Contains(TEXT("Mass")));
	TestEqual(TEXT("existing pins are kept"), Labels(Graph->GetInputNode()->GetOutputPins()).Num(), Before.Num() + 1);
	TestTrue(TEXT("a rollback restores the previous Pins"), Appended->HasField(TEXT("rollback")));

	TestTrue(TEXT("an output pin is added"), Ok(SetPins(TEXT("output"), { Pin(TEXT("Debug"), TEXT("Param")) }, TEXT("append"))));
	TestTrue(TEXT("the output node grows the pin"), Labels(Graph->GetOutputNode()->GetInputPins()).Contains(TEXT("Debug")));

	TestTrue(TEXT("remove succeeds"), Ok(SetPins(TEXT("input"), { Pin(TEXT("Mass")) }, TEXT("remove"))));
	TestFalse(TEXT("the pin is gone"), Labels(Graph->GetInputNode()->GetOutputPins()).Contains(TEXT("Mass")));

	const TArray<FString> Unchanged = Labels(Graph->GetInputNode()->GetOutputPins());
	TestFalse(TEXT("an unknown type is refused"), Ok(SetPins(TEXT("input"), { Pin(TEXT("Bad"), TEXT("NotAType")) }, nullptr)));
	TestFalse(TEXT("removing a missing label is refused"), Ok(SetPins(TEXT("input"), { Pin(TEXT("Nope")) }, TEXT("remove"))));
	TestFalse(TEXT("a bad side is refused"), Ok(SetPins(TEXT("middle"), { Pin(TEXT("X")) }, nullptr)));
	TestEqual(TEXT("refusals change nothing"), Labels(Graph->GetInputNode()->GetOutputPins()), Unchanged);

	TestTrue(TEXT("replace succeeds"), Ok(SetPins(TEXT("input"), { Pin(TEXT("A"), TEXT("Point")), Pin(TEXT("B"), TEXT("Spatial")) }, TEXT("replace"))));
	TestEqual(TEXT("the pins are exactly the list"), Labels(Graph->GetInputNode()->GetOutputPins()), TArray<FString>{ TEXT("A"), TEXT("B") });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPPCGSelectorSyntaxTest,
	"UE.MCP.PCG.Authoring.SelectorSyntax",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPCGSelectorSyntaxTest::RunTest(const FString& Parameters)
{
	using namespace PCGGraphAuthoringTests;
	const FMCPScopedTestMount Mount{ FString(Root), TEXT("UEMCPPCGAuthoring") };
	FMCPHandlerRegistry Registry;
	FPCGHandlers::RegisterHandlers(Registry);
	const FString GraphPath = NewGraph(Registry);
	UPCGGraph* Graph = GraphPath.IsEmpty() ? nullptr : LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!TestNotNull(TEXT("the graph loads"), Graph)) return false;
	const FString Filter = AddNode(Registry, GraphPath, TEXT("PCGAttributeFilteringSettings"));
	if (!TestFalse(TEXT("a filter node is added"), Filter.IsEmpty())) return false;

	for (const TCHAR* Text : { TEXT("ST_Meadow_RedGrass"), TEXT("$Density"), TEXT("$Position.Z"), TEXT("@Last") })
	{
		auto Settings = MakeShared<FJsonObject>();
		Settings->SetStringField(TEXT("TargetAttribute"), Text);
		TestTrue(FString::Printf(TEXT("'%s' is accepted"), Text), Ok(WriteSettings(Registry, GraphPath, Filter, Settings)));
		TestEqual(FString::Printf(TEXT("'%s' reads back as written"), Text), SelectorText(Graph, Filter, TEXT("TargetAttribute")), FString(Text));
	}

	// Through import_graph as well.
	auto NodeSettings = MakeShared<FJsonObject>();
	NodeSettings->SetStringField(TEXT("TargetAttribute"), TEXT("$Position.Y"));
	auto Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("name"), TEXT("ImportedFilter"));
	Node->SetStringField(TEXT("class"), TEXT("PCGAttributeFilteringSettings"));
	Node->SetObjectField(TEXT("settings"), NodeSettings);
	auto Import = MakeShared<FJsonObject>();
	Import->SetStringField(TEXT("assetPath"), GraphPath);
	Import->SetArrayField(TEXT("nodes"), { MakeShared<FJsonValueObject>(Node) });
	TestTrue(TEXT("import_graph accepts selector syntax"), Ok(Invoke(Registry, TEXT("import_pcg_graph"), Import)));
	TestEqual(TEXT("the imported selector keeps its extra name"), SelectorText(Graph, TEXT("ImportedFilter"), TEXT("TargetAttribute")), FString(TEXT("$Position.Y")));
	return true;
}

#endif
