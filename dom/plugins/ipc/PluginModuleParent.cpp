/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: sw=4 ts=4 et :
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef MOZ_WIDGET_QT
#include "PluginHelperQt.h"
#endif

#include "mozilla/plugins/PluginModuleParent.h"

#include "base/process_util.h"
#include "mozilla/Attributes.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/ipc/MessageChannel.h"
#include "mozilla/plugins/BrowserStreamParent.h"
#include "mozilla/plugins/PluginAsyncSurrogate.h"
#include "mozilla/plugins/PluginBridge.h"
#include "mozilla/plugins/PluginInstanceParent.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProcessHangMonitor.h"
#include "mozilla/Services.h"
#include "mozilla/Telemetry.h"
#include "mozilla/unused.h"
#include "nsAutoPtr.h"
#include "nsCRT.h"
#include "nsIFile.h"
#include "nsIObserverService.h"
#include "nsNPAPIPlugin.h"
#include "nsPrintfCString.h"
#include "prsystem.h"
#include "GeckoProfiler.h"
#include "nsPluginTags.h"

#ifdef XP_WIN
#include "mozilla/widget/AudioSession.h"
#include "nsWindowsHelpers.h"
#include "PluginHangUIParent.h"
#endif

#ifdef MOZ_WIDGET_GTK
#include <glib.h>
#elif XP_MACOSX
#include "PluginInterposeOSX.h"
#include "PluginUtilsOSX.h"
#endif

using base::KillProcess;

using mozilla::PluginLibrary;
using mozilla::ipc::MessageChannel;

using namespace mozilla;
using namespace mozilla::plugins;
using namespace mozilla::plugins::parent;

static const char kContentTimeoutPref[] = "dom.ipc.plugins.contentTimeoutSecs";
static const char kChildTimeoutPref[] = "dom.ipc.plugins.timeoutSecs";
static const char kParentTimeoutPref[] = "dom.ipc.plugins.parentTimeoutSecs";
static const char kLaunchTimeoutPref[] = "dom.ipc.plugins.processLaunchTimeoutSecs";
static const char kAsyncInitPref[] = "dom.ipc.plugins.asyncInit";
#ifdef XP_WIN
static const char kHangUITimeoutPref[] = "dom.ipc.plugins.hangUITimeoutSecs";
static const char kHangUIMinDisplayPref[] = "dom.ipc.plugins.hangUIMinDisplaySecs";
#define CHILD_TIMEOUT_PREF kHangUITimeoutPref
#else
#define CHILD_TIMEOUT_PREF kChildTimeoutPref
#endif

template<>
struct RunnableMethodTraits<mozilla::plugins::PluginModuleParent>
{
    typedef mozilla::plugins::PluginModuleParent Class;
    static void RetainCallee(Class* obj) { }
    static void ReleaseCallee(Class* obj) { }
};

bool
mozilla::plugins::SetupBridge(uint32_t aPluginId,
                              dom::ContentParent* aContentParent,
                              bool aForceBridgeNow,
                              nsresult* rv)
{
    PluginModuleChromeParent::ClearInstantiationFlag();
    nsRefPtr<nsPluginHost> host = nsPluginHost::GetInst();
    nsRefPtr<nsNPAPIPlugin> plugin;
    *rv = host->GetPluginForContentProcess(aPluginId, getter_AddRefs(plugin));
    if (NS_FAILED(*rv)) {
        return true;
    }
    PluginModuleChromeParent* chromeParent = static_cast<PluginModuleChromeParent*>(plugin->GetLibrary());
    chromeParent->SetContentParent(aContentParent);
    if (!aForceBridgeNow && chromeParent->IsStartingAsync() &&
        PluginModuleChromeParent::DidInstantiate()) {
        // We'll handle the bridging asynchronously
        return true;
    }
    return PPluginModule::Bridge(aContentParent, chromeParent);
}

/**
 * Objects of this class remain linked until either an error occurs in the
 * plugin initialization sequence, or until
 * PluginModuleContentParent::OnLoadPluginResult has completed executing.
 */
class PluginModuleMapping : public PRCList
{
public:
    explicit PluginModuleMapping(uint32_t aPluginId)
        : mPluginId(aPluginId)
        , mProcessIdValid(false)
        , mModule(nullptr)
        , mChannelOpened(false)
    {
        MOZ_COUNT_CTOR(PluginModuleMapping);
        PR_INIT_CLIST(this);
        PR_APPEND_LINK(this, &sModuleListHead);
    }

    ~PluginModuleMapping()
    {
        PR_REMOVE_LINK(this);
        MOZ_COUNT_DTOR(PluginModuleMapping);
    }

    bool
    IsChannelOpened() const
    {
        return mChannelOpened;
    }

    void
    SetChannelOpened()
    {
        mChannelOpened = true;
    }

    PluginModuleContentParent*
    GetModule()
    {
        if (!mModule) {
            mModule = new PluginModuleContentParent();
        }
        return mModule;
    }

    static PluginModuleMapping*
    AssociateWithProcessId(uint32_t aPluginId, base::ProcessId aProcessId)
    {
        PluginModuleMapping* mapping =
            static_cast<PluginModuleMapping*>(PR_NEXT_LINK(&sModuleListHead));
        while (mapping != &sModuleListHead) {
            if (mapping->mPluginId == aPluginId) {
                mapping->AssociateWithProcessId(aProcessId);
                return mapping;
            }
            mapping = static_cast<PluginModuleMapping*>(PR_NEXT_LINK(mapping));
        }
        return nullptr;
    }

    static PluginModuleMapping*
    Resolve(base::ProcessId aProcessId)
    {
        PluginModuleMapping* mapping = nullptr;

        if (sIsLoadModuleOnStack) {
            // Special case: If loading synchronously, we just need to access
            // the tail entry of the list.
            mapping =
                static_cast<PluginModuleMapping*>(PR_LIST_TAIL(&sModuleListHead));
            MOZ_ASSERT(mapping);
            return mapping;
        }

        mapping =
            static_cast<PluginModuleMapping*>(PR_NEXT_LINK(&sModuleListHead));
        while (mapping != &sModuleListHead) {
            if (mapping->mProcessIdValid && mapping->mProcessId == aProcessId) {
                return mapping;
            }
            mapping = static_cast<PluginModuleMapping*>(PR_NEXT_LINK(mapping));
        }
        return nullptr;
    }

    static PluginModuleMapping*
    FindModuleByPluginId(uint32_t aPluginId)
    {
        PluginModuleMapping* mapping =
            static_cast<PluginModuleMapping*>(PR_NEXT_LINK(&sModuleListHead));
        while (mapping != &sModuleListHead) {
            if (mapping->mPluginId == aPluginId) {
                return mapping;
            }
            mapping = static_cast<PluginModuleMapping*>(PR_NEXT_LINK(mapping));
        }
        return nullptr;
    }

    static bool
    IsLoadModuleOnStack()
    {
        return sIsLoadModuleOnStack;
    }

    class MOZ_STACK_CLASS NotifyLoadingModule
    {
    public:
        explicit NotifyLoadingModule(MOZ_GUARD_OBJECT_NOTIFIER_ONLY_PARAM)
        {
            MOZ_GUARD_OBJECT_NOTIFIER_INIT;
            PluginModuleMapping::sIsLoadModuleOnStack = true;
        }

        ~NotifyLoadingModule()
        {
            PluginModuleMapping::sIsLoadModuleOnStack = false;
        }

    private:
        MOZ_DECL_USE_GUARD_OBJECT_NOTIFIER
    };

private:
    void
    AssociateWithProcessId(base::ProcessId aProcessId)
    {
        MOZ_ASSERT(!mProcessIdValid);
        mProcessId = aProcessId;
        mProcessIdValid = true;
    }

    uint32_t mPluginId;
    bool mProcessIdValid;
    base::ProcessId mProcessId;
    PluginModuleContentParent* mModule;
    bool mChannelOpened;

    friend class NotifyLoadingModule;

    static PRCList sModuleListHead;
    static bool sIsLoadModuleOnStack;
};

PRCList PluginModuleMapping::sModuleListHead =
    PR_INIT_STATIC_CLIST(&PluginModuleMapping::sModuleListHead);

bool PluginModuleMapping::sIsLoadModuleOnStack = false;

void
mozilla::plugins::TerminatePlugin(uint32_t aPluginId)
{
    MOZ_ASSERT(XRE_GetProcessType() == GeckoProcessType_Default);

    nsRefPtr<nsPluginHost> host = nsPluginHost::GetInst();
    nsPluginTag* pluginTag = host->PluginWithId(aPluginId);
    if (!pluginTag || !pluginTag->mPlugin) {
        return;
    }

    nsRefPtr<nsNPAPIPlugin> plugin = pluginTag->mPlugin;
    PluginModuleChromeParent* chromeParent = static_cast<PluginModuleChromeParent*>(plugin->GetLibrary());
    chromeParent->TerminateChildProcess(MessageLoop::current());
}

