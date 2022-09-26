#pragma once
#include <stdint.h>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <string>
#include <linux/kvm.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fstream>
#include <memory>

// Shorthand for the uintX_t types.
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using u128 = __uint128_t;
using u256 = u128[2];

// Concept for a type that can be an element of a vector register. For MMX, SSE
// and AVX the packed elements are either: bytes, words, dwords, qwords, float
// and double.
template<typename T>
concept isPackable = std::is_same<T, u8>::value ||
                     std::is_same<T, u16>::value ||
                     std::is_same<T, u32>::value ||
                     std::is_same<T, u64>::value ||
                     std::is_same<T, double>::value ||
                     std::is_same<T, float>::value;

// Representation of a vector register of a given width (Bits). This is meant to
// replace the built-in types __uint128_t and intrisic types which are usually
// implemented as array of smaller types (e.g. u256 is u32[4]). which makes
// comparison, access and assignment difficult.
template<size_t Bits>
class vec {
    // Vector registers only come in the following sizes: 64 bits (MMX), 128
    // bits (XMM) and 256 bits (YMM). For now we do not support AVX512.
    static_assert(Bits == 64 || Bits == 128 || Bits == 256,
                  "Invalid vector register width");
public:
    // Default value of a vector register: all bytes are zeroed.
    vec() {
        ::memset(m_data, 0, bytes);
    }

    // Create a vector from an array of bytes. This constructure assumes that
    // data contains at least `this::bytes` elements.
    // @param data: The data to initialize the vector with.
    vec(u8 const * const data) {
        ::memcpy(m_data, data, bytes);
    }

    // Construct a vector register values from a list of elements to be packed
    // in the vector reg. Each element must be of the same type (e.g u8, u16,
    // ...) and the number of elements must add up to the size of the vec
    // register.
    // @param high...rest: The elements of the vec register, in "big endian" e.g
    // the first value represents the value of the high/most-significant
    // element in the vector.
    template<typename A, typename... Args>
    requires ((std::is_same<A, Args>::value && ...) && isPackable<A> &&
              ((1 + sizeof...(Args)) * sizeof(A) * 8 == Bits))
    vec(A high, Args ...rest) {
        initArray(high, rest...);
    }

    // The size of the vector in granularity of T's.
    // Note: Enabling the requires below seems to trigger a bug with clang.
    template<typename T> // requires isPackable<T>
    static constexpr u64 size = Bits / (sizeof(T) * 8);

    // The size of the vector in bytes.
    static constexpr u64 bytes = size<u8>;

    // Access an element in the vec register (non-const).
    // @param index: The index of the element to access.
    // @return: Reference to the index'th element of type T in the vec register.
    template<typename T> requires isPackable<T>
    T& elem(u8 const index) {
        assert(index < size<T>);
        return *(reinterpret_cast<T*>(m_data) + index);
    }

    // Access an element in the vec register (const).
    // @param index: The index of the element to access.
    // @return: Const reference to the index'th element of type T in the vec
    // register.
    template<typename T> requires isPackable<T>
    T const& elem(u8 const index) const {
        assert(index < size<T>);
        return *(reinterpret_cast<T const*>(m_data) + index);
    }

    // Compare two vector register of the same width. Let the compiler defines
    // this for us.
    bool operator==(vec<Bits> const& other) const = default;

    // Check if a vector is non-zero.
    // @return true: If the vector contains at least one byte != 0, false
    // otherwise.
    explicit operator bool() const {
        for (u8 i(0); i < size<u64>; ++i) {
            if (!!elem<u64>(i)) {
                return true;
            }
        }
        return false;
    }

private:
    // The underlying data of the register.
    u8 m_data[size<u8>];

    // Internal - Initialize the m_data array recursively. This function is
    // meant to be called by the constructor. As such it makes the following
    // assumptions:
    //  - The size of the arguments is <= size of the vector.
    //  - All arguments are of the same type.
    template<typename A, typename... Args>
    void initArray(A first, Args ...rest) {
        u8 const currIndex(sizeof...(Args));
        elem<A>(currIndex) = first;
        initArray(rest...);
    }

    // Base case for initArray when the argument list is empty.
    void initArray() {}
};

