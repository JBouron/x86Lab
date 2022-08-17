#pragma once
#include <vector>
#include <map>

#include <x86lab/util.hpp>

namespace X86Lab::Assembler {
// Holds the mapping between instructions and addresses and source line numbers.
// This is nothing more than a map offset -> entry, where the offset is the
// value of the instruction pointer/address and entry contains:
//  - The line number in the source file
//  - Listing of the instruction.
class InstructionMap {
public:
    // Construct an empty map.
    InstructionMap();

    // Entry in the map.
    class Entry {
    public:
        // The default entry. This is used as a sentinel value to indicate that
        // there is no entry associated to a particular offset/address.
        Entry();

        // Create an entry.
        // @param line: The line that correspond to the offset associated with
        // this entry.
        // @param instruction: The instruction listing for this line/entry.
        Entry(u64 const line, std::string const& instruction);

        // Test if this entry is the sentinel value or not.
        // @return: true if the entry correspond to a real instruction in the
        // source file, false otherwise.
        operator bool() const;

        u64 const line;
        std::string const instruction;
    };

    // Add an entry to the Instruction map.
    // @param offset: The offset/address associated with the entry.
    // @param entry: The entry to be added.
    void addEntry(u64 const offset, Entry const& entry);

    // Map the value of the instruction pointer to the corresponding entry in
    // the map.
    // @param ip: The instruction pointer.
    // @return: The associated entry. If the instruction pointer does not map to
    // any instruction/line from the source file then the returned entry is the
    // default entry.
    Entry const& mapInstructionPointer(u64 const ip) const;

private:
    // The value to return in mapInstructionPointer if no mapping exists.
    Entry const sentinel;
    // The underlying map of entries.
    std::map<u64, Entry> map;
};

// Wrapper around some assembled code that should be run by a VM.
class Code {
public:
    // Get a pointer to the assembled machine code.
    // @return: A const pointer to the code. 
    u8 const* machineCode() const;

    // Get the size of the assembled machine code.
    // @return: Size in bytes.
    u64 size() const;

    // Get a reference to the InstructionMap associated to this code.
    // @return: const ref to the map.
    InstructionMap const& getInstructionMap() const;

private:
    // Create an instance of Code. This constructor is meant to be used by the
    // assemble() function only, hence why it is private and assemble() is
    // friend of this class.
    Code(std::vector<u8> && code, InstructionMap const * const map);
    friend Code assemble(std::string const& code);

    // Raw machine code.
    std::vector<u8> const code;

    // Map of instruction pointer to instruction / line numbers.
    InstructionMap const * const map;
};

// Assembles the code in `fileName`. The content of the file must be understood
// by NASM.
// @param fileName: Path to the file to compile.
// @return: The resulting Code instance.
Code assemble(std::string const& fileName);
}
