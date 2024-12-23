/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 ts=4 sts=4 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/DebugOnly.h"

#include "nsLoadGroup.h"

#include "nsArrayEnumerator.h"
#include "nsCOMArray.h"
#include "nsCOMPtr.h"
#include "prlog.h"
#include "nsString.h"
#include "nsTArray.h"
#include "mozilla/Atomics.h"
#include "nsAutoPtr.h"
#include "mozilla/net/PSpdyPush.h"
#include "nsITimedChannel.h"
#include "nsIInterfaceRequestor.h"
#include "nsIRequestObserver.h"
#include "CacheObserver.h"
#include "MainThreadUtils.h"

using namespace mozilla;
using namespace mozilla::net;

#if defined(PR_LOGGING)
//
// Log module for nsILoadGroup logging...
//
// To enable logging (see prlog.h for full details):
//
//    set NSPR_LOG_MODULES=LoadGroup:5
//    set NSPR_LOG_FILE=nspr.log
//
// this enables PR_LOG_DEBUG level information and places all output in
// the file nspr.log
//
static PRLogModuleInfo* gLoadGroupLog = nullptr;
#endif

#undef LOG
#define LOG(args) PR_LOG(gLoadGroupLog, PR_LOG_DEBUG, args)

////////////////////////////////////////////////////////////////////////////////

class RequestMapEntry : public PLDHashEntryHdr
{
public:
    explicit RequestMapEntry(nsIRequest *aRequest) :
        mKey(aRequest)
    {
    }

    nsCOMPtr<nsIRequest> mKey;
};

static bool
RequestHashMatchEntry(PLDHashTable *table, const PLDHashEntryHdr *entry,
                      const void *key)
{
    const RequestMapEntry *e =
        static_cast<const RequestMapEntry *>(entry);
    const nsIRequest *request = static_cast<const nsIRequest *>(key);

    return e->mKey == request;
}

static void
RequestHashClearEntry(PLDHashTable *table, PLDHashEntryHdr *entry)
{
    RequestMapEntry *e = static_cast<RequestMapEntry *>(entry);

    // An entry is being cleared, let the entry do its own cleanup.
    e->~RequestMapEntry();
}

static void
RequestHashInitEntry(PLDHashEntryHdr *entry, const void *key)
{
    const nsIRequest *const_request = static_cast<const nsIRequest *>(key);
    nsIRequest *request = const_cast<nsIRequest *>(const_request);

    // Initialize the entry with placement new
    new (entry) RequestMapEntry(request);
}


static void
RescheduleRequest(nsIRequest *aRequest, int32_t delta)
{
    nsCOMPtr<nsISupportsPriority> p = do_QueryInterface(aRequest);
    if (p)
        p->AdjustPriority(delta);
}

static PLDHashOperator
RescheduleRequests(PLDHashTable *table, PLDHashEntryHdr *hdr,
                   uint32_t number, void *arg)
{
    RequestMapEntry *e = static_cast<RequestMapEntry *>(hdr);
    int32_t *delta = static_cast<int32_t *>(arg);

    RescheduleRequest(e->mKey, *delta);
    return PL_DHASH_NEXT;
}


nsLoadGroup::nsLoadGroup(nsISupports* outer)
    : mForegroundCount(0)
    , mLoadFlags(LOAD_NORMAL)
    , mDefaultLoadFlags(0)
    , mStatus(NS_OK)
    , mPriority(PRIORITY_NORMAL)
    , mIsCanceling(false)
    , mDefaultLoadIsTimed(false)
    , mTimedNonCachedRequestsUntilOnEndPageLoad(0)
{
    NS_INIT_AGGREGATED(outer);

#if defined(PR_LOGGING)
    // Initialize the global PRLogModule for nsILoadGroup logging
    if (nullptr == gLoadGroupLog)
        gLoadGroupLog = PR_NewLogModule("LoadGroup");
#endif

    LOG(("LOADGROUP [%x]: Created.\n", this));
}