// Some shortcuts for the common vector register widths.
using vec64 = vec<64>;
using vec128 = vec<128>;
using vec256 = vec<256>;

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

namespace Util {

// RAII class for a temporary file. The file is deleted in the destructor.
class TempFile {
public:
    // Create a TempFile. The resulting file name is named as <pathPrefix>XXXXXX
    // where XXXXXX is replaced with a random string.
    // @param pathPrefix: A path name prefix to use for the temporary file's
    // name.
    TempFile(std::string const& pathPrefix);

    // Close the file.
    ~TempFile();

    // Get the full path to the temporary file.
    // @return: An absolute path to the file.
    std::string const& path() const;

    // Get an std::ifstream on this file, initially pointing at the beginning of
    // the file.
    // @param mode: The open mode to use for the stream.
    // @return: A new ifstream on this file.
    std::ifstream istream(std::ios_base::openmode const mode = std::ios::in);

    // Get an std::ofstream on this file, initially pointing at the beginning of
    // the file.
    // @param mode: The open mode to use for the stream.
    // @return: A new ofstream on this file.
    std::ofstream ostream(std::ios_base::openmode const mode = std::ios::out);

private:
    static const std::string suffix;

    // Absolute path to the file.
    std::string m_absPath;
};

// Functions related to x86 extension support.
namespace Extension {
// Check extension support on the current cpu.
// @return: true if the extension is supported, false otherwise.
bool hasMmx();
bool hasSse();
bool hasSse2();
bool hasSse3();
bool hasSsse3();
bool hasSse4_1();
bool hasSse4_2();
bool hasAvx();
bool hasAvx2();
}

// Collection of helper functions to interact with the KVM API.
namespace Kvm {
// Get a KVM handle.
// @return: A file descriptor on /dev/kvm.
// @throws: A X86Lab::Error in case /dev/kvm cannot be opened.
int getKvmHandle();

// Check if the KVM module support a certain extension. This essentially calls
// the KVM_CHECK_EXTENSION ioctl.
// @param fd: The file descriptor on which to invoke KVM_CHECK_EXTENSION.
// @param capability: The capability to check for.
// @throws: A KvmError in case of error.
// @return: The value returned by ::ioctl(fd, KVM_CHECK_EXTENSION, capability).
int checkExtension(int const fd, int const capability);

// Asserts that an extension is supported on the host's KVM API. If the required
// extension is not supported, a KvmError is thrown.
// @param fd: The file descriptor on which to invoke KVM_CHECK_EXTENSION.
// @param capability: The capability to check for.
void requiresExension(int const fd, int const capability);

// Create a KVM VM.
// @param kvmHandle: File descriptor on /dev/kvm for ioctl calls.
// @return: The file descriptor associated to the created VM.
// @throws: An Error in case of error.
int createVm();

// Create a Vcpu to a KVM Vm.
// @param vmFd: File descriptor on the KVM Vm for which to add the vcpu to.
// @return: The file descriptor associated to the vcpu.
// @throws: An Error in case of error.
int createVcpu(int const vmFd);

// Mmap the run structure of the vcpu. Assumes that vCpu has been initialized.
// @param kvmHandle: File descriptor on /dev/kvm for ioctl calls.
// @param vcpuFd: File descriptor for the vCpu.
// @return: A reference to the kvm_run structure associated with vcpuFd.
// @throws: An Error in case of error.
kvm_run& getVcpuRunStruct(int const vcpuFd);

// Disable Model-Specific-Register filtering. This gives the guest access to its
// own set of MSR without trapping to the host.
// @param vmFd: The VM's file descriptor.
// @throws: An Error in case of error.
void disableMsrFiltering(int const vmFd);

// Setup the CPUID on the guest vcpu to mirror the host's capabilities.
// @param vcpuFd: The virtual CPU's file descriptor to set the CPUID caps to.
// @throws: An Error in case of error.
void setupCpuid(int const vcpuFd);

// Get the register values of a Vcpu (KVM_GET_REGS).
// @param vcpuFd: The file descriptor of the target vcpu.
// @return: A kvm_regs holding the current values of the registers on the
// VM. Note: This returns by value!
// @throws: A KvmError in case of error.
kvm_regs getRegs(int const vcpuFd);

// Set a vcpu's register values (KVM_SET_REGS).
// @param vcpuFd: The file descriptor of the target vcpu.
// @param regs: The kvm_regs to write.
// @throws: A KvmError in case of error.
void setRegs(int const vcpuFd, kvm_regs const& regs);

// Get the special register values of a Vcpu (KVM_GET_SREGS).
// @param vcpuFd: The file descriptor of the target vcpu.
// @return: A kvm_sregs holding the current values of the special registers
// on the VM. Note: This returns by value!
// @throws: A KvmError in case of error.
kvm_sregs getSRegs(int const vcpuFd);

// Set a vcpu's special register values (KVM_SET_SREGS).
// @param vcpuFd: The file descriptor of the target vcpu.
// @param regs: The kvm_sregs to write.
// @throws: A KvmError in case of error.
void setSRegs(int const vcpuFd, kvm_sregs const& regs);

// Get the maximum number of memory slots supported by the vm.
// @param vmFd: The file descriptor for the vm.
// @return: The maximum number of memory slots supported by vmFd.
// @throws: A KvmError in case of error.
u16 getMaxMemSlots(int const vmFd);

// Get the state of the vcpu's FPU. This calls the KVM_GET_FPU ioctl.
// @param vcpuFd: The file descriptor of the target vcpu.
// @return: A kvm_fpu struct containing the current state of the vcpu's FPU.
// @throws: A KvmError in case of error.
kvm_fpu getFpu(int const vcpuFd);

// Set the state of the FPU on a vcpu. This calls the KVM_SET_FPU ioctl.
// @param vcpuFd: The file descriptor of the target vcpu.
// @param fpu: The FPU state to set on the vcpu.
// @throws: A KvmError in case of error.
void setFpu(int const vcpuFd, kvm_fpu const& fpu);

// Memory layout of the XSAVE area returned by KVM_GET_XSAVE2. This is used to
// fish the register values we are interested in, without having to compute the
// specific offsets needed within kvm_xsave.region[].
struct XSaveArea {
    // Default ctor - set all state to 0.
    XSaveArea();

