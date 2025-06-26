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
 */

 #define USING_LOG_PREFIX  SQL_ENG
 #include "ob_expr_func_round_ties_to_even.h"
 #include "ob_expr_util.h"
 #include "ob_datum_cast.h"
 #include "objit/common/ob_item_type.h"

namespace oceanbase
{
namespace sql
{
#define GET_SCALE_FOR_CALC(scale) ((!lib::is_oracle_mode()) \
    ? (scale < 0 ? max(ROUND_MIN_SCALE, scale) : min(ROUND_MAX_SCALE, scale)) \
    : (scale < 0 ? max(OB_MIN_NUMBER_SCALE, scale) : min(OB_MAX_NUMBER_SCALE, scale)))

#define GET_SCALE_FOR_DEDUCE(scale) ((!lib::is_oracle_mode()) \
    ? (scale < 0 ? 0 : min(ROUND_MAX_SCALE, scale)) \
    : (scale < 0 ? max(OB_MIN_NUMBER_SCALE, scale) : min(OB_MAX_NUMBER_SCALE, scale)))

ObExprRoundTiesToEven::ObExprRoundTiesToEven(common::ObIAllocator &alloc) 
  : ObExprFuncRound(alloc, T_FUN_SYS_ROUND_TIES_TO_EVEN, N_ROUND_TIES_TO_EVEN)
{
}

ObExprRoundTiesToEven::~ObExprRoundTiesToEven()
{
}

int ObExprRoundTiesToEven::get_scale(const ObExpr &expr, const ObDatum *datum, int64_t &scale) 
{
  int ret = OB_SUCCESS;
    // get scale
  const ObObjType fmt_type = expr.args_[1]->datum_meta_.type_;
  if (datum->is_null()) {
    // do nothing
  } else if (ObNumberType == fmt_type) {
    const number::ObNumber fmt_nmb(datum->get_number());
    if (OB_FAIL(fmt_nmb.extract_valid_int64_with_trunc(scale))) {
      LOG_WARN("extract_valid_int64_with_trunc failed", K(ret), K(fmt_nmb));
    }
  } else if (ObIntType == fmt_type) {
    scale = datum->get_int();
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected fmt type", K(ret), K(fmt_type), K(expr));
  }
  return ret;
}

int ObExprRoundTiesToEven::round_uint_ties_to_even(int64_t scale, uint64_t& x) 
{
  int ret = OB_SUCCESS;
  if (scale < 0) {
    scale = -scale;
    // 20 is digit num of (UINT64_MAX)18446744073709551615
    if (scale >= 20) {
    	x = 0;
    } else {
      // x: 250, scale: 2, div: 100, tmp: 200
      uint64_t div = 1;
      for (int64_t i = 0; OB_SUCC(ret) && i < scale; ++i) {
        if (is_mul_out_of_range(div, static_cast<uint64_t>(10), div)) {
          ret = OB_DATA_OUT_OF_RANGE;
        }
      }
      if (div == 0) {
        ret = OB_ERR_UNEXPECTED;
      }
      if (OB_SUCC(ret)) {
        uint64_t tmp = x / div * div;
        uint64_t r = x - tmp;
        if ((r < div / 2) || (r == div / 2 && tmp / div % 2 == 0)) {
          // round down
          x = tmp;
        } else {
          // round up
          if (is_add_out_of_range(tmp, div, x)) {
            ret = OB_DATA_OUT_OF_RANGE;
          }
        }
      }
		}
	}
	return ret;
}

int ObExprRoundTiesToEven::round_int_ties_to_even(int64_t scale, int64_t& x_int)
{
  int ret = OB_SUCCESS;
  bool is_neg = x_int < 0;
  x_int = is_neg ? -x_int : x_int;
  uint64_t res_uint = static_cast<uint64_t>(x_int);
  ret = round_uint_ties_to_even(scale, res_uint);
  if (OB_SUCC(ret)) {
    if (res_uint <= static_cast<uint64_t>(INT64_MAX) + static_cast<uint64_t>(is_neg)) {
      x_int = static_cast<int64_t>(is_neg ? -res_uint : res_uint);
    } else {
      ret = OB_DATA_OUT_OF_RANGE;
    }
  }
  return ret;
}

