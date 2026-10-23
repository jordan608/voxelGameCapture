#include "VoxonGraphLib.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_GetArrayItem.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, VoxonGraph)

DEFINE_LOG_CATEGORY_STATIC(LogVoxonGraph, Log, All);

namespace
{
	UEdGraph* EventGraph(UBlueprint* Blueprint)
	{
		if (!Blueprint) return nullptr;
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
			if (Graph && Graph->GetFName() == UEdGraphSchema_K2::GN_EventGraph) return Graph;
		return Blueprint->UbergraphPages.Num() ? Blueprint->UbergraphPages[0] : nullptr;
	}

	// A new node of type T in the event graph; its pins come with Finish.
	template <class T>
	T* Place(UEdGraph* Graph, int32 X, int32 Y)
	{
		T* Node = NewObject<T>(Graph);
		Node->CreateNewGuid();
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Graph->AddNode(Node, false, false);
		return Node;
	}

	UEdGraphNode* Finish(UEdGraphNode* Node, UBlueprint* Blueprint)
	{
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		return Node;
	}
}

UEdGraphNode* UVoxonGraphLib::Event(UBlueprint* Blueprint, FName EventName, int32 X, int32 Y)
{
	UEdGraph* Graph = EventGraph(Blueprint);
	UFunction* Function = (Blueprint && Blueprint->ParentClass) ? Blueprint->ParentClass->FindFunctionByName(EventName) : nullptr;
	if (!Graph || !Function)
	{
		UE_LOG(LogVoxonGraph, Error, TEXT("Event: %s has no event %s"), *GetNameSafe(Blueprint), *EventName.ToString());
		return nullptr;
	}
	UClass* Declaring = Function->GetOwnerClass();
	if (UK2Node_Event* Found = FBlueprintEditorUtils::FindOverrideForFunction(Blueprint, Declaring, EventName))
	{
		Found->SetEnabledState(ENodeEnabledState::Enabled, false);
		return Found;
	}
	UK2Node_Event* Node = Place<UK2Node_Event>(Graph, X, Y);
	Node->EventReference.SetExternalMember(EventName, Declaring);
	Node->bOverrideFunction = true;
	return Finish(Node, Blueprint);
}

UEdGraphNode* UVoxonGraphLib::Call(UBlueprint* Blueprint, UClass* Class, FName Function, int32 X, int32 Y)
{
	UEdGraph* Graph = EventGraph(Blueprint);
	UFunction* Found = Class ? Class->FindFunctionByName(Function) : nullptr;
	if (!Graph || !Found)
	{
		UE_LOG(LogVoxonGraph, Error, TEXT("Call: %s has no function %s"), *GetNameSafe(Class), *Function.ToString());
		return nullptr;
	}
	UK2Node_CallFunction* Node = Place<UK2Node_CallFunction>(Graph, X, Y);
	Node->SetFromFunction(Found);
	return Finish(Node, Blueprint);
}

UEdGraphNode* UVoxonGraphLib::Self(UBlueprint* Blueprint, int32 X, int32 Y)
{
	UEdGraph* Graph = EventGraph(Blueprint);
	return Graph ? Finish(Place<UK2Node_Self>(Graph, X, Y), Blueprint) : nullptr;
}

UEdGraphNode* UVoxonGraphLib::Get(UBlueprint* Blueprint, FName Variable, int32 X, int32 Y)
{
	UEdGraph* Graph = EventGraph(Blueprint);
	if (!Graph) return nullptr;
	UK2Node_VariableGet* Node = Place<UK2Node_VariableGet>(Graph, X, Y);
	Node->VariableReference.SetSelfMember(Variable);
	return Finish(Node, Blueprint);
}

UEdGraphNode* UVoxonGraphLib::Set(UBlueprint* Blueprint, FName Variable, int32 X, int32 Y)
{
	UEdGraph* Graph = EventGraph(Blueprint);
	if (!Graph) return nullptr;
	UK2Node_VariableSet* Node = Place<UK2Node_VariableSet>(Graph, X, Y);
	Node->VariableReference.SetSelfMember(Variable);
	return Finish(Node, Blueprint);
}

UEdGraphNode* UVoxonGraphLib::GetMember(UBlueprint* Blueprint, UClass* Class, FName Property, int32 X, int32 Y)
{
	UEdGraph* Graph = EventGraph(Blueprint);
	if (!Graph || !Class || !Class->FindPropertyByName(Property))
	{
		UE_LOG(LogVoxonGraph, Error, TEXT("GetMember: %s has no property %s"), *GetNameSafe(Class), *Property.ToString());
		return nullptr;
	}
	UK2Node_VariableGet* Node = Place<UK2Node_VariableGet>(Graph, X, Y);
	Node->VariableReference.SetExternalMember(Property, Class);
	return Finish(Node, Blueprint);
}

