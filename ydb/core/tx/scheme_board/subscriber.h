#pragma once

#include "defs.h"

#include <ydb/core/base/pathid.h>

#include <util/generic/string.h>

namespace NKikimr {

IActor* CreateSchemeBoardSubscriber(
    const TActorId& owner,
    const TString& path,
    const ui64 stateStorageGroup,
    const ui64 domainOwnerId
);

IActor* CreateSchemeBoardSubscriber(
    const TActorId& owner,
    const TPathId& pathId,
    const ui64 stateStorageGroup,
    const ui64 domainOwnerId
);

// deprecated
enum class ESchemeBoardSubscriberDeletionPolicy {
    First,
    Second,
    Majority,
};

IActor* CreateSchemeBoardSubscriber(
    const TActorId& owner,
    const TString& path,
    const ui64 stateStorageGroup,
    const ESchemeBoardSubscriberDeletionPolicy deletionPolicy
);

IActor* CreateSchemeBoardSubscriber(
    const TActorId& owner,
    const TPathId& pathId,
    const ui64 stateStorageGroup,
    const ESchemeBoardSubscriberDeletionPolicy deletionPolicy
);

} // NKikimr
