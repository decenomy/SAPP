// Copyright (c) 2018-2020 The Zcash developers
// Copyright (c) 2020 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#ifndef TRANSACTION_BUILDER_H
#define TRANSACTION_BUILDER_H

#include "coins.h"
#include "consensus/params.h"
#include "keystore.h"
#include "optional.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "script/standard.h"
#include "uint256.h"
#include "sapling/address.hpp"
#include "sapling/incrementalmerkletree.hpp"
#include "sapling/note.hpp"
#include "sapling/noteencryption.hpp"

struct SpendDescriptionInfo {
    libzcash::SaplingExpandedSpendingKey expsk;
    libzcash::SaplingNote note;
    uint256 alpha;
    uint256 anchor;
    SaplingWitness witness;

    SpendDescriptionInfo(
        libzcash::SaplingExpandedSpendingKey expsk,
        libzcash::SaplingNote note,
        uint256 anchor,
        SaplingWitness witness);
};

struct OutputDescriptionInfo {
    uint256 ovk;
    libzcash::SaplingNote note;
    std::array<unsigned char, ZC_MEMO_SIZE> memo;

    OutputDescriptionInfo(
        uint256 ovk,
        libzcash::SaplingNote note,
        std::array<unsigned char, ZC_MEMO_SIZE> memo) : ovk(ovk), note(note), memo(memo) {}

    Optional<OutputDescription> Build(void* ctx);
};

struct TransparentInputInfo {
    CScript scriptPubKey;
    CAmount value;

    TransparentInputInfo(
        CScript scriptPubKey,
        CAmount value) : scriptPubKey(scriptPubKey), value(value) {}
};

class TransactionBuilderResult {
private:
    Optional<CTransaction> maybeTx;
    Optional<std::string> maybeError;
public:
    TransactionBuilderResult() = delete;
    TransactionBuilderResult(const CTransaction& tx);
    TransactionBuilderResult(const std::string& error);
    bool IsTx();
    bool IsError();
    CTransaction GetTxOrThrow();
    Optional<CTransaction> GetTx();
    std::string GetError();
};

class TransactionBuilder
{
private:
    Consensus::Params consensusParams;
    int nHeight;
    const CKeyStore* keystore;
    CMutableTransaction mtx;
    CAmount fee = 10000;

    std::vector<SpendDescriptionInfo> spends;
    std::vector<OutputDescriptionInfo> outputs;
    std::vector<TransparentInputInfo> tIns;

    Optional<std::pair<uint256, libzcash::SaplingPaymentAddress>> saplingChangeAddr;
    Optional<CTxDestination> tChangeAddr;

public:
    TransactionBuilder(
        const Consensus::Params& consensusParams,
        int nHeight,
        CKeyStore* keyStore = nullptr);

    void SetFee(CAmount _fee);

    // Throws if the anchor does not match the anchor used by
    // previously-added Sapling spends.
    void AddSaplingSpend(
        libzcash::SaplingExpandedSpendingKey expsk,
        libzcash::SaplingNote note,
        uint256 anchor,
        SaplingWitness witness);

    void AddSaplingOutput(
        uint256 ovk,
        libzcash::SaplingPaymentAddress to,
        CAmount value,
        std::array<unsigned char, ZC_MEMO_SIZE> memo = {{0xF6}});

    // Assumes that the value correctly corresponds to the provided UTXO.
    void AddTransparentInput(COutPoint utxo, CScript scriptPubKey, CAmount value);

    void AddTransparentOutput(const CTxDestination& to, CAmount value);

    void SendChangeTo(libzcash::SaplingPaymentAddress changeAddr, uint256 ovk);

    void SendChangeTo(CTxDestination& changeAddr);

    TransactionBuilderResult Build();
};

#endif /* TRANSACTION_BUILDER_H */
