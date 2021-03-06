/* -*- Mode: C++; indent-tabs-mode: nil -*- */
/*
Below is the grammar definition for the query language. This ragel machine
defined in src/ragel/dr-query/language.rl performs the lexing and populates
the `_tokens' deque. A recursive descent parse over the tokens is performed
here, buildling up an expresssion tree as it goes along.

var ::= [_a-zA-Z][_a-zA-Z0-9]*
var_attribute ::= "." [_a-zA-Z][_a-zA-Z0-9]*

<e1> ::= <e2> (<op_boolean> <e1>)?
<e2> ::= <e2> (<op_comparison> <e3>)?
<e3> ::= <e3> (<op_numeric3> <e3>)?
<e4> ::= <e4> (<op_numeric4> <e4>)?
<e5> ::= function "(" (<e1> ("," <e1>)*)? ")"
       | var var_attribute?
       | "(" <e1> ")"
       | literal_int
       | literal_regex
       | literal_str

*/
#include <schwa/dr/query.h>

#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cstring>
#include <cstdio>
#include <iterator>
#include <regex>
#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>

#include <schwa/dr.h>
#include <schwa/msgpack.h>
#include <schwa/utils/enums.h>
#include <schwa/utils/hash.h>

namespace dr = schwa::dr;
namespace io = schwa::io;
namespace mp = schwa::msgpack;


namespace schwa {
namespace dr {
namespace query {

const char *
valuetype_name(const ValueType type) {
  switch (type) {
  case TYPE_ANN: return "ann";
  case TYPE_DOC: return "doc";
  case TYPE_INTEGER: return "integer";
  case TYPE_MISSING: return "missing";
  case TYPE_REGEX: return "regex";
  case TYPE_STORE: return "store";
  case TYPE_STRING: return "string";
  default: return "";
  }
}


// ============================================================================
// Exception helpers.
// ============================================================================
static void
check_accepts(const char *const msg, const ValueType type, const ValueType accepts_mask) {
  if ((type & accepts_mask) == 0) {
    std::ostringstream ss;
    ss << "Invalid input type to " << msg << ": found " << valuetype_name(type);
    throw RuntimeError(ss.str());
  }
}


static void
check_same_accepts(const char *const msg, const ValueType t1, const ValueType t2, const ValueType accepts_mask) {
  if ((t1 & accepts_mask) == 0) {
    std::ostringstream ss;
    ss << "Invalid left type to " << msg << ": found " << valuetype_name(t1);
    throw RuntimeError(ss.str());
  }
  if ((t2 & accepts_mask) == 0) {
    std::ostringstream ss;
    ss << "Invalid right type to " << msg << ": found " << valuetype_name(t2);
    throw RuntimeError(ss.str());
  }
  if (t1 != t2) {
    std::ostringstream ss;
    ss << "Left and right types to " << msg << " do not match. Found " << valuetype_name(t1) << " and " << valuetype_name(t2);
    throw RuntimeError(ss.str());
  }
}


static void
throw_compile_error(const char *const str, const size_t len) {
  std::ostringstream ss;
  ss << "Invalid input found at '";
  ss.write(str, len);
  ss << "'";
  throw CompileError(ss.str());
}


// ============================================================================
// Value object
// ============================================================================
bool
Value::to_bool(void) const {
  switch (type) {
  case TYPE_ANN: return true;
  case TYPE_DOC: return true;
  case TYPE_INTEGER: return via._int;
  case TYPE_MISSING: return false;
  case TYPE_REGEX: return true;
  case TYPE_STORE: return true;
  case TYPE_STRING: return std::strlen(via._str) != 0;
  default:
    assert(!"Should not get here.");
    return false;
  }
}


// ============================================================================
// Evaluation context data
// ============================================================================
class EvalContext {
private:
  const dr::Doc &_doc;
  Pool _pool;
  std::stack<const dr::RTStoreDef *> _stores;
  std::unordered_map<const char *, Value, cstr_hash, cstr_equal_to> _vars;

public:
  explicit EvalContext(const dr::Doc &doc) : _doc(doc), _pool(4 * 1024) { }

