/* Copyright (c) 2016 PaddlePaddle Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#pragma once

#include <Python.h>

#include <algorithm>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "paddle/fluid/framework/data_type.h"
#include "paddle/fluid/framework/lod_tensor.h"
#include "paddle/fluid/memory/memcpy.h"
#include "paddle/fluid/operators/eigen/eigen_function.h"
#include "paddle/fluid/operators/math/concat_and_split.h"
#include "paddle/fluid/platform/bfloat16.h"
#include "paddle/fluid/platform/device/device_wrapper.h"
#include "paddle/phi/kernels/funcs/strided_memcpy.h"
#if defined(PADDLE_WITH_CUDA) || defined(PADDLE_WITH_HIP)
#include "paddle/fluid/platform/cuda_device_guard.h"
#endif
#include "paddle/fluid/eager/api/generated/eager_generated/forwards/dygraph_functions.h"
#include "paddle/fluid/framework/convert_utils.h"
#include "paddle/fluid/framework/eigen.h"
#include "paddle/fluid/platform/device_context.h"
#include "paddle/fluid/platform/float16.h"
#include "paddle/fluid/platform/profiler/event_tracing.h"
#include "paddle/phi/common/pstring.h"
#include "paddle/phi/core/string_tensor.h"
#include "paddle/phi/kernels/strings/unicode.h"
#include "pybind11/numpy.h"
#include "pybind11/pybind11.h"

namespace py = pybind11;

namespace pybind11 {
namespace detail {

// Note: use same enum number of float16 in numpy.
// import numpy as np
// print np.dtype(np.float16).num  # 23
constexpr int NPY_FLOAT16_ = 23;
constexpr int NPY_UINT16_ = 4;
constexpr int NPY_COMPLEX64 = 14;
constexpr int NPY_COMPLEX128 = 15;

// cast numpy type form S to T, this may allocate new memory
template <class T, class S>
static py::array_t<T> CastNumpyType(py::array_t<S> array) {
  if (std::is_same<T, S>::value) {
    return array;
  }
  auto dim = array.ndim();
  std::vector<py::ssize_t> result_shape(dim);
  for (auto i = 0; i < dim; i++) {
    result_shape[i] = array.shape(i);
  }

  py::array_t<T> result(result_shape);

  return py::vectorize([](S s) { return static_cast<T>(s); })(array);
}

template <class T>
static py::array_t<T> CastNumpyArray(const py::object &array) {
  if (py::isinstance<py::array_t<float>>(array)) {
    return CastNumpyType<T>(array.cast<py::array_t<float>>());
  } else if (py::isinstance<py::array_t<double>>(array)) {
    return CastNumpyType<T>(array.cast<py::array_t<double>>());
  } else if (py::isinstance<py::array_t<int32_t>>(array)) {
    return CastNumpyType<T>(array.cast<py::array_t<int32_t>>());
  } else if (py::isinstance<py::array_t<int64_t>>(array)) {
    return CastNumpyType<T>(array.cast<py::array_t<int64_t>>());
  } else if (py::isinstance<py::array_t<bool>>(array)) {
    return CastNumpyType<T>(array.cast<py::array_t<bool>>());
  } else {
    PADDLE_THROW(paddle::platform::errors::InvalidArgument(
        "Value type error. The assign numpy value allows integer, float, "
        "double and bool, "
        "but received %s.",
        Py_TYPE(array.ptr())->tp_name));
  }
  // can't reach here
  return py::array_t<T>();
}

// Note: Since float16 is not a builtin type in C++, we register
// paddle::platform::float16 as numpy.float16.
// Ref: https://github.com/pybind/pybind11/issues/1776
template <>
struct npy_format_descriptor<paddle::platform::float16> {
  static py::dtype dtype() {
    handle ptr = npy_api::get().PyArray_DescrFromType_(NPY_FLOAT16_);
    return reinterpret_borrow<py::dtype>(ptr);
  }
  static std::string format() {
    // Note: "e" represents float16.
    // Details at:
    // https://docs.python.org/3/library/struct.html#format-characters.
    return "e";
  }
  static constexpr auto name = _("float16");
};

// Note: Since bfloat16 is not a builtin type in C++ and in numpy,
// we register paddle::platform::bfloat16 as numpy.uint16.
template <>
struct npy_format_descriptor<paddle::platform::bfloat16> {
  static py::dtype dtype() {
    handle ptr = npy_api::get().PyArray_DescrFromType_(NPY_UINT16_);
    return reinterpret_borrow<py::dtype>(ptr);
  }
  static std::string format() {
    // Note: "H" represents UINT16.
    // Details at:
    // https://docs.python.org/3/library/struct.html#format-characters.
    return "H";
  }
  static constexpr auto name = _("bfloat16");
};

// we register paddle::platform::complex<float> as numpy.complex64.
template <>
struct npy_format_descriptor<paddle::platform::complex<float>> {
  static py::dtype dtype() {
    handle ptr = npy_api::get().PyArray_DescrFromType_(NPY_COMPLEX64);
    return reinterpret_borrow<py::dtype>(ptr);
  }

  static std::string format() {
    // Note: "F" represents complex64.
    // Details at:
    // https://stackoverflow.com/questions/13997087/what-are-the-available-datatypes-for-dtype-with-numpys-loadtxt-an-genfromtx
    // for k, v in np.sctypeDict.iteritems():
    //     print '{0:14s} : {1:40s}'.format(str(k), v)
    return "F";
  }
  static constexpr auto name = _("complext64");
};

template <>
struct npy_format_descriptor<paddle::platform::complex<double>> {
  static py::dtype dtype() {
    handle ptr = npy_api::get().PyArray_DescrFromType_(NPY_COMPLEX128);
    return reinterpret_borrow<py::dtype>(ptr);
  }

  static std::string format() {
    // Note: "D" represents complex128.
    // Details at:
    // https://stackoverflow.com/questions/13997087/what-are-the-available-datatypes-for-dtype-with-numpys-loadtxt-an-genfromtx
    // for k, v in np.sctypeDict.iteritems():
    //     print '{0:14s} : {1:40s}'.format(str(k), v)
    return "D";
  }
  static constexpr auto name = _("complext128");
};

}  // namespace detail
}  // namespace pybind11

namespace paddle {
namespace pybind {

namespace details {

template <typename T>
class PYBIND11_HIDDEN NumpyAllocation : public memory::Allocation {
 public:
  explicit NumpyAllocation(const py::array &arr)
      : Allocation(const_cast<void *>(arr.data()),
                   sizeof(T) * (arr.size()),
                   paddle::platform::CPUPlace()),
        arr_(arr.ptr()) {
    PADDLE_ENFORCE_NOT_NULL(
        arr_,
        platform::errors::InvalidArgument("The underlying PyObject pointer of "
                                          "numpy array cannot be nullptr"));
    PADDLE_ENFORCE_NE(
        arr_,
        Py_None,
        platform::errors::PreconditionNotMet(
            "The underlying PyObject pointer of numpy array cannot be None"));
    Py_INCREF(arr_);
  }
  ~NumpyAllocation() override {
    py::gil_scoped_acquire gil;
    Py_DECREF(arr_);
  }

 private:
  PyObject *arr_;
};

template <typename T>
struct ValidDTypeToPyArrayChecker {
  static constexpr bool kValue = false;
};

#define DECLARE_VALID_DTYPE_TO_PY_ARRAY(type) \
  template <>                                 \
  struct ValidDTypeToPyArrayChecker<type> {   \
    static constexpr bool kValue = true;      \
  }

DECLARE_VALID_DTYPE_TO_PY_ARRAY(platform::float16);
DECLARE_VALID_DTYPE_TO_PY_ARRAY(platform::bfloat16);
DECLARE_VALID_DTYPE_TO_PY_ARRAY(platform::complex<float>);
DECLARE_VALID_DTYPE_TO_PY_ARRAY(platform::complex<double>);
DECLARE_VALID_DTYPE_TO_PY_ARRAY(float);
DECLARE_VALID_DTYPE_TO_PY_ARRAY(double);
DECLARE_VALID_DTYPE_TO_PY_ARRAY(bool);
DECLARE_VALID_DTYPE_TO_PY_ARRAY(int8_t);
DECLARE_VALID_DTYPE_TO_PY_ARRAY(int16_t);
DECLARE_VALID_DTYPE_TO_PY_ARRAY(int);
DECLARE_VALID_DTYPE_TO_PY_ARRAY(int64_t);
DECLARE_VALID_DTYPE_TO_PY_ARRAY(uint8_t);

inline std::string TensorDTypeToPyDTypeStr(
    framework::proto::VarType::Type type) {
#define TENSOR_DTYPE_TO_PY_DTYPE(T, proto_type)                             \
  if (type == proto_type) {                                                 \
    if (std::is_same<T, platform::float16>::value) {                        \
      return "e";                                                           \
    } else if (std::is_same<T, platform::bfloat16>::value) {                \
      /* NumPy character code of uint16 due to no support for bfloat16 */   \
      return "H";                                                           \
    } else if (std::is_same<T, platform::complex<float>>::value) {          \
      return "F";                                                           \
    } else if (std::is_same<T, platform::complex<double>>::value) {         \
      return "D";                                                           \
    } else {                                                                \
      constexpr auto kIsValidDType = ValidDTypeToPyArrayChecker<T>::kValue; \
      PADDLE_ENFORCE_EQ(                                                    \
          kIsValidDType,                                                    \
          true,                                                             \
          platform::errors::Unimplemented(                                  \
              "This type [%s] of tensor cannot be expose to Python",        \
              typeid(T).name()));                                           \
      return py::format_descriptor<T>::format();                            \
    }                                                                       \
  }

  _ForEachDataType_(TENSOR_DTYPE_TO_PY_DTYPE);