/* static */ PluginLibrary*
PluginModuleContentParent::LoadModule(uint32_t aPluginId)
{
    PluginModuleMapping::NotifyLoadingModule loadingModule;
    nsAutoPtr<PluginModuleMapping> mapping(new PluginModuleMapping(aPluginId));

    MOZ_ASSERT(XRE_GetProcessType() == GeckoProcessType_Content);

    /*
     * We send a LoadPlugin message to the chrome process using an intr
     * message. Before it sends its response, it sends a message to create
     * PluginModuleParent instance. That message is handled by
     * PluginModuleContentParent::Initialize, which saves the instance in
     * its module mapping. We fetch it from there after LoadPlugin finishes.
     */
    dom::ContentChild* cp = dom::ContentChild::GetSingleton();
    nsresult rv;
    if (!cp->SendLoadPlugin(aPluginId, &rv) ||
        NS_FAILED(rv)) {
        return nullptr;
    }

    PluginModuleContentParent* parent = mapping->GetModule();
    MOZ_ASSERT(parent);

    if (!mapping->IsChannelOpened()) {
        // mapping is linked into PluginModuleMapping::sModuleListHead and is
        // needed later, so since this function is returning successfully we
        // forget it here.
        mapping.forget();
    }

    parent->mPluginId = aPluginId;

    return parent;
}

/* static */ void
PluginModuleContentParent::AssociatePluginId(uint32_t aPluginId,
                                             base::ProcessId aProcessId)
{
    DebugOnly<PluginModuleMapping*> mapping =
        PluginModuleMapping::AssociateWithProcessId(aPluginId, aProcessId);
    MOZ_ASSERT(mapping);
}

/* static */ PluginModuleContentParent*
PluginModuleContentParent::Initialize(mozilla::ipc::Transport* aTransport,
                                      base::ProcessId aOtherProcess)
{
    nsAutoPtr<PluginModuleMapping> moduleMapping(
        PluginModuleMapping::Resolve(aOtherProcess));
    MOZ_ASSERT(moduleMapping);
    PluginModuleContentParent* parent = moduleMapping->GetModule();
    MOZ_ASSERT(parent);

    ProcessHandle handle;
    if (!base::OpenProcessHandle(aOtherProcess, &handle)) {
        // Bug 1090578 - need to kill |aOtherProcess|, it's boned.
        return nullptr;
    }

    DebugOnly<bool> ok = parent->Open(aTransport, handle, XRE_GetIOMessageLoop(),
                                      mozilla::ipc::ParentSide);
    MOZ_ASSERT(ok);

    moduleMapping->SetChannelOpened();

    // Request Windows message deferral behavior on our channel. This
    // applies to the top level and all sub plugin protocols since they
    // all share the same channel.
    parent->GetIPCChannel()->SetChannelFlags(MessageChannel::REQUIRE_DEFERRED_MESSAGE_PROTECTION);

    TimeoutChanged(kContentTimeoutPref, parent);

    // moduleMapping is linked into PluginModuleMapping::sModuleListHead and is
    // needed later, so since this function is returning successfully we
    // forget it here.
    moduleMapping.forget();
    return parent;
}

/* static */ void
PluginModuleContentParent::OnLoadPluginResult(const uint32_t& aPluginId,
                                              const bool& aResult)
{
    nsAutoPtr<PluginModuleMapping> moduleMapping(
        PluginModuleMapping::FindModuleByPluginId(aPluginId));
    MOZ_ASSERT(moduleMapping);
    PluginModuleContentParent* parent = moduleMapping->GetModule();
    MOZ_ASSERT(parent);
    parent->RecvNP_InitializeResult(aResult ? NPERR_NO_ERROR
                                            : NPERR_GENERIC_ERROR);
}

void
PluginModuleChromeParent::SetContentParent(dom::ContentParent* aContentParent)
{
    MOZ_ASSERT(aContentParent);
    mContentParent = aContentParent;
}

bool
PluginModuleChromeParent::SendAssociatePluginId()
{
    MOZ_ASSERT(mContentParent);
    return mContentParent->SendAssociatePluginId(mPluginId, OtherSidePID());
}

// static
PluginLibrary*
PluginModuleChromeParent::LoadModule(const char* aFilePath, uint32_t aPluginId,
                                     nsPluginTag* aPluginTag)
{
    PLUGIN_LOG_DEBUG_FUNCTION;

    int32_t sandboxLevel = 0;
#if defined(XP_WIN) && defined(MOZ_SANDBOX)
    nsAutoCString sandboxPref("dom.ipc.plugins.sandbox-level.");
    sandboxPref.Append(aPluginTag->GetNiceFileName());
    if (NS_FAILED(Preferences::GetInt(sandboxPref.get(), &sandboxLevel))) {
      sandboxLevel = Preferences::GetInt("dom.ipc.plugins.sandbox-level.default");
    }
#endif

    nsAutoPtr<PluginModuleChromeParent> parent(new PluginModuleChromeParent(aFilePath, aPluginId));
    UniquePtr<LaunchCompleteTask> onLaunchedRunnable(new LaunchedTask(parent));
    parent->mSubprocess->SetCallRunnableImmediately(!parent->mIsStartingAsync);
    TimeStamp launchStart = TimeStamp::Now();
    bool launched = parent->mSubprocess->Launch(Move(onLaunchedRunnable),
                                                sandboxLevel);
    if (!launched) {
        // We never reached open
        parent->mShutdown = true;
        return nullptr;
    }
    parent->mIsFlashPlugin = aPluginTag->mIsFlashPlugin;
    parent->mIsBlocklisted = aPluginTag->GetBlocklistState() != 0;
    if (!parent->mIsStartingAsync) {
        int32_t launchTimeoutSecs = Preferences::GetInt(kLaunchTimeoutPref, 0);
        if (!parent->mSubprocess->WaitUntilConnected(launchTimeoutSecs * 1000)) {
            parent->mShutdown = true;
            return nullptr;
        }
    }
    TimeStamp launchEnd = TimeStamp::Now();
    parent->mTimeBlocked = (launchEnd - launchStart);
    return parent.forget();
}

void
PluginModuleChromeParent::OnProcessLaunched(const bool aSucceeded)
{
    if (!aSucceeded) {
        mShutdown = true;
        OnInitFailure();
        return;
    }
    // We may have already been initialized by another call that was waiting
    // for process connect. If so, this function doesn't need to run.
    if (mAsyncInitRv != NS_ERROR_NOT_INITIALIZED || mShutdown) {
        return;
    }
    Open(mSubprocess->GetChannel(), mSubprocess->GetChildProcessHandle());

    // Request Windows message deferral behavior on our channel. This
    // applies to the top level and all sub plugin protocols since they
    // all share the same channel.
    GetIPCChannel()->SetChannelFlags(MessageChannel::REQUIRE_DEFERRED_MESSAGE_PROTECTION);

    TimeoutChanged(CHILD_TIMEOUT_PREF, this);

    Preferences::RegisterCallback(TimeoutChanged, kChildTimeoutPref, this);
    Preferences::RegisterCallback(TimeoutChanged, kParentTimeoutPref, this);
#ifdef XP_WIN
    Preferences::RegisterCallback(TimeoutChanged, kHangUITimeoutPref, this);
    Preferences::RegisterCallback(TimeoutChanged, kHangUIMinDisplayPref, this);
#endif

#ifdef XP_WIN
    if (!mIsBlocklisted && mIsFlashPlugin &&
        Preferences::GetBool("dom.ipc.plugins.flash.disable-protected-mode", false)) {
        SendDisableFlashProtectedMode();
    }
#endif

    if (mInitOnAsyncConnect) {
        mInitOnAsyncConnect = false;
#if defined(XP_WIN)
        mAsyncInitRv = NP_GetEntryPoints(mNPPIface,
                                         &mAsyncInitError);
        if (NS_SUCCEEDED(mAsyncInitRv))
#endif
        {
#if defined(XP_UNIX) && !defined(XP_MACOSX) && !defined(MOZ_WIDGET_GONK)
            mAsyncInitRv = NP_Initialize(mNPNIface,
                                         mNPPIface,
                                         &mAsyncInitError);
#else
            mAsyncInitRv = NP_Initialize(mNPNIface,
                                         &mAsyncInitError);
#endif
        }

#if defined(XP_MACOSX)
        if (NS_SUCCEEDED(mAsyncInitRv)) {
            mAsyncInitRv = NP_GetEntryPoints(mNPPIface,
                                             &mAsyncInitError);
        }
#endif
    }
}

bool
PluginModuleChromeParent::WaitForIPCConnection()
{
    PluginProcessParent* process = Process();
    MOZ_ASSERT(process);
    process->SetCallRunnableImmediately(true);
    if (!process->WaitUntilConnected()) {
        return false;
    }
    return true;
}

PluginModuleParent::PluginModuleParent(bool aIsChrome)
    : mIsChrome(aIsChrome)
    , mShutdown(false)
    , mClearSiteDataSupported(false)
    , mGetSitesWithDataSupported(false)
    , mNPNIface(nullptr)
    , mNPPIface(nullptr)
    , mPlugin(nullptr)
    , mTaskFactory(this)
    , mIsStartingAsync(false)
    , mNPInitialized(false)
    , mAsyncNewRv(NS_ERROR_NOT_INITIALIZED)
{
#if defined(XP_WIN) || defined(XP_MACOSX) || defined(MOZ_WIDGET_GTK)
    mIsStartingAsync = Preferences::GetBool(kAsyncInitPref, false);
#endif
}

PluginModuleParent::~PluginModuleParent()
{
    if (!OkToCleanup()) {
        NS_RUNTIMEABORT("unsafe destruction");
    }

    if (!mShutdown) {
        NS_WARNING("Plugin host deleted the module without shutting down.");
        NPError err;
        NP_Shutdown(&err);
    }
}