  inline const dr::Doc &doc(void) const { return _doc; }

  inline decltype(_vars)::mapped_type &
  get_var(decltype(_vars)::key_type key) {
    return _vars.find(key)->second;
  }

  inline bool
  has_var(decltype(_vars)::key_type key) const {
    return _vars.find(key) != _vars.end();
  }

  inline void
  set_var(decltype(_vars)::key_type key, decltype(_vars)::mapped_type value) {
    _vars.emplace(key, value);
  }

  inline void
  unset_var(decltype(_vars)::key_type key) {
    _vars.erase(key);
  }

  inline void pop_rtstore(void) { _stores.pop(); }
  inline void push_rtstore(const dr::RTStoreDef *store) { _stores.push(store); }
  inline const dr::RTStoreDef *top_rtstore(void) const { return _stores.top(); }

  char *
  create_str(const char *const orig, const size_t len) {
    char *str = _pool.alloc<char *>(len + 1);
    std::memcpy(str, orig, len);
    str[len] = '\0';
    return str;
  }

  char *
  create_concat_str(const char *const orig0, const char *const orig1) {
    const size_t len0 = std::strlen(orig0);
    const size_t len1 = std::strlen(orig1);
    char *str = _pool.alloc<char *>(len0 + len1 + 1);
    std::memcpy(str, orig0, len0);
    std::memcpy(str + len0, orig1, len1 + 1);
    return str;
  }

private:
  SCHWA_DISALLOW_COPY_AND_ASSIGN(EvalContext);
};


// ============================================================================
// Type casting helpers
// ============================================================================
static int64_t
cast_to_int(const char *const str) {
  char *end;
  const int64_t value = std::strtoll(str, &end, 10);
  if (*end != '\0') {
    std::ostringstream msg;
    msg << "Failed to convert string value '" << str << "' to an integer";
    throw RuntimeError(msg.str());
  }
  return value;
}


static char *
cast_to_str(const int64_t value, EvalContext &ctx) {
  std::ostringstream ss;
  ss << value;
  const std::string string = ss.str();
  return ctx.create_str(string.c_str(), string.size());
}


// ============================================================================
// Expression nodes
// ============================================================================
class Expr {
protected:
  const char *const _token;

  explicit Expr(const char *token) : _token(token) { }
public:
  virtual ~Expr(void) { }

  virtual Value eval(EvalContext &ctx) const = 0;
};


class VariableExpr : public Expr {
protected:
  const char *const _attribute;

  mutable const dr::RTManager *_rt;
  mutable const dr::RTStoreDef *_rtstore;

  Value
  _field_to_value(EvalContext &ctx, const dr::RTFieldDef &field, const mp::Value &value) const {
    if (field.is_slice || field.points_into != nullptr || field.is_self_pointer) {
      std::ostringstream msg;
      msg << "Field '" << _attribute << "' is of an invalid type";
      throw RuntimeError(msg.str());
    }
    else if (mp::is_bool(value.type))
      return Value::as_int(value.via._bool);
    else if (mp::is_sint(value.type))
      return Value::as_int(value.via._int64);
    else if (mp::is_uint(value.type))
      return Value::as_int(static_cast<int64_t>(value.via._uint64));
    else if (mp::is_raw(value.type)) {
      const mp::Raw &raw = *value.via._raw;
      char *const str = ctx.create_str(raw.value(), raw.size());
      return Value::as_str(str);
    }
    else {
      std::ostringstream msg;
      msg << "Field '" << _attribute << "' is of an invalid type";
      throw RuntimeError(msg.str());
    }
  }

  Value
  _eval_ann_attribute(EvalContext &ctx, const Value &v) const {
    const mp::Map &map = *v.via._map;
    const dr::RTStoreDef *const rtstore = ctx.top_rtstore();

    // <instance> ::= { <field_id> : <obj_val> }
    for (uint32_t j = 0; j != map.size(); ++j) {
      assert(mp::is_uint(map.get(j).key.type));
      const mp::Map::Pair &pair = map.get(j);
      const dr::RTFieldDef &field = *rtstore->klass->fields[pair.key.via._uint64];
      if (field.serial == _attribute)
        return _field_to_value(ctx, field, pair.value);
    }

    return Value::as_missing(this);
  }

