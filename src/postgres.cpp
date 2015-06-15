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

#include "dblayer.h"

#ifdef HAVE_POSTGRESQL

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <syslog.h>
#include <postgresql/libpq-fe.h>
#include "util.h"
#include <db.h>

#define PGOK(_res) ((_res) == PGRES_COMMAND_OK || \
            (_res) == PGRES_TUPLES_OK || \
            (_res) == PGRES_EMPTY_QUERY)

#define WHERE_FFL " - from %s %s() line %d"
#define WHERE_FFL_PASS __FILE__, __func__, __LINE__

#define PQ_READ true
#define PQ_WRITE false

// size to allocate for pgsql text and display (bigger than needed)
#define DATE_BUFSIZ (63+1)
#define CDATE_BUFSIZ (127+1)
#define BIGINT_BUFSIZ (63+1)
#define INT_BUFSIZ (63+1)
#define DOUBLE_BUFSIZ (63+1)

#define TXT_BIG 256
#define TXT_MED 128
#define TXT_SML 64
#define TXT_FLAG 1

enum data_type {
    TYPE_STR,
    TYPE_BIGINT,
    TYPE_INT,
    TYPE_BYTEA,
    TYPE_TV,
    TYPE_TVS,
    TYPE_CTV,
    TYPE_BLOB,
    TYPE_BOOL,
    TYPE_DOUBLE,
    TYPE_HASH,
    TYPE_SCRIPT,
    TYPE_ADDR
};

typedef struct timeval tv_t;
 
#define DEFAULT_SELECT_BLK "select id from blk where hash=$1::bytea"
#define DEFAULT_SELECT_TX "select id from tx where hash=$1::bytea"
#define DEFAULT_SELECT_UTX "select id from utx where hash=$1::bytea"

#define DEFAULT_SAVE_BLK\
    "insert into blk (hash, height, version, prev_hash, mrkl_root, time, bits, nonce, blk_size, chain, work) \
    values($1::bytea,$2,$3,$4::bytea,$5::bytea,$6::bigint,$7,$8,$9,$10,$11::bytea) RETURNING id"

#define DEFAULT_SAVE_BLK_TX\
    "insert into blk_tx (blk_id, tx_id, idx) \
    values($1,$2,$3)"

#define DEFAULT_SAVE_TX\
    "insert into tx (hash, version, lock_time, coinbase, tx_size, nhash) \
    values($1::bytea,$2,$3,$4::boolean,$5,$6::bytea)  RETURNING id"

#define DEFAULT_SAVE_TXIN\
    "insert into txin (tx_id, tx_idx, prev_out_index, sequence, script_sig, prev_out, p2sh_type) \
    values($1,$2,$3,$4,$5::bytea,$6::bytea, $7)  RETURNING id"

#define DEFAULT_SAVE_TXOUT\
    "insert into txout (tx_id, tx_idx, pk_script, value, type) \
    values($1,$2,$3::bytea,$4,$5) RETURNING id"

#define DEFAULT_SAVE_ADDR\
    "insert into addr (hash160, type) \
    values($1,$2) RETURNING id"

#define DEFAULT_SAVE_ADDR_OUT\
    "insert into addr_txout (addr_id, txout_id) \
    values($1,$2)"

#define DEFAULT_UPDATE_BLK\
    "update blk set chain=1 where hash=$1::bytea"

#define DEFAULT_SAVE_UTX\
    "insert into utx (hash, version, lock_time, coinbase, tx_size, nhash) \
    values($1::bytea,$2,$3,$4::boolean,$5,$6::bytea)  RETURNING id"

#define DEFAULT_SAVE_UTXIN\
    "insert into utxin (tx_id, tx_idx, prev_out_index, sequence, script_sig, prev_out, p2sh_type) \
    values($1,$2,$3,$4,$5::bytea,$6::bytea, $7)  RETURNING id"

#define DEFAULT_SAVE_UTXOUT\
    "insert into utxout (tx_id, tx_idx, pk_script, value, type) \
    values($1,$2,$3::bytea,$4,$5) RETURNING id"

#define DEFAULT_SAVE_UADDR\
    "insert into uaddr (hash160, type) \
    values($1,$2) RETURNING id"

#define DEFAULT_SAVE_UADDR_OUT\
    "insert into uaddr_txout (addr_id, txout_id) \
    values($1,$2)"

#define DEFAULT_DELETE_UTX    "delete from utx where id=$1"
#define DEFAULT_DELETE_UTXIN  "delete from utxin where tx_id=$1"
#define DEFAULT_DELETE_UTXOUT "delete from utxout where tx_id=$1"
#define DEFAULT_DELETE_UADDR_TXOUT "delete from uaddr_txout where txout_id in (select id from txout where tx_id=$1)"
#define DEFAULT_DELETE_ADDR_TXOUT "delete from addr_txout where txout_id in (select tx_id from blk_tx where blk_id=$1)"