#undef TENSOR_DTYPE_TO_PY_DTYPE
  PADDLE_THROW(platform::errors::Unimplemented(
      "Unsupported tensor data type: %s", framework::DataTypeToString(type)));
}

}  // namespace details

template <typename T>
T TensorGetElement(const phi::DenseTensor &self, size_t offset) {
  PADDLE_ENFORCE_LT(offset,
                    self.numel(),
                    platform::errors::InvalidArgument(
                        "The offset exceeds the size of tensor."));

  T b = static_cast<T>(0);
  if (platform::is_cpu_place(self.place())) {
    b = self.data<T>()[offset];
  } else if (platform::is_xpu_place(self.place())) {
#ifdef PADDLE_WITH_XPU
    const T *a = self.data<T>();
    auto p = self.place();
    paddle::memory::Copy(platform::CPUPlace(), &b, p, a + offset, sizeof(T));
#endif
  } else if (platform::is_gpu_place(self.place())) {
#if defined(PADDLE_WITH_CUDA) || defined(PADDLE_WITH_HIP)
    const T *a = self.data<T>();
    auto p = self.place();
    paddle::memory::Copy(
        platform::CPUPlace(), &b, p, a + offset, sizeof(T), nullptr);
#endif
  } else if (platform::is_mlu_place(self.place())) {
#ifdef PADDLE_WITH_MLU
    const T *a = self.data<T>();
    auto p = self.place();
    paddle::memory::Copy(
        platform::CPUPlace(), &b, p, a + offset, sizeof(T), nullptr);
#endif
  } else if (platform::is_npu_place(self.place())) {
#if defined(PADDLE_WITH_ASCEND_CL)
    const T *a = self.data<T>();
    auto p = self.place();
    paddle::memory::Copy(
        platform::CPUPlace(), &b, p, a + offset, sizeof(T), nullptr);
#endif
  } else if (platform::is_custom_place(self.place())) {
#if defined(PADDLE_WITH_CUSTOM_DEVICE)
    const T *a = self.data<T>();
    auto p = self.place();
    paddle::memory::Copy(
        platform::CPUPlace(), &b, p, a + offset, sizeof(T), nullptr);
#endif
  }
  VLOG(10) << "TensorGetElement, place: " << self.place()
           << ", offset: " << offset << ", element: " << b;
  return b;
}

