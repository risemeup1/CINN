// Copyright (c) 2022 CINN Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>

#include <cfloat>

#include "cinn/cinn.h"
#include "cinn/frontend/net_builder.h"
#include "cinn/frontend/optimize.h"
#include "cinn/frontend/pass/pass_test_helper.h"
#include "cinn/frontend/pass/use_program_pass.h"
#include "cinn/frontend/program_pass.h"
#include "cinn/frontend/syntax.h"
#include "cinn/hlir/framework/graph.h"
#include "cinn/hlir/framework/graph_compiler.h"
#include "cinn/hlir/framework/pass.h"
#include "cinn/hlir/op/use_ops.h"
#include "cinn/hlir/pass/use_pass.h"
#include "cinn/utils/data_util.h"

namespace cinn::frontend {

void RunWithProgram(const Program& program,
                    const Target& target,
                    const std::shared_ptr<hlir::framework::Scope>& scope) {
  auto graph = std::make_shared<hlir::framework::Graph>(program, target);
  hlir::framework::ApplyPasses(graph.get(), {"InferShape"});
  hlir::framework::ApplyPasses(graph.get(), DefaultOpFusionPasses());
  VLOG(1) << "graph:\n" << graph->Visualize();
  hlir::framework::GraphCompiler gc(target, scope, graph);
  auto runtime_program = gc.Build();
  runtime_program->Execute();
}

TEST(TransposeFoldingInput, TransposeWithMultiMamtul) {
  if (!IsCompiledWithCUDA()) {
    return;
  }
  NetBuilder builder("net_builder");
  auto x           = builder.CreateInput(Float(32), {2, 2}, "X");
  auto y           = builder.CreateInput(Float(32), {2, 2}, "Y");
  auto transpose_y = builder.Transpose(y, {1, 0});
  auto dot1        = builder.Matmul(x, transpose_y);
  auto dot2        = builder.Matmul(transpose_y, x);
  auto out         = builder.Add(dot1, dot2);
  auto program     = builder.Build();

  common::Target target = common::DefaultNVGPUTarget();
  std::vector<std::string> input_ids;
  absl::c_transform(std::vector<absl::string_view>{x.id(), y.id()},
                    std::back_inserter(input_ids),
                    [](absl::string_view id) { return std::string(id); });
  std::pair<std::vector<std::string>, std::vector<std::string>> passes{
      {"Decomposer"}, {"TransposeFoldingInput", "GemmRewriter", "TransposeFoldingOutput", "GemmRewriter"}};
  CompareResult(&program, target, input_ids, {out->id}, 1, passes, 123, true);
}

}  // namespace cinn::frontend
