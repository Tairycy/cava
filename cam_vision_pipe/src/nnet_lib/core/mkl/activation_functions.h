#ifndef _MKL_ACTIVATION_FUNCTIONS_H_
#define _MKL_ACTIVATION_FUNCTIONS_H_

#include <functional>
#include <iostream>

#include "mkldnn.hpp"

#include "arch/nnet_mkl.h"
#include "core/ref/activation_functions.h"
#include "utility/utility.h"

namespace nnet_mkl {

template <typename DType>
class ActivationFunctionOp : public BaseMklOp<DType> {
   public:
    ActivationFunctionOp(layer_t* layer,
                         int batch_size,
                         const mkldnn::engine& eng)
            : BaseMklOp<DType>(layer, batch_size, eng),
              input_size(get_dims_size(&layer->outputs)) {}
    virtual ~ActivationFunctionOp() {}

   protected:
    mem_ref_t create_memory(DType* buffer, int size, bool is_output = false) {
        return BaseMklOp<DType>::create_memory(
                buffer, mem_dims({ size }), mem_fmt::x, is_output);
    }

    // Create a primitive descriptor for this eltwise primitive.
    mkldnn::eltwise_forward::primitive_desc create_primitive_desc(
            mkldnn::algorithm alg,
            mem_d input_md,
            DType alpha = 0,
            DType beta = 0) {
        auto desc = mkldnn::eltwise_forward::desc(
                mkldnn::prop_kind::forward, alg, input_md, alpha, beta);
        return mkldnn::eltwise_forward::primitive_desc(desc, this->engine);
    }

    // Return a reference to the eltwise primitive.
    mkldnn::eltwise_forward& create_primitive(mkldnn::algorithm alg,
                                              mem_ref_t input,
                                              mem_ref_t output,
                                              DType alpha = 0,
                                              DType beta = 0) {
        auto pd = create_primitive_desc(
                alg, input.get_primitive_desc().desc(), alpha, beta);
        this->worklist.emplace_back(mkldnn::eltwise_forward(pd, input, output));
        return static_cast<mkldnn::eltwise_forward&>(this->worklist.back());
    }

    mkldnn::eltwise_forward& create_primitive(
            mkldnn::algorithm alg,
            const mkldnn::primitive& input_prim,
            mem_d input_md,
            DType* output_buffer,
            DType alpha = 0,
            DType beta = 0) {
        auto pd = create_primitive_desc(alg, input_md, alpha, beta);
        mem_ref_t output = BaseMklOp<DType>::create_memory(
                pd.dst_primitive_desc(), output_buffer, true);
        this->worklist.emplace_back(
                mkldnn::eltwise_forward(pd, input_prim, output));
        return static_cast<mkldnn::eltwise_forward&>(this->worklist.back());
    }

    int input_size;
};

namespace mkl_impl {

// Operations defined within this namespace use the MKL-DNN eltwise primitives
// to perform the activation functions.

template <typename DType>
class ReluActivationFunctionOp : public ActivationFunctionOp<DType> {
   public:
    using ActivationFunctionOp<DType>::ActivationFunctionOp;

    virtual void init(DType* input_buffer,
                      DType* output_buffer,
                      DType _negative_slope = 0) {
        INFO_MSG("RELU\n");
        negative_slope = _negative_slope;
        auto input_mem = this->create_memory(input_buffer, this->input_size);
        auto output_mem =
                this->create_memory(output_buffer, this->input_size, true);
        this->create_primitive(mkldnn::algorithm::eltwise_relu,
                               input_mem,
                               output_mem,
                               negative_slope);
    }

    virtual void init(const BaseMklOp<DType>& prev_op,
                      DType* output_buffer,
                      DType _negative_slope = 0) {
        INFO_MSG("RELU, chaining\n");
        negative_slope = _negative_slope;
        this->create_primitive(mkldnn::algorithm::eltwise_relu,
                               prev_op.get_final_primitive(),
                               prev_op.get_output_mem_desc(),
                               output_buffer,
                               negative_slope);
    }

    virtual ~ReluActivationFunctionOp() {}
    virtual std::string name() const {
        return negative_slope == 0 ? "RELU" : "LRELU";
    }

   protected:
    DType negative_slope;
};


template <typename DType>
class SigmoidActivationFunctionOp : public ActivationFunctionOp<DType> {
   public:
    using ActivationFunctionOp<DType>::ActivationFunctionOp;

