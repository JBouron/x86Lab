#include <x86lab/util.hpp>
#include <fcntl.h>
#include <unistd.h>

namespace X86Lab::Util::Kvm {

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
    // FIXME: We should somehow guess what this number should be. Maybe by
    // repeatedly calling the ioctl and increasing it every time it fails?
    size_t const nent(64);
    size_t const structSize(
        sizeof(kvm_cpuid2) + nent * sizeof(kvm_cpuid_entry2));
    kvm_cpuid2 * const kvmCpuid(
        reinterpret_cast<kvm_cpuid2*>(malloc(structSize)));
    std::memset(kvmCpuid, 0x0, structSize);
    assert(!!kvmCpuid);
    kvmCpuid->nent = nent;
    if (::ioctl(getKvmHandle(), KVM_GET_SUPPORTED_CPUID, kvmCpuid) == -1) {
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
    int const max(::ioctl(vmFd, KVM_CHECK_EXTENSION, KVM_CAP_NR_MEMSLOTS, 0));
    if (max == -1) {
        throw KvmError("Error while calling KVM_CAP_NR_MEMSLOTS: ", errno);
    }
    return max;
}

kvm_fpu getFpu(int const vcpuFd) {
    kvm_fpu fpu{};
    if (::ioctl(vcpuFd, KVM_GET_FPU, &fpu) == -1) {
        throw KvmError("Cannot get guest FPU state", errno);
    }
    return fpu;
}

void setFpu(int const vcpuFd, kvm_fpu const& fpu) {
    if (::ioctl(vcpuFd, KVM_SET_FPU, &fpu) == -1) {
        throw KvmError("Cannot set guest FPU state", errno);
    }
}
}
