/* -*- Mode: C++; indent-tabs-mode: nil -*- */

%%{
  machine tokenizer;

  action s1 { s.suffix++; }
  action s2 { s.suffix += 2; }
  action s3 { s.suffix += 3; }
  action s4 { s.suffix += 4; }
  action s5 { s.suffix += 5; }
  action s6 { s.suffix += 6; }
  action s7 { s.suffix += 7; }
  action s8 { s.suffix += 8; }

}%%