    virtual void init(DType* input_buffer, DType* output_buffer) {
        auto input_mem =
                this->create_memory(input_buffer, this->input_size);
        auto output_mem =
                this->create_memory(output_buffer, this->input_size, true);
        INFO_MSG("Sigmoid\n");
        this->create_primitive(
                mkldnn::algorithm::eltwise_logistic, input_mem, output_mem);
    }

    virtual void init(const BaseMklOp<DType>& prev_op, DType* output_buffer) {
        INFO_MSG("Sigmoid, chaining\n");
        this->create_primitive(mkldnn::algorithm::eltwise_logistic,
                               prev_op.get_final_primitive(),
                               prev_op.get_output_mem_desc(),
                               output_buffer);
    }
    virtual ~SigmoidActivationFunctionOp() {}
    virtual std::string name() const { return "Sigmoid"; }
};

template <typename DType>
class EluActivationFunctionOp : public ActivationFunctionOp<DType> {
   public:
    using ActivationFunctionOp<DType>::ActivationFunctionOp;
    virtual void init(DType* input_buffer,
                      DType* output_buffer,
                      DType negative_slope = mkl_traits<DType>::to_type(0.1)) {
        auto input_mem = this->create_memory(input_buffer, this->input_size);
        auto output_mem =
                this->create_memory(output_buffer, this->input_size, true);
        INFO_MSG("ELU\n");
        this->create_primitive(mkldnn::algorithm::eltwise_elu,
                               input_mem,
                               output_mem,
                               negative_slope);
    }

    virtual void init(const BaseMklOp<DType>& prev_op,
                      DType* output_buffer,
                      DType negative_slope = mkl_traits<DType>::to_type(0.1)) {
        INFO_MSG("ELU, chaining\n");
        this->create_primitive(mkldnn::algorithm::eltwise_elu,
                               prev_op.get_final_primitive(),
                               prev_op.get_output_mem_desc(),
                               output_buffer,
                               negative_slope);
    }
    virtual ~EluActivationFunctionOp() {}
    virtual std::string name() const { return "ELU"; }
};

template <typename DType>
class TanhActivationFunctionOp : public ActivationFunctionOp<DType> {
   public:
    using ActivationFunctionOp<DType>::ActivationFunctionOp;

    virtual void init(DType* input_buffer, DType* output_buffer) {
        auto input_mem = this->create_memory(input_buffer, this->input_size);
        auto output_mem =
                this->create_memory(output_buffer, this->input_size, true);
        INFO_MSG("Tanh\n");
        this->create_primitive(
                mkldnn::algorithm::eltwise_tanh, input_mem, output_mem);
    }

    virtual void init(const BaseMklOp<DType>& prev_op, DType* output_buffer) {
        INFO_MSG("Tanh, chaining\n");
        this->create_primitive(mkldnn::algorithm::eltwise_tanh,
                               prev_op.get_final_primitive(),
                               prev_op.get_output_mem_desc(),
                               output_buffer);
    }
    virtual ~TanhActivationFunctionOp() {}
    virtual std::string name() const { return "Tanh"; }
};

template <typename DType>
class SeluActivationFunctionOp : public ActivationFunctionOp<DType> {
   public:
    using ActivationFunctionOp<DType>::ActivationFunctionOp;

    static constexpr DType alpha = mkl_traits<DType>::to_type(1.6733);
    static constexpr DType lambda = mkl_traits<DType>::to_type(1.0507);

    virtual void init(DType* input_buffer, DType* output_buffer) {
        INFO_MSG("SELU\n");
        auto input_mem = this->create_memory(input_buffer, this->input_size);
        auto intermediate_mem = this->create_memory(nullptr, this->input_size);
        auto output_mem =
                this->create_memory(output_buffer, this->input_size, true);
        // SELU can be implemented using an ELU, followed by a scaling.
        this->create_primitive(mkldnn::algorithm::eltwise_elu,
                               input_mem,
                               intermediate_mem,
                               alpha);
        // The linear algorithm performs y = Ax + B.
        this->create_primitive(mkldnn::algorithm::eltwise_linear,
                               intermediate_mem,
                               output_mem,
                               lambda);
    }

