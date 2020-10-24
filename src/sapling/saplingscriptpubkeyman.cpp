// Copyright (c) 2016-2020 The ZCash developers
// Copyright (c) 2020 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sapling/saplingscriptpubkeyman.h"
#include "sapling/note.hpp"
#include "chain.h" // for CBlockIndex
#include "validation.h" // for ReadBlockFromDisk()

void SaplingScriptPubKeyMan::AddToSaplingSpends(const uint256& nullifier, const uint256& wtxid)
{
    mapTxSaplingNullifiers.emplace(nullifier, wtxid);

    std::pair<TxNullifiers::iterator, TxNullifiers::iterator> range;
    range = mapTxSaplingNullifiers.equal_range(nullifier);
    wallet->SyncMetaDataN(range);
}

bool SaplingScriptPubKeyMan::IsSaplingSpent(const uint256& nullifier) const {
    LOCK(cs_main);
    std::pair<TxNullifiers::const_iterator, TxNullifiers::const_iterator> range;
    range = mapTxSaplingNullifiers.equal_range(nullifier);

    for (TxNullifiers::const_iterator it = range.first; it != range.second; ++it) {
        const uint256& wtxid = it->second;
        std::map<uint256, CWalletTx>::const_iterator mit = wallet->mapWallet.find(wtxid);
        if (mit != wallet->mapWallet.end() && mit->second.GetDepthInMainChain() >= 0) {
            return true; // Spent
        }
    }
    return false;
}

void SaplingScriptPubKeyMan::UpdateSaplingNullifierNoteMapForBlock(const CBlock *pblock) {
    LOCK(wallet->cs_wallet);

    for (const auto& tx : pblock->vtx) {
        const uint256& hash = tx->GetHash();
        bool txIsOurs = wallet->mapWallet.count(hash);
        if (txIsOurs) {
            UpdateSaplingNullifierNoteMapWithTx(wallet->mapWallet[hash]);
        }
    }
}

/**
 * Update mapSaplingNullifiersToNotes, computing the nullifier from a cached witness if necessary.
 */
void SaplingScriptPubKeyMan::UpdateSaplingNullifierNoteMapWithTx(CWalletTx& wtx) {
    LOCK(wallet->cs_wallet);

    for (mapSaplingNoteData_t::value_type &item : wtx.mapSaplingNoteData) {
        SaplingOutPoint op = item.first;
        SaplingNoteData nd = item.second;

        if (nd.witnesses.empty()) {
            // If there are no witnesses, erase the nullifier and associated mapping.
            if (item.second.nullifier) {
                mapSaplingNullifiersToNotes.erase(item.second.nullifier.get());
            }
            item.second.nullifier = boost::none;
        }
        else {
            uint64_t position = nd.witnesses.front().position();
            auto extfvk = wallet->mapSaplingFullViewingKeys.at(nd.ivk);
            OutputDescription output = wtx.sapData->vShieldedOutput[op.n];
            auto optPlaintext = libzcash::SaplingNotePlaintext::decrypt(output.encCiphertext, nd.ivk, output.ephemeralKey, output.cmu);
            if (!optPlaintext) {
                // An item in mapSaplingNoteData must have already been successfully decrypted,
                // otherwise the item would not exist in the first place.
                assert(false);
            }
            auto optNote = optPlaintext.get().note(nd.ivk);
            if (!optNote) {
                assert(false);
            }
            auto optNullifier = optNote.get().nullifier(extfvk.fvk, position);
            if (!optNullifier) {
                // This should not happen.  If it does, maybe the position has been corrupted or miscalculated?
                assert(false);
            }
            uint256 nullifier = optNullifier.get();
            mapSaplingNullifiersToNotes[nullifier] = op;
            item.second.nullifier = nullifier;
        }
    }
}

/**
 * Update mapSaplingNullifiersToNotes with the cached nullifiers in this tx.
 */
void SaplingScriptPubKeyMan::UpdateNullifierNoteMapWithTx(const CWalletTx& wtx)
{
    {
        LOCK(wallet->cs_wallet);
        for (const mapSaplingNoteData_t::value_type& item : wtx.mapSaplingNoteData) {
            if (item.second.nullifier) {
                mapSaplingNullifiersToNotes[*item.second.nullifier] = item.first;
            }
        }
    }
}

