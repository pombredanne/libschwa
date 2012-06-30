/* -*- Mode: C++; indent-tabs-mode: nil -*- */
#include <schwa/config.h>
#include <schwa/dr.h>
#include <schwa/tokenizer.h>
#include <schwa/tokenizer/streams/debug_text.h>
#include <schwa/tokenizer/streams/docrep.h>
#include <schwa/tokenizer/streams/text.h>

namespace cfg = schwa::config;
namespace tok = schwa::tokenizer;


class Config : public cfg::OpGroup {
public:
  cfg::IStreamOp input;
  cfg::OStreamOp output;
  cfg::EnumOp<std::string> printer;
  cfg::Op<size_t> input_buffer;

  Config(void) :
    cfg::OpGroup("tok", "Schwa-Lab tokenizer"),
    input(*this, "input", "input filename"),
    output(*this, "output", "output filename"),
    printer(*this, "printer", "which printer to use as output", {"text", "debug", "docrep"}, "text"),
    input_buffer(*this, "input_buffer", "input buffer size (bytes)", tok::BUFFER_SIZE)
    { }
  virtual ~Config(void) { }
};


int
main(int argc, char *argv[]) {
  // instantiate a document schema for use in the config framework
  tok::Doc::Schema schema;

  // parse the command line options
  Config c;
  try {
    if (!c.process(argc - 1, argv + 1))
      return 1;
  }
  catch (cfg::ConfigException &e) {
    std::cerr << schwa::print_exception("ConfigException", e) << std::endl;
    c.help(std::cerr);
    return 1;
  }

  // input and file files
  std::istream &in = c.input.file();
  std::ostream &out = c.output.file();

  // printer
  tok::Stream *stream;
  if (c.printer() == "text")
    stream = new tok::TextStream(out);
  else if (c.printer() == "debug")
    stream = new tok::DebugTextStream(out);
  else if (c.printer() == "docrep")
    stream = new tok::DocrepStream(out, schema);
  else
    throw cfg::ConfigException("Unknown value", "printer", c.printer());

  tok::Tokenizer t;
  t.tokenize_stream(*stream, in, c.input_buffer());

  // cleanup
  delete stream;

  return 0;
}
