#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "utility/utility.h"
#include "nnet_fwd.h"

#include "init_data.h"

inline float gen_uniform() {
    return randfloat();
}

void memsetfp(float* buffer, float value, int size) {
    buffer = (float*)ASSUME_ALIGNED(buffer, CACHELINE_SIZE);
    for (int i = 0; i < size; i++) {
        buffer[i] = value;
    }
}

// Returns an approximately normally distributed random value, using the
// Box-Muller method.
float gen_gaussian(float mu, float sigma) {
    static bool return_saved = false;
    static float saved = 0;

    if (return_saved) {
        return_saved = false;
        return saved * sigma + mu;
    } else {
        float u = gen_uniform();
        float v = gen_uniform();
        float scale = sqrt(-2 * log(u));
        float x = scale * cos(2 * 3.1415926535 * v);
        float y = scale * sin(2 * 3.1415926535 * v);
        saved = y;
        return_saved = true;
        return x * sigma + mu;
    }
}

float get_rand_weight(data_init_mode mode, int depth) {
    if (mode == RANDOM) {
        // A standard distribution can still result in large numbers
        // unrepresentable in FP16, so in order to reduce this likelihood, just
        // shrink the variance.
        return conv_float2fixed(gen_gaussian(0, 0.1));
    } else {
        // Give each depth slice a different weight so we don't get all the
        // same value in the output.
        return (depth + 1) * 0.1;
    }
}

void init_fc_weights(float* weights,
                     int w_height,
                     int w_rows,
                     int w_cols,
                     int w_pad,
                     data_init_mode mode,
                     bool transpose) {
    int w_tot_rows = transpose ? w_rows + w_pad : w_rows;
    int w_tot_cols = transpose ? w_cols : w_cols + w_pad;
    if (mode == FAST_FIXED) {
        int num_weights = w_tot_rows * w_tot_cols;
        memsetfp(weights, 0.001, num_weights);
        int num_biases = w_tot_cols;
        memsetfp(weights + num_weights, -12, num_biases);
        return;
    }
    // Always store the biases after all the weights, regardless of whether
    // weights are transposed or not.
    float val = 0;
    // First store the weights.
    for (int i = 0; i < w_rows; i++) {
        for (int j = 0; j < w_tot_cols; j++) {
            if (transpose) {
                // Loop var i can't go past the row padding!
                val = get_rand_weight(mode, i);
                weights[sub2ind(j, i, w_rows + w_pad)] = val;
            } else {
                val = j < w_cols ? get_rand_weight(mode, i) : 0;
                weights[sub2ind(i, j, w_cols + w_pad)] = val;
            }
        }
    }
    int bias_offset = transpose ? (w_rows + w_pad) * w_cols
                                : (w_cols + w_pad) * w_rows;
    // Store the biases.
    for (int j = 0; j < w_tot_cols; j++) {
        if (j < w_cols) {
            val = get_rand_weight(mode, w_rows);
        } else {
            val = 0;
        }
        weights[bias_offset + j] = val;
    }
}

void init_conv_weights(float* weights,
                       int w_depth,
                       int w_height,
                       int w_rows,
                       int w_cols,
                       int w_pad,
                       data_init_mode mode,
                       bool transpose) {
    int w_tot_cols = w_cols + w_pad;
    if (mode == FAST_FIXED) {
        int num_weights = w_depth * w_height * w_rows *  w_tot_cols;
        memsetfp(weights, 0.001, num_weights / 2);
        memsetfp(weights + num_weights / 2, -0.001, num_weights / 2);
        return;
    }
    float val = 0;
    for (int d = 0; d < w_depth; d++) {
        for (int h = 0; h < w_height; h++) {
            for (int i = 0; i < w_rows; i++) {
                for (int j = 0; j < w_tot_cols; j++) {
                    if (j < w_cols) {
                        val= get_rand_weight(mode, d);
                    } else {  // extra zero padding.
                        val = 0;
                    }
                    weights[sub4ind(d, h, i, j, w_height, w_rows, w_tot_cols)] =
                            val;
                }
            }
        }
    }
}

// Pointwise convolution weights are organized like this:
//
//   Filters -->
//
// Channels [ k00 k10 k20 k30 ... ]
//    |     [ k01 k11 k21 k31 ... ]
//    |     [ k02 k12 k22 k32 ... ]
//    V       ...
//            ...
// biases:  [ b0  b1  b2  b3  ... ]
void init_pointwise_conv_weights(float* weights,
                                 int w_height,
                                 int w_rows,
                                 int w_cols,
                                 int w_pad,
                                 data_init_mode mode,
                                 bool transpose) {
    // 1x1 convolutions initialize weights just like we do for FC layers.
    // The last row consists of biases. This is the ONLY convolutional layer
    // type that supports biases currently!
    init_fc_weights(weights, w_height, w_rows, w_cols, w_pad, mode, false);
}

