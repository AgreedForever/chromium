// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromecast/media/cma/backend/android/audio_sink_android_audiotrack_impl.h"

#include <algorithm>
#include <string>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback_helpers.h"
#include "base/logging.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chromecast/media/cma/base/decoder_buffer_base.h"
#include "jni/AudioSinkAudioTrackImpl_jni.h"
#include "media/base/audio_bus.h"

#define RUN_ON_FEEDER_THREAD(callback, ...)                               \
  if (!feeder_task_runner_->BelongsToCurrentThread()) {                   \
    POST_TASK_TO_FEEDER_THREAD(&AudioSinkAndroidAudioTrackImpl::callback, \
                               ##__VA_ARGS__);                            \
    return;                                                               \
  }

#define POST_TASK_TO_FEEDER_THREAD(task, ...) \
  feeder_task_runner_->PostTask(              \
      FROM_HERE, base::BindOnce(task, base::Unretained(this), ##__VA_ARGS__));

#define RUN_ON_CALLER_THREAD(callback, ...)                               \
  if (!caller_task_runner_->BelongsToCurrentThread()) {                   \
    POST_TASK_TO_CALLER_THREAD(&AudioSinkAndroidAudioTrackImpl::callback, \
                               ##__VA_ARGS__);                            \
    return;                                                               \
  }

#define POST_TASK_TO_CALLER_THREAD(task, ...) \
  caller_task_runner_->PostTask(              \
      FROM_HERE, base::BindOnce(task, weak_this_, ##__VA_ARGS__));

using base::android::JavaParamRef;

namespace chromecast {
namespace media {

AudioSinkAndroidAudioTrackImpl::AudioSinkAndroidAudioTrackImpl(
    AudioSinkAndroid::Delegate* delegate,
    int input_samples_per_second,
    bool primary,
    const std::string& device_id,
    AudioContentType content_type)
    : delegate_(delegate),
      input_samples_per_second_(input_samples_per_second),
      primary_(primary),
      device_id_(device_id),
      content_type_(content_type),
      stream_volume_multiplier_(1.0f),
      type_volume_multiplier_(1.0f),
      mute_volume_multiplier_(1.0f),
      fade_ms_(kDefaultSlewTimeMs),
      feeder_thread_("AudioTrack feeder thread"),
      feeder_task_runner_(nullptr),
      caller_task_runner_(base::ThreadTaskRunnerHandle::Get()),
      direct_pcm_buffer_address_(nullptr),
      direct_rendering_delay_address_(nullptr),
      state_(kStateUninitialized),
      weak_factory_(this) {
  LOG(INFO) << __func__ << "(" << this << "):"
            << " input_samples_per_second_=" << input_samples_per_second_
            << " primary_=" << primary_ << " device_id_=" << device_id_
            << " content_type__=" << GetContentTypeName();
  DCHECK(delegate_);

  // Create Java part and initialize.
  DCHECK(j_audio_sink_audiotrack_impl_.is_null());
  j_audio_sink_audiotrack_impl_.Reset(
      Java_AudioSinkAudioTrackImpl_createAudioSinkAudioTrackImpl(
          base::android::AttachCurrentThread(),
          reinterpret_cast<intptr_t>(this)));
  Java_AudioSinkAudioTrackImpl_init(
      base::android::AttachCurrentThread(), j_audio_sink_audiotrack_impl_,
      input_samples_per_second_, kDirectBufferSize);
  base::Thread::Options options;
  options.priority = base::ThreadPriority::REALTIME_AUDIO;
  feeder_thread_.StartWithOptions(options);
  feeder_task_runner_ = feeder_thread_.task_runner();
  weak_this_ = weak_factory_.GetWeakPtr();
}

AudioSinkAndroidAudioTrackImpl::~AudioSinkAndroidAudioTrackImpl() {
  LOG(INFO) << __func__ << "(" << this << "): device_id_=" << device_id_;
  PreventDelegateCalls();
  FinalizeOnFeederThread();
  feeder_thread_.Stop();
  feeder_task_runner_ = nullptr;
}

int AudioSinkAndroidAudioTrackImpl::input_samples_per_second() const {
  return input_samples_per_second_;
}

bool AudioSinkAndroidAudioTrackImpl::primary() const {
  return primary_;
}

std::string AudioSinkAndroidAudioTrackImpl::device_id() const {
  return device_id_;
}

AudioContentType AudioSinkAndroidAudioTrackImpl::content_type() const {
  return content_type_;
}

const char* AudioSinkAndroidAudioTrackImpl::GetContentTypeName() const {
  return GetAudioContentTypeName(content_type_);
}

void AudioSinkAndroidAudioTrackImpl::FinalizeOnFeederThread() {
  RUN_ON_FEEDER_THREAD(FinalizeOnFeederThread);
  LOG(INFO) << __func__ << "(" << this << "):";
  if (j_audio_sink_audiotrack_impl_.is_null()) {
    LOG(WARNING) << "j_audio_sink_audiotrack_impl_ is NULL";
    return;
  }
  Java_AudioSinkAudioTrackImpl_close(base::android::AttachCurrentThread(),
                                     j_audio_sink_audiotrack_impl_);
  j_audio_sink_audiotrack_impl_.Reset();
}

void AudioSinkAndroidAudioTrackImpl::PreventDelegateCalls() {
  DCHECK(caller_task_runner_->BelongsToCurrentThread());
  weak_factory_.InvalidateWeakPtrs();
}

// static
bool AudioSinkAndroidAudioTrackImpl::RegisterJni(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

void AudioSinkAndroidAudioTrackImpl::CacheDirectBufferAddress(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jobject>& pcm_byte_buffer,
    const JavaParamRef<jobject>& timestamp_byte_buffer) {
  LOG(INFO) << __func__ << "(" << this << "):";
  direct_pcm_buffer_address_ =
      static_cast<uint8_t*>(env->GetDirectBufferAddress(pcm_byte_buffer));
  direct_rendering_delay_address_ = static_cast<uint64_t*>(
      env->GetDirectBufferAddress(timestamp_byte_buffer));
}

void AudioSinkAndroidAudioTrackImpl::WritePcm(
    scoped_refptr<DecoderBufferBase> data) {
  DCHECK(caller_task_runner_->BelongsToCurrentThread());
  DCHECK(!pending_data_);
  pending_data_ = std::move(data);
  pending_data_bytes_already_fed_ = 0;
  FeedData();
}

void AudioSinkAndroidAudioTrackImpl::FeedData() {
  RUN_ON_FEEDER_THREAD(FeedData);

  DCHECK(pending_data_);
  if (pending_data_->end_of_stream()) {
    LOG(INFO) << __func__ << "(" << this << "): EOS!";
    state_ = kStateGotEos;
    PostPcmCallback(sink_rendering_delay_);
    return;
  }

  VLOG(3) << __func__ << "(" << this << "):"
          << " [" << pending_data_->data_size() << "]"
          << " @ts=" << pending_data_->timestamp();

  if (pending_data_->data_size() == 0) {
    LOG(INFO) << __func__ << "(" << this << "): empty data buffer!";
    PostPcmCallback(sink_rendering_delay_);
    return;
  }

  ReformatData();

  int written = Java_AudioSinkAudioTrackImpl_writePcm(
      base::android::AttachCurrentThread(), j_audio_sink_audiotrack_impl_,
      pending_data_->data_size());

  if (written < 0) {
    LOG(ERROR) << __func__ << "(" << this << "): Cannot write PCM via JNI!";
    SignalError(AudioSinkAndroid::SinkError::kInternalError);
    return;
  }

  if (state_ == kStatePaused &&
      written < static_cast<int>(pending_data_->data_size())) {
    LOG(INFO) << "Audio Server is full while in PAUSED, "
              << "will continue when entering PLAY mode.";
    pending_data_bytes_already_fed_ = written;
    return;
  }

  if (written != static_cast<int>(pending_data_->data_size())) {
    LOG(ERROR) << __func__ << "(" << this << "): Wrote " << written
               << " instead of " << pending_data_->data_size();
    // continue anyway, better to do a best-effort than fail completely
  }

  // RenderingDelay was returned through JNI via direct buffers.
  sink_rendering_delay_.delay_microseconds = direct_rendering_delay_address_[0];
  sink_rendering_delay_.timestamp_microseconds =
      direct_rendering_delay_address_[1];

  TrackRawMonotonicClockDeviation();

  PostPcmCallback(sink_rendering_delay_);
}

void AudioSinkAndroidAudioTrackImpl::ReformatData() {
  // Data is in planar float format, i.e. all left samples first, then all
  // right -> "LLLLLLLLLLLLLLLLRRRRRRRRRRRRRRRR").
  // AudioTrack needs interleaved format -> "LRLRLRLRLRLRLRLRLRLRLRLRLRLRLRLR").
  DCHECK(direct_pcm_buffer_address_);
  DCHECK_EQ(0, static_cast<int>(pending_data_->data_size() % sizeof(float)));
  CHECK(pending_data_->data_size() < kDirectBufferSize);
  int num_of_samples = pending_data_->data_size() / sizeof(float);
  int num_of_frames = num_of_samples / 2;
  const float* src_left = reinterpret_cast<const float*>(pending_data_->data());
  const float* src_right = src_left + num_of_samples / 2;
  float* dst = reinterpret_cast<float*>(direct_pcm_buffer_address_);
  for (int f = 0; f < num_of_frames; f++) {
    *dst++ = *src_left++;
    *dst++ = *src_right++;
  }
}

void AudioSinkAndroidAudioTrackImpl::TrackRawMonotonicClockDeviation() {
  timespec now = {0, 0};
  clock_gettime(CLOCK_MONOTONIC, &now);
  int64_t now_usec =
      static_cast<int64_t>(now.tv_sec) * 1000000 + now.tv_nsec / 1000;

  clock_gettime(CLOCK_MONOTONIC_RAW, &now);
  int64_t now_raw_usec =
      static_cast<int64_t>(now.tv_sec) * 1000000 + now.tv_nsec / 1000;

  // TODO(ckuiper): Eventually we want to use this to convert from non-RAW to
  // RAW timestamps to improve accuracy.
  VLOG(3) << __func__ << "(" << this << "):"
          << " now - now_raw=" << (now_usec - now_raw_usec);
}

void AudioSinkAndroidAudioTrackImpl::FeedDataContinue() {
  RUN_ON_FEEDER_THREAD(FeedDataContinue);

  DCHECK(pending_data_);
  DCHECK(pending_data_bytes_already_fed_);

  int left_to_send =
      pending_data_->data_size() - pending_data_bytes_already_fed_;
  LOG(INFO) << __func__ << "(" << this << "): send remaining " << left_to_send
            << "/" << pending_data_->data_size();

  memmove(direct_pcm_buffer_address_,
          direct_pcm_buffer_address_ + pending_data_bytes_already_fed_,
          left_to_send);

  int written = Java_AudioSinkAudioTrackImpl_writePcm(
      base::android::AttachCurrentThread(), j_audio_sink_audiotrack_impl_,
      left_to_send);

  DCHECK(written == left_to_send);

  PostPcmCallback(sink_rendering_delay_);
}

void AudioSinkAndroidAudioTrackImpl::PostPcmCallback(
    const MediaPipelineBackendAndroid::RenderingDelay& delay) {
  RUN_ON_CALLER_THREAD(PostPcmCallback, delay);
  DCHECK(pending_data_);
  VLOG(3) << __func__ << "(" << this << "): "
          << " delay=" << delay.delay_microseconds
          << " ts=" << delay.timestamp_microseconds;
  pending_data_ = nullptr;
  pending_data_bytes_already_fed_ = 0;
  delegate_->OnWritePcmCompletion(MediaPipelineBackendAndroid::kBufferSuccess,
                                  delay);
}

void AudioSinkAndroidAudioTrackImpl::SignalError(
    AudioSinkAndroid::SinkError error) {
  DCHECK(feeder_task_runner_->BelongsToCurrentThread());
  state_ = kStateError;
  PostError(error);
}

void AudioSinkAndroidAudioTrackImpl::PostError(
    AudioSinkAndroid::SinkError error) {
  RUN_ON_CALLER_THREAD(PostError, error);
  delegate_->OnSinkError(error);
}

void AudioSinkAndroidAudioTrackImpl::SetPaused(bool paused) {
  RUN_ON_FEEDER_THREAD(SetPaused, paused);

  if (paused) {
    LOG(INFO) << __func__ << "(" << this << "): Pausing";
    state_ = kStatePaused;
    Java_AudioSinkAudioTrackImpl_pause(base::android::AttachCurrentThread(),
                                       j_audio_sink_audiotrack_impl_);
  } else {
    LOG(INFO) << __func__ << "(" << this << "): Unpausing";
    state_ = kStateNormalPlayback;
    Java_AudioSinkAudioTrackImpl_play(base::android::AttachCurrentThread(),
                                      j_audio_sink_audiotrack_impl_);
    if (pending_data_bytes_already_fed_) {
      // The last data buffer was partially fed, complete it now.
      FeedDataContinue();
    }
  }
}

void AudioSinkAndroidAudioTrackImpl::UpdateVolume() {
  DCHECK(feeder_task_runner_->BelongsToCurrentThread());

  LOG(INFO) << __func__ << "(" << this << "): vol=" << EffectiveVolume();

  Java_AudioSinkAudioTrackImpl_setVolume(base::android::AttachCurrentThread(),
                                         j_audio_sink_audiotrack_impl_,
                                         EffectiveVolume());
}

void AudioSinkAndroidAudioTrackImpl::SetVolumeMultiplier(float multiplier) {
  RUN_ON_FEEDER_THREAD(SetVolumeMultiplier, multiplier);

  stream_volume_multiplier_ = std::max(0.0f, std::min(multiplier, 1.0f));
  LOG(INFO) << __func__ << "(" << this << "): device_id_=" << device_id_
            << " stream_multiplier=" << stream_volume_multiplier_ << " ("
            << multiplier << ")"
            << " effective=" << EffectiveVolume();
  UpdateVolume();
}

void AudioSinkAndroidAudioTrackImpl::SetContentTypeVolume(float multiplier,
                                                          int fade_ms) {
  RUN_ON_FEEDER_THREAD(SetContentTypeVolume, multiplier, fade_ms);

  type_volume_multiplier_ = std::max(0.0f, std::min(multiplier, 1.0f));
  LOG(INFO) << __func__ << "(" << this << "): device_id_=" << device_id_
            << " type_multiplier=" << type_volume_multiplier_ << " ("
            << multiplier << ")"
            << " effective=" << EffectiveVolume();
  if (fade_ms < 0) {
    fade_ms = kDefaultSlewTimeMs;
  }
  fade_ms_ = fade_ms;
  LOG(INFO) << "Fade over " << fade_ms_ << " ms";
  UpdateVolume();
}

void AudioSinkAndroidAudioTrackImpl::SetMuted(bool muted) {
  RUN_ON_FEEDER_THREAD(SetMuted, muted);

  mute_volume_multiplier_ = muted ? 0.0f : 1.0f;
  LOG(INFO) << __func__ << "(" << this << "): device_id_=" << device_id_
            << " mute_multiplier=" << mute_volume_multiplier_
            << " effective=" << EffectiveVolume();
  UpdateVolume();
}

float AudioSinkAndroidAudioTrackImpl::EffectiveVolume() const {
  return stream_volume_multiplier_ * type_volume_multiplier_ *
         mute_volume_multiplier_;
}

}  // namespace media
}  // namespace chromecast
