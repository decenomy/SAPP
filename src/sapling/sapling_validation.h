// Copyright (c) 2016-2020 The ZCash developers
// Copyright (c) 2020 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_SAPLING_VALIDATION_H
#define PIVX_SAPLING_VALIDATION_H

#include "chainparams.h"

class CTransaction;
class CValidationState;

namespace SaplingValidation {

/** Check a transaction contextually against a set of consensus rules */
bool ContextualCheckTransaction(const CTransaction &tx, CValidationState &state,
                                const CChainParams &chainparams, int nHeight, bool isMined,
                                bool sInitBlockDownload);

}; // End SaplingValidation namespace

#endif //PIVX_SAPLING_VALIDATION_H