template <typename T>
void TensorSetElement(phi::DenseTensor *self, size_t offset, T elem) {
  PADDLE_ENFORCE_LT(offset,
                    self->numel(),
                    platform::errors::InvalidArgument(
                        "The offset exceeds the size of tensor."));
  VLOG(10) << "TensorSetElement, place: " << self->place()
           << ", offset: " << offset << ", element: " << elem;
  if (platform::is_cpu_place(self->place())) {
    self->mutable_data<T>(self->place())[offset] = elem;
  } else if (platform::is_xpu_place(self->place())) {
#ifdef PADDLE_WITH_XPU
    auto p = self->place();
    T *a = self->mutable_data<T>(p);
    paddle::memory::Copy(p, a + offset, platform::CPUPlace(), &elem, sizeof(T));
#endif
  } else if (platform::is_gpu_place(self->place())) {
#if defined(PADDLE_WITH_CUDA) || defined(PADDLE_WITH_HIP)
    auto p = self->place();
    T *a = self->mutable_data<T>(p);
    paddle::memory::Copy(
        p, a + offset, platform::CPUPlace(), &elem, sizeof(T), nullptr);
#endif
  } else if (platform::is_mlu_place(self->place())) {
#ifdef PADDLE_WITH_MLU
    auto p = self->place();
    T *a = self->mutable_data<T>(p);
    paddle::memory::Copy(
        p, a + offset, platform::CPUPlace(), &elem, sizeof(T), nullptr);
#endif
  } else if (platform::is_npu_place(self->place())) {
#if defined(PADDLE_WITH_ASCEND_CL)
    auto p = self->place();
    T *a = self->mutable_data<T>(p);
    paddle::memory::Copy(
        p, a + offset, platform::CPUPlace(), &elem, sizeof(T), nullptr);
#endif
  } else if (platform::is_custom_place(self->place())) {
#if defined(PADDLE_WITH_CUSTOM_DEVICE)
    auto p = self->place();
    T *a = self->mutable_data<T>(p);
    paddle::memory::Copy(
        p, a + offset, platform::CPUPlace(), &elem, sizeof(T), nullptr);
#endif
  }
}

template <typename T, typename P>
void SetTensorFromPyArrayT(
    phi::DenseTensor *self,
    const py::array_t<T, py::array::c_style | py::array::forcecast> &array,
    const P &place,
    bool zero_copy) {
  std::vector<int64_t> dims;
  dims.reserve(array.ndim());
  for (decltype(array.ndim()) i = 0; i < array.ndim(); ++i) {
    dims.push_back(static_cast<int64_t>(array.shape()[i]));
  }
  self->Resize(phi::make_ddim(dims));

  if (paddle::platform::is_cpu_place(place)) {
    if (zero_copy) {
      auto holder = std::make_shared<details::NumpyAllocation<T>>(array);
      auto type = framework::ToDataType(std::type_index(typeid(T)));
      self->ResetHolderWithType(holder, framework::TransToPhiDataType(type));
    } else {
      auto dst = self->mutable_data<T>(place);
      std::memcpy(dst, array.data(), array.nbytes());
    }
  } else if (paddle::platform::is_xpu_place(place)) {
#ifdef PADDLE_WITH_XPU
    // NOTE(wangxi): When copying data to the accelerator card,
    // we need set_device(dev_id) first.
    platform::Place tmp_place = place;
    platform::XPUDeviceGuard guard(tmp_place.device);
    auto dst = self->mutable_data<T>(place);
    memory::Copy(tmp_place,
                 static_cast<void *>(dst),
                 platform::CPUPlace(),
                 static_cast<const void *>(array.data()),
                 array.nbytes());
#else
    PADDLE_THROW(platform::errors::PermissionDenied(
        "Cannot use XPUPlace in CPU/GPU version, "
        "Please recompile or reinstall Paddle with XPU support."));
#endif
  } else if (paddle::platform::is_ipu_place(place)) {
#ifdef PADDLE_WITH_IPU
    if (zero_copy) {
      auto holder = std::make_shared<details::NumpyAllocation<T>>(array);
      auto type = framework::ToDataType(std::type_index(typeid(T)));
      self->ResetHolderWithType(holder, framework::TransToPhiDataType(type));
    } else {
      // IPU does not store Tensor data, Tensor will be created on CPU
      if (!self->initialized()) {
        auto dst = self->mutable_data<T>(place);
        std::memcpy(dst, array.data(), array.nbytes());
      } else {
        auto dst = self->mutable_data<T>(self->place());
        std::memcpy(dst, array.data(), array.nbytes());
      }
    }
#else
    PADDLE_THROW(platform::errors::PermissionDenied(
        "Cannot use IPUPlace in CPU/GPU/XPU/NPU version, "
        "Please recompile or reinstall Paddle with IPU support."));
#endif
  } else if (paddle::platform::is_npu_place(place)) {
#ifdef PADDLE_WITH_ASCEND_CL
    platform::Place tmp_place = place;
    platform::NPUDeviceGuard guard(tmp_place.device);
    auto dst = self->mutable_data<T>(place);
    platform::NPUMemcpySync(
        dst, array.data(), array.nbytes(), ACL_MEMCPY_HOST_TO_DEVICE);
    platform::DeviceContextPool &pool = platform::DeviceContextPool::Instance();
    auto &ctx = *pool.Get(place);
    ctx.Wait();
#else
    PADDLE_THROW(platform::errors::PermissionDenied(
        "Cannot use NPUPlace in CPU/GPU/XPU version. "
        "Please recompile or reinstall Paddle with NPU support."));
#endif
  } else if (paddle::platform::is_mlu_place(place)) {
#ifdef PADDLE_WITH_MLU
    platform::Place tmp_place = place;
    platform::MLUDeviceGuard guard(tmp_place.device);
    auto dst = self->mutable_data<T>(place);
    platform::DeviceContextPool &pool = platform::DeviceContextPool::Instance();
    auto dev_ctx = static_cast<platform::MLUDeviceContext *>(pool.Get(place));
    paddle::platform::MLUMemcpyH2DAsync(
        dst, array.data(), array.nbytes(), dev_ctx->stream());
    dev_ctx->Wait();
#else
    PADDLE_THROW(platform::errors::PermissionDenied(
        "Cannot use MLUPlace in CPU/GPU version, "
        "Please recompile or reinstall Paddle with MLU support."));
#endif
  } else if (paddle::platform::is_custom_place(place)) {
#ifdef PADDLE_WITH_CUSTOM_DEVICE
    platform::Place tmp_place = place;
    phi::DeviceGuard guard(tmp_place);
    auto dst = self->mutable_data<T>(place);

    phi::DeviceManager::GetDeviceWithPlace(tmp_place)->MemoryCopyH2D(
        reinterpret_cast<void *>(dst),
        const_cast<void *>(reinterpret_cast<const void *>(array.data())),
        array.nbytes());
    platform::DeviceContextPool &pool = platform::DeviceContextPool::Instance();
    auto &ctx = *pool.Get(place);
    ctx.Wait();
#else
    PADDLE_THROW(platform::errors::PermissionDenied(
        "Cannot use CustomDevice in CPU/GPU/XPU version. "
        "Please recompile or reinstall Paddle with CustomDevice support."));
#endif
  } else {
#if defined(PADDLE_WITH_CUDA) || defined(PADDLE_WITH_HIP)
    if (paddle::platform::is_gpu_place(place)) {
      // NOTE(wangxi): When copying data to the accelerator card,
      // we need set_device(dev_id) first.
      platform::CUDADeviceGuard guard(place.device);
      auto dst = self->mutable_data<T>(place);
#ifdef PADDLE_WITH_HIP
      paddle::platform::GpuMemcpySync(
          dst, array.data(), array.nbytes(), hipMemcpyHostToDevice);
#else
      paddle::platform::GpuMemcpySync(
          dst, array.data(), array.nbytes(), cudaMemcpyHostToDevice);
#endif

    } else if (paddle::platform::is_cuda_pinned_place(place)) {
      auto dst = self->mutable_data<T>(place);
      std::memcpy(dst, array.data(), array.nbytes());
    } else {
      PADDLE_THROW(platform::errors::InvalidArgument(
          "Incompatible place type: Tensor.set() supports "
          "CPUPlace, CUDAPlace "
          "and CUDAPinnedPlace, but got %s!",
          place));
    }
#else
    PADDLE_THROW(platform::errors::PermissionDenied(
        "Cannot use CUDAPlace or CUDAPinnedPlace in CPU only version, "
        "Please recompile or reinstall Paddle with CUDA support."));
#endif
  }
}