UEdGraphNode* UVoxonGraphLib::SetMember(UBlueprint* Blueprint, UClass* Class, FName Property, int32 X, int32 Y)
{
	UEdGraph* Graph = EventGraph(Blueprint);
	if (!Graph || !Class || !Class->FindPropertyByName(Property))
	{
		UE_LOG(LogVoxonGraph, Error, TEXT("SetMember: %s has no property %s"), *GetNameSafe(Class), *Property.ToString());
		return nullptr;
	}
	UK2Node_VariableSet* Node = Place<UK2Node_VariableSet>(Graph, X, Y);
	Node->VariableReference.SetExternalMember(Property, Class);
	return Finish(Node, Blueprint);
}

UEdGraphNode* UVoxonGraphLib::Branch(UBlueprint* Blueprint, int32 X, int32 Y)
{
	UEdGraph* Graph = EventGraph(Blueprint);
	return Graph ? Finish(Place<UK2Node_IfThenElse>(Graph, X, Y), Blueprint) : nullptr;
}

UEdGraphNode* UVoxonGraphLib::Sequence(UBlueprint* Blueprint, int32 Outputs, int32 X, int32 Y)
{
	UEdGraph* Graph = EventGraph(Blueprint);
	if (!Graph) return nullptr;
	UK2Node_ExecutionSequence* Node = Place<UK2Node_ExecutionSequence>(Graph, X, Y);
	Finish(Node, Blueprint);
	for (int32 i = 2; i < Outputs; ++i) Node->AddInputPin();   // it starts with two; AddInputPin adds an output
	return Node;
}

UEdGraphNode* UVoxonGraphLib::ArrayGet(UBlueprint* Blueprint, int32 X, int32 Y)
{
	UEdGraph* Graph = EventGraph(Blueprint);
	return Graph ? Finish(Place<UK2Node_GetArrayItem>(Graph, X, Y), Blueprint) : nullptr;
}

bool UVoxonGraphLib::Link(UEdGraphNode* From, FName FromPin, UEdGraphNode* To, FName ToPin)
{
	UEdGraphPin* A = From ? From->FindPin(FromPin) : nullptr;
	UEdGraphPin* B = To ? To->FindPin(ToPin) : nullptr;
	if (!A || !B)
	{
		UE_LOG(LogVoxonGraph, Error, TEXT("Link: no pin %s on %s, or no pin %s on %s"), *FromPin.ToString(),
			*GetNameSafe(From), *ToPin.ToString(), *GetNameSafe(To));
		return false;
	}
	const bool bJoined = A->GetSchema()->TryCreateConnection(A, B);
	if (!bJoined)
		UE_LOG(LogVoxonGraph, Error, TEXT("Link: %s.%s cannot be joined to %s.%s"), *GetNameSafe(From), *FromPin.ToString(),
			*GetNameSafe(To), *ToPin.ToString());
	return bJoined;
}

bool UVoxonGraphLib::SetPin(UEdGraphNode* Node, FName Pin, const FString& Value)
{
	UEdGraphPin* P = Node ? Node->FindPin(Pin) : nullptr;
	if (!P)
	{
		UE_LOG(LogVoxonGraph, Error, TEXT("SetPin: no pin %s on %s"), *Pin.ToString(), *GetNameSafe(Node));
		return false;
	}
	P->GetSchema()->TrySetDefaultValue(*P, Value);
	return true;
}

TArray<FString> UVoxonGraphLib::Pins(UEdGraphNode* Node)
{
	TArray<FString> Out;
	if (Node)
		for (const UEdGraphPin* P : Node->Pins)
			Out.Add(FString::Printf(TEXT("%s (%s) %s%s"), *P->PinName.ToString(), P->Direction == EGPD_Input ? TEXT("in") : TEXT("out"),
				*P->PinType.PinCategory.ToString(), P->bHidden ? TEXT(" hidden") : TEXT("")));
	return Out;
}

FString UVoxonGraphLib::Export(UBlueprint* Blueprint)
{
	FString Text;
	if (UEdGraph* Graph = EventGraph(Blueprint))
	{
		TSet<UObject*> Nodes;
		for (UEdGraphNode* Node : Graph->Nodes) Nodes.Add(Node);
		FEdGraphUtilities::ExportNodesToText(Nodes, Text);
	}
	return Text;
}
