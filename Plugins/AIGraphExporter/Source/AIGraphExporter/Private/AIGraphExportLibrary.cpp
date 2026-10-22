#include "AIGraphExportLibrary.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace AIExport
{
using FObject = TSharedPtr<FJsonObject>;
using FValues = TArray<TSharedPtr<FJsonValue>>;

static FString Path(const UObject* Object) { return Object ? Object->GetPathName() : FString(); }
static TSharedPtr<FJsonValue> Value(const FObject& Object) { return MakeShared<FJsonValueObject>(Object); }
static FString Stringify(const FObject& Object)
{
    FString Text;
    FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
    return Text;
}

struct FContext
{
    TArray<FString> Unavailable;
    TSet<FString> RelatedBlueprints;
    void Missing(const FString& Detail) { Unavailable.AddUnique(Detail); }
    FString BlueprintForClass(const UClass* Class)
    {
        for (const UClass* Current = Class; Current; Current = Current->GetSuperClass())
        {
            if (const UBlueprint* Blueprint = Cast<UBlueprint>(Current->ClassGeneratedBy))
            {
                const FString Result = Blueprint->GetPathName();
                RelatedBlueprints.Add(Result);
                return Result;
            }
        }
        return FString(); // Native class: no Blueprint asset, not an invented path.
    }
};

// Reflection is used only to read serialized settings. UObject references remain paths;
// this avoids following owner/parent cycles or initializing Behavior Tree key selectors.
static TSharedPtr<FJsonValue> PropertyValue(const FProperty* Property, const void* Data,
    FContext& Context, const FString& Location, int32 Depth = 0)
{
    if (Depth > 12)
    {
        Context.Missing(Location + TEXT(": nested property depth exceeded; no value inferred"));
        return MakeShared<FJsonValueNull>();
    }
    if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
        return MakeShared<FJsonValueBoolean>(Bool->GetPropertyValue(Data));
    if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        const UObject* Object = ObjectProperty->GetObjectPropertyValue(Data);
        if (const UClass* Class = Cast<UClass>(Object)) Context.BlueprintForClass(Class);
        return Object ? MakeShared<FJsonValueString>(Path(Object)) : TSharedPtr<FJsonValue>(MakeShared<FJsonValueNull>());
    }
    if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Helper(Array, Data);
        FValues Values;
        for (int32 Index = 0; Index < Helper.Num(); ++Index)
            Values.Add(PropertyValue(Array->Inner, Helper.GetRawPtr(Index), Context,
                FString::Printf(TEXT("%s[%d]"), *Location, Index), Depth + 1));
        return MakeShared<FJsonValueArray>(Values);
    }
    if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
    {
        FObject Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("struct_type"), Struct->Struct->GetPathName());
        for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
        {
            if (It->HasAnyPropertyFlags(CPF_Transient)) continue;
            if (It->ArrayDim != 1)
            {
                Context.Missing(Location + TEXT(".") + It->GetName() + TEXT(": fixed-size struct field not decoded"));
                continue;
            }
            Result->SetField(It->GetName(), PropertyValue(*It, It->ContainerPtrToValuePtr<void>(Data),
                Context, Location + TEXT(".") + It->GetName(), Depth + 1));
        }
        return Value(Result);
    }
    // Text preserves enum labels, integer precision, names, map/set/delegate settings.
    // The representation is explicitly labelled instead of claiming structured decoding.
    FString Text;
    Property->ExportTextItem_Direct(Text, Data, nullptr, nullptr, PPF_None);
    FObject Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
    Result->SetStringField(TEXT("export_text"), Text);
    Result->SetStringField(TEXT("representation"), TEXT("unreal_property_text"));
    return Value(Result);
}

static FObject Properties(const UObject* Object, FContext& Context)
{
    FObject Result = MakeShared<FJsonObject>();
    for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
    {
        if (It->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) continue;
        if (It->ArrayDim == 1)
            Result->SetField(It->GetName(), PropertyValue(*It, It->ContainerPtrToValuePtr<void>(Object),
                Context, Object->GetPathName() + TEXT(".") + It->GetName()));
        else
        {
            FValues Values;
            for (int32 Index = 0; Index < It->ArrayDim; ++Index)
                Values.Add(PropertyValue(*It, It->ContainerPtrToValuePtr<void>(Object, Index), Context, It->GetName()));
            Result->SetArrayField(It->GetName(), Values);
        }
    }
    return Result;
}

