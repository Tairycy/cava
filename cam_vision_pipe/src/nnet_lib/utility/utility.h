#ifndef _NNET_LIB_UTILITY_H_
#define _NNET_LIB_UTILITY_H_

#include <stdbool.h>
#include "nnet_fwd.h"
#include "common/utility.h"

float* grab_matrix(float* w, int n, int* n_rows, int* n_columns);
size_t get_weights_loc_for_layer(layer_t* layers, int layer);

#if defined(DMA_INTERFACE_V2)
#define INPUT_BYTES(layers, lnum)                                              \
    get_input_activations_size(layers, lnum) * sizeof(float)
#define OUTPUT_BYTES(layers, lnum)                                             \
    get_output_activations_size(layers, lnum) * sizeof(float)
#define WEIGHT_BYTES(layers, lnum)                                             \
    get_num_weights_layer(layers, lnum) * sizeof(float)

int get_input_activations_size(layer_t* layers, int num_layers);
int get_output_activations_size(layer_t* layers, int num_layers);
void grab_weights_dma(float* weights, int layer, layer_t* layers);
size_t grab_input_activations_dma(float* activations, int layer, layer_t* layers);
size_t grab_output_activations_dma(float* activations, int layer, layer_t* layers);
size_t store_output_activations_dma(float* activations, int layer, layer_t* layers);

#elif defined(DMA_INTERFACE_V3)
#define INPUT_BYTES(layers, lnum)                                              \
    get_input_activations_size(&layers[lnum]) * sizeof(float)
#define OUTPUT_BYTES(layers, lnum)                                             \
    get_output_activations_size(&layers[lnum]) * sizeof(float)
#define WEIGHT_BYTES(layers, lnum)                                             \
    get_num_weights_layer(layers, lnum) * sizeof(float)

int get_input_activations_size(layer_t* layer);
int get_output_activations_size(layer_t* layer);
void grab_weights_dma(float* host_weights,
                      float* accel_weights,
                      int layer,
                      layer_t* layers);
size_t grab_output_activations_dma(float* host_activations,
                                   float* accel_activations,
                                   layer_t* layer);
size_t grab_input_activations_dma(float* host_activations,
                                  float* accel_activations,
                                  layer_t* layer);
size_t store_output_activations_dma(float* host_activations,
                                    float* accel_activations,
                                    layer_t* layer);

#endif

void copy_data_col_range(float* original_data,
                              dims_t* original_dims,
                              int start_col,
                              int num_cols,
                              float* new_buffer);

static inline void clflush(void* addr) {
    __asm__ volatile("clflush %0" : : "r"(addr));
}

// Some of older our processors (e.g., Xeon E5-2670) do not implement clflushopt
// and clwb instructions (introduced in 2014), so if we want to build on the
// older machines, gcc won't allow us to generate those intructions. That's why
// we end up using raw bytes to make the flush intructions and forcing the input
// to rax instead of letting the compiler pick any available register.

static inline void clflushopt(void* addr) {
    __asm__ volatile("mov %0, %%rax\n\t"
                     ".byte 0x66, 0x0F, 0xAE, 0x38"
                     :
                     : "r"(addr) /* input */
                     : "%rax"    /* clobbered register */
                     );
}

static inline void clwb(void* addr) {
    __asm__ volatile("mov %0, %%rax\n\t"
                     ".byte 0x66, 0x0F, 0xAE, 0x30"
                     :
                     : "r"(addr) /* input */
                     : "%rax"    /* clobbered register */
                     );
}

bool has_padding(padding* pad);
int calc_conv_rows(layer_t* layer, bool account_for_padding);
int calc_conv_cols(layer_t* layer, bool account_for_padding);

void flush_cache_range(void* src, size_t total_bytes);

float randfloat();
const char* bool_to_yesno(bool value);
void clear_matrix(float* input, int size);
void copy_matrix(float* input, float* output, int size);
int arg_max(float* input, int size, int increment);
int arg_min(float* input, int size, int increment);
int calc_padding(int value, unsigned alignment);
int get_weights_offset_layer(layer_t* layers, int l);
void get_weights_dims_layer(layer_t* layers,
                            int l,
                            int* num_rows,
                            int* num_cols,
                            int* num_height,
                            int* num_depth,
                            int* num_pad);
void get_unpadded_inputs_dims_layer(layer_t* layers,
                                    int l,
                                    int* num_rows,
                                    int* num_cols,
                                    int* num_height,
                                    int* pad_amt);
int get_num_weights_layer(layer_t* layers, int l);
int get_total_num_weights(layer_t* layers, int num_layers);
bool is_dummy_layer(layer_t* layers, int l);
size_t next_multiple(size_t request, size_t align);
size_t get_dims_size(dims_t* dims);
size_t get_nhwc_dims_size(dims_t* dims);
dims_t transpose_dims(dims_t* orig_dims, int data_alignment);

float compute_errors(float* network_pred,
                     int* correct_labels,
                     int batch_size,
                     int num_classes);
void write_output_labels(const char* fname,
                         float* network_pred,
                         int batch_size,
                         int num_classes,
                         int pred_pad);

void print_debug(float* array,
                 int rows_to_print,
                 int cols_to_print,
                 int num_columns);
void print_debug4d(float* array, int rows, int cols, int height);
void print_debug4d_fp16(
        packed_fp16* array, int num, int height, int rows, int cols);
void print_data_and_weights(float* data, float* weights, layer_t first_layer);

data_list* create_new_data_list_if_necessary(data_list* curr_data,
                                             int size_in_bytes,
                                             data_storage_t tgt_storage_fmt);
void require_data_type(data_list* list, int index, data_storage_t req_type);

const char* data_storage_str(data_storage_t type);
void init_data_list_storage(data_list* list, int len);
data_list* init_data_list(int len);
data_list* copy_data_list(data_list* dest, data_list* source);
void free_data_list(data_list* list);
farray_t* init_farray(int len, bool zero);
fp16array_t* init_fp16array(int len, bool zero);
void free_farray(farray_t* array);
void free_fp16array(fp16array_t* array);
farray_t* create_new_farray_if_necessary(farray_t* array,
                                         size_t num_elems,
                                         bool zero);
fp16array_t* create_new_fp16array_if_necessary(fp16array_t* array,
                                               size_t num_elems,
                                               bool zero);
farray_t* copy_farray(farray_t* existing);
fp16array_t* copy_fp16array(fp16array_t* existing);

#ifdef BITWIDTH_REDUCTION
// Don't add this function unless we want to model bit width quantization
// effects. In particular, do not enable this if we are building a trace.  We
// don't want to add in the cost of dynamically doing this operation - we
// assume the data is already in the specified reduced precision and all
// functional units are designed to handle that bitwidth. So just make this
// function go away.
float conv_float2fixed(float input);
#else
#define conv_float2fixed(X) (X)
#endif

#endif
