/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 * This file contains implementation for eval_priv_st_equals.
 */

#ifndef OCEANBASE_SQL_ENGINE_OB_SQL_EXPR_FUNC_ROUND_TIES_TO_EVEN_
#define OCEANBASE_SQL_ENGINE_OB_SQL_EXPR_FUNC_ROUND_TIES_TO_EVEN_
#include "sql/engine/expr/ob_expr_func_round.h"

namespace oceanbase 
{
namespace sql
{
class ObExprRoundTiesToEven : public ObExprFuncRound 
{
public:
  explicit ObExprRoundTiesToEven(common::ObIAllocator &alloc);
  virtual ~ObExprRoundTiesToEven();
    

  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

  virtual bool need_rt_ctx() const override { return true; }

  static int calc_round_expr_numeric1(const sql::ObExpr &expr, sql::ObEvalCtx &ctx,
                              sql::ObDatum &res_datum);

  static int calc_round_expr_numeric2(const sql::ObExpr &expr, sql::ObEvalCtx &ctx,
                               sql::ObDatum &res_datum);

  static int calc_round_expr_numeric2_batch(const ObExpr &expr,
                            ObEvalCtx &ctx,
                            const ObBitVector &skip,
                            const int64_t batch_size);

  static int calc_round_expr_numeric2_vector(const ObExpr &expr,
                            ObEvalCtx &ctx,
                            const ObBitVector &skip,
                            const EvalBound &bound);

private:
  template<typename T1, typename T2, typename T3>
  OB_INLINE static bool is_mul_out_of_range(T1 val1, T2 val2, T3 &res)
  {
    return __builtin_mul_overflow(val1, val2, &res);
  }

  template<typename T1, typename T2, typename T3>
  OB_INLINE static bool is_add_out_of_range(T1 val1, T2 val2, T3 &res)
  {
    return __builtin_add_overflow(val1, val2, &res);
  }

  static int get_scale(const ObExpr &expr, const ObDatum *datum, int64_t &scale);

  static int round_uint_ties_to_even(int64_t scale, uint64_t& x);

  static int round_int_ties_to_even(int64_t scale, int64_t& x_int);

  static int calc_round_decimalint(
    const ObDatumMeta &in_meta, const ObDatumMeta &out_meta, const int64_t round_scale,
    const ObDatum &in_datum, ObDatum &res_datum);

  static int do_round_decimalint(
    const int16_t in_prec, const int16_t in_scale,
    const int16_t out_prec, const int16_t out_scale, const int64_t round_scale,
    const ObDatum &in_datum, ObDecimalIntBuilder &res_val);

  static int do_round_ties_to_even_by_type(
    const ObDatumMeta &in_meta, 
    const ObDatumMeta &out_meta, 
    const int64_t round_scale,
    const ObDatum &x_datum, 
    ObEvalCtx &ctx,
    ObDatum &res_datum
  );

  template <typename LeftVec, typename ResVec, bool IsCheck>
  static int do_round_by_type_vector(const int64_t scale, const ObExpr &expr,
                                     ObEvalCtx &ctx, const ObBitVector &skip,
                                     const EvalBound &bound);

  template <typename LeftVec, typename ResVec>
  static int inner_calc_round_expr_numeric2_vector(const ObExpr &expr,
                             ObEvalCtx &ctx,
                             const ObBitVector &skip,
                             const EvalBound &bound);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_OB_SQL_EXPR_FUNC_ROUND_TIES_TO_EVEN_