template<typename NoteDataMap>
void CopyPreviousWitnesses(NoteDataMap& noteDataMap, int indexHeight, int64_t nWitnessCacheSize)
{
    for (auto& item : noteDataMap) {
        auto* nd = &(item.second);
        // Only increment witnesses that are behind the current height
        if (nd->witnessHeight < indexHeight) {
            // Check the validity of the cache
            // The only time a note witnessed above the current height
            // would be invalid here is during a reindex when blocks
            // have been decremented, and we are incrementing the blocks
            // immediately after.
            assert(nWitnessCacheSize >= (int64_t) nd->witnesses.size());
            // Witnesses being incremented should always be either -1
            // (never incremented or decremented) or one below indexHeight
            assert((nd->witnessHeight == -1) || (nd->witnessHeight == indexHeight - 1));
            // Copy the witness for the previous block if we have one
            if (nd->witnesses.size() > 0) {
                nd->witnesses.push_front(nd->witnesses.front());
            }
            if (nd->witnesses.size() > WITNESS_CACHE_SIZE) {
                nd->witnesses.pop_back();
            }
        }
    }
}

template<typename NoteDataMap>
void AppendNoteCommitment(NoteDataMap& noteDataMap, int indexHeight, int64_t nWitnessCacheSize, const uint256& note_commitment)
{
    for (auto& item : noteDataMap) {
        auto* nd = &(item.second);
        if (nd->witnessHeight < indexHeight && nd->witnesses.size() > 0) {
            // Check the validity of the cache
            // See comment in CopyPreviousWitnesses about validity.
            assert(nWitnessCacheSize >= (int64_t) nd->witnesses.size());
            nd->witnesses.front().append(note_commitment);
        }
    }
}

template<typename OutPoint, typename NoteData, typename Witness>
void WitnessNoteIfMine(std::map<OutPoint, NoteData>& noteDataMap, int indexHeight, int64_t nWitnessCacheSize, const OutPoint& key, const Witness& witness)
{
    if (noteDataMap.count(key) && noteDataMap[key].witnessHeight < indexHeight) {
        auto* nd = &(noteDataMap[key]);
        if (nd->witnesses.size() > 0) {
            // We think this can happen because we write out the
            // witness cache state after every block increment or
            // decrement, but the block index itself is written in
            // batches. So if the node crashes in between these two
            // operations, it is possible for IncrementNoteWitnesses
            // to be called again on previously-cached blocks. This
            // doesn't affect existing cached notes because of the
            // NoteData::witnessHeight checks. See #1378 for details.
            LogPrintf("Inconsistent witness cache state found for %s\n- Cache size: %d\n- Top (height %d): %s\n- New (height %d): %s\n",
                      key.ToString(), nd->witnesses.size(),
                      nd->witnessHeight,
                      nd->witnesses.front().root().GetHex(),
                      indexHeight,
                      witness.root().GetHex());
            nd->witnesses.clear();
        }
        nd->witnesses.push_front(witness);
        // Set height to one less than pindex so it gets incremented
        nd->witnessHeight = indexHeight - 1;
        // Check the validity of the cache
        assert(nWitnessCacheSize >= (int64_t) nd->witnesses.size());
    }
}

template<typename NoteDataMap>
void UpdateWitnessHeights(NoteDataMap& noteDataMap, int indexHeight, int64_t nWitnessCacheSize)
{
    for (auto& item : noteDataMap) {
        auto* nd = &(item.second);
        if (nd->witnessHeight < indexHeight) {
            nd->witnessHeight = indexHeight;
            // Check the validity of the cache
            // See comment in CopyPreviousWitnesses about validity.
            assert(nWitnessCacheSize >= (int64_t) nd->witnesses.size());
        }
    }
}

