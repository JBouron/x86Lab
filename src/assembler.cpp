#include <x86lab/assembler.hpp>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sstream>
#include <vector>
#include <unistd.h>

namespace X86Lab::Assembler {
// Implementation of X86Lab::Assembler::invoke using the NASM assembler.

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

// Run the NASM assembler with the given argument and wait for it to exit.
// @param args: Cmd-line arguments to pass to NASM. args[i] will be mapped to
// argv[i+1] when calling nasm (e.g. args[0] is the first arg, not the name of
// the executable).
// @throw: An Error in case the call to nasm failed.
static void runNasm(std::vector<char const *> const& args) {
    std::vector<char const *> argv(args);
    argv.insert(argv.begin(), "nasm");
    argv.push_back(NULL);
    int const childPid(fork());
    if (childPid == -1) {
        throw Error("Couldn't fork nasm", errno);
    }
    if (!childPid) {
        // This is the child process, run the assembler.
        ::execvp("nasm", const_cast<char *const *>(argv.data()));
        // In case of error return 1 to the parent.
        std::exit(1);
    }

    int wstatus;
    do {
        if (waitpid(childPid, &wstatus, 0) == -1) {
            throw Error("Failed to call waitpid", errno);
        }
    } while(!WIFEXITED(wstatus));

    int const childRet(WEXITSTATUS(wstatus));
    if (!!childRet) {
        throw Error("nasm returned error", errno);
    }
}

std::tuple<std::unique_ptr<u8>, u64, std::unique_ptr<InstructionMap const>>
invoke(std::string const& filePath) {
    // Output file to be used by the NASM assembler.
    Util::TempFile outputFile("/tmp/x86lab_assemblerOutput");
    // Output list file to be used by NASM.
    Util::TempFile listFile("/tmp/x86lab_listFile");
    // Use binary output format ("-f bin") so that the output file only contains
    // the raw machine code.
    std::vector<char const *> const args({
        "-f", "bin",
        "-l", listFile.path().c_str(),
        filePath.c_str(),
        "-o", outputFile.path().c_str(),
    });
    runNasm(args);

    // Get size of machine code.
    struct stat outputFileStat;
    if (::stat(outputFile.path().c_str(), &outputFileStat) == -1) {
        throw Error("Could not get assembler output file size", errno);
    }
    u64 const outputFileSize(outputFileStat.st_size);

    // Now read the machine code from the output file.
    std::ifstream output(outputFile.istream(std::ios::in | std::ios::binary));
    if (!output) {
        throw Error("Could not open assembler output file", errno);
    }

    // Insert the bytes of the file into `code`.
    std::unique_ptr<u8> code(new u8[outputFileSize]);
    output.read(reinterpret_cast<char*>(code.get()), outputFileSize);
    std::unique_ptr<InstructionMap const> map(parseListFile(listFile.path()));

    return {std::move(code), outputFileSize, std::move(map)};
}
}
