#!/usr/bin/env python3

from test_framework.test_framework import PivxTestFramework
from test_framework.util import *
import time

"""
Test checking:
 1) Masternode setup/creation.
 2) Tier two network sync (masternode broadcasting).
 3) Masternode activation.
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

    def check_first_mn_in_mnlist_enabled(self, node):
        listMNs = node.listmasternodes()
        assert(len(listMNs) > 0)
        assert_equal(listMNs[0]["status"], "ENABLED")

    def run_test(self):
        mnOwner = self.nodes[0]
        mnRemote = self.nodes[1]
        miner = self.nodes[2]

        print("generating 301 blocks..")
        miner.generate(301)

        print("adding balance to the mn owner..")
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

        print("all good, creating the masternode..")
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

        print("collateral accepted.. updating masternode.conf and stopping the node")

        # verify collateral confirmed

        # create the masternode.conf and add it
        confData = masternodeAlias + " 127.0.0.1:" + str(remotePort) + " " + str(masternodePrivKey) + " " + str(mnCollateralOutput["txhash"]) + " " + str(mnCollateralOutput["outputidx"])
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

        print("starting nodes again..")
        self.restart_node(0)
        self.start_node(1)
        connect_nodes(self.nodes[0], 1)
        connect_nodes(self.nodes[1], 0)
        connect_nodes(self.nodes[1], 2)
        connect_nodes(self.nodes[2], 1)

        print("syncing tier two across recently started peers..")
        # let the nodes sync the tier two
        time.sleep(20)
        miner.generate(1)
        sync_blocks(self.nodes)

        # wait a little bit until the tier two is synced.
        time.sleep(20)
        self.check_mnsync_finished()

        print("tier two synced! starting masternode..")

        ## Now everything is set, start the masternode
        ret = mnOwner.startmasternode("alias", "false", masternodeAlias)
        assert_equal(ret["result"], "success")

        print("masternode started, waiting 125 seconds until it's enabled..")
        for i in range(1, 5):
            miner.generate(1)
            sync_blocks(self.nodes)
            time.sleep(25)

        # check masternode enabled
        self.check_first_mn_in_mnlist_enabled(mnRemote)
        self.check_first_mn_in_mnlist_enabled(mnOwner)
        self.check_first_mn_in_mnlist_enabled(miner)

        print("masternode enabled and running properly!")



if __name__ == '__main__':
    MasternodeActivationTest().main()