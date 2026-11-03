// Inject errors at the actual NativeSubmission boundary, without a GPU/game.
#include "tdur/render/d3d12/native_device.hpp"

#include <atomic>
#include <iostream>
#include <stdexcept>

using namespace tdur::render::d3d12;

namespace {
uint32_t checks{};
void Require(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        throw std::runtime_error(message);
    }
}
template <class Operation> void Throws(Operation operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }
    Require(rejected, "Fault/closed submission did not reject operation");
}

struct Trace {
    HRESULT closeResult = S_OK;
    HRESULT signalResult = S_OK;
    HRESULT waitResult = S_OK;
    HRESULT removal = S_OK;
    uint64_t completed{};
    uint32_t closeCalls{};
    uint32_t executeCalls{};
    uint32_t signalCalls{};
    uint32_t waitCalls{};
    uint32_t lastWaitMilliseconds{};
    uint32_t liveObjects{};
    uint32_t destroyedObjects{};
    uint32_t destroyedCallbacks{};
    uint32_t expectedAtExecute = 1;
    bool retainedAtExecute = false;
    bool completeDuringWait = false;
};

class OwnedObject final : public IUnknown {
  public:
    explicit OwnedObject(std::shared_ptr<Trace> trace)
        : trace_(std::move(trace)) {
        ++trace_->liveObjects;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                             void** output) override {
        if (!output) {
            return E_POINTER;
        }
        *output = nullptr;
        if (iid == __uuidof(IUnknown)) {
            *output = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --references_;
        if (!remaining) {
            delete this;
        }
        return remaining;
    }

  private:
    ~OwnedObject() {
        --trace_->liveObjects;
        ++trace_->destroyedObjects;
    }
    std::atomic<ULONG> references_{1};
    std::shared_ptr<Trace> trace_;
};

ComPtr<IUnknown> Object(const std::shared_ptr<Trace>& trace) {
    ComPtr<IUnknown> object;
    object.Attach(new OwnedObject(trace));
    return object;
}

class OwnedOperations final : public NativeSubmissionOperations {
  public:
    explicit OwnedOperations(std::shared_ptr<Trace> trace)
        : trace_(std::move(trace)) {}
    ~OwnedOperations() override { ++trace_->destroyedCallbacks; }
    HRESULT close() noexcept override {
        ++trace_->closeCalls;
        return trace_->closeResult;
    }
    void execute() noexcept override {
        ++trace_->executeCalls;
        trace_->retainedAtExecute =
            trace_->liveObjects == trace_->expectedAtExecute;
    }
    HRESULT signal(uint64_t) noexcept override {
        ++trace_->signalCalls;
        return trace_->signalResult;
    }
    uint64_t completed() noexcept override { return trace_->completed; }
    HRESULT wait(uint64_t value, uint32_t milliseconds) noexcept override {
        ++trace_->waitCalls;
        trace_->lastWaitMilliseconds = milliseconds;
        if (trace_->completeDuringWait) {
            trace_->completed = value;
        }
        return trace_->waitResult;
    }
    HRESULT removedReason() noexcept override { return trace_->removal; }

  private:
    std::shared_ptr<Trace> trace_;
};
} // namespace