template <typename P>
void SetTensorFromPyArray(phi::DenseTensor *self,
                          const py::object &obj,
                          const P &place,
                          bool zero_copy) {
  auto array = obj.cast<py::array>();
  if (py::isinstance<py::array_t<float>>(array)) {
    SetTensorFromPyArrayT<float, P>(self, array, place, zero_copy);
  } else if (py::isinstance<py::array_t<int>>(array)) {
    SetTensorFromPyArrayT<int, P>(self, array, place, zero_copy);
  } else if (py::isinstance<py::array_t<int64_t>>(array)) {
    SetTensorFromPyArrayT<int64_t, P>(self, array, place, zero_copy);
  } else if (py::isinstance<py::array_t<double>>(array)) {
    SetTensorFromPyArrayT<double, P>(self, array, place, zero_copy);
  } else if (py::isinstance<py::array_t<int8_t>>(array)) {
    SetTensorFromPyArrayT<int8_t, P>(self, array, place, zero_copy);
  } else if (py::isinstance<py::array_t<int16_t>>(array)) {
    SetTensorFromPyArrayT<int16_t, P>(self, array, place, zero_copy);
  } else if (py::isinstance<py::array_t<uint8_t>>(array)) {
    SetTensorFromPyArrayT<uint8_t, P>(self, array, place, zero_copy);
  } else if (py::isinstance<py::array_t<paddle::platform::float16>>(array)) {
    SetTensorFromPyArrayT<paddle::platform::float16, P>(
        self, array, place, zero_copy);
  } else if (py::isinstance<py::array_t<paddle::platform::complex<float>>>(
                 array)) {
    SetTensorFromPyArrayT<paddle::platform::complex<float>, P>(
        self, array, place, zero_copy);
  } else if (py::isinstance<py::array_t<paddle::platform::complex<double>>>(
                 array)) {
    SetTensorFromPyArrayT<paddle::platform::complex<double>, P>(
        self, array, place, zero_copy);
  } else if (py::isinstance<py::array_t<uint16_t>>(array)) {
    // since there is still no support for bfloat16 in NumPy,
    // uint16 is used for casting bfloat16
    SetTensorFromPyArrayT<paddle::platform::bfloat16, P>(
        self, array, place, zero_copy);
  } else if (py::isinstance<py::array_t<bool>>(array)) {
    SetTensorFromPyArrayT<bool, P>(self, array, place, zero_copy);
  } else {
    // obj may be any type, obj.cast<py::array>() may be failed,
    // then the array.dtype will be string of unknown meaning,
    PADDLE_THROW(platform::errors::InvalidArgument(
        "Input object type error or incompatible array data type. "
        "tensor.set() supports array with bool, float16, float32, "
        "float64, int8, int16, int32, int64, uint8 or uint16, "
        "please check your input or input array data type."));
  }
}

template <typename P>
void SetStringTensorFromPyArray(phi::StringTensor *self,
                                const py::array &array,
                                const P &place) {
  bool is_string_pyarray =
      array.dtype().kind() == 'S' || array.dtype().kind() == 'U';
  PADDLE_ENFORCE_EQ(is_string_pyarray,
                    true,
                    platform::errors::InvalidArgument(
                        "Expect the dtype of numpy array is string or "
                        "unicode, but recevie dtype %s",
                        array.dtype()));
  std::vector<int64_t> dims;
  dims.reserve(array.ndim());
  dims.reserve(array.ndim());
  for (decltype(array.ndim()) i = 0; i < array.ndim(); ++i) {
    dims.push_back(static_cast<int>(array.shape()[i]));
  }
  self->Resize(phi::make_ddim(dims));
  auto itemsize = array.itemsize();
  if (paddle::platform::is_cpu_place(place)) {
    auto dst = self->mutable_data(place);
    if (array.dtype().kind() == 'S') {
      for (int i = 0; i < self->numel(); ++i) {
        dst[i] =
            pstring(reinterpret_cast<const char *>(array.data()) + itemsize * i,
                    itemsize);
      }
    } else {
      // array.dtype().kind() == 'U'
      VLOG(6) << "numpy array itemsize: " << itemsize;
      for (int i = 0; i < self->numel(); ++i) {
        // Note(zhoushunjie): The itemsize of unicode numpy array is the
        // the size of each unicode string. Each unicode string is aligned
        // to max length of the array of unicode strings, so the size of
        // each unicode string is same. The size of each unicode character is
        // 4, so the size of unicode string is 4 times of the length of
        // unicode string.
        auto unicode_len = itemsize / 4;
        auto utf8_len = phi::strings::GetUTF8StrLen(
            reinterpret_cast<const uint32_t *>(array.data()) + unicode_len * i,
            unicode_len);
        pstring pstr(utf8_len - 1, 0);
        phi::strings::GetUTF8Str(
            reinterpret_cast<const uint32_t *>(array.data()) + unicode_len * i,
            pstr.mdata(),
            unicode_len);
        dst[i] = pstr;
      }
    }
  } else {
    PADDLE_THROW(platform::errors::InvalidArgument(
        "StringTensor only support CPUPlace now, but receive %s",
        place.DebugString()));
  }
}

