#pragma once
#include <map>
#include <memory>
#include <x86lab/util.hpp>
#include <x86lab/assembler.hpp>

namespace X86Lab {
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
    std::string m_file;

    // Raw machine code.
    std::shared_ptr<u8> m_code;

    // Size in bytes of the raw machine code.
    u64 m_codeSize;

    // Map of instruction pointer to instruction / line numbers.
    std::unique_ptr<Assembler::InstructionMap const> m_map;
};
}
