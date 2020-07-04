#!/usr/bin/env python3
# Copyright (c) 2018 The Zcash developers
# Copyright (c) 2020 The PIVX developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

from test_framework.test_framework import PivxTestFramework
from test_framework.util import *
from decimal import Decimal

my_memo_str = 'c0ffee' # stay awake
my_memo = '633066666565'
my_memo = my_memo + '0'*(1024-len(my_memo))

no_memo = 'f6' + ('0'*1022) # see section 5.5 of the protocol spec

fee = Decimal('0.0001')

class ListReceivedTest (PivxTestFramework):

    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        saplingUpgrade = ['-nuparams=v5_dummy:1']
        self.extra_args = [saplingUpgrade, saplingUpgrade, saplingUpgrade, saplingUpgrade]

    def generate_and_sync(self, new_height):
        current_height = self.nodes[0].getblockcount()
        assert(new_height > current_height)
        self.nodes[0].generate(new_height - current_height)
        self.sync_all()
        assert_equal(new_height, self.nodes[0].getblockcount())

    def run_test_release(self, height): # starts in height 214
        self.generate_and_sync(height+1)
        taddr = self.nodes[1].getnewaddress()
        shield_addr1 = self.nodes[1].getnewshieldedaddress()
        shield_addrExt = self.nodes[3].getnewshieldedaddress()

        self.nodes[0].sendtoaddress(taddr, 6.0) # node_1 in taddr with 6 PIV.
        self.generate_and_sync(height+2)

        # Send 1 PIV to shield addr1
        txid = self.nodes[1].shielded_sendmany(taddr, [ # node_1 with 6 PIV sending them all (fee is 0.0001 PIV)
            {'address': shield_addr1, 'amount': 2, 'memo': my_memo},
            {'address': shield_addrExt, 'amount': 3},
        ])
        self.sync_all()

        # Decrypted transaction details should be correct
        pt = self.nodes[1].viewshieldedtransaction(txid)
        assert_equal(pt['txid'], txid)
        assert_equal(len(pt['spends']), 0)
        assert_equal(len(pt['outputs']), 2)

        # Output orders can be randomized, so we check the output
        # positions and contents separately
        outputs = []

        if pt['outputs'][0]['address'] == shield_addr1:
            assert_equal(pt['outputs'][0]['outgoing'], False)
            #assert_equal(pt['outputs'][0]['memoStr'], my_memo_str) TODO FIX ME
        else:
            assert_equal(pt['outputs'][0]['outgoing'], True)
        outputs.append({
            'address': pt['outputs'][0]['address'],
            'value': pt['outputs'][0]['value'],
            'valueSat': pt['outputs'][0]['valueSat'],
            #'memo': pt['outputs'][0]['memo'],
        })

        if pt['outputs'][1]['address'] == shield_addr1:
            assert_equal(pt['outputs'][1]['outgoing'], False)
            #assert_equal(pt['outputs'][1]['memoStr'], my_memo_str) TODO FIX ME
        else:
            assert_equal(pt['outputs'][1]['outgoing'], True)
        outputs.append({
            'address': pt['outputs'][1]['address'],
            'value': pt['outputs'][1]['value'],
            'valueSat': pt['outputs'][1]['valueSat'],
            #'memo': pt['outputs'][1]['memo'], TODO FIX ME
        })

        assert({
                   'address': shield_addr1,
                   'value': Decimal('2'),
                   'valueSat': 200000000,
                   #'memo': my_memo, TODO FIX ME
               } in outputs)

        assert({
                   'address': shield_addrExt,
                   'value': Decimal('3'),
                   'valueSat': 300000000,
                   #'memo': no_memo,
               } in outputs)

        r = self.nodes[1].listreceivedbyshieldedaddress(shield_addr1)
        assert_true(0 == len(r), "Should have received no confirmed note")
        c = self.nodes[1].getsaplingnotescount()
        assert_true(0 == c, "Count of confirmed notes should be 0")

        # No confirmation required, one note should be present
        r = self.nodes[1].listreceivedbyshieldedaddress(shield_addr1, 0)
        assert_true(1 == len(r), "Should have received one (unconfirmed) note")
        assert_equal(txid, r[0]['txid'])
        assert_equal(2, r[0]['amount'])
        assert_false(r[0]['change'], "Note should not be change")
        #assert_equal(my_memo, r[0]['memo'])
        assert_equal(0, r[0]['confirmations'])
        assert_equal(-1, r[0]['blockindex'])
        assert_equal(0, r[0]['blockheight'])

        c = self.nodes[1].getsaplingnotescount(0)
        assert_true(1 == c, "Count of unconfirmed notes should be 1")

        # Confirm transaction (2 PIV from taddr to shield_addr1)
        self.generate_and_sync(height+3)

        # adjust confirmations
        r[0]['confirmations'] = 1
        # adjust blockindex
        r[0]['blockindex'] = 1
        # adjust height
        r[0]['blockheight'] = height + 3

        # Require one confirmation, note should be present
        r2 = self.nodes[1].listreceivedbyshieldedaddress(shield_addr1)
        # As time will be different (tx was included in a block), need to remove it from the dict
        assert_true(r[0]['blocktime'] != r2[0]['blocktime'])
        del r[0]['blocktime']
        del r2[0]['blocktime']
        # Now can check that the information is the same
        assert_equal(r, r2)

        # Generate some change by sending part of shield_addr1 to shield_addr2
        txidPrev = txid
        shield_addr2 = self.nodes[1].getnewshieldedaddress()
        txid = self.nodes[1].shielded_sendmany(shield_addr1, # shield_addr1 has 2 PIV, send 0.6 PIV + 0.0001 PIV fee
                                        [{'address': shield_addr2, 'amount': 0.6}]) # change 1.3999
        self.sync_all()
        self.generate_and_sync(height+4)

        # Decrypted transaction details should be correct
        pt = self.nodes[1].viewshieldedtransaction(txid)
        assert_equal(pt['txid'], txid)
        assert_equal(len(pt['spends']), 1)
        assert_equal(len(pt['outputs']), 2)

        assert_equal(pt['spends'][0]['txidPrev'], txidPrev)
        assert_equal(pt['spends'][0]['spend'], 0)
        assert_equal(pt['spends'][0]['outputPrev'], 0)
        assert_equal(pt['spends'][0]['address'], shield_addr1)
        assert_equal(pt['spends'][0]['value'], Decimal('2.0'))
        assert_equal(pt['spends'][0]['valueSat'], 200000000)

        # Output orders can be randomized, so we check the output
        # positions and contents separately
        outputs = []

        assert_equal(pt['outputs'][0]['output'], 0)
        assert_equal(pt['outputs'][0]['outgoing'], False)
        outputs.append({
            'address': pt['outputs'][0]['address'],
            'value': pt['outputs'][0]['value'],
            'valueSat': pt['outputs'][0]['valueSat'],
            #'memo': pt['outputs'][0]['memo'],
        })

        assert_equal(pt['outputs'][1]['output'], 1)
        assert_equal(pt['outputs'][1]['outgoing'], False)
        outputs.append({
            'address': pt['outputs'][1]['address'],
            'value': pt['outputs'][1]['value'],
            'valueSat': pt['outputs'][1]['valueSat'],
            #'memo': pt['outputs'][1]['memo'],
        })

        assert({
                   'address': shield_addr2,
                   'value': Decimal('0.6'),
                   'valueSat': 60000000,
                   #'memo': no_memo,
               } in outputs)
        assert({
                   'address': shield_addr1,
                   'value': Decimal('1.3999'),
                   'valueSat': 139990000,
                   #'memo': no_memo,
               } in outputs)

        # shield_addr1 should have a note with change
        r = self.nodes[1].listreceivedbyshieldedaddress(shield_addr1, 0)
        r = sorted(r, key = lambda received: received['amount'])
        assert_true(2 == len(r), "shield_addr1 Should have received 2 notes")

        assert_equal(txid, r[0]['txid'])
        assert_equal(Decimal('1.4')-fee, r[0]['amount'])
        assert_true(r[0]['change'], "Note valued at (1.4-fee) should be change")
        #assert_equal(no_memo, r[0]['memo'])

        # The old note still exists (it's immutable), even though it is spent
        assert_equal(Decimal('2.0'), r[1]['amount'])
        assert_false(r[1]['change'], "Note valued at 1.0 should not be change")
        #assert_equal(my_memo, r[1]['memo'])

        # shield_addr2 should not have change
        r = self.nodes[1].listreceivedbyshieldedaddress(shield_addr2, 0)
        r = sorted(r, key = lambda received: received['amount'])
        assert_true(1 == len(r), "shield_addr2 Should have received 1 notes")
        assert_equal(txid, r[0]['txid'])
        assert_equal(Decimal('0.6'), r[0]['amount'])
        assert_false(r[0]['change'], "Note valued at 0.6 should not be change")
        #assert_equal(no_memo, r[0]['memo'])

        c = self.nodes[1].getsaplingnotescount(0)
        assert_true(3 == c, "Count of unconfirmed notes should be 3(2 in shield_addr1 + 1 in shield_addr2)")

    def run_test(self):
        self.run_test_release(214)

if __name__ == '__main__':
    ListReceivedTest().main()