template <typename T, typename ArgVec, typename ResVec, bool IsCheck>
class RoundTiesToEvenFunctor {
public:
  // using Func = std::function<int (const int64_t, T&)>;
  typedef int (*Func)(int64_t, T&);
  RoundTiesToEvenFunctor(Func round, const ObBitVector &skip, const EvalBound &bound,
               ObBitVector &eval_flags, const int64_t scale)
      : round_(round), skip_(skip), bound_(bound), eval_flags_(eval_flags),
        scale_(scale) {}

  OB_INLINE int64_t operator() (ArgVec *arg_vec, ResVec *res_vec) {
    int ret = OB_SUCCESS;
    if (!IsCheck) {
      for (int i = bound_.start(); OB_SUCC(ret) && i < bound_.end(); ++i) {
        ret = eval_scalar(arg_vec, i, res_vec);
      }
      eval_flags_.set_all(bound_.start(), bound_.end());
    } else {
      for (int i = bound_.start(); OB_SUCC(ret) && i < bound_.end(); ++i) {
        if (skip_.at(i) || eval_flags_.at(i)) {
          continue;
        } else if (OB_UNLIKELY(arg_vec->is_null(i))) {
          res_vec->set_null(i);
        } else {
          ret = eval_scalar(arg_vec, i, res_vec);
        }
        eval_flags_.set(i);
      }
    }
    return ret;
  }

private:
  OB_INLINE int64_t eval_scalar(ArgVec *arg_vec, int idx, ResVec *res_vec) {
    int ret = OB_SUCCESS;
    const char *in_ptr = arg_vec->get_payload(idx);
    T x = *reinterpret_cast<const T *>(in_ptr);
    ret = round_(scale_, x);
    if (OB_SUCC(ret)) {
      res_vec->set_payload(idx, &x, sizeof(T));
    }
    return ret;
  }


  Func round_;
  const ObBitVector &skip_;
  const EvalBound &bound_;
  ObBitVector &eval_flags_;
  int64_t scale_;
  int64_t scale_factor_;
};

int ObExprRoundTiesToEven::calc_round_decimalint(
    const ObDatumMeta &in_meta, const ObDatumMeta &out_meta, const int64_t round_scale,
    const ObDatum &in_datum, ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  if (in_meta.scale_ != round_scale
      || get_decimalint_type(in_meta.precision_) != get_decimalint_type(out_meta.precision_)) {
    ObDecimalIntBuilder res_val;
    if (OB_FAIL(do_round_decimalint(
        in_meta.precision_, in_meta.scale_, out_meta.precision_, out_meta.scale_, round_scale,
        in_datum, res_val))) {
      LOG_WARN("do_round_decimalint failed", K(ret), K(in_meta), K(out_meta), K(round_scale));
    } else {
      res_datum.set_decimal_int(res_val.get_decimal_int(), res_val.get_int_bytes());
    }
  } else {
    res_datum.set_decimal_int(in_datum.get_decimal_int(), in_datum.len_); // need deep copy
  }
  return ret;
}

