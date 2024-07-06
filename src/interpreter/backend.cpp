#include <interpreter/interpreter.h>
#include <interpreter/memory.h>
#include <interpreter/device.h>
#include <interpreter/register.h>
#include <interpreter/executable.h>
#include <linker/layout.h>
#include <utility/config.h>
#include <utility.h>
#include <map>
#include <fstream>

namespace dark {

struct LabelMap {
  private:
    std::map <target_size_t, std::string_view> labels;

  public:
    void add(target_size_t pc, std::string_view label) {
        labels[pc] = label;
    }

    auto get(target_size_t pc) {
        struct Result {
            std::string_view label;
            target_size_t offset;
        };
        auto pos = labels.upper_bound(pc);
        if (pos == labels.begin()) return Result { "", pc };
        pos--; return Result { pos->second, pc - pos->first };
    }
};

RegisterFile::RegisterFile(target_size_t pc, const Config &config)
    : regs(), pc(), new_pc(pc) {
    (*this)[Register::sp] = config.storage_size;
    (*this)[Register::ra] = this->end_pc;
}

void Interpreter::interpret(const Config &config) {
    auto input  = std::ifstream { std::string(config.input_file) };
    auto output = std::ofstream { std::string(config.output_file) };
    auto device_ptr = Device::create(config, input, output);
    auto memory_ptr = Memory::create(config, this->layout);

    auto &device = *device_ptr;
    auto &memory = *memory_ptr;

    target_size_t start_pc = layout.position_table.at("main");
    auto regfile = RegisterFile { start_pc, config };

    while (regfile.advance()) {
        auto &exe = memory.fetch_executable(regfile.get_pc());
        exe(regfile, memory, device);
    }

    std::cout << std::format("Program returned: 0x{:x}\n", regfile[Register::a0]);
}

} // namespace dark