static FObject Identity(const UObject* Object, FContext& Context)
{
    FObject Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("object_path"), Path(Object));
    Result->SetStringField(TEXT("class_path"), Path(Object->GetClass()));
    Result->SetStringField(TEXT("blueprint_asset_path"), Context.BlueprintForClass(Object->GetClass()));
    return Result;
}

static FObject Reference(const FMemberReference& Member, UClass* SelfClass,
    const UFunction* Function, FContext& Context)
{
    FObject Result = MakeShared<FJsonObject>();
    UClass* Owner = Member.GetMemberParentClass(SelfClass);
    Result->SetStringField(TEXT("member_name"), Member.GetMemberName().ToString());
    Result->SetBoolField(TEXT("self_context"), Member.IsSelfContext());
    Result->SetStringField(TEXT("owner_class_path"), Path(Owner));
    Result->SetStringField(TEXT("owner_package_path"), Path(Member.GetMemberParentPackage()));
    Result->SetStringField(TEXT("owner_blueprint_asset_path"), Context.BlueprintForClass(Owner));
    Result->SetStringField(TEXT("resolved_function_path"), Path(Function));
    Result->SetBoolField(TEXT("resolved"), Function != nullptr);
    if (!Function) Context.Missing(TEXT("Unresolved function reference: ") + Member.GetMemberName().ToString());
    return Result;
}

static FObject PinReference(const UEdGraphPin* Pin)
{
    FObject Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("node_path"), Path(Pin->GetOwningNode()));
    Result->SetStringField(TEXT("node_guid"), Pin->GetOwningNode()->NodeGuid.ToString());
    Result->SetStringField(TEXT("pin_id"), Pin->PinId.ToString());
    Result->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
    return Result;
}

static FObject PinType(const FEdGraphPinType& Type)
{
    FObject Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("category"), Type.PinCategory.ToString());
    Result->SetStringField(TEXT("subcategory"), Type.PinSubCategory.ToString());
    Result->SetStringField(TEXT("subcategory_object"), Path(Type.PinSubCategoryObject.Get()));
    Result->SetStringField(TEXT("container"), Type.IsArray() ? TEXT("array") : Type.IsSet() ? TEXT("set") : Type.IsMap() ? TEXT("map") : TEXT("none"));
    Result->SetBoolField(TEXT("is_reference"), Type.bIsReference);
    Result->SetBoolField(TEXT("is_const"), Type.bIsConst);
    Result->SetBoolField(TEXT("is_weak_pointer"), Type.bIsWeakPointer);
    Result->SetBoolField(TEXT("is_uobject_wrapper"), Type.bIsUObjectWrapper);
    Result->SetStringField(TEXT("member_name"), Type.PinSubCategoryMemberReference.MemberName.ToString());
    Result->SetStringField(TEXT("member_parent"), Path(Type.PinSubCategoryMemberReference.MemberParent));
    Result->SetStringField(TEXT("member_guid"), Type.PinSubCategoryMemberReference.MemberGuid.ToString());
    FObject Terminal = MakeShared<FJsonObject>();
    Terminal->SetStringField(TEXT("category"), Type.PinValueType.TerminalCategory.ToString());
    Terminal->SetStringField(TEXT("subcategory"), Type.PinValueType.TerminalSubCategory.ToString());
    Terminal->SetStringField(TEXT("subcategory_object"), Path(Type.PinValueType.TerminalSubCategoryObject.Get()));
    Terminal->SetBoolField(TEXT("is_const"), Type.PinValueType.bTerminalIsConst);
    Terminal->SetBoolField(TEXT("is_weak_pointer"), Type.PinValueType.bTerminalIsWeakPointer);
    Result->SetObjectField(TEXT("map_value_type"), Terminal);
    return Result;
}

