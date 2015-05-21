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
#include <stdlib.h>
#include "version.h"
#include "serialize.h"
#include "util.h"
#include "script/standard.h"
#include "dblayer.h"

#include "sync.h"
#include "util.h"

#include <stdint.h>
 
#include <boost/foreach.hpp>

using namespace std;

#define INDEX_FOREACH(index,a,b)                            \
    for(unsigned int index = static_cast<unsigned int>(-1); \
        index == static_cast<unsigned int>(-1);)            \
            BOOST_FOREACH(a,b) if(++index,true)

struct DBSERVER dbSrv = {
#if defined(HAVE_SQLITE3)
    .db_eng     = SDB_SQLITE,
    .db_ops     = &sqlite_db_ops,
    .db_name    = "bitcoin.db",
#elif defined(HAVE_MYSQL)
    .db_eng     = SDB_MYSQL,
    .db_ops     = &mysql_db_ops,
    .db_name    = "nodes",
    .db_username = "nodes",
    .db_password = "nodes",
    .db_host= "127.0.0.1",
    .db_port= 3306,
#elif defined(HAVE_POSTGRESQL)
    .db_eng     = SDB_POSTGRESQL,
    .db_ops     = &postgresql_db_ops,
    .db_name    = "bitcoin",
    .db_username = "postgres",
    .db_password = "",
    .db_host= "127.0.0.1",
    .db_port= 5432,
#endif
};
 
bool dbOpen()
{
    if (!dbSrv.db_ops->open())                                                                                            
        printf("\n db open fail!\n");
        return false;
    return true;

}

void dbClose()
{
    dbSrv.db_ops->close();                                                                                            
}

int dbSaveTx(const CTransaction &tx)
{
  uint256 hash = tx.GetHash();
  const int32_t version = tx.nVersion;
  const uint32_t lock_time = tx.nLockTime;
  bool coinbase = tx.IsCoinBase();
  unsigned int tx_size = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
  uint256 nhash = 0;

  int tx_id = dbSrv.db_ops->sql_save_tx(hash.begin(), version, lock_time, coinbase, tx_size, nhash.begin());

  INDEX_FOREACH(tx_idx, const CTxIn&txin, tx.vin) {
        CTransaction txp;
        uint256 hashBlockp = 0;
        int prev_out_index = txin.prevout.n;
        uint256 prev_out = txin.prevout.hash;
        int p2sh_type = 0;
             
        if (GetTransaction(txin.prevout.hash, txp, hashBlockp, true))
            {
            const CTxOut& txoutp =  txp.vout[txin.prevout.n];
            CScript script_sig = txoutp.scriptPubKey;
            }
        dbSrv.db_ops->sql_save_txin(tx_id, tx_idx, prev_out_index, txin.nSequence, &txin.scriptSig[0], prev_out.begin(),p2sh_type);
  }

  for (unsigned int i = 0; i < tx.vout.size(); i++) {
      const CTxOut& txout = tx.vout[i];
      
      txnouttype txout_type;
      vector<CTxDestination> addresses;
      int nRequired;

      if (!ExtractDestinations(txout.scriptPubKey, txout_type, addresses, nRequired)) {
          LogPrint("dbsql", "save txout  error: \n");
      }

      int txout_id = dbSrv.db_ops->sql_save_txout(tx_id, i, &txout.scriptPubKey[0], txout.nValue, txout_type);

      BOOST_FOREACH(const CTxDestination& addr, addresses)
          {
          int addr_type = 0;
          int addr_id = dbSrv.db_ops->sql_save_addr(CBitcoinAddress(addr).ToString().c_str() ,addr_type);
          dbSrv.db_ops->sql_save_addr_out(addr_id,txout_id);
          }
  }
  return tx_id;
}  


int dbSaveBlock(const CBlock &block)
{
  uint256 hash = block.GetHash();
  uint256 prev_hash = 0;

  if (mapBlockIndex.count(hash) == 0)
      {
      LogPrint("dbsql", "save blk  error: %s\n",  block.GetHash().GetHex());
      return -1;
      }

  CBlockIndex* blockindex = mapBlockIndex[hash];

  if (blockindex->pprev)
      prev_hash = blockindex->pprev->GetBlockHash();

  int blk_size = (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
  int height = blockindex->nHeight;
  int version = block.nVersion;
  uint256 mrkl_root = block.hashMerkleRoot;
  uint64_t time = block.GetBlockTime();
  int nonce = block.nNonce;
  int bits = block.nBits;
  uint256 work = blockindex->nChainWork;

  int blk_id = dbSrv.db_ops->sql_save_blk( hash.begin(), height, version, prev_hash.begin(), mrkl_root.begin(), time, bits, nonce, blk_size, 0, work.begin());

  INDEX_FOREACH(idx, const CTransaction&tx, block.vtx) {
      int tx_id = dbSaveTx(tx);
      dbSrv.db_ops->sql_save_blk_tx(blk_id, tx_id, idx);
      }
 
  return 0; 
}

int dbSaveUTx(const CTransaction &tx)
{
  uint256 hash = tx.GetHash();
  const int32_t version = tx.nVersion;
  const uint32_t lock_time = tx.nLockTime;
  bool coinbase = tx.IsCoinBase();
  unsigned int tx_size = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
  uint256 nhash = 0;

  int tx_id = dbSrv.db_ops->sql_save_utx(hash.begin(), version, lock_time, coinbase, tx_size, nhash.begin());

  INDEX_FOREACH(tx_idx, const CTxIn&txin, tx.vin) {
        CTransaction txp;
        uint256 hashBlockp = 0;
        int prev_out_index = txin.prevout.n;
        uint256 prev_out = txin.prevout.hash;
        int p2sh_type = 0;
             
        if (GetTransaction(txin.prevout.hash, txp, hashBlockp, true))
            {
            const CTxOut& txoutp =  txp.vout[txin.prevout.n];
            CScript script_sig = txoutp.scriptPubKey;
            }
        dbSrv.db_ops->sql_save_utxin(tx_id, tx_idx, prev_out_index, txin.nSequence, &txin.scriptSig[0], prev_out.begin(),p2sh_type);
  }

  for (unsigned int i = 0; i < tx.vout.size(); i++) {
      const CTxOut& txout = tx.vout[i];
      
      txnouttype txout_type;
      vector<CTxDestination> addresses;
      int nRequired;

      if (!ExtractDestinations(txout.scriptPubKey, txout_type, addresses, nRequired)) {
          LogPrint("dbsql", "save txout  error: \n");
      }

      int txout_id = dbSrv.db_ops->sql_save_utxout(tx_id, i, &txout.scriptPubKey[0], txout.nValue, txout_type);

      BOOST_FOREACH(const CTxDestination& addr, addresses)
          {
          int addr_type = 0;
          int addr_id = dbSrv.db_ops->sql_save_uaddr(CBitcoinAddress(addr).ToString().c_str() ,addr_type);
          dbSrv.db_ops->sql_save_uaddr_out(addr_id,txout_id);
          }
  }
  return tx_id;
}

int dbRemoveUTx(const CTransaction &tx)
{
    return 0;
}
 
int dbRemoveTx(const CTransaction &tx)
    {
    return 0;
    }
 

int dbRemoveBlock(const CBlock &blk)
    {
    return 0;
    }
