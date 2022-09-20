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

// Shorthand for the uintX_t types.
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using u128 = __uint128_t;

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
}

// Collection of helper functions to interact with the KVM API.
namespace Kvm {
// Get a KVM handle.
// @return: A file descriptor on /dev/kvm.
// @throws: A X86Lab::Error in case /dev/kvm cannot be opened.
int getKvmHandle();

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
}
}
}