static FObject Graph(const UEdGraph* GraphObject, UBlueprint* Blueprint, FContext& Context)
{
    FObject Result = Identity(GraphObject, Context);
    Result->SetStringField(TEXT("name"), GraphObject->GetName());
    Result->SetStringField(TEXT("schema_class"), Path(GraphObject->Schema));
    FValues Nodes;
    for (const UEdGraphNode* Node : GraphObject->Nodes)
    {
        if (!Node) { Context.Missing(Path(GraphObject) + TEXT(": null node entry")); continue; }
        FObject NodeJson = Identity(Node, Context);
        NodeJson->SetStringField(TEXT("name"), Node->GetName());
        NodeJson->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());
        NodeJson->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
        NodeJson->SetNumberField(TEXT("x"), Node->NodePosX);
        NodeJson->SetNumberField(TEXT("y"), Node->NodePosY);
        NodeJson->SetStringField(TEXT("stored_error_message"), Node->ErrorMsg);
        NodeJson->SetObjectField(TEXT("serialized_properties"), Properties(Node, Context));
        UClass* SelfClass = Blueprint ? Blueprint->GeneratedClass.Get() : nullptr;
        if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
            NodeJson->SetObjectField(TEXT("function_reference"), Reference(Call->FunctionReference, SelfClass, Call->GetTargetFunction(), Context));
        else if (const UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
        {
            // Native delegate signatures may belong to a package, not a UClass.
            // ResolveMember supports both in UE 5.4 and only reads the reference.
            const UFunction* Function = Event->EventReference.ResolveMember<UFunction>(SelfClass);
            NodeJson->SetObjectField(TEXT("event_reference"), Reference(Event->EventReference, SelfClass, Function, Context));
        }
        FValues Pins;
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin) { Context.Missing(Path(Node) + TEXT(": null pin entry")); continue; }
            FObject PinJson = PinReference(Pin);
            PinJson->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
            PinJson->SetStringField(TEXT("kind"), Pin->PinType.PinCategory == TEXT("exec") ? TEXT("execution") : TEXT("data"));
            PinJson->SetObjectField(TEXT("type"), PinType(Pin->PinType));
            PinJson->SetStringField(TEXT("default_value"), Pin->DefaultValue);
            PinJson->SetStringField(TEXT("autogenerated_default_value"), Pin->AutogeneratedDefaultValue);
            PinJson->SetStringField(TEXT("default_object"), Path(Pin->DefaultObject));
            PinJson->SetStringField(TEXT("default_text"), Pin->DefaultTextValue.ToString());
            FString TextIdentity;
            FTextStringHelper::WriteToBuffer(TextIdentity, Pin->DefaultTextValue);
            PinJson->SetStringField(TEXT("default_text_identity"), TextIdentity);
            PinJson->SetBoolField(TEXT("default_ignored"), Pin->bDefaultValueIsIgnored);
            PinJson->SetBoolField(TEXT("hidden"), Pin->bHidden);
            PinJson->SetBoolField(TEXT("orphaned"), Pin->bOrphanedPin);
            if (Pin->ParentPin) PinJson->SetObjectField(TEXT("parent_pin"), PinReference(Pin->ParentPin));
            FValues Links;
            for (const UEdGraphPin* Linked : Pin->LinkedTo)
            {
                if (Linked) Links.Add(Value(PinReference(Linked)));
                else Context.Missing(Path(Node) + TEXT(".") + Pin->PinName.ToString() + TEXT(": null linked pin"));
            }
            PinJson->SetArrayField(TEXT("links"), Links);
            FValues SubPins;
            for (const UEdGraphPin* Sub : Pin->SubPins) if (Sub) SubPins.Add(Value(PinReference(Sub)));
            PinJson->SetArrayField(TEXT("sub_pins"), SubPins);
            Pins.Add(Value(PinJson));
        }
        NodeJson->SetArrayField(TEXT("pins"), Pins);
        Nodes.Add(Value(NodeJson));
    }
    Result->SetArrayField(TEXT("nodes"), Nodes);
    return Result;
}

static FObject Blueprint(UBlueprint* Asset, FContext& Context)
{
    FObject Result = Identity(Asset, Context);
    Result->SetStringField(TEXT("parent_class"), Path(Asset->ParentClass));
    Result->SetStringField(TEXT("parent_blueprint_asset_path"), Context.BlueprintForClass(Asset->ParentClass));
    Result->SetStringField(TEXT("generated_class"), Path(Asset->GeneratedClass));
    Result->SetStringField(TEXT("graph_source"), TEXT("UBlueprint::GetAllGraphs (local editor graphs, including nested graphs)"));
    TArray<UEdGraph*> Graphs;
    Asset->GetAllGraphs(Graphs);
    FValues GraphValues;
    for (UEdGraph* Item : Graphs)
    {
        if (!Item) { Context.Missing(Path(Asset) + TEXT(": null graph")); continue; }
        FObject GraphJson = Graph(Item, Asset, Context);
        GraphJson->SetStringField(TEXT("kind"), Asset->UbergraphPages.Contains(Item) ? TEXT("event") :
            Asset->FunctionGraphs.Contains(Item) ? TEXT("function") : Asset->MacroGraphs.Contains(Item) ? TEXT("macro") : TEXT("nested_or_other"));
        GraphValues.Add(Value(GraphJson));
    }
    Result->SetArrayField(TEXT("graphs"), GraphValues);
    if (Graphs.IsEmpty()) Context.Missing(Path(Asset) + TEXT(": no serialized editor graphs available"));
    return Result;
}

