#include <iostream>
#include <string>
#include <queue>
#include <thread>
#include "cmdline.h"
#include "Common.hpp"
#include "PlainSlp.hpp"
#include "PoSlp.hpp"
#include "ShapedSlp_Status.hpp"
#include "ShapedSlp.hpp"
#include "ShapedSlpV2.hpp"
#include "SelfShapedSlp.hpp"
#include "SelfShapedSlpV2.hpp"
#include "DirectAccessibleGammaCode.hpp"
#include "IncBitLenCode.hpp"
#include "FixedBitLenCode.hpp"
#include "SelectType.hpp"
#include "VlcVec.hpp"

using namespace std;

using namespace std::chrono;
using timer = std::chrono::high_resolution_clock;

using var_t = uint32_t;

template<class SlpT>
void measure
(
 std::string in,
 const uint64_t numItr,
 const uint64_t lenExpand,
 const uint64_t firstPos,
 const uint64_t jump,
 const uint64_t numThreads,
 const bool dummy_flag
) {
  SlpT slp;

  auto start = timer::now();
  ifstream fs(in);
  slp.load(fs);
  auto stop = timer::now();
  cout << "time to load (ms): " << duration_cast<milliseconds>(stop-start).count() << endl;
  // slp.printStatus();

  cout << "numItr = " << numItr << ", lenExpand = " << lenExpand << ", firstPos = " << firstPos << ", jump = " << jump << endl;
  const uint64_t textLen = slp.getLen();
  if (lenExpand >= textLen or jump * numThreads >= textLen) {
    cout << "lenExpand and (jump * numThreads) should be smaller than text length, which is " << textLen << endl;
    exit(1);
  }

  const uint64_t tlen = (numThreads)? lenExpand / numThreads : 0;
  std::cout << "numThreads = " << numThreads << ", hardware_concurrency = " << std::thread::hardware_concurrency() << std::endl;
  vector<string> substr(numThreads);
  for (uint64_t t = 0; t < numThreads; ++t) {
    substr[t].resize(lenExpand);
  }

  const uint64_t numLoop = 11;
  std::vector<double> times(numLoop);
  for (uint64_t loop = 0; loop < numLoop; ++loop) {
    start = timer::now();
    vector<thread> threads;
    for (uint64_t t = 0; t < numThreads; ++t) {
      const uint64_t jumpt = jump * (t + 1);
      char * data = substr[t].data();
      threads.push_back
        (
         thread([textLen, lenExpand, numItr, jumpt, data, &slp] () {
           uint64_t beg = jumpt;
           for (uint64_t i = 0; i < numItr; ++i) {
             slp.expandSubstr(beg, lenExpand, data);
             beg += jumpt;
             if (beg > textLen - lenExpand) {
               beg -= (textLen - lenExpand);
             }
           }
         }));
    }
    for (auto &th : threads) {
      if (th.joinable()) {
        th.join();
      }
    }
    stop = timer::now();
    times[loop] = (double)duration_cast<microseconds>(stop-start).count() / (numItr * numThreads);
  }

  std::sort(times.begin(), times.end());
  cout << "time to expand (micro sec per query): " << times[numLoop / 2] << endl;

  if (dummy_flag) {
    for (uint64_t t = 0; t < numThreads; ++t) {
      cout << substr[t] << endl;
    }
  }
}


