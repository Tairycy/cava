#ifndef _SMIV_CORE_H_
#define _SMIV_CORE_H_

#include <stdint.h>

#include "core/nnet_fwd_defs.h"

void matrix_multiply_with_bias_smiv(float* a,
                                    float* b,
                                    int a_height,
                                    int b_height,
                                    int b_width,
                                    int a_pad,
                                    activation_type act_func,
                                    bool do_bias,
                                    float* result);

void reduction_smiv(float* a, layer_t curr_layer, float* result);

void convolution3d_smiv(float* a,
                        float* kernels,
                        layer_t curr_layer,
                        int start_chan,
                        float* result);

void batch_norm_simd_fxp(float* inputs,
                         float* weights,
                         const layer_t* curr_layer,
                         int batch_size,
                         float* result,
                         int weight_col_start);

void maxpooling_nhwc_smiv(float* inputs,
                          layer_t curr_layer,
                          int input_start_chan,
                          float* results);

void avgpooling_nhwc_smiv(float* inputs,
                          layer_t curr_layer,
                          int input_start_chan,
                          float* results);

void decompress_packed_csr_data_smiv_fxp(uint32_t* cmp_data,
                                         int cmp_col_offset,
                                         int cmp_row_offset,
                                         int dest_offset,
                                         dims_t* data_dims,
                                         float* dcmp_data);

#endif
