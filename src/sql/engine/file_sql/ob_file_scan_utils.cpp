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

#define USING_LOG_PREFIX SQL_ENG

#include "ob_file_scan_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace sql
{

using namespace common;

static const int64_t FILE_READ_BUFFER_SIZE = 64 * 1024;

// ---------------------------------------------------------------- 工具

bool ObFileSqlUtils::is_integer_literal(const ObString &s)
{
  bool ret = false;
  int64_t i = 0;
  const char *p = s.ptr();
  const int64_t n = s.length();
  if (NULL != p && n > 0 && n <= 20) {
    if ('-' == p[0] || '+' == p[0]) {
      i = 1;
    }
    if (i < n) {
      bool all_digit = true;
      for (int64_t k = i; all_digit && k < n; ++k) {
        all_digit = (p[k] >= '0' && p[k] <= '9');
      }
      ret = all_digit;
    }
  }
  return ret;
}

bool ObFileSqlUtils::is_double_literal(const ObString &s)
{
  bool ret = false;
  const char *p = s.ptr();
  const int64_t n = s.length();
  if (NULL != p && n > 0 && n <= 64) {
    int64_t i = 0;
    if ('-' == p[0] || '+' == p[0]) {
      i = 1;
    }
    bool has_digit = false;
    bool has_dot = false;
    bool valid = true;
    for (int64_t k = i; valid && k < n; ++k) {
      const char c = p[k];
      if (c >= '0' && c <= '9') {
        has_digit = true;
      } else if ('.' == c && !has_dot) {
        has_dot = true;
      } else if (('e' == c || 'E' == c) && has_digit) {
        // 指数部分
        int64_t kk = k + 1;
        if (kk < n && ('-' == p[kk] || '+' == p[kk])) {
          ++kk;
        }
        bool exp_digit = false;
        for (; valid && kk < n; ++kk) {
          if (p[kk] >= '0' && p[kk] <= '9') {
            exp_digit = true;
          } else {
            valid = false;
          }
        }
        if (!exp_digit) {
          valid = false;
        }
        has_dot = true;  // 视为浮点
        break;
      } else {
        valid = false;
      }
    }
    ret = valid && has_digit && has_dot;
  }
  return ret;
}

bool ObFileSqlUtils::is_bool_literal(const ObString &s)
{
  return 4 == s.length() ? 0 == strncasecmp(s.ptr(), "true", 4)
                         : (5 == s.length() && 0 == strncasecmp(s.ptr(), "false", 5));
}

int ObFileSqlUtils::parse_bool(const ObString &s, bool &value)
{
  int ret = OB_SUCCESS;
  if (4 == s.length() && 0 == strncasecmp(s.ptr(), "true", 4)) {
    value = true;
  } else if (5 == s.length() && 0 == strncasecmp(s.ptr(), "false", 5)) {
    value = false;
  } else {
    ret = OB_INVALID_DATA;
  }
  return ret;
}

static bool is_digits_range(const char *p, int64_t from, int64_t to)
{
  bool ok = (from < to);
  for (int64_t k = from; ok && k < to; ++k) {
    ok = (p[k] >= '0' && p[k] <= '9');
  }
  return ok;
}

bool ObFileSqlUtils::is_date_literal(const ObString &s)
{
  // YYYY-MM-DD
  const char *p = s.ptr();
  return NULL != p && 10 == s.length()
         && is_digits_range(p, 0, 4) && '-' == p[4]
         && is_digits_range(p, 5, 7) && '-' == p[7]
         && is_digits_range(p, 8, 10);
}

bool ObFileSqlUtils::is_datetime_literal(const ObString &s)
{
  // YYYY-MM-DD[ T]HH:MM:SS
  const char *p = s.ptr();
  bool ret = false;
  if (NULL != p && s.length() >= 19 && s.length() <= 32
      && is_digits_range(p, 0, 4) && '-' == p[4]
      && is_digits_range(p, 5, 7) && '-' == p[7]
      && is_digits_range(p, 8, 10)
      && (' ' == p[10] || 'T' == p[10] || 't' == p[10])
      && is_digits_range(p, 11, 13) && ':' == p[13]
      && is_digits_range(p, 14, 16) && ':' == p[16]
      && is_digits_range(p, 17, 19)) {
    ret = true;
  }
  return ret;
}

int ObFileSqlUtils::unique_column_name(ObString &name,
                                       ObIAllocator &allocator,
                                       ObIArray<ObString> &used)
{
  int ret = OB_SUCCESS;
  ObString candidate = name;
  int64_t suffix = 2;
  bool dup = true;
  while (OB_SUCC(ret) && dup) {
    dup = false;
    for (int64_t i = 0; !dup && i < used.count(); ++i) {
      if (used.at(i) == candidate) {
        dup = true;
      }
    }
    if (dup) {
      char buf[64];
      int64_t pos = 0;
      if (OB_FAIL(databuff_printf(buf, sizeof(buf), pos, "__%ld", suffix++))) {
      } else {
        ObString new_name;
        char *dst = static_cast<char *>(allocator.alloc(name.length() + pos));
        if (OB_ISNULL(dst)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("alloc column name failed", K(ret));
        } else {
          MEMCPY(dst, name.ptr(), name.length());
          MEMCPY(dst + name.length(), buf, pos);
          new_name.assign_ptr(dst, static_cast<int32_t>(name.length() + pos));
          candidate = new_name;
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    name = candidate;
    if (OB_FAIL(used.push_back(name))) {
      LOG_WARN("push column name failed", K(ret));
    }
  }
  return ret;
}

// ---------------------------------------------------------------- 读取器

ObTextFileReader::ObTextFileReader()
  : fp_(NULL), opt_(), buf_(NULL), buf_cap_(0), buf_pos_(0), buf_end_(0),
    eof_(false), line_no_(0), read_row_buf_(NULL)
{
}

int ObTextFileReader::open(const ObString &path, const ObFileScanOptions &options)
{
  int ret = OB_SUCCESS;
  if (NULL != fp_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("reader already opened", K(ret));
  } else {
    // 路径校验由调用方（resolver）完成。
    // 注意：path 来自 parse node，ptr() 不保证 NUL 结尾，必须自己补。
    char path_buf[4096];
    if (path.length() <= 0 || path.length() >= static_cast<int64_t>(sizeof(path_buf))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid file path length", K(ret), K(path));
    } else {
      MEMCPY(path_buf, path.ptr(), path.length());
      path_buf[path.length()] = '\0';
      fp_ = fopen(path_buf, "rb");
      if (NULL == fp_) {
        ret = OB_FILE_NOT_EXIST;
        LOG_WARN("open file failed", K(ret), K(path));
      }
    }
    if (OB_FAIL(ret)) {
    } else {
      opt_ = options;
      buf_cap_ = FILE_READ_BUFFER_SIZE;
      buf_ = static_cast<char *>(malloc(buf_cap_));
      if (NULL == buf_) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc read buffer failed", K(ret));
      } else {
        buf_pos_ = 0;
        buf_end_ = 0;
        eof_ = false;
        line_no_ = 0;
        read_row_buf_ = NULL;
      }
    }
  }
  return ret;
}

void ObTextFileReader::close()
{
  if (NULL != fp_) {
    fclose(fp_);
    fp_ = NULL;
  }
  if (NULL != buf_) {
    free(buf_);
    buf_ = NULL;
  }
  buf_cap_ = 0;
  buf_pos_ = 0;
  buf_end_ = 0;
  eof_ = false;
  if (NULL != read_row_buf_) {
    free(read_row_buf_);
    read_row_buf_ = NULL;
  }
}

int ObTextFileReader::fill_buffer()
{
  int ret = OB_SUCCESS;
  if (NULL == fp_) {
    ret = OB_NOT_INIT;
  } else {
    buf_pos_ = 0;
    buf_end_ = 0;
    const size_t n = fread(buf_, 1, buf_cap_, fp_);
    if (0 == n) {
      eof_ = true;
    } else {
      buf_end_ = static_cast<int64_t>(n);
    }
  }
  return ret;
}

int ObTextFileReader::next_row(ObIArray<ObString> &fields)
{
  int ret = OB_SUCCESS;
  enum State { FIELD_BEGIN, UNQUOTED, QUOTED, QUOTED_QUOTE };
  State state = FIELD_BEGIN;

  // 行缓冲：整行字段字节连续存放，字段用 (offset,len) 记录
  char *row_buf = NULL;
  int64_t row_cap = 0;
  int64_t row_len = 0;
  int64_t *offs = NULL;
  int64_t *lens = NULL;
  int64_t field_cnt = 0;
  int64_t field_cnt_cap = 0;
  int64_t field_start = 0;   // 当前字段在 row_buf 中的起点
  bool row_started = false;
  bool finished = false;

  const int64_t INIT_FIELD_CAP = 32;
  field_cnt_cap = INIT_FIELD_CAP;
  offs = static_cast<int64_t *>(malloc(sizeof(int64_t) * field_cnt_cap));
  lens = static_cast<int64_t *>(malloc(sizeof(int64_t) * field_cnt_cap));
  if (NULL == offs || NULL == lens) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  }

  while (OB_SUCC(ret) && !finished) {
    // 取下一个字符
    if (buf_pos_ >= buf_end_) {
      if (eof_) {
        // 文件结束
        if (row_started || field_cnt > 0 || row_len > 0) {
          finished = true;  // 最后一行没有换行符
        } else {
          ret = OB_ITER_END;
        }
        break;
      } else if (OB_FAIL(fill_buffer())) {
        LOG_WARN("fill buffer failed", K(ret));
        break;
      }
      if (eof_) {
        if (row_started || field_cnt > 0 || row_len > 0) {
          finished = true;
        } else {
          ret = OB_ITER_END;
        }
        break;
      }
    }
    const char c = buf_[buf_pos_++];
    row_started = true;

    if (FIELD_BEGIN == state || UNQUOTED == state) {
      if (FIELD_BEGIN == state && c == opt_.quote_) {
        state = QUOTED;
      } else if (c == opt_.delimiter_) {
        // 字段结束（可能是空字段）
        if (field_cnt >= field_cnt_cap) {
          field_cnt_cap *= 2;
          int64_t *n_offs = static_cast<int64_t *>(realloc(offs, sizeof(int64_t) * field_cnt_cap));
          int64_t *n_lens = static_cast<int64_t *>(realloc(lens, sizeof(int64_t) * field_cnt_cap));
          if (NULL == n_offs || NULL == n_lens) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            break;
          }
          offs = n_offs;
          lens = n_lens;
        }
        offs[field_cnt] = field_start;
        lens[field_cnt] = row_len - field_start;
        ++field_cnt;
        field_start = row_len;
        state = FIELD_BEGIN;
      } else if ('\n' == c) {
        ++line_no_;
        finished = true;
      } else if ('\r' == c) {
        ++line_no_;
        if (buf_pos_ < buf_end_ && '\n' == buf_[buf_pos_]) {
          ++buf_pos_;
        }
        finished = true;
      } else {
        state = UNQUOTED;
        if (row_len + 1 > row_cap) {
          row_cap = (row_cap + 1) * 2;
          char *n_buf = static_cast<char *>(realloc(row_buf, row_cap));
          if (NULL == n_buf) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            break;
          }
          row_buf = n_buf;
        }
        row_buf[row_len++] = c;
      }
    } else if (QUOTED == state) {
      if (c == opt_.quote_) {
        state = QUOTED_QUOTE;
      } else {
        // 引号内的逗号与换行都是数据
        if ('\n' == c) {
          ++line_no_;
        }
        if (row_len + 1 > row_cap) {
          row_cap = (row_cap + 1) * 2;
          char *n_buf = static_cast<char *>(realloc(row_buf, row_cap));
          if (NULL == n_buf) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            break;
          }
          row_buf = n_buf;
        }
        row_buf[row_len++] = c;
      }
    } else { // QUOTED_QUOTE
      if (c == opt_.quote_) {
        // 转义的引号 ""
        if (row_len + 1 > row_cap) {
          row_cap = (row_cap + 1) * 2;
          char *n_buf = static_cast<char *>(realloc(row_buf, row_cap));
          if (NULL == n_buf) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            break;
          }
          row_buf = n_buf;
        }
        row_buf[row_len++] = c;
        state = QUOTED;
      } else if (c == opt_.delimiter_) {
        if (field_cnt >= field_cnt_cap) {
          field_cnt_cap *= 2;
          int64_t *n_offs = static_cast<int64_t *>(realloc(offs, sizeof(int64_t) * field_cnt_cap));
          int64_t *n_lens = static_cast<int64_t *>(realloc(lens, sizeof(int64_t) * field_cnt_cap));
          if (NULL == n_offs || NULL == n_lens) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            break;
          }
          offs = n_offs;
          lens = n_lens;
        }
        offs[field_cnt] = field_start;
        lens[field_cnt] = row_len - field_start;
        ++field_cnt;
        field_start = row_len;
        state = FIELD_BEGIN;
      } else if ('\n' == c) {
        ++line_no_;
        finished = true;
      } else if ('\r' == c) {
        ++line_no_;
        if (buf_pos_ < buf_end_ && '\n' == buf_[buf_pos_]) {
          ++buf_pos_;
        }
        finished = true;
      } else {
        state = UNQUOTED;
        if (row_len + 1 > row_cap) {
          row_cap = (row_cap + 1) * 2;
          char *n_buf = static_cast<char *>(realloc(row_buf, row_cap));
          if (NULL == n_buf) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            break;
          }
          row_buf = n_buf;
        }
        row_buf[row_len++] = c;
      }
    }
  }

  // 收尾：最后一列
  if (OB_SUCC(ret) && finished) {
    if (field_cnt >= field_cnt_cap) {
      field_cnt_cap += 1;
      int64_t *n_offs = static_cast<int64_t *>(realloc(offs, sizeof(int64_t) * field_cnt_cap));
      int64_t *n_lens = static_cast<int64_t *>(realloc(lens, sizeof(int64_t) * field_cnt_cap));
      if (NULL == n_offs || NULL == n_lens) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        offs = n_offs;
        lens = n_lens;
      }
    }
    if (OB_SUCC(ret)) {
      offs[field_cnt] = field_start;
      lens[field_cnt] = row_len - field_start;
      ++field_cnt;
    }
  }

  if (OB_SUCC(ret) && finished) {
    fields.reuse();
    for (int64_t i = 0; OB_SUCC(ret) && i < field_cnt; ++i) {
      ObString field;
      if (lens[i] > 0) {
        field.assign_ptr(row_buf + offs[i], static_cast<int32_t>(lens[i]));
      } else {
        field.reset();
      }
      if (OB_FAIL(fields.push_back(field))) {
        LOG_WARN("push field failed", K(ret));
      }
    }
  }

  if (NULL != offs) { free(offs); }
  if (NULL != lens) { free(lens); }
  // row_buf 交给调用方（字段指向它），此处不能释放；用 read_row_buf_ 保存
  if (OB_SUCC(ret) && finished) {
    if (NULL != read_row_buf_) {
      free(read_row_buf_);
    }
    read_row_buf_ = row_buf;
  } else if (NULL != row_buf) {
    free(row_buf);
  }
  return ret;
}

