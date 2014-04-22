/* -*- Mode: C++; indent-tabs-mode: nil -*- */
#include <schwa/unittest.h>

#include <algorithm>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include <schwa/learn.h>


namespace schwa {
namespace learn {

SUITE(schwa__learn__extract) {

class X {
public:
  unsigned int value;

  X(void) : value(0) { }
  explicit X(unsigned int value) : value(value) { }
};


TEST(sentinel_offsets) {
  std::vector<X> xs(20);
  for (unsigned int i = 0; i != xs.size(); ++i)
    xs[i].value = i;

  const SentinelOffsets<X> o(&xs.front(), &xs.back() + 1, [](const X *const x) {
      std::stringstream ss;
      ss << x->value;
      return ss.str();
  });
  CHECK_EQUAL("0", o(0, 0));
  CHECK_EQUAL("1", o(0, 1));
  CHECK_EQUAL(SENTINEL, o(0, -1));

  CHECK_EQUAL("5", o(5, 0));
  CHECK_EQUAL("6", o(5, 1));
  CHECK_EQUAL("4", o(5, -1));

  CHECK_EQUAL("19", o(19, 0));
  CHECK_EQUAL(SENTINEL, o(19, 1));
  CHECK_EQUAL("18", o(19, -1));
}


TEST(window) {
  std::vector<X> xs(20);
  for (unsigned int i = 0; i != xs.size(); ++i)
    xs[i].value = i;

  const SentinelOffsets<X> o(&xs.front(), &xs.back() + 1, [](const X *const x) {
      std::stringstream ss;
      ss << x->value;
      return ss.str();
  });
  const std::function<std::string(const SentinelOffsets<X> &, size_t, ptrdiff_t)> unigram = [](const SentinelOffsets<X> &o, size_t i, ptrdiff_t delta) {
    return o(i, delta);
  };

  Features<> f;
  window("n", 0, -2, 3, o, f, unigram);

  std::stringstream ss;
  f.dump_crfsuite(ss);
  ss.seekg(0);

  std::vector<std::string> units;
  std::string tmp;
  for (const char c : ss.str()) {
    if (c == '\t') {
      if (!tmp.empty()) {
        units.push_back(tmp);
        tmp.clear();
      }
    }
    else
      tmp.push_back(c);
  }
  if (!tmp.empty()) {
    units.push_back(tmp);
    tmp.clear();
  }

  std::sort(units.begin(), units.end());
  for (const auto &x : units)
    std::cout << x << std::endl;
  CHECK_EQUAL(6, units.size());
  CHECK_EQUAL("n[i+1]=1:1", units[0]);
  CHECK_EQUAL("n[i+2]=2:1", units[1]);
  CHECK_EQUAL("n[i+3]=3:1", units[2]);
  CHECK_EQUAL("n[i-1]=__SENTINEL__:1", units[3]);
  CHECK_EQUAL("n[i-2]=__SENTINEL__:1", units[4]);
  CHECK_EQUAL("n[i]=0:1", units[5]);
}

}  // SUITE

}  // namespace learn
}  // namespace schwa