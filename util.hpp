#include <stdint.h>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <string>

// Shorthand for the uintX_t types.
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

namespace X86Lab {

// Custom exception types.
class Error : public std::runtime_error {
public:
    Error(std::string const& what, int const errNo) :
        std::runtime_error(what),
        errNo(errNo) {}
    int errNo;
};

// Exception due to KVM ioctl call failure.
class KvmError : public Error {
public:
    KvmError(std::string const& what, int const errNo) : Error(what, errNo) {}
};

// Exception due to Mmap failure.
class MmapError : public Error {
public:
    MmapError(std::string const& what, int const errNo) : Error(what, errNo) {}
};

}