PluginModuleContentParent::PluginModuleContentParent()
    : PluginModuleParent(false)
{
    Preferences::RegisterCallback(TimeoutChanged, kContentTimeoutPref, this);
}

PluginModuleContentParent::~PluginModuleContentParent()
{
    Preferences::UnregisterCallback(TimeoutChanged, kContentTimeoutPref, this);
}

bool PluginModuleChromeParent::sInstantiated = false;

PluginModuleChromeParent::PluginModuleChromeParent(const char* aFilePath, uint32_t aPluginId)
    : PluginModuleParent(true)
    , mSubprocess(new PluginProcessParent(aFilePath))
    , mPluginId(aPluginId)
    , mChromeTaskFactory(this)
    , mHangAnnotationFlags(0)
#ifdef XP_WIN
    , mPluginCpuUsageOnHang()
    , mHangUIParent(nullptr)
    , mHangUIEnabled(true)
    , mIsTimerReset(true)
#endif
    , mInitOnAsyncConnect(false)
    , mAsyncInitRv(NS_ERROR_NOT_INITIALIZED)
    , mAsyncInitError(NPERR_NO_ERROR)
    , mContentParent(nullptr)
    , mIsFlashPlugin(false)
{
    NS_ASSERTION(mSubprocess, "Out of memory!");
    sInstantiated = true;

    RegisterSettingsCallbacks();

    mozilla::HangMonitor::RegisterAnnotator(*this);
}

PluginModuleChromeParent::~PluginModuleChromeParent()
{
    if (!OkToCleanup()) {
        NS_RUNTIMEABORT("unsafe destruction");
    }

    if (!mShutdown) {
        NS_WARNING("Plugin host deleted the module without shutting down.");
        NPError err;
        NP_Shutdown(&err);
    }

    NS_ASSERTION(mShutdown, "NP_Shutdown didn't");

    if (mSubprocess) {
        mSubprocess->Delete();
        mSubprocess = nullptr;
    }

    UnregisterSettingsCallbacks();

    Preferences::UnregisterCallback(TimeoutChanged, kChildTimeoutPref, this);
    Preferences::UnregisterCallback(TimeoutChanged, kParentTimeoutPref, this);
#ifdef XP_WIN
    Preferences::UnregisterCallback(TimeoutChanged, kHangUITimeoutPref, this);
    Preferences::UnregisterCallback(TimeoutChanged, kHangUIMinDisplayPref, this);

    if (mHangUIParent) {
        delete mHangUIParent;
        mHangUIParent = nullptr;
    }
#endif

    mozilla::HangMonitor::UnregisterAnnotator(*this);
}

void
PluginModuleParent::SetChildTimeout(const int32_t aChildTimeout)
{
    int32_t timeoutMs = (aChildTimeout > 0) ? (1000 * aChildTimeout) :
                      MessageChannel::kNoTimeout;
    SetReplyTimeoutMs(timeoutMs);
}

