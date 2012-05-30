/* -*- Mode: C++; indent-tabs-mode: nil -*- */

namespace schwa {
  namespace dr {

    class BaseDocumentSchema;
    class Document;


    class Writer {
    protected:
      std::ostream &_out;
      BaseDocumentSchema &_dschema;

    public:
      Writer(std::ostream &out, BaseDocumentSchema &dschema) : _out(out), _dschema(dschema) { }
      ~Writer(void) { }

      void write(const Document &doc);

      inline Writer &
      operator <<(const Document &doc) {
        write(doc);
        return *this;
      }
    };

  }
}