int main(int argc, char* argv[])
{
  using Fblc = FixedBitLenCode<>;
  using SelSd = SelectSdvec<>;
  using SelMcl = SelectMcl<>;
  using DagcSd = DirectAccessibleGammaCode<SelSd>;
  using DagcMcl = DirectAccessibleGammaCode<SelMcl>;
  using Vlc64 = VlcVec<sdsl::coder::elias_delta, 64>;
  using Vlc128 = VlcVec<sdsl::coder::elias_delta, 128>;
  using funcs_type = map<string,
                         void(*)
                         (
                          std::string in,
                          const uint64_t numItr,
                          const uint64_t lenExpand,
                          const uint64_t firstPos,
                          const uint64_t jump,
                          uint64_t numThreads,
                          const bool dummy_flag
                          )>;
  funcs_type funcs;

  //// PlainSlp
  funcs.insert(make_pair("PlainSlp_FblcFblc", measure<PlainSlp<var_t, Fblc, Fblc>>));
  funcs.insert(make_pair("PlainSlp_IblcFblc", measure<PlainSlp<var_t, IncBitLenCode, Fblc>>));
  funcs.insert(make_pair("PlainSlp_32Fblc", measure<PlainSlp<var_t, FixedBitLenCode<32>, Fblc>>));

  //// PoSlp: Post-order SLP
  //// Sometimes PoSlp_Sd is better than PoSlp_Iblc
  funcs.insert(make_pair("PoSlp_Iblc", measure<PoSlp<var_t, IncBitLenCode>>));
  funcs.insert(make_pair("PoSlp_Sd", measure<PoSlp<var_t, DagcSd>>));
  // funcs.insert(make_pair("PoSlp_Mcl", measure<PoSlp<var_t, DagcMcl>>));

  //// ShapedSlp: plain implementation of slp encoding that utilizes shape-tree grammar
  //// Since bit length to represent slp element is small, SelMcl is good for them.
  //// For stg and bal element, SelSd is better
  funcs.insert(make_pair("ShapedSlp_SdMclSd_SdMcl", measure<ShapedSlp<var_t, DagcSd, DagcMcl, DagcSd, SelSd, SelMcl>>));
  funcs.insert(make_pair("ShapedSlp_SdSdSd_SdMcl", measure<ShapedSlp<var_t, DagcSd, DagcSd, DagcSd, SelSd, SelMcl>>));

  //// ShapedSlpV2: all vlc vectors are merged into one.
  //// Generally encoding size gets worse than ShapedSlp_SdMclSd_SdMcl because
  //// - Since bit length to represnet stg and bal element is large, DagcSd is a good choice.
  //// - On the other hand, bit size to represent slp element is significantly small, and so SelMcl should be used
  funcs.insert(make_pair("ShapedSlpV2_Sd_SdMcl", measure<ShapedSlpV2<var_t, DagcSd, SelSd, SelMcl>>));
  // funcs.insert(make_pair("ShapedSlpV2_SdSdSd", measure<ShapedSlp<var_t, DagcSd, SelSd, SelSd>>));
  // funcs.insert(make_pair("ShapedSlpV2_SdMclMcl", measure<ShapedSlp<var_t, DagcSd, SelMcl, SelMcl>>));
  // funcs.insert(make_pair("ShapedSlpV2_Vlc128SdSd", measure<ShapedSlp<var_t, Vlc128, SelSd, SelSd>>));

  //// SelfShapedSlp: ShapedSlp that does not use shape-tree grammar
  funcs.insert(make_pair("SelfShapedSlp_SdSd_Sd", measure<SelfShapedSlp<var_t, DagcSd, DagcSd, SelSd>>));
  funcs.insert(make_pair("SelfShapedSlp_SdSd_Mcl", measure<SelfShapedSlp<var_t, DagcSd, DagcSd, SelMcl>>));
  // funcs.insert(make_pair("SelfShapedSlp_MclMcl_Sd", measure<SelfShapedSlp<var_t, DagcMcl, DagcMcl, SelSd>>));
  // funcs.insert(make_pair("SelfShapedSlp_SdMcl_Sd", measure<SelfShapedSlp<var_t, DagcSd, DagcMcl, SelSd>>));

  //// SelfShapedSlpV2:
  //// attempted to asign smaller offsets to frequent variables by giving special seats for hi-frequent ones
  funcs.insert(make_pair("SelfShapedSlpV2_SdSd_Sd", measure<SelfShapedSlpV2<var_t, DagcSd, DagcSd, SelSd>>));
  // funcs.insert(make_pair("SelfShapedSlpV2_SdSd_Mcl", measure<SelfShapedSlpV2<var_t, DagcSd, DagcSd, SelMcl>>));

  string methodList;
  for (auto itr = funcs.begin(); itr != funcs.end(); ++itr) {
    methodList += itr->first + ". ";
  }


  cmdline::parser parser;
  parser.add<string>("input", 'i', "input file name in which ShapedSlp data structure is written.", true);
  parser.add<string>("encoding", 'e', "encoding: " + methodList, true);
  parser.add<uint64_t>("numItr", 'n', "number of iterations", true);
  parser.add<uint64_t>("lenExpand", 'l', "length to expand", true);
  parser.add<uint64_t>("firstPos", 'f', "first position to access", false, 0);
  parser.add<uint64_t>("jump", 'j', "amount of jump when determining the next position to access", false, 38201); // default 38201 is a prime number
  parser.add<uint64_t>("numThreads", 't', "number of threads", false, 1);
  parser.add<bool>("dummy_flag", 0, "this is dummy flag to prevent that optimization deletes codes", false, false);
  parser.parse_check(argc, argv);
  const string in = parser.get<string>("input");
  const string encoding = parser.get<string>("encoding");
  const uint64_t numItr = parser.get<uint64_t>("numItr");
  const uint64_t lenExpand = parser.get<uint64_t>("lenExpand");
  const uint64_t firstPos = parser.get<uint64_t>("firstPos");
  const uint64_t jump = parser.get<uint64_t>("jump");
  const uint64_t nt = parser.get<uint64_t>("numThreads");
  const bool dummy_flag = parser.get<bool>("dummy_flag");

  if (numItr == 0) {
    cout << "numItr should be > 0." << endl;
    exit(1);
  }


  if (encoding.compare("All") == 0) {
    for (auto itr = funcs.begin(); itr != funcs.end(); ++itr) {
      cout << itr->first << ": BEGIN" << std::endl;
      itr->second(in + itr->first, numItr, lenExpand, firstPos, jump, nt, dummy_flag);
      cout << itr->first << ": END" << std::endl;
    }
  } else {
    auto itr = funcs.find(encoding);
    if (itr != funcs.end()) {
      cout << itr->first << ": BEGIN" << std::endl;
      itr->second(in, numItr, lenExpand, firstPos, jump, nt, dummy_flag);
      cout << itr->first << ": END" << std::endl;
    } else {
      cerr << "error: specify a valid encoding name in " + methodList << endl;
      exit(1);
    }
  }


  // { // correctness check
  //   PoSlp<var_t> poslp;
  //   ShapedSlp<var_t> sslp;
  //   ShapedSlpDagc<var_t> sslpdagc;
  //   {
  //     ifstream fs("temp0");
  //     poslp.load(fs);
  //   }
  //   {
  //     ifstream fs("temp1");
  //     sslp.load(fs);
  //   }
  //   {
  //     ifstream fs("temp2");
  //     sslpdagc.load(fs);
  //   }

  //   cout << "numItr = " << numItr << ", lenExpand = " << lenExpand << ", firstPos = " << firstPos << ", jump = " << jump << endl;
  //   const uint64_t textLen = poslp.getLen();
  //   if (firstPos >= textLen or lenExpand >= textLen or lenExpand >= textLen) {
  //     cout << "firstPos, lenExpand and jump should be smaller than text length, which is " << textLen << endl;
  //     exit(1);
  //   }

  //   string substr0, substr1, substr2;
  //   substr0.resize(lenExpand);
  //   substr1.resize(lenExpand);
  //   substr2.resize(lenExpand);
  //   uint64_t beg = firstPos;
  //   for (uint64_t i = 0; i < numItr; ++i) {
  //     cout << beg << endl;
  //     // substr0[0] = poslp.charAt(beg);
  //     // substr1[0] = sslp.charAt(beg);
  //     // substr2[0] = sslpdagc.charAt(beg);
  //     poslp.expandSubstr(beg, lenExpand, substr0.data());
  //     sslp.expandSubstr(beg, lenExpand, substr1.data());
  //     sslpdagc.expandSubstr(beg, lenExpand, substr2.data());
  //     if ((substr0 != substr1) or (substr1 != substr2) or (substr0 != substr2)) {
  //       cout << "wrong: beg = " << beg << endl;
  //       cout << substr0 << endl;
  //       cout << substr1 << endl;
  //       cout << substr2 << endl;
  //       exit(1);
  //     }
  //     beg += jump;
  //     if (beg > textLen - lenExpand) {
  //       beg -= (textLen - lenExpand);
  //     }
  //   }
  // }


  return 0;
}