const char hextbl[] = "0123456789abcdef";

extern struct DBSERVER dbSrv;

static unsigned char* bintostr(const unsigned char *   from,
                               size_t  from_length,
                               size_t *    to_length,
                               bool revert)
{
    const unsigned char *vp;
    unsigned char *rp;
    unsigned char *result;
    size_t      i;
    size_t      len;
    unsigned char c;

    /*
     * empty string has 1 char ('\0')
     */
    len = 1;

    len +=  2 * from_length;

    *to_length = len;
    rp = result = (unsigned char *) malloc(len);
    if (rp == NULL)
    {
        return NULL;
    }

    if ((from_length > 0) && (from != NULL)) {
        if (revert) {
            vp = from + from_length - 1;
            for (i = from_length; i > 0; i--, vp--)
                {
                c = *vp;

                *rp++ = hextbl[(c >> 4) & 0xF];
                *rp++ = hextbl[c & 0xF];
                }
        }
        else {
            vp = from;
            for (i = from_length; i > 0; i--, vp++)
                {
                c = *vp;

                *rp++ = hextbl[(c >> 4) & 0xF];
                *rp++ = hextbl[c & 0xF];
                }
        }
    }
    *rp = '\0';

    return result;
}    

static unsigned char* bintohex(const unsigned char *   from,
                               size_t  from_length,
                               size_t *    to_length,
                               bool revert)
{
    const unsigned char *vp;
    unsigned char *rp;
    unsigned char *result;
    size_t      i;
    size_t      len;
    size_t      bslash_len = 1;
    unsigned char c;

    /*
     * empty string has 1 char ('\0')
     */
    len = 1;

    len += bslash_len + 1 + 2 * from_length;


    *to_length = len;
    rp = result = (unsigned char *) malloc(len);
    if (rp == NULL)
    {
        return NULL;
    }

    *rp++ = '\\';
    *rp++ = 'x';

    if ((from_length > 0) && (from != NULL)) {
        if (revert) {
            vp = from + from_length - 1;
            for (i = from_length; i > 0; i--, vp--)
                {
                c = *vp;

                *rp++ = hextbl[(c >> 4) & 0xF];
                *rp++ = hextbl[c & 0xF];
                }
        }
        else {
            vp = from;
            for (i = from_length; i > 0; i--, vp++)
                {
                c = *vp;

                *rp++ = hextbl[(c >> 4) & 0xF];
                *rp++ = hextbl[c & 0xF];
                }
        }
    }
    *rp = '\0';

    return result;
}

char *data_to_buf(enum data_type typ, void *data, char *buf, size_t siz)
{
    struct tm tm;
    size_t to_len;

    if (!buf) {
        switch (typ) {
            case TYPE_STR:
            case TYPE_BLOB:
                siz = strlen((char *)data) + 1;
                break;
            case TYPE_BIGINT:
                siz = BIGINT_BUFSIZ;
                break;
            case TYPE_INT:
                siz = INT_BUFSIZ;
                break;
            case TYPE_BYTEA:
                if (siz==0) siz=32;
                break;
            case TYPE_HASH:
                siz=32;
                break;
            case TYPE_SCRIPT:
                break;
            case TYPE_ADDR:
                break;
            case TYPE_TV:
            case TYPE_TVS:
                siz = DATE_BUFSIZ;
                break;
            case TYPE_CTV:
                siz = CDATE_BUFSIZ;
                break;
            case TYPE_DOUBLE:
                siz = DOUBLE_BUFSIZ;
                break;
            case TYPE_BOOL:
                siz = 5;
                break;
            default:
                LogPrint("dblayer", "Unknown field (%d) to convert" WHERE_FFL, (int)typ, WHERE_FFL_PASS);
                break;
        }

        if ((typ!=TYPE_SCRIPT) && (typ!=TYPE_HASH) && (typ!=TYPE_ADDR) && (typ!=TYPE_BYTEA)) {
            buf = (char *)malloc(siz);
            if (!buf)
                LogPrint("dblayer", "(%d) OOM" WHERE_FFL, (int)siz, WHERE_FFL_PASS);
        }
    }

    switch (typ) {
        case TYPE_STR:
        case TYPE_BLOB:
            snprintf(buf, siz, "%s", (char *)data);
            break;
        case TYPE_BIGINT:
            snprintf(buf, siz, "%"PRId64, *((uint64_t *)data));
            break;
        case TYPE_INT:
            snprintf(buf, siz, "%"PRId32, *((uint32_t *)data));
            break;
        case TYPE_BYTEA:
            buf = (char *)bintohex((const unsigned char*)data, siz, &to_len, true);
            break;
        case TYPE_HASH:
            buf = (char *)bintohex((const unsigned char*)data, siz, &to_len, true);
            break;
        case TYPE_SCRIPT:
            buf = (char *)bintohex((const unsigned char*)data, siz, &to_len, false);
            break;
        case TYPE_ADDR:
            buf = (char *)bintostr((const unsigned char*)data, siz, &to_len, false);
            break;
        case TYPE_TV:
            gmtime_r(&(((tv_t *)data)->tv_sec), &tm);
            snprintf(buf, siz, "%d-%02d-%02d %02d:%02d:%02d.%06ld+00",
                       tm.tm_year + 1900,
                       tm.tm_mon + 1,
                       tm.tm_mday,
                       tm.tm_hour,
                       tm.tm_min,
                       tm.tm_sec,
                       (((tv_t *)data)->tv_usec));
            break;
        case TYPE_CTV:
            snprintf(buf, siz, "%ld,%ld",
                       (((tv_t *)data)->tv_sec),
                       (((tv_t *)data)->tv_usec));
            break;
        case TYPE_TVS:
            snprintf(buf, siz, "%ld", (((tv_t *)data)->tv_sec));
            break;
        case TYPE_DOUBLE:
            snprintf(buf, siz, "%f", *((double *)data));
            break;
        case TYPE_BOOL:
            snprintf(buf, siz, "%d", *((char *)data));
            break;
 
    }

    return buf;
}