// ---------------------------------------------------------------- 推断

int ObFileSchemaInferer::merge_value(ObFileColumn &col, const ObString &value)
{
  int ret = OB_SUCCESS;
  if (0 == value.length()) {
    col.nullable_ = true;
  } else {
    ObObjType new_type = ObVarcharType;
    if (ObFileSqlUtils::is_bool_literal(value)) {
      new_type = ObTinyIntType;  // BOOLEAN 在 OB 内是 tinyint(1)
    } else if (ObFileSqlUtils::is_integer_literal(value)) {
      new_type = ObIntType;
    } else if (ObFileSqlUtils::is_double_literal(value)) {
      new_type = ObDoubleType;
    } else if (ObFileSqlUtils::is_datetime_literal(value)) {
      new_type = ObDateTimeType;
    } else if (ObFileSqlUtils::is_date_literal(value)) {
      new_type = ObDateType;
    }

    if (ObNullType == col.type_) {
      col.type_ = new_type;
      if (ObTinyIntType == new_type) {
        col.type_len_ = 1;
      }
    } else if (col.type_ == new_type) {
      // 同类型
    } else if ((ObIntType == col.type_ && ObDoubleType == new_type)
               || (ObDoubleType == col.type_ && ObIntType == new_type)) {
      col.type_ = ObDoubleType;
    } else if ((ObDateType == col.type_ && ObDateTimeType == new_type)
               || (ObDateTimeType == col.type_ && ObDateType == new_type)) {
      col.type_ = ObDateTimeType;
    } else {
      col.type_ = ObVarcharType;
    }

    if (ObVarcharType == col.type_ && value.length() > col.type_len_) {
      col.type_len_ = value.length();
    }
  }
  return ret;
}

