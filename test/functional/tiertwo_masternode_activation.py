#!/usr/bin/env python3
# Copyright (c) 2020 The PIVX developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import PivxTestFramework
from test_framework.util import *
import time

"""
Test checking:
 1) Masternode setup/creation.
 2) Tier two network sync (masternode broadcasting).
 3) Masternode activation.
 4) Masternode expiration.
 5) Masternode re activation.
 6) Masternode removal.
 7) Masternode collateral spent removal.
"""

class MasternodeActivationTest(PivxTestFramework):

    def set_test_params(self):
        self.num_nodes = 3
        self.extra_args = [[], [], []]
        self.setup_clean_chain = True

    def check_mnsync_finished(self):
        for i in range(0, 2):
            status = self.nodes[i].mnsync("status")["RequestedMasternodeAssets"]
            assert_equal(status, 999) # sync finished

    def check_first_mn_in_mnlist_status(self, node, status):
        listMNs = node.listmasternodes()
        assert(len(listMNs) > 0)
        assert_equal(listMNs[0]["status"], status)

    def check_mn_list_empty(self, node):
        assert(len(node.listmasternodes()) == 0)

    def start_masternode(self, mnOwner, masternodeAlias, mnRemote, miner):
        ret = mnOwner.startmasternode("alias", "false", masternodeAlias)
        assert_equal(ret["result"], "success")

        self.log.info("masternode started, waiting 125 seconds until it's enabled..")
        for i in range(1, 5):
            miner.generate(1)
            sync_blocks(self.nodes)
            time.sleep(25)

        # check masternode enabled
        self.check_first_mn_in_mnlist_status(mnRemote, "ENABLED")
        self.check_first_mn_in_mnlist_status(mnOwner, "ENABLED")
        self.check_first_mn_in_mnlist_status(miner, "ENABLED")

    def start_remote_mn_and_sync_tier_two(self, miner):
        self.start_node(1)
        connect_nodes(self.nodes[0], 1)
        connect_nodes(self.nodes[1], 0)
        connect_nodes(self.nodes[1], 2)
        connect_nodes(self.nodes[2], 1)

        self.log.info("syncing tier two across recently started peers..")
        # let the nodes sync the tier two
        time.sleep(20)
        miner.generate(1)
        sync_blocks(self.nodes)

        # wait a little bit until the tier two is synced.
        time.sleep(20)
        self.check_mnsync_finished()

    # Build a transaction that spends parent_txid:vout
    def spend_transaction(self, node, parent_txid, vout, value, fee):
        send_value = satoshi_round(value - fee)
        inputs = [{'txid' : parent_txid, 'vout' : vout}]
        outputs = {}
        outputs[node.getnewaddress()] = float(send_value)
        rawtx = node.createrawtransaction(inputs, outputs)
        signedtx = node.signrawtransaction(rawtx)
        txid = node.sendrawtransaction(signedtx['hex'])
        return txid

    def run_test(self):
        mnOwner = self.nodes[0]
        mnRemote = self.nodes[1]
        miner = self.nodes[2]

        self.log.info("generating 301 blocks..")
        miner.generate(301)

        self.log.info("adding balance to the mn owner..")
        mnOwnerAddr = mnOwner.getnewaddress()
        # send to the owner the collateral tx cost
        miner.sendtoaddress(mnOwnerAddr, Decimal('10001'))
        sync_mempools(self.nodes)
        # confirm the tx
        miner.generate(1)
        self.sync_all()
        # verify reception
        assert_equal(mnOwner.getbalance(), Decimal('10001'))

        # get the remote MN port
        remotePort = p2p_port(1)

        self.log.info("all good, creating the masternode..")
        ## Owner side

        # Let's create the masternode
        masternodePrivKey = mnOwner.createmasternodekey()
        masternodeAlias = "masternode1"
        mnAddress = mnOwner.getnewaddress(masternodeAlias)
        collateralTxId = mnOwner.sendtoaddress(mnAddress, Decimal('10000'))

        sync_mempools(self.nodes)
        miner.generate(2)
        self.sync_all()
        # check if tx got confirmed
        assert(mnOwner.getrawtransaction(collateralTxId, 1)["confirmations"] > 0)

        # get the collateral output using the RPC command
        mnCollateralOutput = mnOwner.getmasternodeoutputs()[0]
        assert_equal(mnCollateralOutput["txhash"], collateralTxId)
        mnCollateralOutputIndex = mnCollateralOutput["outputidx"]

        self.log.info("collateral accepted.. updating masternode.conf and stopping the node")

        # verify collateral confirmed

        # create the masternode.conf and add it
        confData = masternodeAlias + " 127.0.0.1:" + str(remotePort) + " " + str(masternodePrivKey) + " " + str(mnCollateralOutput["txhash"]) + " " + str(mnCollateralOutputIndex)
        destinationDirPath = os.path.join(self.options.tmpdir, "node0", "regtest")
        destPath = os.path.join(destinationDirPath, "masternode.conf")
        with open(destPath, "a+") as file_object:
            file_object.write("\n")
            file_object.write(confData)


        ## Remote side
        self.stop_node(1)

        # change the .conf
        destinationDirPath = os.path.join(self.options.tmpdir, "node1")
        destPath = os.path.join(destinationDirPath, "pivx.conf")
        with open(destPath, "a+") as file_object:
            file_object.write("\n")
            file_object.write("listen=1\n")
            file_object.write("masternode=1\n")
            file_object.write("externalip=127.0.0.1\n")
            file_object.write("masternodeaddr=127.0.0.1:"+str(remotePort)+"\n")
            file_object.write("masternodeprivkey=" + str(masternodePrivKey))

        self.log.info("starting nodes again..")
        self.restart_node(0)
        self.start_remote_mn_and_sync_tier_two(miner)

        self.log.info("tier two synced! starting masternode..")

        ## Now everything is set, start the masternode
        self.start_masternode(mnOwner, masternodeAlias, mnRemote, miner)

        self.log.info("masternode enabled and running properly!")

        self.log.info("testing now the expiration, stopping masternode and waiting +180 seconds until expires")
        # now let's stop answering the ping and see how it gets disabled.
        mnRemote.stop()
        expiration_time = 180 # regtest expiration time
        time.sleep(expiration_time)

        # check masternode expired
        self.check_first_mn_in_mnlist_status(mnOwner, "EXPIRED")
        self.check_first_mn_in_mnlist_status(miner, "EXPIRED")

        self.log.info("masternode expired, good. Starting masternode again before gets removed..")

        # start again remote mn again.
        self.start_remote_mn_and_sync_tier_two(miner)

        # now let's try to re enable it before it gets removed.
        self.start_masternode(mnOwner, masternodeAlias, mnRemote, miner)

        self.log.info("testing removal now, remote will not answer any ping for 200 seconds..")
        # check masternode removal
        mnRemote.stop()
        removal_time = 200 # regtest removal time
        time.sleep(removal_time)

        ## Check masternode removed
        listMNs = mnOwner.listmasternodes()
        if (len(listMNs) > 0):
            assert_equal(listMNs[0]["status"], "REMOVE")

        listMNs = miner.listmasternodes()
        if (len(listMNs) > 0):
            assert_equal(listMNs[0]["status"], "REMOVE")

        self.log.info("masternode removed successfully")

        self.log.info("re starting the remote node and re enabling MN..")
        # Starting MN, activating it and spending the collateral now.
        self.start_remote_mn_and_sync_tier_two(miner)
        self.start_masternode(mnOwner, masternodeAlias, mnRemote, miner)

        self.log.info("spending the collateral now..")
        self.spend_transaction(mnOwner, collateralTxId, mnCollateralOutputIndex, 10000, 0.001)
        sync_mempools(self.nodes)
        miner.generate(2)
        self.sync_all()

        self.log.info("checking mn status..")
        time.sleep(10) # wait a little bit
        # Now check vin spent
        self.check_first_mn_in_mnlist_status(mnOwner, "VIN_SPENT")
        self.check_first_mn_in_mnlist_status(miner, "VIN_SPENT")
        self.check_first_mn_in_mnlist_status(mnRemote, "VIN_SPENT")

        self.log.info("masternode list updated successfully, vin spent")




if __name__ == '__main__':
    MasternodeActivationTest().main()