#include <x86lab/assembler.hpp>
#include <fstream>
#include <array>
#include <iostream>
#include <sys/stat.h>
#include <sstream>

namespace X86Lab::Assembler {
Code::Code(std::string const& fileName,
           std::vector<u8> const& code,
           std::unique_ptr<InstructionMap const> map) :
    file(fileName),
    code(code),
    map(std::move(map)) {}

u8 const* Code::machineCode() const {
    return code.data();
}

u64 Code::size() const {
    return code.size();
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

// Parse a listfile generated by NASM and generate an InstructionMap from it.
// @param listFilePath: The path to the listfile to be parsed.
// @return: An instance of InstructionMap filled with the parsed info.
static std::unique_ptr<InstructionMap const> parseListFile(
    std::string const& listFilePath) {
    InstructionMap* const map(new InstructionMap());

    std::ifstream listFile(listFilePath, std::ios::in);
    if (!listFile) {
        throw Error("Could not open listfile", errno);
    }

    // List files generated by NASM are formatted so that there are always 40
    // characters before the first char of the instruction listing.
    // FIXME: This is true as long as there are less than 1 million lines in the
    // file. For now this should be fine.
    u32 const instructionListingStartIdx(40);

    std::string line;
    while (std::getline(listFile, line)) {
        if (line.size() < instructionListingStartIdx) {
            // There are two cases where a line is less than
            // instructionListingStartIdx bytes long:
            //  1. This is an empty line.
            //  2. This is a continuation of the previous line. This happens if
            //  the instruction is too large (number of hex code).
            // In both case we simply skip the line.
            continue;
        }
        // header is the line, offset and machine code, e.g. everything before
        // the instruction listing.
        std::string const header(line.substr(0, instructionListingStartIdx));

        // There is always a line number.
        u64 lineNumber;
        std::istringstream iss(header);
        iss >> lineNumber;
        if (!iss) {
            throw Error("Failed to parse listfile", errno);
        }

        // There isn't always an address, this is the case for some directives
        // like "BITS 64" and the like.
        u64 offset;
        iss >> std::hex;
        iss >> offset;
        if (!!iss) {
            // There is an offset, hence this could be an instruction (not
            // necessarily, as this could also be a directive inserting a value
            // at a specific address).
            assert(!map->contains(offset));
            map->emplace(offset, lineNumber);
        }
        // If there is no offset, then this is not an instruction and no need to
        // insert it in the map.
    }
    return std::unique_ptr<InstructionMap const>(map);
}

// Get a unique file name that is not in use under /tmp.
// @return: The file name.
static std::string getUniqueFileName() {
    // FIXME: std::tmpnam can lead to race conditions.
    return std::tmpnam(NULL);
}

std::unique_ptr<Code> assemble(std::string const& fileName) {
    // Output file to be used by the NASM assembler.
    std::string const outputFileName(getUniqueFileName());
    // Use binary output format ("-f bin") so that the resulting code only
    // contains the raw machine code.
    std::string const listFileName(getUniqueFileName());
    std::string const cmd("nasm -f bin -l " + listFileName + " " + fileName +
        " -o " + outputFileName);
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

    std::unique_ptr<InstructionMap const> map(parseListFile(listFileName));
    return std::unique_ptr<Code>(new Code(fileName, code, std::move(map)));
}
}