void ObFileSchemaInferer::finalize(ObFileColumn &col)
{
  if (ObNullType == col.type_) {
    // 全 NULL 列
    col.type_ = ObVarcharType;
    col.type_len_ = 256;
  } else if (ObVarcharType == col.type_ && col.type_len_ < 256) {
    col.type_len_ = 256;
  }
}

int ObFileSchemaInferer::infer_csv(const ObString &path,
                                   const ObFileScanOptions &options,
                                   ObIAllocator &allocator,
                                   ObFileColumnArray &columns)
{
  int ret = OB_SUCCESS;
  ObTextFileReader reader;
  ObSEArray<ObString, 32> fields;
  ObSEArray<ObString, 32> used_names;
  bool first_row = true;
  int64_t row_count = 0;

  if (OB_FAIL(reader.open(path, options))) {
    LOG_WARN("open file for infer failed", K(ret), K(path));
  }
  while (OB_SUCC(ret)) {
    fields.reuse();
    if (OB_FAIL(reader.next_row(fields))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("read row failed", K(ret));
      }
    } else if (first_row && options.has_header_) {
      // 表头 -> 列名
      for (int64_t i = 0; OB_SUCC(ret) && i < fields.count(); ++i) {
        ObFileColumn col;
        ObString name;
        if (OB_FAIL(ob_write_string(allocator, fields.at(i), name))) {
          LOG_WARN("copy column name failed", K(ret));
        } else if (OB_FAIL(ObFileSqlUtils::unique_column_name(name, allocator, used_names))) {
          LOG_WARN("unique column name failed", K(ret));
        } else {
          col.name_ = name;
          col.nullable_ = false;
          if (OB_FAIL(columns.push_back(col))) {
            LOG_WARN("push column failed", K(ret));
          }
        }
      }
      first_row = false;
    } else {
      if (first_row) {
        // 无表头：用 c1,c2,... 命名
        for (int64_t i = 0; OB_SUCC(ret) && i < fields.count(); ++i) {
          ObFileColumn col;
          char buf[32];
          int64_t pos = 0;
          if (OB_FAIL(databuff_printf(buf, sizeof(buf), pos, "c%ld", i + 1))) {
          } else {
            ObString name;
            if (OB_FAIL(ob_write_string(allocator, ObString(pos, buf), name))) {
            } else {
              col.name_ = name;
              col.nullable_ = false;
              if (OB_FAIL(columns.push_back(col))) {
              }
            }
          }
        }
        first_row = false;
      }
      // 值 -> 类型
      for (int64_t i = 0; OB_SUCC(ret) && i < columns.count(); ++i) {
        ObString value;
        if (i < fields.count()) {
          value = fields.at(i);
        }
        if (OB_FAIL(merge_value(columns.at(i), value))) {
          LOG_WARN("merge value failed", K(ret));
        }
      }
      ++row_count;
    }
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
      break;
    }
  }
  reader.close();
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; i < columns.count(); ++i) {
      finalize(columns.at(i));
    }
  }
  LOG_INFO("file schema inferred", K(path), K(row_count), "column_cnt", columns.count());
  return ret;
}

} // namespace sql
} // namespace oceanbase
