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

#include <stdint.h>
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "chain.h"
  
#define HAVE_POSTGRESQL

struct SERVER_DB_OPS {
    int     (*save_blk)( unsigned char *  hash, 
                             int              height,
                             int              version, 
                             unsigned char *  prev_hash,
                             unsigned char *  mrkl_root,
                             long long        time, 
                             int              bits, 
                             int              nonce, 
                             int              blk_size, 
                             int              chain, 
                             unsigned char *  work);

    int    (*update_blk)(const unsigned char *  hash);
    int    (*save_blk_tx)(int blk_id, int tx_id, int idx);
    int    (*save_tx)(unsigned char * hash, int version, int lock_time, bool coinbase, int tx_size, unsigned char * nhash);
    int    (*save_txin)(int tx_id, int tx_idx, int prev_out_index, int sequence, const unsigned char *script_sig, int script_len, const unsigned char *prev_out, int p2sh_type);
    int    (*save_txout) (int tx_id, int idx, const unsigned char * scriptPubKey, int script_len, long long nValue, int txout_type);
    int    (*save_addr)(const char * addr, int addr_type);
    int    (*save_addr_out)(int addr_id, int txout_id);

    int    (*query_tx)(unsigned char *hash);
    int    (*delete_tx)(int txid);

    int    (*query_maxHeight)();
 
    bool    (*begin)(void);
    void    (*rollback)(void);
    void    (*commit)(void);
    bool    (*open)(void);
    void    (*close)(void);
};

enum SERVER_DB_ENGINE {
    SDB_SQLITE,
    SDB_MYSQL,
    SDB_POSTGRESQL,
};

struct DBSERVER {
    enum   SERVER_DB_ENGINE db_eng;
    struct SERVER_DB_OPS    *db_ops;

    const char      *db_name;
    const char      *db_username;
    const char      *db_password;
    const char      *db_host;
    int              db_port;
    void            *db_conn;
};

bool dbOpen();
void dbClose();
int  dbSaveBlock(const CBlockIndex* blockindex, const CBlock &block);
int  dbDisconnectBlock(const unsigned char * hash);
int  dbSaveTx(const CTransaction &tx);
int  dbAcceptTx(const CTransaction &tx);
int  dbRemoveTx(const CTransaction &tx);
int  dbSync();
bool DbSyncFinish();

#ifdef HAVE_SQLITE3
extern struct SERVER_DB_OPS sqlite_db_ops;
#endif
#ifdef HAVE_MYSQL
extern struct SERVER_DB_OPS mysql_db_ops;
#endif
#ifdef HAVE_POSTGRESQL
extern struct SERVER_DB_OPS postgresql_db_ops;
#endif
 
extern struct DBSERVER dbsrv;