void txt_to_data(enum data_type typ, char *nam, char *fld, void *data, size_t siz)
{
    char *tmp;

    switch (typ) {
        case TYPE_STR:
            // A database field being bigger than local storage is a fatal error
            if (siz < (strlen(fld)+1)) {
                LogPrint("dblayer",  "Field %s structure size %d is smaller than db %d" WHERE_FFL,
                        nam, (int)siz, (int)strlen(fld)+1, WHERE_FFL_PASS);
            }
            strcpy((char *)data, fld);
            break;
        case TYPE_BIGINT:
            if (siz != sizeof(int64_t)) {
                LogPrint("dblayer",  "Field %s bigint incorrect structure size %d - should be %d"
                        WHERE_FFL,
                        nam, (int)siz, (int)sizeof(int64_t), WHERE_FFL_PASS);
            }
            *((long long *)data) = atoll(fld);
            break;
        case TYPE_INT:
            if (siz != sizeof(int32_t)) {
                LogPrint("dblayer",  "Field %s int incorrect structure size %d - should be %d"
                        WHERE_FFL,
                        nam, (int)siz, (int)sizeof(int32_t), WHERE_FFL_PASS);
            }
            *((int32_t *)data) = atoi(fld);
            break;
        case TYPE_BLOB:
            tmp = strdup(fld);
            if (!tmp) {
                LogPrint("dblayer",  "Field %s (%d) OOM" WHERE_FFL,
                        nam, (int)strlen(fld), WHERE_FFL_PASS);
            }
            *((char **)data) = tmp;
            break;
        case TYPE_DOUBLE:
            if (siz != sizeof(double)) {
                LogPrint("dblayer",  "Field %s int incorrect structure size %d - should be %d"
                        WHERE_FFL,
                        nam, (int)siz, (int)sizeof(double), WHERE_FFL_PASS);
            }
            *((double *)data) = atof(fld);
            break;
        default:
            LogPrint("dblayer",  "Unknown field %s (%d) to convert" WHERE_FFL,
                    nam, (int)typ, WHERE_FFL_PASS);
            break;
    }
}


void print_meta_data( PGresult * result ) 
{ 
    int col; 

    LogPrint("dblayer",  "Status: %s\n", PQresStatus( PQresultStatus( result ))); 
    LogPrint("dblayer",  "Returned %d rows ", PQntuples( result )); 
    LogPrint("dblayer",  "with %d columns\n\n", PQnfields( result )); 

    LogPrint("dblayer",  "Column Type TypeMod Size Name \n" ); 

    LogPrint("dblayer",  "------ ---- ------- ---- -----------\n" ); 

    for( col =0 ; col < PQnfields( result ); col++ ) 
        { 
        LogPrint("dblayer",  "%3d %4d %7d %4d %s\n",col, 
                PQftype( result, col ), 
                PQfmod( result, col ), 
                PQfsize( result, col ), 
                PQfname( result, col )); 
        }

} 

