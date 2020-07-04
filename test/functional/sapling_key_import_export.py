#!/usr/bin/env python3
# Copyright (c) 2017 The Zcash developers
# Copyright (c) 2020 The PIVX developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

from decimal import Decimal
from test_framework.test_framework import PivxTestFramework
from test_framework.util import *
from functools import reduce
import logging

#logging.basicConfig(format='%(levelname)s:%(message)s', level=logging.INFO)

fee = Decimal('0.0001') # constant (but can be changed within reason)

class SaplingkeyImportExportTest (PivxTestFramework):

    def set_test_params(self):
        self.num_nodes = 5
        self.setup_clean_chain = True
        saplingUpgrade = ['-nuparams=v5_dummy:1']
        self.extra_args = [saplingUpgrade, saplingUpgrade, saplingUpgrade, saplingUpgrade, saplingUpgrade]

    def run_test(self):
        [alice, bob, charlie, david, miner] = self.nodes

        # the sender loses 'amount' plus fee; to_addr receives exactly 'amount'
        def shielded_send(from_node, from_addr, to_addr, amount):
            from_node.shielded_sendmany(from_addr,
                                        [{"address": to_addr, "amount": Decimal(amount)}], 1)
            self.sync_all()
            miner.generate(1)
            self.sync_all()

        def verify_utxos(node, amts, shield_addr):
            amts.sort(reverse=True)
            txs = node.listreceivedbyshieldedaddress(shield_addr)
            txs.sort(key=lambda x: x["amount"], reverse=True)

            try:
                assert_equal(amts, [tx["amount"] for tx in txs])
                for tx in txs:
                    # make sure outindex keys exist and have valid value
                    assert_equal("outindex" in tx, True)
            except AssertionError:
                logging.error(
                    'Expected amounts: %r; txs: %r',
                    amts, txs)
                raise

        def get_private_balance(node):
            return node.getshieldedbalance()

        # Seed Alice with some funds
        alice.generate(10)
        self.sync_all()
        miner.generate(100)
        self.sync_all()
        fromAddress = alice.listunspent()[0]['address']
        amountTo = 10 * 250 - fee
        # Shield Alice's coinbase funds to her shield_addr
        alice_addr = alice.getnewshieldedaddress()
        txid = alice.shielded_sendmany(fromAddress, [{"address": alice_addr, "amount": Decimal(amountTo)}])
        self.sync_all()
        miner.generate(1)
        self.sync_all()

        # Now get a pristine address for receiving transfers:
        bob_addr = bob.getnewshieldedaddress()
        verify_utxos(bob, [], bob_addr)
        # TODO: Verify that charlie doesn't have funds in addr
        # verify_utxos(charlie, [])

        # the amounts of each txn embodied which generates a single UTXO:
        amounts = list(map(Decimal, ['2.3', '3.7', '1.1', '1.5', '1.0', '0.19']))

        # Internal test consistency assertion:
        assert_greater_than(
            Decimal(get_private_balance(alice)),
            reduce(Decimal.__add__, amounts))

        logging.info("Sending pre-export txns...")
        for amount in amounts[0:2]:
            shielded_send(alice, alice_addr, bob_addr, amount)

        logging.info("Exporting privkey from bob...")
        bob_privkey = bob.exportsaplingkey(bob_addr)

        logging.info("Sending post-export txns...")
        for amount in amounts[2:4]:
            shielded_send(alice, alice_addr, bob_addr, amount)

        verify_utxos(bob, amounts[:4], bob_addr)
        # verify_utxos(charlie, [])

        logging.info("Importing bob_privkey into charlie...")
        # importsaplingkey rescan defaults to "whenkeyisnew", so should rescan here
        ipk_addr = charlie.importsaplingkey(bob_privkey)

        # importsaplingkey should have rescanned for new key, so this should pass:
        verify_utxos(charlie, amounts[:4], ipk_addr["address"])

        # Verify idempotent behavior:
        ipk_addr2 = charlie.importsaplingkey(bob_privkey)
        assert_equal(ipk_addr["address"], ipk_addr2["address"])

        # amounts should be unchanged
        verify_utxos(charlie, amounts[:4], ipk_addr2["address"])

        logging.info("Sending post-import txns...")
        for amount in amounts[4:]:
            shielded_send(alice, alice_addr, bob_addr, amount)

        verify_utxos(bob, amounts, bob_addr)
        verify_utxos(charlie, amounts, ipk_addr["address"])
        verify_utxos(charlie, amounts, ipk_addr2["address"])

        # keep track of the fees incurred by bob (his sends)
        bob_fee = Decimal("0")

        # Try to reproduce zombie balance reported in zcash#1936
        # At generated shield_addr, receive PIV, and send PIV back out. bob -> alice
        for amount in amounts[:2]:
            print("Sending amount from bob to alice: ", amount)
            shielded_send(bob, bob_addr, alice_addr, amount)
            bob_fee += fee

        bob_balance = sum(amounts[2:]) - bob_fee

        assert_equal(bob.getshieldedbalance(bob_addr), bob_balance)

        # importsaplingkey onto new node "david" (blockchain rescan, default or True?)
        d_ipk_addr = david.importsaplingkey(bob_privkey)

        # Check if amt bob spent is deducted for charlie and david
        assert_equal(charlie.getshieldedbalance(ipk_addr["address"]), bob_balance)
        assert_equal(david.getshieldedbalance(d_ipk_addr["address"]), bob_balance)

if __name__ == '__main__':
    SaplingkeyImportExportTest().main()
