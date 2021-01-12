// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2020-2021 The Sapphire Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "chain.h"
#include "chainparams.h"
#include "main.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"

#include <math.h>


unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader* pblock)
{
    if (Params().IsRegTestNet())
        return pindexLast->nBits;

    const CBlockIndex* BlockReading = pindexLast;
    int64_t nActualTimespan = 0;
    int64_t LastBlockTime = 0;
    
    const Consensus::Params& consensus = Params().GetConsensus();

    if (BlockReading == NULL || BlockReading->nHeight == 0) {
        return consensus.powLimit.GetCompact();
    }

    int nHeight = pindexLast->nHeight + 1;

    const bool fTimeV2 = !Params().IsRegTestNet() && consensus.IsTimeProtocolV2(nHeight);

    int64_t nTargetSpacing = consensus.nTargetSpacing;
    int64_t PastBlocks;

    if(nHeight % ((24 * 60 * 60) / nTargetSpacing) == 0) { // 24h interval
        PastBlocks = (24 * 60 * 60) / nTargetSpacing;
    } else if(nHeight % ((12 * 60 * 60) / nTargetSpacing) == 0) { // 12 h interval
        PastBlocks = (12 * 60 * 60) / nTargetSpacing;
    } else if(nHeight % ((6 * 60 * 60) / nTargetSpacing) == 0) { // 6 h interval
        PastBlocks = (6 * 60 * 60) / nTargetSpacing;
    } else if(nHeight % ((3 * 60 * 60) / nTargetSpacing) == 0) { // 3 h interval
        PastBlocks = (3 * 60 * 60) / nTargetSpacing;
    } else if(nHeight % ((1 * 60 * 60) / nTargetSpacing) == 0) { // 1 h interval
        PastBlocks = (1 * 60 * 60) / nTargetSpacing;
    } else { // 10 min by default
        PastBlocks = (10 * 60) / nTargetSpacing;
    }

    if (BlockReading->nHeight < PastBlocks) {
        return consensus.powLimit.GetCompact();
    }

    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (PastBlocks > 0 && i > PastBlocks) {
            break;
        }

        if (LastBlockTime > 0) { // if not the first one
            int64_t Diff = (LastBlockTime - BlockReading->GetBlockTime());
            nActualTimespan += Diff;
        }
        LastBlockTime = BlockReading->GetBlockTime();

        if (BlockReading->pprev == NULL) { // this shouldn't happen
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);

    int64_t nTargetTimespan = PastBlocks * nTargetSpacing;

    if (nActualTimespan < nTargetTimespan / 3)
        nActualTimespan = nTargetTimespan / 3;
    if (nActualTimespan > nTargetTimespan * 3)
        nActualTimespan = nTargetTimespan * 3;

    // on first block with V2 time protocol, reduce the difficulty by a factor 16
    if (fTimeV2 && !consensus.IsTimeProtocolV2(pindexLast->nHeight))
        bnNew <<= 4;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;

    if (bnNew > consensus.powLimit) {
        bnNew = consensus.powLimit;
    }

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits)
{
    bool fNegative;
    bool fOverflow;
    uint256 bnTarget;

    if (Params().IsRegTestNet()) return true;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget.IsNull() || fOverflow || bnTarget > Params().GetConsensus().powLimit)
        return error("CheckProofOfWork() : nBits below minimum work");

    // Check proof of work matches claimed amount
    if (hash > bnTarget)
        return error("CheckProofOfWork() : hash doesn't match nBits");

    return true;
}

uint256 GetBlockProof(const CBlockIndex& block)
{
    uint256 bnTarget;
    bool fNegative;
    bool fOverflow;
    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget.IsNull())
        return UINT256_ZERO;
    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for a uint256. However, as 2**256 is at least as large
    // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
    // or ~bnTarget / (nTarget+1) + 1.
    return (~bnTarget / (bnTarget + 1)) + 1;
}
