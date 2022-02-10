#pragma once

#include "datashard_common_upload.h"
#include "datashard_impl.h"
#include "execution_unit_kind.h"

namespace NKikimr {
namespace NDataShard {

using NTabletFlatExecutor::TTransactionContext;

class TDataShard::TTxStopGuard : public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    TTxStopGuard(TDataShard* ds)
        : TTransactionBase(ds)
    { }

    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override {
        Y_UNUSED(txc);
        Y_UNUSED(ctx);
        return true;
    }

    void Complete(const TActorContext& ctx) override;

    TTxType GetTxType() const override { return TXTYPE_STOP_GUARD; }
};

class TDataShard::TTxGetShardState : public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    TTxGetShardState(TDataShard* ds, TEvDataShard::TEvGetShardState::TPtr ev);
    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override;
    void Complete(const TActorContext &ctx) override;
    TTxType GetTxType() const override { return TXTYPE_GET_STARD_STATE; }
private:
    TEvDataShard::TEvGetShardState::TPtr Ev;
    THolder<TEvDataShard::TEvGetShardStateResult> Result;
};

class TDataShard::TTxInit : public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    TTxInit(TDataShard* ds);
    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override;
    void Complete(const TActorContext &ctx) override;
    TTxType GetTxType() const override { return TXTYPE_INIT; }
private:
    bool CreateScheme(TTransactionContext &txc);
    bool ReadEverything(TTransactionContext &txc);
private:
    TVector<TEvChangeExchange::TEvEnqueueRecords::TRecordInfo> ChangeRecords;
};

class TDataShard::TTxPlanStep : public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    TTxPlanStep(TDataShard *self, TEvTxProcessing::TEvPlanStep::TPtr ev);
    bool Execute(TTransactionContext &txc, const TActorContext &ctx) override;
    void Complete(const TActorContext &ctx) override;
    TTxType GetTxType() const override { return TXTYPE_PLAN_STEP; }

private:
    TEvTxProcessing::TEvPlanStep::TPtr Ev;
    bool IsAccepted;
    TInstant RequestStartTime;
    TMap<TActorId, TVector<ui64>> TxByAck;
};

class TDataShard::TTxProgressTransaction : public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    explicit TTxProgressTransaction(TDataShard *self, TOperation::TPtr op = nullptr);
    bool Execute(TTransactionContext &txc, const TActorContext &ctx) override;
    void Complete(const TActorContext &ctx) override;
    TTxType GetTxType() const override { return TXTYPE_PROGRESS_START; }

private:
    TOperation::TPtr ActiveOp;
    TVector<EExecutionUnitKind> CompleteList;
    TInstant CommitStart;
    bool Rescheduled = false;
};

class TDataShard::TTxProposeTransactionBase : public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    TTxProposeTransactionBase(TDataShard *self,
                              TEvDataShard::TEvProposeTransaction::TPtr &&ev,
                              TInstant receivedAt, ui64 tieBreakerIndex,
                              bool delayed);

    bool Execute(NTabletFlatExecutor::TTransactionContext &txc,
                 const TActorContext &ctx) override;
    void Complete(const TActorContext &ctx) override;
    TTxType GetTxType() const override { return TXTYPE_PROPOSE; }

private:
    bool SyncSchemeOnFollower(TOutputOpData::TResultPtr &result,
                           TTransactionContext &txc,
                           const TActorContext &ctx);

protected:
    TOperation::TPtr Op;
    TEvDataShard::TEvProposeTransaction::TPtr Ev;
    const TInstant ReceivedAt;
    const ui64 TieBreakerIndex;
    EOperationKind Kind;
    ui64 TxId;
    TVector<EExecutionUnitKind> CompleteList;
    TInstant CommitStart;
    bool Acked;
    bool Rescheduled = false;
};

class TDataShard::TTxReadSet : public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    TTxReadSet(TDataShard *self, TEvTxProcessing::TEvReadSet::TPtr ev);
    bool Execute(TTransactionContext &txc, const TActorContext &ctx) override;
    void Complete(const TActorContext &ctx) override;
    TTxType GetTxType() const override { return TXTYPE_READSET; }

