/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ServiceWorkerCommon_h
#define mozilla_dom_ServiceWorkerCommon_h

#include "mozilla/TypedEnum.h"

namespace mozilla {
namespace dom {

// Use multiples of 2 since they can be bitwise ORed when calling
// InvalidateServiceWorkerRegistrationWorker.
MOZ_BEGIN_ENUM_CLASS(WhichServiceWorker)
  INSTALLING_WORKER = 1,
  WAITING_WORKER    = 2,
  ACTIVE_WORKER     = 4,
MOZ_END_ENUM_CLASS(WhichServiceWorker)
MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(WhichServiceWorker)

} // dom namespace
} // mozilla namespace

#endif // mozilla_dom_ServiceWorkerCommon_h
