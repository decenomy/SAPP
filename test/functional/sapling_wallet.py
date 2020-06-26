#!/usr/bin/env python3
# Copyright (c) 2018 The Zcash developers
# Copyright (c) 2020 The PIVX developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

from test_framework.test_framework import PivxTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import (
    assert_equal,
    get_coinstake_address
)

from decimal import Decimal

# Test wallet behaviour with Sapling addresses
class WalletSaplingTest(PivxTestFramework):

    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        saplingUpgrade = ['-nuparams=v5_dummy:1']
        self.extra_args = [saplingUpgrade, saplingUpgrade, saplingUpgrade, saplingUpgrade]

    def run_test(self):
        # generate 100 more to activate sapling in regtest
        self.nodes[2].generate(12)
        self.sync_all()
        self.nodes[0].generate(360)
        # Sanity-check the test harness
        assert_equal(self.nodes[0].getblockcount(), 372)

        taddr1 = self.nodes[1].getnewaddress()
        saplingAddr0 = self.nodes[0].getnewshieldedaddress()
        saplingAddr1 = self.nodes[1].getnewshieldedaddress()

        # Verify addresses
        assert(saplingAddr0 in self.nodes[0].listshieldedaddresses())
        assert(saplingAddr1 in self.nodes[1].listshieldedaddresses())
        assert_equal(self.nodes[0].validateaddress(saplingAddr0)['type'], 'sapling')
        assert_equal(self.nodes[0].validateaddress(saplingAddr1)['type'], 'sapling')

        # Verify balance
        assert_equal(self.nodes[0].getshieldedbalance(saplingAddr0), Decimal('0'))
        assert_equal(self.nodes[1].getshieldedbalance(saplingAddr1), Decimal('0'))
        assert_equal(self.nodes[1].getreceivedbyaddress(taddr1), Decimal('0'))

        # Hardcoded min sapling fee
        nMinDefaultSaplingFee = 1

        # Node 0 shields some funds
        # taddr -> Sapling
        recipients = []
        recipients.append({"address": saplingAddr0, "amount": Decimal('10')})
        coinstake = get_coinstake_address(self.nodes[0])
        mytxid = self.nodes[0].shielded_sendmany(coinstake, recipients, 1, nMinDefaultSaplingFee)
        assert(mytxid != "SendTransaction: CommitTransaction failed")

        self.sync_all()

        # Verify priority of tx is INF_PRIORITY, defined as 1E+25 (10000000000000000000000000)
        mempool = self.nodes[0].getrawmempool(True)
        assert(Decimal(mempool[mytxid]['startingpriority']) == Decimal('1E+25'))

        # Shield another coinbase UTXO
        mytxid = self.nodes[0].shielded_sendmany(get_coinstake_address(self.nodes[0]), recipients, 1, nMinDefaultSaplingFee)

        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()

        # Verify balance
        assert_equal(self.nodes[0].getshieldedbalance(saplingAddr0), Decimal('20'))
        assert_equal(self.nodes[1].getshieldedbalance(saplingAddr1), Decimal('0'))
        assert_equal(self.nodes[1].getreceivedbyaddress(taddr1), Decimal('0'))

        # Node 0 sends some shielded funds to node 1
        # Sapling -> Sapling
        #         -> Sapling (change)
        recipients = []
        recipients.append({"address": saplingAddr1, "amount": Decimal('15')})
        mytxid = self.nodes[0].shielded_sendmany(saplingAddr0, recipients, 1, nMinDefaultSaplingFee)

        self.sync_all()

        # Verify priority of tx is MAX_PRIORITY, defined as 1E+25 (10000000000000000000000000)
        mempool = self.nodes[0].getrawmempool(True)
        assert(Decimal(mempool[mytxid]['startingpriority']) == Decimal('1E+25'))

        self.nodes[2].generate(1)
        self.sync_all()

        # Verify balance
        assert_equal(self.nodes[0].getshieldedbalance(saplingAddr0), Decimal('4')) # 20 receive - (15 sent + 1 fee)
        assert_equal(self.nodes[1].getshieldedbalance(saplingAddr1), Decimal('15'))
        assert_equal(self.nodes[1].getreceivedbyaddress(taddr1), Decimal('0'))

        # Node 1 sends some shielded funds to node 0, as well as unshielding
        # Sapling -> Sapling
        #         -> taddr
        #         -> Sapling (change)
        recipients = []
        recipients.append({"address": saplingAddr0, "amount": Decimal('5')})
        recipients.append({"address": taddr1, "amount": Decimal('5')})
        mytxid = self.nodes[1].shielded_sendmany(saplingAddr1, recipients, 1, nMinDefaultSaplingFee)
        assert(mytxid != "SendTransaction: CommitTransaction failed")
        self.sync_all()

        # Verify priority of tx is MAX_PRIORITY, defined as 1E+25 (10000000000000000000000000)
        mempool = self.nodes[1].getrawmempool(True)
        assert(Decimal(mempool[mytxid]['startingpriority']) == Decimal('1E+25'))

        self.nodes[2].generate(1)
        self.sync_all()

        # Verify balance
        assert_equal(self.nodes[0].getshieldedbalance(saplingAddr0), Decimal('9')) # 20 receive - (15 sent + 1 fee) + 5 receive
        assert_equal(self.nodes[1].getshieldedbalance(saplingAddr1), Decimal('4'))
        assert_equal(self.nodes[1].getreceivedbyaddress(taddr1), Decimal('5'))

        # Verify existence of Sapling related JSON fields
        resp = self.nodes[0].getrawtransaction(mytxid, 1)
        assert_equal(Decimal(resp['valueBalance']), Decimal('6.00')) # 15 shielded input - 5 shielded spend - 4 change
        assert(len(resp['vShieldedSpend']) == 1)
        assert(len(resp['vShieldedOutput']) == 2)
        assert('bindingSig' in resp)
        shieldedSpend = resp['vShieldedSpend'][0]
        assert('cv' in shieldedSpend)
        assert('anchor' in shieldedSpend)
        assert('nullifier' in shieldedSpend)
        assert('rk' in shieldedSpend)
        assert('proof' in shieldedSpend)
        assert('spendAuthSig' in shieldedSpend)
        shieldedOutput = resp['vShieldedOutput'][0]
        assert('cv' in shieldedOutput)
        assert('cmu' in shieldedOutput)
        assert('ephemeralKey' in shieldedOutput)
        assert('encCiphertext' in shieldedOutput)
        assert('outCiphertext' in shieldedOutput)
        assert('proof' in shieldedOutput)

        # Verify importing a spending key will update the nullifiers and witnesses correctly
        sk0 = self.nodes[0].exportsaplingkey(saplingAddr0)
        saplingAddrInfo0 = self.nodes[2].importsaplingkey(sk0, "yes")
        assert_equal(saplingAddrInfo0["address"], saplingAddr0)
        assert_equal(self.nodes[2].getshieldedbalance(saplingAddrInfo0["address"]), Decimal('9'))
        sk1 = self.nodes[1].exportsaplingkey(saplingAddr1)
        saplingAddrInfo1 = self.nodes[2].importsaplingkey(sk1, "yes")
        assert_equal(saplingAddrInfo1["address"], saplingAddr1)
        assert_equal(self.nodes[2].getshieldedbalance(saplingAddrInfo1["address"]), Decimal('4'))

        # Verify importing a viewing key will update the nullifiers and witnesses correctly
        extfvk0 = self.nodes[0].exportsaplingviewingkey(saplingAddr0)
        saplingAddrInfo0 = self.nodes[3].importsaplingviewingkey(extfvk0, "yes")
        assert_equal(saplingAddrInfo0["address"], saplingAddr0)
        assert_equal(Decimal(self.nodes[3].getshieldedbalance(saplingAddrInfo0["address"], 1, True)), Decimal('9'))
        extfvk1 = self.nodes[1].exportsaplingviewingkey(saplingAddr1)
        saplingAddrInfo1 = self.nodes[3].importsaplingviewingkey(extfvk1, "yes")
        assert_equal(saplingAddrInfo1["address"], saplingAddr1)
        assert_equal(self.nodes[3].getshieldedbalance(saplingAddrInfo1["address"], 1, True), Decimal('4'))

        # Verify that getshieldedbalance only includes watch-only addresses when requested
        shieldedBalance = self.nodes[3].getshieldedbalance()
        # no balance in the wallet
        assert_equal(shieldedBalance, Decimal('0'))

        shieldedBalance = self.nodes[3].getshieldedbalance("*", 1, True)
        # watch only balance
        assert_equal(shieldedBalance, Decimal('13.00'))

if __name__ == '__main__':
    WalletSaplingTest().main()