static FValues DecoratorOps(const TArray<FBTDecoratorLogic>& Operations)
{
    FValues Result;
    for (const FBTDecoratorLogic& Operation : Operations)
    {
        FObject Item = MakeShared<FJsonObject>();
        const int32 Code = static_cast<int32>(Operation.Operation.GetValue());
        const TCHAR* Name = Code == EBTDecoratorLogic::Test ? TEXT("Test") : Code == EBTDecoratorLogic::And ? TEXT("And") :
            Code == EBTDecoratorLogic::Or ? TEXT("Or") : Code == EBTDecoratorLogic::Not ? TEXT("Not") : TEXT("Invalid");
        Item->SetStringField(TEXT("operation"), Name);
        Item->SetNumberField(TEXT("operation_code"), Code);
        Item->SetNumberField(TEXT("number"), Operation.Number);
        Result.Add(Value(Item));
    }
    return Result;
}

static FObject BTNode(const UBTNode* Node, FContext& Context, TSet<const UBTNode*>& Seen)
{
    FObject Result = Identity(Node, Context);
    if (Seen.Contains(Node))
    {
        Result->SetBoolField(TEXT("reference_only"), true);
        Context.Missing(Path(Node) + TEXT(": repeated node not expanded"));
        return Result;
    }
    Seen.Add(Node);
    Result->SetStringField(TEXT("name"), Node->GetNodeName());
    Result->SetObjectField(TEXT("settings"), Properties(Node, Context));
    FValues Services;
    if (const UBTCompositeNode* Composite = Cast<UBTCompositeNode>(Node))
    {
        Result->SetStringField(TEXT("kind"), TEXT("composite"));
        for (const UBTService* Service : Composite->Services)
            if (Service) Services.Add(Value(BTNode(Service, Context, Seen)));
        FValues Children;
        for (int32 Index = 0; Index < Composite->Children.Num(); ++Index)
        {
            const FBTCompositeChild& Child = Composite->Children[Index];
            FObject ChildJson = MakeShared<FJsonObject>();
            ChildJson->SetNumberField(TEXT("child_index"), Index);
            FValues Decorators;
            for (const UBTDecorator* Decorator : Child.Decorators)
                if (Decorator) Decorators.Add(Value(BTNode(Decorator, Context, Seen)));
            ChildJson->SetArrayField(TEXT("decorators"), Decorators);
            ChildJson->SetArrayField(TEXT("decorator_logic"), DecoratorOps(Child.DecoratorOps));
            const UBTNode* ChildNode = Child.ChildComposite ? static_cast<const UBTNode*>(Child.ChildComposite.Get()) : Child.ChildTask.Get();
            if (ChildNode) ChildJson->SetObjectField(TEXT("node"), BTNode(ChildNode, Context, Seen));
            else { ChildJson->SetField(TEXT("node"), MakeShared<FJsonValueNull>()); Context.Missing(Path(Node) + TEXT(": empty child")); }
            Children.Add(Value(ChildJson));
        }
        Result->SetArrayField(TEXT("children"), Children);
    }
    else if (const UBTTaskNode* Task = Cast<UBTTaskNode>(Node))
    {
        Result->SetStringField(TEXT("kind"), TEXT("task"));
        for (const UBTService* Service : Task->Services)
            if (Service) Services.Add(Value(BTNode(Service, Context, Seen)));
    }
    else Result->SetStringField(TEXT("kind"), Node->IsA<UBTDecorator>() ? TEXT("decorator") : Node->IsA<UBTService>() ? TEXT("service") : TEXT("other"));
    Result->SetArrayField(TEXT("services"), Services);
    return Result;
}

