// Copyright (c) 2020 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "wallet/test/wallet_test_fixture.h"

#include "primitives/block.h"
#include "random.h"
#include "sapling/note.hpp"
#include "sapling/noteencryption.hpp"
#include "sapling/transaction_builder.h"
#include "test/librust/utiltest.h"
#include "wallet/wallet.h"

#include <boost/filesystem.hpp>

#include <boost/test/unit_test.hpp>

CAmount fee = COIN; // Hardcoded fee

BOOST_FIXTURE_TEST_SUITE(wallet_shielded_balances_tests, WalletTestingSetup)

void setupWallet(CWallet& wallet)
{
    wallet.SetMinVersion(FEATURE_SAPLING);
    wallet.SetupSPKM(false);
}

// Find and set notes data in the tx + add any missing ivk to the wallet's keystore.
CWalletTx& SetWalletNotesData(CWallet& wallet, CWalletTx& wtx)
{
    Optional<mapSaplingNoteData_t> saplingNoteData{nullopt};
    wallet.FindNotesDataAndAddMissingIVKToKeystore(wtx, saplingNoteData);
    BOOST_CHECK(saplingNoteData && !saplingNoteData->empty());
    wtx.SetSaplingNoteData(*saplingNoteData);
    BOOST_CHECK(wallet.AddToWallet(wtx));
    // Updated tx
    return wallet.mapWallet[wtx.GetHash()];
}

CWalletTx& AddShieldedBalanceToWallet(libzcash::SaplingPaymentAddress& sendTo, CAmount amount,
                                CWallet& wallet, const Consensus::Params& consensusParams,
                                libzcash::SaplingExtendedSpendingKey& extskOut)
{
    // Create a transaction shielding balance to 'sendTo' and load it to the wallet.
    BOOST_CHECK(wallet.GetSaplingExtendedSpendingKey(sendTo, extskOut));
    CWalletTx wtx = GetValidSaplingReceive(consensusParams, extskOut, amount, &wallet);

    // Updated tx after load it to the wallet
    CWalletTx& wtxUpdated = SetWalletNotesData(wallet, wtx);
    // Check tx credit now
    BOOST_CHECK_EQUAL(wtxUpdated.GetCredit(ISMINE_ALL), amount);
    BOOST_CHECK(wtxUpdated.IsAmountCached(CWalletTx::CREDIT, ISMINE_SPENDABLE_SHIELDED));
    return wtxUpdated;
}

struct SaplingSpendValues {
    libzcash::SaplingNote note;
    const uint256 anchor;
    const SaplingWitness witness;
};

/**
 * Update the wallet internally as if the wallet would had received a valid block containing wtx.
 * Then return the note, anchor and witness for any subsequent spending process.
 */
SaplingSpendValues UpdateWalletInternalNotesData(CWalletTx& wtx, SaplingOutPoint& sapPoint, CWallet& wallet)
{
    // Get note
    SaplingNoteData nd = wtx.mapSaplingNoteData[sapPoint];
    auto maybe_pt = libzcash::SaplingNotePlaintext::decrypt(
            wtx.sapData->vShieldedOutput[sapPoint.n].encCiphertext,
            nd.ivk,
            wtx.sapData->vShieldedOutput[sapPoint.n].ephemeralKey,
            wtx.sapData->vShieldedOutput[sapPoint.n].cmu);
    assert(static_cast<bool>(maybe_pt));
    boost::optional<libzcash::SaplingNotePlaintext> notePlainText = maybe_pt.get();
    libzcash::SaplingNote note = notePlainText->note(nd.ivk).get();

    // Append note to the tree
    auto commitment = note.cmu().get();
    SaplingMerkleTree tree;
    tree.append(commitment);
    auto anchor = tree.root();
    auto witness = tree.witness();

    // Update wtx credit chain data
    // Pretend we mined the tx by adding a fake witness and nullifier to be able to spend it.
    wtx.mapSaplingNoteData[sapPoint].witnesses.push_front(tree.witness());
    wtx.mapSaplingNoteData[sapPoint].witnessHeight = 1;
    wallet.GetSaplingScriptPubKeyMan()->nWitnessCacheSize = 1;
    wallet.GetSaplingScriptPubKeyMan()->UpdateSaplingNullifierNoteMapWithTx(wtx);
    return {note, anchor, witness};
}

/**
 * Validates:
 * 1) CWalletTx getCredit for shielded credit.
 * Incoming spendable shielded balance must be cached in the cacheableAmounts.
 *
 * 2) CWalletTx getDebit & getCredit for shielded debit to transparent address.
 * Same wallet as point (1), spending half of the credit received in (1) to a transparent remote address.
 * The other half of the balance - minus fee - must appear as credit (shielded change).
 *
 */
