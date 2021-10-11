#include "cinn/frontend/cinn_builder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <random>
#include <vector>

#include "cinn/common/target.h"
#include "cinn/frontend/syntax.h"
#include "cinn/hlir/framework/graph.h"
#include "cinn/hlir/framework/graph_compiler.h"
#include "cinn/hlir/framework/tensor.h"
#include "cinn/hlir/op/use_ops.h"
#ifdef CINN_WITH_CUDA
#include <cuda_runtime.h>
#endif

namespace cinn {
namespace frontend {
namespace {

Program CreateTestProgram() {
  constexpr int B = 8;
  constexpr int M = 32;
  constexpr int N = 24;

  CinnBuilder builder("cinn_builder");
  auto a = builder.CreateInput(Float(32), {M, N / 2}, "A");
  auto b = builder.CreateInput(Float(32), {M, N / 2}, "B");
  auto c = builder.Add(a, b);
  auto x = builder.Div(a, b);
  auto d = builder.Concat(c, x, 1);
  auto e = builder.BroadcastTo(d, {B, M, N}, {1, 2});
  auto f = builder.Concat(a, b, 1);
  auto g = builder.BroadcastTo(f, {B, M, N}, {1, 2});
  auto h = builder.Sub(e, g);
  auto i = builder.Max(e, h);
  auto j = builder.Min(e, h);
  auto k = builder.Mul(i, j);

  auto program = builder.Build();
  return program;
}

void SetRandData(hlir::framework::Tensor tensor, Target target) {
  auto* data = tensor->mutable_data<float>(target);
  std::random_device seed;
  std::default_random_engine engine(seed());
  std::uniform_real_distribution<float> dist(1.f, 2.f);
  size_t num_ele = tensor->shape().numel();
  std::vector<float> random_data(num_ele);
  for (size_t i = 0; i < num_ele; i++) {
    random_data[i] = dist(engine);  // All random data
  }

#ifdef CINN_WITH_CUDA
  cudaMemcpy(data, random_data.data(), num_ele * sizeof(float), cudaMemcpyHostToDevice);
#else
  std::copy(random_data.begin(), random_data.end(), data);
#endif
}

}  // namespace

TEST(cinn_build, basic) {
  auto program = CreateTestProgram();
  // output program
  for (int i = 0; i < program.size(); i++) {
    LOG(INFO) << "instruction: " << program[i];
  }
}

TEST(cinn_build, execution) {
  auto program = CreateTestProgram();
#ifdef CINN_WITH_CUDA
  Target target = common::DefaultNVGPUTarget();
#else
  Target target = common::DefaultHostTarget();
#endif

  auto graph = std::make_shared<hlir::framework::Graph>(program, target);
  LOG(INFO) << "graph:\n" << graph->Visualize();

  auto scope = BuildScope(target, graph);
  hlir::framework::GraphCompiler gc(target, scope, graph);
  auto runtime_program = gc.Build();

  scope->Var<hlir::framework::Tensor>("A");
  scope->Var<hlir::framework::Tensor>("B");

  auto A = scope->GetTensor("A");
  auto B = scope->GetTensor("B");
  SetRandData(A, target);
  SetRandData(B, target);

  runtime_program->Execute();
}

}  // namespace frontend
}  // namespace cinn
