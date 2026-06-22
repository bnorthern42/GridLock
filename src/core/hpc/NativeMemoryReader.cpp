#include "NativeMemoryReader.hpp"
#include <cstring>
#include <cerrno>
#include <QDebug>

#ifdef __linux__
#include <sys/uio.h>
#endif

std::vector<double> NativeMemoryReader::readDoubles(pid_t targetPid, uintptr_t baseAddress, size_t count) {
    std::vector<double> result;
    if (count == 0) return result;
    
    result.resize(count);
    
#ifdef __linux__
    struct iovec local[1];
    local[0].iov_base = result.data();
    local[0].iov_len = count * sizeof(double);

    struct iovec remote[1];
    remote[0].iov_base = reinterpret_cast<void*>(baseAddress);
    remote[0].iov_len = count * sizeof(double);

    ssize_t bytesRead = process_vm_readv(targetPid, local, 1, remote, 1, 0);
    
    if (bytesRead < 0) {
        qDebug() << "[NativeMemory] process_vm_readv failed. Errno:" << errno << "-" << strerror(errno);
        return std::vector<double>();
    }
    
    if (static_cast<size_t>(bytesRead) != count * sizeof(double)) {
        qDebug() << "[NativeMemory] process_vm_readv partial read. Expected:" << (count * sizeof(double)) << "Got:" << bytesRead;
        return std::vector<double>();
    }
#else
    throw std::runtime_error("NativeMemoryReader is only supported on Linux");
#endif

    return result;
}