int ObExprRoundTiesToEven::do_round_decimalint(
    const int16_t in_prec, const int16_t in_scale,
    const int16_t out_prec, const int16_t out_scale, const int64_t round_scale,
    const ObDatum &in_datum, ObDecimalIntBuilder &res_val)
{
  int ret = OB_SUCCESS;
  LOG_WARN("mywarn do_round_decimalint", K(in_scale), K(out_scale), K(round_scale));
  const ObDecimalInt *decint = in_datum.get_decimal_int();
  const int32_t int_bytes = in_datum.get_int_bytes();
  if (in_scale != round_scale || get_decimalint_type(in_prec) != get_decimalint_type(out_prec)) {
    ObDecimalIntBuilder scaled_down_val;
    ObDecimalIntBuilder scaled_up_val;
    int32_t expected_int_bytes = wide::ObDecimalIntConstValue::get_int_bytes_by_precision(out_prec);
    if (OB_FAIL(wide::common_scale_decimalint(
                decint, int_bytes, in_scale, round_scale, scaled_down_val))) {
      LOG_WARN("scale decimal int failed", K(ret), K(int_bytes), K(in_scale), K(round_scale));
    } else if ((round_scale < out_scale)
               && OB_FAIL(wide::common_scale_decimalint(scaled_down_val.get_decimal_int(),
                   int_bytes, round_scale, out_scale, scaled_up_val))) {
      LOG_WARN("scale decimal int failed", K(ret), K(int_bytes), K(in_scale),
               K(out_scale), K(round_scale));
    } else if (OB_FAIL(ObDatumCast::align_decint_precision_unsafe(
      round_scale < out_scale ? scaled_up_val.get_decimal_int() : scaled_down_val.get_decimal_int(),
      round_scale < out_scale ? scaled_up_val.get_int_bytes() : scaled_down_val.get_int_bytes(),
      expected_int_bytes, res_val))) {
      LOG_WARN("align_decint_precision_unsafe failed", K(ret),
          K(scaled_down_val.get_int_bytes()), K(scaled_up_val.get_int_bytes()),
          K(expected_int_bytes), K(in_scale), K(out_scale), K(round_scale));
    }
  } else {
    res_val.from(decint, int_bytes);
  }
  return ret;
}

int ObExprRoundTiesToEven::do_round_ties_to_even_by_type(
	const ObDatumMeta &in_meta, 
	const ObDatumMeta &out_meta, 
	const int64_t round_scale,
	const ObDatum &x_datum, 
	ObEvalCtx &ctx,
	ObDatum &res_datum) 
{
	int ret = OB_SUCCESS;
  const ObObjType &x_type = in_meta.get_type();
  UNUSED(ctx);
  LOG_WARN("mywarn", K(x_type), K(round_scale));
  switch (x_type) {
    case ObNumberType:
    case ObUNumberType: {
      // const number::ObNumber x_nmb(x_datum.get_number());
      // number::ObNumber res_nmb;
      // ObNumStackOnceAlloc tmp_alloc;
      // if (OB_FAIL(res_nmb.from(x_nmb, tmp_alloc))) {
      //   LOG_WARN("get num from x failed", K(ret), K(x_nmb));
      // } else if (OB_FAIL(res_nmb.round(GET_SCALE_FOR_CALC(round_scale)))) {
      //   LOG_WARN("eval round of res_nmb failed", K(ret), K(round_scale), K(res_nmb));
      // } else {
      //   res_datum.set_number(res_nmb);
      // }
      res_datum.set_number(x_datum.get_number());
      break;
    }
    case ObDecimalIntType: {
      if (OB_FAIL(ObExprFuncRound::calc_round_decimalint(
                  in_meta, out_meta, GET_SCALE_FOR_CALC(round_scale), x_datum, res_datum))) {
        LOG_WARN("calc_round_decimalint failed", K(ret), K(in_meta), K(out_meta), K(round_scale));
      }
      res_datum.set_decimal_int(x_datum.get_decimal_int());
      break;
    }
    case ObFloatType:
    case ObDoubleType: {
      // unsupport float number
      ret = OB_INVALID_DATA;
      break;
    }
    case ObIntType: {
      int64_t x_int = x_datum.get_int();
      ret = round_int_ties_to_even(round_scale, x_int);
      if (OB_SUCC(ret)) {
        res_datum.set_int(x_int);
      }
      break;
    }
    case ObUInt64Type: {
      uint64_t x_uint = x_datum.get_uint();
      ret = round_uint_ties_to_even(round_scale, x_uint);
      if (OB_SUCC(ret)) {
        res_datum.set_uint(x_uint);
      }
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected arg type", K(ret), K(x_type));
      break;
    }
  }
  return ret;
}


