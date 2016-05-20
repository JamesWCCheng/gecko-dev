/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CDMProxy.h"

namespace mozilla {

GMPSessionType
ToGMPSessionType(dom::SessionType aSessionType) {
  switch (aSessionType) {
    case dom::SessionType::Temporary: return kGMPTemporySession;
    case dom::SessionType::Persistent: return kGMPPersistentSession;
    default: return kGMPTemporySession;
  };
};

dom::MediaKeyMessageType
ToMediaKeyMessageType(GMPSessionMessageType aMessageType) {
  switch (aMessageType) {
    case kGMPLicenseRequest: return dom::MediaKeyMessageType::License_request;
    case kGMPLicenseRenewal: return dom::MediaKeyMessageType::License_renewal;
    case kGMPLicenseRelease: return dom::MediaKeyMessageType::License_release;
    case kGMPIndividualizationRequest: return dom::MediaKeyMessageType::Individualization_request;
    default: return dom::MediaKeyMessageType::License_request;
  };
};

} // namespace mozilla