  Value
  _eval_doc_attribute(EvalContext &ctx) const {
    const dr::RTSchema *rtdschema = _rt->doc;

    // Decode the lazy document values into dynamic msgpack objects.
    Pool pool(4096);
    io::ArrayReader reader(rtdschema->lazy_data, rtdschema->lazy_nbytes);
    mp::Value *value = mp::read_dynamic(reader, pool);
    assert(value != nullptr);
    assert(mp::is_map(value->type));
    const mp::Map &map = *value->via._map;

    // <instance> ::= { <field_id> : <obj_val> }
    for (uint32_t j = 0; j != map.size(); ++j) {
      assert(mp::is_uint(map.get(j).key.type));
      const mp::Map::Pair &pair = map.get(j);
      const dr::RTFieldDef &field = *rtdschema->fields[pair.key.via._uint64];
      if (field.serial == _attribute)
        return _field_to_value(ctx, field, pair.value);
    }

    for (const dr::RTStoreDef *store : rtdschema->stores) {
      if (store->serial == _attribute) {
        this->_rtstore = store;
        return Value::as_store(this);
      }
    }

    return Value::as_missing(this);
  }

  bool
  _store_iter(EvalContext &ctx, const Expr *const expr, const bool is_any) const {
    assert(_rtstore != nullptr);

    // Decode the lazy store values into dynamic msgpack objects.
    Pool pool(4096);
    io::ArrayReader reader(_rtstore->lazy_data, _rtstore->lazy_nbytes);
    mp::Value *value = mp::read_dynamic(reader, pool);

    // <instances> ::= [ <instance> ]
    assert(is_array(value->type));
    const mp::Array &array = *value->via._array;
    for (uint32_t i = 0; i != array.size(); ++i) {
      assert(is_map(array[i].type));
      const mp::Map &map = *array[i].via._map;

      ctx.set_var("ann", Value::as_ann(&map));
      ctx.push_rtstore(_rtstore);
      const Value v = expr->eval(ctx);
      ctx.pop_rtstore();
      ctx.unset_var("ann");

      if (is_any && v.to_bool())
        return true;
      else if (!is_any && !v.to_bool())
        return false;
    }

    return is_any ? false : true;
  }

public:
  explicit VariableExpr(const char *token, const char *attribute=nullptr) : Expr(token), _attribute(attribute), _rtstore(nullptr) { }
  virtual ~VariableExpr(void) { }

  inline uint32_t store_nelem(void) const { return _rtstore->lazy_nelem; }

  virtual Value
  eval(EvalContext &ctx) const override {
    // Ensure the variable exists.
    if (!ctx.has_var(_token)) {
      std::ostringstream msg;
      msg << "Variable '" << _token << "' does not exist";
      throw RuntimeError(msg.str());
    }

    // If we don't require an attribute, return the raw value of the variable.
    const Value v = ctx.get_var(_token);
    if (_attribute == nullptr)
      return v;

    // If we have an attribute, ensure the variable is an ann or a doc.
    if (!(v.type == TYPE_ANN || v.type == TYPE_DOC)) {
      std::ostringstream msg;
      msg << "Invalid access to attribute '" << _attribute << "' of variable '" << _token << "' of type " << valuetype_name(v.type);
      throw RuntimeError(msg.str());
    }

    // Create the runtime support and try the attribute lookup.
    _rt = ctx.doc().rt();
    if (v.type == TYPE_ANN)
      return _eval_ann_attribute(ctx, v);
    else
      return _eval_doc_attribute(ctx);
  }

  inline bool
  store_all(EvalContext &ctx, const Expr *const expr) const {
    return _store_iter(ctx, expr, false);
  }

