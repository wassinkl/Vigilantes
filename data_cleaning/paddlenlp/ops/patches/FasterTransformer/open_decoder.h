/*
 * Copyright (c) 2021 PaddlePaddle Authors. All Rights Reserved.
 * Copyright (c) 2020, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/**
 * Decoder for a Single Step of a Single Layer
 **/

#pragma once
#include <assert.h>
#include <cuda_fp16.h>
#include <cuda_runtime.h>
#include "fastertransformer/allocator.h"
#include "fastertransformer/common.h"
#include "fastertransformer/common_structure.h"

namespace fastertransformer {

template <typename T>
class DecoderInitParam {
public:
  /* weights for masked_multi_head_attention */
  LayerNormWeight<T> self_layernorm;
  AttentionWeight<T> self_attention;

  LayerNormWeight<T> cross_layernorm;
  AttentionWeight<T> cross_attention;

  LayerNormWeight<T> ffn_layernorm;
  FFNWeight<T> ffn;
  cublasHandle_t cublas_handle;
  cudaStream_t stream;
};

template <OperationType OpType_>
class DecoderTransformerTraits;

template <>
class DecoderTransformerTraits<OperationType::FP32>
    : public TransformerTraits<OperationType::FP32> {};

template <>
class DecoderTransformerTraits<OperationType::FP16>
    : public TransformerTraits<OperationType::FP16> {};

template <OperationType OpType_>
class OpenDecoder {
private:
  typedef DecoderTransformerTraits<OpType_> Traits_;
  typedef typename Traits_::DataType DataType_;
  DecoderInitParam<DataType_> param_;

  const cudaDataType_t computeType_ = Traits_::computeType;
  const cudaDataType_t AType_ = Traits_::AType;
  const cudaDataType_t BType_ = Traits_::BType;
  const cudaDataType_t CType_ = Traits_::CType;
  int cublasAlgo_[5];

  int batch_size_;
  int max_seq_len_;
  int head_num_;
  int size_per_head_;
  int hidden_units_;
  int memory_hidden_units_;
  int normalization_before_;
  ActivationType act_;

  DataType_ *norm_from_tensor_buf_, *query_buf_;
  DataType_ *context_buf_, *masked_output_buf_;
  DataType_ *norm_masked_output_buf_, *cross_output_buf_;
  DataType_ *norm_cross_output_buf_, *ffn_inner_buf_, *ffn_out_buf_;
  DataType_ *key_buf_, *value_buf_;

  DataType_ **qkv_kernel_;
  DataType_ **qkv_input_;
  DataType_ **qkv_buf_;

  bool is_fuse_QKV;

public:
  OpenDecoder(int batch_size,
              int seq_len,
              int head_num,
              int size_per_head,
              int memory_hidden_units,
              bool normalization_before = true,
              ActivationType act = ActivationType::GELU)
      // Activation function default to GELU for GPT.
      : batch_size_(batch_size),
        max_seq_len_(seq_len),
        head_num_(head_num),
        size_per_head_(size_per_head),
        memory_hidden_units_(memory_hidden_units),
        normalization_before_(normalization_before),
        act_(act) {
#ifndef NDEBUG
    PRINT_FUNC_NAME_();
#endif
    hidden_units_ = head_num_ * size_per_head_;

    FILE *fd = fopen("decoding_gemm_config.in", "r");
    int err = 0;
    if (fd == NULL) {
      ;
    } else {
      // First number is a setting for gemm in Decoding, which computes the
      // embedding output.
      // so we need to skip the number
      float split_time, fused_time;
      err = fscanf(fd,
                   "%*d %*f %d %f %d %*f %d %*f %d %*f %d %f",
                   &cublasAlgo_[0],
                   &split_time,
                   &cublasAlgo_[1],
                   &cublasAlgo_[2],
                   &cublasAlgo_[3],
                   &cublasAlgo_[4],
                   &fused_time);
      is_fuse_QKV = fused_time < split_time * 3 ? true : false;
      fclose(fd);
    }
    if (err != 7) {
      // printf("[WARNING] decoder loading GEMM algorithms error, using default
      // GEMM algorithms!\n");
      int default_algo;
      if (Traits_::OpType == OperationType::FP32) {
        default_algo = CUBLAS_GEMM_DEFAULT;
      } else {
        default_algo = CUBLAS_GEMM_DEFAULT_TENSOR_OP;
      }
      for (int i = 0; i < 5; i++) cublasAlgo_[i] = default_algo;
      is_fuse_QKV = false;
    } else {
      // check that the gemm_config setting is runnable
      if (Traits_::OpType == OperationType::FP32) {
        for (int i = 0; i < 5; i++) {
          if (cublasAlgo_[i] > CUBLAS_GEMM_ALGO23 ||
              cublasAlgo_[i] < CUBLAS_GEMM_DEFAULT) {
            // the algorithm is not for FP32
            printf("[ERROR] cuBLAS Algorithm %d is not used in FP32. \n",
                   (int)cublasAlgo_[i]);
            exit(-1);
          }
        }
      } else {
        for (int i = 0; i < 5; i++) {
          if (cublasAlgo_[i] > CUBLAS_GEMM_ALGO15_TENSOR_OP ||
              cublasAlgo_[i] < CUBLAS_GEMM_DEFAULT_TENSOR_OP) {
            // the algorithm is not for FP16
            printf("[ERROR] cuBLAS Algorithm %d is not used in FP16. \n",
                   (int)cublasAlgo_[i]);
            exit(-1);
          }
        }
      }
    }
  }

