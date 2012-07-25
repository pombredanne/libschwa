/* -*- Mode: C++; indent-tabs-mode: nil -*- */
#include <schwa/dr.h>

namespace mp = schwa::msgpack;


namespace schwa { namespace dr {

Reader &
Reader::read(Doc &doc) {
  if (_in.peek() == EOF || _in.eof()) {
    _has_more = false;
    return *this;
  }

  // construct the lazy runtime manager for the document
  assert(doc._rt == nullptr);
  doc._rt = new RTManager();
  assert(doc._rt != nullptr);
  RTManager &rt = *doc._rt;

  // map of each of the registered types
  std::map<std::string, const BaseSchema *> klass_name_map;
  klass_name_map["__meta__"] = &_dschema;
  for (auto &s : _dschema.schemas())
    klass_name_map[s->serial] = s;

  // keep track of the klass_id of __meta__
  bool found_klass_id_meta = false;
  uint32_t klass_id_meta;

  // read the klasses header
  // <klasses> ::= [ <klass> ]
  const uint32_t nklasses = mp::read_array_size(_in);

  for (uint32_t k = 0; k != nklasses; ++k) {
    // <klass> ::= ( <klass_name>, <fields> )
    const uint32_t npair = mp::read_array_size(_in);
    if (npair != 2) {
      std::stringstream msg;
      msg << "Invalid sized tuple read in: expected 2 elements but found " << npair;
      throw ReaderException(msg.str());
    }

    // read in the class name and check that we have a registered class with this name
    RTSchema *rtschema;
    const std::string klass_name = mp::read_raw(_in);
    {
      const auto &kit = klass_name_map.find(klass_name);
      if (kit == klass_name_map.end())
        rtschema = new RTSchema(k, klass_name);
      else
        rtschema = new RTSchema(k, klass_name, kit->second);
      assert(rtschema != nullptr);
    }
    rt.klasses.push_back(rtschema);

    // keep track of the klass_id of __meta__
    if (klass_name == "__meta__") {
      found_klass_id_meta = true;
      klass_id_meta = k;
    }

    // <fields> ::= [ <field> ]
    const uint32_t nfields = mp::read_array_size(_in);

    for (uint32_t f = 0; f != nfields; ++f) {
      std::string field_name;
      size_t store_id = 0;
      bool is_pointer = false, is_slice = false;

      // <field> ::= { <field_type> : <field_val> }
      const uint32_t nitems = mp::read_map_size(_in);
      for (uint32_t i = 0; i != nitems; ++i) {
        const uint8_t key = mp::read_uint_fixed(_in);
        switch (key) {
        case 0: field_name = mp::read_raw(_in); break;
        case 1: store_id = mp::read_uint(_in); is_pointer = true; break;
        case 2: is_slice = mp::read_bool(_in); break;
        default:
          std::stringstream msg;
          msg << "Unknown value " << static_cast<unsigned int>(key) << " as key in <field> map";
          throw ReaderException(msg.str());
        }
      }
      if (field_name.empty()) {
        std::stringstream msg;
        msg << "Field number " << (f + 1) << " did not contain a NAME key";
        throw ReaderException(msg.str());
      }

      // see if the read in field exists on the registered class's schema
      RTFieldDef *rtfield;
      if (rtschema->is_lazy()) {
        rtfield = new RTFieldDef(f, field_name, reinterpret_cast<const RTStoreDef *>(store_id), is_slice);
        assert(rtfield != nullptr);
      }
      else {
        // try and find the field on the registered class
        const BaseFieldDef *def = nullptr;
        for (auto &f : rtschema->def->fields()) {
          if (f->serial == field_name) {
            def = f;
            break;
          }
        }
        rtfield = new RTFieldDef(f, field_name, reinterpret_cast<const RTStoreDef *>(store_id), is_slice, def);
        assert(rtfield != nullptr);

        // perform some sanity checks that the type of data on the stream is what we're expecting
        if (def != nullptr) {
          if (is_pointer != def->is_pointer) {
            std::stringstream msg;
            msg << "Field '" << field_name << "' of class '" << klass_name << "' has IS_POINTER as " << is_pointer << " on the stream, but " << def->is_pointer << " on the class's field";
            throw ReaderException(msg.str());
          }
          if (is_slice != def->is_slice) {
            std::stringstream msg;
            msg << "Field '" << field_name << "' of class '" << klass_name << "' has IS_SLICE as " << is_slice << " on the stream, but " << def->is_slice << " on the class's field";
            throw ReaderException(msg.str());
          }
        }
      }
      rtschema->fields.push_back(rtfield);
    } // for each field
  } // for each klass

  if (!found_klass_id_meta)
    throw ReaderException("Did not read in a __meta__ class");
  rt.doc = rt.klasses[klass_id_meta];

  // read the stores header
  // <stores> ::= [ <store> ]
  const uint32_t nstores = mp::read_array_size(_in);

  for (uint32_t n = 0; n != nstores; ++n) {
    // <store> ::= ( <store_name>, <klass_id>, <store_nelem> )
    const uint32_t ntriple = mp::read_array_size(_in);
    if (ntriple != 3) {
      std::stringstream msg;
      msg << "Invalid sized tuple read in: expected 3 elements but found " << ntriple;
      throw ReaderException(msg.str());
    }
    const std::string store_name = mp::read_raw(_in);
    const size_t klass_id = mp::read_uint(_in);
    const size_t nelem = mp::read_uint(_in);

    // sanity check on the value of the klass_id
    if (klass_id >= rt.klasses.size()) {
      std::stringstream msg;
      msg << "klass_id value " << klass_id << " >= number of klasses (" << rt.klasses.size() << ")";
      throw ReaderException(msg.str());
    }

    // lookup the store on the Doc class
    const BaseStoreDef *def = nullptr;
    for (auto &s : _dschema.stores()) {
      if (s->serial == store_name) {
        def = s;
        break;
      }
    }

    RTStoreDef *rtstore;
    if (def == nullptr)
      rtstore = new RTStoreDef(n, store_name, rt.klasses[klass_id], nullptr, 0, nelem);
    else
      rtstore = new RTStoreDef(n, store_name, rt.klasses[klass_id], def);
    assert(rtstore != nullptr);
    rt.klasses[klass_id_meta]->stores.push_back(rtstore);

    // ensure that the stream store and the static store agree on the klass they're storing
    if (!rtstore->is_lazy()) {
      const TypeInfo &store_ptr_type = def->pointer_type();
      const TypeInfo &klass_ptr_type = rt.klasses[klass_id]->def->type;
      if (store_ptr_type != klass_ptr_type) {
        std::stringstream msg;
        msg << "Store '" << store_name << "' points to " << store_ptr_type << " but the stream says it points to " << klass_ptr_type;
        throw ReaderException(msg.str());
      }

      // resize the store to house the correct number of instances
      def->resize(doc, nelem);
    }
  } // for each store


  // back-fill each of the pointer fields to point to the actual RTStoreDef instance
  for (auto &klass : rt.klasses) {
    for (auto &field : klass->fields) {
      if (field->pointer != nullptr) {
        // sanity check on the value of store_id
        const size_t store_id = reinterpret_cast<size_t>(field->pointer);
        if (store_id >= klass->stores.size()) {
          std::stringstream msg;
          msg << "store_id value " << store_id << " >= number of stores (" << klass->stores.size() << ")";
          throw ReaderException(msg.str());
        }
        const RTStoreDef *const store = klass->stores[store_id];

        // ensure they point to the same type
        const ptrdiff_t field_store_offset = field->def->store_offset(nullptr);
        const ptrdiff_t store_store_offset = store->def->store_offset(nullptr);
        if (field_store_offset != store_store_offset) {
          std::stringstream msg;
          msg << "field_store_offset (" << field_store_offset << ") != store_store_offset (" << store_store_offset << ")";
          throw ReaderException(msg.str());
        }

        // update the field pointer value
        field->pointer = store;
      }
    }
  }


  // buffer for reading the <instances> into
  const char *instances_bytes = nullptr;
  size_t instances_bytes_size = 0;

  // read the document instance
  // <doc_instance> ::= <instances_nbytes> <instance>
  {
    const size_t instances_nbytes = mp::read_uint(_in);

    // read in all of the instances data in one go into a buffer
    if (instances_nbytes > instances_bytes_size) {
      delete [] instances_bytes;
      instances_bytes = new char[instances_nbytes];
      instances_bytes_size = instances_nbytes;
      assert(instances_bytes != nullptr);
    }
    _in.read(const_cast<char *>(instances_bytes), instances_nbytes);
    if (!_in.good()) {
      std::stringstream msg;
      msg << "Failed to read in " << instances_nbytes << " from the input stream";
      throw ReaderException(msg.str());
    }

    // wrap the read in instances in a reader
    io::ArrayReader reader(instances_bytes, instances_nbytes);

    // allocate space to write the lazy attributes to
    char *const lazy_bytes = new char[instances_nbytes];
    assert(lazy_bytes != nullptr);
    uint32_t lazy_nfields = 0;

    // wrap the lazy bytes in a writer
    io::UnsafeArrayWriter lazy_writer(lazy_bytes);

    // <instance> ::= { <field_id> : <obj_val> }
    const uint32_t size = mp::read_map_size(reader);
    for (uint32_t i = 0; i != size; ++i) {
      const uint32_t key = static_cast<uint32_t>(mp::read_uint(reader));
      RTFieldDef &field = *rt.klasses[klass_id_meta]->fields[key];

      // deserialize the field value if required
      if (field.is_lazy()) {
        mp::WireType type;
        ++lazy_nfields;
        mp::write_uint(lazy_writer, key);
        mp::read_lazy(reader, lazy_writer, type);
      }
      else {
        const char *const before = reader.upto();
        field.def->reader(reader, static_cast<void *>(&doc), static_cast<void *>(&doc));
        const char *const after = reader.upto();

        // keep a lazy serialized copy of the field if required
        if (field.def->mode == FieldMode::READ_ONLY) {
          ++lazy_nfields;
          mp::write_uint(lazy_writer, key);
          lazy_writer.write(before, after - before);
        }
      }
    } // for each field

    // check whether or not the lazy slab was used
    if (lazy_writer.data() == lazy_writer.upto())
      delete [] lazy_bytes;
    else {
      doc._lazy = lazy_writer.data();
      doc._lazy_nelem = lazy_nfields;
      doc._lazy_nbytes = lazy_writer.upto() - lazy_writer.data();
      rt.lazy_buffers.push_back(lazy_bytes);
    }
  }


  // read the store instances
  // <instances_groups> ::= <instances_group>*
  for (auto &store : rt.klasses[klass_id_meta]->stores) {
    // <instances_group>  ::= <instances_nbytes> <instances>
    const size_t instances_nbytes = mp::read_uint(_in);

    // allocate space to write the lazy attributes to
    char *const lazy_bytes = new char[instances_nbytes];
    assert(lazy_bytes != nullptr);

    // read in the store lazily if required
    if (store->is_lazy()) {
      // read in all of the bytes in one go
      _in.read(lazy_bytes, instances_nbytes);
      if (!_in.good()) {
        std::stringstream msg;
        msg << "Failed to read in " << instances_nbytes << " from the input stream";
        throw ReaderException(msg.str());
      }

      // attach the lazy store to the RTStoreDef instance
      store->lazy_data = lazy_bytes;
      store->lazy_nbytes = instances_nbytes;

      rt.lazy_buffers.push_back(lazy_bytes);
      continue;
    }

    // read in all of the instances data in one go into a buffer
    if (instances_nbytes > instances_bytes_size) {
      delete [] instances_bytes;
      instances_bytes = new char[instances_nbytes];
      instances_bytes_size = instances_nbytes;
      assert(instances_bytes != nullptr);
    }
    _in.read(const_cast<char *>(instances_bytes), instances_nbytes);
    if (!_in.good()) {
      std::stringstream msg;
      msg << "Failed to read in " << instances_nbytes << " from the input stream";
      throw ReaderException(msg.str());
    }

    // wrap the read in instances in a reader
    io::ArrayReader reader(instances_bytes, instances_nbytes);

    // wrap the lazy bytes in a writer
    io::UnsafeArrayWriter lazy_writer(lazy_bytes);

    // get pointers to the vector of Ann instances
    char *objects = store->def->read_begin(doc);
    const size_t objects_delta = store->def->read_size();

    // <instances> ::= [ <instance> ]
    const uint32_t ninstances = mp::read_array_size(reader);
    for (uint32_t i = 0; i != ninstances; ++i) {
      const char *const lazy_before = lazy_writer.upto();
      uint32_t lazy_nfields = 0;

      // <instance> ::= { <field_id> : <obj_val> }
      const uint32_t size = mp::read_map_size(reader);
      for (uint32_t j = 0; j != size; ++j) {
        const uint32_t key = static_cast<uint32_t>(mp::read_uint(reader));
        RTFieldDef &field = *store->klass->fields[key];

        // deserialize the field value if required
        if (field.is_lazy()) {
          mp::WireType type;
          ++lazy_nfields;
          mp::write_uint(lazy_writer, key);
          mp::read_lazy(reader, lazy_writer, type);
        }
        else {
          const char *const before = reader.upto();
          field.def->reader(reader, objects, static_cast<void *>(&doc));
          const char *const after = reader.upto();

          // keep a lazy serialized copy of the field if required
          if (field.def->mode == FieldMode::READ_ONLY) {
            ++lazy_nfields;
            mp::write_uint(lazy_writer, key);
            lazy_writer.write(before, after - before);
          }
        }
      } // for each field

      // check whether or not the Ann instance used any of the lazy buffer
      const char *const lazy_after = lazy_writer.upto();
      if (lazy_after != lazy_before) {
        Ann &ann = *reinterpret_cast<Ann *>(objects);
        ann._lazy = lazy_before;
        ann._lazy_nelem = lazy_nfields;
        ann._lazy_nbytes = lazy_after - lazy_before;
      }

      // move on to the next annotation instance
      objects += objects_delta;
    } // for each instance

    // check whether or not the lazy slab was used
    if (lazy_writer.data() == lazy_writer.upto())
      delete [] lazy_bytes;
    else
      rt.lazy_buffers.push_back(lazy_bytes);
  } // for each instance group

  delete [] instances_bytes;

  _has_more = true;
  return *this;
}

} }