  inline bool
  store_any(EvalContext &ctx, const Expr *const expr) const {
    return _store_iter(ctx, expr, true);
  }
};


class BinaryOperatorExpr : public Expr {
protected:
  Expr *const _left;
  Expr *const _right;

public:
  BinaryOperatorExpr(const char *token, Expr *left, Expr *right) :
      Expr(token),
      _left(left),
      _right(right) { }
  virtual ~BinaryOperatorExpr(void) { }

  virtual Value
  eval(EvalContext &ctx) const override {
    const Value v1 = _left->eval(ctx);
    const Value v2 = _right->eval(ctx);
    if (std::strcmp(_token, "&&") == 0) {
      check_same_accepts(_token, v1.type, v2.type, TYPE_INTEGER);
      return Value::as_int(v1.via._int && v2.via._int);
    }
    else if (std::strcmp(_token, "||") == 0) {
      check_same_accepts(_token, v1.type, v2.type, TYPE_INTEGER);
      return Value::as_int(v1.via._int || v2.via._int);
    }
    else if (std::strcmp(_token, "==") == 0) {
      check_same_accepts(_token, v1.type, v2.type, TYPE_INTEGER | TYPE_STRING);
      if (v1.type == TYPE_INTEGER)
        return Value::as_int(v1.via._int == v2.via._int);
      else
        return Value::as_int(std::strcmp(v1.via._str, v2.via._str) == 0);
    }
    else if (std::strcmp(_token, "!=") == 0) {
      check_same_accepts(_token, v1.type, v2.type, TYPE_INTEGER | TYPE_STRING);
      if (v1.type == TYPE_INTEGER)
        return Value::as_int(v1.via._int != v2.via._int);
      else
        return Value::as_int(std::strcmp(v1.via._str, v2.via._str) != 0);
    }
    else if (std::strcmp(_token, "<=") == 0) {
      check_same_accepts(_token, v1.type, v2.type, TYPE_INTEGER);
      return Value::as_int(v1.via._int <= v2.via._int);
    }
    else if (std::strcmp(_token, "<") == 0) {
      check_same_accepts(_token, v1.type, v2.type, TYPE_INTEGER);
      return Value::as_int(v1.via._int < v2.via._int);
    }
    else if (std::strcmp(_token, ">=") == 0) {
      check_same_accepts(_token, v1.type, v2.type, TYPE_INTEGER);
      return Value::as_int(v1.via._int >= v2.via._int);
    }
    else if (std::strcmp(_token, ">") == 0) {
      check_same_accepts(_token, v1.type, v2.type, TYPE_INTEGER);
      return Value::as_int(v1.via._int > v2.via._int);
    }
    else if (std::strcmp(_token, "~=") == 0) {
      check_accepts("left side of ~=", v1.type, TYPE_STRING);
      check_accepts("right side of ~=", v2.type, TYPE_REGEX);
      std::cmatch m;
      const bool found = std::regex_search(v1.via._str, m, *v2.via._re);
      return Value::as_int(found && static_cast<size_t>(m.length()) == std::strlen(v1.via._str));
    }
    else if (std::strcmp(_token, "~") == 0) {
      check_accepts("left side of ~", v1.type, TYPE_STRING);
      check_accepts("right side of ~", v2.type, TYPE_REGEX);
      return Value::as_int(std::regex_search(v1.via._str, *v2.via._re));
    }
    else if (std::strcmp(_token, "+") == 0) {
      check_same_accepts(_token, v1.type, v2.type, TYPE_INTEGER | TYPE_STRING);
      if (v1.type == TYPE_INTEGER)
        return Value::as_int(v1.via._int + v2.via._int);
      else {
        char *const str = ctx.create_concat_str(v1.via._str, v2.via._str);
        return Value::as_str(str);
      }
    }
    else if (std::strcmp(_token, "-") == 0) {
      check_same_accepts(_token, v1.type, v2.type, TYPE_INTEGER);
      return Value::as_int(v1.via._int - v2.via._int);
    }
    else if (std::strcmp(_token, "%") == 0) {
      check_same_accepts(_token, v1.type, v2.type, TYPE_INTEGER);
      return Value::as_int(v1.via._int % v2.via._int);
    }
    else if (std::strcmp(_token, "*") == 0) {
      check_same_accepts(_token, v1.type, v2.type, TYPE_INTEGER);
      return Value::as_int(v1.via._int * v2.via._int);
    }
    else if (std::strcmp(_token, "/") == 0) {
      check_same_accepts(_token, v1.type, v2.type, TYPE_INTEGER);
      return Value::as_int(v1.via._int / v2.via._int);
    }
    else {
      assert(!"Should never get here");
      return Value::as_int(0);
    }
  }
};


class FunctionExpr : public Expr {
protected:
  std::vector<Expr *, PoolAllocator<Expr *>> _args;

