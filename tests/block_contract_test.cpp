#include <cstdlib>
#include <iostream>

#include "../src/tci/source/block_impl.cc"

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #condition << '\n'; \
        std::exit(EXIT_FAILURE); \
    } \
} while (false)

int main() {
    Jetstream::Blocks::TciIqSourceBlockImpl block;
    CHECK(block.define() == Jetstream::Result::SUCCESS);
    CHECK(block.outputKeys == std::vector<std::string>{"signal"});
    CHECK(block.configTypes.size() == 5);
    CHECK((block.configTypes[0] == std::pair<std::string, std::string>("host", "text")));
    CHECK((block.configTypes[1] == std::pair<std::string, std::string>("port", "uint")));
    CHECK((block.configTypes[2] == std::pair<std::string, std::string>("receiver", "uint")));
    CHECK((block.configTypes[3] == std::pair<std::string, std::string>("sampleRateHz", "dropdown")));
    CHECK((block.configTypes[4] == std::pair<std::string, std::string>("bufferSize", "uint")));

    block.host = "127.0.0.1";
    block.port = 50002;
    block.receiver = 2;
    block.sampleRateHz = 96000;
    block.bufferSize = 4096;
    CHECK(block.configure() == Jetstream::Result::SUCCESS);
    CHECK(block.create() == Jetstream::Result::SUCCESS);
    CHECK(block.createdModuleName == "tci_iq_source");
    CHECK((block.exposedOutput == std::pair<std::string, std::pair<std::string, std::string>>{
        "signal", {"tci_iq_source", "signal"}}));

    const auto module = std::dynamic_pointer_cast<Jetstream::Modules::TciIqSource>(block.createdConfig);
    CHECK(module != nullptr);
    CHECK(module->host == "127.0.0.1" && module->port == 50002 && module->receiver == 2);
    CHECK(module->sampleRateHz == 96000 && module->bufferSize == 4096);
    std::cout << "block define/create config contract passed\n";
    return 0;
}