template <typename T>
void SetUVATensorFromPyArrayImpl(
    phi::DenseTensor *self_tensor,
    const py::array_t<T, py::array::c_style | py::array::forcecast> &array,
    int device_id) {
#if defined(PADDLE_WITH_CUDA)
  VLOG(4) << "Running in SetUVATensorFromPyArrayImpl.";
  std::vector<int64_t> dims;
  dims.reserve(array.ndim());
  int64_t numel = 1;
  for (decltype(array.ndim()) i = 0; i < array.ndim(); ++i) {
    dims.emplace_back(static_cast<int64_t>(array.shape()[i]));
    numel *= static_cast<int64_t>(array.shape()[i]);
  }
  self_tensor->Resize(phi::make_ddim(dims));

  auto data_type = framework::ToDataType(std::type_index(typeid(T)));
  const auto &need_allocate_size = numel * framework::SizeOfType(data_type);
  T *data_ptr;
  cudaHostAlloc(reinterpret_cast<void **>(&data_ptr),
                need_allocate_size,
                cudaHostAllocWriteCombined | cudaHostAllocMapped);
  std::memcpy(data_ptr, array.data(), array.nbytes());

  void *cuda_device_pointer = nullptr;
  cudaHostGetDevicePointer(reinterpret_cast<void **>(&cuda_device_pointer),
                           reinterpret_cast<void *>(data_ptr),
                           0);
  std::shared_ptr<memory::allocation::Allocation> holder =
      std::make_shared<memory::allocation::Allocation>(
          cuda_device_pointer,
          need_allocate_size,
          platform::CUDAPlace(device_id));
  self_tensor->ResetHolderWithType(holder,
                                   framework::TransToPhiDataType(data_type));
#endif
}

template <typename T>
void SetUVATensorFromPyArray(
    const std::shared_ptr<paddle::imperative::VarBase> &self,
    const py::array_t<T, py::array::c_style | py::array::forcecast> &array,
    int device_id) {
#if defined(PADDLE_WITH_CUDA)
  VLOG(4) << "Running in SetUVATensorFromPyArray for VarBase.";
  auto *self_tensor = self->MutableVar()->GetMutable<phi::DenseTensor>();
  SetUVATensorFromPyArrayImpl<T>(self_tensor, array, device_id);
#endif
}

template <typename T>
void SetUVATensorFromPyArray(
    const std::shared_ptr<paddle::experimental::Tensor> &self,
    const py::array_t<T> &array,
    int device_id) {
#if defined(PADDLE_WITH_CUDA)
  VLOG(4) << "Running in SetUVATensorFromPyArray for Phi::Tensor.";
  phi::DenseTensorMeta meta =
      phi::DenseTensorMeta(phi::DataType::FLOAT32, phi::make_ddim({1, 1}));
  std::shared_ptr<phi::DenseTensor> tmp_t = std::make_shared<phi::DenseTensor>(
      std::make_unique<paddle::experimental::DefaultAllocator>(
          paddle::platform::CPUPlace())
          .get(),
      meta);
  self.get()->set_impl(tmp_t);
  auto *self_tensor = static_cast<phi::DenseTensor *>(self.get()->impl().get());

  SetUVATensorFromPyArrayImpl<T>(self_tensor, array, device_id);
#endif
}

template <typename T, size_t D>
void _sliceCompute(const phi::DenseTensor *in,
                   phi::DenseTensor *out,
                   const phi::CPUContext &ctx,
                   const std::vector<int> &axes,
                   const std::vector<int> &starts) {
  auto &eigen_place = *ctx.eigen_device();
  auto out_dims = out->dims();
  auto in_dims = in->dims();

  auto offsets = Eigen::DSizes<Eigen::DenseIndex, D>();
  auto extents = Eigen::DSizes<Eigen::DenseIndex, D>();
  for (size_t i = 0; i < D; ++i) {
    offsets[i] = 0;
    extents[i] = out_dims[i];
  }
  int start;
  for (size_t i = 0; i < axes.size(); ++i) {
    start = starts[i];
    if (start < 0) {
      start = (start + in_dims[axes[i]]);
    }
    start = std::max(start, 0);
    offsets[axes[i]] = start;
  }
  auto in_t =
      framework::EigenTensor<T, D, Eigen::RowMajor, Eigen::DenseIndex>::From(
          *in);
  auto out_t =
      framework::EigenTensor<T, D, Eigen::RowMajor, Eigen::DenseIndex>::From(
          *out);
  operators::EigenSlice<std::decay_t<decltype(eigen_place)>, T, D>::Eval(
      eigen_place, out_t, in_t, offsets, extents);
}

template <typename T>
void _concatCompute(const std::vector<phi::DenseTensor> &ins,
                    phi::DenseTensor *out,
                    const phi::CPUContext &ctx,
                    int64_t axis) {
  if (axis == 0 && ins.size() < 10) {
    size_t output_offset = 0;
    for (auto &in : ins) {
      auto in_stride = phi::stride_numel(in.dims());
      auto out_stride = phi::stride_numel(out->dims());
      phi::funcs::StridedNumelCopyWithAxis<T>(ctx,
                                              axis,
                                              out->data<T>() + output_offset,
                                              out_stride,
                                              in.data<T>(),
                                              in_stride,
                                              in_stride[axis]);
      output_offset += in_stride[axis];
    }
  } else {
    paddle::operators::math::ConcatFunctor<phi::CPUContext, T> concat_functor;
    concat_functor(ctx, ins, static_cast<int>(axis), out);
  }
}