void
PluginModuleParent::TimeoutChanged(const char* aPref, void* aModule)
{
    PluginModuleParent* module = static_cast<PluginModuleParent*>(aModule);

    NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
#ifndef XP_WIN
    if (!strcmp(aPref, kChildTimeoutPref)) {
      MOZ_ASSERT(module->IsChrome());
      // The timeout value used by the parent for children
      int32_t timeoutSecs = Preferences::GetInt(kChildTimeoutPref, 0);
      module->SetChildTimeout(timeoutSecs);
#else
    if (!strcmp(aPref, kChildTimeoutPref) ||
        !strcmp(aPref, kHangUIMinDisplayPref) ||
        !strcmp(aPref, kHangUITimeoutPref)) {
      MOZ_ASSERT(module->IsChrome());
      static_cast<PluginModuleChromeParent*>(module)->EvaluateHangUIState(true);
#endif // XP_WIN
    } else if (!strcmp(aPref, kParentTimeoutPref)) {
      // The timeout value used by the child for its parent
      MOZ_ASSERT(module->IsChrome());
      int32_t timeoutSecs = Preferences::GetInt(kParentTimeoutPref, 0);
      unused << static_cast<PluginModuleChromeParent*>(module)->SendSetParentHangTimeout(timeoutSecs);
    } else if (!strcmp(aPref, kContentTimeoutPref)) {
      MOZ_ASSERT(!module->IsChrome());
      int32_t timeoutSecs = Preferences::GetInt(kContentTimeoutPref, 0);
      module->SetChildTimeout(timeoutSecs);
    }
}

void
PluginModuleChromeParent::CleanupFromTimeout(const bool aFromHangUI)
{
    if (mShutdown) {
      return;
    }

    if (!OkToCleanup()) {
        // there's still plugin code on the C++ stack, try again
        MessageLoop::current()->PostDelayedTask(
            FROM_HERE,
            mChromeTaskFactory.NewRunnableMethod(
                &PluginModuleChromeParent::CleanupFromTimeout, aFromHangUI), 10);
        return;
    }

    /* If the plugin container was terminated by the Plugin Hang UI, 
       then either the I/O thread detects a channel error, or the 
       main thread must set the error (whomever gets there first).
       OTOH, if we terminate and return false from 
       ShouldContinueFromReplyTimeout, then the channel state has 
       already been set to ChannelTimeout and we should call the 
       regular Close function. */
    if (aFromHangUI) {
        GetIPCChannel()->CloseWithError();
    } else {
        Close();
    }
}

#ifdef XP_WIN
namespace {

uint64_t
FileTimeToUTC(const FILETIME& ftime) 
{
  ULARGE_INTEGER li;
  li.LowPart = ftime.dwLowDateTime;
  li.HighPart = ftime.dwHighDateTime;
  return li.QuadPart;
}

struct CpuUsageSamples
{
  uint64_t sampleTimes[2];
  uint64_t cpuTimes[2];
};

bool 
GetProcessCpuUsage(const InfallibleTArray<base::ProcessHandle>& processHandles, InfallibleTArray<float>& cpuUsage)
{
  InfallibleTArray<CpuUsageSamples> samples(processHandles.Length());
  FILETIME creationTime, exitTime, kernelTime, userTime, currentTime;
  BOOL res;

  for (uint32_t i = 0; i < processHandles.Length(); ++i) {
    ::GetSystemTimeAsFileTime(&currentTime);
    res = ::GetProcessTimes(processHandles[i], &creationTime, &exitTime, &kernelTime, &userTime);
    if (!res) {
      NS_WARNING("failed to get process times");
      return false;
    }
  
    CpuUsageSamples s;
    s.sampleTimes[0] = FileTimeToUTC(currentTime);
    s.cpuTimes[0]    = FileTimeToUTC(kernelTime) + FileTimeToUTC(userTime);
    samples.AppendElement(s);
  }

  // we already hung for a while, a little bit longer won't matter
  ::Sleep(50);

  const int32_t numberOfProcessors = PR_GetNumberOfProcessors();

  for (uint32_t i = 0; i < processHandles.Length(); ++i) {
    ::GetSystemTimeAsFileTime(&currentTime);
    res = ::GetProcessTimes(processHandles[i], &creationTime, &exitTime, &kernelTime, &userTime);
    if (!res) {
      NS_WARNING("failed to get process times");
      return false;
    }

    samples[i].sampleTimes[1] = FileTimeToUTC(currentTime);
    samples[i].cpuTimes[1]    = FileTimeToUTC(kernelTime) + FileTimeToUTC(userTime);    

    const uint64_t deltaSampleTime = samples[i].sampleTimes[1] - samples[i].sampleTimes[0];
    const uint64_t deltaCpuTime    = samples[i].cpuTimes[1]    - samples[i].cpuTimes[0];
    const float usage = 100.f * (float(deltaCpuTime) / deltaSampleTime) / numberOfProcessors;
    cpuUsage.AppendElement(usage);
  }

  return true;
}

} // anonymous namespace

#endif // #ifdef XP_WIN

void
PluginModuleChromeParent::EnteredCxxStack()
{
    mHangAnnotationFlags |= kInPluginCall;
}

void
PluginModuleChromeParent::ExitedCxxStack()
{
    mHangAnnotationFlags = 0;
#ifdef XP_WIN
    FinishHangUI();
#endif
}

/**
 * This function is always called by the HangMonitor thread.
 */
void
PluginModuleChromeParent::AnnotateHang(mozilla::HangMonitor::HangAnnotations& aAnnotations)
{
    uint32_t flags = mHangAnnotationFlags;
    if (flags) {
        /* We don't actually annotate anything specifically for kInPluginCall;
           we use it to determine whether to annotate other things. It will
           be pretty obvious from the ChromeHang stack that we're in a plugin
           call when the hang occurred. */
        if (flags & kHangUIShown) {
            aAnnotations.AddAnnotation(NS_LITERAL_STRING("HangUIShown"),
                                       true);
        }
        if (flags & kHangUIContinued) {
            aAnnotations.AddAnnotation(NS_LITERAL_STRING("HangUIContinued"),
                                       true);
        }
        if (flags & kHangUIDontShow) {
            aAnnotations.AddAnnotation(NS_LITERAL_STRING("HangUIDontShow"),
                                       true);
        }
        aAnnotations.AddAnnotation(NS_LITERAL_STRING("pluginName"), mPluginName);
        aAnnotations.AddAnnotation(NS_LITERAL_STRING("pluginVersion"),
                                   mPluginVersion);
    }
}

bool
PluginModuleChromeParent::ShouldContinueFromReplyTimeout()
{
    if (mIsFlashPlugin) {
        MessageLoop::current()->PostTask(
            FROM_HERE,
            mTaskFactory.NewRunnableMethod(
                &PluginModuleChromeParent::NotifyFlashHang));
    }

#ifdef XP_WIN
    if (LaunchHangUI()) {
        return true;
    }
    // If LaunchHangUI returned false then we should proceed with the 
    // original plugin hang behaviour and kill the plugin container.
    FinishHangUI();
#endif // XP_WIN
    TerminateChildProcess(MessageLoop::current());
    GetIPCChannel()->CloseWithTimeout();
    return false;
}

bool
PluginModuleContentParent::ShouldContinueFromReplyTimeout()
{
    nsRefPtr<ProcessHangMonitor> monitor = ProcessHangMonitor::Get();
    if (!monitor) {
        return true;
    }
    monitor->NotifyPluginHang(mPluginId);
    return true;
}

void
PluginModuleContentParent::OnExitedSyncSend()
{
    ProcessHangMonitor::ClearHang();
}

void
PluginModuleChromeParent::TerminateChildProcess(MessageLoop* aMsgLoop)
{
#ifdef XP_WIN
    // collect cpu usage for plugin processes

    InfallibleTArray<base::ProcessHandle> processHandles;

    processHandles.AppendElement(OtherProcess());

    if (!GetProcessCpuUsage(processHandles, mPluginCpuUsageOnHang)) {
      mPluginCpuUsageOnHang.Clear();
    }
#endif

    // this must run before the error notification from the channel,
    // or not at all
    bool isFromHangUI = aMsgLoop != MessageLoop::current();
    aMsgLoop->PostTask(
        FROM_HERE,
        mChromeTaskFactory.NewRunnableMethod(
            &PluginModuleChromeParent::CleanupFromTimeout, isFromHangUI));

    if (!KillProcess(OtherProcess(), 1, false))
        NS_WARNING("failed to kill subprocess!");
}

bool
PluginModuleParent::GetPluginDetails(nsACString& aPluginName,
                                     nsACString& aPluginVersion)
{
    nsRefPtr<nsPluginHost> host = nsPluginHost::GetInst();
    if (!host) {
        return false;
    }
    nsPluginTag* pluginTag = host->TagForPlugin(mPlugin);
    if (!pluginTag) {
        return false;
    }
    aPluginName = pluginTag->mName;
    aPluginVersion = pluginTag->mVersion;
    return true;
}

#ifdef XP_WIN
void
PluginModuleChromeParent::EvaluateHangUIState(const bool aReset)
{
    int32_t minDispSecs = Preferences::GetInt(kHangUIMinDisplayPref, 10);
    int32_t autoStopSecs = Preferences::GetInt(kChildTimeoutPref, 0);
    int32_t timeoutSecs = 0;
    if (autoStopSecs > 0 && autoStopSecs < minDispSecs) {
        /* If we're going to automatically terminate the plugin within a 
           time frame shorter than minDispSecs, there's no point in 
           showing the hang UI; it would just flash briefly on the screen. */
        mHangUIEnabled = false;
    } else {
        timeoutSecs = Preferences::GetInt(kHangUITimeoutPref, 0);
        mHangUIEnabled = timeoutSecs > 0;
    }
    if (mHangUIEnabled) {
        if (aReset) {
            mIsTimerReset = true;
            SetChildTimeout(timeoutSecs);
            return;
        } else if (mIsTimerReset) {
            /* The Hang UI is being shown, so now we're setting the 
               timeout to kChildTimeoutPref while we wait for a user 
               response. ShouldContinueFromReplyTimeout will fire 
               after (reply timeout / 2) seconds, which is not what 
               we want. Doubling the timeout value here so that we get 
               the right result. */
            autoStopSecs *= 2;
        }
    }
    mIsTimerReset = false;
    SetChildTimeout(autoStopSecs);
}

bool
PluginModuleChromeParent::LaunchHangUI()
{
    if (!mHangUIEnabled) {
        return false;
    }
    if (mHangUIParent) {
        if (mHangUIParent->IsShowing()) {
            // We've already shown the UI but the timeout has expired again.
            return false;
        }
        if (mHangUIParent->DontShowAgain()) {
            mHangAnnotationFlags |= kHangUIDontShow;
            bool wasLastHangStopped = mHangUIParent->WasLastHangStopped();
            if (!wasLastHangStopped) {
                mHangAnnotationFlags |= kHangUIContinued;
            }
            return !wasLastHangStopped;
        }
        delete mHangUIParent;
        mHangUIParent = nullptr;
    }
    mHangUIParent = new PluginHangUIParent(this, 
            Preferences::GetInt(kHangUITimeoutPref, 0),
            Preferences::GetInt(kChildTimeoutPref, 0));
    bool retval = mHangUIParent->Init(NS_ConvertUTF8toUTF16(mPluginName));
    if (retval) {
        mHangAnnotationFlags |= kHangUIShown;
        /* Once the UI is shown we switch the timeout over to use 
           kChildTimeoutPref, allowing us to terminate a hung plugin 
           after kChildTimeoutPref seconds if the user doesn't respond to 
           the hang UI. */
        EvaluateHangUIState(false);
    }
    return retval;
}

void
PluginModuleChromeParent::FinishHangUI()
{
    if (mHangUIEnabled && mHangUIParent) {
        bool needsCancel = mHangUIParent->IsShowing();
        // If we're still showing, send a Cancel notification
        if (needsCancel) {
            mHangUIParent->Cancel();
        }
        /* If we cancelled the UI or if the user issued a response,
           we need to reset the child process timeout. */
        if (needsCancel ||
            (!mIsTimerReset && mHangUIParent->WasShown())) {
            /* We changed the timeout to kChildTimeoutPref when the plugin hang
               UI was displayed. Now that we're finishing the UI, we need to 
               switch it back to kHangUITimeoutPref. */
            EvaluateHangUIState(true);
        }
    }
}

void
PluginModuleChromeParent::OnHangUIContinue()
{
    mHangAnnotationFlags |= kHangUIContinued;
}
#endif // XP_WIN

void
PluginModuleParent::ActorDestroy(ActorDestroyReason why)
{
    switch (why) {
    case AbnormalShutdown: {
        mShutdown = true;
        // Defer the PluginCrashed method so that we don't re-enter
        // and potentially modify the actor child list while enumerating it.
        if (mPlugin)
            MessageLoop::current()->PostTask(
                FROM_HERE,
                mTaskFactory.NewRunnableMethod(
                    &PluginModuleParent::NotifyPluginCrashed));
        break;
    }
    case NormalShutdown:
        mShutdown = true;
        break;

    default:
        NS_RUNTIMEABORT("Unexpected shutdown reason for toplevel actor.");
    }
}

void
PluginModuleChromeParent::ActorDestroy(ActorDestroyReason why)
{
    if (why == AbnormalShutdown) {
        Telemetry::Accumulate(Telemetry::SUBPROCESS_ABNORMAL_ABORT,
                              NS_LITERAL_CSTRING("plugin"), 1);
    }

    // We can't broadcast settings changes anymore.
    UnregisterSettingsCallbacks();

    PluginModuleParent::ActorDestroy(why);
}

void
PluginModuleParent::NotifyFlashHang()
{
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    if (obs) {
        obs->NotifyObservers(nullptr, "flash-plugin-hang", nullptr);
    }
}

void
PluginModuleParent::NotifyPluginCrashed()
{
    if (!OkToCleanup()) {
        // there's still plugin code on the C++ stack.  try again
        MessageLoop::current()->PostDelayedTask(
            FROM_HERE,
            mTaskFactory.NewRunnableMethod(
                &PluginModuleParent::NotifyPluginCrashed), 10);
        return;
    }

    if (mPlugin)
        mPlugin->PluginCrashed(mPluginDumpID, mBrowserDumpID);
}

PPluginInstanceParent*
PluginModuleParent::AllocPPluginInstanceParent(const nsCString& aMimeType,
                                               const uint16_t& aMode,
                                               const InfallibleTArray<nsCString>& aNames,
                                               const InfallibleTArray<nsCString>& aValues)
{
    NS_ERROR("Not reachable!");
    return nullptr;
}

bool
PluginModuleParent::DeallocPPluginInstanceParent(PPluginInstanceParent* aActor)
{
    PLUGIN_LOG_DEBUG_METHOD;
    delete aActor;
    return true;
}

void
PluginModuleParent::SetPluginFuncs(NPPluginFuncs* aFuncs)
{
    aFuncs->version = (NP_VERSION_MAJOR << 8) | NP_VERSION_MINOR;
    aFuncs->javaClass = nullptr;

    // Gecko should always call these functions through a PluginLibrary object.
    aFuncs->newp = nullptr;
    aFuncs->clearsitedata = nullptr;
    aFuncs->getsiteswithdata = nullptr;

    aFuncs->destroy = NPP_Destroy;
    aFuncs->setwindow = NPP_SetWindow;
    aFuncs->newstream = NPP_NewStream;
    aFuncs->destroystream = NPP_DestroyStream;
    aFuncs->asfile = NPP_StreamAsFile;
    aFuncs->writeready = NPP_WriteReady;
    aFuncs->write = NPP_Write;
    aFuncs->print = NPP_Print;
    aFuncs->event = NPP_HandleEvent;
    aFuncs->urlnotify = NPP_URLNotify;
    aFuncs->getvalue = NPP_GetValue;
    aFuncs->setvalue = NPP_SetValue;
    aFuncs->gotfocus = nullptr;
    aFuncs->lostfocus = nullptr;
    aFuncs->urlredirectnotify = nullptr;

    // Provide 'NPP_URLRedirectNotify', 'NPP_ClearSiteData', and
    // 'NPP_GetSitesWithData' functionality if it is supported by the plugin.
    bool urlRedirectSupported = false;
    unused << CallOptionalFunctionsSupported(&urlRedirectSupported,
                                             &mClearSiteDataSupported,
                                             &mGetSitesWithDataSupported);
    if (urlRedirectSupported) {
      aFuncs->urlredirectnotify = NPP_URLRedirectNotify;
    }
}

#define RESOLVE_AND_CALL(instance, func)                                       \
NP_BEGIN_MACRO                                                                 \
    PluginAsyncSurrogate* surrogate = nullptr;                                 \
    PluginInstanceParent* i = PluginInstanceParent::Cast(instance, &surrogate);\
    if (surrogate && (!i || i->UseSurrogate())) {                              \
        return surrogate->func;                                                \
    }                                                                          \
    if (!i) {                                                                  \
        return NPERR_GENERIC_ERROR;                                            \
    }                                                                          \
    return i->func;                                                            \
NP_END_MACRO

NPError
PluginModuleParent::NPP_Destroy(NPP instance,
                                NPSavedData** saved)
{
    // FIXME/cjones:
    //  (1) send a "destroy" message to the child
    //  (2) the child shuts down its instance
    //  (3) remove both parent and child IDs from map
    //  (4) free parent

    PLUGIN_LOG_DEBUG_FUNCTION;
    PluginAsyncSurrogate* surrogate = nullptr;
    PluginInstanceParent* parentInstance =
        PluginInstanceParent::Cast(instance, &surrogate);
    if (surrogate && (!parentInstance || parentInstance->UseSurrogate())) {
        return surrogate->NPP_Destroy(saved);
    }

    if (!parentInstance)
        return NPERR_NO_ERROR;

    NPError retval = parentInstance->Destroy();
    instance->pdata = nullptr;

    unused << PluginInstanceParent::Call__delete__(parentInstance);
    return retval;
}

NPError
PluginModuleParent::NPP_NewStream(NPP instance, NPMIMEType type,
                                  NPStream* stream, NPBool seekable,
                                  uint16_t* stype)
{
    PROFILER_LABEL("PluginModuleParent", "NPP_NewStream",
      js::ProfileEntry::Category::OTHER);
    RESOLVE_AND_CALL(instance, NPP_NewStream(type, stream, seekable, stype));
}

NPError
PluginModuleParent::NPP_SetWindow(NPP instance, NPWindow* window)
{
    RESOLVE_AND_CALL(instance, NPP_SetWindow(window));
}

NPError
PluginModuleParent::NPP_DestroyStream(NPP instance,
                                      NPStream* stream,
                                      NPReason reason)
{
    RESOLVE_AND_CALL(instance, NPP_DestroyStream(stream, reason));
}

int32_t
PluginModuleParent::NPP_WriteReady(NPP instance,
                                   NPStream* stream)
{
    PluginAsyncSurrogate* surrogate = nullptr;
    BrowserStreamParent* s = StreamCast(instance, stream, &surrogate);
    if (!s) {
        if (surrogate) {
            return surrogate->NPP_WriteReady(stream);
        }
        return -1;
    }

    return s->WriteReady();
}

int32_t
PluginModuleParent::NPP_Write(NPP instance,
                              NPStream* stream,
                              int32_t offset,
                              int32_t len,
                              void* buffer)
{
    BrowserStreamParent* s = StreamCast(instance, stream);
    if (!s)
        return -1;

    return s->Write(offset, len, buffer);
}

void
PluginModuleParent::NPP_StreamAsFile(NPP instance,
                                     NPStream* stream,
                                     const char* fname)
{
    BrowserStreamParent* s = StreamCast(instance, stream);
    if (!s)
        return;

    s->StreamAsFile(fname);
}

void
PluginModuleParent::NPP_Print(NPP instance, NPPrint* platformPrint)
{

    PluginInstanceParent* i = PluginInstanceParent::Cast(instance);
    i->NPP_Print(platformPrint);
}

int16_t
PluginModuleParent::NPP_HandleEvent(NPP instance, void* event)
{
    RESOLVE_AND_CALL(instance, NPP_HandleEvent(event));
}

void
PluginModuleParent::NPP_URLNotify(NPP instance, const char* url,
                                  NPReason reason, void* notifyData)
{
    PluginInstanceParent* i = PluginInstanceParent::Cast(instance);
    if (!i)
        return;

    i->NPP_URLNotify(url, reason, notifyData);
}

NPError
PluginModuleParent::NPP_GetValue(NPP instance,
                                 NPPVariable variable, void *ret_value)
{
    // The rules are slightly different for this function.
    // If there is a surrogate, we *always* use it.
    PluginAsyncSurrogate* surrogate = nullptr;
    PluginInstanceParent* i = PluginInstanceParent::Cast(instance, &surrogate);
    if (surrogate) {
        return surrogate->NPP_GetValue(variable, ret_value);
    }
    if (!i) {
        return NPERR_GENERIC_ERROR;
    }
    return i->NPP_GetValue(variable, ret_value);
}

NPError
PluginModuleParent::NPP_SetValue(NPP instance, NPNVariable variable,
                                 void *value)
{
    RESOLVE_AND_CALL(instance, NPP_SetValue(variable, value));
}

bool
PluginModuleParent::RecvBackUpXResources(const FileDescriptor& aXSocketFd)
{
#ifndef MOZ_X11
    NS_RUNTIMEABORT("This message only makes sense on X11 platforms");
#else
    MOZ_ASSERT(0 > mPluginXSocketFdDup.get(),
               "Already backed up X resources??");
    mPluginXSocketFdDup.forget();
    if (aXSocketFd.IsValid()) {
      mPluginXSocketFdDup.reset(aXSocketFd.PlatformHandle());
    }
#endif
    return true;
}

void
PluginModuleParent::NPP_URLRedirectNotify(NPP instance, const char* url,
                                          int32_t status, void* notifyData)
{
  PluginInstanceParent* i = PluginInstanceParent::Cast(instance);
  if (!i)
    return;

  i->NPP_URLRedirectNotify(url, status, notifyData);
}

BrowserStreamParent*
PluginModuleParent::StreamCast(NPP instance, NPStream* s,
                               PluginAsyncSurrogate** aSurrogate)
{
    PluginInstanceParent* ip = PluginInstanceParent::Cast(instance, aSurrogate);
    if (!ip || (aSurrogate && *aSurrogate && ip->UseSurrogate())) {
        return nullptr;
    }

    BrowserStreamParent* sp =
        static_cast<BrowserStreamParent*>(static_cast<AStream*>(s->pdata));
    if (sp->mNPP != ip || s != sp->mStream) {
        NS_RUNTIMEABORT("Corrupted plugin stream data.");
    }
    return sp;
}

bool
PluginModuleParent::HasRequiredFunctions()
{
    return true;
}

nsresult
PluginModuleParent::AsyncSetWindow(NPP instance, NPWindow* window)
{
    PluginAsyncSurrogate* surrogate = nullptr;
    PluginInstanceParent* i = PluginInstanceParent::Cast(instance, &surrogate);
    if (surrogate && (!i || i->UseSurrogate())) {
        return surrogate->AsyncSetWindow(window);
    } else if (!i) {
        return NS_ERROR_FAILURE;
    }
    return i->AsyncSetWindow(window);
}

nsresult
PluginModuleParent::GetImageContainer(NPP instance,
                             mozilla::layers::ImageContainer** aContainer)
{
    PluginInstanceParent* i = PluginInstanceParent::Cast(instance);
    return !i ? NS_ERROR_FAILURE : i->GetImageContainer(aContainer);
}

nsresult
PluginModuleParent::GetImageSize(NPP instance,
                                 nsIntSize* aSize)
{
    PluginInstanceParent* i = PluginInstanceParent::Cast(instance);
    return !i ? NS_ERROR_FAILURE : i->GetImageSize(aSize);
}

nsresult
PluginModuleParent::SetBackgroundUnknown(NPP instance)
{
    PluginInstanceParent* i = PluginInstanceParent::Cast(instance);
    if (!i)
        return NS_ERROR_FAILURE;

    return i->SetBackgroundUnknown();
}

nsresult
PluginModuleParent::BeginUpdateBackground(NPP instance,
                                          const nsIntRect& aRect,
                                          gfxContext** aCtx)
{
    PluginInstanceParent* i = PluginInstanceParent::Cast(instance);
    if (!i)
        return NS_ERROR_FAILURE;

    return i->BeginUpdateBackground(aRect, aCtx);
}

nsresult
PluginModuleParent::EndUpdateBackground(NPP instance,
                                        gfxContext* aCtx,
                                        const nsIntRect& aRect)
{
    PluginInstanceParent* i = PluginInstanceParent::Cast(instance);
    if (!i)
        return NS_ERROR_FAILURE;

    return i->EndUpdateBackground(aCtx, aRect);
}

void
PluginModuleParent::OnInitFailure()
{
    if (GetIPCChannel()->CanSend()) {
        Close();
    }

    mShutdown = true;

    if (mIsStartingAsync) {
        /* If we've failed then we need to enumerate any pending NPP_New calls
           and clean them up. */
        uint32_t len = mSurrogateInstances.Length();
        for (uint32_t i = 0; i < len; ++i) {
            mSurrogateInstances[i]->NotifyAsyncInitFailed();
        }
        mSurrogateInstances.Clear();
    }
}

class OfflineObserver final : public nsIObserver
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIOBSERVER

    explicit OfflineObserver(PluginModuleChromeParent* pmp)
      : mPmp(pmp)
    {}

private:
    ~OfflineObserver() {}
    PluginModuleChromeParent* mPmp;
};

