/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaDataRendererProxy.h"
#include "MediaData.h"

namespace mozilla {

RefPtr<MediaDataDecoder::InitPromise>
MediaDataRendererProxy::InternalInit()
{
  MOZ_ASSERT(!mIsShutdown);

  return mProxyRenderer->Init();
}

RefPtr<MediaDataDecoder::InitPromise>
MediaDataRendererProxy::Init()
{
  MOZ_ASSERT(!mIsShutdown);

  return InvokeAsync(mProxyThreadWrapper, this, __func__,
                     &MediaDataRendererProxy::InternalInit);
}

nsresult
MediaDataRendererProxy::RenderVideoPacket(MediaRawData* aSample)
{
  MOZ_ASSERT(!IsOnProxyThread());
  MOZ_ASSERT(!mIsShutdown);

  nsCOMPtr<nsIRunnable> task =
    new RenderTask<decltype(&MediaDataRenderer::RenderVideoPacket)>(mProxyRenderer,
                                                                    &MediaDataRenderer::RenderVideoPacket,
                                                                    aSample);
  nsresult rv = mProxyThread->Dispatch(task, NS_DISPATCH_NORMAL);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
MediaDataRendererProxy::RenderAudioPacket(MediaRawData* aSample)
{
  MOZ_ASSERT(!IsOnProxyThread());
  MOZ_ASSERT(!mIsShutdown);

  nsCOMPtr<nsIRunnable> task =
    new RenderTask<decltype(&MediaDataRenderer::RenderAudioPacket)>(mProxyRenderer,
                                                                    &MediaDataRenderer::RenderAudioPacket,
                                                                    aSample);
  nsresult rv = mProxyThread->Dispatch(task, NS_DISPATCH_NORMAL);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
MediaDataRendererProxy::SetPlaybackRate(double aRate)
{
  MOZ_ASSERT(!IsOnProxyThread());
  MOZ_ASSERT(!mIsShutdown);

  nsCOMPtr<nsIRunnable> task;
  task = NS_NewRunnableMethodWithArg<int32_t>(mProxyRenderer,
                                     &MediaDataRenderer::SetPlaybackRate,
                                     aRate);
  nsresult rv = mProxyThread->Dispatch(task, NS_DISPATCH_NORMAL);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

nsresult
MediaDataRendererProxy::SetVolume(double aVolume)
{
  MOZ_ASSERT(!IsOnProxyThread());
  MOZ_ASSERT(!mIsShutdown);

  nsCOMPtr<nsIRunnable> task;
  task = NS_NewRunnableMethodWithArg<double>(mProxyRenderer,
                                             &MediaDataRenderer::SetVolume,
                                             aVolume);
  nsresult rv = mProxyThread->Dispatch(task, NS_DISPATCH_NORMAL);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

nsresult
MediaDataRendererProxy::SetPreservesPitch(bool aPreservesPitch)
{
  MOZ_ASSERT(!IsOnProxyThread());
  MOZ_ASSERT(!mIsShutdown);

  nsCOMPtr<nsIRunnable> task;
  task = NS_NewRunnableMethodWithArg<int32_t>(mProxyRenderer,
                                     &MediaDataRenderer::SetPreservesPitch,
                                     aPreservesPitch);
  nsresult rv = mProxyThread->Dispatch(task, NS_DISPATCH_NORMAL);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

nsresult
MediaDataRendererProxy::Shutdown()
{
  // Note that this *may* be called from the proxy thread also.
  MOZ_ASSERT(!mIsShutdown);
#if defined(DEBUG)
  mIsShutdown = true;
#endif
  nsCOMPtr<nsIRunnable> task;
  task = NS_NewRunnableMethod(mProxyRenderer, &MediaDataRenderer::Shutdown);
  nsresult rv = mProxyThread->Dispatch(task, NS_DISPATCH_SYNC);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

nsresult
MediaDataRendererProxy::Start(int64_t aStartTime)
{
  MOZ_ASSERT(!IsOnProxyThread());
  MOZ_ASSERT(!mIsShutdown);

  nsCOMPtr<nsIRunnable> task;
  task = NS_NewRunnableMethodWithArg<int64_t>(mProxyRenderer,
                                              &MediaDataRenderer::Start,
                                              aStartTime);
  nsresult rv = mProxyThread->Dispatch(task, NS_DISPATCH_NORMAL);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

nsresult
MediaDataRendererProxy::Stop()
{
  MOZ_ASSERT(!IsOnProxyThread());
  MOZ_ASSERT(!mIsShutdown);

  nsCOMPtr<nsIRunnable> task;
  task = NS_NewRunnableMethod(mProxyRenderer, &MediaDataRenderer::Stop);
  nsresult rv = mProxyThread->Dispatch(task, NS_DISPATCH_NORMAL);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

nsresult
MediaDataRendererProxy::ContentChanged(int64_t aTime)
{
  MOZ_ASSERT(!IsOnProxyThread());
  MOZ_ASSERT(!mIsShutdown);

  nsCOMPtr<nsIRunnable> task;
  task =
    NS_NewRunnableMethodWithArg<int64_t>(mProxyRenderer,
                                         &MediaDataRenderer::ContentChanged,
                                         aTime);
  nsresult rv = mProxyThread->Dispatch(task, NS_DISPATCH_NORMAL);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

nsresult
MediaDataRendererProxy::SetDisplayPosition(GMPRect aDisplayRect)
{
  MOZ_ASSERT(!IsOnProxyThread());
  MOZ_ASSERT(!mIsShutdown);

  nsCOMPtr<nsIRunnable> task;
  task = NS_NewRunnableMethodWithArg<GMPRect>(mProxyRenderer,
                                                     &MediaDataRenderer::SetDisplayPosition,
                                                     aDisplayRect);
  nsresult rv = mProxyThread->Dispatch(task, NS_DISPATCH_NORMAL);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

} // namespace mozilla
