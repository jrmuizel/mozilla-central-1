/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is Netscape Communications
 * Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s): 
 */

#include "nsCOMPtr.h"
#include "nsWalletService.h"
#include "nsIServiceManager.h"
#include "wallet.h"
#include "singsign.h"
#include "nsIObserverService.h"
#include "nsIDOMHTMLDocument.h"
#include "nsIDOMHTMLCollection.h"
#include "nsIDOMHTMLFormElement.h"
#include "nsIContent.h"
#include "nsIDocument.h"
#include "nsIDocumentLoader.h"
#include "nsIWebShell.h"
#include "nsIDocumentViewer.h"
#include "nsIDOMHTMLInputElement.h"
#include "nsIFormControl.h"

static NS_DEFINE_IID(kIWalletServiceIID, NS_IWALLETSERVICE_IID);
static NS_DEFINE_IID(kIDOMHTMLFormElementIID, NS_IDOMHTMLFORMELEMENT_IID);
static NS_DEFINE_IID(kIFormSubmitObserverIID, NS_IFORMSUBMITOBSERVER_IID);
static NS_DEFINE_IID(kIDOMHTMLDocumentIID, NS_IDOMHTMLDOCUMENT_IID);
static NS_DEFINE_IID(kDocLoaderServiceCID, NS_DOCUMENTLOADER_SERVICE_CID);
static NS_DEFINE_IID(kIDocumentLoaderIID, NS_IDOCUMENTLOADER_IID);


nsWalletlibService::nsWalletlibService()
{
  NS_INIT_REFCNT();
  Init();
}

nsWalletlibService::~nsWalletlibService()
{
}

NS_IMETHODIMP
nsWalletlibService::QueryInterface(REFNSIID iid, void** result)
{
  if (! result) {
    return NS_ERROR_NULL_POINTER;
  }
  *result = nsnull;

  if (iid.Equals(NS_GET_IID(nsISupports)) || iid.Equals(kIWalletServiceIID)) {
    *result = NS_STATIC_CAST(nsIWalletService*, this);
    AddRef();
    return NS_OK;
  }
  if (iid.Equals(kIFormSubmitObserverIID)) {
    *result = NS_STATIC_CAST(nsIFormSubmitObserver*, this);
    AddRef();
    return NS_OK;
  }
  if (iid.Equals(nsIDocumentLoaderObserver::GetIID())) {
    *result = (void*) ((nsIDocumentLoaderObserver*)this);
    AddRef();
    return NS_OK;
  }
  return NS_NOINTERFACE;
}

NS_IMPL_ADDREF(nsWalletlibService);
NS_IMPL_RELEASE(nsWalletlibService);

NS_IMETHODIMP nsWalletlibService::WALLET_PreEdit(nsAutoString& walletList) {
  ::WLLT_PreEdit(walletList);
  return NS_OK;
}

NS_IMETHODIMP nsWalletlibService::WALLET_PostEdit(nsAutoString walletList) {
  ::WLLT_PostEdit(walletList);
  return NS_OK;
}

NS_IMETHODIMP nsWalletlibService::WALLET_ChangePassword() {
  ::WLLT_ChangePassword();
  return NS_OK;
}

NS_IMETHODIMP nsWalletlibService::WALLET_RequestToCapture(nsIPresShell* shell) {
  ::WLLT_RequestToCapture(shell);
  return NS_OK;
}

NS_IMETHODIMP nsWalletlibService::WALLET_Prefill(nsIPresShell* shell, PRBool quick) {
  return ::WLLT_Prefill(shell, quick);
}

NS_IMETHODIMP nsWalletlibService::WALLET_PrefillReturn(nsAutoString results){
  ::WLLT_PrefillReturn(results);
  return NS_OK;
}

NS_IMETHODIMP nsWalletlibService::WALLET_FetchFromNetCenter(){
  ::WLLT_FetchFromNetCenter();
  return NS_OK;
}

NS_IMETHODIMP nsWalletlibService::PromptUsernameAndPasswordURL
    (const PRUnichar *text, PRUnichar **user, PRUnichar **pwd,
     const char *urlname, nsIPrompt* dialog, PRBool *returnValue) {
  return ::SINGSIGN_PromptUsernameAndPassword(text, user, pwd, urlname, dialog, returnValue);
}

NS_IMETHODIMP nsWalletlibService::PromptPasswordURL
    (const PRUnichar *text, PRUnichar **pwd, const char *urlname, nsIPrompt* dialog, PRBool *returnValue) {
  return ::SINGSIGN_PromptPassword(text, pwd, urlname, dialog, returnValue);
}

