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

#include "ob_file_scan_op.h"
#include <stdlib.h>
#include "sql/engine/ob_exec_context.h"
#include "share/ob_errno.h"

namespace oceanbase
{
namespace sql
{

using namespace common;

OB_SERIALIZE_MEMBER((ObFileScanSpec, ObOpSpec),
                    path_,
                    format_,
                    has_header_,
                    file_column_cnt_);

int ObFileScanOp::inner_open()
{
  int ret = OB_SUCCESS;
  ObFileScanOptions options;
  options.format_ = static_cast<ObFileFormat>(MY_SPEC.format_);
  options.has_header_ = MY_SPEC.has_header_;
  if (OB_FAIL(reader_.open(MY_SPEC.path_, options))) {
    LOG_WARN("open file failed", K(ret), K(MY_SPEC.path_));
  } else {
    is_opened_ = true;
    header_skipped_ = false;
    if (MY_SPEC.has_header_ && OB_FAIL(skip_header())) {
      LOG_WARN("skip header failed", K(ret));
    }
  }
  return ret;
}

int ObFileScanOp::skip_header()
{
  int ret = OB_SUCCESS;
  if (!header_skipped_) {
    fields_.reuse();
    if (OB_FAIL(reader_.next_row(fields_))) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;  // 空文件
      } else {
        LOG_WARN("read header failed", K(ret));
      }
    } else {
      header_skipped_ = true;
    }
  }
  return ret;
}

int ObFileScanOp::inner_rescan()
{
  int ret = OB_SUCCESS;
  reader_.close();
  is_opened_ = false;
  header_skipped_ = false;
  if (OB_FAIL(inner_open())) {
    LOG_WARN("rescan open failed", K(ret));
  } else if (OB_FAIL(ObOperator::inner_rescan())) {
    LOG_WARN("inner rescan failed", K(ret));
  }
  return ret;
}

int ObFileScanOp::fill_output(const ObIArray<ObString> &fields)
{
  int ret = OB_SUCCESS;
  const int64_t out_cnt = MY_SPEC.output_.count();
  for (int64_t i = 0; OB_SUCC(ret) && i < out_cnt; ++i) {
    ObExpr *expr = MY_SPEC.output_.at(i);
    ObDatum &datum = expr->locate_datum_for_write(eval_ctx_);
    ObString value;
    if (i < fields.count()) {
      value = fields.at(i);
    }
    if (0 == value.length()) {
      // CSV 空字段视作 NULL
      datum.set_null();
    } else {
      switch (expr->datum_meta_.type_) {
        case ObIntType:
        case ObInt32Type:
        case ObUInt64Type: {
          char buf[64];
          int64_t len = value.length();
          if (len >= static_cast<int64_t>(sizeof(buf))) {
            ret = OB_INVALID_DATA;
            LOG_WARN("integer field too long", K(ret), K(len));
          } else {
            MEMCPY(buf, value.ptr(), len);
            buf[len] = '\0';
            char *end = NULL;
            const int64_t v = strtoll(buf, &end, 10);
            if (NULL == end || '\0' != *end) {
              ret = OB_INVALID_DATA;
              LOG_WARN("invalid integer field", K(ret), K(value));
            } else {
              datum.set_int(v);
            }
          }
          break;
        }
        case ObDoubleType:
        case ObFloatType:
        case ObNumberType:
        case ObDecimalIntType: {
          char buf[128];
          int64_t len = value.length();
          if (len >= static_cast<int64_t>(sizeof(buf))) {
            ret = OB_INVALID_DATA;
            LOG_WARN("double field too long", K(ret), K(len));
          } else {
            MEMCPY(buf, value.ptr(), len);
            buf[len] = '\0';
            char *end = NULL;
            const double v = strtod(buf, &end);
            if (NULL == end || '\0' != *end) {
              ret = OB_INVALID_DATA;
              LOG_WARN("invalid double field", K(ret), K(value));
            } else {
              datum.set_double(v);
            }
          }
          break;
        }
        case ObTinyIntType: {
          // BOOLEAN 在 OB 内是 tinyint(1)
          bool bval = false;
          if (OB_FAIL(ObFileSqlUtils::parse_bool(value, bval))) {
            char buf[64];
            int64_t len = value.length();
            if (len < static_cast<int64_t>(sizeof(buf))) {
              MEMCPY(buf, value.ptr(), len);
              buf[len] = '\0';
              bval = (0 != strtoll(buf, NULL, 10));
              ret = OB_SUCCESS;
            }
          }
          if (OB_SUCC(ret)) {
            datum.set_bool(bval);
          }
          break;
        }
        case ObVarcharType:
        case ObCharType:
        case ObLongTextType:
        case ObMediumTextType:
        case ObTextType: {
          char *dst = static_cast<char *>(eval_ctx_.get_expr_res_alloc().alloc(value.length()));
          if (OB_ISNULL(dst)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("alloc string failed", K(ret), K(value.length()));
          } else {
            MEMCPY(dst, value.ptr(), value.length());
            datum.set_string(dst, value.length());
          }
          break;
        }
        default: {
          // 其余类型先按字符串输出（DATE/DATETIME 由下一增量补齐）
          char *dst = static_cast<char *>(eval_ctx_.get_expr_res_alloc().alloc(value.length()));
          if (OB_ISNULL(dst)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("alloc string failed", K(ret), K(value.length()));
          } else {
            MEMCPY(dst, value.ptr(), value.length());
            datum.set_string(dst, value.length());
          }
          break;
        }
      }
    }
    if (OB_SUCC(ret)) {
      expr->set_evaluated_projected(eval_ctx_);
    }
  }
  return ret;
}

int ObFileScanOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  fields_.reuse();
  if (OB_UNLIKELY(!is_opened_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("file scan op not opened", K(ret));
  } else if (OB_FAIL(reader_.next_row(fields_))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("read row failed", K(ret));
    }
  } else {
    clear_evaluated_flag();
    if (OB_FAIL(fill_output(fields_))) {
      LOG_WARN("fill output failed", K(ret));
    }
  }
  return ret;
}

int ObFileScanOp::inner_close()
{
  reader_.close();
  is_opened_ = false;
  header_skipped_ = false;
  return ObOperator::inner_close();
}

void ObFileScanOp::destroy()
{
  reader_.close();
  is_opened_ = false;
  ObOperator::destroy();
}

} // namespace sql
} // namespace oceanbase