template <typename LeftVec, typename ResVec, bool IsCheck>
int ObExprRoundTiesToEven::do_round_by_type_vector(
                                    const int64_t scale, const ObExpr &expr,
                                    ObEvalCtx &ctx, const ObBitVector &skip,
                                    const EvalBound &bound)
{
  int ret = OB_SUCCESS;
  if (!IsCheck) { UNUSED(skip); }
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  ResVec *res_vec = static_cast<ResVec *>(expr.get_vector(ctx));
  LeftVec *left_vec = static_cast<LeftVec *>(expr.args_[0]->get_vector(ctx));
  const ObObjType x_type = expr.args_[0]->datum_meta_.type_;
  switch(x_type) {
    case ObNumberType:
    case ObUNumberType: {
      // TODO
      LOG_WARN("mywarn unimplmented");
      ret = OB_ERR_UNDEFINED;
      break;
    }
    case ObDecimalIntType: {
      // TODO
      LOG_WARN("mywarn unimplmented");
      ret = OB_ERR_UNDEFINED;
      break;
    }
    case ObFloatType:
    case ObDoubleType: {
        // unsupport float number
        ret = OB_INVALID_DATA;
        break;
    }
    case ObIntType:{
      RoundTiesToEvenFunctor<int64_t, LeftVec, ResVec, IsCheck> round_func(
            round_int_ties_to_even, skip, bound, eval_flags, scale);
      if (OB_FAIL(round_func(left_vec, res_vec))) {
        LOG_WARN("RoundFunctor<int64_t>::operator() failed", K(ret));
      }
      break;
    }
    case ObUInt64Type: {
        RoundTiesToEvenFunctor<uint64_t, LeftVec, ResVec, IsCheck> round_func(
            round_uint_ties_to_even, skip, bound, eval_flags, scale);
        if (OB_FAIL(round_func(left_vec, res_vec))) {
          LOG_WARN("RoundFunctor<uint64>::operator() failed", K(ret));
        }
        break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected arg type", K(ret), K(x_type));
      break;
    }
  }
  return ret;
}

template<VectorFormat FORMAT>
struct VectorFormatTraits {
  typedef ObVectorBase VectorFormatType;
};

template<>
struct VectorFormatTraits<VEC_FIXED> {
  typedef ObFixedLengthBase VectorFormatType;
};

template<>
struct VectorFormatTraits<VEC_DISCRETE> {
  typedef ObDiscreteFormat VectorFormatType;
};

template<>
struct VectorFormatTraits<VEC_CONTINUOUS> {
  typedef ObContinuousFormat VectorFormatType;
};

template<>
struct VectorFormatTraits<VEC_UNIFORM> {
  typedef ObUniformFormat<false> VectorFormatType;
};

template<>
struct VectorFormatTraits<VEC_UNIFORM_CONST> {
  typedef ObUniformFormat<true> VectorFormatType;
};

#define ROUND_DISPATCH_VECTOR_IN_LEFT_ARG_FORMAT(func_name, res_vec)            \
switch (left_format) {                                                          \
  case VEC_FIXED: {                                                             \
    ret = func_name<ObFixedLengthBase, res_vec>(expr, ctx, skip, bound);        \
    break;                                                                      \
  }                                                                             \
  case VEC_DISCRETE: {                                                          \
    ret = func_name<ObDiscreteFormat, res_vec>(expr, ctx, skip, bound);         \
    break;                                                                      \
  }                                                                             \
  case VEC_CONTINUOUS: {                                                        \
    ret = func_name<ObContinuousFormat, res_vec>(expr, ctx, skip, bound);       \
    break;                                                                      \
  }                                                                             \
  case VEC_UNIFORM: {                                                           \
    ret = func_name<ObUniformFormat<false>, res_vec>(expr, ctx, skip, bound);   \
    break;                                                                      \
  }                                                                             \
  case VEC_UNIFORM_CONST: {                                                     \
    ret = func_name<ObUniformFormat<true>, res_vec>(expr, ctx, skip, bound);    \
    break;                                                                      \
  }                                                                             \
  default: {                                                                    \
    ret = func_name<ObVectorBase, res_vec>(expr, ctx, skip, bound);             \
  }                                                                             \
}