NS_IMETHODIMP nsWalletlibService::PromptURL
    (const PRUnichar *text, const PRUnichar *defaultText, PRUnichar **resultText,
     const char *urlname, nsIPrompt* dialog, PRBool *returnValue) {
  return ::SINGSIGN_Prompt(text, defaultText, resultText, urlname, dialog, returnValue);
}

NS_IMETHODIMP nsWalletlibService::SI_RemoveUser(const char *URLName, char *userName) {
  ::SINGSIGN_RemoveUser(URLName, userName);
  return NS_OK;
}


NS_IMETHODIMP nsWalletlibService::WALLET_GetNopreviewListForViewer(nsString& aNopreviewList){
  ::WLLT_GetNopreviewListForViewer(aNopreviewList);
  return NS_OK;
}

NS_IMETHODIMP nsWalletlibService::WALLET_GetNocaptureListForViewer(nsString& aNocaptureList){
  ::WLLT_GetNocaptureListForViewer(aNocaptureList);
  return NS_OK;
}

NS_IMETHODIMP nsWalletlibService::WALLET_GetPrefillListForViewer(nsString& aPrefillList){
  ::WLLT_GetPrefillListForViewer(aPrefillList);
  return NS_OK;
}

NS_IMETHODIMP nsWalletlibService::SI_GetSignonListForViewer(nsString& aSignonList){
  ::SINGSIGN_GetSignonListForViewer(aSignonList);
  return NS_OK;
}

NS_IMETHODIMP nsWalletlibService::SI_GetRejectListForViewer(nsString& aRejectList){
  ::SINGSIGN_GetRejectListForViewer(aRejectList);
  return NS_OK;
}

NS_IMETHODIMP nsWalletlibService::SI_SignonViewerReturn(nsAutoString results){
  ::SINGSIGN_SignonViewerReturn(results);
  return NS_OK;
}

NS_IMETHODIMP nsWalletlibService::Observe(nsISupports*, const PRUnichar*, const PRUnichar*) 
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

#define CRLF "\015\012"   
NS_IMETHODIMP nsWalletlibService::Notify(nsIContent* formNode) 
{
  if (!formNode) {
    return NS_ERROR_FAILURE;
  }
  ::WLLT_OnSubmit(formNode);
  return NS_OK;
}

void nsWalletlibService::Init() 
{
  nsIObserverService *svc = 0;
  nsIDocumentLoader *docLoaderService;

  nsresult rv = nsServiceManager::GetService
    (NS_OBSERVERSERVICE_PROGID, nsIObserverService::GetIID(), (nsISupports**)&svc );
  if ( NS_SUCCEEDED( rv ) && svc ) {
    nsString  topic(NS_FORMSUBMIT_SUBJECT);
    rv = svc->AddObserver( this, topic.GetUnicode());
    nsServiceManager::ReleaseService( NS_OBSERVERSERVICE_PROGID, svc );
  }

  // Get the global document loader service...  
  rv = nsServiceManager::GetService
    (kDocLoaderServiceCID, kIDocumentLoaderIID, (nsISupports **)&docLoaderService);
  if (NS_SUCCEEDED(rv) && docLoaderService) {
    //Register ourselves as an observer for the new doc loader
    docLoaderService->AddObserver((nsIDocumentLoaderObserver*)this);
    nsServiceManager::ReleaseService(kDocLoaderServiceCID, docLoaderService );
  }
}

NS_IMETHODIMP
nsWalletlibService::OnStartDocumentLoad(nsIDocumentLoader* aLoader, nsIURI* aURL, const char* aCommand)
{
  return NS_OK;
}

#include "prmem.h"

