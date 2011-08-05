/* -*- Mode: C++; indent-tabs-mode: nil -*- */

namespace schwa { namespace tokens {

class Stream {
public:
  virtual void add(Type type, const char *raw, offset_type begin, offset_type len,
                   const char *norm = 0) = 0;

  virtual void begin_sentence(void){}
	virtual void end_sentence(void){}

  virtual void begin_list(void){}
  virtual void end_list(void){}

  virtual void begin_item(void){}
  virtual void end_item(void){}

  virtual void begin_paragraph(void){}
	virtual void end_paragraph(void){ end_sentence(); }

  virtual void begin_heading(int depth){ end_paragraph(); }
  virtual void end_heading(int depth){ end_sentence(); }

  virtual void begin_document(void){}
	virtual void end_document(void){ end_paragraph(); }
};

} }
