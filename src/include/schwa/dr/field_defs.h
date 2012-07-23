/* -*- Mode: C++; indent-tabs-mode: nil -*- */

namespace schwa {
  namespace dr {

    class RTSchema;

    // ========================================================================
    // Constants
    // ========================================================================
    enum class FieldMode : uint8_t {
      READ_WRITE = 1,
      READ_ONLY = 2,
      DELETE = 3,
      STREAM_ONLY = 4
    };


    // ========================================================================
    // Base classes for field and store definitions
    // ========================================================================
    class BaseDef {
    public:
      typedef void (*reader_type)(io::ArrayReader &in, void *, void *);
      typedef bool (*writer_type)(io::WriteBuffer &out, const uint32_t, const void *, const void *);

      const std::string name;
      const std::string help;
      const FieldMode mode;
      std::string serial;
      reader_type reader;
      writer_type writer;

    protected:
      BaseDef(const std::string &name, const std::string &help, const FieldMode mode, const std::string &serial) : name(name), help(help), mode(mode), serial(serial), reader(nullptr), writer(nullptr) {
        if (mode == FieldMode::STREAM_ONLY)
          throw ValueException("Invalid `mode' value: must either be READ_WRITE, READ_ONLY, or DELETE");
      }

    public:
      virtual ~BaseDef(void) { }

      virtual const TypeInfo &pointer_type(void) const { return *static_cast<const TypeInfo *>(nullptr); }
      virtual ptrdiff_t store_offset(const Doc *) const = 0;
    };


    class BaseFieldDef : public BaseDef {
    public:
      const bool is_pointer;
      const bool is_slice;

    protected:
      BaseFieldDef(const std::string &name, const std::string &help, const FieldMode mode, const std::string &serial, const bool is_pointer, const bool is_slice) : BaseDef(name, help, mode, serial), is_pointer(is_pointer), is_slice(is_slice) { }

    public:
      virtual ~BaseFieldDef(void) { }

      ptrdiff_t store_offset(const Doc *) const { assert(!"this should never be invoked"); return -1; }
    };


    class BaseStoreDef : public BaseDef {
    protected:
      BaseStoreDef(const std::string &name, const std::string &help, const FieldMode mode, const std::string &serial) : BaseDef(name, help, mode, serial) { }

    public:
      virtual ~BaseStoreDef(void) { }

      virtual void resize(Doc &doc, const size_t size) const = 0;

      virtual size_t size(const Doc &doc) const = 0;
      virtual void write(io::WriteBuffer &out, const Doc &_doc, const RTSchema &schema, void (*writer)(io::WriteBuffer &, const Doc &, const RTSchema &, const void *const)) const = 0;

      virtual char *read_begin(Doc &_doc) const = 0;
      virtual size_t read_size(void) const = 0;
    };


    // ========================================================================
    // Templated FieldDef and StoreDef
    // ========================================================================
    template <typename T, T fn>
    class FieldDef;

    template <typename R, typename T, R T::*member_obj_ptr>
    class FieldDef<R T::*, member_obj_ptr> : public BaseFieldDef {
    public:
      static_assert(FieldTraits<R>::is_dr_ptr_type == false, "DR_FIELD must be used with POD fields only. Use DR_POINTER for schwa::dr field types instead.");
      static_assert(FieldTraits<R>::is_pod_ptr == false, "Fields cannot be POD pointers. Use schwa::dr::Pointer<T> instead.");
      typedef R value_type;
      typedef T annotation_type;

      FieldDef(BaseSchema &schema, const std::string &name, const std::string &help, const FieldMode mode) : FieldDef(schema, name, help, mode, name) { }
      FieldDef(BaseSchema &schema, const std::string &name, const std::string &help, const FieldMode mode, const std::string &serial) : BaseFieldDef(name, help, mode, serial, false, FieldTraits<R>::is_slice) {
        schema.add(this);
      }
      virtual ~FieldDef(void) { }
    };


    template <typename T1, T1 fn1, typename T2, T2 fn2>
    class FieldDefWithStore;