  int getWorkspaceSize() {
    int buf_size = batch_size_ * hidden_units_;
    return 13 * buf_size + sizeof(DataType_ *) * 9;
  }

  void initialize_stream(cudaStream_t stream) { param_.stream = stream; }

  void initialize(DecoderInitParam<DataType_> param, DataType_ *buf) {
#ifndef NDEBUG
// PRINT_FUNC_NAME_();
#endif
    param_ = param;
    const int buf_size = batch_size_ * hidden_units_;
    norm_from_tensor_buf_ = buf;
    ffn_out_buf_ = buf;
    query_buf_ = buf + buf_size;  // store the query values (from_tensor * Q) in
                                  // both masked and multi-head attention
    key_buf_ = buf + 2 * buf_size;
    value_buf_ = buf + 3 * buf_size;
    context_buf_ = buf + 4 * buf_size;  // store the context result
                                        // (softmax(qk)v) in both masked and
                                        // multi-head attention

    masked_output_buf_ = buf + 5 * buf_size;  // masked_attention_output
    norm_masked_output_buf_ =
        buf + 6 * buf_size;  // norm(masked_attention_output)

    cross_output_buf_ = buf + 7 * buf_size;  // mutli-head attention_output
    norm_cross_output_buf_ =
        buf + 8 * buf_size;               // norm(multi-head attention_output)
    ffn_inner_buf_ = buf + 9 * buf_size;  // 4 buf size to store inner product

    qkv_kernel_ = (DataType_ **)(ffn_inner_buf_ + 4 * buf_size);
    qkv_input_ = qkv_kernel_ + 3;
    qkv_buf_ = qkv_input_ + 3;

    if (is_fuse_QKV == true) {
      const DataType_ *hA[]{param_.self_attention.query_weight.kernel,
                            param_.self_attention.key_weight.kernel,
                            param_.self_attention.value_weight.kernel,
                            norm_from_tensor_buf_,
                            norm_from_tensor_buf_,
                            norm_from_tensor_buf_,
                            query_buf_,
                            key_buf_,
                            value_buf_};
      cudaMemcpyAsync((void *)qkv_kernel_,
                      hA,
                      sizeof(DataType_ *) * 9,
                      cudaMemcpyHostToDevice,
                      param_.stream);
    }
  }

