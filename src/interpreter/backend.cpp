#include <interpreter/interpreter.h>
#include <interpreter/memory.h>
#include <interpreter/device.h>
#include <interpreter/register.h>
#include <interpreter/executable.h>
#include <linker/memory.h>
#include <utility/config.h>
#include <utility.h>
#include <fstream>

namespace dark {

void Interpreter::interpret(const Config &config, MemoryLayout layout) {
    auto input  = std::ifstream { std::string(config.input_file) };
    auto output = std::ofstream { std::string(config.output_file) };
    auto device_ptr = Device::create(config, input, output);
    auto memory_ptr = Memory::create(config, layout);

    auto &device = *device_ptr;
    auto &memory = *memory_ptr;
    auto regfile = RegisterFile {};

    auto start_pc = layout.position_table.at("main");
    regfile.set_pc(start_pc);

    // If the program reach 0x2, it should stop
    static constexpr target_size_t end_pc = 0x2;

    regfile[Register::ra] = end_pc;

    while (regfile.advance() != end_pc) {
        // Required by the RISC-V ISA
        regfile[Register::zero] = 0;
        auto &exe = memory.fetch_executable(regfile.get_pc());
        exe(regfile, memory, device);
    }

    std::cout << std::format("Program returned: {}\n", regfile[Register::a0]);
}

} // namespace dark