    template <typename R, typename T, typename S, typename D, R T::*field_ptr, Store<S> D::*store_ptr>
    class FieldDefWithStore<R T::*, field_ptr, Store<S> D::*, store_ptr> : public BaseFieldDef {
    public:
      static_assert(FieldTraits<R>::is_dr_ptr_type == true, "DR_POINTER must be used with schwa::dr field types only");
      static_assert(boost::is_same<typename FieldTraits<R>::value_type, S>::value, "Field (type T) and storage field (Store<T>) must have the same type (T)");
      static_assert(boost::is_base_of<Ann, S>::value, "Store<T> type T must be a subclass of Ann");
      typedef R value_type;
      typedef T annotation_type;
      typedef S store_type;
      typedef D doc_type;

    private:
      const TypeInfo _pointer_type;

    public:
      FieldDefWithStore(BaseSchema &schema, const std::string &name, const std::string &help, const FieldMode mode) : FieldDefWithStore(schema, name, help, mode, name) { }
      FieldDefWithStore(BaseSchema &schema, const std::string &name, const std::string &help, const FieldMode mode, const std::string &serial) : BaseFieldDef(name, help, mode, serial, true, FieldTraits<R>::is_slice), _pointer_type(TypeInfo::create<S>()) {
        schema.add(this);
      }
      virtual ~FieldDefWithStore(void) { }

      const TypeInfo &pointer_type(void) const { return _pointer_type; }
      ptrdiff_t store_offset(const Doc *doc) const { return reinterpret_cast<const char *>(&(static_cast<const D *>(doc)->*store_ptr)) - reinterpret_cast<const char *>(doc); }
    };


    template <typename T, T fn>
    class StoreDef;

    template <typename S, typename T, Store<S> T::*store_ptr>
    class StoreDef<Store<S> T::*, store_ptr> : public BaseStoreDef {
    public:
      static_assert(boost::is_base_of<Ann, S>::value, "Store<T> type T must be a subclass of Ann");
      typedef S store_type;

    private:
      const TypeInfo _pointer_type;

    public:
      StoreDef(BaseDocSchema &schema, const std::string &name, const std::string &help, const FieldMode mode) : StoreDef(schema, name, help, mode, name) { }
      StoreDef(BaseDocSchema &schema, const std::string &name, const std::string &help, const FieldMode mode, const std::string &serial) : BaseStoreDef(name, help, mode, serial), _pointer_type(TypeInfo::create<S>()) {
        schema.add(this);
      }
      virtual ~StoreDef(void) { }

      void resize(Doc &doc, const size_t size) const { (static_cast<T &>(doc).*store_ptr).resize(size); }

      const TypeInfo &pointer_type(void) const { return _pointer_type; }
      size_t size(const Doc &doc) const { return (static_cast<const T &>(doc).*store_ptr).size(); }
      ptrdiff_t store_offset(const Doc *doc) const { return reinterpret_cast<const char *>(&(static_cast<const T *>(doc)->*store_ptr)) - reinterpret_cast<const char *>(doc); }

      char *
      read_begin(Doc &_doc) const {
        T &doc = static_cast<T &>(_doc);
        Store<S> &store = doc.*store_ptr;
        return reinterpret_cast<char *>(&store[0]);
      }

      inline size_t read_size(void) const { return sizeof(S); }

      void
      write(io::WriteBuffer &out, const Doc &_doc, const RTSchema &schema, void (*writer)(io::WriteBuffer &, const Doc &, const RTSchema &, const void *const)) const {
        namespace mp = schwa::msgpack;
        const T &doc = static_cast<const T &>(_doc);
        const Store<S> &store = doc.*store_ptr;

        // <instances> ::= [ <instance> ]
        mp::write_array_size(out, store.size());
        for (auto &obj : store)
          writer(out, _doc, schema, &obj);
      }
    };


    #define DR_FIELD(member_obj_ptr)                  schwa::dr::FieldDef<decltype(member_obj_ptr), member_obj_ptr>
    #define DR_POINTER(member_obj_ptr, store_obj_ptr) schwa::dr::FieldDefWithStore<decltype(member_obj_ptr), member_obj_ptr, decltype(store_obj_ptr), store_obj_ptr>
    #define DR_STORE(member_obj_ptr)                  schwa::dr::StoreDef<decltype(member_obj_ptr), member_obj_ptr>

  }
}