NS_IMPL_ISUPPORTS(OfflineObserver, nsIObserver)

NS_IMETHODIMP
OfflineObserver::Observe(nsISupports *aSubject,
                         const char *aTopic,
                         const char16_t *aData)
{
    MOZ_ASSERT(!strcmp(aTopic, "ipc:network:set-offline"));
    mPmp->CachedSettingChanged();
    return NS_OK;
}

static const char* kSettingsPrefs[] =
    {"javascript.enabled",
     "dom.ipc.plugins.nativeCursorSupport"};

void
PluginModuleChromeParent::RegisterSettingsCallbacks()
{
    for (size_t i = 0; i < ArrayLength(kSettingsPrefs); i++) {
        Preferences::RegisterCallback(CachedSettingChanged, kSettingsPrefs[i], this);
    }

    nsCOMPtr<nsIObserverService> observerService = mozilla::services::GetObserverService();
    if (observerService) {
        mOfflineObserver = new OfflineObserver(this);
        observerService->AddObserver(mOfflineObserver, "ipc:network:set-offline", false);
    }
}

void
PluginModuleChromeParent::UnregisterSettingsCallbacks()
{
    for (size_t i = 0; i < ArrayLength(kSettingsPrefs); i++) {
        Preferences::UnregisterCallback(CachedSettingChanged, kSettingsPrefs[i], this);
    }

    nsCOMPtr<nsIObserverService> observerService = mozilla::services::GetObserverService();
    if (observerService) {
        observerService->RemoveObserver(mOfflineObserver, "ipc:network:set-offline");
        mOfflineObserver = nullptr;
    }
}

