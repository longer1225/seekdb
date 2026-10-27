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

#ifndef OCEANBASE_SQL_ENGINE_FILE_SQL_OB_FILE_SCAN_UTILS_H_
#define OCEANBASE_SQL_ENGINE_FILE_SQL_OB_FILE_SCAN_UTILS_H_

#include "lib/ob_define.h"
#include "lib/string/ob_string.h"
#include "lib/container/ob_se_array.h"
#include "lib/allocator/ob_allocator.h"
#include "common/object/ob_object.h"

namespace oceanbase
{
namespace sql
{

// 文件表格式
enum ObFileFormat
{
  OB_FILE_FORMAT_CSV = 0,
  OB_FILE_FORMAT_JSONL = 1,
  OB_FILE_FORMAT_PARQUET = 2,
};

// CSV/JSONL 推断出的列
struct ObFileColumn
{
  ObFileColumn()
    : name_(), type_(ObNullType), type_len_(0), type_scale_(0), nullable_(true) {}
  common::ObString name_;
  ObObjType type_;
  int32_t type_len_;    // VARCHAR 长度
  int16_t type_scale_;  // DOUBLE/DATETIME scale
  bool nullable_;
  TO_STRING_KV(K_(name), K_(type), K_(type_len), K_(type_scale), K_(nullable));
};

typedef common::ObSEArray<ObFileColumn, 16, common::ModulePageAllocator, true> ObFileColumnArray;

// 扫描选项
struct ObFileScanOptions
{
  ObFileScanOptions()
    : format_(OB_FILE_FORMAT_CSV), has_header_(true), sample_size_(-1),
      max_rows_(INT64_MAX), delimiter_(','), quote_('"') {}
  ObFileFormat format_;
  bool has_header_;
  int64_t sample_size_;  // 仅支持 -1（全量推断）
  int64_t max_rows_;
  char delimiter_;
  char quote_;
};

// 流式文本读取器：按行产出字段，不物化整个文件
class ObTextFileReader
{
public:
  ObTextFileReader();
  ~ObTextFileReader() { close(); }

  // path 由调用方保证已经过 secure_file_priv 校验
  int open(const common::ObString &path, const ObFileScanOptions &options);
  void close();
  // 读一行；fields 指向内部缓冲，只在下次 next_row 前有效
  // 返回 OB_ITER_END 表示文件结束
  int next_row(common::ObIArray<common::ObString> &fields);
  int64_t get_line_no() const { return line_no_; }

private:
  int fill_buffer();

private:
  FILE *fp_;
  ObFileScanOptions opt_;
  char *buf_;
  int64_t buf_cap_;
  int64_t buf_pos_;
  int64_t buf_end_;
  bool eof_;
  int64_t line_no_;
  // 上一行字段字节的宿主，字段 ObString 指向它，只在下次 next_row 前有效
  char *read_row_buf_;
};

// 类型推断器：全量扫描文件（不缓存数据）推断列类型
class ObFileSchemaInferer
{
public:
  static int infer_csv(const common::ObString &path,
                       const ObFileScanOptions &options,
                       common::ObIAllocator &allocator,
                       ObFileColumnArray &columns);
  // 把单个文本值并入列类型（宽化规则）
  static int merge_value(ObFileColumn &col, const common::ObString &value);
  // 累积类型 -> 最终类型（全 NULL 列、VARCHAR 长度）
  static void finalize(ObFileColumn &col);
};

// 工具
class ObFileSqlUtils
{
public:
  // 去重：重复列名变为 name__2、name__3...
  static int unique_column_name(common::ObString &name,
                                common::ObIAllocator &allocator,
                                common::ObIArray<common::ObString> &used);
  static bool is_integer_literal(const common::ObString &s);
  static bool is_double_literal(const common::ObString &s);
  static bool is_bool_literal(const common::ObString &s);
  static bool is_date_literal(const common::ObString &s);
  static bool is_datetime_literal(const common::ObString &s);
  static int parse_bool(const common::ObString &s, bool &value);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_FILE_SQL_OB_FILE_SCAN_UTILS_H_