private:
    TEvTxProcessing::TEvReadSet::TPtr Ev;
    THolder<IEventHandle> Ack;

    THolder<IEventHandle> MakeAck(const TActorContext &ctx);
};

class TDataShard::TTxProgressResendRS : public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    TTxProgressResendRS(TDataShard *self, ui64 seqno);
    bool Execute(TTransactionContext &txc, const TActorContext &ctx) override;
    void Complete(const TActorContext &ctx) override;
    TTxType GetTxType() const override { return TXTYPE_PROGRESS_RESEND_RS; }
private:
    const ui64 Seqno;
};

class TDataShard::TTxCancelTransactionProposal : public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    TTxCancelTransactionProposal(TDataShard *self, ui64 txId);
    bool Execute(TTransactionContext &txc, const TActorContext &ctx) override;
    void Complete(const TActorContext &ctx) override;
    TTxType GetTxType() const override { return TXTYPE_CANCEL_TX_PROPOSAL; }
private:
    const ui64 TxId;
};

inline bool MaybeRequestMoreTxMemory(ui64 usage, NTabletFlatExecutor::TTransactionContext &txc) {
    if (usage > txc.GetMemoryLimit()) {
        ui64 request = Max(usage - txc.GetMemoryLimit(), txc.GetMemoryLimit() * MEMORY_REQUEST_FACTOR);
        txc.RequestMemory(request);
        return true;
    }
    return false;
}

class TDataShard::TTxStoreTablePath : public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    TTxStoreTablePath(TDataShard *self,
                      ui64 pathId,
                      const TString &path);
    bool Execute(TTransactionContext &txc,
                 const TActorContext &ctx) override;
    void Complete(const TActorContext &ctx) override;
    TTxType GetTxType() const override { return TXTYPE_STORE_TABLE_PATH; }

private:
    ui64 PathId;
    TString Path;
};

class TDataShard::TTxStoreScanState : public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    TTxStoreScanState(TDataShard* ds, TEvPrivate::TEvPersistScanState::TPtr ev);
    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override;
    void Complete(const TActorContext &ctx) override;
    TTxType GetTxType() const override { return TXTYPE_STORE_SCAN_STATE; }
private:
    TEvPrivate::TEvPersistScanState::TPtr Ev;
    TActorId Sender;
};

class TDataShard::TTxRefreshVolatileSnapshot : public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    TTxRefreshVolatileSnapshot(TDataShard* ds, TEvDataShard::TEvRefreshVolatileSnapshotRequest::TPtr ev);
    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override;
    void Complete(const TActorContext& ctx) override;
    TTxType GetTxType() const override { return TXTYPE_REFRESH_VOLATILE_SNAPSHOT; }
private:
    TEvDataShard::TEvRefreshVolatileSnapshotRequest::TPtr Ev;
    THolder<TEvDataShard::TEvRefreshVolatileSnapshotResponse> Reply;
};

class TDataShard::TTxDiscardVolatileSnapshot : public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    TTxDiscardVolatileSnapshot(TDataShard* ds, TEvDataShard::TEvDiscardVolatileSnapshotRequest::TPtr ev);
    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override;
    void Complete(const TActorContext& ctx) override;
    TTxType GetTxType() const override { return TXTYPE_DISCARD_VOLATILE_SNAPSHOT; }
private:
    TEvDataShard::TEvDiscardVolatileSnapshotRequest::TPtr Ev;
    THolder<TEvDataShard::TEvDiscardVolatileSnapshotResponse> Reply;
};

class TDataShard::TTxCleanupRemovedSnapshots : public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    TTxCleanupRemovedSnapshots(TDataShard* ds);
    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override;
    void Complete(const TActorContext& ctx) override;
    TTxType GetTxType() const override { return TXTYPE_CLEANUP_REMOVED_SNAPSHOTS; }
};

class TDataShard::TTxMigrateSchemeShard : public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    TTxMigrateSchemeShard(TDataShard* ds, TEvDataShard::TEvMigrateSchemeShardRequest::TPtr ev);
    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override;
    void Complete(const TActorContext& ctx) override;
    TTxType GetTxType() const override { return TXTYPE_MIGRATE_SCHEMESHARD; }