    virtual void init(const BaseMklOp<DType>& prev_op, DType* output_buffer) {
        INFO_MSG("SELU, chaining\n");
        auto elu_pd = this->create_primitive_desc(
                mkldnn::algorithm::eltwise_elu, prev_op.get_output_mem_desc(),
                alpha);
        auto intermediate_mem =
                BaseMklOp<DType>::create_memory(elu_pd.dst_primitive_desc());

        auto linear_pd = this->create_primitive_desc(
                mkldnn::algorithm::eltwise_linear,
                elu_pd.dst_primitive_desc().desc(), lambda);
        auto output_mem = BaseMklOp<DType>::create_memory(
                linear_pd.dst_primitive_desc(), output_buffer, true);

        this->worklist.emplace_back(mkldnn::eltwise_forward(
                elu_pd, prev_op.get_final_primitive(), intermediate_mem));
        this->worklist.emplace_back(mkldnn::eltwise_forward(
                linear_pd, this->worklist.back(), output_mem));
    }

    virtual ~SeluActivationFunctionOp() {}
    virtual std::string name() const { return "SELU"; }
};

template <typename DType>
class SoftmaxActivationFunctionOp : public ActivationFunctionOp<DType> {
   public:
    using ActivationFunctionOp<DType>::ActivationFunctionOp;

    virtual void init(DType* input_buffer, DType* output_buffer) {
        INFO_MSG("Softmax\n");
        auto input_mem = this->create_memory(
                input_buffer, this->batch_size, this->input_size);
        auto output_mem = this->create_memory(
                output_buffer, this->input_size, this->batch_size, true);
        create_primitive(input_mem, output_mem);
    }

    virtual void init(const BaseMklOp<DType>& prev_op, DType* output_buffer) {
        INFO_MSG("Softmax, chaining\n");
        auto output_mem = this->create_memory(
                output_buffer, this->batch_size, this->input_size, true);
        create_primitive(prev_op.get_final_primitive(),
                         prev_op.get_output_mem_desc(), output_mem);
    }

    mem_ref_t create_memory(DType* buffer,
                            int batch_size,
                            int softmax_size,
                            bool is_output = false) {
        return BaseMklOp<DType>::create_memory(
                buffer,
                mem_dims({ batch_size, softmax_size }),
                mem_fmt::nc,
                is_output);
    }

    mkldnn::softmax_forward& create_primitive(mem_ref_t input,
                                              mem_ref_t output) {
        auto desc = mkldnn::softmax_forward::desc(
                mkldnn::prop_kind::forward_inference,
                input.get_primitive_desc().desc(),
                1);
        auto pd = mkldnn::softmax_forward::primitive_desc(desc, this->engine);
        this->worklist.emplace_back(mkldnn::softmax_forward(pd, input, output));
        return static_cast<mkldnn::softmax_forward&>(this->worklist.back());
    }

    mkldnn::softmax_forward& create_primitive(const mkldnn::primitive& input,
                                              mem_d input_md,
                                              mem_ref_t output) {
        auto desc = mkldnn::softmax_forward::desc(
                mkldnn::prop_kind::forward_inference, input_md, 1);
        auto pd = mkldnn::softmax_forward::primitive_desc(desc, this->engine);
        this->worklist.emplace_back(mkldnn::softmax_forward(pd, input, output));
        return static_cast<mkldnn::softmax_forward&>(this->worklist.back());
    }