void SaplingScriptPubKeyMan::IncrementNoteWitnesses(const CBlockIndex* pindex,
                                     const CBlock* pblockIn,
                                     SaplingMerkleTree& saplingTree)
{
    LOCK(wallet->cs_wallet);
    int chainHeight = pindex->nHeight;
    for (std::pair<const uint256, CWalletTx>& wtxItem : wallet->mapWallet) {
        ::CopyPreviousWitnesses(wtxItem.second.mapSaplingNoteData, chainHeight, nWitnessCacheSize);
    }

    if (nWitnessCacheSize < WITNESS_CACHE_SIZE) {
        nWitnessCacheSize += 1;
        nWitnessCacheNeedsUpdate = true;
    }

    const CBlock* pblock {pblockIn};
    CBlock block;
    if (!pblock) {
        ReadBlockFromDisk(block, pindex);
        pblock = &block;
    }

    for (const auto& tx : pblock->vtx) {
        if (!tx->hasSaplingData()) continue;

        const uint256& hash = tx->GetHash();
        bool txIsOurs = wallet->mapWallet.count(hash);

        // Sapling
        for (uint32_t i = 0; i < tx->sapData->vShieldedOutput.size(); i++) {
            const uint256& note_commitment = tx->sapData->vShieldedOutput[i].cmu;
            saplingTree.append(note_commitment);

            // Increment existing witnesses
            for (std::pair<const uint256, CWalletTx>& wtxItem : wallet->mapWallet) {
                ::AppendNoteCommitment(wtxItem.second.mapSaplingNoteData, chainHeight, nWitnessCacheSize, note_commitment);
            }

            // If this is our note, witness it
            if (txIsOurs) {
                SaplingOutPoint outPoint {hash, i};
                ::WitnessNoteIfMine(wallet->mapWallet[hash].mapSaplingNoteData, chainHeight, nWitnessCacheSize, outPoint, saplingTree.witness());
            }
        }

    }

    // Update witness heights
    for (std::pair<const uint256, CWalletTx>& wtxItem : wallet->mapWallet) {
        ::UpdateWitnessHeights(wtxItem.second.mapSaplingNoteData, chainHeight, nWitnessCacheSize);
    }

    // For performance reasons, we write out the witness cache in
    // CWallet::SetBestChain() (which also ensures that overall consistency
    // of the wallet.dat is maintained).
}

template<typename NoteDataMap>
void DecrementNoteWitnesses(NoteDataMap& noteDataMap, int indexHeight, int64_t nWitnessCacheSize)
{
    for (auto& item : noteDataMap) {
        auto* nd = &(item.second);
        // Only decrement witnesses that are not above the current height
        if (nd->witnessHeight <= indexHeight) {
            // Check the validity of the cache
            // See comment below (this would be invalid if there were a
            // prior decrement).
            assert(nWitnessCacheSize >= (int64_t) nd->witnesses.size());
            // Witnesses being decremented should always be either -1
            // (never incremented or decremented) or equal to the height
            // of the block being removed (indexHeight)
            assert((nd->witnessHeight == -1) || (nd->witnessHeight == indexHeight));
            if (nd->witnesses.size() > 0) {
                nd->witnesses.pop_front();
            }
            // indexHeight is the height of the block being removed, so
            // the new witness cache height is one below it.
            nd->witnessHeight = indexHeight - 1;
        }
        // Check the validity of the cache
        // Technically if there are notes witnessed above the current
        // height, their cache will now be invalid (relative to the new
        // value of nWitnessCacheSize). However, this would only occur
        // during a reindex, and by the time the reindex reaches the tip
        // of the chain again, the existing witness caches will be valid
        // again.
        // We don't set nWitnessCacheSize to zero at the start of the
        // reindex because the on-disk blocks had already resulted in a
        // chain that didn't trigger the assertion below.
        if (nd->witnessHeight < indexHeight) {
            // Subtract 1 to compare to what nWitnessCacheSize will be after
            // decrementing.
            assert((nWitnessCacheSize - 1) >= (int64_t) nd->witnesses.size());
        }
    }
}

