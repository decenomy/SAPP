// Copyright (c) 2016-2020 The ZCash developers
// Copyright (c) 2020 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "sapling/sapling_validation.h"

#include "consensus/consensus.h" // for MAX_BLOCK_SIZE_CURRENT
#include "validation.h" // for MAX_ZEROCOIN_TX_SIZE
#include "script/interpreter.h" // for SigHash
#include "consensus/validation.h" // for CValidationState
#include "util.h" // for error()
#include "consensus/upgrades.h" // for CurrentEpochBranchId()

#include <librustzcash.h>

namespace SaplingValidation {

/**
* Check a transaction contextually against a set of consensus rules valid at a given block height.
*
* Notes:
* 1. AcceptToMemoryPool calls CheckTransaction and this function.
* 2. ProcessNewBlock calls AcceptBlock, which calls CheckBlock (which calls CheckTransaction)
*    and ContextualCheckBlock (which calls this function).
* 3. For consensus rules that relax restrictions (where a transaction that is invalid at
*    nHeight can become valid at a later height), we make the bans conditional on not
*    being in Initial Block Download mode.
* 4. The isInitBlockDownload argument is a function parameter to assist with testing.
*
* todo: For now, this is NOT connected, only used in unit tests.
*/
bool ContextualCheckTransaction(
        const CTransaction& tx,
        CValidationState &state,
        const CChainParams& chainparams,
        const int nHeight,
        const bool isMined,
        bool isInitBlockDownload)
{
    const int DOS_LEVEL_BLOCK = 100;
    // DoS level set to 10 to be more forgiving.
    const int DOS_LEVEL_MEMPOOL = 10;

    // For constricting rules, we don't need to account for IBD mode.
    auto dosLevelConstricting = isMined ? DOS_LEVEL_BLOCK : DOS_LEVEL_MEMPOOL;
    // For rules that are relaxing (or might become relaxing when a future
    // network upgrade is implemented), we need to account for IBD mode.
    auto dosLevelPotentiallyRelaxing = isMined ? DOS_LEVEL_BLOCK : (
            isInitBlockDownload ? 0 : DOS_LEVEL_MEMPOOL);

    // If Sapling is not active return quickly
    if (!chainparams.GetConsensus().NetworkUpgradeActive(nHeight, Consensus::UPGRADE_V5_DUMMY)) {
        return state.DoS(
                dosLevelConstricting,
                error("%s: Sapling not active", __func__ ),
                REJECT_INVALID, "bad-tx-sapling-not-active");
    }

    // Reject transactions with invalid version
    if (tx.nVersion < CTransaction::SAPLING_VERSION ) {
        return state.DoS(
                dosLevelConstricting,
                error("%s: Sapling version too low", __func__ ),
                REJECT_INVALID, "bad-tx-sapling-version-too-low");
    }

    // Reject transactions with invalid version
    if (tx.nVersion > CTransaction::SAPLING_VERSION ) {
        return state.DoS(
                dosLevelPotentiallyRelaxing,
                error("%s: Sapling version too high", __func__),
                REJECT_INVALID, "bad-tx-sapling-version-too-high");
    }

    // Size limits
    BOOST_STATIC_ASSERT(MAX_BLOCK_SIZE_CURRENT > MAX_ZEROCOIN_TX_SIZE); // sanity
    if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) > MAX_ZEROCOIN_TX_SIZE)
        return state.DoS(
                dosLevelPotentiallyRelaxing,
                error("%s: size limits failed", __func__ ),
                REJECT_INVALID, "bad-txns-oversize");

    bool hasShieldedData = tx.hasSaplingData();
    // A coinbase/coinstake transaction cannot have output descriptions nor shielded spends
    if (tx.IsCoinBase() || tx.IsCoinStake()) {
        if (hasShieldedData)
            return state.DoS(
                    dosLevelPotentiallyRelaxing,
                    error("%s: coinbase/coinstake has output/spend descriptions", __func__ ),
                    REJECT_INVALID, "bad-cs-has-shielded-data");
    }

    if (hasShieldedData) {
        uint256 dataToBeSigned;
        // Empty output script.
        CScript scriptCode;
        try {
            dataToBeSigned = SignatureHash(scriptCode, tx, NOT_AN_INPUT, SIGHASH_ALL, 0, SIGVERSION_SAPLING);
        } catch (const std::logic_error& ex) {
            // A logic error should never occur because we pass NOT_AN_INPUT and
            // SIGHASH_ALL to SignatureHash().
            return state.DoS(100, error("%s: error computing signature hash", __func__ ),
                             REJECT_INVALID, "error-computing-signature-hash");
        }

        // Sapling verification process
        auto ctx = librustzcash_sapling_verification_ctx_init();

        for (const SpendDescription &spend : tx.sapData->vShieldedSpend) {
            if (!librustzcash_sapling_check_spend(
                    ctx,
                    spend.cv.begin(),
                    spend.anchor.begin(),
                    spend.nullifier.begin(),
                    spend.rk.begin(),
                    spend.zkproof.begin(),
                    spend.spendAuthSig.begin(),
                    dataToBeSigned.begin())) {
                librustzcash_sapling_verification_ctx_free(ctx);
                return state.DoS(
                        dosLevelPotentiallyRelaxing,
                        error("%s: Sapling spend description invalid", __func__ ),
                        REJECT_INVALID, "bad-txns-sapling-spend-description-invalid");
            }
        }

        for (const OutputDescription &output : tx.sapData->vShieldedOutput) {
            if (!librustzcash_sapling_check_output(
                    ctx,
                    output.cv.begin(),
                    output.cmu.begin(),
                    output.ephemeralKey.begin(),
                    output.zkproof.begin())) {
                librustzcash_sapling_verification_ctx_free(ctx);
                // This should be a non-contextual check, but we check it here
                // as we need to pass over the outputs anyway in order to then
                // call librustzcash_sapling_final_check().
                return state.DoS(100, error("%s: Sapling output description invalid", __func__ ),
                                 REJECT_INVALID, "bad-txns-sapling-output-description-invalid");
            }
        }

        if (!librustzcash_sapling_final_check(
                ctx,
                tx.sapData->valueBalance,
                tx.sapData->bindingSig.begin(),
                dataToBeSigned.begin())) {
            librustzcash_sapling_verification_ctx_free(ctx);
            return state.DoS(
                    dosLevelPotentiallyRelaxing,
                    error("%s: Sapling binding signature invalid", __func__ ),
                    REJECT_INVALID, "bad-txns-sapling-binding-signature-invalid");
        }

        librustzcash_sapling_verification_ctx_free(ctx);
    }
    return true;
}


} // End SaplingValidation namespace