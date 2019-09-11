#include "octree_parser.h"
#include "octree_util.h"

#include <tensorflow/core/framework/common_shape_fns.h>
#include <tensorflow/core/framework/op.h>
#include <tensorflow/core/framework/op_kernel.h>
#include <tensorflow/core/framework/shape_inference.h>

namespace tensorflow {

auto pad_shape_fun = [](::tensorflow::shape_inference::InferenceContext* c) {
  auto top_shape = c->input(0);
  TF_RETURN_IF_ERROR(c->ReplaceDim(top_shape, 2, c->UnknownDim(), &top_shape));
  c->set_output(0, top_shape);
  return Status::OK();
};

REGISTER_OP("OctreePad")
    .Input("btm_data: float")
    .Input("octree: int8")
    .Attr("depth: int")
    .Output("top_data: float")
    .SetShapeFn(pad_shape_fun)
    .Doc(R"doc(Octree padding operator.)doc");

REGISTER_OP("OctreeDepad")
    .Input("top_data: float")
    .Input("octree: int8")
    .Attr("depth: int")
    .Output("btm_data: float")
    .SetShapeFn(pad_shape_fun)
    .Doc(R"doc(Octree depadding operator.)doc");


class OctreePadBase : public OpKernel {
 public:
  explicit OctreePadBase(OpKernelConstruction* context)
    : OpKernel(context) {
    OP_REQUIRES_OK(context, context->GetAttr("depth", &depth_));
    CHECK_GE(depth_, 1) << "Depth should be larger than 1";
  }

  void set_octree_parser(OpKernelContext* context) {
    auto in_octree_ptr = context->input(1).flat<int8>().data();
    octree_.set_gpu(in_octree_ptr);
  }

 protected:
  int depth_;
  OctreeParser octree_;
};


class OctreePadOp : public OctreePadBase {
 public:
  explicit OctreePadOp(OpKernelConstruction* context)
    : OctreePadBase(context) {}

  void Compute(OpKernelContext* context) override {
    // in octree
    this->set_octree_parser(context);

    // btm data
    const Tensor& btm_data = context->input(0);
    const TensorShape& btm_shape = btm_data.shape();
    auto btm_ptr = btm_data.flat<float>().data();
    int channel = btm_shape.dim_size(1);
    int btm_h = btm_shape.dim_size(2);

    // check
    int depth = this->depth_;
    CHECK_EQ(this->octree_.info().node_num_nempty(depth), btm_h);

    // top data
    TensorShape top_shape = btm_shape;
    int top_h = this->octree_.info().node_num(depth);
    top_shape.set_dim(2, top_h);
    Tensor* top_data = nullptr;
    OP_REQUIRES_OK(context, context->allocate_output(0, top_shape, &top_data));
    auto top_ptr = top_data->flat<float>().data();

    // padding data
    pad_forward_gpu(top_ptr, top_h, channel,
      btm_ptr, btm_h, this->octree_.children_gpu(depth));
  }
};


class OctreeDepadOp : public OctreePadBase {
 public:
   explicit OctreeDepadOp(OpKernelConstruction* context)
     : OctreePadBase(context) {}

  void Compute(OpKernelContext* context) override {
    // in octree
    this->set_octree_parser(context);

    // top grad
    const Tensor& top_data = context->input(0);
    const TensorShape& top_shape = top_data.shape();
    auto top_ptr = top_data.flat<float>().data();
    int channel = top_shape.dim_size(1);
    int top_h = top_shape.dim_size(2);

    // check
    int depth = this->depth_;
    CHECK_EQ(this->octree_.info().node_num(depth), top_h) << "depth :" << depth;
    
    // btm grad
    TensorShape btm_shape = top_shape;
    int btm_h = this->octree_.info().node_num_nempty(depth);
    btm_shape.set_dim(2, btm_h);
    Tensor* btm_data = nullptr;
    OP_REQUIRES_OK(context, context->allocate_output(0, btm_shape, &btm_data));
    auto btm_ptr = btm_data->flat<float>().data();
       
    // padding data
    pad_backward_gpu(btm_ptr, btm_h, channel,
      top_ptr, top_h, this->octree_.children_gpu(depth));
  }
};


REGISTER_KERNEL_BUILDER(Name("OctreePad").Device(DEVICE_GPU), OctreePadOp);
REGISTER_KERNEL_BUILDER(Name("OctreeDepad").Device(DEVICE_GPU), OctreeDepadOp);

}  // namespace tensorflow
