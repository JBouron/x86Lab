#pragma once
#include <vector>
#include <map>
#include <memory>

#include <x86lab/util.hpp>

namespace X86Lab::Assembler {
// Holds the mapping between addresses and source line numbers.
// This is nothing more than a map address -> line#.
using InstructionMap = std::map<u64, u64>;

// Wrapper around some assembled code that should be run by a VM.
class Code {
public:
    // Assemble code from the given file.
    // @param filePath: Path to a file containing x86 assembly code to be
    // assembled.
    Code(std::string const& fileName);

    // Get a pointer to the assembled machine code.
    // @return: A const pointer to the code. 
    u8 const* machineCode() const;

    // Get the size of the assembled machine code.
    // @return: Size in bytes.
    u64 size() const;

    // Get the line number for a given offset in the code.
    // @param offset: The offset to map.
    // @return: The line number corresponding to the offset. If offset cannot be
    // mapped to any line, 0 is returned.
    u64 offsetToLine(u64 const offset) const;

    // Get the filename.
    std::string const& fileName() const;

private:
    // Path to the source file.
    std::string file;

    // Raw machine code.
    std::shared_ptr<u8> code;

    // Size in bytes of the raw machine code.
    u64 codeSize;

    // Map of instruction pointer to instruction / line numbers.
    std::unique_ptr<InstructionMap const> map;
};
}
