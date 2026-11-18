// Minimal CANN 8.5+ AscendCL/aclnn RMSNorm operator demonstration.
// SN14/CacheOn research code; NOT a valid CUDA/SGLang CacheOn proposal.
#include <acl/acl.h>
#include <aclnnop/aclnn_rms_norm.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void check(aclError rc, const char* what) {
    if (rc != ACL_SUCCESS) throw std::runtime_error(std::string(what) + ": " + std::to_string(rc));
}

// The two-phase aclnn functions return aclnnStatus (convertible to integer).
static void check_op(int rc, const char* what) {
    if (rc != 0) throw std::runtime_error(std::string(what) + ": " + std::to_string(rc));
}

struct Runtime {
    int device;
    bool initialized = false, selected = false;
    aclrtStream stream = nullptr;
    explicit Runtime(int d) : device(d) {
        check(aclInit(nullptr), "aclInit");
        initialized = true;
        check(aclrtSetDevice(device), "aclrtSetDevice");
        selected = true;
        check(aclrtCreateStream(&stream), "aclrtCreateStream");
    }
    ~Runtime() {
        if (stream) aclrtDestroyStream(stream);
        if (selected) aclrtResetDevice(device);
        if (initialized) aclFinalize();
    }
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
};

struct Tensor {
    void* device_ptr = nullptr;
    aclTensor* desc = nullptr;
    size_t bytes = 0;
    Tensor(const std::vector<int64_t>& shape, const std::vector<float>* initial = nullptr) {
        int64_t n = 1;
        for (auto d : shape) {
            if (d <= 0 || n > (1 << 22) / d) throw std::invalid_argument("invalid/oversized tensor shape");
            n *= d;
        }
        bytes = static_cast<size_t>(n) * sizeof(float);
        check(aclrtMalloc(&device_ptr, bytes, ACL_MEM_MALLOC_HUGE_FIRST), "aclrtMalloc");
        if (initial) {
            if (initial->size() != static_cast<size_t>(n)) throw std::invalid_argument("tensor data size mismatch");
            check(aclrtMemcpy(device_ptr, bytes, initial->data(), bytes,
                              ACL_MEMCPY_HOST_TO_DEVICE), "H2D aclrtMemcpy");
        }
        std::vector<int64_t> strides(shape.size(), 1);
        for (int i = static_cast<int>(shape.size()) - 2; i >= 0; --i)
            strides[static_cast<size_t>(i)] = strides[static_cast<size_t>(i + 1)] * shape[static_cast<size_t>(i + 1)];
        desc = aclCreateTensor(shape.data(), shape.size(), ACL_FLOAT, strides.data(), 0,
                               ACL_FORMAT_ND, shape.data(), shape.size(), device_ptr);
        if (!desc) throw std::runtime_error("aclCreateTensor failed");
    }
    ~Tensor() {
        if (desc) aclDestroyTensor(desc);
        if (device_ptr) aclrtFree(device_ptr);
    }
    Tensor(const Tensor&) = delete;
    Tensor& operator=(const Tensor&) = delete;
    std::vector<float> read() const {
        std::vector<float> out(bytes / sizeof(float));
        check(aclrtMemcpy(out.data(), bytes, device_ptr, bytes, ACL_MEMCPY_DEVICE_TO_HOST),
              "D2H aclrtMemcpy");
        return out;
    }
};

struct Workspace {
    void* ptr = nullptr;
    explicit Workspace(uint64_t size) {
        if (size) check(aclrtMalloc(&ptr, size, ACL_MEM_MALLOC_HUGE_FIRST), "workspace aclrtMalloc");
    }
    ~Workspace() { if (ptr) aclrtFree(ptr); }
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;
};

static int parse_positive(const char* arg, const char* name, int maximum) {
    std::string value(arg);
    size_t pos = 0;
    long long parsed = std::stoll(value, &pos);
    if (pos != value.size() || parsed < 1 || parsed > maximum)
        throw std::invalid_argument(std::string(name) + " must be 1.." + std::to_string(maximum));
    return static_cast<int>(parsed);
}

int main(int argc, char** argv) {
    try {
        if (argc > 4) throw std::invalid_argument("usage: ascend_sn14 [device=0] [rows=32] [width=4096]");
        int device = 0;
        if (argc > 1) {
            std::string v(argv[1]); size_t p = 0;
            long long d = std::stoll(v, &p);
            if (p != v.size() || d < 0 || d > 127) throw std::invalid_argument("device must be 0..127");
            device = static_cast<int>(d);
        }
        const int rows = argc > 2 ? parse_positive(argv[2], "rows", 4096) : 32;
        const int width = argc > 3 ? parse_positive(argv[3], "width", 65536) : 4096;
        if (static_cast<int64_t>(rows) * width > (1 << 22))
            throw std::invalid_argument("rows * width must not exceed 4194304 elements");

        const double epsilon = 1e-6;
        std::vector<float> x_data(static_cast<size_t>(rows) * width), gamma_data(width, 1.0f);
        for (size_t i = 0; i < x_data.size(); ++i) x_data[i] = static_cast<float>(static_cast<long long>(i % 101) - 50LL) / 100.0f;
        Runtime runtime(device);
        Tensor x({rows, width}, &x_data);
        Tensor gamma({width}, &gamma_data);
        Tensor y({rows, width});
        Tensor rstd({rows, 1}); // 2-D x and 1-D gamma => [rows, 1] reciprocal RMS.

        uint64_t workspace_size = 0;
        aclOpExecutor* executor = nullptr;
        check_op(aclnnRmsNormGetWorkspaceSize(x.desc, gamma.desc, epsilon,
                 y.desc, rstd.desc, &workspace_size, &executor),
                 "aclnnRmsNormGetWorkspaceSize");
        Workspace workspace(workspace_size);
        check_op(aclnnRmsNorm(workspace.ptr, workspace_size, executor, runtime.stream), "aclnnRmsNorm");
        check(aclrtSynchronizeStream(runtime.stream), "aclrtSynchronizeStream");

        const auto result = y.read();
        double max_error = 0.0;
        for (int r = 0; r < rows; ++r) {
            double mean_sq = 0.0;
            for (int c = 0; c < width; ++c) {
                const double v = x_data[static_cast<size_t>(r) * width + c];
                mean_sq += v * v;
            }
            const double inv = 1.0 / std::sqrt(mean_sq / width + epsilon);
            for (int c = 0; c < width; ++c) {
                size_t i = static_cast<size_t>(r) * width + c;
                max_error = std::max(max_error, std::abs(static_cast<double>(result[i]) - x_data[i] * inv));
            }
        }
        std::cout << "CANN aclnnRmsNorm OK | device=" << device
                  << " rows=" << rows << " width=" << width
                  << " max_abs_error=" << max_error << " output0=" << result.front() << '\n';
        // Numerical agreement here verifies a single local operator only.
        return max_error < 0.02 ? 0 : 2;
    } catch (const std::exception& ex) {
        std::cerr << "ERROR: " << ex.what() << '\n';
        return 1;
    }
}
