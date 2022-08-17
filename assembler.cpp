#include "assembler.hpp"
#include <fstream>
#include <array>
#include <iostream>
#include <sys/stat.h>

namespace X86Lab::Assembler {
Code::Code(std::vector<u8> && code) : code(code) {}

u8 const* Code::machineCode() const {
    return code.data();
}

u64 Code::size() const {
    return code.size();
}

// Get a unique file name that is not in use under /tmp.
// @return: The file name.
static std::string getUniqueFileName() {
    // FIXME: std::tmpnam can lead to race conditions.
    return std::tmpnam(NULL);
}

Code assemble(std::string const& fileName) {
    // Output file to be used by the NASM assembler.
    std::string const outputFileName(getUniqueFileName());
    // Use binary output format ("-f bin") so that the resulting code only
    // contains the raw machine code.
    std::string const cmd("nasm -f bin " + fileName + " -o " + outputFileName);
    if (!!std::system(cmd.c_str())) {
        throw Error("Could not invoke assembler", errno);
    }

    // Get size of machine code.
    struct stat outputFileStat;
    if (::stat(outputFileName.c_str(), &outputFileStat) == -1) {
        throw Error("Could not get assembler output file size", errno);
    }
    u64 const outputFileSize(outputFileStat.st_size);

    // Now read the machine code from the output file.
    std::ifstream output(outputFileName, std::ios::in | std::ios::binary);
    if (!output) {
        throw Error("Could not open assembler output file", errno);
    }

    // Insert the bytes of the file into `code`.
    std::vector<u8> code(outputFileSize, 0x00);
    output.read(reinterpret_cast<char*>(code.data()), code.size());
    std::cout << "Machine code is " << code.size() << " bytes long" << std::endl;
    return Code(std::move(code));
}
}
