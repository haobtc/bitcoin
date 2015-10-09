/*
 * Copyright 2015 daniel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "base58.h"
#include "keystore.h"
#include "main.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "version.h"
#include "serialize.h"
#include "pubkey.h"
#include "sync.h"
#include "util.h"
#include "script/standard.h"
#include "dblayer.h"

#include <boost/foreach.hpp>

using namespace std;

#define INDEX_FOREACH(index, a, b)                                             \
  for (unsigned int index = static_cast<unsigned int>(-1);                     \
       index == static_cast<unsigned int>(-1);)                                \
  BOOST_FOREACH(a, b) if (++index, true)

struct DBSERVER dbSrv = {
#if defined(HAVE_SQLITE3)
  .db_eng = SDB_SQLITE, 
  .db_ops = &sqlite_db_ops, 
#elif defined(HAVE_MYSQL)
  .db_eng = SDB_MYSQL,
  .db_ops = &mydb_ops,
#elif defined(HAVE_POSTGRESQL)
  .db_eng = SDB_POSTGRESQL,
  .db_ops = &postgresql_db_ops,
#endif
  .db_conn = NULL,
};

bool dbOpen() {

  if (!dbSrv.db_ops->open()) {
    LogPrint("dblayer", "\n db open fail!\n");
    return false;
  }
  LogPrint("dblayer", "\n db open success!\n");
  return true;
}

void dbClose() { dbSrv.db_ops->close(); }

int dbSaveTx(const CTransaction &tx) {
  uint256 hash = tx.GetHash();
  const int32_t version = tx.nVersion;
  const uint32_t lock_time = tx.nLockTime;
  bool coinbase = tx.IsCoinBase();
  unsigned int tx_size = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
  int in_count = tx.vin.size();
  int out_count = tx.vout.size();
  long long in_value = 0;
  long long out_value = 0;

  int tx_id = dbSrv.db_ops->query_tx(hash.begin());
  if (tx_id == -1) {
    tx_id = dbSrv.db_ops->save_tx(hash.begin(), version, lock_time, coinbase,
                                  tx_size, tx.nTimeReceived,
                                  tx.relayIp.c_str());
    if (tx_id == -1) {
      LogPrint("dblayer", "save_tx error tx hash %s \n", hash.ToString());
      return -1;
    }

    INDEX_FOREACH(tx_idx, const CTxIn & txin, tx.vin) {
      CTransaction txp;
      uint256 hashBlockp = 0;
      int prev_out_index = txin.prevout.n;
      uint256 prev_out = txin.prevout.hash;

      if (GetTransaction(txin.prevout.hash, txp, hashBlockp, true)) {
        const CTxOut &txoutp = txp.vout[txin.prevout.n];
        CScript script_sig = txoutp.scriptPubKey;
        vector<CTxDestination> addresses;
        in_value += txoutp.nValue;
      }

      int txin_id = dbSrv.db_ops->save_txin(
          tx_id, tx_idx, prev_out_index, txin.nSequence, &txin.scriptSig[0],
          txin.scriptSig.size(), prev_out.begin());
      if (txin_id == -1) {
        LogPrint("dblayer", "save_txin error txid %d txin index %d \n", tx_id,
                 tx_idx);
        return -1;
      }
    }

    for (unsigned int i = 0; i < tx.vout.size(); i++) {
      const CTxOut &txout = tx.vout[i];
      txnouttype txout_type;
      vector<CTxDestination> addresses;
      int nRequired;

      out_value += txout.nValue;

      if (!ExtractDestinations(txout.scriptPubKey, txout_type, addresses,
                               nRequired)) {
        // LogPrint("dblayer", "ExtractDestinations error: \n");
      }

      int txout_id = dbSrv.db_ops->save_txout(tx_id, i, &txout.scriptPubKey[0],
                                              txout.scriptPubKey.size(),
                                              txout.nValue, txout_type);
      if (txout_id == -1) {
        LogPrint("dblayer", "save_txout error txid %d txout index %d \n", tx_id,
                 i);
        return -1;
      }

      BOOST_FOREACH(const CTxDestination & dest, addresses) {
        if (boost::get<CNoDestination>(&dest))
          continue;

        CBitcoinAddress addr(dest);

        int addr_id = dbSrv.db_ops->save_addr(
            addr.ToString().c_str(), (const char *)addr.Get160());
        if (addr_id == -1) {
          LogPrint("dblayer", "save_addr error addr: %s \n", addr.ToString());
          return -1;
        }

        if (dbSrv.db_ops->save_addr_out(addr_id, txout_id) == -1) {
          LogPrint("dblayer", "save_addr_out error addr: %s \n",
                   addr.ToString());
          return -1;
        }
      }
    }

    dbSrv.db_ops->add_tx_statics(tx_id, in_count, out_count, in_value,
                                 out_value);
  }
  return tx_id;
}

int dbSaveBlock(const CBlockIndex *blockindex, CBlock &block) {
  uint256 hash = block.GetHash();
  uint256 prev_hash = 0;

  if (blockindex->pprev)
    prev_hash = blockindex->pprev->GetBlockHash();
  else
    LogPrint("dblayer", "prev blk is Null, blk hash : %s\n",
             block.GetHash().GetHex());

  int blk_size = (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
  int height = blockindex->nHeight;
  int version = block.nVersion;
  uint256 mrkl_root = block.hashMerkleRoot;
  uint64_t time = block.GetBlockTime();
  unsigned int nonce = block.nNonce;
  int bits = block.nBits;
  uint256 work = blockindex->nChainWork;

  if (dbSrv.db_ops->begin() == -1) {
    dbSrv.db_ops->rollback();
    LogPrint("dblayer", "block save first roll back height: %d \n", height);
    return -1;
  }

  int blk_id = dbSrv.db_ops->save_blk(
      hash.begin(), height, version, prev_hash.begin(), mrkl_root.begin(), time,
      bits, nonce, blk_size, work.begin(), block.vtx.size());
  if (blk_id == -1) {
    dbSrv.db_ops->rollback();

    if (dbSrv.db_ops->begin() == -1) {
      dbSrv.db_ops->rollback();
      LogPrint("dblayer", "block save second roll back height: %d \n", height);
      return -1;
    }
    blk_id = dbSrv.db_ops->save_blk(
        hash.begin(), height, version, prev_hash.begin(), mrkl_root.begin(),
        time, bits, nonce, blk_size, work.begin(), block.vtx.size());
    if (blk_id == -1) {
      LogPrint("dblayer", "block save fail height: %d \n", height);
      dbSrv.db_ops->rollback();
      return -1;
    }
  }

  block.vtx[0].nTimeReceived = time;
  INDEX_FOREACH(idx, const CTransaction & tx, block.vtx) {
    int tx_id = dbSaveTx(tx);
    if (tx_id == -1) {
      LogPrint("dblayer", "tx save fail block height: %d,  txhash: %s \n",
               height, tx.GetHash().ToString());
      dbSrv.db_ops->rollback();
      return -1;
    }
    dbSrv.db_ops->save_blk_tx(blk_id, tx_id, idx);
  }

  dbSrv.db_ops->add_blk_statics(blk_id);
  dbSrv.db_ops->commit();

  return 0;
}

int dbAcceptTx(const CTransaction &tx) {

  if (dbSrv.db_ops->begin() == -1) {
    dbSrv.db_ops->rollback();
    LogPrint("dblayer", "dbAcceptTx roll back: %s \n", tx.GetHash().ToString());
    return -1;
  }

  int tx_id = dbSaveTx(tx);
  dbSrv.db_ops->save_utx(tx_id);
  dbSrv.db_ops->commit();

  return 0;
}

int dbRemoveTx(const CTransaction &tx) {
  uint256 hash = tx.GetHash();
  int txid = dbSrv.db_ops->query_tx(hash.begin());
  if (txid == -1) {
    LogPrint("dblayer", "dbRemoveTx: tx: %x  not in database  \n", tx.GetHash().ToString());
    return 0;
  }
  
  LogPrint("dblayer", "dbRemoveTx: tx: %x ,txid:%d in db  \n", tx.GetHash().ToString(),txid);
  if (dbSrv.db_ops->begin() == -1) {
    dbSrv.db_ops->rollback();
    LogPrint("dblayer", "dbRemoveTx roll back: %s \n", tx.GetHash().ToString());
    return -1;
  }

  dbSrv.db_ops->delete_tx(txid);

  dbSrv.db_ops->commit();
  return 0;
}

int dbSync() {
  int maxHeight = dbSrv.db_ops->query_maxHeight();
  int i = 0;

  CBlock block;
  CBlockIndex *pblockindex;

  if (maxHeight == -1)
     return -1;

  // syndb
  if (maxHeight < chainActive.Height()) {
    if (maxHeight != 0)
      i = maxHeight + 1;
    for (; i < (chainActive.Height() + 1); i++) {
      pblockindex = chainActive[i];
      int64_t nStart = GetTimeMicros();
      if (!ReadBlockFromDisk(block, pblockindex))
        return -1;
      LogPrint("dblayer", "- read block from disk: %.2fms\n",
               (GetTimeMicros() - nStart) * 0.001);
      if (dbSaveBlock(pblockindex, block) == -1)
        return -1;
      LogPrint("dblayer", "- Save block to db: %.2fms height %d\n",
               (GetTimeMicros() - nStart) * 0.001, pblockindex->nHeight);
    }
  }

  return 0;
}

int dbDisconnectBlock(const unsigned char *hash) {

  if (dbSrv.db_ops->begin() == -1) {
    dbSrv.db_ops->rollback();
    return -1;
  }
   LogPrint("dblayer", "-delete blk: %s\n", hash);
  // set blk to side chain
  dbSrv.db_ops->delete_blk(hash);

  dbSrv.db_ops->commit();
  return 0;
}


 