#define MAX_PRINT_LEN 40
static char separator[MAX_PRINT_LEN+1];
void print_result_set( PGresult * result )
{ 
    int col; 
    int row; 
    int * sizes; 

    /* 
     ** Compute the size for each column 
     */ 
    sizes = (int *)calloc( PQnfields( result ), sizeof( int )); 

    for( col = 0; col < PQnfields( result ); col++ ) 
        { 
        int len = 0; 

        for( row = 0; row < PQntuples( result ); row++ ) 
            { 
            if( PQgetisnull( result, row, col )) 
                len = 0; 
            else 
                len = PQgetlength( result, row, col ); 

            if( len > sizes[col] ) 
                sizes[col] = len; 
            } 

        if(( len = strlen( PQfname( result, col ))) > sizes[col] ) 
            sizes[col] = len; 

        if( sizes[col] > MAX_PRINT_LEN ) 
            sizes[col] = MAX_PRINT_LEN; 
        } 

    /* 
     ** Print the field names. 
     */ 
    for( col = 0; col < PQnfields( result ); col++ ) 
        { 
        LogPrint("dblayer",  "%-*s ", sizes[col], PQfname( result, col )); 
        } 

    LogPrint("dblayer",  "\n" ); 

    /* 
     ** Print the separator line 
     */ 
    memset( separator, '-', MAX_PRINT_LEN ); 

    for( col = 0; col < PQnfields( result ); col++ ) 
        { 
        LogPrint("dblayer",  "%*.*s ", sizes[col], sizes[col], separator ); 
        } 

    LogPrint("dblayer",  "\n" ); 

    /* 
     ** Now loop through each of the tuples returned by 
     ** our query and print the results. 
     */ 
    for( row = 0; row < PQntuples( result ); row++ ) 
        { 
        for( col = 0; col < PQnfields( result ); col++ ) 
            { 
            if( PQgetisnull( result, row, col )) 
                LogPrint("dblayer",  "%*s", sizes[col], "" ); 
            else 
                LogPrint("dblayer",  "%*s ", sizes[col], PQgetvalue( result, row, col )); 
            } 

        LogPrint("dblayer",  "\n" ); 

        } 
    LogPrint("dblayer",  "(%d rows)\n\n", PQntuples( result )); 
    free( sizes ); 
}

void pq_print(PGresult *res)
{
  PQprintOpt options;

  //options.header = 1; /* Ask for column headers */ 
  //options.align = 1; /* Pad short columns for alignment */ 
  //options.fieldSep = "|"; /* Use a pipe as the field separator */ 
  //options.expanded= 1; 
  PQprint(stdout, res, &options );

}

static bool pg_conncheck(void)
{
    if (PQstatus((const PGconn*)(dbSrv.db_conn)) != CONNECTION_OK) {
        LogPrint("dblayer", "Connection to PostgreSQL lost: reconnecting.\n");
        PQreset((PGconn*)(dbSrv.db_conn));
        if (PQstatus((const PGconn*)(dbSrv.db_conn)) != CONNECTION_OK) {
            LogPrint("dblayer", "Reconnect attempt failed.\n");
            return false;
        }
    }
    return true;
}

static int pg_query_maxHeight()
{
    PGresult *res;
    ExecStatusType rescode;
    int height = 0;
 
    res = PQexec((PGconn*)dbSrv.db_conn, "select max(height) from blk");
    rescode = PQresultStatus(res);
    if (!PGOK(rescode)) {
        LogPrint("dblayer", "pg_query_maxHeight error: %s\n", PQerrorMessage((const PGconn*)dbSrv.db_conn));
        PQclear(res);
        return -1;
    }
    height =  atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    return height;
}

static int pg_query_blk(unsigned char *  hash)
{
    PGresult *res;
    ExecStatusType rescode;
    int i=0;
    int id=0;
    const char *paramvalues[1];
 
    paramvalues[i++] = data_to_buf(TYPE_HASH, (void *)(hash), NULL, 0);
    res = PQexecParams((PGconn*)dbSrv.db_conn, DEFAULT_SELECT_BLK, i, NULL, paramvalues, NULL, NULL, PQ_READ);

    free((char *)paramvalues[0]);

    rescode = PQresultStatus(res);
    if (!PGOK(rescode)) {
        LogPrint("dblayer", "pg_query_blk error: %s\n", PQerrorMessage((const PGconn*)dbSrv.db_conn));
        PQclear(res);
        return -1;
    }

   if (PQntuples(res)>0)
       id = ntohl(*((int *)PQgetvalue(res, 0, 0)));
    else
       id = -1;

    PQclear(res);

    return id;
}

static int pg_update_blk(const unsigned char * hash)
{
    PGresult *res;
    ExecStatusType rescode;
    int i=0;
    const char *paramvalues[1];

    paramvalues[i++] = data_to_buf(TYPE_HASH, (void *)&hash, NULL, 0);

    res = PQexecParams((PGconn*)dbSrv.db_conn, DEFAULT_UPDATE_BLK, i, NULL, paramvalues, NULL, NULL, PQ_READ);
    rescode = PQresultStatus(res);
    if (!PGOK(rescode)) {
        LogPrint("dblayer", "pg_update_blk error: %s\n", PQerrorMessage((const PGconn*)dbSrv.db_conn));
        free((char *)paramvalues[0]);
        PQclear(res);
        return -1;
    }
                                                
    free((char *)paramvalues[0]);
    PQclear(res);

    return 0;
}