nsLoadGroup::~nsLoadGroup()
{
    DebugOnly<nsresult> rv = Cancel(NS_BINDING_ABORTED);
    NS_ASSERTION(NS_SUCCEEDED(rv), "Cancel failed");

    if (mRequests.IsInitialized()) {
        PL_DHashTableFinish(&mRequests);
    }

    mDefaultLoadRequest = 0;

    LOG(("LOADGROUP [%x]: Destroyed.\n", this));
}


////////////////////////////////////////////////////////////////////////////////
// nsISupports methods:

NS_IMPL_AGGREGATED(nsLoadGroup)
NS_INTERFACE_MAP_BEGIN_AGGREGATED(nsLoadGroup)
    NS_INTERFACE_MAP_ENTRY(nsILoadGroup)
    NS_INTERFACE_MAP_ENTRY(nsPILoadGroupInternal)
    NS_INTERFACE_MAP_ENTRY(nsILoadGroupChild)
    NS_INTERFACE_MAP_ENTRY(nsIRequest)
    NS_INTERFACE_MAP_ENTRY(nsISupportsPriority)
    NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
NS_INTERFACE_MAP_END

////////////////////////////////////////////////////////////////////////////////
// nsIRequest methods:

NS_IMETHODIMP
nsLoadGroup::GetName(nsACString &result)
{
    // XXX is this the right "name" for a load group?

    if (!mDefaultLoadRequest) {
        result.Truncate();
        return NS_OK;
    }
    
    return mDefaultLoadRequest->GetName(result);
}