#define ROUND_DISPATCH_VECTOR_IN_RES_ARG_FORMAT(func_name)                          \
switch (res_format) {                                                               \
  case VEC_FIXED: {                                                                 \
    ROUND_DISPATCH_VECTOR_IN_LEFT_ARG_FORMAT(func_name, ObFixedLengthBase);         \
    break;                                                                          \
  }                                                                                 \
  case VEC_DISCRETE: {                                                              \
    ROUND_DISPATCH_VECTOR_IN_LEFT_ARG_FORMAT(func_name, ObDiscreteFormat);          \
    break;                                                                          \
  }                                                                                 \
  case VEC_CONTINUOUS: {                                                            \
    ROUND_DISPATCH_VECTOR_IN_LEFT_ARG_FORMAT(func_name, ObContinuousFormat);        \
    break;                                                                          \
  }                                                                                 \
  case VEC_UNIFORM: {                                                               \
    ROUND_DISPATCH_VECTOR_IN_LEFT_ARG_FORMAT(func_name, ObUniformFormat<false>);    \
    break;                                                                          \
  }                                                                                 \
  case VEC_UNIFORM_CONST: {                                                         \
    ROUND_DISPATCH_VECTOR_IN_LEFT_ARG_FORMAT(func_name, ObUniformFormat<true>);     \
    break;                                                                          \
  }                                                                                 \
  default: {                                                                        \
    ROUND_DISPATCH_VECTOR_IN_LEFT_ARG_FORMAT(func_name, ObVectorBase);              \
  }                                                                                 \
}

template <typename LeftVec, typename ResVec>
int ObExprRoundTiesToEven::inner_calc_round_expr_numeric2_vector(const ObExpr &expr,
                            ObEvalCtx &ctx,
                            const ObBitVector &skip,
                            const EvalBound &bound)
{
  int ret = OB_SUCCESS;
  ObDatum *fmt_datum = NULL;
  int64_t scale = 0;
  if (expr.arg_cnt_ == 2) {
    if (OB_FAIL(expr.args_[1]->eval(ctx, fmt_datum))) {
      LOG_WARN("eval arg failed", K(ret), K(expr));
    } else if (OB_FAIL(get_scale(expr, fmt_datum, scale))) {
    } else if (fmt_datum->is_null()) {
      ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
      ResVec *res_vec = static_cast<ResVec *>(expr.get_vector(ctx));
      for (int64_t j = bound.start(); OB_SUCC(ret) && j < bound.end(); ++j) {
        eval_flags.set(j);
        res_vec->set_null(j);
      }
    } else if (is_mysql_mode()
               && (ob_is_number_tc(expr.args_[0]->datum_meta_.get_type())
               || ob_is_decimal_int_tc(expr.args_[0]->datum_meta_.get_type()))) {
      if (expr.args_[0]->datum_meta_.scale_ < scale
              // eg : select round(123.123, 100);
              //      -> result is 123.123
              || expr.datum_meta_.scale_ < scale) {
              // eg : select round(123.123456789123456789123456789123456789, 50);
              //      -> result accuracy is precision:34, scale:30 (max result scale is 30)
        scale = expr.datum_meta_.scale_;
      }
    }
  }

  if (OB_SUCC(ret)) {
    LeftVec *left_vec = static_cast<LeftVec *>(expr.args_[0]->get_vector(ctx));
    ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
    bool is_need_call_all = ObBitVector::bit_op_zero(skip, eval_flags, bound,
                                      [](uint64_t l, uint64_t r) { return l | r; });
    for (int64_t j = bound.start(); is_need_call_all && j < bound.end(); ++j) {
      is_need_call_all = !(left_vec->is_null(j));
    }
    if (is_need_call_all) {
      if (OB_FAIL((do_round_by_type_vector<LeftVec, ResVec, false>)(scale, expr, ctx, skip, bound))) {
        const ObObjType x_type = expr.args_[0]->datum_meta_.type_;
        LOG_WARN("calc round by type failed", K(ret), K(x_type), K(expr));
      }
    } else {
      if (OB_FAIL((do_round_by_type_vector<LeftVec, ResVec, true>)(scale, expr, ctx, skip, bound))) {
        const ObObjType x_type = expr.args_[0]->datum_meta_.type_;
        LOG_WARN("calc round by type failed", K(ret), K(x_type), K(expr));
      }
    }
  }
  return ret;
}