static int pg_save_blk(unsigned char *  hash, 
                       int              height,
                       int              version, 
                       unsigned char *  prev_hash,
                       unsigned char *  mrkl_root,
                       long long        time, 
                       int              bits, 
                       int              nonce, 
                       int              blk_size, 
                       int              chain, 
                       unsigned char *  work)
{
    PGresult *res;
    ExecStatusType rescode;
    int i=0;
    int n=0;
    int id=0;
    const char *paramvalues[11];

    //check if block in database
    if (pg_query_blk(hash)>0)
       {
       LogPrint("dblayer", "pg_save_blk : block %d exists in database.\n", height);
       return -1; 
       }

    /* PG does a fine job with timestamps so we won't bother. */

    paramvalues[i++] = data_to_buf(TYPE_BYTEA, (void *)(hash), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&height), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&version), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_BYTEA, (void *)(prev_hash), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_BYTEA, (void *)(mrkl_root), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_BIGINT, (void *)(&time), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&bits), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&nonce), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&blk_size), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&chain), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_BYTEA, (void *)(work), NULL, 0);

    res = PQexecParams((PGconn*)dbSrv.db_conn, DEFAULT_SAVE_BLK, i, NULL, paramvalues, NULL, NULL, false);

    for (n = 0; n < i; n++)
        free((char *)paramvalues[n]);

    rescode = PQresultStatus(res);
    if (!PGOK(rescode)) {
        LogPrint("dblayer", "pg_save_blk failed: %s", PQerrorMessage((const PGconn*)dbSrv.db_conn));
        PQclear(res);
        return -1;
    }
    id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    return id;

}

int pg_save_blk_tx(int blk_id, int tx_id, int idx)
{
    PGresult *res;
    ExecStatusType rescode;
    int i=0;
    int n=0;
    const char *paramvalues[3];

    /* PG does a fine job with timestamps so we won't bother. */

    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&blk_id), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&tx_id), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&idx), NULL, 0);

    res = PQexecParams((PGconn*)dbSrv.db_conn, DEFAULT_SAVE_BLK_TX, i, NULL, paramvalues, NULL, NULL, false);

    for (n = 0; n < i; n++)
        free((char *)paramvalues[n]);

    rescode = PQresultStatus(res);
    if (!PGOK(rescode)) {
        LogPrint("dblayer",  "pg_save_blk_tx failed: %s", PQerrorMessage((const PGconn*)dbSrv.db_conn));
        PQclear(res);
        return -1;
    }

    PQclear(res);
    return 0;
}

int pg_save_tx(unsigned char * hash, int version, int lock_time, bool coinbase, int tx_size, unsigned char * nhash)
{
     PGresult *res;
    ExecStatusType rescode;
    int i = 0;
    int n =0;
    int id=0;
    const char *paramvalues[6];

    /* PG does a fine job with timestamps so we won't bother. */

    paramvalues[i++] = data_to_buf(TYPE_BYTEA, (void *)(hash), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&version), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&lock_time), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_BOOL, (void *)(&coinbase), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&tx_size), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_BYTEA, (void *)(nhash), NULL, 0);

    res = PQexecParams((PGconn*)dbSrv.db_conn, DEFAULT_SAVE_TX, i, NULL, paramvalues, NULL, NULL, false);

    for (n = 0; n < i; n++)
        free((char *)paramvalues[n]);

    rescode = PQresultStatus(res);
    if (!PGOK(rescode)) {
        LogPrint("dblayer",  "pg_save_tx failed: %s", PQerrorMessage((const PGconn*)dbSrv.db_conn));
        PQclear(res);
        return -1;
    }

    id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    return id;
}

int pg_save_txin(int tx_id, int tx_idx, int prev_out_index, int sequence, const unsigned char *script_sig, int script_len, const unsigned char *prev_out, int p2sh_type)
{
     PGresult *res;
    ExecStatusType rescode;
    int i=0;
    int n =0;
    int id=0;
    const char *paramvalues[7];

    /* PG does a fine job with timestamps so we won't bother. */

    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&tx_id), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&tx_idx), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&prev_out_index), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&sequence), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_SCRIPT, (void *)(script_sig), NULL, script_len);
    paramvalues[i++] = data_to_buf(TYPE_BYTEA, (void *)(prev_out), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&p2sh_type), NULL, 0);

    res = PQexecParams((PGconn*)dbSrv.db_conn, DEFAULT_SAVE_TXIN, i, NULL, paramvalues, NULL, NULL, false);

    for (n = 0; n < i; n++)
        free((char *)paramvalues[n]);
    
    rescode = PQresultStatus(res);
    if (!PGOK(rescode)) {
        LogPrint("dblayer",  "pg_save_txin failed: %s", PQerrorMessage((const PGconn*)dbSrv.db_conn));
        PQclear(res);
        return -1;
    }

    id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    return id;
}