void SaplingScriptPubKeyMan::DecrementNoteWitnesses(const CBlockIndex* pindex)
{
    LOCK(wallet->cs_wallet);
    for (std::pair<const uint256, CWalletTx>& wtxItem : wallet->mapWallet) {
        ::DecrementNoteWitnesses(wtxItem.second.mapSaplingNoteData, pindex->nHeight, nWitnessCacheSize);
    }
    nWitnessCacheSize -= 1;
    nWitnessCacheNeedsUpdate = true;
    // TODO: If nWitnessCache is zero, we need to regenerate the caches (#1302)
    if (Params().IsRegTestNet()) { // throw an error in regtest to be able to catch it from the sapling_wallet_tests.cpp unit test.
        if (nWitnessCacheSize <= 0) throw std::runtime_error("nWitnessCacheSize > 0");
    } else assert(nWitnessCacheSize > 0);

    // For performance reasons, we write out the witness cache in
    // CWallet::SetBestChain() (which also ensures that overall consistency
    // of the wallet.dat is maintained).
}

/**
 * Finds all output notes in the given transaction that have been sent to
 * SaplingPaymentAddresses in this wallet.
 *
 * It should never be necessary to call this method with a CWalletTx, because
 * the result of FindMySaplingNotes (for the addresses available at the time) will
 * already have been cached in CWalletTx.mapSaplingNoteData.
 */
std::pair<mapSaplingNoteData_t, SaplingIncomingViewingKeyMap> SaplingScriptPubKeyMan::FindMySaplingNotes(const CTransaction &tx) const
{
    // First check that this tx is a Sapling tx.
    if (!tx.isSapling() || !tx.hasSaplingData()) {
        return {};
    }

    LOCK(wallet->cs_KeyStore);
    const uint256& hash = tx.GetHash();

    mapSaplingNoteData_t noteData;
    SaplingIncomingViewingKeyMap viewingKeysToAdd;

    // Protocol Spec: 4.19 Block Chain Scanning (Sapling)
    for (uint32_t i = 0; i < tx.sapData->vShieldedOutput.size(); ++i) {
        const OutputDescription output = tx.sapData->vShieldedOutput[i];
        for (auto it = wallet->mapSaplingFullViewingKeys.begin(); it != wallet->mapSaplingFullViewingKeys.end(); ++it) {
            libzcash::SaplingIncomingViewingKey ivk = it->first;
            auto result = libzcash::SaplingNotePlaintext::decrypt(output.encCiphertext, ivk, output.ephemeralKey, output.cmu);
            if (!result) {
                continue;
            }

            // Check if we already have it.
            auto address = ivk.address(result.get().d);
            if (address && wallet->mapSaplingIncomingViewingKeys.count(address.get()) == 0) {
                viewingKeysToAdd[address.get()] = ivk;
            }
            // We don't cache the nullifier here as computing it requires knowledge of the note position
            // in the commitment tree, which can only be determined when the transaction has been mined.
            SaplingOutPoint op {hash, i};
            SaplingNoteData nd;
            nd.ivk = ivk;
            noteData.insert(std::make_pair(op, nd));
            break;
        }
    }

    return std::make_pair(noteData, viewingKeysToAdd);
}

bool SaplingScriptPubKeyMan::IsSaplingNullifierFromMe(const uint256& nullifier) const
{
    LOCK(wallet->cs_wallet);
    return mapSaplingNullifiersToNotes.count(nullifier) &&
        wallet->mapWallet.count(mapSaplingNullifiersToNotes.at(nullifier).hash);
}

void SaplingScriptPubKeyMan::GetSaplingNoteWitnesses(const std::vector<SaplingOutPoint>& notes,
                                      std::vector<Optional<SaplingWitness>>& witnesses,
                                      uint256& final_anchor)
{
    LOCK(wallet->cs_wallet);
    witnesses.resize(notes.size());
    Optional<uint256> rt;
    int i = 0;
    for (SaplingOutPoint note : notes) {
        if (wallet->mapWallet.count(note.hash) &&
            wallet->mapWallet[note.hash].mapSaplingNoteData.count(note) &&
            wallet->mapWallet[note.hash].mapSaplingNoteData[note].witnesses.size() > 0) {
            witnesses[i] = wallet->mapWallet[note.hash].mapSaplingNoteData[note].witnesses.front();
            if (!rt) {
                rt = witnesses[i]->root();
            } else {
                assert(*rt == witnesses[i]->root());
            }
        }
        i++;
    }
    // All returned witnesses have the same anchor
    if (rt) {
        final_anchor = *rt;
    }
}