    virtual ~SoftmaxActivationFunctionOp() {}
    virtual std::string name() const { return "Softmax"; }
};

}  // namespace mkl_impl

// We bind all arguments to the functor at initialization time so no additional
// arguments are needed. THis is done to ensure that the behavior cannot
// unexpectedly change based on the value of SIGMOID_IMPL in the middle of
// execution (if it should change for any reason).
using RefFunctor = std::function<void()>;

namespace lut {

// Operations defined within this namespace use our reference lookup-table
// implementations to perform the activation functions involving exponential
// functions.

template <typename DType>
class SigmoidActivationFunctionOp
        : public mkl_impl::SigmoidActivationFunctionOp<DType> {
   public:
    using mkl_impl::SigmoidActivationFunctionOp<
            DType>::SigmoidActivationFunctionOp;

    virtual void create_functor(DType* input_buffer, DType* output_buffer) {
        op = std::bind(SIGMOID_IMPL == CenteredLUT
                               ? &sigmoid_lookup_centered
                               : &sigmoid_lookup_noncentered,
                       input_buffer,
                       this->input_size,
                       output_buffer);
    }

    virtual void init(DType* input_buffer, DType* output_buffer) {
        INFO_MSG("Sigmoid LUT\n");
        this->create_memory(input_buffer, this->input_size);
        this->create_memory(output_buffer, this->input_size, true);
        create_functor(input_buffer,
                       reinterpret_cast<DType*>(
                               this->get_output_mem().get_data_handle()));
    }

    virtual void init(const BaseMklOp<DType>& prev_op, DType* output_buffer) {
        INFO_MSG("Sigmoid LUT, chaining\n");
        BaseMklOp<DType>::create_memory(
                prev_op.get_output_mem().get_primitive_desc(),
                output_buffer,
                true);
        create_functor((DType*)prev_op.get_output_mem().get_data_handle(),
                       reinterpret_cast<DType*>(
                               this->get_output_mem().get_data_handle()));
    }

    virtual void run_work() { op(); }

  protected:
    RefFunctor op;
};

template <typename DType>
class EluActivationFunctionOp
        : public mkl_impl::EluActivationFunctionOp<DType> {
   public:
    using mkl_impl::EluActivationFunctionOp<DType>::EluActivationFunctionOp;

    virtual void create_functor(DType* input_buffer,
                                DType* output_buffer,
                                DType alpha) {
        op = std::bind(&elu_lut_fxp, input_buffer, this->input_size, alpha,
                       output_buffer);
    }

    virtual void init(DType* input_buffer,
                      DType* output_buffer,
                      DType alpha = mkl_traits<DType>::to_type(0.1)) {
        INFO_MSG("ELU (LUT)\n");
        this->create_memory(input_buffer, this->input_size);
        this->create_memory(output_buffer, this->input_size, true);
        create_functor(input_buffer,
                       reinterpret_cast<DType*>(
                               this->get_output_mem().get_data_handle()),
                       alpha);
    }

    virtual void init(const BaseMklOp<DType>& prev_op,
                      DType* output_buffer,
                      DType alpha = mkl_traits<DType>::to_type(0.1)) {
        INFO_MSG("ELU (LUT), chaining\n");
        BaseMklOp<DType>::create_memory(
                prev_op.get_output_mem().get_primitive_desc(),
                output_buffer,
                true);
        create_functor((DType*)prev_op.get_output_mem().get_data_handle(),
                       reinterpret_cast<DType*>(
                               this->get_output_mem().get_data_handle()),
                       alpha);
    }
    virtual void run_work() { op(); }

   protected:
    RefFunctor op;
};

template <typename DType>
class TanhActivationFunctionOp : public mkl_impl::TanhActivationFunctionOp<DType> {
   public:
    using mkl_impl::TanhActivationFunctionOp<DType>::TanhActivationFunctionOp;

    virtual void create_functor(DType* input_buffer, DType* output_buffer) {
        op = std::bind(
                &tanh_act, input_buffer, this->input_size, output_buffer);
    }

    virtual void init(DType* input_buffer, DType* output_buffer) {
        INFO_MSG("Tanh LUT\n");
        this->create_memory(input_buffer, this->input_size);
        this->create_memory(output_buffer, this->input_size, true);
        create_functor(input_buffer,
                       reinterpret_cast<DType*>(
                               this->get_output_mem().get_data_handle()));
    }

    virtual void init(const BaseMklOp<DType>& prev_op, DType* output_buffer) {
        INFO_MSG("Tanh, chaining\n");
        BaseMklOp<DType>::create_memory(
                prev_op.get_output_mem().get_primitive_desc(),
                output_buffer,
                true);
        create_functor((DType*)prev_op.get_output_mem().get_data_handle(),
                       reinterpret_cast<DType*>(
                               this->get_output_mem().get_data_handle()));
    }
    virtual void run_work() { op(); }