int pg_save_txout (int tx_id, int idx, const unsigned char * scriptPubKey, int script_len, long long nValue, int txout_type)
{
     PGresult *res;
    ExecStatusType rescode;
    int i=0;
    int n =0;
    int id=0;
    const char *paramvalues[5];

    /* PG does a fine job with timestamps so we won't bother. */

    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&tx_id), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&idx), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_SCRIPT, (void *)(scriptPubKey), NULL, script_len);
    paramvalues[i++] = data_to_buf(TYPE_BIGINT, (void *)(&nValue), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&txout_type), NULL, 0);

    res = PQexecParams((PGconn*)dbSrv.db_conn, DEFAULT_SAVE_TXOUT, i, NULL, paramvalues, NULL, NULL, false);

    for (n = 0; n < i; n++)
        free((char *)paramvalues[n]);
 
    rescode = PQresultStatus(res);
    if (!PGOK(rescode)) {
        LogPrint("dblayer",  "pg_save_txout failed: %s", PQerrorMessage((const PGconn*)dbSrv.db_conn));
        PQclear(res);
        return -1;
    }

    id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    return id;
}

int pg_save_addr(const char * addr, int addr_type)
{
    PGresult *res;
    ExecStatusType rescode;
    int i=0;
    int n =0;
    int id=0;
    const char *paramvalues[2];

    /* PG does a fine job with timestamps so we won't bother. */

    paramvalues[i++] = data_to_buf(TYPE_ADDR, (void *)(addr), NULL, 20);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&addr_type), NULL, 0);

    res = PQexecParams((PGconn*)dbSrv.db_conn, DEFAULT_SAVE_ADDR, i, NULL, paramvalues, NULL, NULL, false);

    for (n = 0; n < i; n++)
        free((char *)paramvalues[n]);

    rescode = PQresultStatus(res);
    if (!PGOK(rescode)) {
        LogPrint("dblayer",  "pg_save_addr failed: %s", PQerrorMessage((const PGconn*)dbSrv.db_conn));
        PQclear(res);
        return -1;
    }

    id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    return id;
}

static int pg_query_utx(unsigned char *  hash)
{
    PGresult *res;
    ExecStatusType rescode;
    int i=0;
    int id=0;
    const char *paramvalues[1];
 
    paramvalues[i++] = data_to_buf(TYPE_HASH, (void *)(hash), NULL, 0);
    res = PQexecParams((PGconn*)dbSrv.db_conn, DEFAULT_SELECT_UTX, i, NULL, paramvalues, NULL, NULL, 1);
    free((char *)paramvalues[0]);

    //res = PQexec((PGconn*)dbSrv.db_conn, "select id from utx where hash='\\x707145792a2806858e73cb7734319dc52a28c7f3437e8d2bbf67c769f857fd41'");

    rescode = PQresultStatus(res);
    if (!PGOK(rescode)) {
        LogPrint("dblayer", "pg_query_tx error: %s\n", PQerrorMessage((const PGconn*)dbSrv.db_conn));
        PQclear(res);
        return -1;
    }

   if (PQntuples(res)>0)
       id = ntohl(*((int *)PQgetvalue(res, 0, 0)));
    else
       id = -1;

    PQclear(res);

    return id;
}

static int pg_delete_utx(int txid)
{
    PGresult *res;
    ExecStatusType rescode;
    int i=0;
    const char *paramvalues[1];

    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)&txid, NULL, 0);
                                                
    res = PQexecParams((PGconn*)dbSrv.db_conn, DEFAULT_DELETE_UADDR_TXOUT, i, NULL, paramvalues, NULL, NULL, PQ_READ);
    rescode = PQresultStatus(res);
    if (!PGOK(rescode)) {
        LogPrint("dblayer", "pg_delete_uaddr_txout error: %s\n", PQerrorMessage((const PGconn*)dbSrv.db_conn));
        free((char *)paramvalues[0]);
        PQclear(res);
        return -1;
    }

    res = PQexecParams((PGconn*)dbSrv.db_conn, DEFAULT_DELETE_UTXIN, i, NULL, paramvalues, NULL, NULL, PQ_READ);
    rescode = PQresultStatus(res);
    if (!PGOK(rescode)) {
        LogPrint("dblayer", "pg_delete_utxin error: %s\n", PQerrorMessage((const PGconn*)dbSrv.db_conn));
        free((char *)paramvalues[0]);
        PQclear(res);
        return -1;
    }

    res = PQexecParams((PGconn*)dbSrv.db_conn, DEFAULT_DELETE_UTXOUT, i, NULL, paramvalues, NULL, NULL, PQ_READ);
    rescode = PQresultStatus(res);
    if (!PGOK(rescode)) {
        LogPrint("dblayer", "pg_delete_utxout error: %s\n", PQerrorMessage((const PGconn*)dbSrv.db_conn));
        free((char *)paramvalues[0]);
        PQclear(res);
        return -1;
    }

    res = PQexecParams((PGconn*)dbSrv.db_conn, DEFAULT_DELETE_UTX, i, NULL, paramvalues, NULL, NULL, PQ_READ);
    rescode = PQresultStatus(res);
    if (!PGOK(rescode)) {
        LogPrint("dblayer", "pg_delete_utx error: %s\n", PQerrorMessage((const PGconn*)dbSrv.db_conn));
        free((char *)paramvalues[0]);
        PQclear(res);
        return -1;
    }

    free((char *)paramvalues[0]);
    PQclear(res);

    return 0;
}   

