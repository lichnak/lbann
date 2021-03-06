////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2014-2016, Lawrence Livermore National Security, LLC. 
// Produced at the Lawrence Livermore National Laboratory. 
// Written by the LBANN Research Team (B. Van Essen, et al.) listed in
// the CONTRIBUTORS file. <lbann-dev@llnl.gov>
//
// LLNL-CODE-697807.
// All rights reserved.
//
// This file is part of LBANN: Livermore Big Artificial Neural Network
// Toolkit. For details, see http://software.llnl.gov/LBANN or
// https://github.com/LLNL/LBANN. 
//
// Licensed under the Apache License, Version 2.0 (the "Licensee"); you
// may not use this file except in compliance with the License.  You may
// obtain a copy of the License at:
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the license.
//
// cudnn_wrapper .hpp .cpp - cuDNN support - wrapper classes, utility functions
////////////////////////////////////////////////////////////////////////////////

#ifndef CUDNN_WRAPPER_HPP_INCLUDED
#define CUDNN_WRAPPER_HPP_INCLUDED

#include <vector>
#include "lbann/lbann_base.hpp"
#include "lbann/lbann_comm.hpp"
#include "lbann/utils/lbann_exception.hpp"
#include "lbann/layers/lbann_layer_activations.hpp"

#ifdef __LIB_CUDNN
#include <cuda.h>
#include <cudnn.h>
#include <cub/util_allocator.cuh>
#endif // #ifdef __LIB_CUDNN

// Error utility macros
#ifdef __LIB_CUDNN
#ifdef LBANN_DEBUG
#define checkCUDA(cuda_call) {                                          \
    const cudaError_t status = cuda_call;                               \
    if (status != cudaSuccess) {                                        \
      std::cerr << "CUDA error: " << cudaGetErrorString(status) << "\n"; \
      std::cerr << "Error at " << __FILE__ << ":" << __LINE__ << "\n";  \
      cudaDeviceReset();                                                \
      throw lbann::lbann_exception("CUDA error");                       \
    }                                                                   \
  }
#define checkCUDNN(cudnn_call) {                                        \
    const cudnnStatus_t status = cudnn_call;                            \
    if (status != CUDNN_STATUS_SUCCESS) {                               \
      std::cerr << "cuDNN error: " << cudnnGetErrorString(status) << "\n"; \
      std::cerr << "Error at " << __FILE__ << ":" << __LINE__ << "\n";  \
      cudaDeviceReset();                                                \
      throw lbann::lbann_exception("cuDNN error");                      \
    }                                                                   \
  }
#else
#define checkCUDA(cuda_call) cuda_call
#define checkCUDNN(cudnn_call) cudnn_call
#endif // #ifdef LBANN_DEBUG
#endif // #ifdef __LIB_CUDNN

namespace cudnn
{

  /** cuDNN manager class */
  class cudnn_manager
  {
#ifdef __LIB_CUDNN

  public:
    /** Constructor
     *  @param _comm         Pointer to LBANN communicator
     *  @param max_num_gpus  Maximum Number of available GPUs. If
     *                       negative, then use all available GPUs.
     */
    cudnn_manager(lbann::lbann_comm* _comm, Int max_num_gpus = -1);

    /** Destructor */
    ~cudnn_manager();

    /** Print cuDNN version information to standard output. */
    void print_version() const;
    /** Get cuDNN data type associated with C++ data type. */
    cudnnDataType_t get_cudnn_data_type() const;

