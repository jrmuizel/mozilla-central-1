/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set et sw=4 ts=4: */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is written by Juan Lang.
 *
 * The Initial Developer of the Original Code is 
 * Juan Lang.
 * Portions created by the Initial Developer are Copyright (C) 2003,2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or 
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <ole2.h>
#include <netcon.h>
#include <objbase.h>
#include <iprtrmib.h>
#include "prmem.h"
#include "plstr.h"
#include "nsThreadUtils.h"
#include "nsIObserverService.h"
#include "nsServiceManagerUtils.h"
#include "nsNotifyAddrListener.h"
#include "nsString.h"
#include "nsAutoPtr.h"
#include "mozilla/Services.h"
#include "nsCRT.h"

#include <iptypes.h>
#include <iphlpapi.h>

typedef void (WINAPI *NcFreeNetconPropertiesFunc)(NETCON_PROPERTIES*);

static HMODULE sNetshell;
static NcFreeNetconPropertiesFunc sNcFreeNetconProperties;

static void InitNetshellLibrary(void)
{
    if (!sNetshell) {
        sNetshell = LoadLibraryW(L"Netshell.dll");
        if (sNetshell) {
            sNcFreeNetconProperties = (NcFreeNetconPropertiesFunc)
                GetProcAddress(sNetshell, "NcFreeNetconProperties");
        }
    }
}

static void FreeDynamicLibraries(void)
{
    if (sNetshell) {
        sNcFreeNetconProperties = nsnull;
        FreeLibrary(sNetshell);
        sNetshell = nsnull;
    }
}

NS_IMPL_THREADSAFE_ISUPPORTS3(nsNotifyAddrListener,
                              nsINetworkLinkService,
                              nsIRunnable,
                              nsIObserver)

nsNotifyAddrListener::nsNotifyAddrListener()
    : mLinkUp(true)  // assume true by default
    , mStatusKnown(false)
    , mCheckAttempted(false)
    , mShutdownEvent(nsnull)
{
}

nsNotifyAddrListener::~nsNotifyAddrListener()
{
    NS_ASSERTION(!mThread, "nsNotifyAddrListener thread shutdown failed");
    FreeDynamicLibraries();
}

NS_IMETHODIMP
nsNotifyAddrListener::GetIsLinkUp(bool *aIsUp)
{
    if (!mCheckAttempted && !mStatusKnown) {
        mCheckAttempted = true;
        CheckLinkStatus();
    }

    *aIsUp = mLinkUp;
    return NS_OK;
}

NS_IMETHODIMP
nsNotifyAddrListener::GetLinkStatusKnown(bool *aIsUp)
{
    *aIsUp = mStatusKnown;
    return NS_OK;
}

NS_IMETHODIMP
nsNotifyAddrListener::GetLinkType(PRUint32 *aLinkType)
{
  NS_ENSURE_ARG_POINTER(aLinkType);

  // XXX This function has not yet been implemented for this platform
  *aLinkType = nsINetworkLinkService::LINK_TYPE_UNKNOWN;
  return NS_OK;
}

NS_IMETHODIMP
nsNotifyAddrListener::Run()
{
    HANDLE ev = CreateEvent(nsnull, FALSE, FALSE, nsnull);
    NS_ENSURE_TRUE(ev, NS_ERROR_OUT_OF_MEMORY);

    HANDLE handles[2] = { ev, mShutdownEvent };
    OVERLAPPED overlapped = { 0 };
    bool shuttingDown = false;

    overlapped.hEvent = ev;
    while (!shuttingDown) {
        HANDLE h;
        DWORD ret = NotifyAddrChange(&h, &overlapped);

        if (ret == ERROR_IO_PENDING) {
            ret = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            if (ret == WAIT_OBJECT_0) {
                CheckLinkStatus();
            } else {
                shuttingDown = true;
            }
        } else {
            shuttingDown = true;
        }
    }
    CloseHandle(ev);

    return NS_OK;
}

NS_IMETHODIMP
nsNotifyAddrListener::Observe(nsISupports *subject,
                              const char *topic,
                              const PRUnichar *data)
{
    if (!strcmp("xpcom-shutdown-threads", topic))
        Shutdown();

    return NS_OK;
}

