#!/usr/bin/env python3
# Copyright (c) 2020 The PIVX developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import PivxTestFramework
from test_framework.util import *
import time

"""
Test checking:
 1) Masternodes setup/creation.
 2) Proposal creation.
 3) Vote creation.
 4) Proposal and vote broadcast.
 5) Proposal and vote sync.
"""

class MasternodeGovernanceBasicTest(PivxTestFramework):

    def set_test_params(self):
        self.num_nodes = 5
        self.extra_args = [[], ["-listen", "-externalip=127.0.0.1"], [], ["-listen", "-externalip=127.0.0.1"], []]
        self.setup_clean_chain = True
        self.enable_mocktime()

        self.ownerOnePos = 0
        self.remoteOnePos = 1
        self.ownerTwoPos = 2
        self.remoteTwoPos = 3
        self.minerPos = 4

        self.mnOnePrivkey = "9247iC59poZmqBYt9iDh9wDam6v9S1rW5XekjLGyPnDhrDkP4AK"
        self.mnTwoPrivkey = "92Hkebp3RHdDidGZ7ARgS4orxJAGyFUPDXNqtsYsiwho1HGVRbF"

    def check_mnsync_finished(self):
        for node in self.nodes:
            status = node.mnsync("status")["RequestedMasternodeAssets"]
            assert_equal(status, 999) # sync finished

    def check_mn_status_in_mnlist(self, node, mnTxHash, status):
        listMNs = node.listmasternodes()
        assert(len(listMNs) > 0)
        found = False
        for mnData in listMNs:
            if (mnData["txhash"] == mnTxHash):
                assert_equal(mnData["status"], status)
                found = True
        return found

    def check_mn_list_empty(self, node):
        assert(len(node.listmasternodes()) == 0)

    def check_budget_finalization_sync(self):
        for i in range(0, len(self.nodes)):
            node = self.nodes[i]
            assert_true(len(node.mnfinalbudget("show")) == 1, "MN budget finalization not synced in node" + str(i))

    def start_masternode(self, mnOwner, masternodeAlias):
        ret = mnOwner.startmasternode("alias", "false", masternodeAlias, True)
        assert_equal(ret["result"], "success")

    def broadcastbudgetfinalization(self, node):
        self.log.info("suggesting the budget finalization..")
        node.mnfinalbudgetsuggest()

        self.log.info("confirming the budget finalization..")
        self.generate_two_blocks()
        time.sleep(2)
        self.generate_two_blocks()

        self.log.info("broadcasting the budget finalization..")
        node.mnfinalbudgetsuggest()

    def generate_two_blocks(self):
        # create two blocks (mocktime +120 sec) + sleep 5 seconds on every block
        # so the node has time to broadcast the mnping.
        for i in range(0, 2):
            self.generate(1)
            time.sleep(5)
        set_node_times(self.nodes, self.mocktime + 15)
        time.sleep(3)

    def check_mn_status(self, mnTxHash, status):
        for i in range(len(self.nodes)):
            node = self.nodes[i]
            found = self.check_mn_status_in_mnlist(node, mnTxHash, status)
            assert_true(found, "mn status invalid for node"+str(i))

    def setupMasternode(self,
                        mnOwner,
                        miner,
                        masternodeAlias,
                        mnOwnerDirPath,
                        mnRemotePos,
                        masternodePrivKey):
        self.log.info("adding balance to the mn owner for " + masternodeAlias + "..")
        mnAddress = mnOwner.getnewaddress(masternodeAlias)
        # send to the owner the collateral tx cost
        collateralTxId = miner.sendtoaddress(mnAddress, Decimal('10000'))
        # confirm and verify reception
        self.generate(1)
        assert_equal(mnOwner.getbalance(), Decimal('10000'))
        assert(mnOwner.getrawtransaction(collateralTxId, 1)["confirmations"] > 0)

        self.log.info("all good, creating masternode " + masternodeAlias + "..")

        # get the collateral output using the RPC command
        mnCollateralOutput = mnOwner.getmasternodeoutputs()[0]
        assert_equal(mnCollateralOutput["txhash"], collateralTxId)
        mnCollateralOutputIndex = mnCollateralOutput["outputidx"]

        self.log.info("collateral accepted for "+ masternodeAlias +".. updating masternode.conf and stopping the node")

        # verify collateral confirmed

        # create the masternode.conf and add it
        confData = masternodeAlias + " 127.0.0.1:" + str(p2p_port(mnRemotePos)) + " " + str(masternodePrivKey) + " " + str(mnCollateralOutput["txhash"]) + " " + str(mnCollateralOutputIndex)
        destinationDirPath = mnOwnerDirPath
        destPath = os.path.join(destinationDirPath, "masternode.conf")
        with open(destPath, "a+") as file_object:
            file_object.write("\n")
            file_object.write(confData)

        # return the collateral id
        return collateralTxId

    def check_proposal_existence(self, proposalName, proposalHash):
        for node in self.nodes:
            proposals = node.getbudgetinfo(proposalName)
            assert(len(proposals) > 0)
            assert_equal(proposals[0]["Hash"], proposalHash)

    def check_vote_existence(self, proposalName, mnCollateralHash, voteType):
        for i in range(0, len(self.nodes)):
            node = self.nodes[i]
            votesInfo = node.getbudgetvotes(proposalName)
            assert(len(votesInfo) > 0)
            found = False
            for voteInfo in votesInfo:
                if (voteInfo["mnId"] == mnCollateralHash) :
                    assert_equal(voteInfo["Vote"], voteType)
                    found = True
            assert_true(found, "Error checking vote existence in node " + str(i))


    def generate(self, gen):
        for i in range(0, gen):
            self.mocktime = self.generate_pow(self.minerPos, self.mocktime)
            set_node_times(self.nodes, self.mocktime)
        sync_blocks(self.nodes)

    def run_test(self):
        # init time
        set_node_times(self.nodes, self.mocktime)

        ownerOne = self.nodes[self.ownerOnePos]
        remoteOne = self.nodes[self.remoteOnePos]
        ownerTwo = self.nodes[self.ownerTwoPos]
        remoteTwo = self.nodes[self.remoteTwoPos]
        miner = self.nodes[self.minerPos]

        ownerOneDir = os.path.join(self.options.tmpdir, "node0")
        remoteOneDir = os.path.join(self.options.tmpdir, "node1")
        ownerTwoDir = os.path.join(self.options.tmpdir, "node2")
        remoteTwoDir = os.path.join(self.options.tmpdir, "node3")

        self.log.info("generating 415 blocks..")
        self.generate(415)

        self.log.info("masternodes setup..")
        # setup first masternode node, corresponding to nodeOne
        masternodeOneAlias = "mnOne"
        mnOneTxHash = self.setupMasternode(
            ownerOne,
            miner,
            masternodeOneAlias,
            os.path.join(ownerOneDir, "regtest"),
            self.remoteOnePos,
            self.mnOnePrivkey)

        # setup second masternode node, corresponding to nodeTwo
        masternodeTwoAlias = "mntwo"
        mnTwoTxHash = self.setupMasternode(
            ownerTwo,
            miner,
            masternodeTwoAlias,
            os.path.join(ownerTwoDir, "regtest"),
            self.remoteTwoPos,
            self.mnTwoPrivkey)

        self.log.info("masternodes setup completed, initializing them..")
        # now both are configured,
        # let's activate the masternodes

        set_node_times(self.nodes, self.mocktime + 10) # Advance a little bit in time.
        # init masternodes at runtime
        remoteOnePort = p2p_port(self.remoteTwoPos)
        remoteTwoPort = p2p_port(self.remoteOnePos)
        remoteOne.initmasternode(self.mnOnePrivkey, "127.0.0.1:"+str(remoteOnePort))
        remoteTwo.initmasternode(self.mnTwoPrivkey, "127.0.0.1:"+str(remoteTwoPort))
        set_node_times(self.nodes, self.mocktime + 5)
        time.sleep(5)

        # now need connection
        self.generate(1)
        set_node_times(self.nodes, self.mocktime + 15)

        self.check_mnsync_finished()
        self.log.info("tier two synced! starting masternodes..")
        #
        # ## Now everything is set, can start both masternodes
        self.start_masternode(ownerOne, masternodeOneAlias)
        self.start_masternode(ownerTwo, masternodeTwoAlias)

        time.sleep(7) # sleep to have everyone updated.
        self.check_mn_status(mnOneTxHash, "ACTIVE")
        self.check_mn_status(mnTwoTxHash, "ACTIVE")
        set_node_times(self.nodes, self.mocktime + 10)
        time.sleep(5)

        self.log.info("masternodes started, waiting until both gets enabled..")
        self.generate_two_blocks()

        # check masternode enabled
        self.check_mn_status(mnOneTxHash, "ENABLED")
        self.check_mn_status(mnTwoTxHash, "ENABLED")

        #
        self.log.info("masternodes enabled and running properly!")

        # now let's prepare the proposal
        self.log.info("preparing budget proposal..")
        firstProposalName = "super-cool"
        firstProposalLink = "https://forum.pivx.org/t/test-proposal"
        firstProposalCycles = 2
        firstProposalAddress = miner.getnewaddress()
        firstProposalAmountPerCycle = 300
        nextSuperBlockHeight = miner.getnextsuperblock()

        proposalFeeTxId = miner.preparebudget(
            firstProposalName,
            firstProposalLink,
            firstProposalCycles,
            nextSuperBlockHeight,
            firstProposalAddress,
            firstProposalAmountPerCycle)

        # generate 3 blocks to confirm the tx (waiting in-between to let nodes update the mnping)
        self.generate_two_blocks()
        self.generate(1)
        time.sleep(2)

        txinfo = miner.gettransaction(proposalFeeTxId)
        assert_equal(txinfo['amount'], -50.00)

        self.log.info("submitting the budget proposal..")

        proposalHash = miner.submitbudget(
            firstProposalName,
            firstProposalLink,
            firstProposalCycles,
            nextSuperBlockHeight,
            firstProposalAddress,
            firstProposalAmountPerCycle,
            proposalFeeTxId)

        # let's wait a little bit and see if all nodes are sync
        time.sleep(5)
        self.check_proposal_existence(firstProposalName, proposalHash)
        self.log.info("proposal broadcast succeed!")

        # now let's vote for the proposal with the first MN
        self.log.info("broadcasting votes for the proposal now..")
        ownerOne.mnbudgetvote("alias", proposalHash, "yes", masternodeOneAlias)
        time.sleep(5)
        # check that the vote was accepted everywhere
        self.check_vote_existence(firstProposalName, mnOneTxHash, "YES")
        self.log.info("all good, MN1 vote accepted everywhere!")

        # now let's vote for the proposal with the second MN
        ownerTwo.mnbudgetvote("alias", proposalHash, "yes", masternodeTwoAlias)
        time.sleep(5)
        # check that the vote was accepted everywhere
        self.check_vote_existence(firstProposalName, mnTwoTxHash, "YES")
        self.log.info("all good, MN2 vote accepted everywhere!")

        # Quick block count check.
        assert_equal(ownerOne.getblockcount(), 423)
        # Proposal needs to be on the chain > 5 min.
        self.generate_two_blocks()
        self.generate_two_blocks()

        self.log.info("starting budget finalization sync test..")
        # Now let's submit a budget finalization, next superblock will occur at block 432

        # assert that there is no budget finalization first.
        assert_true(len(ownerOne.mnfinalbudget("show")) == 0)

        # suggest the budget finalization and confirm the tx.
        self.broadcastbudgetfinalization(miner)
        time.sleep(5)

        self.log.info("checking budget finalization sync..")
        self.check_budget_finalization_sync()

        self.log.info("budget finalization synced!")




if __name__ == '__main__':
    MasternodeGovernanceBasicTest().main()