private:
    TEvDataShard::TEvMigrateSchemeShardRequest::TPtr Ev;
    THolder<TEvDataShard::TEvMigrateSchemeShardResponse> Reply;
};

class TDataShard::TTxGetS3Upload: public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    TTxGetS3Upload(TDataShard* ds, TEvDataShard::TEvGetS3Upload::TPtr ev);
    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override;
    void Complete(const TActorContext& ctx) override;
    TTxType GetTxType() const override { return TXTYPE_GET_S3_UPLOAD_ID; }
private:
    TEvDataShard::TEvGetS3Upload::TPtr Ev;
    THolder<TEvDataShard::TEvS3Upload> Reply;
};

class TDataShard::TTxStoreS3UploadId: public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    TTxStoreS3UploadId(TDataShard* ds, TEvDataShard::TEvStoreS3UploadId::TPtr ev);
    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override;
    void Complete(const TActorContext& ctx) override;
    TTxType GetTxType() const override { return TXTYPE_STORE_S3_UPLOAD_ID; }
private:
    TEvDataShard::TEvStoreS3UploadId::TPtr Ev;
    THolder<TEvDataShard::TEvS3Upload> Reply;
};

class TDataShard::TTxChangeS3UploadStatus: public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    TTxChangeS3UploadStatus(TDataShard* ds, TEvDataShard::TEvChangeS3UploadStatus::TPtr ev);
    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override;
    void Complete(const TActorContext& ctx) override;
    TTxType GetTxType() const override { return TXTYPE_CHANGE_S3_UPLOAD_STATUS; }
private:
    TEvDataShard::TEvChangeS3UploadStatus::TPtr Ev;
    THolder<TEvDataShard::TEvS3Upload> Reply;
};

class TDataShard::TTxGetS3DownloadInfo: public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    TTxGetS3DownloadInfo(TDataShard* ds, TEvDataShard::TEvGetS3DownloadInfo::TPtr ev);
    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override;
    void Complete(const TActorContext& ctx) override;
    TTxType GetTxType() const override { return TXTYPE_GET_S3_DOWNLOAD_INFO; }
private:
    TEvDataShard::TEvGetS3DownloadInfo::TPtr Ev;
    THolder<TEvDataShard::TEvS3DownloadInfo> Reply;
};

class TDataShard::TTxStoreS3DownloadInfo: public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    TTxStoreS3DownloadInfo(TDataShard* ds, TEvDataShard::TEvStoreS3DownloadInfo::TPtr ev);
    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override;
    void Complete(const TActorContext& ctx) override;
    TTxType GetTxType() const override { return TXTYPE_STORE_S3_DOWNLOAD_INFO; }
private:
    TEvDataShard::TEvStoreS3DownloadInfo::TPtr Ev;
    THolder<TEvDataShard::TEvS3DownloadInfo> Reply;
};

class TDataShard::TTxUnsafeUploadRows: public NTabletFlatExecutor::TTransactionBase<TDataShard>,
                                              public TCommonUploadOps<
                                                  TEvDataShard::TEvUnsafeUploadRowsRequest,
                                                  TEvDataShard::TEvUnsafeUploadRowsResponse> {
public:
    TTxUnsafeUploadRows(TDataShard* ds, TEvDataShard::TEvUnsafeUploadRowsRequest::TPtr& ev);
    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override;
    void Complete(const TActorContext& ctx) override;
    TTxType GetTxType() const override { return TXTYPE_UNSAFE_UPLOAD_ROWS; }
};

class TDataShard::TTxExecuteMvccStateChange: public NTabletFlatExecutor::TTransactionBase<TDataShard> {
public:
    TTxExecuteMvccStateChange(TDataShard* ds);
    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override;
    void Complete(const TActorContext& ctx) override;
    TTxType GetTxType() const override { return TXTYPE_EXECUTE_MVCC_STATE_CHANGE; }
private:
    bool ActivateWaitingOps = false;
};

}}