BOOST_AUTO_TEST_CASE(GetShieldedSimpleCachedCreditAndDebit)
{

    ///////////////////////
    //////// Credit ////////
    ///////////////////////

    auto consensusParams = RegtestActivateSapling();

    // Main wallet
    CWallet &wallet = *pwalletMain;
    LOCK2(cs_main, wallet.cs_wallet);
    setupWallet(wallet);

    // First generate a shielded address
    libzcash::SaplingPaymentAddress pa = wallet.GenerateNewSaplingZKey();
    CAmount firstCredit = COIN * 10;

    // Add shielded balance.
    libzcash::SaplingExtendedSpendingKey extskOut;
    CWalletTx& wtxUpdated = AddShieldedBalanceToWallet(pa, firstCredit, wallet, consensusParams, extskOut);

    ///////////////////////
    //////// Debit ////////
    ///////////////////////

    // Update transaction and wallet internal state to be able to spend it.
    SaplingOutPoint sapPoint {wtxUpdated.GetHash(), 0};
    SaplingSpendValues sapSpendValues = UpdateWalletInternalNotesData(wtxUpdated, sapPoint, wallet);

    // Debit value
    CAmount firstDebit = COIN * 5;
    CAmount firstDebitShieldedChange = firstDebit - fee;

    // Create the spending transaction
    auto builder = TransactionBuilder(consensusParams, 1, &wallet);
    builder.SetFee(fee);
    builder.AddSaplingSpend(
            extskOut.expsk,
            sapSpendValues.note,
            sapSpendValues.anchor,
            sapSpendValues.witness);

    // Send to transparent address
    builder.AddTransparentOutput(CreateDummyDestinationScript(),
                                 firstDebit);

    CTransaction tx = builder.Build().GetTxOrThrow();
    // add tx to wallet and update it.
    wallet.AddToWallet({&wallet, tx});
    CWalletTx& wtxDebit = wallet.mapWallet[tx.GetHash()];
    // Update tx notes data (shielded change need it)
    CWalletTx& wtxDebitUpdated = SetWalletNotesData(wallet, wtxDebit);

    // The debit need to be the entire first note value
    BOOST_CHECK_EQUAL(wtxDebitUpdated.GetDebit(ISMINE_ALL), firstCredit);
    BOOST_CHECK(wtxDebitUpdated.IsAmountCached(CWalletTx::DEBIT, ISMINE_SPENDABLE_SHIELDED));
    // The credit should be only the change.
    BOOST_CHECK_EQUAL(wtxDebitUpdated.GetCredit(ISMINE_ALL), firstDebitShieldedChange);
    BOOST_CHECK(wtxDebitUpdated.IsAmountCached(CWalletTx::CREDIT, ISMINE_SPENDABLE_SHIELDED));

    // Checks that the only shielded output of this tx is change.
    BOOST_CHECK(wallet.GetSaplingScriptPubKeyMan()->IsNoteSaplingChange(
            SaplingOutPoint(wtxDebitUpdated.GetHash(), 0), pa));

    // Revert to default
    RegtestDeactivateSapling();
}

/**
 * Validates shielded to shielded + change cached balances.
 */
BOOST_AUTO_TEST_CASE(VerifyShieldedToRemoteShieldedCachedBalance)
{
    auto consensusParams = RegtestActivateSapling();

    // Main wallet
    CWallet &wallet = *pwalletMain;
    LOCK2(cs_main, wallet.cs_wallet);
    setupWallet(wallet);

    // First generate a shielded address
    libzcash::SaplingPaymentAddress pa = wallet.GenerateNewSaplingZKey();
    CAmount firstCredit = COIN * 20;

    // Add shielded balance.
    libzcash::SaplingExtendedSpendingKey extskOut;
    CWalletTx& wtxUpdated = AddShieldedBalanceToWallet(pa, firstCredit, wallet, consensusParams, extskOut);

    // Update transaction and wallet internal state to be able to spend it.
    SaplingOutPoint sapPoint {wtxUpdated.GetHash(), 0};
    SaplingSpendValues sapSpendValues = UpdateWalletInternalNotesData(wtxUpdated, sapPoint, wallet);

    // New dummy wallet to create an outgoing shielded address
    CWallet dummyWallet;
    dummyWallet.SetMinVersion(FEATURE_SAPLING);
    dummyWallet.SetupSPKM(false, true);
    // Destination values
    libzcash::SaplingPaymentAddress destShieldedAddress = dummyWallet.GenerateNewSaplingZKey();
    CAmount destAmount = COIN * 8;

    // Create the spending transaction
    auto builder = TransactionBuilder(consensusParams, 1, &wallet);
    builder.SetFee(fee);
    builder.AddSaplingSpend(
            extskOut.expsk,
            sapSpendValues.note,
            sapSpendValues.anchor,
            sapSpendValues.witness);

    // Send to shielded address
    builder.AddSaplingOutput(
            extskOut.expsk.ovk,
            destShieldedAddress,
            destAmount,
            {}
            );

    CTransaction tx = builder.Build().GetTxOrThrow();
    // add tx to wallet and update it.
    wallet.AddToWallet({&wallet, tx});
    CWalletTx& wtxDebit = wallet.mapWallet[tx.GetHash()];
    // Update tx notes data (shielded change need it)
    CWalletTx& wtxDebitUpdated = SetWalletNotesData(wallet, wtxDebit);

    // Validate results
    CAmount expectedShieldedChange = firstCredit - destAmount - fee;

    // The debit need to be the entire first note value
    BOOST_CHECK_EQUAL(wtxDebitUpdated.GetDebit(ISMINE_ALL), firstCredit);
    BOOST_CHECK(wtxDebitUpdated.IsAmountCached(CWalletTx::DEBIT, ISMINE_SPENDABLE_SHIELDED));
    // The credit should be only the change.
    BOOST_CHECK_EQUAL(wtxDebitUpdated.GetCredit(ISMINE_ALL), expectedShieldedChange);
    BOOST_CHECK(wtxDebitUpdated.IsAmountCached(CWalletTx::CREDIT, ISMINE_SPENDABLE_SHIELDED));

    // Revert to default
    RegtestDeactivateSapling();
}

BOOST_AUTO_TEST_SUITE_END()
