// Copyright (c) 2016-2020 The Zcash developers
// Copyright (c) 2020 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "utiltest.h"

#include "key_io.h"
#include "consensus/upgrades.h"
#include "sapling/transaction_builder.h"

#include <array>

static const std::string T_SECRET_REGTEST = "cND2ZvtabDbJ1gucx9GWH6XT9kgTAqfb6cotPt5Q5CyxVDhid2EN";

const Consensus::Params& RegtestActivateSapling() {
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V5_DUMMY, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    g_IsSaplingActive = true;
    return Params().GetConsensus();
}

void RegtestDeactivateSapling() {
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V5_DUMMY, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    g_IsSaplingActive = false;
}

libzcash::SaplingExtendedSpendingKey GetTestMasterSaplingSpendingKey() {
    std::vector<unsigned char, secure_allocator<unsigned char>> rawSeed(32);
    HDSeed seed(rawSeed);
    return libzcash::SaplingExtendedSpendingKey::Master(seed);
}

CKey CreateCkey(bool genNewKey) {
    CKey tsk;
    if (genNewKey) tsk.MakeNewKey(true);
    else tsk = KeyIO::DecodeSecret(T_SECRET_REGTEST);
    if (!tsk.IsValid()) throw std::runtime_error("CreateCkey:: Invalid priv key");
    return tsk;
}

CKey AddTestCKeyToKeyStore(CBasicKeyStore& keyStore, bool genNewKey) {
    CKey tsk = CreateCkey(genNewKey);
    keyStore.AddKey(tsk);
    return tsk;
}

TestSaplingNote GetTestSaplingNote(const libzcash::SaplingPaymentAddress& pa, CAmount value) {
    // Generate dummy Sapling note
    libzcash::SaplingNote note(pa, value);
    uint256 cm = note.cmu().get();
    SaplingMerkleTree tree;
    tree.append(cm);
    return { note, tree };
}

CWalletTx GetValidSaplingReceive(const Consensus::Params& consensusParams,
                                 CBasicKeyStore& keyStore,
                                 const libzcash::SaplingExtendedSpendingKey &sk,
                                 CAmount value,
                                 bool genNewKey,
                                 const CWallet* pwalletIn) {
    // From taddr
    CKey tsk = AddTestCKeyToKeyStore(keyStore, genNewKey);
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());
    // To shielded addr
    auto fvk = sk.expsk.full_viewing_key();
    auto pa = sk.DefaultAddress();

    auto builder = TransactionBuilder(consensusParams, 1, &keyStore);
    builder.SetFee(0);
    builder.AddTransparentInput(COutPoint(), scriptPubKey, value);
    builder.AddSaplingOutput(fvk.ovk, pa, value, {});

    CTransaction tx = builder.Build().GetTxOrThrow();
    CWalletTx wtx {pwalletIn, tx};
    return wtx;
}

CWalletTx GetValidSaplingReceive(const Consensus::Params& consensusParams,
                                 const libzcash::SaplingExtendedSpendingKey &sk,
                                 CAmount value,
                                 const CWallet* pwalletIn) {
    // Dummy wallet, used to generate the dummy transparent input key and sign it in the transaction builder
    CWallet wallet;
    wallet.SetMinVersion(FEATURE_SAPLING);
    wallet.SetupSPKM(false, true);

    CWalletTx tx = GetValidSaplingReceive(
            consensusParams,
            wallet,
            sk,
            value,
            true,
            pwalletIn
            );
    return tx;
}

CScript CreateDummyDestinationScript() {
    CKey key = CreateCkey(true);
    return GetScriptForDestination(key.GetPubKey().GetID());
}