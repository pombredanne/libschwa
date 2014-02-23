/* -*- Mode: C++; indent-tabs-mode: nil -*- */
#include <schwa/drui/drui.h>

#include <cassert>
#include <iomanip>
#include <iostream>

#include <schwa/io/array_reader.h>
#include <schwa/pool.h>
#include <schwa/port.h>


namespace dr = schwa::dr;
namespace io = schwa::io;
namespace mp = schwa::msgpack;


namespace schwa {
namespace drui {

// ============================================================================
// FauxDoc
// ============================================================================
FauxDoc::Schema::Schema(void) : dr::Doc::Schema<FauxDoc>("FauxDoc", "The document class") { }


// ============================================================================
// FauxDocProcessor
// ============================================================================
const std::string FauxDocProcessor::SEP = std::string("\t") + port::DARK_GREY + "# ";


FauxDocProcessor::FauxDocProcessor(std::ostream &out) :
    _out(out),
    _doc(nullptr),
    _doc_num(0),
    _indent(0)
  { }


void
FauxDocProcessor::process_doc(const FauxDoc &doc, const unsigned int doc_num) {
  _doc = &doc;
  _doc_num = doc_num;
  _indent = 0;

  const dr::RTManager &rt = *_doc->rt();

  _write_indent() << port::BOLD << _doc_num << ":" << port::OFF << " {" << SEP << "Document" << port::OFF << "\n";
  ++_indent;

  const dr::RTSchema *schema = rt.doc;
  assert(schema != nullptr);
  // TODO document fields? where are they?
  //_process_fields(schema->fields);
  for (const auto *store : schema->stores)
    _process_store(*store);

  --_indent;
  _write_indent() << "},\n";
}


void
FauxDocProcessor::_process_fields(const std::vector<dr::RTFieldDef *> &fields) {
  for (const dr::RTFieldDef *field : fields) {
    (void)field;
  }
}


void
FauxDocProcessor::_process_store(const dr::RTStoreDef &store) {
  assert(store.is_lazy());
  const dr::RTSchema &klass = *store.klass;

  // iterate through each field name to find the largest name so we can align
  // all of the values when printing out.
  unsigned int max_length = 0;
  for (const auto& field : klass.fields)
    if (field->serial.size() > max_length)
      max_length = field->serial.size();

  // decode the lazy store values into dynamic msgpack objects
  Pool pool(4096);
  io::ArrayReader reader(store.lazy_data, store.lazy_nbytes);
  mp::Value *value = mp::read_dynamic(reader, pool);

  // <instances> ::= [ <instance> ]
  assert(is_array(value->type));
  const mp::Array &array = *value->via._array;

  // write header
  _write_indent() << port::BOLD << store.serial << ":" << port::OFF << " {";
  _out << SEP << klass.serial;
  _out << " (" << array.size() << ")"<< port::OFF << "\n";
  ++_indent;

  for (uint32_t i = 0; i != array.size(); ++i) {
    assert(is_map(array[i].type));
    const mp::Map &map = *array[i].via._map;

    _write_indent() << port::BOLD << "0x" << std::hex << i << std::dec << ":" << port::OFF << " {\n";
    ++_indent;


    // <instance> ::= { <field_id> : <obj_val> }
    for (uint32_t j = 0; j != map.size(); ++j) {
      assert(is_uint(map.get(j).key.type));
      const mp::Map::Pair &pair = map.get(j);
      const dr::RTFieldDef &field = *klass.fields[pair.key.via._uint64];

      _write_indent() << port::BOLD << std::setw(max_length) << std::left << field.serial << ": " << port::OFF;
      _write_field(store, field, pair.value) << "\n";
    }

    --_indent;
    _write_indent() << "},\n";
  }

  --_indent;
  _write_indent() << "},\n";
}


std::ostream &
FauxDocProcessor::_write_indent(void) {
  for (unsigned int i = 0; i != _indent; ++i)
    _out << "  ";
  return _out;
}


std::ostream &
FauxDocProcessor::_write_field(const dr::RTStoreDef &store, const dr::RTFieldDef &field, const mp::Value &value) {
  const auto flags = _out.flags();

  if (field.is_slice)
    _write_slice(field, value);
  else if (field.points_into != nullptr || field.is_self_pointer)
    _write_pointer(store, field, value);
  else
    _write_primitive(value);

  _out.flags(flags);
  return _out;
}


void
FauxDocProcessor::_write_slice(const dr::RTFieldDef &field, const mp::Value &value) {
  assert(is_array(value.type));
  const auto &array = *value.via._array;
  assert(array.size() == 2);
  assert(is_uint(array[0].type));
  assert(is_uint(array[1].type));

  const uint64_t a = array[0].via._uint64;
  const uint64_t b = array[1].via._uint64;
  _out << "[0x" << std::hex << a << ", 0x" << std::hex << (a + b) << "],";
  _out << SEP;
  if (field.points_into == nullptr)
    _out << "byte slice";
  else
    _out << "slice into " << field.points_into->serial;
  _out << " (" << std::dec << a << ", " << std::dec << (a + b) << ")" << port::OFF;
}


void
FauxDocProcessor::_write_pointer(const dr::RTStoreDef &store, const dr::RTFieldDef &field, const mp::Value &value) {
  if (field.is_collection) {
    assert(is_array(value.type));
    const mp::Array &array = *value.via._array;
    _out << "[";
    for (uint32_t i = 0; i != array.size(); ++i) {
      const mp::Value &v = array[i];
      if (i != 0)
        _out << ", ";
      if (is_nil(v.type) || is_uint(v.type)) {
        if (is_nil(v.type))
          _out << REPR_NIL;
        else
          _out << "0x" << std::hex << v.via._uint64;
      }
      else
        _out << port::RED << v.type << port::OFF;
    }
    _out << "]";
  }
  else {
    if (is_nil(value.type) || is_uint(value.type)) {
      if (is_nil(value.type))
        _out << REPR_NIL;
      else
        _out << "0x" << std::hex << value.via._uint64;
    }
    else
      _out << port::RED << value.type << port::OFF;
  }

  _out << SEP;
  if (field.is_self_pointer)
    _out << "self-pointer" << (field.is_collection ? "s" : "") << " into " << store.serial;
  else
    _out << "pointer" << (field.is_collection ? "s" : "") << " into " << field.points_into->serial;
  _out << port::OFF;
}


void
FauxDocProcessor::_write_primitive(const mp::Value &value) {
  if (is_bool(value.type)) {
    _out << std::boolalpha << value.via._bool << ",";
  }
  else if (is_double(value.type)) {
    _out << value.via._double << ",";
    _out << SEP << "double" << port::OFF;
  }
  else if (is_float(value.type)) {
    _out << value.via._float << ",";
    _out << SEP << "float" << port::OFF;
  }
  else if (is_nil(value.type)) {
    _out << REPR_NIL;
  }
  else if (is_sint(value.type)) {
    _out << "0x" << std::hex << value.via._int64 << std::dec << ",";
    _out << SEP << "int (" << value.via._int64 << ")" << port::OFF;
  }
  else if (is_uint(value.type)) {
    _out << "0x" << std::hex << value.via._uint64 << std::dec << ",";
    _out << SEP << "uint (" << value.via._uint64 << ")" << port::OFF;
  }
  else if (is_array(value.type)) {
    _out << port::RED << "TODO array" << port::OFF;
  }
  else if (is_map(value.type)) {
    _out << port::RED << "TODO map" << port::OFF;
  }
  else if (is_raw(value.type)) {
    const mp::Raw &raw = *value.via._raw;
    (_out << "\"").write(raw.value(), raw.size()) << "\",";
    _out << SEP << "raw (" << std::dec << raw.size() << "B)" << port::OFF;
  }
  else
    _out << port::RED << REPR_UNKNOWN << port::OFF;
}

}  // namespace drui
}  // namespace schwa
