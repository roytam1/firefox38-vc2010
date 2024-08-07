/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_Response_h
#define mozilla_dom_Response_h

#include "nsWrapperCache.h"
#include "nsISupportsImpl.h"

#include "mozilla/dom/Fetch.h"
#include "mozilla/dom/ResponseBinding.h"

#include "InternalResponse.h"

class nsPIDOMWindow;

namespace mozilla {
namespace dom {

class ArrayBufferOrArrayBufferViewOrUSVStringOrURLSearchParams;
class Headers;
class InternalHeaders;
class Promise;

class Response final : public nsISupports
                         , public FetchBody<Response>
                         , public nsWrapperCache
{
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(Response)

public:
  Response(nsIGlobalObject* aGlobal, InternalResponse* aInternalResponse);

  Response(const Response& aOther) MOZ_DELETE;

  JSObject*
  WrapObject(JSContext* aCx) override
  {
    return ResponseBinding::Wrap(aCx, this);
  }

  ResponseType
  Type() const
  {
    return mInternalResponse->Type();
  }

  void
  GetUrl(DOMString& aUrl) const
  {
    nsCString url;
    mInternalResponse->GetUrl(url);
    aUrl.AsAString() = NS_ConvertUTF8toUTF16(url);
  }

  bool
  GetFinalURL(ErrorResult& aRv) const
  {
    return mInternalResponse->FinalURL();
  }

  void
  SetFinalURL(bool aFinalURL, ErrorResult& aRv);

  uint16_t
  Status() const
  {
    return mInternalResponse->GetStatus();
  }

  bool
  Ok() const
  {
    return mInternalResponse->GetStatus() >= 200 &&
           mInternalResponse->GetStatus() <= 299;
  }

  void
  GetStatusText(nsCString& aStatusText) const
  {
    aStatusText = mInternalResponse->GetStatusText();
  }

  InternalHeaders*
  GetInternalHeaders() const
  {
    return mInternalResponse->Headers();
  }

  Headers* Headers_();

  void
  GetBody(nsIInputStream** aStream) { return mInternalResponse->GetBody(aStream); }

  static already_AddRefed<Response>
  Error(const GlobalObject& aGlobal);

  static already_AddRefed<Response>
  Redirect(const GlobalObject& aGlobal, const nsAString& aUrl, uint16_t aStatus, ErrorResult& aRv);

  static already_AddRefed<Response>
  Constructor(const GlobalObject& aGlobal,
              const Optional<ArrayBufferOrArrayBufferViewOrBlobOrFormDataOrUSVStringOrURLSearchParams>& aBody,
              const ResponseInit& aInit, ErrorResult& rv);

  nsIGlobalObject* GetParentObject() const
  {
    return mOwner;
  }

  already_AddRefed<Response>
  Clone(ErrorResult& aRv) const;

  void
  SetBody(nsIInputStream* aBody);

  already_AddRefed<InternalResponse>
  GetInternalResponse() const;

private:
  ~Response();

  nsCOMPtr<nsIGlobalObject> mOwner;
  nsRefPtr<InternalResponse> mInternalResponse;
  // Lazily created
  nsRefPtr<Headers> mHeaders;
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_Response_h
