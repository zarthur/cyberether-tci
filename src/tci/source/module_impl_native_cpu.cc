#include <chrono>
#include <functional>
#include <vector>

#include <jetstream/module_context.hh>
#include <jetstream/registry.hh>
#include <jetstream/runtime_context_native_cpu.hh>
#include <jetstream/scheduler_context.hh>
#include <jetstream/tools/automatic_iterator.hh>

#include "module_impl.hh"

namespace Jetstream::Modules {

struct TciIqSourceImplNativeCpu : public TciIqSourceImpl,
                                  public NativeCpuRuntimeContext,
                                  public Scheduler::Context {
    Result hasPendingCompute() override;
    Result computeSubmit() override;
};

Result TciIqSourceImplNativeCpu::hasPendingCompute() {
    return client.waitForSamples(signal.size(), std::chrono::milliseconds(100))
               ? Result::SUCCESS
               : Result::YIELD;
}

Result TciIqSourceImplNativeCpu::computeSubmit() {
    std::vector<std::complex<float>> samples(signal.size());
    const auto read = client.popWithMetadata(samples);
    if (read.samplesCopied != samples.size()) return Result::YIELD;

    const auto& metadata = read.metadata;
    if (metadata.ready && metadata.iqStarted && metadata.sampleRateHz != 0 && metadata.panCenterHz != 0 &&
        (metadata.sampleRateHz != lastSampleRateHz || metadata.panCenterHz != lastCenterHz)) {
        lastSampleRateHz = metadata.sampleRateHz;
        lastCenterHz = metadata.panCenterHz;
        JST_CHECK(signal.setAttribute("sampleRate", static_cast<F32>(lastSampleRateHz)));
        JST_CHECK(signal.setAttribute("frequency", static_cast<F32>(lastCenterHz)));
    }

    U64 index = 0;
    return AutomaticIterator<CF32>([&](auto& output) { output = samples[index++]; }, signal);
}

JST_REGISTER_MODULE(TciIqSourceImplNativeCpu, DeviceType::CPU, RuntimeType::NATIVE, "generic");

}  // namespace Jetstream::Modules
