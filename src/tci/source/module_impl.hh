#pragma once

#include <cyberether_tci/client.hpp>
#include <cyberether_tci/source/module.hh>
#include <jetstream/detail/module_impl.hh>

namespace Jetstream::Modules {

struct TciIqSourceImpl : public Module::Impl, public DynamicConfig<TciIqSource> {
    Result validate() override;
    Result define() override;
    Result create() override;
    Result destroy() override;
    Result reconfigure() override;

  protected:
    Tensor signal;
    cyberether_tci::TciClient client;
    U32 lastSampleRateHz = 0;
    U32 lastCenterHz = 0;
};

}  // namespace Jetstream::Modules
