#!/usr/bin/env python

# Test save/restore memory pool functionality


import os
import sys
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "python-bitcoinrpc"))

import json
import shutil
import subprocess
import tempfile
import traceback

from bitcoinrpc.authproxy import AuthServiceProxy, JSONRPCException
from test_framework.test_framework import BitcoinTestFramework 
from test_framework.util import *


class MempoolSaveTest(BitcoinTestFramework):

    def setup_network(self):
        args = ["-checkmempool", "-debug=mempool"]
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir, args))
        self.nodes.append(start_node(1, self.options.tmpdir, args))
        connect_nodes(self.nodes[1], 0)
        self.is_network_split = False
        self.sync_all()
 
    def run_test(nodes, test_dir):
        # Three transactions for the memory pools. Each node gets a send-to-self
        # (which only they care about, and will restore to memory pool from wallet
        # at startup) and sends back-and-forth:
        txid1 = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 0.11)
        txid2 = self.nodes[1].sendtoaddress(self.nodes[1].getnewaddress(), 0.21)
        txid3 = self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 0.31)
        txid4 = self.nodes[1].sendtoaddress(self.nodes[0].getnewaddress(), 0.41)
    
        # wait for mempools to sync
        sync_mempools(nodes)
        assert_equal(len(nodes[0].getrawmempool()), 4)
        assert_equal(len(nodes[1].getrawmempool()), 4)
        
        # restart nodes; their memory pools should restore on restart.
        stop_nodes(nodes)
        wait_bitcoinds()
        nodes[:] = start_nodes(2, test_dir)
        assert_equal(len(nodes[0].getrawmempool()), 4)
        assert_equal(len(nodes[1].getrawmempool()), 4)
    
    def add_options(self, parser):
        parser = optparse.OptionParser(usage="%prog [options]")
        parser.add_option("--nocleanup", dest="nocleanup", default=False, action="store_true",
                          help="Leave bitcoinds and test.* datadir on exit or error")
        parser.add_option("--srcdir", dest="srcdir", default="../../src",
                          help="Source directory containing bitcoind/bitcoin-cli (default: %default%)")
        parser.add_option("--tmpdir", dest="tmpdir", default=tempfile.mkdtemp(prefix="test"),
                          help="Root directory for datadirs")

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain(self.options.tmpdir)
   

if __name__ == '__main__':
    MempoolSaveTest().main()
