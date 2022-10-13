#include <x86lab/util.hpp>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <x86lab/vm.hpp>

namespace X86Lab::Util {

// ::mkstemp requires exactly six X chars.
std::string const TempFile::suffix("XXXXXX");

TempFile::TempFile(std::string const& pathPrefix) :
    m_absPath(pathPrefix + suffix) {
    // We don't need to keep the file descriptor around.
    if (::mkstemp(m_absPath.data()) == -1) {
        throw Error("Could not create temporary file", errno);
    }
    char const * absPathStr(realpath(m_absPath.c_str(), nullptr));
    if (!absPathStr) {
        throw Error("Could not compute absolute path to temporary file", errno);
    }
    m_absPath = absPathStr;
}

TempFile::~TempFile() {
    // Ignore any error because of noexcept.
    ::unlink(m_absPath.c_str());
}

std::string const& TempFile::path() const {
    return m_absPath;
}

std::ifstream TempFile::istream(std::ios_base::openmode const mode) {
    return std::ifstream(m_absPath.c_str(), mode);
}

std::ofstream TempFile::ostream(std::ios_base::openmode const mode) {
    return std::ofstream(m_absPath.c_str(), mode);
}

namespace Extension {
// Holds the result of a CPUID instruction.
struct CpuidResult {
    u32 eax;
    u32 ebx;
    u32 ecx;
    u32 edx;
} __attribute__((packed));

// Implementation of cpuid() to be called by cpuid() only.
extern "C" void _cpuid(u32 const eax, u32 const ecx, CpuidResult * const dest);

// Execute the cpuid instruction.
// @param eax: The parameter for CPUID, the value that is loaded into EAX before
// executing the CPUID instruction.
// @param ecx: The ECX parameter for CPUID.
// @return: A CpuidResult containing the values returned by CPUID in the eax,
// ebx, ecx and edx registers.
static CpuidResult cpuid(u32 const eax, u32 const ecx) {
    CpuidResult res{};
    _cpuid(eax, ecx, &res);
    return res;
}

bool hasMmx()       { return !!(cpuid(0x1, 0x0).edx & (1 << 23)); }
bool hasSse()       { return !!(cpuid(0x1, 0x0).edx & (1 << 25)); }
bool hasSse2()      { return !!(cpuid(0x1, 0x0).edx & (1 << 26)); }
bool hasSse3()      { return !!(cpuid(0x1, 0x0).ecx & (1 << 0));  }
bool hasSsse3()     { return !!(cpuid(0x1, 0x0).ecx & (1 << 9));  }
bool hasSse4_1()    { return !!(cpuid(0x1, 0x0).ecx & (1 << 19)); }
bool hasSse4_2()    { return !!(cpuid(0x1, 0x0).ecx & (1 << 20)); }
bool hasAvx()       { return !!(cpuid(0x1, 0x0).ecx & (1 << 28)); }
bool hasAvx2()      { return !!(cpuid(0x7, 0x0).ecx & (1 << 5));  }
// Note: We do a crude check regarding AVX512 by only looking for AVX512
// foundation instruction support.
bool hasAvx512()    { return !!(cpuid(0x7, 0x0).ebx & (1 << 16)); }
}

namespace Kvm {

int getKvmHandle() {
    static int kvmHandle(0);
    if (!kvmHandle) {
        // We only really need to open /dev/kvm once. Then each VM and Vcpu can
        // reuse this handle.
        kvmHandle = ::open("/dev/kvm", O_RDWR | O_CLOEXEC);
        if (kvmHandle == -1) {
            throw KvmError("Cannot open /dev/kvm", errno);
        }
    }
    return kvmHandle;
}

int checkExtension(int const fd, int const capability) {
    int const res(::ioctl(fd, KVM_CHECK_EXTENSION, capability));
    if (res == -1) {
        // KVM_CHECK_EXTENSION either returns 0 if the extension is not
        // supported or an integer >= 1 if it is.
        // -1 indicates an error.
        throw KvmError("Error calling KVM_CHECK_EXTENSION", errno);
    } else {
        return res;
    }
}

void requiresExension(int const fd, int const capability) {
    if (!checkExtension(fd, capability)) {
        throw KvmError(("Required extension " + std::to_string(capability) +
            " not supported").c_str(), errno);
    }
}

int createVm() {
    int const vmFd(::ioctl(getKvmHandle(), KVM_CREATE_VM, 0));
    if (vmFd == -1) {
        throw KvmError("Cannot create VM", errno);
    } else {
        return vmFd;
    }
}

int createVcpu(int const vmFd) {
    int const vcpuFd(::ioctl(vmFd, KVM_CREATE_VCPU, 0));
    if (vcpuFd == -1) {
        throw KvmError("Cannot create VCPU", errno);
    } else {
        return vcpuFd;
    }
}

kvm_run& getVcpuRunStruct(int const vcpuFd) {
    int const vcpuRunSize(::ioctl(getKvmHandle(), KVM_GET_VCPU_MMAP_SIZE, 0));
    if (vcpuRunSize == -1) {
        throw KvmError("Cannot get kvm_run structure size", errno);
    }
    int const prot(PROT_READ | PROT_WRITE);
    int const flags(MAP_PRIVATE);
    void * const mapRes(::mmap(NULL, vcpuRunSize, prot, flags, vcpuFd, 0));
    if (mapRes == MAP_FAILED) {
        throw MmapError("Failed to mmap kvm_run structure", errno);
    } else {
        return *reinterpret_cast<kvm_run*>(mapRes);
    }
}

void disableMsrFiltering(int const vmFd) {
    kvm_msr_filter msrFilter{};
    // Setting all ranges to 0 disable filtering. In this case flags must be
    // zero as well.
    for (size_t i(0); i < KVM_MSR_FILTER_MAX_RANGES; ++i) {
        msrFilter.ranges[i].nmsrs = 0;
    }
    if (::ioctl(vmFd, KVM_X86_SET_MSR_FILTER, &msrFilter) == -1) {
        throw KvmError("Failed to allow MSR access", errno);
    }
}

void setupCpuid(int const vcpuFd) {
    size_t nent(32);
    kvm_cpuid2 * kvmCpuid(nullptr);
    // KVM_GET_SUPPORTED_CPUID is a bit weird as we need to pass it a kvm_cpuid2
    // that is big enough to contain the result. Essentially we are left to
    // guess the value of kvm_cpuid2::nent needed.
    // Per the KVM docs, if nent is too small, E2BIG is returned; conversely if
    // nent is too bit ENOMEM is returned. Hence start with a base nent and
    // double it until the dest is big enough to contain the data.
    int ret(0);
    do {
        // If the struct was allocated in a previous run, then free it. First
        // iteration is fine as free(NULL) is allowed.
        free(kvmCpuid);
        size_t const structSize(
            sizeof(kvm_cpuid2) + nent * sizeof(kvm_cpuid_entry2));
        kvmCpuid = reinterpret_cast<kvm_cpuid2*>(malloc(structSize));
        std::memset(kvmCpuid, 0x0, structSize);
        kvmCpuid->nent = nent;
        ret = ::ioctl(getKvmHandle(), KVM_GET_SUPPORTED_CPUID, kvmCpuid);
        nent *= 2;
    } while (ret == -1 && errno == E2BIG);
    if (ret != 0) {
        throw KvmError("Failed to get supported CPUID", errno);
    }

    // Now set the CPUID capabilities.
    if (::ioctl(vcpuFd, KVM_SET_CPUID2, kvmCpuid) == -1) {
        throw KvmError("Failed to set supported CPUID", errno);
    }
}

kvm_regs getRegs(int const vcpuFd) {
    kvm_regs regs{};
    if (::ioctl(vcpuFd, KVM_GET_REGS, &regs) == -1) {
        throw KvmError("Cannot get guest registers", errno);
    }
    return regs;
}

void setRegs(int const vcpuFd, kvm_regs const& regs) {
    if (::ioctl(vcpuFd, KVM_SET_REGS, std::addressof(regs)) == -1) {
        throw KvmError("Cannot set guest registers", errno);
    }
}

kvm_sregs getSRegs(int const vcpuFd) {
    kvm_sregs sregs{};
    if (::ioctl(vcpuFd, KVM_GET_SREGS, &sregs) == -1) {
        throw KvmError("Cannot get guest special registers", errno);
    }
    return sregs;
}

void setSRegs(int const vcpuFd, kvm_sregs const& regs) {
    if (::ioctl(vcpuFd, KVM_SET_SREGS, std::addressof(regs)) == -1) {
        throw KvmError("Cannot set guest special registers", errno);
    }
}

u16 getMaxMemSlots(int const vmFd) {
    return checkExtension(vmFd, KVM_CAP_NR_MEMSLOTS);
}

XSaveArea::XSaveArea() {
    for (u8 i(0); i < Vm::State::Registers::NumMmxRegs; ++i) {
        mmx[i] = vec64(u32(0), u32(0));
    }
    mxcsr = 0;
    mxcsrMask = 0;
    for (u8 i(0); i < Vm::State::Registers::NumZmmRegs; ++i) {
        zmm[i] = vec512(u64(0), u64(0), u64(0), u64(0),
                        u64(0), u64(0), u64(0), u64(0));
    }
    for (u8 i(0); i < Vm::State::Registers::NumKRegs; ++i) {
        k[i] = 0;
    }
}

XSaveArea::XSaveArea(kvm_xsave const& xsave) {
    // Intel's reference vol. 1, chapter 13-6, describes the offset of the state
    // of the FPU and other vector registers in the XSAVE area.
    u8 const * const state(reinterpret_cast<u8 const*>(xsave.region));

    // MMX registers: MMi is at offset 32 + 16 * i.
    u32 const mmxBase(32);
    for (u8 i(0); i < Vm::State::Registers::NumMmxRegs; ++i) {
        u32 const mmxiOff(mmxBase + i * 16);
        mmx[i] = *reinterpret_cast<vec64 const*>(state + mmxiOff);
    }

    // For i < 16, XMMi, YMMi and ZMMi are sharing their bottom 128 bits. For
    // this reason, XSAVE does not duplicate this state and instead stores the
    // following info at different offsets:
    //  - The full XMMi             @ 160 + i * 16.
    //  - The top 128 bits of YMMi  @ $(cpuid(eax = 0xD, ecx = 2).ebx) + i * 16.
    //  - The top 256 bits of ZMMi  @ $(cpuid(eax = 0xD, ecx = 6).ebx) + i * 32.
    // We therefore need to parse all three to reconstruct the full state of
    // ZMMi.
    u32 const xmmBase(160);
    u32 const ymmHighHalfBase(Extension::cpuid(0xD, 0x2).ebx);
    u32 const zmmHighHalfBase(Extension::cpuid(0xD, 0x6).ebx);
    for (u8 i(0); i < Vm::State::Registers::NumXmmRegs; ++i) {
        // XMMi
        u32 const xmmiOff(xmmBase + i * 16);
        vec128 const xmmi(*reinterpret_cast<vec128 const*>(state + xmmiOff));

        // YMMi
        u32 const ymmiHiOff(ymmHighHalfBase + i * 16);
        vec128 const ymmiHi(
            *reinterpret_cast<vec128 const*>(state + ymmiHiOff));

        // ZMMi
        u32 const zmmiHiOff(zmmHighHalfBase + i * 32);
        vec256 const zmmiHi(
            *reinterpret_cast<vec256 const*>(state + zmmiHiOff));
        zmm[i] = vec512(zmmiHi.elem<u64>(3),
                        zmmiHi.elem<u64>(2),
                        zmmiHi.elem<u64>(1),
                        zmmiHi.elem<u64>(0),
                        ymmiHi.elem<u64>(1),
                        ymmiHi.elem<u64>(0),
                        xmmi.elem<u64>(1),
                        xmmi.elem<u64>(0));
    }

    // MXCSR and its mask are stored at offset 24.
    mxcsr = *reinterpret_cast<u32 const*>(state + 24);
    mxcsrMask = *reinterpret_cast<u32 const*>(state + 28);

    // AVX512:
    // For i >= 16, ZMMi is fully stored at offset:
    //  $(cpuid(eax = 0xD, ecx = 7).ebx) + i * 64.
    // ZMMi for i < 16 we parsed above along with YMMi and XMMi.
    u64 const highZmmBase(Extension::cpuid(0xD, 0x7).ebx);
    for (u8 i(0); i < Vm::State::Registers::NumZmmRegs / 2; ++i) {
        u32 const zmmiOff(highZmmBase + i * 64);
        zmm[i + 16] = *(reinterpret_cast<vec512 const*>(state + zmmiOff));
    }
    // OPmask ki is @ $(cpuid(eax = 0xD, ecx = 5).ebx) + i * 8.
    u32 const opMaskBase(Extension::cpuid(0xD, 0x5).ebx);
    for (u8 i(0); i < Vm::State::Registers::NumKRegs; ++i) {
        u32 const kiOff(opMaskBase + i * 8);
        k[i] = *(reinterpret_cast<u64 const*>(state + kiOff));
    }
}

void XSaveArea::fillKvmXSave(kvm_xsave * const xsave) const {
    // Intel's reference vol. 1, chapter 13-6, describes the offset of the state
    // of the FPU and other vector registers in the XSAVE area.
    u8 * const state(reinterpret_cast<u8*>(xsave->region));

    // MMX registers: MMi is at offset 32 + 16 * i.
    u32 const mmxBase(32);
    for (u8 i(0); i < Vm::State::Registers::NumMmxRegs; ++i) {
        u32 const mmxiOff(mmxBase + i * 16);
        *reinterpret_cast<vec64*>(state + mmxiOff) = mmx[i];
    }

    // For i < 16, XMMi, YMMi and ZMMi are sharing their bottom 128 bits. For
    // this reason, XSAVE does not duplicate this state and instead stores the
    // following info at different offsets:
    //  - The full XMMi             @ 160 + i * 16.
    //  - The top 128 bits of YMMi  @ $(cpuid(eax = 0xD, ecx = 2).ebx) + i * 16.
    //  - The top 256 bits of ZMMi  @ $(cpuid(eax = 0xD, ecx = 6).ebx) + i * 32.
    // We therefore need to store all three separately.
    u32 const xmmBase(160);
    u32 const ymmHighHalfBase(Extension::cpuid(0xD, 0x2).ebx);
    u32 const zmmHighHalfBase(Extension::cpuid(0xD, 0x6).ebx);
    for (u8 i(0); i < Vm::State::Registers::NumXmmRegs; ++i) {
        // XMMi
        u32 const xmmiOff(xmmBase + i * 16);
        vec128 const xmmi(zmm[i].elem<u64>(1), zmm[i].elem<u64>(0));
        *reinterpret_cast<vec128*>(state + xmmiOff) = xmmi;

        // YMMi high bits
        u32 const ymmiHiOff(ymmHighHalfBase + i * 16);
        vec128 const ymmiHi(zmm[i].elem<u64>(3), zmm[i].elem<u64>(2));
        *reinterpret_cast<vec128*>(state + ymmiHiOff) = ymmiHi;

        // ZMMi high bits
        u32 const zmmiHiOff(zmmHighHalfBase + i * 32);
        vec256 const zmmiHi(zmm[i].elem<u64>(7),
                            zmm[i].elem<u64>(6),
                            zmm[i].elem<u64>(5),
                            zmm[i].elem<u64>(4));
        *reinterpret_cast<vec256*>(state + zmmiHiOff) = zmmiHi;
    }

    // MXCSR and its mask are stored at offset 24.
    *reinterpret_cast<u32*>(state + 24) = mxcsr;
    *reinterpret_cast<u32*>(state + 28) = mxcsrMask;

    // AVX512:
    // For i >= 16, ZMMi is fully stored at offset:
    //  $(cpuid(eax = 0xD, ecx = 7).ebx) + i * 64.
    // ZMMi for i < 16 were stored above along with YMMi and XMMi.
    u64 const highZmmBase(Extension::cpuid(0xD, 0x7).ebx);
    for (u8 i(0); i < Vm::State::Registers::NumZmmRegs / 2; ++i) {
        u32 const zmmiOff(highZmmBase + i * 64);
        *reinterpret_cast<vec512*>(state + zmmiOff) = zmm[i + 16];
    }
    // OPmask ki is @ $(cpuid(eax = 0xD, ecx = 5).ebx) + i * 8.
    u32 const opMaskBase(Extension::cpuid(0xD, 0x5).ebx);
    for (u8 i(0); i < Vm::State::Registers::NumKRegs; ++i) {
        u32 const kiOff(opMaskBase + i * 8);
        *reinterpret_cast<u64*>(state + kiOff) = k[i];
    }
}

std::unique_ptr<XSaveArea> getXSave(int const vcpuFd) {
    kvm_xsave xsave{};
    if (::ioctl(vcpuFd, KVM_GET_XSAVE, &xsave) == -1) {
        throw KvmError("Cannot get guest XSAVE state", errno);
    }
    return std::unique_ptr<XSaveArea>(new XSaveArea(xsave));
}

void setXSave(int const vcpuFd, XSaveArea const& xsave) {
    // Read the current state of XSave so that we only overwrite the state
    // supported by XSaveArea while leaving the other bits unchanged.
    kvm_xsave kx;
    if (::ioctl(vcpuFd, KVM_GET_XSAVE, &kx) == -1) {
        throw KvmError("Cannot get guest XSAVE state", errno);
    }

    // Update the state per XSaveArea.
    xsave.fillKvmXSave(&kx);

    // The KVM API does not set the state-component bitmap of the XSAVE area,
    // e.g. it is left to 0. We need to set it ourselves in order for
    // KVM_SET_XSAVE to actually write the registers we want.
    // The first 2 bits of xstateBv indicate the presence of the state for x87
    // and SSE respectively. Bit 2 indicates the presence of AVX state. Bits 5
    // to 7 indicates the presence of AVX512 state.
    u8 * const xstateBv(reinterpret_cast<u8*>(kx.region) + 512);
    *xstateBv |= 0x7 | (1 << 5) | (1 << 6) | (1 << 7);

    if (::ioctl(vcpuFd, KVM_SET_XSAVE, &kx) != 0) {
        throw KvmError("Cannot set guest XSAVE state", errno);
    }
}

u64 getXcr0(int const vcpuFd) {
    kvm_xcrs dest{};
    if (::ioctl(vcpuFd, KVM_GET_XCRS, &dest) == -1) {
        throw KvmError("Failed KVM_GET_XCRS", errno);
    }
    return dest.xcrs[0].value;
}

void setXcr0(int const vcpuFd, u64 const xcr0) {
    kvm_xcrs src{};
    if (::ioctl(vcpuFd, KVM_GET_XCRS, &src) == -1) {
        throw KvmError("Failed KVM_GET_XCRS", errno);
    }
    src.xcrs[0].value |= xcr0;
    if (::ioctl(vcpuFd, KVM_SET_XCRS, &src) == -1) {
        throw KvmError("Failed KVM_SET_XCRS", errno);
    }

}

}
}