inline void _getSliceinfo(const phi::DenseTensor &self,
                          py::object obj,
                          const int64_t dim,
                          int64_t *pstart,
                          int64_t *pstop,
                          int64_t *pstep,
                          int64_t *pslicelength) {
  auto &start = *pstart;
  auto &stop = *pstop;
  auto &step = *pstep;
  auto &slicelength = *pslicelength;
  const framework::DDim &srcDDim = self.dims();
  PADDLE_ENFORCE(
      0 <= dim && dim < srcDDim.size(),
      platform::errors::OutOfRange("The dim %d of slice is out of bounds, it "
                                   "shound be in the range of [0, %d).",
                                   dim,
                                   srcDDim.size()));

  if (py::isinstance<py::slice>(obj)) {
    size_t lstart, lstop, lstep, lslicelength;
    py::slice s = static_cast<py::slice>(obj);
    if (!s.compute(srcDDim[dim], &lstart, &lstop, &lstep, &lslicelength)) {
      PADDLE_THROW(platform::errors::OutOfRange(
          "Slice on dim: %d is error, please check the validity of tensor "
          "dims or slice item.",
          dim));
    }
    start = static_cast<int64_t>(lstart);
    stop = static_cast<int64_t>(lstop);
    step = static_cast<int64_t>(lstep);
    slicelength = static_cast<int64_t>(lslicelength);
  } else if (py::isinstance<py::int_>(obj)) {
    start = static_cast<int64_t>(static_cast<py::int_>(obj));
    PADDLE_ENFORCE(
        std::abs(start) < srcDDim[dim],
        platform::errors::OutOfRange("The start %d of slice is out of bounds, "
                                     "it shound be in the range of (%d, %d).",
                                     start,
                                     -srcDDim[dim],
                                     srcDDim[dim]));
    start = (start >= 0) ? start : srcDDim[dim] - start;
    stop = start + 1;
    step = 1;
    slicelength = 1;
  } else {
    PADDLE_THROW(
        platform::errors::OutOfRange("Index object error, the index object for "
                                     "slice only supports slice(::) and int."));
  }
}

inline phi::DenseTensor *_getTensor(const phi::DenseTensor &self,
                                    const framework::DDim &ddim) {
  phi::DenseTensor *output = new phi::DenseTensor();
  output->Resize(ddim);
  auto place = self.place();
  if (platform::is_cpu_place(place)) {
    output->mutable_data(place, self.dtype());
  } else if (platform::is_xpu_place(place)) {
#ifdef PADDLE_WITH_XPU
    output->mutable_data(place, self.dtype());
#endif
  } else if (platform::is_mlu_place(place)) {
#ifdef PADDLE_WITH_MLU
    output->mutable_data(place, self.dtype());
#endif
  } else {
#if defined(PADDLE_WITH_CUDA) || defined(PADDLE_WITH_HIP)
    if (platform::is_cuda_pinned_place(place)) {
      output->mutable_data(place, self.dtype());
    } else if ((platform::is_gpu_place(place))) {
      output->mutable_data(place, self.dtype());
    }
#endif
  }
  return output;
}

template <typename T>
void _sliceDapper(const phi::DenseTensor *in,
                  phi::DenseTensor *out,
                  const phi::CPUContext &ctx,
                  const std::vector<int> &axes,
                  const std::vector<int> &starts,
                  int size) {
  switch (size) {
    case 1:
      _sliceCompute<T, 1>(in, out, ctx, axes, starts);
      break;
    case 2:
      _sliceCompute<T, 2>(in, out, ctx, axes, starts);
      break;
    case 3:
      _sliceCompute<T, 3>(in, out, ctx, axes, starts);
      break;
    case 4:
      _sliceCompute<T, 4>(in, out, ctx, axes, starts);
      break;
    case 5:
      _sliceCompute<T, 5>(in, out, ctx, axes, starts);
      break;
    case 6:
      _sliceCompute<T, 6>(in, out, ctx, axes, starts);
      break;
    case 7:
      _sliceCompute<T, 7>(in, out, ctx, axes, starts);
      break;
    case 8:
      _sliceCompute<T, 8>(in, out, ctx, axes, starts);
      break;
    case 9:
      _sliceCompute<T, 9>(in, out, ctx, axes, starts);
      break;
    default:
      PADDLE_THROW(platform::errors::InvalidArgument(
          "The dim size should be 1 to 9, current is %d", size));
      break;
  }
}

template <typename T>
inline phi::DenseTensor *_sliceWrapper(const phi::DenseTensor &self,
                                       const phi::CPUContext &ctx,
                                       py::object obj,
                                       int dim,
                                       int64_t start,
                                       int64_t slicelength) {
  framework::DDim dstDDim = self.dims();
  dstDDim[dim] = static_cast<int64_t>(slicelength);
  std::vector<int> axes({dim});
  std::vector<int> starts({static_cast<int>(start)});
  phi::DenseTensor *output = _getTensor(self, dstDDim);
  _sliceDapper<T>(&self, output, ctx, axes, starts, dstDDim.size());
  return output;
}

template <typename T>
inline phi::DenseTensor *_sliceAndConcat(const phi::DenseTensor &self,
                                         py::object obj,
                                         int dim) {
  phi::CPUContext ctx;
  int64_t start, stop, step, slicelength;
  _getSliceinfo(self, obj, dim, &start, &stop, &step, &slicelength);
  if (step == 1 || slicelength == 1) {
    return _sliceWrapper<T>(self, ctx, obj, dim, start, slicelength);
  } else {
    std::vector<phi::DenseTensor> ins;
    for (auto i = 0; i < slicelength; ++i, start += step) {
      ins.emplace_back(*_sliceWrapper<T>(self, ctx, obj, dim, start, 1));
    }

    // do the concat operation
    framework::DDim dstDDim = self.dims();
    dstDDim[dim] = static_cast<int64_t>(slicelength);
    phi::DenseTensor *output1 = _getTensor(self, dstDDim);
    _concatCompute<T>(ins, output1, ctx, dim);
    return output1;
  }
}