int main() {
    try {
        Require(NativeSubmission::quarantinedCount() == 0,
                "Initial quarantine not empty");
        {
            auto trace = std::make_shared<Trace>();
            auto operations = std::make_shared<OwnedOperations>(trace);
            NativeSubmission submission(operations, 7);
            auto object = Object(trace);
            submission.retain(object.Get());
            submission.retain(object.Get());
            object.Reset();
            Require(submission.retainedObjectCount() == 1,
                    "Duplicate retain not deduplicated");
            submission.submit();
            Require(trace->retainedAtExecute && trace->executeCalls == 1,
                    "Resource not owned before Execute");
            Require(!submission.recording() && submission.executed() &&
                        !submission.faulted(),
                    "Normal close/execute/signal phase incorrect");
            Throws([&] { submission.submit(); });
            Throws([&] { submission.retain(nullptr); });
            Require(trace->closeCalls == 1 && trace->signalCalls == 1,
                    "Closed batch was resubmitted");
            Require(submission.tryDrain().state == NativeDrainState::pending &&
                        trace->liveObjects == 1,
                    "Resource released before fence completion");
            trace->completed = 7;
            Require(submission.tryDrain().state == NativeDrainState::complete &&
                        trace->liveObjects == 0,
                    "Completed resource did not retire");
        }
        {
            auto trace = std::make_shared<Trace>();
            trace->closeResult = E_FAIL;
            auto operations = std::make_shared<OwnedOperations>(trace);
            NativeSubmission submission(operations, 11);
            auto object = Object(trace);
            submission.retain(object.Get());
            object.Reset();
            Throws([&] { submission.submit(); });
            Require(!submission.recording() && submission.faulted() &&
                        !submission.executed(),
                    "Close failure did not latch closed fault");
            Throws([&] { submission.submit(); });
            Require(trace->closeCalls == 1 && trace->executeCalls == 0 &&
                        trace->signalCalls == 0,
                    "Close failure submitted work or closed twice");
            Require(submission.tryDrain().state == NativeDrainState::complete &&
                        trace->liveObjects == 0,
                    "Never-executed objects could not be discarded safely");
        }
        {
            auto trace = std::make_shared<Trace>();
            trace->signalResult = E_OUTOFMEMORY;
            auto operations = std::make_shared<OwnedOperations>(trace);
            std::weak_ptr<NativeSubmissionOperations> weak = operations;
            {
                NativeSubmission submission(operations, 13);
                auto object = Object(trace);
                submission.retain(object.Get());
                object.Reset();
                Throws([&] { submission.submit(); });
                Require(submission.faulted() && !submission.recording() &&
                            submission.executed(),
                        "Signal-after-Execute failure state incorrect");
                Throws([&] { submission.submit(); });
                Require(trace->executeCalls == 1 && trace->retainedAtExecute &&
                            trace->liveObjects == 1,
                        "Signal failure destroyed resource or executed twice");
                const auto drain = submission.tryDrain();
                Require(drain.state == NativeDrainState::pending &&
                            drain.error == E_OUTOFMEMORY,
                        "Failed Signal was treated as completion");
                operations.reset();
            }
            Require(NativeSubmission::quarantinedCount() == 1 &&
                        !weak.expired() && trace->liveObjects == 1,
                    "Quarantine failed to own resource AND callback context");
            Require(NativeSubmission::drainQuarantined() == 1 &&
                        trace->liveObjects == 1,
                    "Unproven quarantine was released");
            trace->signalResult = S_OK;
            Require(NativeSubmission::drainQuarantined() == 1 &&
                        trace->liveObjects == 1,
                    "Retrying Signal falsely proved its fence complete");
            trace->completed = 13;
            Require(NativeSubmission::drainQuarantined() == 0 &&
                        trace->liveObjects == 0 && weak.expired(),
                    "Proven drain did not release owned callback/resources");
            Require(
                trace->closeCalls == 1 && trace->executeCalls == 1 &&
                    trace->destroyedCallbacks == 1,
                "Drain retried Close/Execute instead of only fence operations");
        }
        {
            auto trace = std::make_shared<Trace>();
            trace->waitResult = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
            auto operations = std::make_shared<OwnedOperations>(trace);
            std::weak_ptr<NativeSubmissionOperations> weak = operations;
            {
                NativeSubmission submission(operations, 17);
                auto object = Object(trace);
                submission.retain(object.Get());
                object.Reset();
                submission.submit();
                const auto timeout = submission.tryDrain(7);
                Require(timeout.state == NativeDrainState::pending &&
                            timeout.error == trace->waitResult,
                        "Wait timeout changed error or inferred retirement");
                Require(submission.faulted() && trace->liveObjects == 1 &&
                            trace->lastWaitMilliseconds == 7,
                        "Timeout shortened object lifetime");
                Throws([&] { submission.submit(); });
                operations.reset();
            }
            Require(NativeSubmission::quarantinedCount() == 1 &&
                        !weak.expired(),
                    "Timed-out context lost callback ownership");
            trace->completed = 17;
            Require(NativeSubmission::drainQuarantined() == 0 &&
                        weak.expired() && trace->liveObjects == 0,
                    "Late proven completion did not retire quarantine");
        }
        {
            auto trace = std::make_shared<Trace>();
            auto operations = std::make_shared<OwnedOperations>(trace);
            NativeSubmission submission(operations, 19);
            auto object = Object(trace);
            submission.retain(object.Get());
            object.Reset();
            submission.submit();
            const auto spurious = submission.tryDrain(3);
            Require(
                spurious.state == NativeDrainState::pending &&
                    spurious.error == E_UNEXPECTED && trace->liveObjects == 1,
                "Successful wait without completed fence released resource");
            trace->completed = UINT64_MAX;
            Require(submission.tryDrain().state == NativeDrainState::pending &&
                        trace->liveObjects == 1,
                    "UINT64_MAX was accepted without proven device removal");
            trace->removal = DXGI_ERROR_DEVICE_REMOVED;
            Require(submission.tryDrain().state ==
                            NativeDrainState::device_removed &&
                        trace->liveObjects == 0,
                    "Definitive device removal could not release GPU objects");
        }
        {
            auto trace = std::make_shared<Trace>();
            trace->waitResult = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
            trace->completeDuringWait = true;
            auto operations = std::make_shared<OwnedOperations>(trace);
            NativeSubmission submission(operations, 23);
            auto object = Object(trace);
            submission.retain(object.Get());
            object.Reset();
            submission.submit();
            const auto result = submission.tryDrain(1);
            Require(result.state == NativeDrainState::complete &&
                        FAILED(result.error),
                    "Drain proof and wait error were incorrectly conflated");
            Require(submission.faulted() && trace->liveObjects == 0,
                    "Safe retirement incorrectly cleared the fault latch");
            Throws([&] { submission.submit(); });
        }
        {
            auto trace = std::make_shared<Trace>();
            auto operations = std::make_shared<OwnedOperations>(trace);
            Throws([&] { NativeSubmission invalid(operations, 0); });
            Throws([&] { NativeSubmission invalid(operations, UINT64_MAX); });
            Throws([&] { NativeSubmission invalid({}, 1); });
            std::vector<std::unique_ptr<NativeSubmission>> active;
            for (size_t i = 0; i < NativeSubmission::MaxTrackedSubmissions;
                 ++i) {
                active.push_back(
                    std::make_unique<NativeSubmission>(operations, 100 + i));
            }
            Throws([&] { NativeSubmission excess(operations, 200); });
            Require(trace->closeCalls == 0 && trace->executeCalls == 0,
                    "Capacity refusal occurred after Execute");
            active.clear();
        }
        {
            auto trace = std::make_shared<Trace>();
            trace->expectedAtExecute =
                uint32_t(NativeSubmission::MaxRetainedObjects);
            auto operations = std::make_shared<OwnedOperations>(trace);
            NativeSubmission submission(operations, 29);
            std::vector<ComPtr<IUnknown>> objects;
            for (size_t i = 0; i < NativeSubmission::MaxRetainedObjects + 1;
                 ++i) {
                objects.push_back(Object(trace));
            }
            for (size_t i = 0; i < NativeSubmission::MaxRetainedObjects; ++i) {
                submission.retain(objects[i].Get());
            }
            Throws([&] { submission.retain(objects.back().Get()); });
            objects.clear();
            Require(trace->liveObjects == NativeSubmission::MaxRetainedObjects,
                    "Retained-object refusal changed accepted ownership");
            Require(submission.faulted() && !submission.recording(),
                    "Incomplete retention set did not abort recording");
            Throws([&] { submission.submit(); });
            Require(trace->executeCalls == 0 && trace->closeCalls == 0,
                    "Incomplete retention set was submitted during cleanup");
            Require(submission.tryDrain().state == NativeDrainState::complete &&
                        trace->liveObjects == 0,
                    "Bounded retained objects did not drain");
        }
        Require(NativeSubmission::quarantinedCount() == 0,
                "Fixture left unresolved quarantine");
        std::cout
            << "{\"project\":\"TDU-Recompilation\",\"scope\":\"owned-"
               "submission-lifecycle\","
               "\"gpu_executed\":false,\"game_executed\":false,\"checks\":"
            << checks
            << ",\"close_signal_wait_failures\":true,"
               "\"no_double_execute\":true,\"callback_lifetime\":true,"
               "\"quarantine_drained\":true}\n";
    } catch (const std::exception& error) {
        std::cerr << "Owned lifecycle fixture: " << error.what() << '\n';
        return 1;
    }
}