  void forward(const DataType_ *from_tensor,
               const DataType_ *memory_tensor,
               DataType_ *key_cache_,
               DataType_ *value_cache_,
               DataType_ *key_mem_cache_,
               DataType_ *value_mem_cache_,
               const int *memory_sequence_length,
               DataType_ *decoder_output,
               const int step,
               const bool is_cross_attention) {
#ifndef NDEBUG
// PRINT_FUNC_NAME_();
#endif
    const int m = batch_size_;
    const int n = hidden_units_;

    try {
      /* masked multi-head attention */
      /* layernorm(from_tensor) -> norm_from_tensor_buf_ */
      if (normalization_before_) {
        decoder_norm1(from_tensor,
                      param_.self_layernorm.gamma,
                      param_.self_layernorm.beta,
                      norm_from_tensor_buf_,
                      m,
                      n);

#ifndef NDEBUG
        cudaDeviceSynchronize();
        check_cuda_error(cudaGetLastError());
#endif
        masked_multi_head_attention(norm_from_tensor_buf_,
                                    key_cache_,
                                    value_cache_,
                                    masked_output_buf_,
                                    step);

#ifndef NDEBUG
        cudaDeviceSynchronize();
        check_cuda_error(cudaGetLastError());
#endif

        if (is_cross_attention == true) {
          /*
              add bias to masked_output_buf_
              masked_output_buf_ + from_tensor -> masked_output_buf_
              norm(masked_output_buf_) -> norm_masked_output_buf_
          */
          decoder_norm2(from_tensor,
                        param_.cross_layernorm.gamma,
                        param_.cross_layernorm.beta,
                        param_.self_attention.attention_output_weight.bias,
                        masked_output_buf_,
                        norm_masked_output_buf_,
                        m,
                        n);
#ifndef NDEBUG
          cudaDeviceSynchronize();
          check_cuda_error(cudaGetLastError());
#endif
          // For Attention is All You Need decoder
          /* cross attention with memory */
          cross_multi_head_attention(norm_masked_output_buf_,
                                     memory_tensor,
                                     key_mem_cache_,
                                     value_mem_cache_,
                                     cross_output_buf_,
                                     memory_sequence_length,
                                     max_seq_len_,
                                     step);
#ifndef NDEBUG
          cudaDeviceSynchronize();
          check_cuda_error(cudaGetLastError());
#endif
          /*
              cross_output_buf_ + bias + masked_output_buf_ -> cross_output_buf_
              norm(cross_otuput_buf) -> normed_last_context (input for ffn)
          */
          decoder_norm2(masked_output_buf_,
                        param_.ffn_layernorm.gamma,
                        param_.ffn_layernorm.beta,
                        param_.cross_attention.attention_output_weight.bias,
                        cross_output_buf_,
                        norm_cross_output_buf_,
                        m,
                        n);
#ifndef NDEBUG
          cudaDeviceSynchronize();
          check_cuda_error(cudaGetLastError());
#endif
          ffn(norm_cross_output_buf_,
              ffn_inner_buf_,
              decoder_output,
              m,
              4 * n,
              n,
              act_);
#ifndef NDEBUG
          cudaDeviceSynchronize();
          check_cuda_error(cudaGetLastError());
#endif
          add_bias_input(decoder_output, cross_output_buf_, m, n);
        } else {
          decoder_norm2(from_tensor,
                        param_.ffn_layernorm.gamma,
                        param_.ffn_layernorm.beta,
                        param_.self_attention.attention_output_weight.bias,
                        masked_output_buf_,
                        norm_masked_output_buf_,
                        m,
                        n);
#ifndef NDEBUG
          cudaDeviceSynchronize();
          check_cuda_error(cudaGetLastError());
#endif
          // For GPT-2 decoder
          ffn(norm_masked_output_buf_,
              ffn_inner_buf_,
              decoder_output,
              m,
              4 * n,
              n,
              act_);
#ifndef NDEBUG
          cudaDeviceSynchronize();
          check_cuda_error(cudaGetLastError());
#endif
          add_bias_input(decoder_output, masked_output_buf_, m, n);
        }
#ifndef NDEBUG
        cudaDeviceSynchronize();
        check_cuda_error(cudaGetLastError());
#endif
      } else {
        // post-normalization
        masked_multi_head_attention(
            from_tensor, key_cache_, value_cache_, masked_output_buf_, step);

#ifndef NDEBUG
        cudaDeviceSynchronize();
        check_cuda_error(cudaGetLastError());
#endif
        decoder_norm2(from_tensor,
                      param_.self_layernorm.gamma,
                      param_.self_layernorm.beta,
                      param_.self_attention.attention_output_weight.bias,
                      masked_output_buf_,
                      norm_masked_output_buf_,
                      m,
                      n);

#ifndef NDEBUG
        cudaDeviceSynchronize();
        check_cuda_error(cudaGetLastError());
#endif
        if (is_cross_attention == true) {
          // For Attention is All You Need decoder
          /* cross attention with memory */
          cross_multi_head_attention(norm_masked_output_buf_,
                                     memory_tensor,
                                     key_mem_cache_,
                                     value_mem_cache_,
                                     cross_output_buf_,
                                     memory_sequence_length,
                                     max_seq_len_,
                                     step);

#ifndef NDEBUG
          cudaDeviceSynchronize();
          check_cuda_error(cudaGetLastError());
#endif
          /*
              cross_output_buf_ + bias + masked_output_buf_ -> cross_output_buf_
              norm(cross_otuput_buf) -> normed_last_context (input for ffn)
          */
          decoder_norm2(norm_masked_output_buf_,
                        param_.cross_layernorm.gamma,
                        param_.cross_layernorm.beta,
                        param_.cross_attention.attention_output_weight.bias,
                        cross_output_buf_,
                        norm_cross_output_buf_,
                        m,
                        n);

#ifndef NDEBUG
          cudaDeviceSynchronize();
          check_cuda_error(cudaGetLastError());
#endif
          ffn(norm_cross_output_buf_,
              ffn_inner_buf_,
              ffn_out_buf_,
              m,
              4 * n,
              n,
              act_);

#ifndef NDEBUG
          cudaDeviceSynchronize();
          check_cuda_error(cudaGetLastError());
#endif
          add_bias_input(ffn_out_buf_, norm_cross_output_buf_, m, n);

#ifndef NDEBUG
          cudaDeviceSynchronize();
          check_cuda_error(cudaGetLastError());
#endif

          decoder_norm1(ffn_out_buf_,
                        param_.ffn_layernorm.gamma,
                        param_.ffn_layernorm.beta,
                        decoder_output,
                        m,
                        n);

        } else {
          ffn(norm_masked_output_buf_,
              ffn_inner_buf_,
              ffn_out_buf_,
              m,
              4 * n,
              n,
              act_);
#ifndef NDEBUG
          cudaDeviceSynchronize();
          check_cuda_error(cudaGetLastError());
#endif

          add_bias_input(ffn_out_buf_, norm_masked_output_buf_, m, n);

#ifndef NDEBUG
          cudaDeviceSynchronize();
          check_cuda_error(cudaGetLastError());
#endif

          decoder_norm1(ffn_out_buf_,
                        param_.ffn_layernorm.gamma,
                        param_.ffn_layernorm.beta,
                        decoder_output,
                        m,
                        n);
        }
      }
    } catch (std::runtime_error &error) {
      throw error;
    }

#ifndef NDEBUG
    cudaDeviceSynchronize();
    check_cuda_error(cudaGetLastError());
#endif
  }
  void masked_multi_head_attention(const DataType_ *from_tensor,
                                   DataType_ *key_cache_,
                                   DataType_ *value_cache_,
                                   DataType_ *decoder_output,
                                   const int step);

