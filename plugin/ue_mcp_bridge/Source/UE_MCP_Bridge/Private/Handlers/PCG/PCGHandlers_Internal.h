// Helpers shared by the PCG handler files. The module is a unity build, so a
// helper used by more than one PCGHandlers_*.cpp lives here, not as copies.
#pragma once

#include "CoreMinimal.h"
#include "HandlerUtils.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGEdge.h"
#include "PCGSettings.h"

namespace MCPPCG
{
	/** A node by engine name. UPCGGraph::GetNodes() leaves out the graph's input and output nodes, so they are checked too (#1324). */
	inline UPCGNode* FindNode(UPCGGraph* Graph, const FString& Name)
	{
		if (!Graph || Name.IsEmpty()) return nullptr;
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (Node && Node->GetName() == Name) return Node;
		}
		if (UPCGNode* In = Graph->GetInputNode(); In && In->GetName() == Name) return In;
		if (UPCGNode* Out = Graph->GetOutputNode(); Out && Out->GetName() == Name) return Out;
		return nullptr;
	}

	/** Whether Node is the graph's input or output node, which the graph owns and no handler may remove. */
	inline bool IsGraphIONode(const UPCGGraph* Graph, const UPCGNode* Node)
	{
		return Graph && Node && (Node == Graph->GetInputNode() || Node == Graph->GetOutputNode());
	}

	/** " - did you mean 'DefaultInputNode'?" for the names callers guess for the input/output nodes (#239), else empty. */
	inline FString IONodeNameHint(const UPCGGraph* Graph, const FString& Name)
	{
		if (!Graph) return FString();
		auto Hint = [](const UPCGNode* Node)
		{
			return Node ? FString::Printf(TEXT(" - did you mean '%s'?"), *Node->GetName()) : FString();
		};
		for (const TCHAR* Guess : { TEXT("Input"), TEXT("InputNode"), TEXT("GraphInput") })
		{
			if (Name.Equals(Guess, ESearchCase::IgnoreCase)) return Hint(Graph->GetInputNode());
		}
		for (const TCHAR* Guess : { TEXT("Output"), TEXT("OutputNode"), TEXT("GraphOutput") })
		{
			if (Name.Equals(Guess, ESearchCase::IgnoreCase)) return Hint(Graph->GetOutputNode());
		}
		return FString();
	}

	/** "<Role> not found: <name>", with the input/output hint when the name is a guess at one. */
	inline TSharedPtr<FJsonValue> NodeNotFoundError(const UPCGGraph* Graph, const FString& Name, const TCHAR* Role = TEXT("Node"))
	{
		const FString Hint = IONodeNameHint(Graph, Name);
		return MCPError(FString::Printf(TEXT("%s not found: %s%s"), Role, *Name,
			Hint.IsEmpty() ? TEXT(". read_graph lists the node names.") : *Hint));
	}
	/**
	 * Delivers a change event per written top-level property. Settings that rebuild pins from a property
	 * (Load PCG Data Asset's Asset, an IO node's Pins) react only to an event naming it (#1256).
	 */
	inline void NotifySettingsChanged(UPCGSettings* Settings, const TArray<FString>& WrittenKeys)
	{
		TSet<FName> Notified;
		for (const FString& Key : WrittenKeys)
		{
			int32 Cut = INDEX_NONE;
			FString Top = Key;
			if (Top.FindChar(TEXT('.'), Cut)) Top.LeftInline(Cut);
			if (Top.FindChar(TEXT('['), Cut)) Top.LeftInline(Cut);
			FProperty* Prop = Settings->GetClass()->FindPropertyByName(FName(*Top));
			if (!Prop || Notified.Contains(Prop->GetFName())) continue;
			Notified.Add(Prop->GetFName());
			FPropertyChangedEvent Event(Prop, EPropertyChangeType::ValueSet);
			// Public on UObject, protected on UPCGSettings; the call still dispatches to the settings override.
			static_cast<UObject*>(Settings)->PostEditChangeProperty(Event);
		}
		if (Notified.Num() == 0) Settings->PostEditChange();
	}

	/** One edge touching a node, as "from.fromPin->to.toPin" plus its parts. */
	struct FEdgeKey
	{
		FString From, FromPin, To, ToPin;
		FString Key() const { return From + TEXT(".") + FromPin + TEXT("->") + To + TEXT(".") + ToPin; }
		TSharedPtr<FJsonValue> ToJson() const
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("from"), From);
			Obj->SetStringField(TEXT("fromPin"), FromPin);
			Obj->SetStringField(TEXT("to"), To);
			Obj->SetStringField(TEXT("toPin"), ToPin);
			return MakeShared<FJsonValueObject>(Obj);
		}
	};

	/** Every edge on Node's input and output pins. */
	inline TArray<FEdgeKey> CollectNodeEdges(const UPCGNode* Node)
	{
		TArray<FEdgeKey> Out;
		if (!Node) return Out;
		for (const TObjectPtr<UPCGPin>& Pin : Node->GetOutputPins())
		{
			if (!Pin) continue;
			for (const TObjectPtr<UPCGEdge>& Edge : Pin->Edges)
			{
				const UPCGPin* Other = Edge ? (Edge->InputPin == Pin ? Edge->OutputPin.Get() : Edge->InputPin.Get()) : nullptr;
				if (!Other || !Other->Node) continue;
				Out.Add({ Node->GetName(), Pin->Properties.Label.ToString(), Other->Node->GetName(), Other->Properties.Label.ToString() });
			}
		}
		for (const TObjectPtr<UPCGPin>& Pin : Node->GetInputPins())
		{
			if (!Pin) continue;
			for (const TObjectPtr<UPCGEdge>& Edge : Pin->Edges)
			{
				const UPCGPin* Other = Edge ? (Edge->OutputPin == Pin ? Edge->InputPin.Get() : Edge->OutputPin.Get()) : nullptr;
				if (!Other || !Other->Node) continue;
				Out.Add({ Other->Node->GetName(), Other->Properties.Label.ToString(), Node->GetName(), Pin->Properties.Label.ToString() });
			}
		}
		return Out;
	}

	/** The edges in Before that are not in After, as JSON {from, fromPin, to, toPin}. */
	inline TArray<TSharedPtr<FJsonValue>> DroppedEdges(const TArray<FEdgeKey>& Before, const TArray<FEdgeKey>& After)
	{
		TSet<FString> Still;
		for (const FEdgeKey& E : After) Still.Add(E.Key());
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FEdgeKey& E : Before)
		{
			if (!Still.Contains(E.Key())) Out.Add(E.ToJson());
		}
		return Out;
	}

	inline TArray<TSharedPtr<FJsonValue>> PinLabelsJson(const TArray<TObjectPtr<UPCGPin>>& Pins)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const UPCGPin* Pin : Pins)
		{
			if (Pin) Out.Add(MakeShared<FJsonValueString>(Pin->Properties.Label.ToString()));
		}
		return Out;
	}
}
