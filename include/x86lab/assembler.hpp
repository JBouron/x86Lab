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
    // Create an instance of Code. This constructor is meant to be used by the
    // assemble() function only, hence why it is private and assemble() is
    // friend of this class.
    Code(std::string const& fileName,
         std::vector<u8> const& code,
         std::unique_ptr<InstructionMap const> map);
    friend std::unique_ptr<Code> assemble(std::string const& code);

    std::string file;

    // Raw machine code.
    std::vector<u8> code;

    // Map of instruction pointer to instruction / line numbers.
    std::unique_ptr<InstructionMap const> map;
};

// Assembles the code in `fileName`. The content of the file must be understood
// by NASM.
// @param fileName: Path to the file to compile.
// @return: The resulting Code instance.
std::unique_ptr<Code> assemble(std::string const& fileName);
}
