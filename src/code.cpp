#include <x86lab/code.hpp>
#include <fstream>
#include <array>
#include <iostream>
#include <sys/stat.h>
#include <sstream>

namespace X86Lab {

Code::Code(std::string const& filePath) : file(filePath) {
    std::tuple<std::unique_ptr<u8>,
               u64,
               std::unique_ptr<Assembler::InstructionMap const>>
    assemblerOutput(X86Lab::Assembler::invoke(filePath));

    code = std::move(std::get<0>(assemblerOutput));
    codeSize = std::get<1>(assemblerOutput);
    map = std::move(std::get<2>(assemblerOutput));
}

u8 const* Code::machineCode() const {
    return code.get();
}

u64 Code::size() const {
    return codeSize;
}

u64 Code::offsetToLine(u64 const offset) const {
    if (!map->contains(offset)) {
        return 0;
    } else {
        return map->at(offset);
    }
}

std::string const& Code::fileName() const {
    return file;
}
}
