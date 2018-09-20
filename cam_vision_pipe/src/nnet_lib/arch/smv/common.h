#ifndef _ARCH_SMV_COMMON_H_
#define _ARCH_SMV_COMMON_H_

#include "nnet_fwd.h"
#include "utility/profiling.h"

// SMV has the same scratchpad sizes as SMIV.
#define SMV_DEFAULT_SPAD_SIZE (131072)
#define SMV_DEFAULT_UMEM_SIZE (3*1048576)
// TODO: This sets the default L2 size to a really large value so that we never
// have to use L2 tiling unless we explicitly set it to a more reasonable size.
// We should have a more explicit way of turning off L2 tiling.
#define SMV_DEFAULT_L2_SIZE (1024*1024*1024)

// Accelerator id codes.
extern unsigned kSmvConvolutionHw;
extern unsigned kSmvInnerProductHw;
extern unsigned kSmvReductionHw;
extern unsigned kSmvBatchNormHw;
extern unsigned kSmvPoolingHw;

typedef struct _smv_global {
    //----------------------//
    // This section must be IDENTICAL to smiv_global (smiv/common.h)!
    float* umem;
    float* spad0;
    float* spad1;
    unsigned kConvolutionHw;
    unsigned kInnerProductHw;
    unsigned kReductionHw;
    unsigned kBatchNormHw;
    unsigned kPoolingHw;
    size_t kUmemSize;
    size_t kSpadSize;
    size_t kL2Size;
    //-----------------------//
} smv_global;

extern smv_global g_smv;

typedef struct _dma_options {
    int src_offset;  // in elements.
    int dst_offset;  // in elements.
    int length;  // in bytes.
    bool use_pipelined_dma;
    bool is_load;
    bool fp16_input;
} dma_options;

typedef enum _smv_sram {
    SMV_SPAD0,
    SMV_SPAD1,
    SMV_UMEM
} smv_sram;

bool smv_inner_product_needs_work_division(layer_t* curr_layer,
                                           smv_global* g_smv);

void smv_standard_convolution_layer_impl(data_list* host_activations,
                                         data_list* host_weights,
                                         layer_t* layers,
                                         int lnum,
                                         data_list* host_result,
                                         smv_global* g_smv,
                                         device_t* device,
                                         sampling_param_t* sampling_param);
void smv_inner_product_layer_impl(data_list* host_activations,
                                  layer_t* layers,
                                  int lnum,
                                  data_list* host_results,
                                  smv_global* g_smv,
                                  device_t* device);
void smv_batch_norm_layer_impl(data_list* activations,
                               layer_t* layers,
                               int lnum,
                               data_list* result,
                               smv_global* g_smv,
                               device_t* device);
void smv_pooling_layer_impl(data_list* inputs,
                            layer_t* curr_layer,
                            smv_global* g_smv,
                            data_list* results,
                            device_t* device);
void smv_activation_fun_fxp(float* local_activations,
                            int batch_size,
                            int input_size,
                            int start_pixel,
                            activation_type activation);
void dma_copy_impl(float* dst_base_loc,
                   float* src_base_loc,
                   unsigned accel_id,
                   int layer_num,
                   smv_global* g_smv,
                   dma_options* options);
#endif