static FObject Blackboard(const UBlackboardData* Asset, FContext& Context, TSet<const UBlackboardData*>& Seen)
{
    FObject Result = Identity(Asset, Context);
    if (Seen.Contains(Asset)) { Context.Missing(Path(Asset) + TEXT(": parent cycle")); return Result; }
    Seen.Add(Asset);
    if (Asset->Parent) Result->SetObjectField(TEXT("parent"), Blackboard(Asset->Parent, Context, Seen));
    FValues Keys;
    for (const FBlackboardEntry& Entry : Asset->Keys)
    {
        FObject Key = MakeShared<FJsonObject>();
        Key->SetStringField(TEXT("name"), Entry.EntryName.ToString());
        Key->SetBoolField(TEXT("instance_synced"), Entry.bInstanceSynced);
        Key->SetStringField(TEXT("description"), Entry.EntryDescription);
        Key->SetStringField(TEXT("category"), Entry.EntryCategory.ToString());
        if (Entry.KeyType)
        {
            FObject Type = Identity(Entry.KeyType, Context);
            Type->SetObjectField(TEXT("settings"), Properties(Entry.KeyType, Context));
            Key->SetObjectField(TEXT("key_type"), Type);
        }
        else Context.Missing(Path(Asset) + TEXT(": missing type for key ") + Entry.EntryName.ToString());
        Keys.Add(Value(Key));
    }
    Result->SetArrayField(TEXT("local_keys"), Keys);
    return Result;
}

static FObject Tree(UBehaviorTree* Asset, FContext& Context)
{
    FObject Result = Identity(Asset, Context);
    TSet<const UBTNode*> Seen;
    if (Asset->RootNode) Result->SetObjectField(TEXT("root"), BTNode(Asset->RootNode, Context, Seen));
    else { Result->SetField(TEXT("root"), MakeShared<FJsonValueNull>()); Context.Missing(TEXT("Behavior Tree has no saved root")); }
    FValues RootDecorators;
    for (const UBTDecorator* Decorator : Asset->RootDecorators)
        if (Decorator) RootDecorators.Add(Value(BTNode(Decorator, Context, Seen)));
    Result->SetArrayField(TEXT("root_decorators"), RootDecorators);
    Result->SetArrayField(TEXT("root_decorator_logic"), DecoratorOps(Asset->RootDecoratorOps));
    if (Asset->BlackboardAsset)
    {
        TSet<const UBlackboardData*> SeenBoards;
        Result->SetObjectField(TEXT("blackboard"), Blackboard(Asset->BlackboardAsset, Context, SeenBoards));
    }
    else Context.Missing(TEXT("Behavior Tree has no Blackboard asset"));
    // Also preserve editor links without calling UpdateAsset/Compile or rebuilding the tree.
    if (Asset->BTGraph) Result->SetObjectField(TEXT("editor_graph"), Graph(Asset->BTGraph, nullptr, Context));
    else Context.Missing(TEXT("Behavior Tree editor graph not available"));
    return Result;
}