int ObExprRoundTiesToEven::calc_round_expr_numeric1(const sql::ObExpr &expr, sql::ObEvalCtx &ctx,
                              sql::ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *x_datum = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, x_datum))) {
    LOG_WARN("eval arg failed", K(ret), K(expr));
  } else if (x_datum->is_null()) {
    res_datum.set_null();
  } else if (OB_FAIL(do_round_ties_to_even_by_type(
              expr.args_[0]->datum_meta_, expr.datum_meta_, 0, *x_datum, ctx, res_datum))) {
    LOG_WARN("calc round by type failed",
        K(ret), K(expr.args_[0]->datum_meta_), K(expr.datum_meta_));
  }
  return ret;
}

int ObExprRoundTiesToEven::calc_round_expr_numeric2(const sql::ObExpr &expr, sql::ObEvalCtx &ctx,
                              sql::ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *x_datum = NULL;
  ObDatum *fmt_datum = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, x_datum)) ||
      OB_FAIL(expr.args_[1]->eval(ctx, fmt_datum))) {
    LOG_WARN("eval arg failed", K(ret), K(expr));
  } else if (x_datum->is_null() || fmt_datum->is_null()) {
    res_datum.set_null();
  } else {
    int64_t scale = 0;
    ret = get_scale(expr, fmt_datum, scale);
    if (OB_SUCC(ret)) {
      if (is_mysql_mode()
          && (ob_is_number_tc(expr.args_[0]->datum_meta_.get_type())
              || ob_is_decimal_int_tc(expr.args_[0]->datum_meta_.get_type()))) {
        if (expr.args_[0]->datum_meta_.scale_ < scale
            // eg : select round(123.123, 100);
            //      -> result is 123.123
            || expr.datum_meta_.scale_ < scale) {
            // eg : select round(123.123456789123456789123456789123456789, 50);
            //      -> result accuracy is precision:34, scale:30 (max result scale is 30)
          scale = expr.datum_meta_.scale_;
        }
      }
      if (OB_FAIL(do_round_ties_to_even_by_type(
                  expr.args_[0]->datum_meta_, expr.datum_meta_, scale, *x_datum, ctx, res_datum))) {
        LOG_WARN("calc round by type failed",
                 K(ret), K(expr.args_[0]->datum_meta_), K(expr.datum_meta_));
      }
    }
  }
  return ret;
}

int ObExprRoundTiesToEven::calc_round_expr_numeric2_batch(const ObExpr &expr,
                            ObEvalCtx &ctx,
                            const ObBitVector &skip,
                            const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  ObDatum *x_datum = NULL;
  ObDatum *fmt_datum = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, x_datum)) ||
      OB_FAIL(expr.args_[1]->eval(ctx, fmt_datum))) {
    LOG_WARN("eval arg failed", K(ret), K(expr));
  } else {
    x_datum->set_uint(1);
    LOG_WARN("mywarn calc num2 batch");
  }
  return ret;
}