bool
PluginModuleParent::GetSetting(NPNVariable aVariable)
{
    NPBool boolVal = false;
    mozilla::plugins::parent::_getvalue(nullptr, aVariable, &boolVal);
    return boolVal;
}

void
PluginModuleParent::GetSettings(PluginSettings* aSettings)
{
    aSettings->javascriptEnabled() = GetSetting(NPNVjavascriptEnabledBool);
    aSettings->asdEnabled() = GetSetting(NPNVasdEnabledBool);
    aSettings->isOffline() = GetSetting(NPNVisOfflineBool);
    aSettings->supportsXembed() = GetSetting(NPNVSupportsXEmbedBool);
    aSettings->supportsWindowless() = GetSetting(NPNVSupportsWindowless);
    aSettings->userAgent() = NullableString(mNPNIface->uagent(nullptr));

#if defined(XP_MACOSX)
    aSettings->nativeCursorsSupported() =
      Preferences::GetBool("dom.ipc.plugins.nativeCursorSupport", false);
#else
    // Need to initialize this to satisfy IPDL.
    aSettings->nativeCursorsSupported() = false;
#endif
}

void
PluginModuleChromeParent::CachedSettingChanged()
{
    PluginSettings settings;
    GetSettings(&settings);
    unused << SendSettingChanged(settings);
}

/* static */ void
PluginModuleChromeParent::CachedSettingChanged(const char* aPref, void* aModule)
{
    PluginModuleChromeParent *module = static_cast<PluginModuleChromeParent*>(aModule);
    module->CachedSettingChanged();
}

#if defined(XP_UNIX) && !defined(XP_MACOSX) && !defined(MOZ_WIDGET_GONK)
nsresult
PluginModuleParent::NP_Initialize(NPNetscapeFuncs* bFuncs, NPPluginFuncs* pFuncs, NPError* error)
{
    PLUGIN_LOG_DEBUG_METHOD;

    mNPNIface = bFuncs;
    mNPPIface = pFuncs;

    if (mShutdown) {
        *error = NPERR_GENERIC_ERROR;
        return NS_ERROR_FAILURE;
    }

    *error = NPERR_NO_ERROR;
    if (mIsStartingAsync) {
        if (GetIPCChannel()->CanSend()) {
            // We're already connected, so we may call this immediately.
            RecvNP_InitializeResult(*error);
        } else {
            PluginAsyncSurrogate::NP_GetEntryPoints(pFuncs);
        }
    } else {
        SetPluginFuncs(pFuncs);
    }

    return NS_OK;
}

nsresult
PluginModuleChromeParent::NP_Initialize(NPNetscapeFuncs* bFuncs, NPPluginFuncs* pFuncs, NPError* error)
{
    PLUGIN_LOG_DEBUG_METHOD;

    if (mShutdown) {
        *error = NPERR_GENERIC_ERROR;
        return NS_ERROR_FAILURE;
    }

    *error = NPERR_NO_ERROR;

    mNPNIface = bFuncs;
    mNPPIface = pFuncs;

    // NB: This *MUST* be set prior to checking whether the subprocess has
    // been connected!
    if (mIsStartingAsync) {
        PluginAsyncSurrogate::NP_GetEntryPoints(pFuncs);
    }

    if (!mSubprocess->IsConnected()) {
        // The subprocess isn't connected yet. Defer NP_Initialize until
        // OnProcessLaunched is invoked.
        mInitOnAsyncConnect = true;
        return NS_OK;
    }

    PluginSettings settings;
    GetSettings(&settings);

    TimeStamp callNpInitStart = TimeStamp::Now();
    // Asynchronous case
    if (mIsStartingAsync) {
        if (!SendAsyncNP_Initialize(settings)) {
            Close();
            return NS_ERROR_FAILURE;
        }
        TimeStamp callNpInitEnd = TimeStamp::Now();
        mTimeBlocked += (callNpInitEnd - callNpInitStart);
        return NS_OK;
    }

    // Synchronous case
    if (!CallNP_Initialize(settings, error)) {
        Close();
        return NS_ERROR_FAILURE;
    }
    else if (*error != NPERR_NO_ERROR) {
        Close();
        return NS_ERROR_FAILURE;
    }
    TimeStamp callNpInitEnd = TimeStamp::Now();
    mTimeBlocked += (callNpInitEnd - callNpInitStart);

    RecvNP_InitializeResult(*error);

    return NS_OK;
}

bool
PluginModuleParent::RecvNP_InitializeResult(const NPError& aError)
{
    if (aError != NPERR_NO_ERROR) {
        OnInitFailure();
        return true;
    }

    SetPluginFuncs(mNPPIface);
    if (mIsStartingAsync) {
        InitAsyncSurrogates();
    }

    mNPInitialized = true;
    return true;
}

bool
PluginModuleChromeParent::RecvNP_InitializeResult(const NPError& aError)
{
    if (!mContentParent) {
        return PluginModuleParent::RecvNP_InitializeResult(aError);
    }
    bool initOk = aError == NPERR_NO_ERROR;
    if (initOk) {
        SetPluginFuncs(mNPPIface);
        if (mIsStartingAsync && !SendAssociatePluginId()) {
            initOk = false;
        }
    }
    mNPInitialized = initOk;
    return mContentParent->SendLoadPluginResult(mPluginId, initOk);
}

#else

nsresult
PluginModuleParent::NP_Initialize(NPNetscapeFuncs* bFuncs, NPError* error)
{
    PLUGIN_LOG_DEBUG_METHOD;

    mNPNIface = bFuncs;

    if (mShutdown) {
        *error = NPERR_GENERIC_ERROR;
        return NS_ERROR_FAILURE;
    }

    *error = NPERR_NO_ERROR;
    return NS_OK;
}

#if defined(XP_WIN) || defined(XP_MACOSX)

nsresult
PluginModuleContentParent::NP_Initialize(NPNetscapeFuncs* bFuncs, NPError* error)
{
    PLUGIN_LOG_DEBUG_METHOD;
    nsresult rv = PluginModuleParent::NP_Initialize(bFuncs, error);
    if (mIsStartingAsync && GetIPCChannel()->CanSend()) {
        // We're already connected, so we may call this immediately.
        RecvNP_InitializeResult(*error);
    }
    return rv;
}

#endif

