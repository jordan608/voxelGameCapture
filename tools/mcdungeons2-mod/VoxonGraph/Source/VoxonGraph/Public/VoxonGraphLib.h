// VoxonGraphLib.h - editor scripting for make_mod.py. Stock Unreal Python can give a Blueprint components and
// variables but not graph nodes; these calls add the nodes and join their pins, so the whole mod is built from a script.
#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "VoxonGraphLib.generated.h"

class UBlueprint;
class UEdGraphNode;

UCLASS()
class UVoxonGraphLib : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** An event the Blueprint's parent declares (ReceiveBeginPlay, ReceiveTick): the one already in the event graph, else
	 *  a new one. Enabled either way - a template's default events are greyed out until something is joined to them. */
	UFUNCTION(BlueprintCallable, Category = "Voxon Graph")
	static UEdGraphNode* Event(UBlueprint* Blueprint, FName EventName, int32 X, int32 Y);

	/** A call to Class's Function: a library's static function, or a member function called on its Target pin. */
	UFUNCTION(BlueprintCallable, Category = "Voxon Graph")
	static UEdGraphNode* Call(UBlueprint* Blueprint, UClass* Class, FName Function, int32 X, int32 Y);

	/** A Self node. */
	UFUNCTION(BlueprintCallable, Category = "Voxon Graph")
	static UEdGraphNode* Self(UBlueprint* Blueprint, int32 X, int32 Y);

	/** A Get of one of the Blueprint's own variables, its components included (compile the Blueprint after adding them). */
	UFUNCTION(BlueprintCallable, Category = "Voxon Graph")
	static UEdGraphNode* Get(UBlueprint* Blueprint, FName Variable, int32 X, int32 Y);

	/** A Set of one of the Blueprint's own variables. */
	UFUNCTION(BlueprintCallable, Category = "Voxon Graph")
	static UEdGraphNode* Set(UBlueprint* Blueprint, FName Variable, int32 X, int32 Y);

	/** A Get or a Set of another class's property (a component's FOVAngle, say), on its Target pin. */
	UFUNCTION(BlueprintCallable, Category = "Voxon Graph")
	static UEdGraphNode* GetMember(UBlueprint* Blueprint, UClass* Class, FName Property, int32 X, int32 Y);
	UFUNCTION(BlueprintCallable, Category = "Voxon Graph")
	static UEdGraphNode* SetMember(UBlueprint* Blueprint, UClass* Class, FName Property, int32 X, int32 Y);

	/** A Branch: execute, Condition -> then, else. */
	UFUNCTION(BlueprintCallable, Category = "Voxon Graph")
	static UEdGraphNode* Branch(UBlueprint* Blueprint, int32 X, int32 Y);

	/** A Sequence with Outputs exec outputs, then_0 to then_{Outputs-1}. */
	UFUNCTION(BlueprintCallable, Category = "Voxon Graph")
	static UEdGraphNode* Sequence(UBlueprint* Blueprint, int32 Outputs, int32 X, int32 Y);

	/** An array Get (by copy): Array, Dimension 1 -> Output; its type comes from the array joined to it. */
	UFUNCTION(BlueprintCallable, Category = "Voxon Graph")
	static UEdGraphNode* ArrayGet(UBlueprint* Blueprint, int32 X, int32 Y);

	/** Two pins joined, if the graph's schema allows it. */
	UFUNCTION(BlueprintCallable, Category = "Voxon Graph")
	static bool Link(UEdGraphNode* From, FName FromPin, UEdGraphNode* To, FName ToPin);

	/** A pin's literal value, as it would be typed in the graph editor ("0,0,210", "true", "2.0"). */
	UFUNCTION(BlueprintCallable, Category = "Voxon Graph")
	static bool SetPin(UEdGraphNode* Node, FName Pin, const FString& Value);

	/** Each pin as "name (in|out) category", to find pin names by. */
	UFUNCTION(BlueprintCallable, Category = "Voxon Graph")
	static TArray<FString> Pins(UEdGraphNode* Node);

	/** The event graph's nodes as the graph editor copies them, to read what a Blueprint holds. */
	UFUNCTION(BlueprintCallable, Category = "Voxon Graph")
	static FString Export(UBlueprint* Blueprint);
};
