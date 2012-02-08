#include <schwa/docrep.hpp>

namespace schwa {
namespace docrep {


Document::~Document(void) {
  delete [] _bytes;
  for (auto it(_tokens.begin()); it != _tokens.end(); ++it)
    delete *it;
  for (auto it(_spans.begin()); it != _spans.end(); ++it)
    for (auto it2(it->second.begin()); it2 != it->second.end(); ++it2)
      delete *it2;
}


size_t
Document::add_token(Token *const token) {
  _tokens.push_back(token);
  return _tokens.size();
}


bool
Document::remove_token(const Token *const token) {
  for (auto it(_tokens.begin()); it != _tokens.end(); ++it)
    if (*it == token) {
      _tokens.erase(it);
      return true;
    }
  return false;
}


std::ostream &
Document::serialize(std::ostream &out) const {
  msgpack::packer<std::ostream> packer(out);
  std::cerr << "Document::serialize begin" << std::endl;
  packer.pack_raw(_nbytes);
  packer.pack_raw_body(_bytes, _nbytes);
  packer.pack_array(_tokens.size());
  for (auto &token : _tokens)
    token->serialize(out);
  std::cerr << "Document::serialize end" << std::endl;
  return out;
}

}
}