int pg_save_addr_out(int addr_id, int txout_id)
{
    PGresult *res;
    ExecStatusType rescode;
    int i=0;
    int n=0;
    const char *paramvalues[2];

    /* PG does a fine job with timestamps so we won't bother. */

    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&addr_id), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&txout_id), NULL, 0);

    res = PQexecParams((PGconn*)dbSrv.db_conn, DEFAULT_SAVE_ADDR_OUT, i, NULL, paramvalues, NULL, NULL, false);

    for (n = 0; n < i; n++)
        free((char *)paramvalues[n]);

    rescode = PQresultStatus(res);
    if (!PGOK(rescode)) {
        LogPrint("dblayer",  "pg_save_addr_out failed: %s", PQerrorMessage((const PGconn*)dbSrv.db_conn));
        PQclear(res);
        return -1;
    }
    PQclear(res);

    return 0;
}


int pg_save_utx(unsigned char * hash, int version, int lock_time, bool coinbase, int tx_size, unsigned char * nhash)
{
    PGresult *res;
    ExecStatusType rescode;
    int i = 0;
    int n =0;
    int id=0;
    const char *paramvalues[6];

    /* PG does a fine job with timestamps so we won't bother. */

    paramvalues[i++] = data_to_buf(TYPE_BYTEA, (void *)(hash), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&version), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&lock_time), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_BOOL, (void *)(&coinbase), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&tx_size), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_BYTEA, (void *)(nhash), NULL, 0);

    res = PQexecParams((PGconn*)dbSrv.db_conn, DEFAULT_SAVE_UTX, i, NULL, paramvalues, NULL, NULL, false);

    for (n = 0; n < i; n++)
        free((char *)paramvalues[n]);

    rescode = PQresultStatus(res);
    if (!PGOK(rescode)) {
        LogPrint("dblayer",  "pg_save_utx failed: %s", PQerrorMessage((const PGconn*)dbSrv.db_conn));
        PQclear(res);
        return -1;
    }

    id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    return id;
}

int pg_save_utxin(int tx_id, int tx_idx, int prev_out_index, int sequence, const unsigned char *script_sig, int script_len, const unsigned char *prev_out, int p2sh_type)
{
    PGresult *res;
    ExecStatusType rescode;
    int i=0;
    int n =0;
    int id=0;
    const char *paramvalues[7];

    /* PG does a fine job with timestamps so we won't bother. */

    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&tx_id), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&tx_idx), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&prev_out_index), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&sequence), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_SCRIPT, (void *)(script_sig), NULL, script_len);
    paramvalues[i++] = data_to_buf(TYPE_BYTEA, (void *)(prev_out), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&p2sh_type), NULL, 0);

    res = PQexecParams((PGconn*)dbSrv.db_conn, DEFAULT_SAVE_UTXIN, i, NULL, paramvalues, NULL, NULL, false);

    for (n = 0; n < i; n++)
        free((char *)paramvalues[n]);
    
    rescode = PQresultStatus(res);
    if (!PGOK(rescode)) {
        LogPrint("dblayer",  "pg_save_utxin failed: %s", PQerrorMessage((const PGconn*)dbSrv.db_conn));
        PQclear(res);
        return -1;
    }

    id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    return id;
}

int pg_save_utxout (int tx_id, int idx, const unsigned char * scriptPubKey, int script_len, long long nValue, int txout_type)
{
     PGresult *res;
    ExecStatusType rescode;
    int i=0;
    int n =0;
    int id=0;
    const char *paramvalues[5];

    /* PG does a fine job with timestamps so we won't bother. */

    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&tx_id), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&idx), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_SCRIPT, (void *)(scriptPubKey), NULL, script_len);
    paramvalues[i++] = data_to_buf(TYPE_BIGINT, (void *)(&nValue), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&txout_type), NULL, 0);

    res = PQexecParams((PGconn*)dbSrv.db_conn, DEFAULT_SAVE_UTXOUT, i, NULL, paramvalues, NULL, NULL, false);

    for (n = 0; n < i; n++)
        free((char *)paramvalues[n]);
 
    rescode = PQresultStatus(res);
    if (!PGOK(rescode)) {
        LogPrint("dblayer",  "pg_save_utxout failed: %s", PQerrorMessage((const PGconn*)dbSrv.db_conn));
        PQclear(res);
        return -1;
    }

    id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    return id;
}