inline phi::DenseTensor *_sliceTensor(const phi::DenseTensor &self,
                                      py::object obj,
                                      int dim) {
  auto src_type = framework::TransToProtoVarType(self.dtype());
  switch (src_type) {
    case framework::proto::VarType::FP16:
      return _sliceAndConcat<paddle::platform::float16>(self, obj, dim);
    case framework::proto::VarType::BF16:
      return _sliceAndConcat<paddle::platform::bfloat16>(self, obj, dim);
    case framework::proto::VarType::COMPLEX64:
      return _sliceAndConcat<paddle::platform::complex<float>>(self, obj, dim);
    case framework::proto::VarType::COMPLEX128:
      return _sliceAndConcat<paddle::platform::complex<double>>(self, obj, dim);
    case framework::proto::VarType::FP32:
      return _sliceAndConcat<float>(self, obj, dim);
    case framework::proto::VarType::FP64:
      return _sliceAndConcat<double>(self, obj, dim);
    case framework::proto::VarType::INT8:
      return _sliceAndConcat<int8_t>(self, obj, dim);
    case framework::proto::VarType::INT16:
      return _sliceAndConcat<int16_t>(self, obj, dim);
    case framework::proto::VarType::INT32:
      return _sliceAndConcat<int>(self, obj, dim);
    case framework::proto::VarType::INT64:
      return _sliceAndConcat<int64_t>(self, obj, dim);
    case framework::proto::VarType::BOOL:
      return _sliceAndConcat<bool>(self, obj, dim);
    case framework::proto::VarType::UINT8:
      return _sliceAndConcat<uint8_t>(self, obj, dim);
    default:
      PADDLE_THROW(platform::errors::InvalidArgument(
          "Not support tensor type: %s",
          framework::DataTypeToString(src_type)));
  }
}

inline phi::DenseTensor *_pySliceTensor(const phi::DenseTensor &self,
                                        py::object obj) {
  if (py::isinstance<py::tuple>(obj)) {
    py::list l = static_cast<py::list>(obj);
    std::unique_ptr<phi::DenseTensor> target;
    phi::DenseTensor *src = const_cast<phi::DenseTensor *>(&self);
    for (auto i = 0; i < static_cast<int>(l.size()); ++i) {
      src = _sliceTensor(*src, l[i], i);
      if (i + 1 == static_cast<int>(l.size())) {
        return src;
      } else {
        target.reset(src);
      }
    }
    return nullptr;
  } else {
    return _sliceTensor(self, obj, 0);
  }
}

inline phi::DenseTensor *PySliceTensor(const phi::DenseTensor &self,
                                       py::object obj) {
  if (platform::is_gpu_place(self.place())) {
    std::unique_ptr<phi::DenseTensor> holder;
    phi::DenseTensor src;
    framework::TensorCopySync(self, platform::CPUPlace(), &src);
    phi::DenseTensor *output = _pySliceTensor(src, obj);
    holder.reset(output);
    phi::DenseTensor *dst = _getTensor(*output, output->dims());
    framework::TensorCopySync(*output, self.place(), dst);
    return dst;
  } else {
    return _pySliceTensor(self, obj);
  }
}