NS_IMETHODIMP
nsWalletlibService::OnEndDocumentLoad(nsIDocumentLoader* aLoader, nsIChannel* channel, nsresult aStatus,
									nsIDocumentLoaderObserver * aObserver)
{
  nsresult rv = NS_OK;

  if (aLoader == nsnull) {
    return rv;
  }
  nsCOMPtr<nsISupports> cont;
  rv = aLoader->GetContainer(getter_AddRefs(cont));
  if (NS_FAILED(rv) || (cont == nsnull)) {
    return rv;
  }
  nsCOMPtr<nsIWebShell> ws(do_QueryInterface(cont));
  NS_ENSURE_TRUE(ws, NS_ERROR_FAILURE);

  nsCOMPtr<nsIContentViewer> cv;
  rv = ws->GetContentViewer(getter_AddRefs(cv));
  if (NS_FAILED(rv) || (cv == nsnull)) {
    return rv;
  }
  nsCOMPtr<nsIDocumentViewer> docViewer(do_QueryInterface(cv));
  NS_ENSURE_TRUE(docViewer, NS_ERROR_FAILURE);

  nsCOMPtr<nsIDocument> doc;
  rv = docViewer->GetDocument(*getter_AddRefs(doc));
  if (NS_FAILED(rv) || (doc == nsnull)) {
    return rv;
  }
  
  /* get url name as ascii string */
  char *URLName = nsnull;
  nsIURI* docURL = nsnull;
  char* spec;
  if (!doc) {
    return NS_OK;
  }
  docURL = doc->GetDocumentURL();
  if (!docURL) {
    return NS_OK;
  }
  (void)docURL->GetSpec(&spec);
  URLName = (char*)PR_Malloc(PL_strlen(spec)+1);
  PL_strcpy(URLName, spec);
  NS_IF_RELEASE(docURL);
  nsCRT::free(spec);

  nsCOMPtr<nsIDOMHTMLDocument> htmldoc(do_QueryInterface(doc));
  if (htmldoc == nsnull) {
    PR_Free(URLName);
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIDOMHTMLCollection> forms;
  rv = htmldoc->GetForms(getter_AddRefs(forms));
  if (NS_FAILED(rv) || (forms == nsnull)) {
    PR_Free(URLName);
    return rv;
  }

  PRUint32 numForms;
  forms->GetLength(&numForms);
  for (PRUint32 formX = 0; formX < numForms; formX++) {
    nsCOMPtr<nsIDOMNode> formNode;
    forms->Item(formX, getter_AddRefs(formNode));
    if (nsnull != formNode) {
      nsCOMPtr<nsIDOMHTMLFormElement> formElement(do_QueryInterface(formNode));
      if ((nsnull != formElement)) {
        nsCOMPtr<nsIDOMHTMLCollection> elements;
        rv = formElement->GetElements(getter_AddRefs(elements));
        if ((NS_SUCCEEDED(rv)) && (nsnull != elements)) {
          /* got to the form elements at long last */ 
          PRUint32 numElements;
          elements->GetLength(&numElements);
          for (PRUint32 elementX = 0; elementX < numElements; elementX++) {
            nsCOMPtr<nsIDOMNode> elementNode;
            elements->Item(elementX, getter_AddRefs(elementNode));
            if (nsnull != elementNode) {
              nsCOMPtr<nsIDOMHTMLInputElement> inputElement(do_QueryInterface(elementNode));
              if ((NS_SUCCEEDED(rv)) && (nsnull != inputElement)) {
                nsAutoString type("");
                rv = inputElement->GetType(type);
                if (NS_SUCCEEDED(rv)) {
                  if ((type == "") || (type.Compare("text", PR_TRUE) == 0) ||
                    (type.Compare("password", PR_TRUE) == 0)) {
                    nsAutoString field;
                    rv = inputElement->GetName(field);
                    if (NS_SUCCEEDED(rv)) {
                      char* nameString = field.ToNewCString();
                      if (nameString) {
                        char* valueString = NULL;
                        SINGSIGN_RestoreSignonData(URLName, nameString, &valueString);
                        if (valueString) {
                          nsAutoString value(valueString);                                    
                          rv = inputElement->SetValue(value);
                          // warning! don't delete valueString
                        }
                        delete[] nameString;
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  PR_Free(URLName);
  return rv;
}

NS_IMETHODIMP
nsWalletlibService::OnStartURLLoad
  (nsIDocumentLoader* loader, nsIChannel* channel, nsIContentViewer* aViewer)
{
 return NS_OK;
}

NS_IMETHODIMP
nsWalletlibService::OnProgressURLLoad
  (nsIDocumentLoader* loader, nsIChannel* channel, PRUint32 aProgress, PRUint32 aProgressMax)
{
  return NS_OK;
}

NS_IMETHODIMP
nsWalletlibService::OnStatusURLLoad
  (nsIDocumentLoader* loader, nsIChannel* channel, nsString& aMsg)
{
  return NS_OK;
}


NS_IMETHODIMP
nsWalletlibService::OnEndURLLoad
  (nsIDocumentLoader* loader, nsIChannel* channel, nsresult aStatus)
{
  return NS_OK;
}

NS_IMETHODIMP
nsWalletlibService::HandleUnknownContentType
  (nsIDocumentLoader* loader, nsIChannel* channel, const char *aContentType, const char *aCommand )
{
  return NS_OK;
}