bool SaplingScriptPubKeyMan::UpdatedNoteData(const CWalletTx& wtxIn, CWalletTx& wtx)
{
    bool unchangedSaplingFlag = (wtxIn.mapSaplingNoteData.empty() || wtxIn.mapSaplingNoteData == wtx.mapSaplingNoteData);
    if (!unchangedSaplingFlag) {
        auto tmp = wtxIn.mapSaplingNoteData;
        // Ensure we keep any cached witnesses we may already have

        for (const std::pair <SaplingOutPoint, SaplingNoteData> nd : wtx.mapSaplingNoteData) {
            if (tmp.count(nd.first) && nd.second.witnesses.size() > 0) {
                tmp.at(nd.first).witnesses.assign(
                        nd.second.witnesses.cbegin(), nd.second.witnesses.cend());
            }
            tmp.at(nd.first).witnessHeight = nd.second.witnessHeight;
        }

        // Now copy over the updated note data
        wtx.mapSaplingNoteData = tmp;
    }

    return !unchangedSaplingFlag;
}

void SaplingScriptPubKeyMan::ClearNoteWitnessCache()
{
    LOCK(wallet->cs_wallet);
    for (std::pair<const uint256, CWalletTx>& wtxItem : wallet->mapWallet) {
        for (mapSaplingNoteData_t::value_type& item : wtxItem.second.mapSaplingNoteData) {
            item.second.witnesses.clear();
            item.second.witnessHeight = -1;
        }
    }
    nWitnessCacheSize = 0;
    nWitnessCacheNeedsUpdate = true;
}

Optional<libzcash::SaplingExtendedSpendingKey> SaplingScriptPubKeyMan::GetSpendingKeyForPaymentAddress(const libzcash::SaplingPaymentAddress &addr) const
{
    libzcash::SaplingExtendedSpendingKey extsk;
    if (wallet->GetSaplingExtendedSpendingKey(addr, extsk)) {
        return extsk;
    } else {
        return nullopt;
    }
}

Optional<libzcash::SaplingExtendedFullViewingKey> SaplingScriptPubKeyMan::GetViewingKeyForPaymentAddress(
        const libzcash::SaplingPaymentAddress &addr) const
{
    libzcash::SaplingIncomingViewingKey ivk;
    libzcash::SaplingExtendedFullViewingKey extfvk;

    if (wallet->GetSaplingIncomingViewingKey(addr, ivk) &&
        wallet->GetSaplingFullViewingKey(ivk, extfvk))
    {
        return extfvk;
    } else {
        return nullopt;
    }
}

