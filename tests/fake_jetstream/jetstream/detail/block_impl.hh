#pragma once

#include "jetstream/block.hh"
#include "jetstream/module.hh"

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Jetstream {

template<typename ConfigType>
struct DynamicConfig : ConfigType {};

struct Block::Impl {
    virtual ~Impl() = default;
    virtual Result configure() { return Result::SUCCESS; }
    virtual Result define() { return Result::SUCCESS; }
    virtual Result create() { return Result::SUCCESS; }

    Result defineInterfaceOutput(const std::string& key, const std::string&, const std::string&) {
        outputKeys.push_back(key);
        return Result::SUCCESS;
    }

    Result defineInterfaceConfig(const std::string& key, const std::string&, const std::string&,
                                 const Parser::Map& format) {
        static const std::set<std::string> allowed{
            "bool", "int", "uint", "float", "range", "dropdown", "text", "multiline",
            "vector", "vector-inline", "filepicker", "filesave", "tensor-config", "markdown",
            "python", "python-console",
        };
        const auto type = format.find("type");
        if (type == format.end()) return Result::ERROR;
        const auto* name = std::any_cast<std::string>(&type->second.data);
        if (name == nullptr || !allowed.contains(*name)) return Result::ERROR;
        if (*name == "dropdown") {
            const auto options = format.find("options");
            if (options == format.end()) return Result::ERROR;
            const auto* sequence = std::any_cast<Parser::Sequence>(&options->second.data);
            if (sequence == nullptr) return Result::ERROR;
            for (const auto& option : *sequence) {
                const auto label = option.find("label");
                const auto value = option.find("value");
                if (label == option.end() || value == option.end() ||
                    std::any_cast<std::string>(&label->second.data) == nullptr ||
                    std::any_cast<std::string>(&value->second.data) == nullptr) return Result::ERROR;
            }
        }
        configTypes.emplace_back(key, *name);
        return Result::SUCCESS;
    }

    Result moduleCreate(const std::string& name, const std::shared_ptr<Module::Config>& config,
                        const TensorMap&) {
        createdModuleName = name;
        createdConfig = config;
        return Result::SUCCESS;
    }

    Result moduleExposeOutput(const std::string& blockPort, const std::pair<std::string, std::string>& output) {
        exposedOutput = {blockPort, output};
        return Result::SUCCESS;
    }

    std::vector<std::string> outputKeys;
    std::vector<std::pair<std::string, std::string>> configTypes;
    std::string createdModuleName;
    std::shared_ptr<Module::Config> createdConfig;
    std::pair<std::string, std::pair<std::string, std::string>> exposedOutput;
};

}  // namespace Jetstream
