#include "kqp_host_impl.h"

#include <ydb/core/kqp/common/kqp_yql.h>
#include <ydb/core/kqp/provider/yql_kikimr_provider_impl.h>

#include <ydb/library/yql/core/type_ann/type_ann_core.h>
#include "ydb/library/yql/core/type_ann/type_ann_impl.h"
#include <ydb/library/yql/core/yql_opt_utils.h>
#include <ydb/library/yql/core/yql_expr_type_annotation.h>
#include <ydb/library/yql/dq/type_ann/dq_type_ann.h>
#include <ydb/library/yql/utils/log/log.h>

namespace NKikimr {
namespace NKqp {

using namespace NYql;
using namespace NYql::NDq;
using namespace NYql::NNodes;

using TStatus = IGraphTransformer::TStatus;

namespace {

const TTypeAnnotationNode* MakeKqpEffectType(TExprContext& ctx) {
    return ctx.MakeType<TResourceExprType>(KqpEffectTag);
}

bool CheckKeyTuple(const TKqlKeyTuple& tuple, const TKikimrTableDescription& tableDesc,
    const TKikimrTableMetadataPtr meta, TExprContext& ctx)
{
    YQL_ENSURE(meta);

    for (ui32 i = 0; i < tuple.ArgCount(); ++i) {
        auto actualType = tuple.Arg(i).Ref().GetTypeAnn();
        YQL_ENSURE(actualType);

        YQL_ENSURE(i < meta->KeyColumnNames.size());
        auto expectedType = tableDesc.GetColumnType(meta->KeyColumnNames[i]);
        YQL_ENSURE(expectedType);

        if (IsSameAnnotation(*expectedType, *actualType)) {
            continue;
        }

        if (expectedType->GetKind() == ETypeAnnotationKind::Optional) {
            if (IsSameAnnotation(*expectedType->Cast<TOptionalExprType>()->GetItemType(), *actualType)) {
                continue;
            }
        }

        ctx.AddError(TIssue(ctx.GetPosition(tuple.Pos()), TStringBuilder()
            << "Table key type mismatch"
            << ", column: " << meta->KeyColumnNames[i]
            << ", table: " << meta->Name
            << ", expected: " << *expectedType
            << ", actual: " << *actualType));

        return false;
    }

    return true;
}

TStatus AnnotateTable(const TExprNode::TPtr& node, TExprContext& ctx, const TString& cluster,
    const TKikimrTablesData& tablesData)
{
    if (!EnsureArgsCount(*node, 4, ctx)) {
        return TStatus::Error;
    }

    auto* path = node->Child(TKqpTable::idx_Path);
    auto* pathId = node->Child(TKqpTable::idx_PathId);
    auto* sysView = node->Child(TKqpTable::idx_SysView);
    auto* version = node->Child(TKqpTable::idx_Version);

    if (!EnsureAtom(*path, ctx) || !EnsureAtom(*pathId, ctx) || !EnsureAtom(*sysView, ctx) || !EnsureAtom(*version, ctx)) {
        return TStatus::Error;
    }

    TString tablePath(path->Content());
    auto tableDesc = tablesData.EnsureTableExists(cluster, tablePath, node->Pos(), ctx);
    if (!tableDesc) {
        return TStatus::Error;
    }

    YQL_ENSURE(tableDesc->Metadata);
    auto& meta  = *tableDesc->Metadata;

    if (meta.PathId.ToString() != pathId->Content()) {
        ctx.AddError(YqlIssue(ctx.GetPosition(node->Pos()), TIssuesIds::UNEXPECTED, TStringBuilder()
            << "Query compilation, unexpected table id"
            << ", table: " << meta.Name
            << ", expected: " << meta.PathId.ToString()
            << ", actual: " << pathId->Content()));
        return TStatus::Error;
    }

    if (meta.SchemaVersion != FromString<ui64>(version->Content())) {
        ctx.AddError(YqlIssue(ctx.GetPosition(node->Pos()), TIssuesIds::UNEXPECTED, TStringBuilder()
            << "Query compilation, unexpected table version"
            << ", table: " << meta.Name
            << ", expected: " << meta.SchemaVersion
            << ", actual: " << version->Content()));
        return TStatus::Error;
    }

    node->SetTypeAnn(ctx.MakeType<TVoidExprType>());
    return TStatus::Ok;
}

std::pair<TString, const TKikimrTableDescription*> ResolveTable(const TExprNode* kqpTableNode, TExprContext& ctx,
    const TString& cluster, const TKikimrTablesData& tablesData)
{
    if (!EnsureCallable(*kqpTableNode, ctx)) {
        return {"", nullptr};
    }

    if (!TKqpTable::Match(kqpTableNode)) {
        ctx.AddError(TIssue(ctx.GetPosition(kqpTableNode->Pos()), TStringBuilder()
            << "Expected " << TKqpTable::CallableName()));
        return {"", nullptr};
    }

    TString tableName{kqpTableNode->Child(TKqpTable::idx_Path)->Content()};

    auto tableDesc = tablesData.EnsureTableExists(cluster, tableName, kqpTableNode->Pos(), ctx);
    return {std::move(tableName), tableDesc};
}

const TFlowExprType* GetWideRowsType(TExprContext& ctx, const TStructExprType* rowType) {
    YQL_ENSURE(rowType);

    const auto& columns = rowType->GetItems();

    TTypeAnnotationNode::TListType items;
    items.reserve(columns.size());

    for (const auto& column: columns) {
        items.push_back(column->GetItemType());
    }

    auto wideRowType = ctx.MakeType<TMultiExprType>(items);
    return ctx.MakeType<TFlowExprType>(wideRowType);
}

bool CalcKeyColumnsCount(TExprContext& ctx, const TPositionHandle pos, const TStructExprType& structType,
    const TKikimrTableDescription& tableDesc, const TKikimrTableMetadata& metadata, ui32& keyColumnsCount)
{
    for (auto& keyColumnName : metadata.KeyColumnNames) {
        auto itemIndex = structType.FindItem(keyColumnName);
        if (!itemIndex) {
            break;
        }

        auto itemType = structType.GetItems()[*itemIndex]->GetItemType();
        auto keyColumnType = tableDesc.GetColumnType(keyColumnName);

        if (CanCompare<true>(itemType, keyColumnType) == ECompareOptions::Uncomparable) {
            ctx.AddError(TIssue(ctx.GetPosition(pos), TStringBuilder()
                << "Invalid column type in table lookup, column: " <<  keyColumnName
                << ", expected: " << FormatType(keyColumnType)
                << ", actual: " << FormatType(itemType)));
            return false;
        }

        ++keyColumnsCount;
    }
    return true;
}

TStatus AnnotateReadTable(const TExprNode::TPtr& node, TExprContext& ctx, const TString& cluster,
    const TKikimrTablesData& tablesData, bool withSystemColumns)
{
    const bool readIndex = TKqlReadTableIndex::Match(node.Get());
    if (readIndex && !EnsureArgsCount(*node, 5, ctx)) {
        return TStatus::Error;
    }

    if (!readIndex && !EnsureArgsCount(*node, 4, ctx)) {
        return TStatus::Error;
    }

    auto table = ResolveTable(node->Child(TKqlReadTableBase::idx_Table), ctx, cluster, tablesData);
    if (!table.second) {
        return TStatus::Error;
    }

    YQL_ENSURE(table.second->Metadata, "Expected loaded metadata");

    TKikimrTableMetadataPtr meta;

    if (readIndex) {
        meta = table.second->Metadata->GetIndexMetadata(TString(node->Child(TKqlReadTableIndex::idx_Index)->Content())).first;
        if (!meta) {
            return TStatus::Error;
        }
    } else {
        meta = table.second->Metadata;
    }

    const auto& columns = node->ChildPtr(TKqlReadTableBase::idx_Columns);
    if (!EnsureTupleOfAtoms(*columns, ctx)) {
        return TStatus::Error;
    }

    auto rowType = GetReadTableRowType(ctx, tablesData, cluster, table.first, TCoAtomList(columns), withSystemColumns);
    if (!rowType) {
        return TStatus::Error;
    }

    if (!TKqlKeyRange::Match(node->Child(TKqlReadTableBase::idx_Range))) {
        ctx.AddError(TIssue(ctx.GetPosition(node->Child(TKqlReadTableBase::idx_Range)->Pos()), "Expected TKqlKeyRange tuple"));
        return TStatus::Error;
    }

    TKqlKeyRange range{node->ChildPtr(TKqlReadTableBase::idx_Range)};

    if (!CheckKeyTuple(range.From(), *table.second, meta, ctx)) {
        return TStatus::Error;
    }

    if (!CheckKeyTuple(range.To(), *table.second, meta, ctx)) {
        return TStatus::Error;
    }

    if (TKqlReadTable::Match(node.Get()) || TKqlReadTableIndex::Match(node.Get())) {
        node->SetTypeAnn(ctx.MakeType<TListExprType>(rowType));
    } else if (TKqpReadTable::Match(node.Get())) {
        node->SetTypeAnn(ctx.MakeType<TFlowExprType>(rowType));
    } else if (TKqpWideReadTable::Match(node.Get())) {
        node->SetTypeAnn(GetWideRowsType(ctx, rowType->Cast<TStructExprType>()));
    } else {
        YQL_ENSURE(false, "Unexpected ReadTable callable: " << node->Content());
    }

    return TStatus::Ok;
}
TStatus AnnotateKqpSourceSettings(const TExprNode::TPtr& node, TExprContext& ctx, const TString& cluster,
    const TKikimrTablesData& tablesData, bool withSystemColumns)
{
    auto table = ResolveTable(node->Child(TKqpReadRangesSourceSettings::idx_Table), ctx, cluster, tablesData);
    if (!table.second) {
        return TStatus::Error;
    }

    const auto& columns = node->ChildPtr(TKqpReadRangesSourceSettings::idx_Columns);
    if (!EnsureTupleOfAtoms(*columns, ctx)) {
        return TStatus::Error;
    }

    auto rowType = GetReadTableRowType(ctx, tablesData, cluster, table.first, TCoAtomList(columns), withSystemColumns);
    if (!rowType) {
        return TStatus::Error;
    }

    auto ranges = node->Child(TKqpReadRangesSourceSettings::idx_RangesExpr);
    if (!TCoVoid::Match(ranges) &&
        !TCoArgument::Match(ranges) &&
        !TCoParameter::Match(ranges) &&
        !TCoRangeFinalize::Match(ranges) &&
        !TDqPhyPrecompute::Match(ranges) &&
        !TKqpTxResultBinding::Match(ranges) &&
        !TKqlKeyRange::Match(ranges))
    {
        ctx.AddError(TIssue(
            ctx.GetPosition(ranges->Pos()),
            TStringBuilder()
                << "Expected KeyRange, Void, Parameter, Argument or RangeFinalize in ranges, but got: "
                << ranges->Content()
        ));
        return TStatus::Error;
    }

    node->SetTypeAnn(ctx.MakeType<TStreamExprType>(rowType));
    return TStatus::Ok;
}

TStatus AnnotateReadTableRanges(const TExprNode::TPtr& node, TExprContext& ctx, const TString& cluster,
    const TKikimrTablesData& tablesData, bool withSystemColumns)
{
    bool olapTable = TKqpReadOlapTableRangesBase::Match(node.Get());

    if ((olapTable && !EnsureArgsCount(*node, 6, ctx)) || (!olapTable && !EnsureArgsCount(*node, 5, ctx))) {
        return TStatus::Error;
    }

    auto table = ResolveTable(node->Child(TKqlReadTableRangesBase::idx_Table), ctx, cluster, tablesData);
    if (!table.second) {
        return TStatus::Error;
    }

    const auto& columns = node->ChildPtr(TKqlReadTableRangesBase::idx_Columns);
    if (!EnsureTupleOfAtoms(*columns, ctx)) {
        return TStatus::Error;
    }

    auto rowType = GetReadTableRowType(ctx, tablesData, cluster, table.first, TCoAtomList(columns), withSystemColumns);
    if (!rowType) {
        return TStatus::Error;
    }

    auto ranges = node->Child(TKqlReadTableRangesBase::idx_Ranges);
    if (!TCoVoid::Match(ranges) &&
        !TCoArgument::Match(ranges) &&
        !TCoParameter::Match(ranges) &&
        !TCoRangeFinalize::Match(ranges))
    {
        ctx.AddError(TIssue(
            ctx.GetPosition(ranges->Pos()),
            TStringBuilder()
                << "Expected Void, Parameter, Argument or RangeFinalize in ranges, but got: "
                << ranges->Content()
        ));
        return TStatus::Error;
    }

    if (TKqlReadTableRanges::Match(node.Get())) {
        node->SetTypeAnn(ctx.MakeType<TListExprType>(rowType));
    } else if (TKqpReadTableRanges::Match(node.Get())) {
        node->SetTypeAnn(ctx.MakeType<TFlowExprType>(rowType));
    } else if (TKqpWideReadTableRanges::Match(node.Get())) {
        node->SetTypeAnn(GetWideRowsType(ctx, rowType->Cast<TStructExprType>()));
    } else if (TKqpReadOlapTableRangesBase::Match(node.Get())) {
        if (!EnsureLambda(*node->Child(TKqpReadOlapTableRangesBase::idx_Process), ctx)) {
            return TStatus::Error;
        }

        auto& processLambda = node->ChildRef(TKqpReadOlapTableRangesBase::idx_Process);
        if (!UpdateLambdaAllArgumentsTypes(processLambda, {ctx.MakeType<TFlowExprType>(rowType)}, ctx)) {
            return IGraphTransformer::TStatus::Error;
        }

        if (!processLambda->GetTypeAnn()) {
            return IGraphTransformer::TStatus::Repeat;
        }

        auto processType = processLambda->GetTypeAnn();
        const TTypeAnnotationNode* processRowType;
        if (!EnsureNewSeqType<false, false, true>(node->Pos(), *processType, ctx, &processRowType)) {
            return TStatus::Error;
        }

        if (!EnsureStructType(node->Pos(), *processRowType, ctx)) {
            return IGraphTransformer::TStatus::Error;
        }

        if (TKqpReadOlapTableRanges::Match(node.Get())) {
            node->SetTypeAnn(ctx.MakeType<TFlowExprType>(processRowType));
        } else if (TKqpWideReadOlapTableRanges::Match(node.Get())) {
            node->SetTypeAnn(GetWideRowsType(ctx, processRowType->Cast<TStructExprType>()));
        } else {
            YQL_ENSURE(false, "Unexpected ReadOlapTable callable." << node->Content());
        }
    } else {
        YQL_ENSURE(false, "Unexpected ReadTableRanges callable." << node->Content());
    }

    return TStatus::Ok;
}

TStatus AnnotateLookupTable(const TExprNode::TPtr& node, TExprContext& ctx, const TString& cluster,
    const TKikimrTablesData& tablesData, bool withSystemColumns)
{
    if (!EnsureArgsCount(*node, TKqlLookupIndexBase::Match(node.Get()) ? 4 : 3, ctx)) {
        return TStatus::Error;
    }

    auto table = ResolveTable(node->Child(TKqlLookupTableBase::idx_Table), ctx, cluster, tablesData);
    if (!table.second) {
        return TStatus::Error;
    }

    if (!EnsureTupleOfAtoms(*node->Child(TKqlLookupTableBase::idx_Columns), ctx)) {
        return TStatus::Error;
    }
    TCoAtomList columns{node->ChildPtr(TKqlLookupTableBase::idx_Columns)};

    auto rowType = GetReadTableRowType(ctx, tablesData, cluster, table.first, columns, withSystemColumns);
    if (!rowType) {
        return TStatus::Error;
    }

    bool isPhysical = TKqpLookupTable::Match(node.Get());

    const TTypeAnnotationNode* lookupType;
    if (isPhysical) {
        if (!EnsureNewSeqType<false, false, true>(*node->Child(TKqlLookupTableBase::idx_LookupKeys), ctx, &lookupType)) {
            return TStatus::Error;
        }

        if (node->Child(TKqlLookupTableBase::idx_LookupKeys)->GetTypeAnn()->GetKind() == ETypeAnnotationKind::Flow) {
            auto streamLookupKeys = Build<TCoFromFlow>(ctx, node->Pos())
                .Input(node->ChildRef(TKqlLookupTableBase::idx_LookupKeys))
                .Done();

            node->ChildRef(TKqpLookupTable::idx_LookupKeys) = streamLookupKeys.Ptr();
            return TStatus::Repeat;
        }
    } else {
        if (!EnsureNewSeqType<false, true, false>(*node->Child(TKqlLookupTableBase::idx_LookupKeys), ctx, &lookupType)) {
            return TStatus::Error;
        }
    }

    YQL_ENSURE(lookupType);
    if (!EnsureStructType(node->Pos(), *lookupType, ctx)) {
        return TStatus::Error;
    }
    auto structType = lookupType->Cast<TStructExprType>();

    ui32 keyColumnsCount = 0;
    if (TKqlLookupIndexBase::Match(node.Get())) {
        auto index = node->Child(TKqlLookupIndexBase::idx_Index);
        if (!EnsureAtom(*index, ctx)) {
            return TStatus::Error;
        }
        auto indexMeta = table.second->Metadata->GetIndexMetadata(TString(index->Content())).first;

        if (!CalcKeyColumnsCount(ctx, node->Pos(), *structType, *table.second, *indexMeta, keyColumnsCount)) {
            return TStatus::Error;
        }

    } else {
        if (!CalcKeyColumnsCount(ctx, node->Pos(), *structType, *table.second, *table.second->Metadata, keyColumnsCount)) {
            return TStatus::Error;
        }
    }

    if (!keyColumnsCount) {
        ctx.AddError(TIssue(ctx.GetPosition(node->Pos()), "Table lookup has no key columns."));
        return TStatus::Error;
    }

    if (structType->GetSize() != keyColumnsCount) {
        ctx.AddError(TIssue(ctx.GetPosition(node->Pos()), "Table lookup contains non-key columns."));
        return TStatus::Error;
    }

    if (isPhysical) {
        node->SetTypeAnn(ctx.MakeType<TFlowExprType>(rowType));
    } else {
        node->SetTypeAnn(ctx.MakeType<TListExprType>(rowType));
    }
    return TStatus::Ok;
}

TStatus AnnotateKeyTuple(const TExprNode::TPtr& node, TExprContext& ctx) {
    TVector<const TTypeAnnotationNode*> keyTypes;
    for (const auto& arg : node->ChildrenList()) {
        keyTypes.push_back(arg->GetTypeAnn());
    }

    auto tupleType = ctx.MakeType<TTupleExprType>(keyTypes);
    node->SetTypeAnn(tupleType);
    return TStatus::Ok;
}

TStatus AnnotateUpsertRows(const TExprNode::TPtr& node, TExprContext& ctx, const TString& cluster,
    const TKikimrTablesData& tablesData)
{
    if (!EnsureMinArgsCount(*node, 3, ctx)) {
        return TStatus::Error;
    }

    auto table = ResolveTable(node->Child(TKqlUpsertRowsBase::idx_Table), ctx, cluster, tablesData);
    if (!table.second) {
        return TStatus::Error;
    }

    if (!EnsureTupleOfAtoms(*node->Child(TKqlUpsertRowsBase::idx_Columns), ctx)) {
        return TStatus::Error;
    }
    TCoAtomList columns{node->ChildPtr(TKqlUpsertRowsBase::idx_Columns)};

    const TTypeAnnotationNode* itemType = nullptr;
    bool isStream;

    auto* input = node->Child(TKqlUpsertRowsBase::idx_Input);

    if (TKqpUpsertRows::Match(node.Get())) {
        if (!EnsureStreamType(*input, ctx)) {
            return TStatus::Error;
        }
        itemType = input->GetTypeAnn()->Cast<TStreamExprType>()->GetItemType();
        isStream = true;
    } else {
        YQL_ENSURE(TKqlUpsertRows::Match(node.Get()) || TKqlUpsertRowsIndex::Match(node.Get()));

        if (!EnsureListType(*input, ctx)) {
            return TStatus::Error;
        }
        itemType = input->GetTypeAnn()->Cast<TListExprType>()->GetItemType();
        isStream = false;
    }

    if (!EnsureStructType(input->Pos(), *itemType, ctx)) {
        return TStatus::Error;
    }

    auto rowType = itemType->Cast<TStructExprType>();
    for (const auto& column : columns) {
        if (!rowType->FindItem(column.Value())) {
            ctx.AddError(TIssue(ctx.GetPosition(node->Pos()), TStringBuilder()
                << "Missing column in input type: " << column.Value()));
            return TStatus::Error;
        }
    }

    if (TKqpUpsertRows::Match(node.Get()) && rowType->GetItems().size() != columns.Size()) {
        ctx.AddError(TIssue(ctx.GetPosition(node->Pos()), TStringBuilder()
            << "Input type contains excess columns"));
        return TStatus::Error;
    }

    for (auto& keyColumnName : table.second->Metadata->KeyColumnNames) {
        if (!rowType->FindItem(keyColumnName)) {
            ctx.AddError(YqlIssue(ctx.GetPosition(node->Pos()), TIssuesIds::KIKIMR_PRECONDITION_FAILED, TStringBuilder()
                << "Missing key column in input type: " << keyColumnName));
            return TStatus::Error;
        }
    }

    for (auto& [name, meta] : table.second->Metadata->Columns) {
        if (meta.NotNull && !rowType->FindItem(name)) {
            ctx.AddError(YqlIssue(ctx.GetPosition(node->Pos()), TIssuesIds::KIKIMR_NO_COLUMN_DEFAULT_VALUE, TStringBuilder()
                << "Missing not null column in input: " << name
                << ". All not null columns should be initialized"));
            return TStatus::Error;
        }

        if (meta.NotNull && rowType->FindItemType(name)->HasOptionalOrNull()) {
            ctx.AddError(YqlIssue(ctx.GetPosition(node->Pos()), TIssuesIds::KIKIMR_BAD_COLUMN_TYPE, TStringBuilder()
                << "Can't set optional or NULL value to not null column: " << name
                << ". All not null columns should be initialized"));
            return TStatus::Error;
        }
    }

    if (TKqlUpsertRowsIndex::Match(node.Get())) {
        Y_ENSURE(!table.second->Metadata->SecondaryGlobalIndexMetadata.empty());
    }

    auto effectType = MakeKqpEffectType(ctx);
    if (isStream) {
        node->SetTypeAnn(ctx.MakeType<TStreamExprType>(effectType));
    } else {
        node->SetTypeAnn(ctx.MakeType<TListExprType>(effectType));
    }
    return TStatus::Ok;
}

TStatus AnnotateInsertRows(const TExprNode::TPtr& node, TExprContext& ctx, const TString& cluster,
    const TKikimrTablesData& tablesData)
{
    if (!EnsureArgsCount(*node, 4, ctx)) {
        return TStatus::Error;
    }

    auto table = ResolveTable(node->Child(TKqlInsertRows::idx_Table), ctx, cluster, tablesData);
    if (!table.second) {
        return TStatus::Error;
    }

    if (!EnsureTupleOfAtoms(*node->Child(TKqlInsertRows::idx_Columns), ctx)) {
        return TStatus::Error;
    }
    TCoAtomList columns{node->ChildPtr(TKqlInsertRows::idx_Columns)};

    auto* input = node->Child(TKqlInsertRows::idx_Input);
    if (!EnsureListType(*input, ctx)) {
        return TStatus::Error;
    }

    auto itemType = input->GetTypeAnn()->Cast<TListExprType>()->GetItemType();
    if (!EnsureStructType(input->Pos(), *itemType, ctx)) {
        return TStatus::Error;
    }

    auto rowType = itemType->Cast<TStructExprType>();
    for (const auto& column : columns) {
        if (!rowType->FindItem(column.Value())) {
            ctx.AddError(TIssue(ctx.GetPosition(node->Pos()), TStringBuilder()
                << "Missing column in input type: " << column.Value()));
            return TStatus::Error;
        }
    }

    for (auto& keyColumnName : table.second->Metadata->KeyColumnNames) {
        if (!rowType->FindItem(keyColumnName)) {
            ctx.AddError(YqlIssue(ctx.GetPosition(node->Pos()), TIssuesIds::KIKIMR_PRECONDITION_FAILED, TStringBuilder()
                << "Missing key column in input type: " << keyColumnName));
            return TStatus::Error;
        }
    }

    for (auto& [name, meta] : table.second->Metadata->Columns) {
        if (meta.NotNull && !rowType->FindItem(name)) {
            ctx.AddError(YqlIssue(ctx.GetPosition(node->Pos()), TIssuesIds::KIKIMR_NO_COLUMN_DEFAULT_VALUE, TStringBuilder()
                << "Missing not null column in input: " << name
                << ". All not null columns should be initialized"));
            return TStatus::Error;
        }

        if (meta.NotNull && rowType->FindItemType(name)->HasOptionalOrNull()) {
            ctx.AddError(YqlIssue(ctx.GetPosition(node->Pos()), TIssuesIds::KIKIMR_BAD_COLUMN_TYPE, TStringBuilder()
                << "Can't set optional or NULL value to not null column: " << name
                << ". All not null columns should be initialized"));
            return TStatus::Error;
        }
    }

    if (!EnsureAtom(*node->Child(TKqlInsertRows::idx_OnConflict), ctx)) {
        return TStatus::Error;
    }

    TStringBuf onConflict = node->Child(TKqlInsertRows::idx_OnConflict)->Content();
    if (onConflict != "abort"sv && onConflict != "revert"sv) {
        ctx.AddError(TIssue(ctx.GetPosition(node->Pos()), TStringBuilder()
            << "Unsupported insert on-conflict policy `" << onConflict << "`."));
        return TStatus::Error;
    }

    node->SetTypeAnn(ctx.MakeType<TListExprType>(MakeKqpEffectType(ctx)));
    return TStatus::Ok;
}

TStatus AnnotateUpdateRows(const TExprNode::TPtr& node, TExprContext& ctx, const TString& cluster,
    const TKikimrTablesData& tablesData)
{
    if (!EnsureArgsCount(*node, 3, ctx)) {
        return TStatus::Error;
    }

    auto table = ResolveTable(node->Child(TKqlUpdateRows::idx_Table), ctx, cluster, tablesData);
    if (!table.second) {
        return TStatus::Error;
    }

    if (!EnsureTupleOfAtoms(*node->Child(TKqlUpdateRows::idx_Columns), ctx)) {
        return TStatus::Error;
    }
    TCoAtomList columns{node->ChildPtr(TKqlUpdateRows::idx_Columns)};

    auto* input = node->Child(TKqlUpdateRows::idx_Input);
    if (!EnsureListType(*input, ctx)) {
        return TStatus::Error;
    }

    auto itemType = input->GetTypeAnn()->Cast<TListExprType>()->GetItemType();
    if (!EnsureStructType(input->Pos(), *itemType, ctx)) {
        return TStatus::Error;
    }

    auto rowType = itemType->Cast<TStructExprType>();
    for (const auto& column : columns) {
        YQL_ENSURE(rowType->FindItem(column.Value()), "Missing column in input type: " << column.Value());
    }

    for (auto& keyColumnName : table.second->Metadata->KeyColumnNames) {
        if (!rowType->FindItem(keyColumnName)) {
            ctx.AddError(YqlIssue(ctx.GetPosition(node->Pos()), TIssuesIds::KIKIMR_NO_COLUMN_DEFAULT_VALUE, TStringBuilder()
                << "Missing key column in input type: " << keyColumnName));
            return TStatus::Error;
        }
    }

    for (const auto& item : rowType->GetItems()) {
        auto column = table.second->Metadata->Columns.FindPtr(TString(item->GetName()));
        YQL_ENSURE(column);
        if (column->NotNull && item->HasOptionalOrNull()) {
            ctx.AddError(YqlIssue(ctx.GetPosition(node->Pos()), TIssuesIds::KIKIMR_BAD_COLUMN_TYPE, TStringBuilder()
                << "Can't set optional or NULL value to not null column: " << column->Name));
            return TStatus::Error;
        }
    }

    node->SetTypeAnn(ctx.MakeType<TListExprType>(MakeKqpEffectType(ctx)));
    return TStatus::Ok;
}

TStatus AnnotateDeleteRows(const TExprNode::TPtr& node, TExprContext& ctx, const TString& cluster,
    const TKikimrTablesData& tablesData)
{
    if (!EnsureArgsCount(*node, 2, ctx)) {
        return TStatus::Error;
    }

    auto table = ResolveTable(node->Child(TKqlDeleteRowsBase::idx_Table), ctx, cluster, tablesData);
    if (!table.second) {
        return TStatus::Error;
    }

    const TTypeAnnotationNode* itemType = nullptr;
    bool isStream = false;

    auto* input = node->Child(TKqlDeleteRowsBase::idx_Input);

    if (TKqpDeleteRows::Match(node.Get())) {
        if (!EnsureStreamType(*input, ctx)) {
            return TStatus::Error;
        }
        itemType = input->GetTypeAnn()->Cast<TStreamExprType>()->GetItemType();
        isStream = true;
    } else {
        YQL_ENSURE(TKqlDeleteRows::Match(node.Get()) || TKqlDeleteRowsIndex::Match(node.Get()));
        if (!EnsureListType(*input, ctx)) {
            return TStatus::Error;
        }
        itemType = input->GetTypeAnn()->Cast<TListExprType>()->GetItemType();
        isStream = false;
    }

    if (!EnsureStructType(input->Pos(), *itemType, ctx)) {
        return TStatus::Error;
    }

    auto rowType = itemType->Cast<TStructExprType>();
    for (auto& keyColumnName : table.second->Metadata->KeyColumnNames) {
        if (!rowType->FindItem(keyColumnName)) {
            ctx.AddError(YqlIssue(ctx.GetPosition(node->Pos()), TIssuesIds::KIKIMR_PRECONDITION_FAILED, TStringBuilder()
                << "Missing key column in input type: " << keyColumnName));
            return TStatus::Error;
        }
    }

    if (rowType->GetItems().size() != table.second->Metadata->KeyColumnNames.size()) {
        ctx.AddError(TIssue(ctx.GetPosition(node->Pos()), "Input type contains non-key columns"));
        return TStatus::Error;
    }

    auto effectType = MakeKqpEffectType(ctx);
    if (isStream) {
        node->SetTypeAnn(ctx.MakeType<TStreamExprType>(effectType));
    } else {
        node->SetTypeAnn(ctx.MakeType<TListExprType>(effectType));
    }
    return TStatus::Ok;
}

TStatus AnnotateOlapUnaryLogicOperator(const TExprNode::TPtr& node, TExprContext& ctx) {
    if (!EnsureArgsCount(*node, 1, ctx)) {
        return TStatus::Error;
    }

    node->SetTypeAnn(ctx.MakeType<TUnitExprType>());
    return TStatus::Ok;
}

TStatus AnnotateOlapBinaryLogicOperator(const TExprNode::TPtr& node, TExprContext& ctx) {
    if (!EnsureMinArgsCount(*node, 1, ctx)) {
        return TStatus::Error;
    }

    node->SetTypeAnn(ctx.MakeType<TUnitExprType>());
    return TStatus::Ok;
}

bool ValidateOlapFilterConditions(const TExprNode* node, const TStructExprType* itemType, TExprContext& ctx) {
    YQL_ENSURE(itemType);

    if (TKqpOlapAnd::Match(node) || TKqpOlapOr::Match(node) || TKqpOlapXor::Match(node) || TKqpOlapNot::Match(node)) {
        bool res = true;
        for (auto arg : node->ChildrenList()) {
            res &= ValidateOlapFilterConditions(arg.Get(), itemType, ctx);
            if (!res) {
                break;
            }
        }
        return res;
    } else if (TKqpOlapFilterCompare::Match(node)) {
        if (!EnsureArgsCount(*node, 3, ctx)) {
            return false;
        }
        auto op = node->Child(TKqpOlapFilterCompare::idx_Operator);
        if (!EnsureAtom(*op, ctx)) {
            ctx.AddError(TIssue(ctx.GetPosition(node->Pos()),
                TStringBuilder() << "Expected string as operator in OLAP comparison filter, got: " << op->Content()
            ));
            return false;
        }
        auto opStr = op->Content();
        if (opStr != "eq"sv && opStr != "neq"sv && opStr != "lt"sv && opStr != "lte"sv && opStr != "gt"sv && opStr != "gte"sv) {
            ctx.AddError(TIssue(ctx.GetPosition(node->Pos()),
                TStringBuilder() << "Expected one of eq/neq/lt/lte/gt/gte operators in OLAP comparison filter, got: " << op->Content()
            ));
            return false;
        }
        return ValidateOlapFilterConditions(node->Child(TKqpOlapFilterCompare::idx_Left), itemType, ctx)
            && ValidateOlapFilterConditions(node->Child(TKqpOlapFilterCompare::idx_Right), itemType, ctx);
    } else if (TKqpOlapFilterExists::Match(node)) {
        if (!EnsureArgsCount(*node, 1, ctx)) {
            return false;
        }
        auto column = node->Child(TKqpOlapFilterExists::idx_Column);
        if (!EnsureAtom(*column, ctx)) {
            ctx.AddError(TIssue(ctx.GetPosition(node->Pos()),
                TStringBuilder() << "Expected column in OLAP Exists filter, got: " << column->Content()
            ));
            return false;
        }
        return ValidateOlapFilterConditions(column, itemType, ctx);
    }

    // Column name, validate that it is present in Input node
    if (TCoAtom::Match(node)) {
        if (itemType->FindItem(node->Content())) {
            return true;
        }

        ctx.AddError(TIssue(ctx.GetPosition(node->Pos()),
            TStringBuilder() << "Missing column in input type: " << node->Content()
        ));

        return false;
    }

    // Null argument for IS NULL/NOT NULL
    if (TCoNull::Match(node)) {
        return true;
    }

    // Incoming parameter
    if (TCoParameter::Match(node)) {
        return true;
    }

    // Any supported literal
    if (TCoDataCtor::Match(node)) {
        return true;
    }

    // SafeCast, the checks about validity should be placed in kqp_opt_phy_olap_filter.cpp
    if (TCoSafeCast::Match(node)) {
        return true;
    }

    ctx.AddError(TIssue(ctx.GetPosition(node->Pos()),
        TStringBuilder() << "Expected literal or column as OLAP filter value, got: " << node->Content()
    ));

    return false;
}

TStatus AnnotateOlapFilter(const TExprNode::TPtr& node, TExprContext& ctx) {
    if (!EnsureArgsCount(*node, 2, ctx)) {
        return TStatus::Error;
    }

    auto* input = node->Child(TKqpOlapFilter::idx_Input);

    const TTypeAnnotationNode* itemType;
    if (!EnsureNewSeqType<false, false, true>(*input, ctx, &itemType)) {
        return TStatus::Error;
    }

    if (!EnsureStructType(input->Pos(), *itemType, ctx)) {
        return TStatus::Error;
    }

    if (!ValidateOlapFilterConditions(node->Child(TKqpOlapFilter::idx_Condition), itemType->Cast<TStructExprType>(), ctx)) {
        return TStatus::Error;
    }

    node->SetTypeAnn(input->GetTypeAnn());
    return TStatus::Ok;
}

TStatus AnnotateOlapAgg(const TExprNode::TPtr& node, TExprContext& ctx) {
    if (!EnsureArgsCount(*node, 3, ctx)) {
        return TStatus::Error;
    }

    auto* input = node->Child(TKqpOlapAgg::idx_Input);

    const TTypeAnnotationNode* itemType;
    if (!EnsureNewSeqType<false, false, true>(*input, ctx, &itemType)) {
        return TStatus::Error;
    }

    if (!EnsureStructType(input->Pos(), *itemType, ctx)) {
        return TStatus::Error;
    }

    auto structType = itemType->Cast<TStructExprType>();

    if (!EnsureTuple(*node->Child(TKqpOlapAgg::idx_Aggregates), ctx)) {
        return TStatus::Error;
    }

    TVector<const TItemExprType*> aggTypes;
    for (auto agg : node->Child(TKqpOlapAgg::idx_Aggregates)->ChildrenList()) {
        auto aggName = agg->Child(TKqpOlapAggOperation::idx_Name);
        auto opType = agg->Child(TKqpOlapAggOperation::idx_Type);
        auto colName = agg->Child(TKqpOlapAggOperation::idx_Column);
        if (!EnsureAtom(*opType, ctx)) {
            ctx.AddError(TIssue(
                ctx.GetPosition(node->Pos()),
                TStringBuilder() << "Expected operation type in OLAP aggregation, got: " << opType->Content()
            ));
            return TStatus::Error;
        }
        if (!EnsureAtom(*colName, ctx)) {
            ctx.AddError(TIssue(
                ctx.GetPosition(node->Pos()),
                TStringBuilder() << "Expected column name in OLAP aggregation, got: " << colName->Content()
            ));
            return TStatus::Error;
        }
        if (!EnsureAtom(*aggName, ctx)) {
            ctx.AddError(TIssue(
                ctx.GetPosition(node->Pos()),
                TStringBuilder() << "Expected aggregate column generated name in OLAP aggregation, got: " << aggName->Content()
            ));
            return TStatus::Error;
        }
        if (opType->Content() == "count") {
            aggTypes.push_back(ctx.MakeType<TItemExprType>(aggName->Content(), ctx.MakeType<TDataExprType>(EDataSlot::Uint64)));
        } else if (opType->Content() == "sum") {
            auto colType = structType->FindItemType(colName->Content());
            const TTypeAnnotationNode* resultType = nullptr;
            if(!GetSumResultType(node->Pos(), *colType, resultType, ctx)) {
                ctx.AddError(TIssue(ctx.GetPosition(node->Pos()),
                    TStringBuilder() << "Unsupported type: " << FormatType(colType) << ". Expected Data or Optional of Data or Null."));
                return TStatus::Error;
            }
            aggTypes.push_back(ctx.MakeType<TItemExprType>(aggName->Content(), resultType));
        } else if (opType->Content() == "min" || opType->Content() == "max") {
            auto colType = structType->FindItemType(colName->Content());
            aggTypes.push_back(ctx.MakeType<TItemExprType>(aggName->Content(), colType));
        } else {
            ctx.AddError(TIssue(
                ctx.GetPosition(node->Pos()),
                TStringBuilder() << "Unsupported operation type in OLAP aggregation, got: " << opType->Content()
            ));
            return TStatus::Error;
        }
    }

    if (!EnsureTuple(*node->Child(TKqpOlapAgg::idx_KeyColumns), ctx)) {
        return TStatus::Error;
    }
    for (auto keyCol : node->Child(TKqpOlapAgg::idx_KeyColumns)->ChildrenList()) {
        if (!EnsureAtom(*keyCol, ctx)) {
            ctx.AddError(TIssue(
                ctx.GetPosition(node->Pos()),
                TStringBuilder() << "Expected column name in OLAP key columns, got: " << keyCol->Content()
            ));
            return TStatus::Error;
        }
        aggTypes.push_back(ctx.MakeType<TItemExprType>(keyCol->Content(), structType->FindItemType(keyCol->Content())));
    }

    node->SetTypeAnn(MakeSequenceType(input->GetTypeAnn()->GetKind(), *ctx.MakeType<TStructExprType>(aggTypes), ctx));
    return TStatus::Ok;
}


TStatus AnnotateOlapExtractMembers(const TExprNode::TPtr& node, TExprContext& ctx) {
    if (!EnsureArgsCount(*node, 2, ctx)) {
        return IGraphTransformer::TStatus::Error;
    }

    const TTypeAnnotationNode* nodeItemType = nullptr;
    if (!EnsureNewSeqType<true>(node->Head(), ctx, &nodeItemType)) {
        return IGraphTransformer::TStatus::Error;
    }

    if (!EnsureStructType(node->Head().Pos(), *nodeItemType, ctx)) {
        return IGraphTransformer::TStatus::Error;
    }

    const auto structType = nodeItemType->Cast<TStructExprType>();
    TVector<const TItemExprType*> resItems;
    for (auto& x : node->Tail().Children()) {
        YQL_ENSURE(x->IsAtom());
        auto pos = NYql::NTypeAnnImpl::FindOrReportMissingMember(x->Content(), node->Head().Pos(), *structType, ctx);
        if (!pos) {
            return IGraphTransformer::TStatus::Error;
        }

        resItems.push_back(structType->GetItems()[*pos]);
    }

    const auto resItemType = ctx.MakeType<TStructExprType>(resItems);
    if (!resItemType->Validate(node->Pos(), ctx)) {
        return IGraphTransformer::TStatus::Error;
    }

    node->SetTypeAnn(MakeSequenceType(node->Head().GetTypeAnn()->GetKind(), *resItemType, ctx));
    return IGraphTransformer::TStatus::Ok;
}

TStatus AnnotateKqpTxInternalBinding(const TExprNode::TPtr& node, TExprContext& ctx) {
    if (!EnsureArgsCount(*node, 2, ctx)) {
        return TStatus::Error;
    }

    auto* kind = node->Child(TKqpTxInternalBinding::idx_Kind);
    if (!EnsureAtom(*kind, ctx)) {
        return TStatus::Error;
    }

    if (kind->IsAtom("Unspecified")) {
        ctx.AddError(TIssue(ctx.GetPosition(node->Pos()), "Unspecified internal binding kind"));
        return TStatus::Error;
    }

    auto* type = node->Child(TKqpTxInternalBinding::idx_Type);
    if (!EnsureType(*type, ctx)) {
        return TStatus::Error;
    }

    node->SetTypeAnn(type->GetTypeAnn()->Cast<TTypeExprType>()->GetType());
    return TStatus::Ok;
}

TStatus AnnotateKqpTxResultBinding(const TExprNode::TPtr& node, TExprContext& ctx) {
    if (!EnsureArgsCount(*node, 3, ctx)) {
        return TStatus::Error;
    }

    auto* txIndex = node->Child(TKqpTxResultBinding::idx_TxIndex);
    auto* txResultIndex = node->Child(TKqpTxResultBinding::idx_ResultIndex);
    if (!EnsureAtom(*txIndex, ctx) || !EnsureAtom(*txResultIndex, ctx)) {
        return TStatus::Error;
    }

    auto* type = node->Child(TKqpTxResultBinding::idx_Type);
    if (!EnsureType(*type, ctx)) {
        return TStatus::Error;
    }

    node->SetTypeAnn(type->GetTypeAnn()->Cast<TTypeExprType>()->GetType());
    return TStatus::Ok;
}

TStatus AnnotateKqpPhysicalTx(const TExprNode::TPtr& node, TExprContext& ctx) {
    if (!EnsureArgsCount(*node, 4, ctx)) {
        return TStatus::Error;
    }

    // TODO: ???

    node->SetTypeAnn(ctx.MakeType<TVoidExprType>());
    return TStatus::Ok;
}

TStatus AnnotateKqpPhysicalQuery(const TExprNode::TPtr& node, TExprContext& ctx) {
    if (!EnsureArgsCount(*node, 3, ctx)) {
        return TStatus::Error;
    }

    // TODO: ???

    node->SetTypeAnn(ctx.MakeType<TVoidExprType>());
    return TStatus::Ok;
}

TStatus AnnotateKqpEffects(const TExprNode::TPtr& node, TExprContext& ctx) {
    auto kqpEffectType = MakeKqpEffectType(ctx);

    for (const auto& arg : node->ChildrenList()) {
        if (!EnsureCallable(*arg, ctx)) {
            return TStatus::Error;
        }

        if (!TKqpUpsertRows::Match(arg.Get()) && !TKqpDeleteRows::Match(arg.Get())) {
            ctx.AddError(TIssue(ctx.GetPosition(node->Pos()), TStringBuilder()
                << "Unexpected effect: " << arg->Content()));
            return TStatus::Error;
        }

        if (!EnsureStreamType(*arg, ctx)) {
            return TStatus::Error;
        }

        auto itemType = arg->GetTypeAnn()->Cast<TStreamExprType>()->GetItemType();
        if (!IsSameAnnotation(*kqpEffectType, *itemType)) {
            ctx.AddError(TIssue(ctx.GetPosition(node->Pos()), TStringBuilder()
                << "Invalid YDB effect type, expected: " << FormatType(kqpEffectType)
                << ", actual: " << FormatType(itemType)));
            return TStatus::Error;
        }
    }

    node->SetTypeAnn(ctx.MakeType<TStreamExprType>(kqpEffectType));
    return TStatus::Ok;
}

TStatus AnnotateKqpProgram(const TExprNode::TPtr& node, TExprContext& ctx) {
    if (!EnsureArgsCount(*node, 2, ctx)) {
        return TStatus::Error;
    }

    auto* argsType = node->Child(TKqpProgram::idx_ArgsType);

    if (!EnsureType(*argsType, ctx)) {
        return TStatus::Error;
    }
    auto argTypesTupleRaw = argsType->GetTypeAnn()->Cast<TTypeExprType>()->GetType();

    if (!EnsureTupleType(node->Pos(), *argTypesTupleRaw, ctx)) {
        return TStatus::Error;
    }
    auto argTypesTuple = argTypesTupleRaw->Cast<TTupleExprType>();

    std::vector<const TTypeAnnotationNode*> argTypes;
    argTypes.reserve(argTypesTuple->GetSize());

    for (const auto& argTypeRaw : argTypesTuple->GetItems()) {
        if (!EnsureStreamType(node->Pos(), *argTypeRaw, ctx)) {
            return TStatus::Error;
        }
        argTypes.push_back(argTypeRaw);
    }

    auto& lambda = node->ChildRef(TKqpProgram::idx_Lambda);
    if (!EnsureLambda(*lambda, ctx)) {
        return TStatus::Error;
    }

    if (!UpdateLambdaAllArgumentsTypes(lambda, argTypes, ctx)) {
        return TStatus::Error;
    }

    if (!lambda->GetTypeAnn()) {
        return TStatus::Repeat;
    }

    node->SetTypeAnn(ctx.MakeType<TVoidExprType>());
    return TStatus::Ok;
}

TStatus AnnotateKqpEnsure(const TExprNode::TPtr& node, TExprContext& ctx) {
    if (!EnsureArgsCount(*node, 4, ctx)) {
        return TStatus::Error;
    }

    if (!EnsureComputable(node->Head(), ctx)) {
        return TStatus::Error;
    }

    const TDataExprType* dataType;
    bool isOptional;
    if (!EnsureDataOrOptionalOfData(*node->Child(TKqpEnsure::idx_Predicate), isOptional, dataType, ctx)) {
        return TStatus::Error;
    }

    if (!EnsureSpecificDataType(node->Child(TKqpEnsure::idx_Predicate)->Pos(), *dataType, EDataSlot::Bool, ctx)) {
        return TStatus::Error;
    }

    if (!EnsureAtom(*node->Child(TKqpEnsure::idx_IssueCode), ctx)) {
        return TStatus::Error;
    }

    ui64 code;
    if (!TryFromString(node->Child(TKqpEnsure::idx_IssueCode)->Content(), code)) {
        ctx.AddError(TIssue(ctx.GetPosition(node->Pos()), TStringBuilder()
            << "Issue code expected to be an integer: " << node->Child(TKqpEnsure::idx_IssueCode)->Content()));
        return TStatus::Error;
    }

    const auto* message = node->Child(TKqpEnsure::idx_Message);
    if (message->GetTypeAnn()->GetKind() != ETypeAnnotationKind::Data) {
        ctx.AddError(TIssue(ctx.GetPosition(message->Pos()), TStringBuilder()
            << "Expected Utf8, but got: " << *message->GetTypeAnn()));
        return TStatus::Error;
    }

    if (const auto dataSlot = message->GetTypeAnn()->Cast<TDataExprType>()->GetSlot(); dataSlot != EDataSlot::Utf8) {
        ctx.AddError(TIssue(ctx.GetPosition(message->Pos()), TStringBuilder()
            << "Expected Utf8, but got: " << *message->GetTypeAnn()));
        return TStatus::Error;
    }

    node->SetTypeAnn(node->Head().GetTypeAnn());
    return TStatus::Ok;
}

TStatus AnnotateStreamLookupConnection(const TExprNode::TPtr& node, TExprContext& ctx, const TString& cluster,
    const TKikimrTablesData& tablesData, bool withSystemColumns) {

    if (!EnsureArgsCount(*node, 4, ctx)) {
        return TStatus::Error;
    }

    if (!EnsureCallable(*node->Child(TKqpCnStreamLookup::idx_Output), ctx)) {
        return TStatus::Error;
    }

    if (!TDqOutput::Match(node->Child(TKqpCnStreamLookup::idx_Output))) {
        ctx.AddError(TIssue(ctx.GetPosition(node->Child(TDqCnMerge::idx_Output)->Pos()),
            TStringBuilder() << "Expected " << TDqOutput::CallableName()));
        return TStatus::Error;
    }

    auto table = ResolveTable(node->Child(TKqpCnStreamLookup::idx_Table), ctx, cluster, tablesData);
    if (!table.second) {
        return TStatus::Error;
    }

    auto lookupKeysTypeNode = node->Child(TKqpCnStreamLookup::idx_LookupKeysType);
    if (!EnsureType(*lookupKeysTypeNode, ctx)) {
        return TStatus::Error;
    }

    auto lookupKeysType = lookupKeysTypeNode->GetTypeAnn()->Cast<TTypeExprType>()->GetType();
    if (!EnsureListType(node->Pos(), *lookupKeysType, ctx)) {
        return TStatus::Error;
    }

    auto lookupKeyType = lookupKeysType->Cast<TListExprType>()->GetItemType();
    if (!EnsureStructType(node->Pos(), *lookupKeyType, ctx)) {
        return TStatus::Error;
    }

    const auto& lookupKeyColumns = lookupKeyType->Cast<TStructExprType>()->GetItems();
    for (const auto& keyColumn : lookupKeyColumns) {
        if (!table.second->GetKeyColumnIndex(TString(keyColumn->GetName()))) {
            return TStatus::Error;
        }
    }

    if (!EnsureTupleOfAtoms(*node->Child(TKqpCnStreamLookup::idx_Columns), ctx)) {
        return TStatus::Error;
    }

    TCoAtomList columns{node->ChildPtr(TKqpCnStreamLookup::idx_Columns)};

    auto rowType = GetReadTableRowType(ctx, tablesData, cluster, table.first, columns, withSystemColumns);
    if (!rowType) {
        return TStatus::Error;
    }

    node->SetTypeAnn(ctx.MakeType<TStreamExprType>(rowType));
    return TStatus::Ok;
}

} // namespace

TAutoPtr<IGraphTransformer> CreateKqpTypeAnnotationTransformer(const TString& cluster,
    TIntrusivePtr<TKikimrTablesData> tablesData, TTypeAnnotationContext& typesCtx, TKikimrConfiguration::TPtr config)
{
    TAutoPtr<IGraphTransformer> dqTransformer = CreateDqTypeAnnotationTransformer(typesCtx);

    return CreateFunctorTransformer(
        [cluster, tablesData, dqTransformer, config](const TExprNode::TPtr& input, TExprNode::TPtr& output,
            TExprContext& ctx) -> TStatus
        {
            output = input;

            TIssueScopeGuard issueScope(ctx.IssueManager, [&input, &ctx] {
                return MakeIntrusive<TIssue>(ctx.GetPosition(input->Pos()),
                        TStringBuilder() << "At function: " << input->Content());
            });

            if (TKqpTable::Match(input.Get())) {
                return AnnotateTable(input, ctx, cluster, *tablesData);
            }

            if (TKqlReadTableBase::Match(input.Get())) {
                return AnnotateReadTable(input, ctx, cluster, *tablesData, config->SystemColumnsEnabled());
            }

            if (TKqlReadTableRangesBase::Match(input.Get())) {
                return AnnotateReadTableRanges(input, ctx, cluster, *tablesData, config->SystemColumnsEnabled());
            }

            if (TKqlLookupTableBase::Match(input.Get())) {
                return AnnotateLookupTable(input, ctx, cluster, *tablesData, config->SystemColumnsEnabled());
            }

            if (TKqlKeyInc::Match(input.Get()) || TKqlKeyExc::Match(input.Get())) {
                return AnnotateKeyTuple(input, ctx);
            }

            if (TKqlUpsertRowsBase::Match(input.Get())) {
                return AnnotateUpsertRows(input, ctx, cluster, *tablesData);
            }

            if (TKqlInsertRowsBase::Match(input.Get())) {
                return AnnotateInsertRows(input, ctx, cluster, *tablesData);
            }

            if (TKqlUpdateRowsBase::Match(input.Get())) {
                return AnnotateUpdateRows(input, ctx, cluster, *tablesData);
            }

            if (TKqlDeleteRowsBase::Match(input.Get())) {
                return AnnotateDeleteRows(input, ctx, cluster, *tablesData);
            }

            if (TKqpOlapAnd::Match(input.Get())
                || TKqpOlapOr::Match(input.Get())
                || TKqpOlapXor::Match(input.Get())
            )
            {
                return AnnotateOlapBinaryLogicOperator(input, ctx);
            }

            if (TKqpOlapNot::Match(input.Get())) {
                return AnnotateOlapUnaryLogicOperator(input, ctx);
            }

            if (TKqpOlapFilter::Match(input.Get())) {
                return AnnotateOlapFilter(input, ctx);
            }

            if (TKqpOlapAgg::Match(input.Get())) {
                return AnnotateOlapAgg(input, ctx);
            }

            if (TKqpOlapExtractMembers::Match(input.Get())) {
                return AnnotateOlapExtractMembers(input, ctx);
            }

            if (TKqpCnMapShard::Match(input.Get()) || TKqpCnShuffleShard::Match(input.Get())) {
                return AnnotateDqConnection(input, ctx);
            }

            if (TKqpCnStreamLookup::Match(input.Get())) {
                return AnnotateStreamLookupConnection(input, ctx, cluster, *tablesData, config->SystemColumnsEnabled());
            }

            if (TKqpTxResultBinding::Match(input.Get())) {
                return AnnotateKqpTxResultBinding(input, ctx);
            }

            if (TKqpTxInternalBinding::Match(input.Get())) {
                return AnnotateKqpTxInternalBinding(input, ctx);
            }

            if (TKqpPhysicalTx::Match(input.Get())) {
                return AnnotateKqpPhysicalTx(input, ctx);
            }

            if (TKqpPhysicalQuery::Match(input.Get())) {
                return AnnotateKqpPhysicalQuery(input, ctx);
            }

            if (TKqpEffects::Match(input.Get())) {
                return AnnotateKqpEffects(input, ctx);
            }

            if (TKqpProgram::Match(input.Get())) {
                return AnnotateKqpProgram(input, ctx);
            }

            if (TKqpEnsure::Match(input.Get())) {
                return AnnotateKqpEnsure(input, ctx);
            }

            if (TKqpReadRangesSourceSettings::Match(input.Get())) {
                return AnnotateKqpSourceSettings(input, ctx, cluster, *tablesData, config->SystemColumnsEnabled());
            }

            return dqTransformer->Transform(input, output, ctx);
        });
}

TAutoPtr<IGraphTransformer> CreateKqpCheckQueryTransformer() {
    return CreateFunctorTransformer(
        [](const TExprNode::TPtr& input, TExprNode::TPtr& output, TExprContext& ctx) -> TStatus {
            output = input;

            YQL_ENSURE(TMaybeNode<TKqlQuery>(input));

            auto query = TKqlQuery(input);
            for (const auto& result : query.Results()) {
                if (!EnsureTupleSize(result.Ref(), 2, ctx)) {
                    return TStatus::Error;
                }
                if (!EnsureListType(result.Value().Ref(), ctx)) {
                    return TStatus::Error;
                }
            }

            return TStatus::Ok;
        });
}

} // namespace NKqp
} // namespace NKikimr