//! TODO: Should be Sapling address format, SaplingPaymentAddress
// Generate a new Sapling spending key and return its public payment address
libzcash::SaplingPaymentAddress SaplingScriptPubKeyMan::GenerateNewSaplingZKey()
{
    LOCK(wallet->cs_wallet); // mapSaplingZKeyMetadata

    // Try to get the seed
    CKey seedKey;
    if (!wallet->GetKey(hdChain.GetID(), seedKey))
        throw std::runtime_error(std::string(__func__) + ": HD seed not found");

    HDSeed seed(seedKey.GetPrivKey());
    auto m = libzcash::SaplingExtendedSpendingKey::Master(seed);

    // We use a fixed keypath scheme of m/32'/coin_type'/account'
    // Derive m/32'
    auto m_32h = m.Derive(32 | ZIP32_HARDENED_KEY_LIMIT);
    // Derive m/32'/coin_type'
    auto m_32h_cth = m_32h.Derive(119 | ZIP32_HARDENED_KEY_LIMIT);

    // Derive account key at next index, skip keys already known to the wallet
    libzcash::SaplingExtendedSpendingKey xsk;
    do {
        xsk = m_32h_cth.Derive(hdChain.nExternalChainCounter | ZIP32_HARDENED_KEY_LIMIT);
        hdChain.nExternalChainCounter++; // Increment childkey index
    } while (wallet->HaveSaplingSpendingKey(xsk.ToXFVK()));

    // Update the chain model in the database
    if (wallet->fFileBacked && !CWalletDB(wallet->strWalletFile).WriteHDChain(hdChain))
        throw std::runtime_error(std::string(__func__) + ": Writing HD chain model failed");

    // Create new metadata
    int64_t nCreationTime = GetTime();
    auto ivk = xsk.expsk.full_viewing_key().in_viewing_key();
    CKeyMetadata metadata(nCreationTime);
    metadata.key_origin.path.push_back(32 | BIP32_HARDENED_KEY_LIMIT);
    metadata.key_origin.path.push_back(119 | BIP32_HARDENED_KEY_LIMIT);
    metadata.key_origin.path.push_back(hdChain.nExternalChainCounter | BIP32_HARDENED_KEY_LIMIT);
    metadata.hd_seed_id = hdChain.GetID();
    mapSaplingZKeyMetadata[ivk] = metadata;

    if (!AddSaplingZKey(xsk)) {
        throw std::runtime_error(std::string(__func__) + ": AddSaplingZKey failed");
    }
    // return default sapling payment address.
    return xsk.DefaultAddress();
}

void SaplingScriptPubKeyMan::GetConflicts(const CWalletTx& wtx, std::set<uint256>& result) const
{
    AssertLockHeld(wallet->cs_wallet);
    std::pair<TxNullifiers::const_iterator, TxNullifiers::const_iterator> range_o;

    if (wtx.hasSaplingData()) {
        for (const SpendDescription& spend : wtx.sapData->vShieldedSpend) {
            const uint256& nullifier = spend.nullifier;
            if (mapTxSaplingNullifiers.count(nullifier) <= 1) {
                continue;  // No conflict if zero or one spends
            }
            range_o = mapTxSaplingNullifiers.equal_range(nullifier);
            for (TxNullifiers::const_iterator it = range_o.first; it != range_o.second; ++it) {
                result.insert(it->second);
            }
        }
    }
}

KeyAddResult SaplingScriptPubKeyMan::AddViewingKeyToWallet(const libzcash::SaplingExtendedFullViewingKey &extfvk) const {
    if (wallet->HaveSaplingSpendingKey(extfvk)) {
        return SpendingKeyExists;
    } else if (wallet->HaveSaplingFullViewingKey(extfvk.fvk.in_viewing_key())) {
        return KeyAlreadyExists;
    } else if (wallet->AddSaplingFullViewingKey(extfvk)) {
        return KeyAdded;
    } else {
        return KeyNotAdded;
    }
}

KeyAddResult SaplingScriptPubKeyMan::AddSpendingKeyToWallet(
        const Consensus::Params &params,
        const libzcash::SaplingExtendedSpendingKey &sk,
        int64_t nTime)
{
    auto extfvk = sk.ToXFVK();
    auto ivk = extfvk.fvk.in_viewing_key();
    {
        //LogPrint(BCLog::SAPLING, "Importing shielded addr %s...\n", KeyIO::EncodePaymentAddress(sk.DefaultAddress()));
        // Don't throw error in case a key is already there
        if (wallet->HaveSaplingSpendingKey(extfvk)) {
            return KeyAlreadyExists;
        } else {
            if (!wallet-> AddSaplingZKey(sk)) {
                return KeyNotAdded;
            }

            int64_t nTimeToSet;
            // Sapling addresses can't have been used in transactions prior to activation.
            if (params.vUpgrades[Consensus::UPGRADE_V5_DUMMY].nActivationHeight == Consensus::NetworkUpgrade::ALWAYS_ACTIVE) {
                nTimeToSet = nTime;
            } else {
                // TODO: Update epoch before release v5.
                // 154051200 seconds from epoch is Friday, 26 October 2018 00:00:00 GMT - definitely before Sapling activates
                nTimeToSet = std::max((int64_t) 154051200, nTime);
            }

            mapSaplingZKeyMetadata[ivk] = CKeyMetadata(nTimeToSet);
            return KeyAdded;
        }
    }
}

