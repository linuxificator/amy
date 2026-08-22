#include <jni.h>
#include <android/log.h>
#include <oboe/Oboe.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

extern "C" {
#include "amy.h"
#include "amy_socket_transport.h"
}

#define LOG_TAG "AmyAndroid"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/*
 * AMY's generic API always calls these platform hooks. The Android build does
 * not use AMY's miniaudio/I2S platform layer; Oboe owns the output stream and
 * calls amy_simple_fill_buffer() directly.
 */
extern "C" void amy_platform_init(void) {}
extern "C" void amy_platform_deinit(void) {}
extern "C" void amy_update_tasks(void) {}
extern "C" int16_t *amy_render_audio(void) { return nullptr; }
extern "C" size_t amy_i2s_write(const uint8_t *, size_t) { return 0; }

namespace {

constexpr int kMaxCommandsPerBlock = 64;

class AmyAndroidEngine final : public oboe::AudioStreamDataCallback,
                               public oboe::AudioStreamErrorCallback {
public:
    int start(const char *socketPath) {
        if (socketPath == nullptr || socketPath[0] == '\0') return -EINVAL;
        if (mRunning.load(std::memory_order_acquire)) return -EALREADY;

        amy_config_t config = amy_default_config();
        config.audio = AMY_AUDIO_IS_NONE;
        config.features.audio_in = 0;
        config.features.default_synths = 0;
        config.features.startup_bleep = 0;
        /* Keep AMY rendering entirely on Oboe's realtime callback thread. */
        config.platform.multicore = 0;
        config.platform.multithread = 0;
        /* Omnichord Physical Strings can require fourteen simultaneous KS voices. */
        config.ks_oscs = 16;

        amy_start(config);
        mAmyStarted = true;

        mSocket = amy_socket_server_create();
        if (mSocket == nullptr) {
            stopAmy();
            return -ENOMEM;
        }
        int socketResult = amy_socket_server_start(mSocket, socketPath);
        if (socketResult != 0) {
            amy_socket_server_destroy(mSocket);
            mSocket = nullptr;
            stopAmy();
            return socketResult;
        }

        oboe::AudioStreamBuilder builder;
        builder.setDirection(oboe::Direction::Output);
        builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
        builder.setSharingMode(oboe::SharingMode::Exclusive);
        builder.setFormat(oboe::AudioFormat::I16);
        builder.setChannelCount(AMY_NCHANS);
        builder.setSampleRate(AMY_SAMPLE_RATE);
        builder.setUsage(oboe::Usage::Game);
        builder.setContentType(oboe::ContentType::Music);
        builder.setDataCallback(this);
        builder.setErrorCallback(this);

        oboe::Result result = builder.openStream(mStream);
        if (result != oboe::Result::OK || !mStream) {
            LOGE("Oboe openStream failed: %s", oboe::convertToText(result));
            cleanupSocketAndAmy();
            return static_cast<int>(result);
        }

        if (mStream->getFormat() != oboe::AudioFormat::I16 ||
            mStream->getChannelCount() != AMY_NCHANS ||
            mStream->getSampleRate() != AMY_SAMPLE_RATE) {
            LOGE("Unexpected Oboe format: format=%d channels=%d rate=%d",
                 static_cast<int>(mStream->getFormat()),
                 mStream->getChannelCount(),
                 mStream->getSampleRate());
            mStream->close();
            mStream.reset();
            cleanupSocketAndAmy();
            return -ERANGE;
        }

        mBlock = nullptr;
        mBlockFrame = AMY_BLOCK_SIZE;
        mRunning.store(true, std::memory_order_release);
        result = mStream->requestStart();
        if (result != oboe::Result::OK) {
            LOGE("Oboe requestStart failed: %s", oboe::convertToText(result));
            mRunning.store(false, std::memory_order_release);
            mStream->close();
            mStream.reset();
            cleanupSocketAndAmy();
            return static_cast<int>(result);
        }

        LOGI("AMY/Oboe started: %d Hz, %d-frame AMY blocks, socket=%s",
             AMY_SAMPLE_RATE, AMY_BLOCK_SIZE, socketPath);
        return 0;
    }

    void stop() {
        mRunning.store(false, std::memory_order_release);

        if (mStream) {
            mStream->requestStop();
            mStream->close();
            mStream.reset();
        }

        cleanupSocketAndAmy();
        mBlock = nullptr;
        mBlockFrame = AMY_BLOCK_SIZE;
    }

    oboe::DataCallbackResult onAudioReady(
        oboe::AudioStream *,
        void *audioData,
        int32_t numFrames) override {
        int16_t *output = static_cast<int16_t *>(audioData);
        if (!mRunning.load(std::memory_order_acquire)) {
            std::memset(output, 0, static_cast<size_t>(numFrames) * AMY_NCHANS * sizeof(int16_t));
            return oboe::DataCallbackResult::Stop;
        }

        int32_t outputFrame = 0;
        while (outputFrame < numFrames) {
            if (mBlock == nullptr || mBlockFrame >= AMY_BLOCK_SIZE) {
                drainCommands();
                mBlock = amy_simple_fill_buffer();
                mBlockFrame = 0;
                if (mBlock == nullptr) {
                    std::memset(output + outputFrame * AMY_NCHANS, 0,
                                static_cast<size_t>(numFrames - outputFrame) *
                                    AMY_NCHANS * sizeof(int16_t));
                    break;
                }
            }

            const int32_t available = AMY_BLOCK_SIZE - mBlockFrame;
            const int32_t frames = std::min(available, numFrames - outputFrame);
            std::memcpy(
                output + outputFrame * AMY_NCHANS,
                mBlock + mBlockFrame * AMY_NCHANS,
                static_cast<size_t>(frames) * AMY_NCHANS * sizeof(int16_t));
            outputFrame += frames;
            mBlockFrame += frames;
        }

        return oboe::DataCallbackResult::Continue;
    }

    void onErrorAfterClose(oboe::AudioStream *, oboe::Result error) override {
        mRunning.store(false, std::memory_order_release);
        LOGE("Oboe stream closed after error: %s", oboe::convertToText(error));
        /* Lifecycle owner may restart the service; no work is done on Oboe's error thread. */
    }

private:
    void drainCommands() {
        if (mSocket == nullptr) return;
        char command[AMY_SOCKET_MAX_MESSAGE];
        for (int count = 0; count < kMaxCommandsPerBlock; ++count) {
            int length = amy_socket_server_pop(mSocket, command, sizeof(command));
            if (length <= 0) break;
            amy_add_message(command);
        }
    }

    void stopAmy() {
        if (mAmyStarted) {
            amy_stop();
            mAmyStarted = false;
        }
    }

    void cleanupSocketAndAmy() {
        if (mSocket != nullptr) {
            uint32_t dropped = amy_socket_server_dropped(mSocket);
            if (dropped != 0) LOGE("AMY socket dropped %u messages", dropped);
            amy_socket_server_destroy(mSocket);
            mSocket = nullptr;
        }
        stopAmy();
    }

    std::atomic<bool> mRunning{false};
    bool mAmyStarted = false;
    amy_socket_server_t *mSocket = nullptr;
    std::shared_ptr<oboe::AudioStream> mStream;
    int16_t *mBlock = nullptr;
    int32_t mBlockFrame = AMY_BLOCK_SIZE;
};

std::mutex gLifecycleMutex;
std::unique_ptr<AmyAndroidEngine> gEngine;

}  // namespace

extern "C" JNIEXPORT jint JNICALL
Java_org_amy_audio_AmyService_nativeStart(JNIEnv *env, jclass, jstring socketPath) {
    if (socketPath == nullptr) return -EINVAL;

    const char *path = env->GetStringUTFChars(socketPath, nullptr);
    if (path == nullptr) return -ENOMEM;

    std::lock_guard<std::mutex> guard(gLifecycleMutex);
    if (gEngine) gEngine->stop();
    gEngine = std::make_unique<AmyAndroidEngine>();
    int result = gEngine->start(path);
    if (result != 0) gEngine.reset();

    env->ReleaseStringUTFChars(socketPath, path);
    return result;
}

extern "C" JNIEXPORT void JNICALL
Java_org_amy_audio_AmyService_nativeStop(JNIEnv *, jclass) {
    std::lock_guard<std::mutex> guard(gLifecycleMutex);
    if (gEngine) {
        gEngine->stop();
        gEngine.reset();
    }
}
