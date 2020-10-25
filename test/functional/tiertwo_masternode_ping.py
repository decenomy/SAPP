#!/usr/bin/env python3
# Copyright (c) 2020 The PIVX developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import PivxTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    connect_nodes_clique,
    disconnect_nodes,
    Decimal,
    p2p_port,
    satoshi_round,
    sync_blocks,
    wait_until,
)

import os
import time

"""
Test checking masternode ping thread
Does not use functions of PivxTier2TestFramework as we don't want to send
pings on demand. Here, instead, mocktime is disabled, and we just wait with
time.sleep to verify that masternodes send pings correctly.
"""

class MasternodePingTest(PivxTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        # 0=miner 1=mn_owner 2=mn_remote
        self.num_nodes = 3


    def run_test(self):
        miner = self.nodes[0]
        owner = self.nodes[1]
        remote = self.nodes[2]
        mnPrivkey = "9247iC59poZmqBYt9iDh9wDam6v9S1rW5XekjLGyPnDhrDkP4AK"

        self.log.info("generating 141 blocks...")
        miner.generate(141)
        sync_blocks(self.nodes)

        # Create collateral
        self.log.info("funding masternode controller...")
        masternodeAlias = "mnode"
        mnAddress = owner.getnewaddress(masternodeAlias)
        collateralTxId = miner.sendtoaddress(mnAddress, Decimal('10000'))
        miner.generate(1)
        sync_blocks(self.nodes)
        assert_equal(owner.getbalance(), Decimal('10000'))
        assert_greater_than(owner.getrawtransaction(collateralTxId, 1)["confirmations"], 0)

        # Setup controller
        self.log.info("controller setup...")
        o = owner.getmasternodeoutputs()
        assert_equal(len(o), 1)
        assert_equal(o[0]["txhash"], collateralTxId)
        vout = o[0]["outputidx"]
        self.log.info("collateral accepted for "+ masternodeAlias +".. updating masternode.conf and stopping the node")
        confData = masternodeAlias + " 127.0.0.1:" + str(p2p_port(2)) + " " + \
                   str(mnPrivkey) +  " " + str(collateralTxId) + " " + str(vout)
        destPath = os.path.join(self.options.tmpdir, "node1", "regtest", "masternode.conf")
        with open(destPath, "a+") as file_object:
            file_object.write("\n")
            file_object.write(confData)

        # Init remote
        self.log.info("initializing remote masternode...")
        remote.initmasternode(mnPrivkey, "127.0.0.1:" + str(p2p_port(2)))

        # Wait until mnsync is complete (max 30 seconds)
        self.log.info("waiting complete mnsync...")
        synced = [False] * 3
        timeout = time.time() + 30
        while time.time() < timeout:
            for i in range(3):
                if not synced[i]:
                    synced[i] = (self.nodes[i].mnsync("status")["RequestedMasternodeAssets"] == 999)
            if synced != [True] * 3:
                time.sleep(1)
        if synced != [True] * 3:
            raise AssertionError("Unable to complete mnsync: %s" % str(synced))

        # Send Start message
        self.log.info("sending masternode broadcast...")
        self.controller_start_masternode(owner, masternodeAlias)
        miner.generate(1)
        sync_blocks(self.nodes)
        time.sleep(1)

        # Wait until masternode is enabled anywhere (max 100 secs)
        self.log.info("waiting till masternode gets enabled...")
        enabled = [""] * 3
        timeout = time.time() + 100
        while time.time() < timeout:
            for i in range(3):
                if enabled[i] != "ENABLED":
                    enabled[i] = self.get_mn_status(self.nodes[i], collateralTxId)
            if enabled != ["ENABLED"] * 3:
                time.sleep(1)
        if enabled != ["ENABLED"] * 3:
            raise AssertionError("Unable to get to \"ENABLED\" state: %s" % str(enabled))
        self.log.info("Good. Masternode enabled")
        miner.generate(1)
        sync_blocks(self.nodes)
        time.sleep(1)

        last_seen = [self.get_mn_lastseen(node, collateralTxId) for node in self.nodes]
        self.log.info("Current lastseen: %s" % str(last_seen))
        self.log.info("Waiting 2 * 25 seconds and check new lastseen...")
        time.sleep(50)
        new_last_seen = [self.get_mn_lastseen(node, collateralTxId) for node in self.nodes]
        self.log.info("New lastseen: %s" % str(new_last_seen))
        for i in range(self.num_nodes):
            assert_greater_than(new_last_seen[i], last_seen[i])
        self.log.info("All good.")



if __name__ == '__main__':
    MasternodePingTest().main()