inline py::array TensorToPyArray(const phi::DenseTensor &tensor,
                                 bool need_deep_copy = false) {
  if (!tensor.IsInitialized()) {
    return py::array();
  }
  bool is_gpu_tensor = platform::is_gpu_place(tensor.place());
  bool is_xpu_tensor = platform::is_xpu_place(tensor.place());
  bool is_npu_tensor = platform::is_npu_place(tensor.place());
  bool is_mlu_tensor = platform::is_mlu_place(tensor.place());
  bool is_custom_device_tensor = platform::is_custom_place(tensor.place());
  const auto &tensor_dims = tensor.dims();
  auto tensor_dtype = framework::TransToProtoVarType(tensor.dtype());
  size_t sizeof_dtype = framework::SizeOfType(tensor_dtype);

  std::vector<size_t> py_dims(tensor_dims.size());
  std::vector<size_t> py_strides(tensor_dims.size());

  size_t numel = 1;
  for (int i = tensor_dims.size() - 1; i >= 0; --i) {
    py_dims[i] = static_cast<size_t>(tensor_dims[i]);
    py_strides[i] = sizeof_dtype * numel;
    numel *= py_dims[i];
  }

  const void *tensor_buf_ptr = tensor.data();

  std::string py_dtype_str = details::TensorDTypeToPyDTypeStr(
      framework::TransToProtoVarType(tensor.dtype()));

  if (!is_gpu_tensor && !is_xpu_tensor && !is_npu_tensor && !is_mlu_tensor &&
      !is_custom_device_tensor) {
    if (!need_deep_copy) {
      auto base = py::cast(std::move(tensor));
      return py::array(py::dtype(py_dtype_str.c_str()),
                       py_dims,
                       py_strides,
                       const_cast<void *>(tensor_buf_ptr),
                       base);
    } else {
      py::array py_arr(py::dtype(py_dtype_str.c_str()), py_dims, py_strides);
      PADDLE_ENFORCE_EQ(
          py_arr.writeable(),
          true,
          platform::errors::InvalidArgument(
              "PyArray is not writable, in which case memory leak "
              "or double free would occur"));
      PADDLE_ENFORCE_EQ(
          py_arr.owndata(),
          true,
          platform::errors::InvalidArgument(
              "PyArray does not own data, in which case  memory leak "
              "or double free would occur"));
      platform::CPUPlace place;
      size_t copy_bytes = sizeof_dtype * numel;
      paddle::memory::Copy(
          place, py_arr.mutable_data(), place, tensor_buf_ptr, copy_bytes);
      return py_arr;
    }
  } else if (is_xpu_tensor) {
#ifdef PADDLE_WITH_XPU
    py::array py_arr(py::dtype(py_dtype_str.c_str()), py_dims, py_strides);
    PADDLE_ENFORCE_EQ(py_arr.writeable(),
                      true,
                      platform::errors::InvalidArgument(
                          "PyArray is not writable, in which case memory leak "
                          "or double free would occur"));
    PADDLE_ENFORCE_EQ(
        py_arr.owndata(),
        true,
        platform::errors::InvalidArgument(
            "PyArray does not own data, in which case  memory leak "
            "or double free would occur"));

    size_t copy_bytes = sizeof_dtype * numel;
    auto p = tensor.place();
    paddle::memory::Copy(platform::CPUPlace(),
                         py_arr.mutable_data(),
                         p,
                         tensor_buf_ptr,
                         copy_bytes);
    return py_arr;
#else
    PADDLE_THROW(platform::errors::PermissionDenied(
        "Cannot use XPUPlace in CPU/GPU version, "
        "Please recompile or reinstall Paddle with XPU support."));
#endif
  } else if (is_gpu_tensor) {
#if defined(PADDLE_WITH_CUDA) || defined(PADDLE_WITH_HIP)
    py::array py_arr(py::dtype(py_dtype_str.c_str()), py_dims, py_strides);
    PADDLE_ENFORCE_EQ(py_arr.writeable(),
                      true,
                      platform::errors::InvalidArgument(
                          "PyArray is not writable, in which case memory leak "
                          "or double free would occur"));
    PADDLE_ENFORCE_EQ(
        py_arr.owndata(),
        true,
        platform::errors::InvalidArgument(
            "PyArray does not own data, in which case  memory leak "
            "or double free would occur"));

    size_t copy_bytes = sizeof_dtype * numel;
    auto p = tensor.place();
    paddle::memory::Copy(platform::CPUPlace(),
                         py_arr.mutable_data(),
                         p,
                         tensor_buf_ptr,
                         copy_bytes,
                         nullptr);
    return py_arr;
#else
    PADDLE_THROW(platform::errors::PermissionDenied(
        "Cannot use CUDAPlace in CPU only version, "
        "Please recompile or reinstall Paddle with CUDA support."));
#endif
  } else if (is_npu_tensor) {
#ifdef PADDLE_WITH_ASCEND_CL
    py::array py_arr(py::dtype(py_dtype_str.c_str()), py_dims, py_strides);
    PADDLE_ENFORCE_EQ(py_arr.writeable(),
                      true,
                      platform::errors::InvalidArgument(
                          "PyArray is not writable, in which case memory leak "
                          "or double free would occur"));
    PADDLE_ENFORCE_EQ(
        py_arr.owndata(),
        true,
        platform::errors::InvalidArgument(
            "PyArray does not own data, in which case  memory leak "
            "or double free would occur"));

    size_t copy_bytes = sizeof_dtype * numel;
    auto p = tensor.place();
    platform::DeviceContextPool &pool = platform::DeviceContextPool::Instance();
    auto &ctx = *pool.Get(tensor.place());
    paddle::memory::Copy(
        platform::CPUPlace(),
        py_arr.mutable_data(),
        p,
        tensor_buf_ptr,
        copy_bytes,
        reinterpret_cast<const platform::NPUDeviceContext &>(ctx).stream());
    ctx.Wait();
    return py_arr;
#else
    PADDLE_THROW(platform::errors::PermissionDenied(
        "Cannot use NPUPlace in CPU/GPU/XPU version, "
        "Please recompile or reinstall Paddle with NPU support."));
#endif
  } else if (is_mlu_tensor) {
#ifdef PADDLE_WITH_MLU
    py::array py_arr(py::dtype(py_dtype_str.c_str()), py_dims, py_strides);
    PADDLE_ENFORCE_EQ(py_arr.writeable(),
                      true,
                      platform::errors::InvalidArgument(
                          "PyArray is not writable, in which case memory leak "
                          "or double free would occur"));
    PADDLE_ENFORCE_EQ(
        py_arr.owndata(),
        true,
        platform::errors::InvalidArgument(
            "PyArray does not own data, in which case  memory leak "
            "or double free would occur"));

    size_t copy_bytes = sizeof_dtype * numel;
    auto p = tensor.place();
    platform::DeviceContextPool &pool = platform::DeviceContextPool::Instance();
    auto &ctx = *pool.Get(tensor.place());
    paddle::memory::Copy(
        platform::CPUPlace(),
        py_arr.mutable_data(),
        p,
        tensor_buf_ptr,
        copy_bytes,
        reinterpret_cast<const platform::MLUDeviceContext &>(ctx).stream());
    ctx.Wait();
    return py_arr;
#else
    PADDLE_THROW(platform::errors::PermissionDenied(
        "Cannot use MLUPlace in CPU/GPU/XPU/NPU version, "
        "Please recompile or reinstall Paddle with MLU support."));
#endif
  } else if (is_custom_device_tensor) {
#ifdef PADDLE_WITH_CUSTOM_DEVICE
    py::array py_arr(py::dtype(py_dtype_str.c_str()), py_dims, py_strides);
    PADDLE_ENFORCE_EQ(py_arr.writeable(),
                      true,
                      platform::errors::InvalidArgument(
                          "PyArray is not writable, in which case memory leak "
                          "or double free would occur"));
    PADDLE_ENFORCE_EQ(
        py_arr.owndata(),
        true,
        platform::errors::InvalidArgument(
            "PyArray does not own data, in which case  memory leak "
            "or double free would occur"));

    // TODO(qili93): temporary for ascned npu performance to be removed along
    // with npu_identity op
    paddle::experimental::Tensor tensor_out(
        std::make_shared<phi::DenseTensor>());
    if (tensor.storage_properties_initialized()) {
      paddle::experimental::Tensor tensor_in(
          std::make_shared<phi::DenseTensor>(tensor));
      tensor_out = npu_identity_ad_func(tensor_in, -1);
      auto dense_tensor =
          std::dynamic_pointer_cast<phi::DenseTensor>(tensor_out.impl());
      tensor_buf_ptr = dense_tensor->data();
    }

    size_t copy_bytes = sizeof_dtype * numel;
    platform::DeviceContextPool &pool = platform::DeviceContextPool::Instance();
    auto &ctx = *pool.Get(tensor.place());
    paddle::memory::Copy(
        platform::CPUPlace(),
        py_arr.mutable_data(),
        tensor.place(),
        tensor_buf_ptr,
        copy_bytes,
        reinterpret_cast<const platform::CustomDeviceContext &>(ctx).stream());
    ctx.Wait();
    return py_arr;
#else
    PADDLE_THROW(platform::errors::PermissionDenied(
        "Cannot use CustomPlace in CPU/GPU/XPU/NPU version, "
        "Please recompile or reinstall Paddle with CustomPlace "
        "support."));
#endif
  }
  PADDLE_THROW(platform::errors::Unimplemented("Place is not supported"));
  return py::array();
}

}  // namespace pybind
}  // namespace paddle
