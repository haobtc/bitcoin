// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "data/tx_invalid.json.h"
#include "data/tx_valid.json.h"
#include "test/test_bitcoin.h"

#include "clientversion.h"
#include "checkqueue.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "key.h"
#include "keystore.h"
#include "main.h" // For CheckTransaction
#include "policy/policy.h"
#include "script/script.h"
#include "script/sign.h"
#include "script/script_error.h"
#include "script/standard.h"
#include "utilstrencodings.h"
#include "chainparams.h"
#include "dblayer.h"

#include <map>
#include <string>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include <univalue.h>

using namespace std;

typedef vector<unsigned char> valtype;

// In script_tests.cpp
extern UniValue read_json(const std::string& jsondata);

BOOST_FIXTURE_TEST_SUITE(db_tests, BasicTestingSetup)
BOOST_AUTO_TEST_CASE(test_save_blk)
{
  int i = 0;
  std::string strHash = ("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38");
  uint256 hash(uint256S(strHash));

  if (mapBlockIndex.count(hash) == 0)
        BOOST_ERROR("Bad test: " << strHash);

  CBlock block;
  CBlockIndex* pblockindex = mapBlockIndex[hash];
 
  for (; i < 10; i++) {

      int64_t nStart = GetTimeMicros();
      if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus())) {
        BOOST_ERROR("Bad test: " << strHash);
      }
      LogPrint("dblayer", "- read block from disk: %.2fms\n",
               (GetTimeMicros() - nStart) * 0.001);
      if (dbSaveBlock(pblockindex, block) == -1) {
        BOOST_ERROR("Bad test: " << strHash);
      }
      LogPrint("dblayer", "- Save block to db: %.2fms height %d\n",
               (GetTimeMicros() - nStart) * 0.001, pblockindex->nHeight);

      pblockindex = pblockindex->pprev; 
  }

  BOOST_CHECK(!0);
}

BOOST_AUTO_TEST_SUITE_END() 