nsresult
nsNotifyAddrListener::Init(void)
{
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    if (!observerService)
        return NS_ERROR_FAILURE;

    nsresult rv = observerService->AddObserver(this, "xpcom-shutdown-threads",
                                               false);
    NS_ENSURE_SUCCESS(rv, rv);

    mShutdownEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    NS_ENSURE_TRUE(mShutdownEvent, NS_ERROR_OUT_OF_MEMORY);

    rv = NS_NewThread(getter_AddRefs(mThread), this);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
}

nsresult
nsNotifyAddrListener::Shutdown(void)
{
    // remove xpcom shutdown observer
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    if (observerService)
        observerService->RemoveObserver(this, "xpcom-shutdown-threads");

    if (!mShutdownEvent)
        return NS_OK;

    SetEvent(mShutdownEvent);

    nsresult rv = mThread->Shutdown();

    // Have to break the cycle here, otherwise nsNotifyAddrListener holds
    // onto the thread and the thread holds onto the nsNotifyAddrListener
    // via its mRunnable
    mThread = nsnull;

    CloseHandle(mShutdownEvent);
    mShutdownEvent = NULL;

    return rv;
}

/* Sends the given event to the UI thread.  Assumes aEventID never goes out
 * of scope (static strings are ideal).
 */
nsresult
nsNotifyAddrListener::SendEventToUI(const char *aEventID)
{
    if (!aEventID)
        return NS_ERROR_NULL_POINTER;

    nsresult rv;
    nsCOMPtr<nsIRunnable> event = new ChangeEvent(this, aEventID);
    if (NS_FAILED(rv = NS_DispatchToMainThread(event)))
        NS_WARNING("Failed to dispatch ChangeEvent");
    return rv;
}

NS_IMETHODIMP
nsNotifyAddrListener::ChangeEvent::Run()
{
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    if (observerService)
        observerService->NotifyObservers(
                mService, NS_NETWORK_LINK_TOPIC,
                NS_ConvertASCIItoUTF16(mEventID).get());
    return NS_OK;
}

bool
nsNotifyAddrListener::CheckIsGateway(PIP_ADAPTER_ADDRESSES aAdapter)
{
    if (!aAdapter->FirstUnicastAddress)
        return false;

    LPSOCKADDR aAddress = aAdapter->FirstUnicastAddress->Address.lpSockaddr;
    if (!aAddress)
        return false;

    PSOCKADDR_IN in_addr = (PSOCKADDR_IN)aAddress;
    bool isGateway = (aAddress->sa_family == AF_INET &&
        in_addr->sin_addr.S_un.S_un_b.s_b1 == 192 &&
        in_addr->sin_addr.S_un.S_un_b.s_b2 == 168 &&
        in_addr->sin_addr.S_un.S_un_b.s_b3 == 0 &&
        in_addr->sin_addr.S_un.S_un_b.s_b4 == 1);

    if (isGateway)
      isGateway = CheckICSStatus(aAdapter->FriendlyName);

    return isGateway;
}