nsresult
PluginModuleChromeParent::NP_Initialize(NPNetscapeFuncs* bFuncs, NPError* error)
{
    nsresult rv = PluginModuleParent::NP_Initialize(bFuncs, error);
    if (NS_FAILED(rv))
        return rv;

#if defined(XP_MACOSX)
    if (!mSubprocess->IsConnected()) {
        // The subprocess isn't connected yet. Defer NP_Initialize until
        // OnProcessLaunched is invoked.
        mInitOnAsyncConnect = true;
        *error = NPERR_NO_ERROR;
        return NS_OK;
    }
#else
    if (mInitOnAsyncConnect) {
        *error = NPERR_NO_ERROR;
        return NS_OK;
    }
#endif

    PluginSettings settings;
    GetSettings(&settings);

    TimeStamp callNpInitStart = TimeStamp::Now();
    if (mIsStartingAsync) {
        if (!SendAsyncNP_Initialize(settings)) {
            return NS_ERROR_FAILURE;
        }
        TimeStamp callNpInitEnd = TimeStamp::Now();
        mTimeBlocked += (callNpInitEnd - callNpInitStart);
        return NS_OK;
    }

    if (!CallNP_Initialize(settings, error)) {
        Close();
        return NS_ERROR_FAILURE;
    }
    TimeStamp callNpInitEnd = TimeStamp::Now();
    mTimeBlocked += (callNpInitEnd - callNpInitStart);
    RecvNP_InitializeResult(*error);
    return NS_OK;
}

bool
PluginModuleParent::RecvNP_InitializeResult(const NPError& aError)
{
    if (aError != NPERR_NO_ERROR) {
        OnInitFailure();
        return true;
    }

    if (mIsStartingAsync) {
        SetPluginFuncs(mNPPIface);
        InitAsyncSurrogates();
    }

    mNPInitialized = true;
    return true;
}

bool
PluginModuleChromeParent::RecvNP_InitializeResult(const NPError& aError)
{
    bool ok = true;
    if (mContentParent) {
        if ((ok = SendAssociatePluginId())) {
            ok = mContentParent->SendLoadPluginResult(mPluginId,
                                                      aError == NPERR_NO_ERROR);
        }
    } else if (aError == NPERR_NO_ERROR) {
        // Initialization steps when e10s is disabled
#if defined XP_WIN
        if (mIsStartingAsync) {
            SetPluginFuncs(mNPPIface);
        }

        // Send the info needed to join the chrome process's audio session to the
        // plugin process
        nsID id;
        nsString sessionName;
        nsString iconPath;

        if (NS_SUCCEEDED(mozilla::widget::GetAudioSessionData(id, sessionName,
                                                              iconPath))) {
            unused << SendSetAudioSessionData(id, sessionName, iconPath);
        }
#endif

    }

    return PluginModuleParent::RecvNP_InitializeResult(aError) && ok;
}

#endif

void
PluginModuleParent::InitAsyncSurrogates()
{
    uint32_t len = mSurrogateInstances.Length();
    for (uint32_t i = 0; i < len; ++i) {
        NPError err;
        mAsyncNewRv = mSurrogateInstances[i]->NPP_New(&err);
        if (NS_FAILED(mAsyncNewRv)) {
            mSurrogateInstances[i]->NotifyAsyncInitFailed();
            continue;
        }
    }
    mSurrogateInstances.Clear();
}

bool
PluginModuleParent::RemovePendingSurrogate(
                            const nsRefPtr<PluginAsyncSurrogate>& aSurrogate)
{
    return mSurrogateInstances.RemoveElement(aSurrogate);
}

nsresult
PluginModuleParent::NP_Shutdown(NPError* error)
{
    PLUGIN_LOG_DEBUG_METHOD;

    if (mShutdown) {
        *error = NPERR_GENERIC_ERROR;
        return NS_ERROR_FAILURE;
    }

    bool ok = true;
    if (IsChrome()) {
        ok = CallNP_Shutdown(error);
    }

    // if NP_Shutdown() is nested within another interrupt call, this will
    // break things.  but lord help us if we're doing that anyway; the
    // plugin dso will have been unloaded on the other side by the
    // CallNP_Shutdown() message
    Close();

    return ok ? NS_OK : NS_ERROR_FAILURE;
}

nsresult
PluginModuleParent::NP_GetMIMEDescription(const char** mimeDesc)
{
    PLUGIN_LOG_DEBUG_METHOD;

    *mimeDesc = "application/x-foobar";
    return NS_OK;
}

nsresult
PluginModuleParent::NP_GetValue(void *future, NPPVariable aVariable,
                                   void *aValue, NPError* error)
{
    PR_LOG(GetPluginLog(), PR_LOG_WARNING, ("%s Not implemented, requested variable %i", __FUNCTION__,
                                        (int) aVariable));

    //TODO: implement this correctly
    *error = NPERR_GENERIC_ERROR;
    return NS_OK;
}

#if defined(XP_WIN) || defined(XP_MACOSX)
nsresult
PluginModuleParent::NP_GetEntryPoints(NPPluginFuncs* pFuncs, NPError* error)
{
    NS_ASSERTION(pFuncs, "Null pointer!");

    *error = NPERR_NO_ERROR;
    if (mIsStartingAsync && !IsChrome()) {
        PluginAsyncSurrogate::NP_GetEntryPoints(pFuncs);
        mNPPIface = pFuncs;
    } else {
        SetPluginFuncs(pFuncs);
    }

    return NS_OK;
}

nsresult
PluginModuleChromeParent::NP_GetEntryPoints(NPPluginFuncs* pFuncs, NPError* error)
{
#if defined(XP_MACOSX)
    if (mInitOnAsyncConnect) {
        PluginAsyncSurrogate::NP_GetEntryPoints(pFuncs);
        mNPPIface = pFuncs;
        *error = NPERR_NO_ERROR;
        return NS_OK;
    }
#else
    if (mIsStartingAsync) {
        PluginAsyncSurrogate::NP_GetEntryPoints(pFuncs);
    }
    if (!mSubprocess->IsConnected()) {
        mNPPIface = pFuncs;
        mInitOnAsyncConnect = true;
        *error = NPERR_NO_ERROR;
        return NS_OK;
    }
#endif

    // We need to have the plugin process update its function table here by
    // actually calling NP_GetEntryPoints. The parent's function table will
    // reflect nullptr entries in the child's table once SetPluginFuncs is
    // called.

    if (!CallNP_GetEntryPoints(error)) {
        return NS_ERROR_FAILURE;
    }
    else if (*error != NPERR_NO_ERROR) {
        return NS_OK;
    }

    return PluginModuleParent::NP_GetEntryPoints(pFuncs, error);
}

#endif

nsresult
PluginModuleParent::NPP_New(NPMIMEType pluginType, NPP instance,
                            uint16_t mode, int16_t argc, char* argn[],
                            char* argv[], NPSavedData* saved,
                            NPError* error)
{
    PLUGIN_LOG_DEBUG_METHOD;

    if (mShutdown) {
        *error = NPERR_GENERIC_ERROR;
        return NS_ERROR_FAILURE;
    }

    if (mIsStartingAsync) {
        if (!PluginAsyncSurrogate::Create(this, pluginType, instance, mode,
                                          argc, argn, argv)) {
            *error = NPERR_GENERIC_ERROR;
            return NS_ERROR_FAILURE;
        }

        if (!mNPInitialized) {
            nsRefPtr<PluginAsyncSurrogate> surrogate =
                PluginAsyncSurrogate::Cast(instance);
            mSurrogateInstances.AppendElement(surrogate);
            *error = NPERR_NO_ERROR;
            return NS_PLUGIN_INIT_PENDING;
        }
    }

    if (mPluginName.IsEmpty()) {
        GetPluginDetails(mPluginName, mPluginVersion);
        /** mTimeBlocked measures the time that the main thread has been blocked
         *  on plugin module initialization. As implemented, this is the sum of
         *  plugin-container launch + toolhelp32 snapshot + NP_Initialize.
         *  We don't accumulate its value until here because the plugin info
         *  is not available until *after* NP_Initialize.
         */
        Telemetry::Accumulate(Telemetry::BLOCKED_ON_PLUGIN_MODULE_INIT_MS,
                              GetHistogramKey(),
                              static_cast<uint32_t>(mTimeBlocked.ToMilliseconds()));
        mTimeBlocked = TimeDuration();
    }

    // create the instance on the other side
    InfallibleTArray<nsCString> names;
    InfallibleTArray<nsCString> values;

    for (int i = 0; i < argc; ++i) {
        names.AppendElement(NullableString(argn[i]));
        values.AppendElement(NullableString(argv[i]));
    }

    nsresult rv = NPP_NewInternal(pluginType, instance, mode, names, values,
                                  saved, error);
    if (NS_FAILED(rv) || !mIsStartingAsync) {
        return rv;
    }
    return NS_PLUGIN_INIT_PENDING;
}