  void cross_multi_head_attention(const DataType_ *from_tensor,
                                  const DataType_ *memory_tensor,
                                  DataType_ *key_mem_cache_,
                                  DataType_ *value_mem_cache_,
                                  DataType_ *decoder_output,
                                  const int *memory_sequence_length,
                                  const int max_seq_len,
                                  const int step);

  void ffn(const DataType_ *input,
           DataType_ *ffn_inner,
           DataType_ *output,
           const int m,
           const int inner_size,
           const int n,
           ActivationType activation_type);

  void decoder_norm1(const DataType_ *from_tensor,
                     const DataType_ *gamma,
                     const DataType_ *beta,
                     DataType_ *norm_from_tensor_buf_,
                     const int m,
                     const int n);

  void decoder_norm2(const DataType_ *from_tensor,
                     const DataType_ *gamma,
                     const DataType_ *beta,
                     const DataType_ *bias,
                     DataType_ *output,
                     DataType_ *norm_output_buf_,
                     const int m,
                     const int n);

  void add_bias_input(DataType_ *output,
                      const DataType_ *input,
                      const int m,
                      const int n);

  ~OpenDecoder() {
    norm_from_tensor_buf_ = nullptr;
    query_buf_ = nullptr;
    key_buf_ = nullptr;
    value_buf_ = nullptr;
    context_buf_ = nullptr;

    masked_output_buf_ = nullptr;
    norm_masked_output_buf_ = nullptr;

    cross_output_buf_ = nullptr;
    norm_cross_output_buf_ = nullptr;
    ffn_inner_buf_ = nullptr;
  }
};
}  // namespace fastertransformer