int ObExprRoundTiesToEven::calc_round_expr_numeric2_vector(const ObExpr &expr,
                            ObEvalCtx &ctx,
                            const ObBitVector &skip,
                            const EvalBound &bound)
{
  int ret = OB_SUCCESS;
  LOG_WARN("mywarn calc num2 vector");
  if (OB_FAIL(expr.args_[0]->eval_vector(ctx, skip, bound))) {
    LOG_WARN("eval arg failed", K(ret), K(expr));
  } else {
    VectorFormat res_format = expr.get_format(ctx);
    VectorFormat left_format = expr.args_[0]->get_format(ctx);
    ROUND_DISPATCH_VECTOR_IN_RES_ARG_FORMAT(inner_calc_round_expr_numeric2_vector);
  }
  return ret;
}

int ObExprRoundTiesToEven::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const 
{
	int ret = OB_SUCCESS;
	if (rt_expr.arg_cnt_ < 1 || rt_expr.arg_cnt_ > 2) {
		ret = OB_ERR_UNEXPECTED;
		LOG_WARN("invalid arg cnt of expr", K(ret), K(rt_expr));
	} else {
		const ObObjType &x_type = rt_expr.args_[0]->datum_meta_.type_;
		const ObObjType &res_type = rt_expr.datum_meta_.type_;
		if (OB_UNLIKELY(x_type != res_type)) {
			ret = OB_ERR_UNEXPECTED;
			LOG_WARN("invalid arg type or res type", K(ret), K(x_type), K(res_type));
		} else if (2 == rt_expr.arg_cnt_) {
			const ObObjType fmt_type = rt_expr.args_[1]->datum_meta_.type_;
			if (is_oracle_mode()) {
				// if (ObDateTimeType == x_type && ObVarcharType == fmt_type) {
				// 	rt_expr.eval_func_ = calc_round_expr_datetime2;
				// 		// Only implement vectorization when parameter 0 is batch and parameter 1 is constant
				// 	if (rt_expr.args_[0]->is_batch_result() && !(rt_expr.args_[1]->is_batch_result())) {
				// 		rt_expr.eval_batch_func_ = calc_round_expr_datetime2_batch;
				// 		rt_expr.eval_vector_func_ = calc_round_expr_datetime2_vector;
				// 	}
				// } else {
				// 		rt_expr.eval_func_ = calc_round_expr_numeric2;
				// 		// Only implement vectorization when parameter 0 is batch and parameter 1 is constant
				// 		if (rt_expr.args_[0]->is_batch_result() && !(rt_expr.args_[1]->is_batch_result())) {
				// 			rt_expr.eval_batch_func_ = calc_round_expr_numeric2_batch;
				// 			rt_expr.eval_vector_func_ = calc_round_expr_numeric2_vector;
				// 		}
				// }
        LOG_WARN("mywarn unimplement");
			} else {
				rt_expr.eval_func_ = calc_round_expr_numeric2;
				// Only implement vectorization when parameter 0 is batch and parameter 1 is constant
				if (rt_expr.args_[0]->is_batch_result() && !(rt_expr.args_[1]->is_batch_result())) {
					rt_expr.eval_vector_func_ = calc_round_expr_numeric2_vector;
				}
			}
		} else {
			if (ObDateTimeType == x_type) {
				// rt_expr.eval_func_ = calc_round_expr_datetime1;
				// rt_expr.eval_batch_func_ = calc_round_expr_datetime1_batch;
				// rt_expr.eval_vector_func_ = calc_round_expr_datetime1_vector;
			} else {
				rt_expr.eval_func_ = calc_round_expr_numeric1;
				// rt_expr.eval_batch_func_ = calc_round_expr_numeric1_batch;
				rt_expr.eval_vector_func_ = calc_round_expr_numeric2_vector;
			}
      LOG_WARN("mywarn unimplement");
		}
    LOG_WARN("mywarn", K(rt_expr.arg_cnt_), K(x_type), K(res_type));
	}
	return ret;
}


} // namespace sql
} // namespace oceanbase