int pg_save_uaddr(const char * addr, int addr_type)
{
    PGresult *res;
    ExecStatusType rescode;
    int i=0;
    int n =0;
    int id=0;
    const char *paramvalues[2];

    /* PG does a fine job with timestamps so we won't bother. */

    paramvalues[i++] = data_to_buf(TYPE_ADDR, (void *)(addr), NULL, 20);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&addr_type), NULL, 0);

    res = PQexecParams((PGconn*)dbSrv.db_conn, DEFAULT_SAVE_UADDR, i, NULL, paramvalues, NULL, NULL, false);

    for (n = 0; n < i; n++)
        free((char *)paramvalues[n]);

    rescode = PQresultStatus(res);
    if (!PGOK(rescode)) {
        LogPrint("dblayer",  "pg_save_uaddr failed: %s", PQerrorMessage((const PGconn*)dbSrv.db_conn));
        PQclear(res);
        return -1;
    }

    id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    return id;

}

int pg_save_uaddr_out(int addr_id, int txout_id)
{
    PGresult *res;
    ExecStatusType rescode;
    int i=0;
    int n=0;
    const char *paramvalues[2];

    /* PG does a fine job with timestamps so we won't bother. */

    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&addr_id), NULL, 0);
    paramvalues[i++] = data_to_buf(TYPE_INT, (void *)(&txout_id), NULL, 0);

    res = PQexecParams((PGconn*)dbSrv.db_conn, DEFAULT_SAVE_UADDR_OUT, i, NULL, paramvalues, NULL, NULL, false);

    for (n = 0; n < i; n++)
        free((char *)paramvalues[n]);

    rescode = PQresultStatus(res);
    if (!PGOK(rescode)) {
        LogPrint("dblayer",  "pg_save_uaddr_out failed: %s", PQerrorMessage((const PGconn*)dbSrv.db_conn));
        PQclear(res);
        return -1;
    }
    PQclear(res);

    return 0;
}

static void pg_rollback(void)
{
    PGresult *res;
    res = PQexec((PGconn*)dbSrv.db_conn, "Rollback");
    PQclear(res); 
}

static bool pg_begin(void)
{
    PGresult *res;

     /*
     * check to see that the backend connection was successfully made
     */
    if (!pg_conncheck())
        return false;

    /* debug = fopen("/tmp/trace.out","w"); */
    /* PQtrace(conn, debug);  */

    /* start a transaction block */
    res = PQexec((PGconn*)dbSrv.db_conn, "BEGIN");
    if (!res || PQresultStatus(res) != PGRES_COMMAND_OK)
    {
        fprintf(stderr, "BEGIN command failed\n");
        PQclear(res);
        return false;
    } 

    /*
     * should PQclear PGresult whenever it is no longer needed to avoid
     * memory leaks
     */
    PQclear(res);
   
    return true;
}

static void pg_commit(void)
{
    PGresult *res;
    res = PQexec((PGconn*)dbSrv.db_conn, "Commit");
    PQclear(res);
}

static void pg_close(void)
{
    PQfinish((PGconn*)dbSrv.db_conn);
}

static bool pg_open(void)
{
    char *portstr = NULL;
    if (dbSrv.db_port > 0)
        if (asprintf(&portstr, "%d", dbSrv.db_port) < 0)
            return false;
    dbSrv.db_conn = (void *)PQsetdbLogin(dbSrv.db_host, portstr, NULL, NULL,
                  dbSrv.db_name, dbSrv.db_username,
                  dbSrv.db_password);
    free(portstr);
    if (PQstatus((const PGconn*)dbSrv.db_conn) != CONNECTION_OK) {
        LogPrint("dblayer",  "failed to connect to postgresql: %s",
               PQerrorMessage((const PGconn*)dbSrv.db_conn));
        pg_close();
        return false;
    }

    if (PQsetClientEncoding((PGconn*)dbSrv.db_conn, "UTF-8"))
    {
        LogPrint("dblayer",  "failed to connect to postgresql: %s",
               PQerrorMessage((const PGconn*)dbSrv.db_conn));
        pg_close();
        return false;
    }

    LogPrint("dblayer",  "Connect to postgresql\n");
    return true;
}

struct SERVER_DB_OPS postgresql_db_ops = {
    .save_blk       = pg_save_blk,
    .update_blk     = pg_update_blk,
    .save_blk_tx    = pg_save_blk_tx,
    .save_tx        = pg_save_tx,
    .save_txin      = pg_save_txin,
    .save_txout     = pg_save_txout,
    .save_addr      = pg_save_addr,   
    .save_addr_out  = pg_save_addr_out,
    .save_utx       = pg_save_utx,
    .save_utxin     = pg_save_utxin,
    .save_utxout    = pg_save_utxout,
    .save_uaddr     = pg_save_uaddr,   
    .save_uaddr_out = pg_save_uaddr_out,
    .query_utx      = pg_query_utx,
    .delete_utx     = pg_delete_utx,

    .query_maxHeight = pg_query_maxHeight,

    .begin           = pg_begin,
    .rollback        = pg_rollback,
    .commit          = pg_commit,

    .open            = pg_open,
    .close           = pg_close,
};

#endif /* HAVE_POSTGRESQL */