nsresult
PluginModuleParent::NPP_NewInternal(NPMIMEType pluginType, NPP instance,
                                    uint16_t mode,
                                    InfallibleTArray<nsCString>& names,
                                    InfallibleTArray<nsCString>& values,
                                    NPSavedData* saved, NPError* error)
{
    PluginInstanceParent* parentInstance =
        new PluginInstanceParent(this, instance,
                                 nsDependentCString(pluginType), mNPNIface);

    if (!parentInstance->Init()) {
        delete parentInstance;
        return NS_ERROR_FAILURE;
    }

    // Release the surrogate reference that was in pdata
    nsRefPtr<PluginAsyncSurrogate> surrogate(
        dont_AddRef(PluginAsyncSurrogate::Cast(instance)));
    // Now replace it with the instance
    instance->pdata = static_cast<PluginDataResolver*>(parentInstance);

    if (!SendPPluginInstanceConstructor(parentInstance,
                                        nsDependentCString(pluginType), mode,
                                        names, values)) {
        // |parentInstance| is automatically deleted.
        instance->pdata = nullptr;
        *error = NPERR_GENERIC_ERROR;
        return NS_ERROR_FAILURE;
    }

    {   // Scope for timer
        Telemetry::AutoTimer<Telemetry::BLOCKED_ON_PLUGIN_INSTANCE_INIT_MS>
            timer(GetHistogramKey());
        if (mIsStartingAsync) {
            MOZ_ASSERT(surrogate);
            surrogate->AsyncCallDeparting();
            if (!SendAsyncNPP_New(parentInstance)) {
                *error = NPERR_GENERIC_ERROR;
                return NS_ERROR_FAILURE;
            }
            *error = NPERR_NO_ERROR;
        } else {
            if (!CallSyncNPP_New(parentInstance, error)) {
                // if IPC is down, we'll get an immediate "failed" return, but
                // without *error being set.  So make sure that the error
                // condition is signaled to nsNPAPIPluginInstance
                if (NPERR_NO_ERROR == *error) {
                    *error = NPERR_GENERIC_ERROR;
                }
                return NS_ERROR_FAILURE;
            }
        }
    }

    if (*error != NPERR_NO_ERROR) {
        if (!mIsStartingAsync) {
            NPP_Destroy(instance, 0);
        }
        return NS_ERROR_FAILURE;
    }

    UpdatePluginTimeout();

    return NS_OK;
}

void
PluginModuleChromeParent::UpdatePluginTimeout()
{
    TimeoutChanged(kParentTimeoutPref, this);
}

nsresult
PluginModuleParent::NPP_ClearSiteData(const char* site, uint64_t flags,
                                      uint64_t maxAge)
{
    if (!mClearSiteDataSupported)
        return NS_ERROR_NOT_AVAILABLE;

    NPError result;
    if (!CallNPP_ClearSiteData(NullableString(site), flags, maxAge, &result))
        return NS_ERROR_FAILURE;

    switch (result) {
    case NPERR_NO_ERROR:
        return NS_OK;
    case NPERR_TIME_RANGE_NOT_SUPPORTED:
        return NS_ERROR_PLUGIN_TIME_RANGE_NOT_SUPPORTED;
    case NPERR_MALFORMED_SITE:
        return NS_ERROR_INVALID_ARG;
    default:
        return NS_ERROR_FAILURE;
    }
}

nsresult
PluginModuleParent::NPP_GetSitesWithData(InfallibleTArray<nsCString>& result)
{
    if (!mGetSitesWithDataSupported)
        return NS_ERROR_NOT_AVAILABLE;

    if (!CallNPP_GetSitesWithData(&result))
        return NS_ERROR_FAILURE;

    return NS_OK;
}

#if defined(XP_MACOSX)
nsresult
PluginModuleParent::IsRemoteDrawingCoreAnimation(NPP instance, bool *aDrawing)
{
    PluginInstanceParent* i = PluginInstanceParent::Cast(instance);
    if (!i)
        return NS_ERROR_FAILURE;

    return i->IsRemoteDrawingCoreAnimation(aDrawing);
}

nsresult
PluginModuleParent::ContentsScaleFactorChanged(NPP instance, double aContentsScaleFactor)
{
    PluginInstanceParent* i = PluginInstanceParent::Cast(instance);
    if (!i)
        return NS_ERROR_FAILURE;

    return i->ContentsScaleFactorChanged(aContentsScaleFactor);
}
#endif // #if defined(XP_MACOSX)

#if defined(MOZ_WIDGET_QT)
bool
PluginModuleParent::AnswerProcessSomeEvents()
{
    PLUGIN_LOG_DEBUG(("Spinning mini nested loop ..."));
    PluginHelperQt::AnswerProcessSomeEvents();
    PLUGIN_LOG_DEBUG(("... quitting mini nested loop"));

    return true;
}

#elif defined(XP_MACOSX)
bool
PluginModuleParent::AnswerProcessSomeEvents()
{
    mozilla::plugins::PluginUtilsOSX::InvokeNativeEventLoop();
    return true;
}

#elif !defined(MOZ_WIDGET_GTK)
bool
PluginModuleParent::AnswerProcessSomeEvents()
{
    NS_RUNTIMEABORT("unreached");
    return false;
}

#else
static const int kMaxChancesToProcessEvents = 20;

bool
PluginModuleParent::AnswerProcessSomeEvents()
{
    PLUGIN_LOG_DEBUG(("Spinning mini nested loop ..."));

    int i = 0;
    for (; i < kMaxChancesToProcessEvents; ++i)
        if (!g_main_context_iteration(nullptr, FALSE))
            break;

    PLUGIN_LOG_DEBUG(("... quitting mini nested loop; processed %i tasks", i));

    return true;
}
#endif

bool
PluginModuleParent::RecvProcessNativeEventsInInterruptCall()
{
    PLUGIN_LOG_DEBUG(("%s", FULLFUNCTION));
#if defined(OS_WIN)
    ProcessNativeEventsInInterruptCall();
    return true;
#else
    NS_NOTREACHED(
        "PluginModuleParent::RecvProcessNativeEventsInInterruptCall not implemented!");
    return false;
#endif
}

void
PluginModuleParent::ProcessRemoteNativeEventsInInterruptCall()
{
#if defined(OS_WIN)
    unused << SendProcessNativeEventsInInterruptCall();
    return;
#endif
    NS_NOTREACHED(
        "PluginModuleParent::ProcessRemoteNativeEventsInInterruptCall not implemented!");
}

bool
PluginModuleParent::RecvPluginShowWindow(const uint32_t& aWindowId, const bool& aModal,
                                         const int32_t& aX, const int32_t& aY,
                                         const size_t& aWidth, const size_t& aHeight)
{
    PLUGIN_LOG_DEBUG(("%s", FULLFUNCTION));
#if defined(XP_MACOSX)
    CGRect windowBound = ::CGRectMake(aX, aY, aWidth, aHeight);
    mac_plugin_interposing::parent::OnPluginShowWindow(aWindowId, windowBound, aModal);
    return true;
#else
    NS_NOTREACHED(
        "PluginInstanceParent::RecvPluginShowWindow not implemented!");
    return false;
#endif
}

bool
PluginModuleParent::RecvPluginHideWindow(const uint32_t& aWindowId)
{
    PLUGIN_LOG_DEBUG(("%s", FULLFUNCTION));
#if defined(XP_MACOSX)
    mac_plugin_interposing::parent::OnPluginHideWindow(aWindowId, OtherSidePID());
    return true;
#else
    NS_NOTREACHED(
        "PluginInstanceParent::RecvPluginHideWindow not implemented!");
    return false;
#endif
}

bool
PluginModuleParent::RecvSetCursor(const NSCursorInfo& aCursorInfo)
{
    PLUGIN_LOG_DEBUG(("%s", FULLFUNCTION));
#if defined(XP_MACOSX)
    mac_plugin_interposing::parent::OnSetCursor(aCursorInfo);
    return true;
#else
    NS_NOTREACHED(
        "PluginInstanceParent::RecvSetCursor not implemented!");
    return false;
#endif
}

bool
PluginModuleParent::RecvShowCursor(const bool& aShow)
{
    PLUGIN_LOG_DEBUG(("%s", FULLFUNCTION));
#if defined(XP_MACOSX)
    mac_plugin_interposing::parent::OnShowCursor(aShow);
    return true;
#else
    NS_NOTREACHED(
        "PluginInstanceParent::RecvShowCursor not implemented!");
    return false;
#endif
}

bool
PluginModuleParent::RecvPushCursor(const NSCursorInfo& aCursorInfo)
{
    PLUGIN_LOG_DEBUG(("%s", FULLFUNCTION));
#if defined(XP_MACOSX)
    mac_plugin_interposing::parent::OnPushCursor(aCursorInfo);
    return true;
#else
    NS_NOTREACHED(
        "PluginInstanceParent::RecvPushCursor not implemented!");
    return false;
#endif
}

bool
PluginModuleParent::RecvPopCursor()
{
    PLUGIN_LOG_DEBUG(("%s", FULLFUNCTION));
#if defined(XP_MACOSX)
    mac_plugin_interposing::parent::OnPopCursor();
    return true;
#else
    NS_NOTREACHED(
        "PluginInstanceParent::RecvPopCursor not implemented!");
    return false;
#endif
}

bool
PluginModuleParent::RecvNPN_SetException(const nsCString& aMessage)
{
    PLUGIN_LOG_DEBUG(("%s", FULLFUNCTION));

    // This function ignores its first argument.
    mozilla::plugins::parent::_setexception(nullptr, NullableStringGet(aMessage));
    return true;
}

bool
PluginModuleParent::RecvNPN_ReloadPlugins(const bool& aReloadPages)
{
    PLUGIN_LOG_DEBUG(("%s", FULLFUNCTION));

    mozilla::plugins::parent::_reloadplugins(aReloadPages);
    return true;
}

bool
PluginModuleChromeParent::RecvNotifyContentModuleDestroyed()
{
    nsRefPtr<nsPluginHost> host = nsPluginHost::GetInst();
    if (host) {
        host->NotifyContentModuleDestroyed(mPluginId);
    }
    return true;
}
