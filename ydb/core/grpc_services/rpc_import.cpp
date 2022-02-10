#include "grpc_request_proxy.h"
#include "rpc_import_base.h"
#include "rpc_calls.h"
#include "rpc_operation_request_base.h"

#include <ydb/core/tx/schemeshard/schemeshard_import.h>

#include <library/cpp/actors/core/hfunc.h>

#include <util/generic/ptr.h>
#include <util/string/builder.h>

namespace NKikimr {
namespace NGRpcService {

using namespace NActors;
using namespace NSchemeShard;
using namespace NKikimrIssues;
using namespace Ydb;

template <typename TDerived, typename TEvRequest>
class TImportRPC: public TRpcOperationRequestActor<TDerived, TEvRequest, true>, public TImportConv {
    TStringBuf GetLogPrefix() const override {
        return "[CreateImport]";
    }

    IEventBase* MakeRequest() override {
        const auto& request = *this->Request->GetProtoRequest();

        auto ev = MakeHolder<TEvImport::TEvCreateImportRequest>();
        ev->Record.SetTxId(this->TxId);
        ev->Record.SetDatabaseName(this->DatabaseName);
        if (this->UserToken) {
            ev->Record.SetUserSID(this->UserToken->GetUserSID());
        }

        auto& createImport = *ev->Record.MutableRequest();
        createImport.MutableOperationParams()->CopyFrom(request.operation_params());
        if (std::is_same_v<TEvRequest, TEvImportFromS3Request>) {
            createImport.MutableImportFromS3Settings()->CopyFrom(request.settings());
        }

        return ev.Release();
    }

    void Handle(TEvImport::TEvCreateImportResponse::TPtr& ev) {
        const auto& record = ev->Get()->Record.GetResponse();

        LOG_D("Handle TEvImport::TEvCreateImportResponse"
            << ": record# " << record.ShortDebugString());

        this->Reply(TImportConv::ToOperation(record.GetEntry()));
    }

public:
    using TRpcOperationRequestActor<TDerived, TEvRequest, true>::TRpcOperationRequestActor;

    void Bootstrap(const TActorContext&) {
        const auto& request = *this->Request->GetProtoRequest();

        if (request.settings().items().empty()) {
            return this->Reply(StatusIds::BAD_REQUEST, TIssuesIds::DEFAULT_ERROR, "Items are not set");
        }

        this->AllocateTxId();
        this->Become(&TDerived::StateWait);
    }

    STATEFN(StateWait) {
        switch (ev->GetTypeRewrite()) {
            hFunc(TEvImport::TEvCreateImportResponse, Handle);
        default:
            return this->StateBase(ev, TlsActivationContext->AsActorContext());
        }
    }

}; // TImportRPC

class TImportFromS3RPC: public TImportRPC<TImportFromS3RPC, TEvImportFromS3Request> {
public:
    using TImportRPC::TImportRPC;
};

void TGRpcRequestProxy::Handle(TEvImportFromS3Request::TPtr& ev, const TActorContext& ctx) {
    ctx.Register(new TImportFromS3RPC(ev->Release().Release()));
}

} // namespace NGRpcService
} // namespace NKikimr