bool
nsNotifyAddrListener::CheckICSStatus(PWCHAR aAdapterName)
{
    InitNetshellLibrary();

    // This method enumerates all privately shared connections and checks if some
    // of them has the same name as the one provided in aAdapterName. If such
    // connection is found in the collection the adapter is used as ICS gateway
    bool isICSGatewayAdapter = false;

    HRESULT hr;
    nsRefPtr<INetSharingManager> netSharingManager;
    hr = CoCreateInstance(
                CLSID_NetSharingManager,
                NULL,
                CLSCTX_INPROC_SERVER,
                IID_INetSharingManager,
                getter_AddRefs(netSharingManager));

    nsRefPtr<INetSharingPrivateConnectionCollection> privateCollection;
    if (SUCCEEDED(hr)) {
        hr = netSharingManager->get_EnumPrivateConnections(
                    ICSSC_DEFAULT,
                    getter_AddRefs(privateCollection));
    }

    nsRefPtr<IEnumNetSharingPrivateConnection> privateEnum;
    if (SUCCEEDED(hr)) {
        nsRefPtr<IUnknown> privateEnumUnknown;
        hr = privateCollection->get__NewEnum(getter_AddRefs(privateEnumUnknown));
        if (SUCCEEDED(hr)) {
            hr = privateEnumUnknown->QueryInterface(
                        IID_IEnumNetSharingPrivateConnection,
                        getter_AddRefs(privateEnum));
        }
    }

    if (SUCCEEDED(hr)) {
        ULONG fetched;
        VARIANT connectionVariant;
        while (!isICSGatewayAdapter &&
               SUCCEEDED(hr = privateEnum->Next(1, &connectionVariant,
               &fetched)) &&
               fetched) {
            if (connectionVariant.vt != VT_UNKNOWN) {
                // We should call VariantClear here but it needs to link
                // with oleaut32.lib that produces a Ts incrase about 10ms
                // that is undesired. As it is quit unlikely the result would
                // be of a different type anyway, let's pass the variant
                // unfreed here.
                NS_ERROR("Variant of unexpected type, expecting VT_UNKNOWN, we probably leak it!");
                continue;
            }

            nsRefPtr<INetConnection> connection;
            if (SUCCEEDED(connectionVariant.punkVal->QueryInterface(
                              IID_INetConnection,
                              getter_AddRefs(connection)))) {
                connectionVariant.punkVal->Release();

                NETCON_PROPERTIES *properties;
                if (SUCCEEDED(connection->GetProperties(&properties))) {
                    if (!wcscmp(properties->pszwName, aAdapterName))
                        isICSGatewayAdapter = true;

                    if (sNcFreeNetconProperties)
                        sNcFreeNetconProperties(properties);
                }
            }
        }
    }

    return isICSGatewayAdapter;
}

DWORD
nsNotifyAddrListener::CheckAdaptersAddresses(void)
{
    static const DWORD flags =
        GAA_FLAG_SKIP_FRIENDLY_NAME | GAA_FLAG_SKIP_ANYCAST |
        GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;

    ULONG len = 16384;

    PIP_ADAPTER_ADDRESSES addresses = (PIP_ADAPTER_ADDRESSES) malloc(len);
    if (!addresses)
        return ERROR_OUTOFMEMORY;

    DWORD ret = GetAdaptersAddresses(AF_UNSPEC, 0, NULL, addresses, &len);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        free(addresses);
        addresses = (PIP_ADAPTER_ADDRESSES) malloc(len);
        if (!addresses)
            return ERROR_BUFFER_OVERFLOW;
        ret = GetAdaptersAddresses(AF_UNSPEC, 0, NULL, addresses, &len);
    }

    if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) {
        free(addresses);
        return ERROR_NOT_SUPPORTED;
    }

    if (ret == ERROR_SUCCESS) {
        PIP_ADAPTER_ADDRESSES ptr;
        bool linkUp = false;

        for (ptr = addresses; !linkUp && ptr; ptr = ptr->Next) {
            if (ptr->OperStatus == IfOperStatusUp &&
                    ptr->IfType != IF_TYPE_SOFTWARE_LOOPBACK &&
                    !CheckIsGateway(ptr))
                linkUp = true;
        }
        mLinkUp = linkUp;
        mStatusKnown = true;
    }
    free(addresses);

    CoUninitialize();

    return ret;
}

/**
 * Checks the status of all network adapters.  If one is up and has a valid IP
 * address, sets mLinkUp to true.  Sets mStatusKnown to true if the link status
 * is definitive.
 */
void
nsNotifyAddrListener::CheckLinkStatus(void)
{
    DWORD ret;
    const char *event;

    // This call is very expensive (~650 milliseconds), so we don't want to
    // call it synchronously. Instead, we just start up assuming we have a
    // network link, but we'll report that the status is unknown.
    if (NS_IsMainThread()) {
        NS_WARNING("CheckLinkStatus called on main thread! No check "
                   "performed. Assuming link is up, status is unknown.");
        mLinkUp = true;
    } else {
        ret = CheckAdaptersAddresses();
        if (ret != ERROR_SUCCESS) {
            mLinkUp = true;
        }
    }

    if (mStatusKnown)
        event = mLinkUp ? NS_NETWORK_LINK_DATA_UP : NS_NETWORK_LINK_DATA_DOWN;
    else
        event = NS_NETWORK_LINK_DATA_UNKNOWN;
    SendEventToUI(event);
}