// Add spending key to keystore
bool SaplingScriptPubKeyMan::AddSaplingZKey(
        const libzcash::SaplingExtendedSpendingKey &sk)
{
    AssertLockHeld(wallet->cs_wallet); // mapSaplingZKeyMetadata

    if (!IsEnabled()) {
        return error("%s: Sapling spkm not enabled", __func__ );
    }

    if (!AddSaplingSpendingKey(sk)) {
        return false;
    }

    if (!wallet->fFileBacked) {
        return true;
    }

    if (!wallet->IsCrypted()) {
        auto ivk = sk.expsk.full_viewing_key().in_viewing_key();
        return CWalletDB(wallet->strWalletFile).WriteSaplingZKey(ivk, sk, mapSaplingZKeyMetadata[ivk]);
    }

    return true;
}

bool SaplingScriptPubKeyMan::AddSaplingSpendingKey(
        const libzcash::SaplingExtendedSpendingKey &sk)
{
    {
        LOCK(wallet->cs_KeyStore);
        if (!wallet->IsCrypted()) {
            return wallet->AddSaplingSpendingKey(sk); // keystore
        }

        if (wallet->IsLocked()) {
            return false;
        }

        std::vector<unsigned char> vchCryptedSecret;
        CSecureDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << sk;
        CKeyingMaterial vchSecret(ss.begin(), ss.end());
        auto extfvk = sk.ToXFVK();
        if (!EncryptSecret(wallet->GetEncryptionKey(), vchSecret, extfvk.fvk.GetFingerprint(), vchCryptedSecret)) {
            return false;
        }

        if (!AddCryptedSaplingSpendingKeyDB(extfvk, vchCryptedSecret)) {
            return false;
        }
    }
    return true;
}

// Add payment address -> incoming viewing key map entry
bool SaplingScriptPubKeyMan::AddSaplingIncomingViewingKey(
        const libzcash::SaplingIncomingViewingKey &ivk,
        const libzcash::SaplingPaymentAddress &addr)
{
    AssertLockHeld(wallet->cs_wallet); // mapSaplingZKeyMetadata

    if (!wallet->AddSaplingIncomingViewingKey(ivk, addr)) {
        return false;
    }

    if (!wallet->fFileBacked) {
        return true;
    }

    if (!wallet->IsCrypted()) {
        return CWalletDB(wallet->strWalletFile).WriteSaplingPaymentAddress(addr, ivk);
    }

    return true;
}

bool SaplingScriptPubKeyMan::EncryptSaplingKeys(CKeyingMaterial& vMasterKeyIn)
{
    AssertLockHeld(wallet->cs_wallet); // mapSaplingSpendingKeys

    for (SaplingSpendingKeyMap::value_type& mSaplingSpendingKey : wallet->mapSaplingSpendingKeys) {
        const libzcash::SaplingExtendedSpendingKey &sk = mSaplingSpendingKey.second;
        CSecureDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << sk;
        CKeyingMaterial vchSecret(ss.begin(), ss.end());
        auto extfvk = sk.ToXFVK();
        std::vector<unsigned char> vchCryptedSecret;
        if (!EncryptSecret(vMasterKeyIn, vchSecret, extfvk.fvk.GetFingerprint(), vchCryptedSecret)) {
            return false;
        }
        if (!AddCryptedSaplingSpendingKeyDB(extfvk, vchCryptedSecret)) {
            return false;
        }
    }
    wallet->mapSaplingSpendingKeys.clear();
    return true;
}