  void
  _check_arity(const decltype(_args)::size_type arity) const {
    if (_args.size() != arity) {
      std::ostringstream ss;
      ss << "Incorrect number of arguments to '" << _token << "'. Found " << _args.size() << " but expected " << arity;
      throw RuntimeError(ss.str());
    }
  }

public:
  FunctionExpr(const char *token, Pool &pool, const std::vector<Expr *> &args) : Expr(token), _args(PoolAllocator<Expr *>(pool)) {
    std::copy(args.begin(), args.end(), std::back_inserter(_args));
  }
  virtual ~FunctionExpr(void) { }

  virtual Value
  eval(EvalContext &ctx) const override {
    if (std::strcmp(_token, "all") == 0) {
      _check_arity(2);
      const Value v = _args[0]->eval(ctx);
      check_accepts("arg0 of 'all'", v.type, TYPE_MISSING | TYPE_STORE);
      if (v.type == TYPE_MISSING)
        return Value::as_int(false);
      const bool all = v.via._variable->store_all(ctx, _args[1]);
      return Value::as_int(all);
    }
    if (std::strcmp(_token, "any") == 0) {
      _check_arity(2);
      const Value v = _args[0]->eval(ctx);
      check_accepts("arg0 of 'any'", v.type, TYPE_MISSING | TYPE_STORE);
      if (v.type == TYPE_MISSING)
        return Value::as_int(false);
      const bool any = v.via._variable->store_any(ctx, _args[1]);
      return Value::as_int(any);
    }
    else if (std::strcmp(_token, "int") == 0) {
      _check_arity(1);
      const Value v = _args[0]->eval(ctx);
      check_accepts("arg0 of 'int'", v.type, TYPE_INTEGER | TYPE_MISSING | TYPE_STRING);
      switch (v.type) {
      case TYPE_INTEGER: return v;
      case TYPE_MISSING: return Value::as_int(0);
      case TYPE_STRING: return Value::as_int(cast_to_int(v.via._str));
      }
    }
    else if (std::strcmp(_token, "len") == 0) {
      _check_arity(1);
      const Value v = _args[0]->eval(ctx);
      check_accepts("arg0 of 'len'", v.type, TYPE_MISSING | TYPE_STRING | TYPE_STORE);
      switch (v.type) {
      case TYPE_MISSING: return Value::as_int(0);
      case TYPE_STRING: return Value::as_int(std::strlen(v.via._str));
      case TYPE_STORE: return Value::as_int(v.via._variable->store_nelem());
      }
    }
    else if (std::strcmp(_token, "str") == 0) {
      _check_arity(1);
      const Value v = _args[0]->eval(ctx);
      check_accepts("arg0 of 'str'", v.type, TYPE_INTEGER | TYPE_MISSING | TYPE_STRING);
      switch (v.type) {
      case TYPE_INTEGER: return Value::as_str(cast_to_str(v.via._int, ctx));
      case TYPE_MISSING: return Value::as_str("");
      case TYPE_STRING: return v;
      }
    }
    assert(!"Should never get here");
    return Value::as_int(0);
  }
};


class LiteralIntegerExpr : public Expr {
protected:
  int64_t _value;

public:
  explicit LiteralIntegerExpr(const char *token) : Expr(token) {
    const int ret = std::sscanf(_token, "%" SCNd64, &_value);
    assert(ret == 1);
  }
  virtual ~LiteralIntegerExpr(void) { }

