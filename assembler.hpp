#pragma once
#include <vector>

#include "util.hpp"

namespace X86Lab::Assembler {
// Wrapper around some assembled code that should be run by a VM.
class Code {
public:
    // Get a pointer to the assembled machine code.
    // @return: A const pointer to the code. 
    u8 const* machineCode() const;

    // Get the size of the assembled machine code.
    // @return: Size in bytes.
    u64 size() const;

private:
    // Create an instance of Code. This constructor is meant to be used by the
    // assemble() function only, hence why it is private and assemble() is
    // friend of this class.
    Code(std::vector<u8> && code);
    friend Code assemble(std::string const& code);

    // Raw machine code.
    std::vector<u8> const code;
};

// Assembles the code in `fileName`. The content of the file must be understood
// by NASM.
// @param fileName: Path to the file to compile.
// @return: The resulting Code instance.
Code assemble(std::string const& fileName);
}
