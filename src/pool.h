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

enum POOL_TYPE {
    POOL_UNKNOWN = 0,
    POOL_MEGABIGPOWER = 1,
    POOL_BTCC_POOL = 2,
    POOL_HUOBI = 3,
    POOL_POLMINE = 4,
    POOL_8BAOCHI = 11,
    POOL_F2POOL = 12,
    POOL_TELCO_214 = 13,
    POOL_DIGITALX_MINTSY = 14,
    POOL_BITSOLO = 15,
    POOL_COINTERRA = 16,
    POOL_ECLIPSEMC = 17,
    POOL_21_INC = 18,
    POOL_BTC_NUGGETS = 21,
    POOL_MYBTCCOIN_POOL = 22,
    POOL_BTC_POOL_PARTY = 23,
    POOL_GHASH_IO = 25,
    POOL_CLOUDHASHING = 26,
    POOL_BW_COM = 27,
    POOL_DIGITALBTC = 28,
    POOL_BITMINTER = 29,
    POOL_BITFURY = 31,
    POOL_BITCLUB_NETWORK = 35,
    POOL_BITALO = 36,
    POOL_TBDICE = 37,
    POOL_SLUSH = 38,
    POOL_ANTPOOL = 47,
    POOL_MMPOOL = 49,
    POOL_OZCOIN = 50,
    POOL_BITCOIN_AFFILIATE_NETWORK = 54,
    POOL_TRIPLEMINING = 57,
    POOL_GIVE_ME_COINS = 58,
    POOL_BTC_GUILD = 59,
    POOL_SOLO_CKPOOL = 60,
    POOL_MT_RED = 64,
    POOL_NMCBIT = 65,
    POOL_175BTC = 67,
    POOL_ASICMINER = 69,
    POOL_SIMPLECOIN_US = 70,
    POOL_50BTC = 71,
    POOL_BITPARKING = 72,
    POOL_ST_MINING_CORP = 74,
    POOL_BCPOOL_IO = 78,
    POOL_KANO_CKPOOL = 79,
    POOL_ELIGIUS = 81,
    POOL_MULTICOIN_CO = 84,
    POOL_BTCSERV = 86,
    POOL_NICEHASH_SOLO = 87,
    POOL_MAXBTC = 88,
    POOL_COINLAB = 91,
    POOL_YOURBTC_NET = 93,
    POOL_KNCMINER = 94,
    POOL_HHTT = 95,
    POOL_NODE_STRATUM_POOL = 96,
    POOL_STRATUM_POOL = 97,
    POOL_P2POOL = 98,
    POOL_CKPOOL_DEVASTRA = 99,
    POOL_MULTIPOOL = 100, //https://www.multipool.us/
    POOL_MYBTCCOIN = 101, //http://mybtccoin.com
    POOL_BTCDIG= 102,  //https://btcdig.com/stats/blocks/
    POOL_WIZKID057 = 103,
    POOL_2AV0ID51PCT=104,
    POOL_HASHPOOL=105,  //"http://hashpool.com/"
    POOL_TRANSACTIONCOINMINING = 106, //"http://sha256.transactioncoinmining.com/"
    POOL_A_XBT = 107, //"http://www.a-xbt.com/"
    POOL_NEXIOUS = 108, //https://nexious.com/
    POOL_BRAVO =109,    //http://www.bravo-mining.com/
    POOL_TRICKYS =110,  //http://pool.wemine.uk/
    POOL_HOTPOOL =111,  //http://hotpool.co/
    POOL_BTCMP = 112,   //https://www.btcmp.com/
    POOL_CKPOOL = 200,
};
 
enum BIP_TYPE {
    BIP_DEFAULT=1,
    BIP_8M=2,
    BIP_100=3,
    BIP_101_8M=4,
    BIP_101_2M=5,
    BIP_248=6,
};
 
int getPoolIdByPrefix(const unsigned char *coinbase, int coinbaseLen);
int getPoolIdByAddr(const char *addr);
int getPoolSupportBip(const unsigned char *coinbase, int coinbaseLen, int version);

