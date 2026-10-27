/*
 * Copyright (c) 2025 OceanBase.
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

#ifndef OCEANBASE_SQL_ENGINE_FILE_SQL_OB_FILE_SCAN_OP_H_
#define OCEANBASE_SQL_ENGINE_FILE_SQL_OB_FILE_SCAN_OP_H_

#include "sql/engine/ob_operator.h"
#include "sql/engine/file_sql/ob_file_scan_utils.h"

namespace oceanbase
{
namespace sql
{

// 文件扫描算子：流式读取 CSV/JSONL，按投影列输出
class ObFileScanSpec : public ObOpSpec
{
  OB_UNIS_VERSION_V(1);
public:
  ObFileScanSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObOpSpec(alloc, type),
      path_(),
      path_expr_(nullptr),
      format_(0),
      has_header_(true),
      file_column_cnt_(0) {}
  // 文件绝对路径（已通过 secure_file_priv 校验）
  common::ObString path_;
  ObExpr *path_expr_;   // codegen 生成的路径表达式
  int32_t format_;            // ObFileFormat
  bool has_header_;
  int32_t file_column_cnt_;
};

class ObFileScanOp : public ObOperator
{
public:
  ObFileScanOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
    : ObOperator(exec_ctx, spec, input),
      reader_(),
      is_opened_(false),
      header_skipped_(false),
      fields_() {}
  virtual ~ObFileScanOp() {}

  virtual int inner_open() override;
  virtual int inner_rescan() override;
  virtual int inner_get_next_row() override;
  virtual int inner_close() override;
  virtual void destroy() override;

private:
  int skip_header();
  int fill_output(const common::ObIArray<common::ObString> &fields);

private:
  ObTextFileReader reader_;
  bool is_opened_;
  bool header_skipped_;
  common::ObSEArray<common::ObString, 32, common::ModulePageAllocator, true> fields_;
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_FILE_SQL_OB_FILE_SCAN_OP_H_