    /** Get number of GPUs assigned to current process. */
    Int get_num_gpus() const;
    /** Get number of GPUs on current node. */
    Int get_num_total_gpus() const;
    /** Get GPUs for current process. */
    std::vector<int>& get_gpus();
    /** Get GPUs for current process (const). */
    const std::vector<int>& get_gpus() const;
    /** Get ith GPU for current process. */
    int get_gpu(Int i=0) const;
    /** Get GPU memory allocator. */
    cub::CachingDeviceAllocator& get_gpu_memory();
    /** Get GPU memory allocator (const). */
    const cub::CachingDeviceAllocator& get_gpu_memory() const;
    /** Get CUDA streams for current process. */
    std::vector<cudaStream_t>& get_streams();
    /** Get CUDA streams for current process (const). */
    const std::vector<cudaStream_t>& get_streams() const;
    /** Get ith CUDA stream for current process. */
    cudaStream_t& get_stream(Int i=0);
    /** Get ith CUDA stream for current process (const). */
    const cudaStream_t& get_stream(Int i=0) const;
    /** Get cuDNN handles for current process. */
    std::vector<cudnnHandle_t>& get_handles();
    /** Get cuDNN handles for current process (const). */
    const std::vector<cudnnHandle_t>& get_handles() const;
    /** Get ith cuDNN handle for current process. */
    cudnnHandle_t& get_handle(Int i=0);
    /** Get ith cuDNN handle for current process (const). */
    const cudnnHandle_t& get_handle(Int i=0) const;
    

    /** Allocate memory on GPUs. */
    void allocate_on_gpus(std::vector<DataType*>& gpu_data,
                          Int height,
                          Int width_per_gpu);
    /** Deallocate memory on GPUs. */
    void deallocate_on_gpus(std::vector<DataType*>& gpu_data);

    /** Copy data on GPUs. */
    void copy_on_gpus(std::vector<DataType*>& gpu_dst_data,
                      const std::vector<DataType*>& gpu_src_data,
                      Int height,
                      Int width_per_gpu);
    /** Copy data from CPU to GPUs.
     *  Matrix columns are scattered amongst GPUs.
     */
    void scatter_to_gpus(std::vector<DataType*>& gpu_data,
                         const Mat& cpu_data,
                         Int width_per_gpu);
    /** Copy data from GPUs to CPU.
     *  Matrix columns are gathered from GPUs.
     */
    void gather_from_gpus(Mat& cpu_data,
                          const std::vector<DataType*>& gpu_data,
                          Int width_per_gpu);
    /** Copy data from CPU to GPUs.
     *  Data is duplicated across GPUs.
     */
    void broadcast_to_gpus(std::vector<DataType*>& gpu_data,
                           const Mat& cpu_data);
    /** Copy data from GPUs to CPU and reduce.
     */
    void reduce_from_gpus(Mat& cpu_data,
                          const std::vector<DataType*>& gpu_data);

    /** Synchronize GPUs. */
    void synchronize();

    /// Register a block of memory to pin
    void pin_ptr(void* ptr, size_t sz);
    /// Pin the memory block of a matrix and return the block size
    size_t pin_memory_block(ElMat *mat);
    /// Unpin the memory block of a matrix
    void unpin_memory_block(ElMat *mat);
    /// Unregister a block of pinnedmemory
    void unpin_ptr(void* ptr);
    /// Unregister all the memories registered to pin
    void unpin_ptrs(void);
    /// report the total size of memory blocks pinned
    size_t get_total_size_of_pinned_blocks(void) const;

  private:

    /** LBANN communicator. */
    lbann::lbann_comm* comm;

    /** Number of GPUs for current process. */
    Int m_num_gpus;
    /** Number of available GPUs. */
    Int m_num_total_gpus;

    /** GPU memory allocator.
     *  Faster than cudaMalloc/cudaFree since it uses a memory pool. */
    cub::CachingDeviceAllocator* m_gpu_memory;

    /** GPUs for current process. */
    std::vector<int> m_gpus;
    /** CUDA streams for current process. */
    std::vector<cudaStream_t> m_streams;
    /** cuDNN handles for current process. */
    std::vector<cudnnHandle_t> m_handles;
    /** Pinned memory addresses. */
    std::map<void*, size_t> pinned_ptr;

#endif // #ifdef __LIB_CUDNN
  };

}

#endif // CUDNN_WRAPPER_HPP_INCLUDED
