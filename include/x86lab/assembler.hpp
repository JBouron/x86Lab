#pragma once
#include <map>
#include <memory>
#include <utility>

#include <x86lab/util.hpp>

namespace X86Lab::Assembler {
// Holds the mapping between addresses and source line numbers.
// This is nothing more than a map address -> line#.
using InstructionMap = std::map<u64, u64>;

// Invoke the assembler on a source file.
// @param filePath: Path to the file to be assembled.
// @return: A tuple containing the assembled code, its size in bytes and the
// corresponding InstructionMap.
std::tuple<std::unique_ptr<u8>, u64, std::unique_ptr<InstructionMap const>>
invoke(std::string const& filePath);
}