void init_bn_weights(float* weights,
                     int w_height,
                     int w_rows,
                     int w_cols,
                     int w_pad,
                     data_init_mode mode,
                     bool precompute_variance) {
    static const float kEpsilon = 1e-5;
    int w_tot_cols = w_cols + w_pad;
    if (mode == FAST_FIXED) {
        int num_weights = w_height * w_rows *  w_tot_cols;
        memsetfp(weights, kEpsilon, num_weights / 2);
        memsetfp(weights + num_weights / 2, -kEpsilon, num_weights / 2);
        return;
    }

    float val = 0;
    for (int h = 0; h < w_height; h++) {
        for (int i = 0; i < w_rows; i++) {
            // BN parameters are stored in blocks of w_rows * w_tot_cols.
            // The block order is:
            //   1. mean
            //   2. variance
            //   3. gamma
            //   4. beta
            for (int j = 0; j < w_tot_cols; j++) {
                if (j < w_cols) {
                    val = get_rand_weight(mode, 0);
                } else {  // extra zero padding.
                    val = 0;
                }
                bool is_variance_block = (i / (w_rows / 4)) == 1;
                if (is_variance_block) {
                    // Variance cannot be negative.
                    val = val < 0 ? -val : val;
                    if (precompute_variance) {
                        // Precompute 1/sqrt(var + eps) if ARCH is not MKLDNN
                        // (in MKLDNN, we can't do this trick yet).
                        val = 1.0 / (sqrt(val + kEpsilon));
                    }
                }

                weights[sub3ind(h, i, j, w_rows, w_tot_cols)] = val;
            }
        }
    }
}

void init_weights(float* weights,
                  layer_t* layers,
                  int num_layers,
                  data_init_mode mode,
                  bool transpose) {
    int l;
    int w_rows, w_cols, w_height, w_depth, w_offset, w_pad;

    assert(mode == RANDOM || mode == FIXED || mode == FAST_FIXED);
    w_offset = 0;
    printf("Initializing weights randomly\n");

    for (l = 0; l < num_layers; l++) {
        get_weights_dims_layer(
                layers, l, &w_rows, &w_cols, &w_height, &w_depth, &w_pad);
        switch (layers[l].type) {
            case CONV_POINTWISE:
                init_pointwise_conv_weights(weights + w_offset, w_height,
                                            w_rows, w_cols, w_pad, mode,
                                            transpose);
                break;
            case FC:
                init_fc_weights(weights + w_offset, w_height, w_rows, w_cols,
                                w_pad, mode, transpose);
                break;
            case CONV_STANDARD:
            case CONV_DEPTHWISE:
                init_conv_weights(weights + w_offset, w_depth, w_height, w_rows,
                                  w_cols, w_pad, mode, transpose);
                break;
            case BATCH_NORM:
                init_bn_weights(weights + w_offset, w_height, w_rows, w_cols,
                                w_pad, mode, PRECOMPUTE_BN_VARIANCE);
                break;
            default:
                continue;
        }
        w_offset += get_num_weights_layer(layers, l);
    }
    // NOTE: FOR SIGMOID ACTIVATION FUNCTION, WEIGHTS SHOULD BE BIG
    // Otherwise everything just becomes ~0.5 after sigmoid, and results are
    // boring
}

void init_data(float* data,
               network_t* network,
               size_t num_test_cases,
               data_init_mode mode) {
    unsigned i;
    int j, k, l;
    int input_rows, input_cols, input_height, input_align_pad;
    int input_dim;

    input_rows = network->layers[0].inputs.rows;
    input_cols = network->layers[0].inputs.cols;
    input_height = network->layers[0].inputs.height;
    input_align_pad = network->layers[0].inputs.align_pad;
    input_dim = input_rows * input_cols * input_height;

    ARRAY_4D(float, _data, data, input_height, input_rows,
             input_cols + input_align_pad);
    assert(mode == RANDOM || mode == FIXED || mode == FAST_FIXED);
    printf("Initializing data randomly\n");
    if (mode == FAST_FIXED) {
        int num_inputs = num_test_cases * input_height * input_rows *
                         (input_cols + input_align_pad);
        memsetfp(data, 0.002, num_inputs / 2);
        memsetfp(data + num_inputs / 2, -0.002, num_inputs / 2);
        return;
    }
    // Generate random input data, size num_test_cases by num_units[0]
    // (input dimensionality)
    for (i = 0; i < num_test_cases; i++) {
        int offset = 0;
        for (j = 0; j < input_height; j++) {
            for (k = 0; k < input_rows; k++) {
                for (l = 0; l < input_cols; l++) {
                    if (mode == RANDOM) {
                        _data[i][j][k][l] = conv_float2fixed(gen_gaussian(0, 1));
                    } else {
                        // Make each input image distinguishable.
                        _data[i][j][k][l] = 1.0 * i + (float)offset / input_dim;
                        offset++;
                    }
                }
                for (l = input_cols; l < input_cols + input_align_pad; l++) {
                    _data[i][j][k][l] = 0;
                }
            }
        }
    }
    PRINT_MSG("Input activations:\n");
    PRINT_DEBUG4D(data, input_rows, input_cols + input_align_pad, input_height);
}

void init_labels(int* labels, size_t label_size, data_init_mode mode) {
    unsigned i;
    assert(mode == RANDOM || mode == FIXED || mode == FAST_FIXED);
    printf("Initializing labels randomly\n");
    for (i = 0; i < label_size; i++) {
        labels[i] = 0;  // set all labels to 0
    }
}