    // Create a XSaveArea.
    // @param xsave: The xsave data returned by KVM_GET_XSAVE2.
    XSaveArea(kvm_xsave const& xsave);

    // Update a kvm_xsave struct with the state contained in this XSaveArea.
    // This essentially implements the inverse of the constructor taking a
    // kvm_xsave as parameter.
    // @param xsave: Pointer to the kvm_xsave to be updated.
    void fillKvmXSave(kvm_xsave * const xsave) const;

    // The MMX registers.
    vec64 mmx[8];
    // SSE's MXCSR register and its mask.
    u32 mxcsr;
    u32 mxcsrMask;
    // The YMM registers, also contains the XMM registers in their lower 128
    // bits.
    vec256 ymm[16];
} __attribute__((packed));

// Get the vcpu's XSAVE area. This calls the KVM_GET_XSAVE2 ioctl.
// @param vcpuFd: The file descriptor of the target vcpu.
// @return: A XSaveArea containing the vcpu's current XSAVE.
// @throws: A KvmError in case of error.
std::unique_ptr<XSaveArea> getXSave(int const vcpuFd);

// Set the vcpu's XSAVE area. This calls the KVM_SET_XSAVE ioctl.
// @param vcpuFd: The file descriptor of the target vcpu.
// @param xsave: The XSAVE area to write to the vcpu.
// @throws: A KvmError in case of error.
void setXSave(int const vcpuFd, XSaveArea const& xsave);

// Get the current value of vcpu's XCR0 register. This calls KVM_GET_XCRS.
// @param vcpuFd: The file descriptor of the target vcpu.
// @return XCR0.
// @throws: A KvmError in case of error.
u64 getXcr0(int const vcpuFd);

// Set the value of vcpu's XCR0 register. This calls KVM_SET_XCRS.
// @param vcpuFd: The file descriptor of the target vcpu.
// @param xcr0: The value to write into XCR0.
// @throws: A KvmError in case of error.
void setXcr0(int const vcpuFd, u64 const xcr0);

}
}
}