static FString Export(const FString& AssetPath, bool bTree)
{
    FObject Status = MakeShared<FJsonObject>();
    Status->SetBoolField(TEXT("success"), false);
    Status->SetStringField(TEXT("asset_path"), AssetPath);
    if (!IsInGameThread())
    {
        Status->SetStringField(TEXT("error"), TEXT("Export must run on the editor game thread"));
        return Stringify(Status);
    }
    UObject* Asset = bTree ? static_cast<UObject*>(LoadObject<UBehaviorTree>(nullptr, *AssetPath)) : LoadObject<UBlueprint>(nullptr, *AssetPath);
    if (!Asset)
    {
        Status->SetStringField(TEXT("error"), TEXT("Asset could not be loaded as the requested type"));
        return Stringify(Status);
    }
    const bool bDirtyBefore = Asset->GetOutermost()->IsDirty();
    FContext Context;
    FObject Document = MakeShared<FJsonObject>();
    Document->SetNumberField(TEXT("schema_version"), 1);
    Document->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
    Document->SetStringField(TEXT("exported_at_utc"), FDateTime::UtcNow().ToIso8601());
    Document->SetBoolField(TEXT("read_only"), true);
    Document->SetStringField(TEXT("source"), TEXT("loaded editor asset; may include pre-existing unsaved edits"));
    Document->SetObjectField(TEXT("asset"), bTree ? Tree(CastChecked<UBehaviorTree>(Asset), Context) : Blueprint(CastChecked<UBlueprint>(Asset), Context));
    FValues Related;
    TArray<FString> Paths = Context.RelatedBlueprints.Array();
    Paths.Sort();
    for (const FString& RelatedPath : Paths) Related.Add(MakeShared<FJsonValueString>(RelatedPath));
    Document->SetArrayField(TEXT("related_blueprint_assets"), Related);
    FValues Missing;
    for (const FString& Detail : Context.Unavailable) Missing.Add(MakeShared<FJsonValueString>(Detail));
    Document->SetArrayField(TEXT("unavailable_information"), Missing);
    FValues Limitations;
    for (const TCHAR* Detail : {
        TEXT("No live execution state, perception history, or runtime Blackboard values are captured."),
        TEXT("No Blueprint compilation, graph reconstruction, Behavior Tree rebuild, or package save is requested."),
        TEXT("Function references are exported; native function bodies and inherited Blueprint graphs are not expanded."),
        TEXT("Map/set/delegate and scalar settings retain Unreal property text; they are not parsed into inferred values."),
        TEXT("BT children use the saved runtime Children array order; unsaved editor changes can differ from that structure."),
        TEXT("Related Blueprint assets are identified, but their graphs are not automatically expanded."),
        TEXT("Static key selectors export stored SelectedKeyName and bNoneIsAllowedValue. Transient SelectedKeyID, SelectedKeyType and AllowedTypes are excluded; selectors are not resolved or initialized."),
        TEXT("Transient/deprecated UObject properties and transient struct fields are intentionally excluded.") })
        Limitations.Add(MakeShared<FJsonValueString>(Detail));
    Document->SetArrayField(TEXT("limitations"), Limitations);
    const bool bDirtyAfter = Asset->GetOutermost()->IsDirty();
    Document->SetBoolField(TEXT("package_dirty_before"), bDirtyBefore);
    Document->SetBoolField(TEXT("package_dirty_after"), bDirtyAfter);
    if (bDirtyBefore != bDirtyAfter)
    {
        Status->SetStringField(TEXT("error"), TEXT("Package dirty flag changed during inspection; no asset saved"));
        return Stringify(Status);
    }
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AIExports")));
    const FString Filename = FPaths::Combine(Directory, Asset->GetName() + TEXT("_") +
        FMD5::HashAnsiString(*Asset->GetPathName()).Left(8) + (bTree ? TEXT("_behavior_tree.json") : TEXT("_blueprint.json")));
    if (!IFileManager::Get().MakeDirectory(*Directory, true) ||
        !FFileHelper::SaveStringToFile(Stringify(Document), *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        Status->SetStringField(TEXT("error"), TEXT("Could not write export JSON under Saved/AIExports"));
        return Stringify(Status);
    }
    Status->SetBoolField(TEXT("success"), true);
    Status->SetStringField(TEXT("output_file"), Filename);
    Status->SetNumberField(TEXT("unavailable_count"), Context.Unavailable.Num());
    return Stringify(Status);
}
}

FString UAIGraphExportLibrary::ExportBlueprintGraph(const FString& AssetPath) { return AIExport::Export(AssetPath, false); }
FString UAIGraphExportLibrary::ExportBehaviorTree(const FString& AssetPath) { return AIExport::Export(AssetPath, true); }
FString UAIGraphExportLibrary::ExportGhostGraphs()
{
    const FString Blueprint = ExportBlueprintGraph(TEXT("/Game/BP_Object/AIGhost/AIC_Ghost"));
    const FString Tree = ExportBehaviorTree(TEXT("/Game/BP_Object/AIGhost/BT_Ghost"));
    TSharedPtr<FJsonObject> BPStatus, BTStatus;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Blueprint), BPStatus);
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Tree), BTStatus);
    TSharedPtr<FJsonObject> Status = MakeShared<FJsonObject>();
    Status->SetObjectField(TEXT("blueprint"), BPStatus);
    Status->SetObjectField(TEXT("behavior_tree"), BTStatus);
    Status->SetBoolField(TEXT("success"), BPStatus->GetBoolField(TEXT("success")) && BTStatus->GetBoolField(TEXT("success")));
    return AIExport::Stringify(Status);
}

UAIGraphExportCommandlet::UAIGraphExportCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
    ShowErrorCount = false;
}

int32 UAIGraphExportCommandlet::Main(const FString& Params)
{
    const FString Status = UAIGraphExportLibrary::ExportGhostGraphs();
    UE_LOG(LogTemp, Display, TEXT("AI_GRAPH_EXPORT_RESULT %s"), *Status);
    TSharedPtr<FJsonObject> Json;
    return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Status), Json) && Json->GetBoolField(TEXT("success")) ? 0 : 1;
}