bool SaplingScriptPubKeyMan::AddCryptedSaplingSpendingKeyDB(const libzcash::SaplingExtendedFullViewingKey &extfvk,
                                           const std::vector<unsigned char> &vchCryptedSecret)
{
    if (!wallet->AddCryptedSaplingSpendingKey(extfvk, vchCryptedSecret))
        return false;
    if (!wallet->fFileBacked)
        return true;
    {
        LOCK(wallet->cs_wallet);
        if (wallet->pwalletdbEncryption) {
            return wallet->pwalletdbEncryption->WriteCryptedSaplingZKey(extfvk,
                                                                vchCryptedSecret,
                                                                mapSaplingZKeyMetadata[extfvk.fvk.in_viewing_key()]);
        } else {
            return CWalletDB(wallet->strWalletFile).WriteCryptedSaplingZKey(extfvk,
                                                                    vchCryptedSecret,
                                                                    mapSaplingZKeyMetadata[extfvk.fvk.in_viewing_key()]);
        }
    }
    return false;
}

bool SaplingScriptPubKeyMan::HaveSpendingKeyForPaymentAddress(const libzcash::SaplingPaymentAddress &zaddr) const
{
    libzcash::SaplingIncomingViewingKey ivk;
    libzcash::SaplingExtendedFullViewingKey extfvk;

    return wallet->GetSaplingIncomingViewingKey(zaddr, ivk) &&
           wallet->GetSaplingFullViewingKey(ivk, extfvk) &&
           wallet->HaveSaplingSpendingKey(extfvk);
}

///////////////////// Load ////////////////////////////////////////

bool SaplingScriptPubKeyMan::LoadCryptedSaplingZKey(
        const libzcash::SaplingExtendedFullViewingKey &extfvk,
        const std::vector<unsigned char> &vchCryptedSecret)
{
    return wallet->AddCryptedSaplingSpendingKey(extfvk, vchCryptedSecret);
}

bool SaplingScriptPubKeyMan::LoadSaplingZKeyMetadata(const libzcash::SaplingIncomingViewingKey &ivk, const CKeyMetadata &meta)
{
    AssertLockHeld(wallet->cs_wallet); // mapSaplingZKeyMetadata
    mapSaplingZKeyMetadata[ivk] = meta;
    return true;
}

bool SaplingScriptPubKeyMan::LoadSaplingZKey(const libzcash::SaplingExtendedSpendingKey &key)
{
    return wallet->AddSaplingSpendingKey(key);
}

bool SaplingScriptPubKeyMan::LoadSaplingPaymentAddress(
        const libzcash::SaplingPaymentAddress &addr,
        const libzcash::SaplingIncomingViewingKey &ivk)
{
    return wallet->AddSaplingIncomingViewingKey(ivk, addr);
}

///////////////////// Setup ///////////////////////////////////////

bool SaplingScriptPubKeyMan::SetupGeneration(const CKeyID& keyID, bool force)
{
    SetHDSeed(keyID, force);
    return true;
}

void SaplingScriptPubKeyMan::SetHDSeed(const CPubKey& seed, bool force, bool memonly)
{
    SetHDSeed(seed.GetID(), force, memonly);
}

void SaplingScriptPubKeyMan::SetHDSeed(const CKeyID& keyID, bool force, bool memonly)
{
    if (!hdChain.IsNull() && !force)
        throw std::runtime_error(std::string(__func__) + ": sapling trying to set a hd seed on an already created chain");

    LOCK(wallet->cs_wallet);
    // store the keyid (hash160) together with
    // the child index counter in the database
    // as a hdChain object
    CHDChain newHdChain(HDChain::ChainCounterType::Sapling);
    if (!newHdChain.SetSeed(keyID) ) {
        throw std::runtime_error(std::string(__func__) + ": set sapling hd seed failed");
    }

    SetHDChain(newHdChain, memonly);
}

void SaplingScriptPubKeyMan::SetHDChain(CHDChain& chain, bool memonly)
{
    LOCK(wallet->cs_wallet);
    if (chain.chainType != HDChain::ChainCounterType::Sapling)
        throw std::runtime_error(std::string(__func__) + ": trying to store an invalid chain type");

    if (!memonly && !CWalletDB(wallet->strWalletFile).WriteHDChain(chain))
        throw std::runtime_error(std::string(__func__) + ": writing sapling chain failed");

    hdChain = chain;

    // Sanity check
    if (!wallet->HaveKey(hdChain.GetID()))
        throw std::runtime_error(std::string(__func__) + ": Not found sapling seed in wallet");
}