   protected:
    RefFunctor op;
};

// We only use the LUT implementation for the ELU part of SELU; the linear
// scaling and shift can still be done via MKL-DNN.
template <typename DType>
class SeluActivationFunctionOp
        : public mkl_impl::SeluActivationFunctionOp<DType> {
   public:
    SeluActivationFunctionOp(layer_t* layer,
                             int batch_size,
                             const mkldnn::engine& eng)
            : mkl_impl::SeluActivationFunctionOp<DType>(layer, batch_size, eng),
              elu_op(layer, batch_size, eng) {}

    static constexpr DType alpha = mkl_traits<DType>::to_type(1.6733);
    static constexpr DType lambda = mkl_traits<DType>::to_type(1.0507);

    virtual void init(DType* input_buffer, DType* output_buffer) {
        INFO_MSG("SELU (LUT)\n");
        this->create_memory(input_buffer, this->input_size);
        auto intermediate_mem = this->create_memory(nullptr, this->input_size);
        auto output_mem =
                this->create_memory(output_buffer, this->input_size, true);
        elu_op.init(input_buffer,
                    (DType*)intermediate_mem.get_data_handle(),
                    alpha);
        // The linear algorithm performs y = Ax + B.
        this->create_primitive(mkldnn::algorithm::eltwise_linear,
                               intermediate_mem,
                               output_mem,
                               lambda);
    }

    virtual void init(const BaseMklOp<DType>& prev_op, DType* output_buffer) {
        INFO_MSG("SELU, chaining\n");
        auto intermediate_mem = BaseMklOp<DType>::create_memory(
                prev_op.get_output_mem().get_primitive_desc());
        auto output_mem = BaseMklOp<DType>::create_memory(
                prev_op.get_output_mem().get_primitive_desc(),
                output_buffer,
                true);
        elu_op.init((DType*)prev_op.get_output_mem().get_data_handle(),
                    (DType*)intermediate_mem.get_data_handle(),
                    alpha);
        // The linear algorithm performs y = Ax + B.
        this->create_primitive(mkldnn::algorithm::eltwise_linear,
                               intermediate_mem,
                               output_mem,
                               lambda);
    }

    virtual void run_work() {
        elu_op.run_work();
        BaseMklOp<DType>::run_work();
    }

   protected:
    EluActivationFunctionOp<DType> elu_op;
};

}  // namespace lut

namespace ref {

template <typename DType>
class HardTanhActivationFunctionOp : public ActivationFunctionOp<DType> {
   public:
    using ActivationFunctionOp<DType>::ActivationFunctionOp;

    virtual void create_functor(DType* input_buffer,
                                DType* output_buffer,
                                DType min,
                                DType max) {
        op = std::bind(&hard_tanh,
                       input_buffer,
                       this->input_size,
                       min,
                       max,
                       output_buffer);
    }

    virtual void init(DType* input_buffer,
                      DType* output_buffer,
                      DType min = -1,
                      DType max = 1) {
        INFO_MSG("Hard Tanh ref\n");
        this->create_memory(input_buffer, this->input_size);
        this->create_memory(output_buffer, this->input_size, true);
        create_functor(input_buffer,
                       reinterpret_cast<DType*>(
                               this->get_output_mem().get_data_handle()),
                       min, max);
    }

    virtual void init(const BaseMklOp<DType>& prev_op,
                      DType* output_buffer,
                      DType min = -1,
                      DType max = 1) {
        INFO_MSG("Hard Tanh ref, chaining\n");
        BaseMklOp<DType>::create_memory(
                prev_op.get_output_mem().get_primitive_desc(),
                output_buffer,
                true);
        create_functor((DType*)prev_op.get_output_mem().get_data_handle(),
                       reinterpret_cast<DType*>(
                               this->get_output_mem().get_data_handle()),
                       min, max);
    }

    virtual void run_work() { op(); }

    virtual ~HardTanhActivationFunctionOp() {}
    virtual std::string name() const {
        return "Hard Tanh";
    }

   protected:
    RefFunctor op;

    DType min;
    DType max;
};


}  // namespace ref

void sigmoid(float* activations,
             int batch_size,
             layer_t* layer,
             MklSession* session,
             float* results);

void relu(float* activations,
          int batch_size,
          layer_t* layer,
          MklSession* session,
          float* results,
          float negative_slope = 0);

void elu(float* activations,
         int batch_size,
         layer_t* layer,
         MklSession* session,
         float* results);

void selu(float* activations,
          int batch_size,
          layer_t* layer,
          MklSession* session,
          float* results);

void tanh(float* activations,
          int batch_size,
          layer_t* layer,
          MklSession* session,
          float* results);

void hard_tanh(float* activations,
               int batch_size,
               float min,
               float max,
               layer_t* layer,
               MklSession* session,
               float* results);

void softmax(float* a,
             int batch_size,
             layer_t* layer,
             MklSession* session,
             float* results);

void activation_fun(float* activations,
                    int batch_size,
                    layer_t* layer,
                    float* results,
                    device_t* device);

}  // namespace nnet_mkl

#endif