  virtual Value
  eval(EvalContext &) const override {
    return Value::as_int(_value);
  }
};


class LiteralRegexExpr : public Expr {
protected:
  std::regex _re;

public:
  explicit LiteralRegexExpr(const char *token) : Expr(token) {
    try {
      _re = std::regex(_token, std::regex::nosubs | std::regex::ECMAScript);
    }
    catch (std::regex_error &e) {
      throw CompileError(e.what());
    }
  }
  virtual ~LiteralRegexExpr(void) { }

  virtual Value
  eval(EvalContext &) const override {
    return Value::as_re(&_re);
  }
};


class LiteralStringExpr : public Expr {
public:
  explicit LiteralStringExpr(const char *token) : Expr(token) { }
  virtual ~LiteralStringExpr(void) { }

  virtual Value
  eval(EvalContext &) const override {
    return Value::as_str(_token);
  }
};


// ============================================================================
// Interpreter
// ============================================================================
Interpreter::Interpreter(void) : _pool(4 * 1024), _expr(nullptr) { }

Interpreter::~Interpreter(void) {
  delete _expr;
}


Expr *
Interpreter::_parse_e1(void) {
  Expr *left = _parse_e2();
  if (!_tokens.empty() && _tokens.front().first == TokenType::OP_BOOLEAN) {
    decltype(_tokens)::value_type pair = _tokens.front();
    _tokens.pop_front();
    Expr *right = _parse_e1();
    left = new (_pool) BinaryOperatorExpr(pair.second, left, right);
  }
  return left;
}


Expr *
Interpreter::_parse_e2(void) {
  Expr *left = _parse_e3();
  if (!_tokens.empty() && _tokens.front().first == TokenType::OP_COMPARISON) {
    decltype(_tokens)::value_type pair = _tokens.front();
    _tokens.pop_front();
    Expr *right = _parse_e3();
    left = new (_pool) BinaryOperatorExpr(pair.second, left, right);
  }
  return left;
}


Expr *
Interpreter::_parse_e3(void) {
  Expr *left = _parse_e4();
  if (!_tokens.empty() && _tokens.front().first == TokenType::OP_NUMERIC3) {
    decltype(_tokens)::value_type pair = _tokens.front();
    _tokens.pop_front();
    Expr *right = _parse_e3();
    left = new (_pool) BinaryOperatorExpr(pair.second, left, right);
  }
  return left;
}


Expr *
Interpreter::_parse_e4(void) {
  Expr *left = _parse_e5();
  if (!_tokens.empty() && _tokens.front().first == TokenType::OP_NUMERIC4) {
    decltype(_tokens)::value_type pair = _tokens.front();
    _tokens.pop_front();
    Expr *right = _parse_e4();
    left = new (_pool) BinaryOperatorExpr(pair.second, left, right);
  }
  return left;
}


Expr *
Interpreter::_parse_e5(void) {
  if (_tokens.empty())
    throw CompileError("Expected <e5> but no more tokens available");

  const decltype(_tokens)::value_type pair = _tokens.front();
  decltype(_tokens)::value_type tmp_pair;
  _tokens.pop_front();

  Expr *expr = nullptr;
  switch (pair.first) {
  case TokenType::OPEN_PAREN:
    {
      expr = _parse_e1();
      if (_tokens.empty())
        throw CompileError("Expected CLOSE_PAREN in <e5> but no more tokens available");
      tmp_pair = _tokens.front();
      _tokens.pop_front();
      if (tmp_pair.first != TokenType::CLOSE_PAREN) {
        std::ostringstream msg;
        msg << "Expected CLOSE_PAREN in <e5> but found token type " << to_underlying(tmp_pair.first) << " instead";
        throw CompileError(msg.str());
      }
    }
    break;

  case TokenType::LITERAL_INTEGER:
    expr = new (_pool) LiteralIntegerExpr(pair.second);
    break;

  case TokenType::LITERAL_REGEX:
    expr = new (_pool) LiteralRegexExpr(pair.second);
    break;

  case TokenType::LITERAL_STRING:
    expr = new (_pool) LiteralStringExpr(pair.second);
    break;

  case TokenType::VAR:
    {
      if (!_tokens.empty() && _tokens.front().first == TokenType::VAR_ATTRIBUTE) {
        tmp_pair = _tokens.front();
        _tokens.pop_front();
        expr = new (_pool) VariableExpr(pair.second, tmp_pair.second);
      }
      else
        expr = new (_pool) VariableExpr(pair.second);
    }
    break;

  case TokenType::FUNCTION:
    {
      std::vector<Expr *> args;
      // "("
      if (_tokens.empty())
        throw CompileError("Expected OPEN_PAREN in <e5> function but no more tokens available");
      tmp_pair = _tokens.front();
      _tokens.pop_front();
      if (tmp_pair.first != TokenType::OPEN_PAREN) {
        std::ostringstream msg;
        msg << "Expected OPEN_PAREN in <e5> function but found token type " << to_underlying(tmp_pair.first) << " instead";
        throw CompileError(msg.str());
      }
      // Loop for each argument.
      while (true) {
        // ")"
        if (_tokens.empty())
          throw CompileError("Expected CLOSE_PAREN in <e5> function but no more tokens available");
        tmp_pair = _tokens.front();
        if (tmp_pair.first == TokenType::CLOSE_PAREN) {
          _tokens.pop_front();
          break;
        }
        // ","?
        if (!args.empty()) {
          if (_tokens.empty())
            throw CompileError("Expected COMMA in <e5> function but no more tokens available");
          tmp_pair = _tokens.front();
          _tokens.pop_front();
          if (tmp_pair.first != TokenType::COMMA) {
            std::ostringstream msg;
            msg << "Expected COMMA in <e5> function but found token type " << to_underlying(tmp_pair.first) << " instead";
            throw CompileError(msg.str());
          }
        }
        // <e1>
        Expr *arg = _parse_e1();
        args.push_back(arg);
      }
      expr = new (_pool) FunctionExpr(pair.second, _pool, args);
    }
    break;

  default:
    {
      _tokens.push_front(pair);
      std::ostringstream msg;
      msg << "Expected <e5> token but found token type " << to_underlying(pair.first) << " instead (";
      while (!_tokens.empty()) {
        msg << " '" << _tokens.front().second << "'";
        _tokens.pop_front();
      }
      msg << ")";
      throw CompileError(msg.str());
    }
  }
  return expr;
}


void
Interpreter::_parse(void) {
  if (_tokens.empty())
    throw CompileError("Query string cannot be empty");

  // Parse and consume the tokens.
  _expr = _parse_e1();

  // Ensure there are no more tokens left to consume.
  if (!_tokens.empty()) {
    const auto &pair = _tokens.front();
    throw_compile_error(pair.second, std::strlen(pair.second));
  }
}


void
Interpreter::_push_token(const TokenType type, const char *const ts, const char *const te) {
  const size_t len = te - ts;
  char *const str = _pool.alloc<char *>(len + 1);
  std::memcpy(str, ts, len);
  str[len] = '\0';
  _tokens.push_back(decltype(_tokens)::value_type(type, str));
}


void
Interpreter::_throw_compile_error(const char *const p, const char *const pe) {
  throw_compile_error(p, pe - p);
}


void
Interpreter::compile(const char *const str, const size_t len) {
  _tokenise(str, len);
  _parse();
}


Value
Interpreter::eval(const dr::Doc &doc, const uint32_t doc_num) const {
  EvalContext ctx(doc);
  ctx.set_var("doc", Value::as_doc(&doc));
  ctx.set_var("index", Value::as_int(static_cast<int64_t>(doc_num)));

  return _expr->eval(ctx);
}

}  // namespace query
}  // namesapce dr
}  // namespace schwa
