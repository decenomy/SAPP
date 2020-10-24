// Copyright (c) 2016-2020 The Zcash developers
// Copyright (c) 2020 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#ifndef PIVX_UTIL_TEST_H
#define PIVX_UTIL_TEST_H

#include "sapling/address.hpp"
#include "sapling/incrementalmerkletree.hpp"
#include "sapling/note.hpp"
#include "sapling/noteencryption.hpp"
#include "wallet/wallet.h"

struct TestSaplingNote {
    libzcash::SaplingNote note;
    SaplingMerkleTree tree;
};

const Consensus::Params& RegtestActivateSapling();

void RegtestDeactivateSapling();

libzcash::SaplingExtendedSpendingKey GetTestMasterSaplingSpendingKey();

CKey AddTestCKeyToKeyStore(CBasicKeyStore& keyStore, bool genNewKey = false);

/**
 * Generate a dummy SaplingNote and a SaplingMerkleTree with that note's commitment.
 */
TestSaplingNote GetTestSaplingNote(const libzcash::SaplingPaymentAddress& pa, CAmount value);

CWalletTx GetValidSaplingReceive(const Consensus::Params& consensusParams,
                                 CBasicKeyStore& keyStore,
                                 const libzcash::SaplingExtendedSpendingKey &sk,
                                 CAmount value,
                                 bool genNewKey = false);

#endif // PIVX_UTIL_TEST_H