NS_IMETHODIMP
nsLoadGroup::IsPending(bool *aResult)
{
    *aResult = (mForegroundCount > 0) ? true : false;
    return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::GetStatus(nsresult *status)
{
    if (NS_SUCCEEDED(mStatus) && mDefaultLoadRequest)
        return mDefaultLoadRequest->GetStatus(status);
    
    *status = mStatus;
    return NS_OK; 
}

// PLDHashTable enumeration callback that appends strong references to
// all nsIRequest to an nsTArray<nsIRequest*>.
static PLDHashOperator
AppendRequestsToArray(PLDHashTable *table, PLDHashEntryHdr *hdr,
                      uint32_t number, void *arg)
{
    RequestMapEntry *e = static_cast<RequestMapEntry *>(hdr);
    nsTArray<nsIRequest*> *array = static_cast<nsTArray<nsIRequest*> *>(arg);

    nsIRequest *request = e->mKey;
    NS_ASSERTION(request, "What? Null key in pldhash entry?");

    bool ok = array->AppendElement(request) != nullptr;

    if (!ok) {
        return PL_DHASH_STOP;
    }

    NS_ADDREF(request);

    return PL_DHASH_NEXT;
}

NS_IMETHODIMP
nsLoadGroup::Cancel(nsresult status)
{
    MOZ_ASSERT(NS_IsMainThread());

    NS_ASSERTION(NS_FAILED(status), "shouldn't cancel with a success code");
    nsresult rv;
    uint32_t count = mRequests.EntryCount();

    nsAutoTArray<nsIRequest*, 8> requests;

    PL_DHashTableEnumerate(&mRequests, AppendRequestsToArray,
                           static_cast<nsTArray<nsIRequest*> *>(&requests));

    if (requests.Length() != count) {
        for (uint32_t i = 0, len = requests.Length(); i < len; ++i) {
            NS_RELEASE(requests[i]);
        }

        return NS_ERROR_OUT_OF_MEMORY;
    }

    // set the load group status to our cancel status while we cancel 
    // all our requests...once the cancel is done, we'll reset it...
    //
    mStatus = status;

    // Set the flag indicating that the loadgroup is being canceled...  This
    // prevents any new channels from being added during the operation.
    //
    mIsCanceling = true;

    nsresult firstError = NS_OK;

    while (count > 0) {
        nsIRequest* request = requests.ElementAt(--count);

        NS_ASSERTION(request, "NULL request found in list.");

        if (!PL_DHashTableSearch(&mRequests, request)) {
            // |request| was removed already
            NS_RELEASE(request);
            continue;
        }

#if defined(PR_LOGGING)
        nsAutoCString nameStr;
        request->GetName(nameStr);
        LOG(("LOADGROUP [%x]: Canceling request %x %s.\n",
             this, request, nameStr.get()));
#endif

        //
        // Remove the request from the load group...  This may cause
        // the OnStopRequest notification to fire...
        //
        // XXX: What should the context be?
        //
        (void)RemoveRequest(request, nullptr, status);

        // Cancel the request...
        rv = request->Cancel(status);

        // Remember the first failure and return it...
        if (NS_FAILED(rv) && NS_SUCCEEDED(firstError))
            firstError = rv;

        NS_RELEASE(request);
    }

#if defined(DEBUG)
    NS_ASSERTION(mRequests.EntryCount() == 0, "Request list is not empty.");
    NS_ASSERTION(mForegroundCount == 0, "Foreground URLs are active.");
#endif

    mStatus = NS_OK;
    mIsCanceling = false;

    return firstError;
}


NS_IMETHODIMP
nsLoadGroup::Suspend()
{
    nsresult rv, firstError;
    uint32_t count = mRequests.EntryCount();

    nsAutoTArray<nsIRequest*, 8> requests;

    PL_DHashTableEnumerate(&mRequests, AppendRequestsToArray,
                           static_cast<nsTArray<nsIRequest*> *>(&requests));

    if (requests.Length() != count) {
        for (uint32_t i = 0, len = requests.Length(); i < len; ++i) {
            NS_RELEASE(requests[i]);
        }

        return NS_ERROR_OUT_OF_MEMORY;
    }

    firstError = NS_OK;
    //
    // Operate the elements from back to front so that if items get
    // get removed from the list it won't affect our iteration
    //
    while (count > 0) {
        nsIRequest* request = requests.ElementAt(--count);

        NS_ASSERTION(request, "NULL request found in list.");
        if (!request)
            continue;

#if defined(PR_LOGGING)
        nsAutoCString nameStr;
        request->GetName(nameStr);
        LOG(("LOADGROUP [%x]: Suspending request %x %s.\n",
            this, request, nameStr.get()));
#endif

        // Suspend the request...
        rv = request->Suspend();

        // Remember the first failure and return it...
        if (NS_FAILED(rv) && NS_SUCCEEDED(firstError))
            firstError = rv;

        NS_RELEASE(request);
    }

    return firstError;
}


NS_IMETHODIMP
nsLoadGroup::Resume()
{
    nsresult rv, firstError;
    uint32_t count = mRequests.EntryCount();

    nsAutoTArray<nsIRequest*, 8> requests;

    PL_DHashTableEnumerate(&mRequests, AppendRequestsToArray,
                           static_cast<nsTArray<nsIRequest*> *>(&requests));

    if (requests.Length() != count) {
        for (uint32_t i = 0, len = requests.Length(); i < len; ++i) {
            NS_RELEASE(requests[i]);
        }

        return NS_ERROR_OUT_OF_MEMORY;
    }

    firstError = NS_OK;
    //
    // Operate the elements from back to front so that if items get
    // get removed from the list it won't affect our iteration
    //
    while (count > 0) {
        nsIRequest* request = requests.ElementAt(--count);

        NS_ASSERTION(request, "NULL request found in list.");
        if (!request)
            continue;

#if defined(PR_LOGGING)
        nsAutoCString nameStr;
        request->GetName(nameStr);
        LOG(("LOADGROUP [%x]: Resuming request %x %s.\n",
            this, request, nameStr.get()));
#endif

        // Resume the request...
        rv = request->Resume();

        // Remember the first failure and return it...
        if (NS_FAILED(rv) && NS_SUCCEEDED(firstError))
            firstError = rv;

        NS_RELEASE(request);
    }

    return firstError;
}

NS_IMETHODIMP
nsLoadGroup::GetLoadFlags(uint32_t *aLoadFlags)
{
    *aLoadFlags = mLoadFlags;
    return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::SetLoadFlags(uint32_t aLoadFlags)
{
    mLoadFlags = aLoadFlags;
    return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::GetLoadGroup(nsILoadGroup **loadGroup)
{
    *loadGroup = mLoadGroup;
    NS_IF_ADDREF(*loadGroup);
    return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::SetLoadGroup(nsILoadGroup *loadGroup)
{
    mLoadGroup = loadGroup;
    return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsILoadGroup methods:

NS_IMETHODIMP
nsLoadGroup::GetDefaultLoadRequest(nsIRequest * *aRequest)
{
    *aRequest = mDefaultLoadRequest;
    NS_IF_ADDREF(*aRequest);
    return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::SetDefaultLoadRequest(nsIRequest *aRequest)
{
    mDefaultLoadRequest = aRequest;
    // Inherit the group load flags from the default load request
    if (mDefaultLoadRequest) {
        mDefaultLoadRequest->GetLoadFlags(&mLoadFlags);
        //
        // Mask off any bits that are not part of the nsIRequest flags.
        // in particular, nsIChannel::LOAD_DOCUMENT_URI...
        //
        mLoadFlags &= nsIRequest::LOAD_REQUESTMASK;

        nsCOMPtr<nsITimedChannel> timedChannel = do_QueryInterface(aRequest);
        mDefaultLoadIsTimed = timedChannel != nullptr;
        if (mDefaultLoadIsTimed) {
            timedChannel->GetChannelCreation(&mDefaultRequestCreationTime);
            timedChannel->SetTimingEnabled(true);
        }
    }
    // Else, do not change the group's load flags (see bug 95981)
    return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::AddRequest(nsIRequest *request, nsISupports* ctxt)
{
    nsresult rv;

#if defined(PR_LOGGING)
    {
        nsAutoCString nameStr;
        request->GetName(nameStr);
        LOG(("LOADGROUP [%x]: Adding request %x %s (count=%d).\n",
             this, request, nameStr.get(), mRequests.EntryCount()));
    }
#endif /* PR_LOGGING */

    NS_ASSERTION(!PL_DHashTableSearch(&mRequests, request),
                 "Entry added to loadgroup twice, don't do that");

    //
    // Do not add the channel, if the loadgroup is being canceled...
    //
    if (mIsCanceling) {

#if defined(PR_LOGGING)
        LOG(("LOADGROUP [%x]: AddChannel() ABORTED because LoadGroup is"
             " being canceled!!\n", this));
#endif /* PR_LOGGING */

        return NS_BINDING_ABORTED;
    }

    nsLoadFlags flags;
    // if the request is the default load request or if the default
    // load request is null, then the load group should inherit its
    // load flags from the request.
    if (mDefaultLoadRequest == request || !mDefaultLoadRequest)
        rv = request->GetLoadFlags(&flags);
    else
        rv = MergeLoadFlags(request, flags);
    if (NS_FAILED(rv)) return rv;
    
    //
    // Add the request to the list of active requests...
    //

    RequestMapEntry *entry = static_cast<RequestMapEntry *>
        (PL_DHashTableAdd(&mRequests, request, fallible));

    if (!entry) {
        return NS_ERROR_OUT_OF_MEMORY;
    }

    if (mPriority != 0)
        RescheduleRequest(request, mPriority);

    nsCOMPtr<nsITimedChannel> timedChannel = do_QueryInterface(request);
    if (timedChannel)
        timedChannel->SetTimingEnabled(true);

    if (!(flags & nsIRequest::LOAD_BACKGROUND)) {
        // Update the count of foreground URIs..
        mForegroundCount += 1;

        //
        // Fire the OnStartRequest notification out to the observer...
        //
        // If the notification fails then DO NOT add the request to
        // the load group.
        //
        nsCOMPtr<nsIRequestObserver> observer = do_QueryReferent(mObserver);
        if (observer) {
            LOG(("LOADGROUP [%x]: Firing OnStartRequest for request %x."
                 "(foreground count=%d).\n", this, request, mForegroundCount));

            rv = observer->OnStartRequest(request, ctxt);
            if (NS_FAILED(rv)) {
                LOG(("LOADGROUP [%x]: OnStartRequest for request %x FAILED.\n",
                    this, request));
                //
                // The URI load has been canceled by the observer.  Clean up
                // the damage...
                //

                PL_DHashTableRemove(&mRequests, request);

                rv = NS_OK;

                mForegroundCount -= 1;
            }
        }

        // Ensure that we're part of our loadgroup while pending
        if (mForegroundCount == 1 && mLoadGroup) {
            mLoadGroup->AddRequest(this, nullptr);
        }

    }

    return rv;
}

NS_IMETHODIMP
nsLoadGroup::RemoveRequest(nsIRequest *request, nsISupports* ctxt,
                           nsresult aStatus)
{
    NS_ENSURE_ARG_POINTER(request);
    nsresult rv;

#if defined(PR_LOGGING)
    {
        nsAutoCString nameStr;
        request->GetName(nameStr);
        LOG(("LOADGROUP [%x]: Removing request %x %s status %x (count=%d).\n",
            this, request, nameStr.get(), aStatus, mRequests.EntryCount() - 1));
    }
#endif

    // Make sure we have a owning reference to the request we're about
    // to remove.

    nsCOMPtr<nsIRequest> kungFuDeathGrip(request);

    //
    // Remove the request from the group.  If this fails, it means that
    // the request was *not* in the group so do not update the foreground
    // count or it will get messed up...
    //
    RequestMapEntry *entry =
        static_cast<RequestMapEntry *>
                   (PL_DHashTableSearch(&mRequests, request));

    if (!entry) {
        LOG(("LOADGROUP [%x]: Unable to remove request %x. Not in group!\n",
            this, request));

        return NS_ERROR_FAILURE;
    }

    PL_DHashTableRawRemove(&mRequests, entry);

    // Undo any group priority delta...
    if (mPriority != 0)
        RescheduleRequest(request, -mPriority);

    nsLoadFlags flags;
    rv = request->GetLoadFlags(&flags);
    if (NS_FAILED(rv)) return rv;

    if (!(flags & nsIRequest::LOAD_BACKGROUND)) {
        NS_ASSERTION(mForegroundCount > 0, "ForegroundCount messed up");
        mForegroundCount -= 1;

        // Fire the OnStopRequest out to the observer...
        nsCOMPtr<nsIRequestObserver> observer = do_QueryReferent(mObserver);
        if (observer) {
            LOG(("LOADGROUP [%x]: Firing OnStopRequest for request %x."
                 "(foreground count=%d).\n", this, request, mForegroundCount));

            rv = observer->OnStopRequest(request, ctxt, aStatus);

#if defined(PR_LOGGING)
            if (NS_FAILED(rv)) {
                LOG(("LOADGROUP [%x]: OnStopRequest for request %x FAILED.\n",
                    this, request));
            }
#endif
        }

        // If that was the last request -> remove ourselves from loadgroup
        if (mForegroundCount == 0 && mLoadGroup) {
            mLoadGroup->RemoveRequest(this, nullptr, aStatus);
        }
    }

    return rv;
}

// PLDHashTable enumeration callback that appends all items in the
// hash to an nsCOMArray
static PLDHashOperator
AppendRequestsToCOMArray(PLDHashTable *table, PLDHashEntryHdr *hdr,
                         uint32_t number, void *arg)
{
    RequestMapEntry *e = static_cast<RequestMapEntry *>(hdr);
    static_cast<nsCOMArray<nsIRequest>*>(arg)->AppendObject(e->mKey);
    return PL_DHASH_NEXT;
}

NS_IMETHODIMP
nsLoadGroup::GetRequests(nsISimpleEnumerator * *aRequests)
{
    nsCOMArray<nsIRequest> requests;
    requests.SetCapacity(mRequests.EntryCount());

    PL_DHashTableEnumerate(&mRequests, AppendRequestsToCOMArray, &requests);

    return NS_NewArrayEnumerator(aRequests, requests);
}

NS_IMETHODIMP
nsLoadGroup::SetGroupObserver(nsIRequestObserver* aObserver)
{
    mObserver = do_GetWeakReference(aObserver);
    return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::GetGroupObserver(nsIRequestObserver* *aResult)
{
    nsCOMPtr<nsIRequestObserver> observer = do_QueryReferent(mObserver);
    *aResult = observer;
    NS_IF_ADDREF(*aResult);
    return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::GetActiveCount(uint32_t* aResult)
{
    *aResult = mForegroundCount;
    return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::GetNotificationCallbacks(nsIInterfaceRequestor **aCallbacks)
{
    NS_ENSURE_ARG_POINTER(aCallbacks);
    *aCallbacks = mCallbacks;
    NS_IF_ADDREF(*aCallbacks);
    return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::SetNotificationCallbacks(nsIInterfaceRequestor *aCallbacks)
{
    mCallbacks = aCallbacks;
    return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::GetConnectionInfo(nsILoadGroupConnectionInfo **aCI)
{
    NS_ENSURE_ARG_POINTER(aCI);
    *aCI = mConnectionInfo;
    NS_IF_ADDREF(*aCI);
    return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsILoadGroupChild methods:

/* attribute nsILoadGroup parentLoadGroup; */
NS_IMETHODIMP
nsLoadGroup::GetParentLoadGroup(nsILoadGroup * *aParentLoadGroup)
{
    *aParentLoadGroup = nullptr;
    nsCOMPtr<nsILoadGroup> parent = do_QueryReferent(mParentLoadGroup);
    if (!parent)
        return NS_OK;
    parent.forget(aParentLoadGroup);
    return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::SetParentLoadGroup(nsILoadGroup *aParentLoadGroup)
{
    mParentLoadGroup = do_GetWeakReference(aParentLoadGroup);
    return NS_OK;
}

/* readonly attribute nsILoadGroup childLoadGroup; */
NS_IMETHODIMP
nsLoadGroup::GetChildLoadGroup(nsILoadGroup * *aChildLoadGroup)
{
    NS_ADDREF(*aChildLoadGroup = this);
    return NS_OK;
}

/* readonly attribute nsILoadGroup rootLoadGroup; */
NS_IMETHODIMP
nsLoadGroup::GetRootLoadGroup(nsILoadGroup * *aRootLoadGroup)
{
    // first recursively try the root load group of our parent
    nsCOMPtr<nsILoadGroupChild> ancestor = do_QueryReferent(mParentLoadGroup);
    if (ancestor)
        return ancestor->GetRootLoadGroup(aRootLoadGroup);

    // next recursively try the root load group of our own load grop
    ancestor = do_QueryInterface(mLoadGroup);
    if (ancestor)
        return ancestor->GetRootLoadGroup(aRootLoadGroup);

    // finally just return this
    NS_ADDREF(*aRootLoadGroup = this);
    return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsPILoadGroupInternal methods:

NS_IMETHODIMP
nsLoadGroup::OnEndPageLoad(nsIChannel *aDefaultChannel)
{
    // for the moment, nothing to do here.
    return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsISupportsPriority methods:

NS_IMETHODIMP
nsLoadGroup::GetPriority(int32_t *aValue)
{
    *aValue = mPriority;
    return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::SetPriority(int32_t aValue)
{
    return AdjustPriority(aValue - mPriority);
}

NS_IMETHODIMP
nsLoadGroup::AdjustPriority(int32_t aDelta)
{
    // Update the priority for each request that supports nsISupportsPriority
    if (aDelta != 0) {
        mPriority += aDelta;
        PL_DHashTableEnumerate(&mRequests, RescheduleRequests, &aDelta);
    }
    return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::GetDefaultLoadFlags(uint32_t *aFlags)
{
    *aFlags = mDefaultLoadFlags;
    return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::SetDefaultLoadFlags(uint32_t aFlags)
{
    mDefaultLoadFlags = aFlags;
    return NS_OK;
}


////////////////////////////////////////////////////////////////////////////////


nsresult nsLoadGroup::MergeLoadFlags(nsIRequest *aRequest, nsLoadFlags& outFlags)
{
    nsresult rv;
    nsLoadFlags flags, oldFlags;

    rv = aRequest->GetLoadFlags(&flags);
    if (NS_FAILED(rv)) 
        return rv;

    oldFlags = flags;

    // Inherit the following bits...
    flags |= (mLoadFlags & (LOAD_BACKGROUND |
                            LOAD_BYPASS_CACHE |
                            LOAD_FROM_CACHE |
                            VALIDATE_ALWAYS |
                            VALIDATE_ONCE_PER_SESSION |
                            VALIDATE_NEVER));

    // ... and force the default flags.
    flags |= mDefaultLoadFlags;

    if (flags != oldFlags)
        rv = aRequest->SetLoadFlags(flags);

    outFlags = flags;
    return rv;
}

// nsLoadGroupConnectionInfo

class nsLoadGroupConnectionInfo final : public nsILoadGroupConnectionInfo
{
    ~nsLoadGroupConnectionInfo() {}

public:
    NS_DECL_THREADSAFE_ISUPPORTS
    NS_DECL_NSILOADGROUPCONNECTIONINFO

    nsLoadGroupConnectionInfo();
private:
    Atomic<uint32_t>       mBlockingTransactionCount;
    nsAutoPtr<mozilla::net::SpdyPushCache> mSpdyCache;
};

NS_IMPL_ISUPPORTS(nsLoadGroupConnectionInfo, nsILoadGroupConnectionInfo)

nsLoadGroupConnectionInfo::nsLoadGroupConnectionInfo()
    : mBlockingTransactionCount(0)
{
}

NS_IMETHODIMP
nsLoadGroupConnectionInfo::GetBlockingTransactionCount(uint32_t *aBlockingTransactionCount)
{
    NS_ENSURE_ARG_POINTER(aBlockingTransactionCount);
    *aBlockingTransactionCount = mBlockingTransactionCount;
    return NS_OK;
}

NS_IMETHODIMP
nsLoadGroupConnectionInfo::AddBlockingTransaction()
{
    mBlockingTransactionCount++;
    return NS_OK;
}

NS_IMETHODIMP
nsLoadGroupConnectionInfo::RemoveBlockingTransaction(uint32_t *_retval)
{
    NS_ENSURE_ARG_POINTER(_retval);
        mBlockingTransactionCount--;
        *_retval = mBlockingTransactionCount;
    return NS_OK;
}

/* [noscript] attribute SpdyPushCachePtr spdyPushCache; */
NS_IMETHODIMP
nsLoadGroupConnectionInfo::GetSpdyPushCache(mozilla::net::SpdyPushCache **aSpdyPushCache)
{
    *aSpdyPushCache = mSpdyCache.get();
    return NS_OK;
}

NS_IMETHODIMP
nsLoadGroupConnectionInfo::SetSpdyPushCache(mozilla::net::SpdyPushCache *aSpdyPushCache)
{
    mSpdyCache = aSpdyPushCache;
    return NS_OK;
}

nsresult nsLoadGroup::Init()
{
    static const PLDHashTableOps hash_table_ops =
    {
        PL_DHashVoidPtrKeyStub,
        RequestHashMatchEntry,
        PL_DHashMoveEntryStub,
        RequestHashClearEntry,
        RequestHashInitEntry
    };

    PL_DHashTableInit(&mRequests, &hash_table_ops,
                      sizeof(RequestMapEntry));

    mConnectionInfo = new nsLoadGroupConnectionInfo();

    return NS_OK;
}

#undef LOG
